#include "gt_health_sensor.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pinctrl.h"
#include "soc_osal.h"
#include "uart.h"
#include "watchdog.h"

#define GT_HEALTH_UART_BUS              UART_BUS_1
/* HH-D02 header exposes these chip pins as UART1_TX/UART1_RX, not as header pin 15/16. */
#define GT_HEALTH_UART_TX_PIN           GPIO_15
#define GT_HEALTH_UART_RX_PIN           GPIO_16
#define GT_HEALTH_UART_PIN_MODE         PIN_MODE_1
#define GT_HEALTH_UART_BAUDRATE         38400
#define GT_HEALTH_UART_RX_BUFFER_SIZE   128
#define GT_HEALTH_FRAME_SIZE            88
#define GT_HEALTH_FRAME_HEAD            0xFF
#define GT_HEALTH_START_CMD             0x8A
#define GT_HEALTH_START_DELAY_MS        100
#define GT_HEALTH_UART_POLL_MS          10

#define GT_HEALTH_HEART_RATE_INDEX      65
#define GT_HEALTH_SPO2_INDEX            66
#define GT_HEALTH_MICROCIRC_INDEX       67
#define GT_HEALTH_SYSTOLIC_BP_INDEX     71
#define GT_HEALTH_DIASTOLIC_BP_INDEX    72

static uint8_t g_gt_health_uart_rx_buf[GT_HEALTH_UART_RX_BUFFER_SIZE];
static uart_buffer_config_t g_gt_health_uart_buffer_config = {
    .rx_buffer = g_gt_health_uart_rx_buf,
    .rx_buffer_size = GT_HEALTH_UART_RX_BUFFER_SIZE
};

static int8_t gt_health_decode_wave(uint8_t value)
{
    return (int8_t)((int16_t)(value & 0x7FU) - (int16_t)(value & 0x80U));
}

static errcode_t gt_health_uart_read_byte(uint8_t *value, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;

    if (value == NULL) {
        return ERRCODE_INVALID_PARAM;
    }

    while (timeout_ms == 0 || elapsed_ms < timeout_ms) {
        (void)uapi_watchdog_kick();
        if (uapi_uart_read(GT_HEALTH_UART_BUS, value, 1, 0) == 1) {
            return ERRCODE_SUCC;
        }
        osal_msleep(GT_HEALTH_UART_POLL_MS);
        elapsed_ms += GT_HEALTH_UART_POLL_MS;
    }

    return ERRCODE_FAIL;
}

static void gt_health_parse_frame(const uint8_t *frame, gt_health_data_t *data)
{
    for (uint32_t i = 0; i < GT_HEALTH_WAVE_SAMPLE_COUNT; i++) {
        data->wave[i] = gt_health_decode_wave(frame[i + 1]);
    }

    data->heart_rate = frame[GT_HEALTH_HEART_RATE_INDEX];
    data->spo2 = frame[GT_HEALTH_SPO2_INDEX];
    data->microcirculation = frame[GT_HEALTH_MICROCIRC_INDEX];
    data->systolic_bp = frame[GT_HEALTH_SYSTOLIC_BP_INDEX];
    data->diastolic_bp = frame[GT_HEALTH_DIASTOLIC_BP_INDEX];
}

errcode_t gt_health_sensor_init(void)
{
    uart_attr_t attr = {
        .baud_rate = GT_HEALTH_UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = GT_HEALTH_UART_TX_PIN,
        .rx_pin = GT_HEALTH_UART_RX_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };
    uint8_t start_cmd = GT_HEALTH_START_CMD;

#if defined(CONFIG_PINCTRL_SUPPORT_IE)
    uapi_pin_set_ie(GT_HEALTH_UART_RX_PIN, PIN_IE_1);
#endif
    uapi_pin_set_mode(GT_HEALTH_UART_TX_PIN, GT_HEALTH_UART_PIN_MODE);
    uapi_pin_set_mode(GT_HEALTH_UART_RX_PIN, GT_HEALTH_UART_PIN_MODE);

    (void)uapi_uart_deinit(GT_HEALTH_UART_BUS);
    errcode_t ret = uapi_uart_init(GT_HEALTH_UART_BUS, &pin_config, &attr, NULL, &g_gt_health_uart_buffer_config);
    if (ret != ERRCODE_SUCC) {
        printf("[GT_HEALTH] uart init failed, ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    (void)uapi_uart_write(GT_HEALTH_UART_BUS, &start_cmd, sizeof(start_cmd), 0);
    osal_msleep(GT_HEALTH_START_DELAY_MS);
    printf("[GT_HEALTH] HH-D02 UART1 ready: TX=UART1_TX(GPIO15), RX=UART1_RX(GPIO16), baud=%u\r\n",
        (unsigned int)GT_HEALTH_UART_BAUDRATE);
    return ERRCODE_SUCC;
}

errcode_t gt_health_sensor_read(gt_health_data_t *data, uint32_t timeout_ms)
{
    uint8_t frame[GT_HEALTH_FRAME_SIZE] = {0};
    uint8_t value = 0;

    if (data == NULL) {
        return ERRCODE_INVALID_PARAM;
    }

    do {
        if (gt_health_uart_read_byte(&value, timeout_ms) != ERRCODE_SUCC) {
            return ERRCODE_FAIL;
        }
    } while (value != GT_HEALTH_FRAME_HEAD);

    frame[0] = value;
    for (uint32_t i = 1; i < GT_HEALTH_FRAME_SIZE; i++) {
        if (gt_health_uart_read_byte(&frame[i], timeout_ms) != ERRCODE_SUCC) {
            return ERRCODE_FAIL;
        }
    }

    gt_health_parse_frame(frame, data);
    return ERRCODE_SUCC;
}
