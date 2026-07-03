#include "tjc_display.h"

#include <stdio.h>
#include <string.h>

#include "pinctrl.h"
#include "soc_osal.h"
#include "uart.h"

#define TJC_UART_BUS                 UART_BUS_2
#define TJC_UART_TX_PIN              GPIO_08
#define TJC_UART_RX_PIN              GPIO_07
#define TJC_UART_BAUDRATE            115200
#define TJC_UART_RX_BUFFER_SIZE      64
#define TJC_CMD_BUFFER_SIZE          128
#define TJC_WAVEFORM_OBJ            "s0"
#define TJC_WAVEFORM_CH             0
#define TJC_BPM_OBJ                 "t3"
#define TJC_PATIENT_NAME_OBJ        "t7"
#define TJC_PATIENT_RECORD_OBJ      "t8"
#define TJC_PATIENT_GENDER_OBJ      "t9"
#define TJC_PATIENT_AGE_OBJ         "t10"
#define TJC_PATIENT_PHONE_OBJ       "t11"
#define TJC_PATIENT_NOTE_OBJ        "t12"
#define TJC_WAVE_MIN                10
#define TJC_WAVE_MAX                245
#define TJC_DISPLAY_MV_MIN          (-180)
#define TJC_DISPLAY_MV_MAX          180
#define TJC_UART_WRITE_TIMEOUT_MS    2

static uint8_t g_tjc_uart_rx_buf[TJC_UART_RX_BUFFER_SIZE];

static uart_buffer_config_t g_tjc_uart_buffer_config = {
    .rx_buffer = g_tjc_uart_rx_buf,
    .rx_buffer_size = TJC_UART_RX_BUFFER_SIZE
};

static void tjc_uart_write_bytes(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    (void)uapi_uart_write(TJC_UART_BUS, data, len, TJC_UART_WRITE_TIMEOUT_MS);
}

static void tjc_send_end(void)
{
    static const uint8_t end_cmd[3] = {0xFF, 0xFF, 0xFF};
    tjc_uart_write_bytes(end_cmd, sizeof(end_cmd));
}

static void tjc_send_text_cmd(const char *cmd)
{
    if (cmd == NULL) {
        return;
    }

    tjc_uart_write_bytes((const uint8_t *)cmd, (uint32_t)strlen(cmd));
    tjc_send_end();
}

static void tjc_copy_safe_text(char *dst, size_t dst_len, const char *src)
{
    size_t out = 0;

    if ((dst == NULL) || (dst_len == 0)) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while ((src[0] != '\0') && (out + 1 < dst_len)) {
        char ch = *src++;
        if ((ch == '"') || (ch == '\\') || (ch == '\r') || (ch == '\n')) {
            ch = ' ';
        }
        dst[out++] = ch;
    }
    dst[out] = '\0';
}

static void tjc_display_send_text_value(const char *obj, const char *value)
{
    char safe_value[80];
    char cmd[TJC_CMD_BUFFER_SIZE];
    int len;

    if (obj == NULL) {
        return;
    }

    tjc_copy_safe_text(safe_value, sizeof(safe_value), value);
    len = snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", obj, safe_value);
    if ((len <= 0) || (len >= (int)sizeof(cmd))) {
        return;
    }
    tjc_send_text_cmd(cmd);
}

static uint8_t tjc_map_display_mv(int16_t display_mv)
{
    int32_t clamped = display_mv;
    int32_t range = TJC_DISPLAY_MV_MAX - TJC_DISPLAY_MV_MIN;
    int32_t wave_range = TJC_WAVE_MAX - TJC_WAVE_MIN;

    if (clamped < TJC_DISPLAY_MV_MIN) {
        clamped = TJC_DISPLAY_MV_MIN;
    } else if (clamped > TJC_DISPLAY_MV_MAX) {
        clamped = TJC_DISPLAY_MV_MAX;
    }

    return (uint8_t)(TJC_WAVE_MIN + ((clamped - TJC_DISPLAY_MV_MIN) * wave_range) / range);
}

