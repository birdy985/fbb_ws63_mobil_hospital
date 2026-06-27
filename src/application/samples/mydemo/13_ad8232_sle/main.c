#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../9_ad8232_adc/ad8232_bsp.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include "common_def.h"
#include "../9_ad8232_adc/ecg_processor.h"
#include "ecg_sle_packet.h"
#include "securec.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"
#include "soc_osal.h"
#include "tcxo.h"

#if defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_SERVER)
#include "sle_uart_server/sle_uart_server.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_CLIENT)
#include "sle_uart_client/sle_uart_client.h"
#endif

#define ECG_SLE_TASK_PRIO                 osPriorityNormal
#define ECG_SLE_TASK_STACK_SIZE           0x1800
#define ECG_SLE_SAMPLE_MS                 10
#define ECG_SLE_STATS_INTERVAL_MS         2000
#define ECG_SLE_QUEUE_LEN                 16
#define ECG_SLE_QUEUE_DELAY               0xFFFFFFFF
#define ECG_SLE_ADV_HANDLE_DEFAULT        1
#define ECG_SLE_SERVER_LOG                "[ecg sle server]"
#define ECG_SLE_CLIENT_LOG                "[ecg sle client]"

typedef struct {
    uint16_t len;
    uint8_t data[ECG_SLE_PACKET_LEN];
} ecg_sle_rx_msg_t;

#if defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_SERVER)
static unsigned long g_ecg_sle_msgqueue_id;
static uint8_t g_ecg_sle_header_printed;
static uint8_t g_ecg_sle_has_seq;
static uint32_t g_ecg_sle_expected_seq;

static void ecg_sle_server_print_header(void)
{
    if (g_ecg_sle_header_printed == 0) {
        osal_printk("ECG_SLE,seq,t_ms,raw_mv,base_mv,ecg_mv,filt_mv,disp_mv,r,rr_ms,bpm,q\r\n");
        g_ecg_sle_header_printed = 1;
    }
}

static void ssaps_server_read_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb_para,
    errcode_t status)
{
    if (read_cb_para == NULL) {
        osal_printk("%s read request null status:%x\r\n", ECG_SLE_SERVER_LOG, status);
        return;
    }
    osal_printk("%s read request server_id:%x conn_id:%x handle:%x status:%x\r\n",
        ECG_SLE_SERVER_LOG, server_id, conn_id, read_cb_para->handle, status);
}

static void ecg_sle_server_restart_msg(uint8_t *buffer, uint16_t buffer_size)
{
    ecg_sle_rx_msg_t msg = {0};

    unused(buffer);
    unused(buffer_size);
    msg.len = 0;
    if (osal_msg_queue_write_copy(g_ecg_sle_msgqueue_id, &msg, sizeof(msg), 0) != OSAL_SUCCESS) {
        osal_printk("%s restart queue full\r\n", ECG_SLE_SERVER_LOG);
    }
}

static void ecg_sle_server_write_msgqueue(const uint8_t *buffer, uint16_t buffer_size)
{
    ecg_sle_rx_msg_t msg = {0};

    if ((buffer == NULL) || (buffer_size > ECG_SLE_PACKET_LEN)) {
        osal_printk("%s drop write len=%u\r\n", ECG_SLE_SERVER_LOG, (unsigned int)buffer_size);
        return;
    }
    msg.len = buffer_size;
    if (memcpy_s(msg.data, sizeof(msg.data), buffer, buffer_size) != EOK) {
        osal_printk("%s copy write payload failed\r\n", ECG_SLE_SERVER_LOG);
        return;
    }
    if (osal_msg_queue_write_copy(g_ecg_sle_msgqueue_id, &msg, sizeof(msg), 0) != OSAL_SUCCESS) {
        osal_printk("%s rx queue full, drop seq packet\r\n", ECG_SLE_SERVER_LOG);
    }
}

