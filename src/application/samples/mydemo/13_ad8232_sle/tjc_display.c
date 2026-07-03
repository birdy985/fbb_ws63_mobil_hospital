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
#define TJC_CMD_BUFFER_SIZE          384
#define TJC_WAVEFORM_OBJ            "s0"
#define TJC_WAVEFORM_CH             0
#define TJC_ECG_HR_OBJ              "t0"
#define TJC_ECG_VALUE_OBJ           "t1"
#define TJC_SPO2_OBJ                "t2"
#define TJC_BPM_OBJ                 "t3"
#define TJC_SYS_BP_OBJ              "t4"
#define TJC_DIA_BP_OBJ              "t5"
#define TJC_MICROCIRC_OBJ           "t6"
#define TJC_PATIENT_NAME_OBJ        "t7"
#define TJC_CASE_NO_OBJ             "t8"
#define TJC_GENDER_OBJ              "t9"
#define TJC_AGE_OBJ                 "t10"
#define TJC_PHONE_OBJ               "t11"
#define TJC_REMARK_OBJ              "t12"
#define TJC_WAVE_MIN                10
#define TJC_WAVE_MAX                245
#define TJC_DISPLAY_MV_MIN          (-180)
#define TJC_DISPLAY_MV_MAX          180
#define TJC_UART_WRITE_TIMEOUT_MS    100
#define TJC_PATIENT_CMD_DELAY_MS     30
#define TJC_PATIENT_SEND_REPEAT      2
#define TJC_INVALID_U16             0xFFFF
#define TJC_INVALID_I16             ((int16_t)0x7FFF)
#define TJC_INVALID_I32             ((int32_t)0x7FFFFFFF)

static uint8_t g_tjc_uart_rx_buf[TJC_UART_RX_BUFFER_SIZE];
static osal_mutex g_tjc_uart_lock;
static uint8_t g_tjc_uart_lock_ready;
static volatile uint8_t g_tjc_patient_update_busy;

typedef struct {
    uint16_t ecg_hr;
    int16_t ecg_value;
    int32_t spo2_x10;
    uint16_t spo2_hr;
    uint16_t systolic_bp;
    uint16_t diastolic_bp;
    uint16_t microcirculation;
    char name[TJC_DISPLAY_TEXT_MAX_LEN];
    char case_no[TJC_DISPLAY_TEXT_MAX_LEN];
    char gender[TJC_DISPLAY_TEXT_MAX_LEN];
    char age[TJC_DISPLAY_TEXT_MAX_LEN];
    char phone[TJC_DISPLAY_TEXT_MAX_LEN];
    char remark[TJC_DISPLAY_TEXT_MAX_LEN];
} tjc_display_cache_t;

static tjc_display_cache_t g_tjc_cache = {
    .ecg_hr = TJC_INVALID_U16,
    .ecg_value = TJC_INVALID_I16,
    .spo2_x10 = TJC_INVALID_I32,
    .spo2_hr = TJC_INVALID_U16,
    .systolic_bp = TJC_INVALID_U16,
    .diastolic_bp = TJC_INVALID_U16,
    .microcirculation = TJC_INVALID_U16,
};

static uart_buffer_config_t g_tjc_uart_buffer_config = {
    .rx_buffer = g_tjc_uart_rx_buf,
    .rx_buffer_size = TJC_UART_RX_BUFFER_SIZE
};

static void tjc_uart_write_bytes(const uint8_t *data, uint32_t len)
{
    int32_t ret;

    if (data == NULL || len == 0) {
        return;
    }

    if (g_tjc_uart_lock_ready != 0) {
        (void)osal_mutex_lock(&g_tjc_uart_lock);
    }
    ret = uapi_uart_write(TJC_UART_BUS, data, len, TJC_UART_WRITE_TIMEOUT_MS);
    if (ret != (int32_t)len) {
        osal_printk("[TJC] uart write short ret=%ld len=%lu\r\n", (long)ret, (unsigned long)len);
    }
    if (g_tjc_uart_lock_ready != 0) {
        osal_mutex_unlock(&g_tjc_uart_lock);
    }
}

