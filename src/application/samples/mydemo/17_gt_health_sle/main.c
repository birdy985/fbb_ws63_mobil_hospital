#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_init.h"
#include "cmsis_os2.h"
#include "common_def.h"
#include "../13_ad8232_sle/ecg_sle_packet.h"
#include "../13_ad8232_sle/sle_uart_client/sle_uart_client.h"
#include "../16_gt_health/gt_health_sensor.h"
#include "securec.h"
#include "sle_ssap_client.h"
#include "soc_osal.h"
#include "tcxo.h"

#define GT_HEALTH_SLE_TASK_PRIO             osPriorityNormal
#define GT_HEALTH_SLE_TASK_STACK_SIZE       0x2000
#define GT_HEALTH_SLE_READ_TIMEOUT_MS       1000
#define GT_HEALTH_SLE_READ_FAIL_DELAY_MS    200
#define GT_HEALTH_SLE_STATS_INTERVAL_MS     2000
#define GT_HEALTH_SLE_BP_MIN_SYS            60
#define GT_HEALTH_SLE_BP_MAX_SYS            250
#define GT_HEALTH_SLE_BP_MIN_DIA            40
#define GT_HEALTH_SLE_BP_MAX_DIA            180
#define GT_HEALTH_SLE_LOG                   "[gt health sle client]"

static uint8_t g_gt_health_sle_tx_buf[ECG_SLE_PACKET_GT_LEN];
static uint32_t g_gt_health_sle_seq;

void sle_uart_notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(data);
    osal_printk("%s notification status=0x%x\r\n", GT_HEALTH_SLE_LOG, status);
}

void sle_uart_indication_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(data);
    osal_printk("%s indication status=0x%x\r\n", GT_HEALTH_SLE_LOG, status);
}

static bool gt_health_sle_is_valid_bp(uint8_t systolic_bp, uint8_t diastolic_bp)
{
    if (systolic_bp < GT_HEALTH_SLE_BP_MIN_SYS || systolic_bp > GT_HEALTH_SLE_BP_MAX_SYS) {
        return false;
    }
    if (diastolic_bp < GT_HEALTH_SLE_BP_MIN_DIA || diastolic_bp > GT_HEALTH_SLE_BP_MAX_DIA) {
        return false;
    }
    return systolic_bp > diastolic_bp;
}

static void gt_health_sle_fill_sample(const gt_health_data_t *data, ecg_sle_gt_health_sample_t *sample)
{
    if ((data == NULL) || (sample == NULL)) {
        return;
    }

    sample->seq = g_gt_health_sle_seq++;
    sample->timestamp_ms = (uint32_t)uapi_tcxo_get_ms();
    sample->heart_rate = data->heart_rate;
    sample->spo2 = data->spo2;
    sample->heart_rate_valid = data->heart_rate != 0;
    sample->spo2_valid = data->spo2 != 0;
    sample->microcirculation = data->microcirculation;
    sample->microcirculation_valid = data->microcirculation != 0;
    sample->systolic_bp = data->systolic_bp;
    sample->diastolic_bp = data->diastolic_bp;
    sample->bp_valid = gt_health_sle_is_valid_bp(data->systolic_bp, data->diastolic_bp);
}

static errcode_t gt_health_sle_send_sample(const ecg_sle_gt_health_sample_t *sample)
{
    ssapc_write_param_t *param = get_g_sle_uart_send_param();
    uint16_t conn_id = get_g_sle_uart_conn_id();
    errcode_t ret;

    if ((sample == NULL) || (param == NULL) || (param->handle == 0)) {
        return ERRCODE_FAIL;
    }

    ret = ecg_sle_encode_gt_health_sample(sample, g_gt_health_sle_tx_buf, sizeof(g_gt_health_sle_tx_buf));
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    param->data_len = ECG_SLE_PACKET_GT_LEN;
    param->data = g_gt_health_sle_tx_buf;
    sle_uart_client_mark_write_pending();
    ret = ssapc_write_req(0, conn_id, param);
    if (ret != ERRCODE_SUCC) {
        sle_uart_client_clear_write_pending();
    }
    return ret;
}