static void ssaps_server_write_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para,
    errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    if ((status != ERRCODE_SLE_SUCCESS) || (write_cb_para == NULL)) {
        osal_printk("%s write callback invalid status:%x\r\n", ECG_SLE_SERVER_LOG, status);
        return;
    }
    ecg_sle_server_write_msgqueue(write_cb_para->value, write_cb_para->length);
}

static void ecg_sle_server_handle_msg(const ecg_sle_rx_msg_t *msg)
{
    ecg_monitor_sample_t sample = {0};
    ecg_sle_packet_status_t status;

    if (msg->len == 0) {
        errcode_t ret = sle_start_announce(ECG_SLE_ADV_HANDLE_DEFAULT);
        if (ret != ERRCODE_SLE_SUCCESS) {
            osal_printk("%s restart advertise failed ret=0x%x\r\n", ECG_SLE_SERVER_LOG, ret);
        } else {
            osal_printk("%s restart advertise\r\n", ECG_SLE_SERVER_LOG);
        }
        return;
    }

    status = ecg_sle_decode_sample(msg->data, msg->len, &sample);
    if (status != ECG_SLE_PACKET_OK) {
        osal_printk("%s bad packet status=%s len=%u\r\n", ECG_SLE_SERVER_LOG,
            ecg_sle_packet_status_to_string(status), (unsigned int)msg->len);
        return;
    }

    if ((g_ecg_sle_has_seq != 0) && (sample.seq != g_ecg_sle_expected_seq)) {
        osal_printk("%s seq gap expected=%lu received=%lu\r\n", ECG_SLE_SERVER_LOG,
            (unsigned long)g_ecg_sle_expected_seq, (unsigned long)sample.seq);
    }
    g_ecg_sle_has_seq = 1;
    g_ecg_sle_expected_seq = sample.seq + 1;

    ecg_sle_server_print_header();
    osal_printk("ECG_SLE,%lu,%lu,%u,%d,%d,%d,%d,%u,%u,%u,%u\r\n",
        (unsigned long)sample.seq,
        (unsigned long)sample.timestamp_ms,
        (unsigned int)sample.voltage_mv,
        (int)sample.baseline_mv,
        (int)sample.ecg_mv,
        (int)sample.filtered_mv,
        (int)sample.display_mv,
        (unsigned int)sample.r_peak,
        (unsigned int)sample.rr_interval_ms,
        (unsigned int)sample.bpm,
        (unsigned int)sample.quality);
}

static void *ecg_sle_server_task(const char *arg)
{
    unused(arg);
    ecg_sle_rx_msg_t msg = {0};
    uint32_t msg_size = sizeof(msg);
    errcode_t ret;

    if (osal_msg_queue_create("ecg_sle_rx", ECG_SLE_QUEUE_LEN, &g_ecg_sle_msgqueue_id, 0, sizeof(msg)) !=
        OSAL_SUCCESS) {
        osal_printk("%s create rx queue failed\r\n", ECG_SLE_SERVER_LOG);
        return NULL;
    }

    sle_uart_server_register_msg(ecg_sle_server_restart_msg);
    ret = sle_uart_server_init(ssaps_server_read_request_cbk, ssaps_server_write_request_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s init failed ret=0x%x\r\n", ECG_SLE_SERVER_LOG, ret);
        osal_msg_queue_delete(g_ecg_sle_msgqueue_id);
        return NULL;
    }
    osal_printk("%s ready\r\n", ECG_SLE_SERVER_LOG);

    while (1) {
        msg_size = sizeof(msg);
        if (osal_msg_queue_read_copy(g_ecg_sle_msgqueue_id, &msg, &msg_size, ECG_SLE_QUEUE_DELAY) == OSAL_SUCCESS) {
            ecg_sle_server_handle_msg(&msg);
        }
    }

    return NULL;
}
#elif defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_CLIENT)
static uint8_t g_ecg_sle_tx_buf[ECG_SLE_PACKET_LEN];

void sle_uart_notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(data);
    osal_printk("%s notification status=0x%x\r\n", ECG_SLE_CLIENT_LOG, status);
}