errcode_t tjc_display_init(void)
{
    uart_attr_t attr = {
        .baud_rate = TJC_UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = TJC_UART_TX_PIN,
        .rx_pin = TJC_UART_RX_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

#if defined(CONFIG_PINCTRL_SUPPORT_IE)
    uapi_pin_set_ie(TJC_UART_RX_PIN, PIN_IE_1);
#endif
    uapi_pin_set_mode(TJC_UART_TX_PIN, PIN_MODE_2);
    uapi_pin_set_mode(TJC_UART_RX_PIN, PIN_MODE_2);

    (void)uapi_uart_deinit(TJC_UART_BUS);
    errcode_t ret = uapi_uart_init(TJC_UART_BUS, &pin_config, &attr, NULL, &g_tjc_uart_buffer_config);
    if (ret != ERRCODE_SUCC) {
        printf("[TJC] uart init failed, ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    printf("[TJC] UART2 ready: TX=GPIO08, RX=GPIO07, baud=%u\r\n", (unsigned int)TJC_UART_BAUDRATE);
    return ERRCODE_SUCC;
}

void tjc_display_clear_wave(void)
{
    tjc_send_text_cmd("cle " TJC_WAVEFORM_OBJ ".id,255");
}

void tjc_display_send_sample(const ecg_monitor_sample_t *sample)
{
    char cmd[TJC_CMD_BUFFER_SIZE];
    int len;
    uint8_t wave;

    if (sample == NULL) {
        return;
    }

    wave = tjc_map_display_mv(sample->display_mv);
    len = snprintf(cmd, sizeof(cmd), "add %s.id,%u,%u", TJC_WAVEFORM_OBJ, TJC_WAVEFORM_CH, (unsigned int)wave);
    if (len <= 0 || len >= (int)sizeof(cmd)) {
        return;
    }
    tjc_send_text_cmd(cmd);
}

void tjc_display_send_bpm(uint16_t bpm)
{
    char cmd[TJC_CMD_BUFFER_SIZE];
    int len = snprintf(cmd, sizeof(cmd), "%s.txt=\"%u\"", TJC_BPM_OBJ, (unsigned int)bpm);

    if (len <= 0 || len >= (int)sizeof(cmd)) {
        return;
    }
    tjc_send_text_cmd(cmd);
}

void tjc_display_send_patient_info(const char *name, const char *record_no, const char *gender,
    const char *age, const char *phone, const char *note)
{
    tjc_display_send_text_value(TJC_PATIENT_NAME_OBJ, name);
    tjc_display_send_text_value(TJC_PATIENT_RECORD_OBJ, record_no);
    tjc_display_send_text_value(TJC_PATIENT_GENDER_OBJ, gender);
    tjc_display_send_text_value(TJC_PATIENT_AGE_OBJ, age);
    tjc_display_send_text_value(TJC_PATIENT_PHONE_OBJ, phone);
    tjc_display_send_text_value(TJC_PATIENT_NOTE_OBJ, note);
}

void tjc_display_send_test_pattern(void)
{
    static const uint8_t pattern[] = {
        110, 110, 110, 108, 105, 95, 70, 35, 160, 220,
        80, 105, 115, 118, 116, 112, 110, 110, 110, 110
    };

    for (uint32_t i = 0; i < 80U; i++) {
        char cmd[TJC_CMD_BUFFER_SIZE];
        uint8_t val = pattern[i % (sizeof(pattern) / sizeof(pattern[0]))];
        int len = snprintf(cmd, sizeof(cmd), "add %s.id,%u,%u",
            TJC_WAVEFORM_OBJ, TJC_WAVEFORM_CH, (unsigned int)val);
        if (len > 0 && len < (int)sizeof(cmd)) {
            tjc_send_text_cmd(cmd);
        }
        osal_msleep(10);
    }
}