static void *gt_health_sle_task(const char *arg)
{
    uint32_t sent_count = 0;
    uint32_t drop_count = 0;
    uint32_t fail_count = 0;
    uint32_t backpressure_count = 0;
    uint32_t last_stats_ms;
    errcode_t ret;

    unused(arg);
    sle_uart_client_init(sle_uart_notification_cb, sle_uart_indication_cb);

    ret = gt_health_sensor_init();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s GT health init failed ret=0x%x\r\n", GT_HEALTH_SLE_LOG, ret);
        return NULL;
    }

    last_stats_ms = (uint32_t)uapi_tcxo_get_ms();
    osal_printk("%s ready packet_len=%u\r\n", GT_HEALTH_SLE_LOG, (unsigned int)ECG_SLE_PACKET_GT_LEN);

    while (1) {
        gt_health_data_t data = {0};
        ecg_sle_gt_health_sample_t sample = {0};
        uint32_t now_ms;

        ret = gt_health_sensor_read(&data, GT_HEALTH_SLE_READ_TIMEOUT_MS);
        if (ret == ERRCODE_SUCC) {
            gt_health_sle_fill_sample(&data, &sample);
            osal_printk("%s raw hr=%u spo2=%u micro=%u sys=%u dia=%u\r\n",
                GT_HEALTH_SLE_LOG,
                (unsigned int)data.heart_rate,
                (unsigned int)data.spo2,
                (unsigned int)data.microcirculation,
                (unsigned int)data.systolic_bp,
                (unsigned int)data.diastolic_bp);
            if (!sle_uart_client_can_send()) {
                backpressure_count++;
            } else {
                ret = gt_health_sle_send_sample(&sample);
                if (ret == ERRCODE_SUCC) {
                    sent_count++;
                    osal_printk("%s tx seq=%lu hr=%u spo2=%u micro=%u sys=%u dia=%u valid=0x%x\r\n",
                        GT_HEALTH_SLE_LOG,
                        (unsigned long)sample.seq,
                        (unsigned int)sample.heart_rate,
                        (unsigned int)sample.spo2,
                        (unsigned int)sample.microcirculation,
                        (unsigned int)sample.systolic_bp,
                        (unsigned int)sample.diastolic_bp,
                        (unsigned int)((sample.heart_rate_valid ? 0x01U : 0U) |
                            (sample.spo2_valid ? 0x02U : 0U) |
                            (sample.microcirculation_valid ? 0x04U : 0U) |
                            (sample.bp_valid ? 0x08U : 0U)));
                } else if (get_g_sle_uart_send_param()->handle == 0) {
                    drop_count++;
                } else {
                    fail_count++;
                }
            }
        } else {
            fail_count++;
            osal_msleep(GT_HEALTH_SLE_READ_FAIL_DELAY_MS);
        }

        now_ms = (uint32_t)uapi_tcxo_get_ms();
        if ((now_ms - last_stats_ms) >= GT_HEALTH_SLE_STATS_INTERVAL_MS) {
            osal_printk("%s stats sent=%lu drop=%lu backpressure=%lu fail=%lu handle=0x%x\r\n",
                GT_HEALTH_SLE_LOG,
                (unsigned long)sent_count,
                (unsigned long)drop_count,
                (unsigned long)backpressure_count,
                (unsigned long)fail_count,
                (unsigned int)get_g_sle_uart_send_param()->handle);
            last_stats_ms = now_ms;
        }
    }

    return NULL;
}

static void gt_health_sle_entry(void)
{
    osThreadAttr_t attr = {0};

    attr.name = "GtHealthSleClient";
    attr.stack_size = GT_HEALTH_SLE_TASK_STACK_SIZE;
    attr.priority = GT_HEALTH_SLE_TASK_PRIO;
    if (osThreadNew((osThreadFunc_t)gt_health_sle_task, NULL, &attr) == NULL) {
        osal_printk("%s create task failed\r\n", GT_HEALTH_SLE_LOG);
    }
}

app_run(gt_health_sle_entry);