void sle_uart_indication_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(data);
    osal_printk("%s indication status=0x%x\r\n", ECG_SLE_CLIENT_LOG, status);
}

static errcode_t ecg_sle_client_send_sample(const ecg_monitor_sample_t *sample)
{
    ssapc_write_param_t *param = get_g_sle_uart_send_param();
    uint16_t conn_id = get_g_sle_uart_conn_id();
    errcode_t ret;

    if ((param == NULL) || (param->handle == 0)) {
        return ERRCODE_FAIL;
    }
    ret = ecg_sle_encode_sample(sample, g_ecg_sle_tx_buf, sizeof(g_ecg_sle_tx_buf));
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    param->data_len = ECG_SLE_PACKET_LEN;
    param->data = g_ecg_sle_tx_buf;
    return ssapc_write_req(0, conn_id, param);
}

static void *ecg_sle_client_task(const char *arg)
{
    unused(arg);
    errcode_t ret;
    uint32_t seq = 0;
    uint32_t sent_count = 0;
    uint32_t drop_count = 0;
    uint32_t fail_count = 0;
    uint32_t last_stats_ms;

    sle_uart_client_init(sle_uart_notification_cb, sle_uart_indication_cb);

    ret = ad8232_init();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s AD8232 init failed ret=0x%x\r\n", ECG_SLE_CLIENT_LOG, ret);
        return NULL;
    }
    ecg_processor_init();
    last_stats_ms = (uint32_t)uapi_tcxo_get_ms();
    osal_printk("%s ready sample_ms=%u packet_len=%u\r\n", ECG_SLE_CLIENT_LOG,
        (unsigned int)ECG_SLE_SAMPLE_MS, (unsigned int)ECG_SLE_PACKET_LEN);

    while (1) {
        uint16_t voltage_mv = 0;
        uint32_t now_ms;
        ret = ad8232_read_mv(&voltage_mv);
        if (ret == ERRCODE_SUCC) {
            ecg_input_sample_t input = {
                .seq = seq++,
                .timestamp_ms = (uint32_t)uapi_tcxo_get_ms(),
                .voltage_mv = voltage_mv,
            };
            ecg_monitor_sample_t output = {0};
            ecg_processor_process(&input, &output);
            ret = ecg_sle_client_send_sample(&output);
            if (ret == ERRCODE_SUCC) {
                sent_count++;
            } else if (get_g_sle_uart_send_param()->handle == 0) {
                drop_count++;
            } else {
                fail_count++;
            }
        } else {
            fail_count++;
        }

        now_ms = (uint32_t)uapi_tcxo_get_ms();
        if ((now_ms - last_stats_ms) >= ECG_SLE_STATS_INTERVAL_MS) {
            osal_printk("%s stats sent=%lu drop=%lu fail=%lu handle=0x%x\r\n", ECG_SLE_CLIENT_LOG,
                (unsigned long)sent_count, (unsigned long)drop_count, (unsigned long)fail_count,
                (unsigned int)get_g_sle_uart_send_param()->handle);
            last_stats_ms = now_ms;
        }
        osal_msleep(ECG_SLE_SAMPLE_MS);
    }

    return NULL;
}
#endif

static void ecg_sle_entry(void)
{
    osThreadAttr_t attr = {0};
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = ECG_SLE_TASK_STACK_SIZE;
    attr.priority = ECG_SLE_TASK_PRIO;

#if defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_SERVER)
    attr.name = "EcgSleServerTask";
    if (osThreadNew((osThreadFunc_t)ecg_sle_server_task, NULL, &attr) == NULL) {
        osal_printk("%s create task failed\r\n", ECG_SLE_SERVER_LOG);
    }
#elif defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_CLIENT)
    attr.name = "EcgSleClientTask";
    if (osThreadNew((osThreadFunc_t)ecg_sle_client_task, NULL, &attr) == NULL) {
        osal_printk("%s create task failed\r\n", ECG_SLE_CLIENT_LOG);
    }
#else
    osal_printk("[ecg sle] no role selected\r\n");
#endif
}

app_run(ecg_sle_entry); 