static void tjc_send_text_cmd(const char *cmd)
{
    uint8_t frame[TJC_CMD_BUFFER_SIZE + 3];
    uint32_t len;

    if (cmd == NULL) {
        return;
    }

    len = (uint32_t)strlen(cmd);
    if (len + 3U > sizeof(frame)) {
        return;
    }
    (void)memcpy(frame, cmd, len);
    frame[len++] = 0xFF;
    frame[len++] = 0xFF;
    frame[len++] = 0xFF;
    tjc_uart_write_bytes(frame, len);
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
    if (g_tjc_uart_lock_ready == 0) {
        if (osal_mutex_init(&g_tjc_uart_lock) == OSAL_SUCCESS) {
            g_tjc_uart_lock_ready = 1;
        }
    }

    uapi_pin_set_mode(TJC_UART_TX_PIN, PIN_MODE_2);
    uapi_pin_set_mode(TJC_UART_RX_PIN, PIN_MODE_2);

    (void)uapi_uart_deinit(TJC_UART_BUS);
    errcode_t ret = uapi_uart_init(TJC_UART_BUS, &pin_config, &attr, NULL, &g_tjc_uart_buffer_config);
    if (ret != ERRCODE_SUCC) {
        printf("[TJC] uart init failed, ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    g_tjc_cache.ecg_hr = TJC_INVALID_U16;
    g_tjc_cache.ecg_value = TJC_INVALID_I16;
    g_tjc_cache.spo2_x10 = TJC_INVALID_I32;
    g_tjc_cache.spo2_hr = TJC_INVALID_U16;
    g_tjc_cache.systolic_bp = TJC_INVALID_U16;
    g_tjc_cache.diastolic_bp = TJC_INVALID_U16;
    g_tjc_cache.microcirculation = TJC_INVALID_U16;
    g_tjc_cache.name[0] = '\0';
    g_tjc_cache.case_no[0] = '\0';
    g_tjc_cache.gender[0] = '\0';
    g_tjc_cache.age[0] = '\0';
    g_tjc_cache.phone[0] = '\0';
    g_tjc_cache.remark[0] = '\0';

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

    if ((sample == NULL) || (g_tjc_patient_update_busy != 0)) {
        return;
    }

    wave = tjc_map_display_mv(sample->display_mv);
    len = snprintf(cmd, sizeof(cmd), "add %s.id,%u,%u", TJC_WAVEFORM_OBJ, TJC_WAVEFORM_CH, (unsigned int)wave);
    if (len <= 0 || len >= (int)sizeof(cmd)) {
        return;
    }
    tjc_send_text_cmd(cmd);
}

static void tjc_send_text_value(const char *obj, const char *text)
{
    char cmd[TJC_CMD_BUFFER_SIZE];
    int len;

    if ((obj == NULL) || (text == NULL)) {
        return;
    }

    len = snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", obj, text);
    if (len <= 0 || len >= (int)sizeof(cmd)) {
        return;
    }
    osal_printk("[TJC_TX] %s FF FF FF\r\n", cmd);
    tjc_send_text_cmd(cmd);
}

static void tjc_send_text_u16(const char *obj, uint16_t value)
{
    char text[16];
    int len = snprintf(text, sizeof(text), "%u", (unsigned int)value);

    if (len <= 0 || len >= (int)sizeof(text)) {
        return;
    }
    tjc_send_text_value(obj, text);
}

static void tjc_send_text_i16(const char *obj, int16_t value)
{
    char text[16];
    int len = snprintf(text, sizeof(text), "%d", (int)value);

    if (len <= 0 || len >= (int)sizeof(text)) {
        return;
    }
    tjc_send_text_value(obj, text);
}

static void tjc_send_text_spo2_x10(const char *obj, int32_t spo2_x10)
{
    char text[16];
    int len = snprintf(text, sizeof(text), "%ld.%ld",
        (long)(spo2_x10 / 10), (long)(spo2_x10 >= 0 ? spo2_x10 % 10 : -(spo2_x10 % 10)));

    if (len <= 0 || len >= (int)sizeof(text)) {
        return;
    }
    tjc_send_text_value(obj, text);
}

static void tjc_copy_cache_text(char *dst, uint32_t dst_len, const char *src)
{
    uint32_t out = 0;

    if ((dst == NULL) || (dst_len == 0)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while ((*src != '\0') && (out + 1U < dst_len)) {
        char ch = *src++;
        if ((ch == '"') || (ch == '\\') || (ch == '\r') || (ch == '\n')) {
            ch = ' ';
        }
        dst[out++] = ch;
    }
    dst[out] = '\0';
}

static void tjc_update_cached_text(char *cache, uint32_t cache_len, const char *obj, const char *value)
{
    const char *text = (value == NULL) ? "" : value;

    if ((cache == NULL) || (cache_len == 0)) {
        return;
    }

    if (strncmp(cache, text, cache_len) == 0) {
        return;
    }

    tjc_copy_cache_text(cache, cache_len, text);
    tjc_send_text_value(obj, cache);
}

static void tjc_force_cached_text(char *cache, uint32_t cache_len, const char *obj, const char *value)
{
    const char *text = (value == NULL) ? "" : value;

    if ((cache == NULL) || (cache_len == 0)) {
        return;
    }

    tjc_copy_cache_text(cache, cache_len, text);
    tjc_send_text_value(obj, cache);
}

static void tjc_send_patient_fields_once(const char *name, const char *record_no, const char *gender,
    const char *age, const char *phone, const char *note)
{
    tjc_force_cached_text(g_tjc_cache.name, sizeof(g_tjc_cache.name), TJC_PATIENT_NAME_OBJ, name);
    osal_msleep(TJC_PATIENT_CMD_DELAY_MS);
    tjc_force_cached_text(g_tjc_cache.case_no, sizeof(g_tjc_cache.case_no), TJC_CASE_NO_OBJ, record_no);
    osal_msleep(TJC_PATIENT_CMD_DELAY_MS);
    tjc_force_cached_text(g_tjc_cache.gender, sizeof(g_tjc_cache.gender), TJC_GENDER_OBJ, gender);
    osal_msleep(TJC_PATIENT_CMD_DELAY_MS);
    tjc_force_cached_text(g_tjc_cache.age, sizeof(g_tjc_cache.age), TJC_AGE_OBJ, age);
    osal_msleep(TJC_PATIENT_CMD_DELAY_MS);
    tjc_force_cached_text(g_tjc_cache.phone, sizeof(g_tjc_cache.phone), TJC_PHONE_OBJ, phone);
    osal_msleep(TJC_PATIENT_CMD_DELAY_MS);
    tjc_force_cached_text(g_tjc_cache.remark, sizeof(g_tjc_cache.remark), TJC_REMARK_OBJ, note);
    osal_msleep(TJC_PATIENT_CMD_DELAY_MS);
}

void tjc_display_send_bpm(uint16_t bpm)
{
    if (g_tjc_cache.spo2_hr == bpm) {
        return;
    }
    g_tjc_cache.spo2_hr = bpm;
    tjc_send_text_u16(TJC_BPM_OBJ, bpm);
}

void tjc_display_set_patient_info(const tjc_display_patient_info_t *patient)
{
    char age_text[8];
    int len;

    if (patient == NULL) {
        return;
    }

    tjc_update_cached_text(g_tjc_cache.name, sizeof(g_tjc_cache.name), TJC_PATIENT_NAME_OBJ, patient->name);
    tjc_update_cached_text(g_tjc_cache.case_no, sizeof(g_tjc_cache.case_no), TJC_CASE_NO_OBJ, patient->case_no);
    tjc_update_cached_text(g_tjc_cache.gender, sizeof(g_tjc_cache.gender), TJC_GENDER_OBJ, patient->gender);
    if (patient->age == 0) {
        tjc_update_cached_text(g_tjc_cache.age, sizeof(g_tjc_cache.age), TJC_AGE_OBJ, "--");
    } else {
        len = snprintf(age_text, sizeof(age_text), "%u", (unsigned int)patient->age);
        if (len <= 0 || len >= (int)sizeof(age_text)) {
            age_text[0] = '\0';
        }
        tjc_update_cached_text(g_tjc_cache.age, sizeof(g_tjc_cache.age), TJC_AGE_OBJ, age_text);
    }
    tjc_update_cached_text(g_tjc_cache.phone, sizeof(g_tjc_cache.phone), TJC_PHONE_OBJ, patient->phone);
    tjc_update_cached_text(g_tjc_cache.remark, sizeof(g_tjc_cache.remark), TJC_REMARK_OBJ, patient->remark);
}

static uint8_t tjc_parse_age_text(const char *age)
{
    uint32_t value = 0;

    if (age == NULL) {
        return 0;
    }
    while ((*age >= '0') && (*age <= '9')) {
        value = (value * 10U) + (uint32_t)(*age - '0');
        if (value > 130U) {
            return 0;
        }
        age++;
    }
    return (uint8_t)value;
}

void tjc_display_send_patient_info(const char *name, const char *record_no, const char *gender,
    const char *age, const char *phone, const char *note)
{
    uint8_t parsed_age = tjc_parse_age_text(age);
    const char *age_text = age;

    if (((age == NULL) || (age[0] == '\0')) && (parsed_age == 0)) {
        age_text = "--";
    }

    g_tjc_patient_update_busy = 1;
    for (uint8_t i = 0; i < TJC_PATIENT_SEND_REPEAT; i++) {
        tjc_send_patient_fields_once(name, record_no, gender, age_text, phone, note);
    }
    g_tjc_patient_update_busy = 0;
}

void tjc_display_update_vitals(const tjc_display_vitals_t *vitals)
{
    if (vitals == NULL) {
        return;
    }

    if ((vitals->ecg_valid != 0) && (g_tjc_cache.ecg_hr != vitals->ecg.bpm)) {
        g_tjc_cache.ecg_hr = vitals->ecg.bpm;
        tjc_send_text_u16(TJC_ECG_HR_OBJ, vitals->ecg.bpm);
    }
    if ((vitals->ecg_valid != 0) && (g_tjc_cache.ecg_value != vitals->ecg.display_mv)) {
        g_tjc_cache.ecg_value = vitals->ecg.display_mv;
        tjc_send_text_i16(TJC_ECG_VALUE_OBJ, vitals->ecg.display_mv);
    }
    if (vitals->spo2.spo2_valid && g_tjc_cache.spo2_x10 != vitals->spo2.spo2_x10) {
        g_tjc_cache.spo2_x10 = vitals->spo2.spo2_x10;
        tjc_send_text_spo2_x10(TJC_SPO2_OBJ, vitals->spo2.spo2_x10);
    }
    if (vitals->spo2.hr_valid) {
        tjc_display_send_bpm(vitals->spo2.hr_bpm);
    }
    if (vitals->bp_valid != 0) {
        if (g_tjc_cache.systolic_bp != vitals->systolic_bp) {
            g_tjc_cache.systolic_bp = vitals->systolic_bp;
            tjc_send_text_u16(TJC_SYS_BP_OBJ, vitals->systolic_bp);
        }
        if (g_tjc_cache.diastolic_bp != vitals->diastolic_bp) {
            g_tjc_cache.diastolic_bp = vitals->diastolic_bp;
            tjc_send_text_u16(TJC_DIA_BP_OBJ, vitals->diastolic_bp);
        }
    }
    if ((vitals->microcirculation_valid != 0) && (g_tjc_cache.microcirculation != vitals->microcirculation)) {
        g_tjc_cache.microcirculation = vitals->microcirculation;
        tjc_send_text_u16(TJC_MICROCIRC_OBJ, vitals->microcirculation);
    }
}

void tjc_display_update_gt_health(const ecg_sle_gt_health_sample_t *sample)
{
    if (sample == NULL) {
        return;
    }

    if (sample->spo2_valid) {
        g_tjc_cache.spo2_x10 = (int32_t)sample->spo2 * 10;
        tjc_send_text_u16(TJC_SPO2_OBJ, sample->spo2);
    }
    if (sample->heart_rate_valid) {
        g_tjc_cache.spo2_hr = sample->heart_rate;
        tjc_send_text_u16(TJC_BPM_OBJ, sample->heart_rate);
    }
    if (sample->bp_valid) {
        g_tjc_cache.systolic_bp = sample->systolic_bp;
        g_tjc_cache.diastolic_bp = sample->diastolic_bp;
        tjc_send_text_u16(TJC_SYS_BP_OBJ, sample->systolic_bp);
        tjc_send_text_u16(TJC_DIA_BP_OBJ, sample->diastolic_bp);
    }
    if (sample->microcirculation_valid) {
        g_tjc_cache.microcirculation = sample->microcirculation;
        tjc_send_text_u16(TJC_MICROCIRC_OBJ, sample->microcirculation);
    }
}

void tjc_display_update_from_sle_sample(const ecg_sle_vital_sample_t *sample)
{
    tjc_display_vitals_t vitals = {0};

    if (sample == NULL) {
        return;
    }

    vitals.ecg = sample->ecg;
    vitals.ecg_valid = 1;
    vitals.spo2 = sample->spo2;
    tjc_display_update_vitals(&vitals);
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
