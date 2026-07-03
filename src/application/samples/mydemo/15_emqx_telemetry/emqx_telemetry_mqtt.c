#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_init.h"
#include "cmsis_os2.h"
#include "cJSON.h"
#include "common_def.h"
#include "errcode.h"
#include "lwip/netifapi.h"
#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
#include "securec.h"
#include "soc_osal.h"
#include "td_base.h"
#include "td_type.h"
#include "uart.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"

#include "emqx_telemetry.h"

#if defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_SERVER)
#include "../13_ad8232_sle/tjc_display.h"
#endif

#define EMQX_TASK_PRIO                   osPriorityNormal
#define EMQX_TASK_STACK_SIZE             0x4000
#define EMQX_WIFI_SCAN_AP_LIMIT          64
#define EMQX_WIFI_CONN_CHECK_TIMES       10
#define EMQX_DHCP_CHECK_TIMES            30
#define EMQX_PUBLISH_INTERVAL_MS         1000
#define EMQX_PUBLISH_TIMEOUT_MS          10000L
#define EMQX_MQTT_KEEP_ALIVE_SECONDS     60
#define EMQX_MQTT_QOS                    0
#define EMQX_PATIENT_QUEUE_LEN           4
#define EMQX_PATIENT_PAYLOAD_SIZE        512
#define EMQX_PATIENT_NAME_SIZE           32
#define EMQX_PATIENT_RECORD_SIZE         32
#define EMQX_PATIENT_GENDER_SIZE         12
#define EMQX_PATIENT_AGE_SIZE            8
#define EMQX_PATIENT_PHONE_SIZE          24
#define EMQX_PATIENT_NOTE_SIZE           96
#define EMQX_LOG                         "[EMQX]"

#define EMQX_MQTT_URI                    "ssl://q67a1139.ala.cn-shenzhen.emqxsl.cn:8883"
#define EMQX_MQTT_CLIENT_ID              "ws63_bed01"
#define EMQX_MQTT_TELEMETRY_TOPIC        "hispark/bed01/telemetry"
#define EMQX_MQTT_PATIENT_TOPIC          "hispark/bed01/patient"
#define EMQX_MQTT_USERNAME               CONFIG_EMQX_MQTT_USERNAME
#define EMQX_MQTT_PASSWORD               CONFIG_EMQX_MQTT_PASSWORD

typedef struct {
    uint16_t len;
    char payload[EMQX_PATIENT_PAYLOAD_SIZE];
} emqx_patient_msg_t;

typedef struct {
    char name[EMQX_PATIENT_NAME_SIZE];
    char record_no[EMQX_PATIENT_RECORD_SIZE];
    char gender[EMQX_PATIENT_GENDER_SIZE];
    char age[EMQX_PATIENT_AGE_SIZE];
    char phone[EMQX_PATIENT_PHONE_SIZE];
    char note[EMQX_PATIENT_NOTE_SIZE];
} emqx_patient_info_t;

static MQTTClient g_emqx_client;
static bool g_emqx_client_connected;
static bool g_emqx_patient_queue_ready;
static unsigned long g_emqx_patient_queue_id;
static uint32_t g_emqx_publish_seq;

extern int MQTTClient_init(void);
extern void MQTTClient_cleanup(void);

static errcode_t emqx_wifi_get_match_network(const char *expected_ssid, const char *key,
    wifi_sta_config_stru *expected_bss)
{
    uint32_t num = EMQX_WIFI_SCAN_AP_LIMIT;
    uint32_t scan_len = sizeof(wifi_scan_info_stru) * EMQX_WIFI_SCAN_AP_LIMIT;
    wifi_scan_info_stru *result = osal_kmalloc(scan_len, OSAL_GFP_ATOMIC);

    if ((expected_ssid == NULL) || (key == NULL) || (expected_bss == NULL)) {
        return ERRCODE_FAIL;
    }
    if (result == NULL) {
        return ERRCODE_MALLOC;
    }

    (void)memset_s(result, scan_len, 0, scan_len);
    if (wifi_sta_get_scan_info(result, &num) != ERRCODE_SUCC) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }

    for (uint32_t i = 0; i < num; i++) {
        if ((strlen(expected_ssid) == strlen(result[i].ssid)) &&
            (memcmp(expected_ssid, result[i].ssid, strlen(expected_ssid)) == 0)) {
            if (memcpy_s(expected_bss->ssid, WIFI_MAX_SSID_LEN, result[i].ssid, WIFI_MAX_SSID_LEN) != EOK) {
                osal_kfree(result);
                return ERRCODE_MEMCPY;
            }
            if (memcpy_s(expected_bss->bssid, WIFI_MAC_LEN, result[i].bssid, WIFI_MAC_LEN) != EOK) {
                osal_kfree(result);
                return ERRCODE_MEMCPY;
            }
            expected_bss->security_type = result[i].security_type;
            if (memcpy_s(expected_bss->pre_shared_key, WIFI_MAX_KEY_LEN, key, strlen(key)) != EOK) {
                osal_kfree(result);
                return ERRCODE_MEMCPY;
            }
            expected_bss->ip_type = 1;
            osal_kfree(result);
            return ERRCODE_SUCC;
        }
    }

    osal_kfree(result);
    return ERRCODE_FAIL;
}

static errcode_t emqx_wifi_connect(void)
{
    char ifname[WIFI_IFNAME_MAX_SIZE + 1] = "wlan0";
    wifi_sta_config_stru expected_bss = {0};
    wifi_linked_info_stru wifi_status;
    struct netif *netif_p = NULL;

    while (wifi_is_wifi_inited() == 0) {
        osDelay(10);
    }

    if (wifi_sta_enable() != ERRCODE_SUCC) {
        osal_printk("%s sta enable failed\r\n", EMQX_LOG);
        return ERRCODE_FAIL;
    }

    while (1) {
        osal_printk("%s scan start\r\n", EMQX_LOG);
        if (wifi_sta_scan() != ERRCODE_SUCC) {
            osDelay(100);
            continue;
        }
        osDelay(300);

        if (emqx_wifi_get_match_network(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD, &expected_bss) != ERRCODE_SUCC) {
            osal_printk("%s AP not found\r\n", EMQX_LOG);
            osDelay(100);
            continue;
        }

        osal_printk("%s connect start\r\n", EMQX_LOG);
        if (wifi_sta_connect(&expected_bss) != ERRCODE_SUCC) {
            osDelay(100);
            continue;
        }

        for (uint8_t i = 0; i < EMQX_WIFI_CONN_CHECK_TIMES; i++) {
            osDelay(50);
            (void)memset_s(&wifi_status, sizeof(wifi_status), 0, sizeof(wifi_status));
            if ((wifi_sta_get_ap_info(&wifi_status) == ERRCODE_SUCC) && (wifi_status.conn_state == WIFI_CONNECTED)) {
                netif_p = netifapi_netif_find(ifname);
                if (netif_p == NULL) {
                    return ERRCODE_FAIL;
                }
                if (netifapi_dhcp_start(netif_p) != ERR_OK) {
                    return ERRCODE_FAIL;
                }
                for (uint8_t j = 0; j < EMQX_DHCP_CHECK_TIMES; j++) {
                    osDelay(100);
                    if (netif_p->ip_addr.u_addr.ip4.addr != 0) {
                        osal_printk("%s wifi ready\r\n", EMQX_LOG);
                        return ERRCODE_SUCC;
                    }
                }
                return ERRCODE_FAIL;
            }
        }
        osal_printk("%s connect retry\r\n", EMQX_LOG);
    }
}

static void emqx_mqtt_connection_lost(void *context, char *cause)
{
    unused(context);
    g_emqx_client_connected = false;
    osal_printk("%s connection lost: %s\r\n", EMQX_LOG, cause == NULL ? "unknown" : cause);
}

static void emqx_mqtt_delivery_complete(void *context, MQTTClient_deliveryToken token)
{
    unused(context);
    unused(token);
}

static int emqx_patient_queue_payload(const char *payload, int payload_len)
{
    emqx_patient_msg_t msg = {0};
    uint16_t copy_len;

    if (!g_emqx_patient_queue_ready || (payload == NULL) || (payload_len <= 0)) {
        return 1;
    }

    copy_len = (uint16_t)payload_len;
    if (copy_len >= EMQX_PATIENT_PAYLOAD_SIZE) {
        copy_len = EMQX_PATIENT_PAYLOAD_SIZE - 1;
    }
    if (memcpy_s(msg.payload, sizeof(msg.payload), payload, copy_len) != EOK) {
        osal_printk("%s patient payload copy failed\r\n", EMQX_LOG);
        return 1;
    }
    msg.len = copy_len;
    msg.payload[copy_len] = '\0';
    if (osal_msg_queue_write_copy(g_emqx_patient_queue_id, &msg, sizeof(msg), 0) != OSAL_SUCCESS) {
        osal_printk("%s patient queue full, drop payload\r\n", EMQX_LOG);
    }
    return 1;
}

static int emqx_mqtt_message_arrived(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    unused(context);
    unused(topic_len);

    if ((topic_name != NULL) && (message != NULL) && (strcmp(topic_name, EMQX_MQTT_PATIENT_TOPIC) == 0)) {
        (void)emqx_patient_queue_payload((const char *)message->payload, message->payloadlen);
    }

    if (message != NULL) {
        MQTTClient_freeMessage(&message);
    }
    if (topic_name != NULL) {
        MQTTClient_free(topic_name);
    }
    return 1;
}

static int emqx_mqtt_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;

    ret = MQTTClient_init();
    if (ret != MQTTCLIENT_SUCCESS) {
        osal_printk("%s MQTTClient_init failed ret=%d\r\n", EMQX_LOG, ret);
        return ret;
    }

    ret = MQTTClient_create(&g_emqx_client, EMQX_MQTT_URI, EMQX_MQTT_CLIENT_ID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (ret != MQTTCLIENT_SUCCESS) {
        osal_printk("%s create failed ret=%d\r\n", EMQX_LOG, ret);
        MQTTClient_cleanup();
        return ret;
    }

    ret = MQTTClient_setCallbacks(g_emqx_client, NULL, emqx_mqtt_connection_lost,
        emqx_mqtt_message_arrived, emqx_mqtt_delivery_complete);
    if (ret != MQTTCLIENT_SUCCESS) {
        osal_printk("%s set callbacks failed ret=%d\r\n", EMQX_LOG, ret);
        MQTTClient_destroy(&g_emqx_client);
        MQTTClient_cleanup();
        return ret;
    }

    ssl_opts.sslVersion = MQTT_SSL_VERSION_TLS_1_2;
    ssl_opts.enableServerCertAuth = 0;
    conn_opts.keepAliveInterval = EMQX_MQTT_KEEP_ALIVE_SECONDS;
    conn_opts.cleansession = 1;
    conn_opts.username = EMQX_MQTT_USERNAME;
    conn_opts.password = EMQX_MQTT_PASSWORD;
    conn_opts.ssl = &ssl_opts;

    ret = MQTTClient_connect(g_emqx_client, &conn_opts);
    if (ret != MQTTCLIENT_SUCCESS) {
        osal_printk("%s connect failed ret=%d\r\n", EMQX_LOG, ret);
        MQTTClient_destroy(&g_emqx_client);
        MQTTClient_cleanup();
        return ret;
    }

    g_emqx_client_connected = true;
    osal_printk("%s mqtt connected\r\n", EMQX_LOG);

    ret = MQTTClient_subscribe(g_emqx_client, EMQX_MQTT_PATIENT_TOPIC, EMQX_MQTT_QOS);
    if (ret == MQTTCLIENT_SUCCESS) {
        osal_printk("%s subscribed %s\r\n", EMQX_LOG, EMQX_MQTT_PATIENT_TOPIC);
    } else {
        osal_printk("%s subscribe %s failed ret=%d\r\n", EMQX_LOG, EMQX_MQTT_PATIENT_TOPIC, ret);
    }
    return MQTTCLIENT_SUCCESS;
}

static int emqx_build_payload(char *buffer, size_t buffer_len)
{
    emqx_telemetry_snapshot_t snapshot = {0};
    int spo2_int = 0;
    int spo2_frac = 0;
    uint32_t heart_rate = 0;
    const char *quality = "--";

    emqx_telemetry_get_snapshot(&snapshot);
    if (snapshot.spo2.spo2_valid) {
        spo2_int = snapshot.spo2.spo2_x10 / 10;
        spo2_frac = snapshot.spo2.spo2_x10 % 10;
    }
    if (snapshot.ecg.ecg_valid && (snapshot.ecg.heart_rate > 0)) {
        heart_rate = snapshot.ecg.heart_rate;
    } else if (snapshot.spo2.hr_valid) {
        heart_rate = snapshot.spo2.hr_bpm;
    }
    if (snapshot.spo2.quality != NULL) {
        quality = snapshot.spo2.quality;
    }

    return snprintf_s(buffer, buffer_len, buffer_len - 1,
        "{\"deviceId\":\"bed01\",\"seq\":%lu,\"dispplay_mv\":%d,\"display_mv\":%d,"
        "\"heartRate\":%lu,\"spo2\":%d.%d,\"spo2_x10\":%ld,\"quality\":\"%s\","
        "\"ecgValid\":%s,\"spo2Valid\":%s,\"ts\":%lu}",
        (unsigned long)g_emqx_publish_seq++,
        (int)snapshot.ecg.display_mv,
        (int)snapshot.ecg.display_mv,
        (unsigned long)heart_rate,
        spo2_int,
        spo2_frac < 0 ? -spo2_frac : spo2_frac,
        (long)snapshot.spo2.spo2_x10,
        quality,
        snapshot.ecg.ecg_valid ? "true" : "false",
        snapshot.spo2.spo2_valid ? "true" : "false",
        (unsigned long)(snapshot.ecg.timestamp_ms > snapshot.spo2.timestamp_ms ?
            snapshot.ecg.timestamp_ms : snapshot.spo2.timestamp_ms));
}

static int emqx_publish_once(void)
{
    char payload[384];
    int len;
    int ret;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    len = emqx_build_payload(payload, sizeof(payload));
    if ((len <= 0) || ((size_t)len >= sizeof(payload))) {
        osal_printk("%s build payload failed len=%d\r\n", EMQX_LOG, len);
        return MQTTCLIENT_FAILURE;
    }

    pubmsg.payload = payload;
    pubmsg.payloadlen = len;
    pubmsg.qos = EMQX_MQTT_QOS;
    pubmsg.retained = 0;

    ret = MQTTClient_publishMessage(g_emqx_client, EMQX_MQTT_TELEMETRY_TOPIC, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        osal_printk("%s publish failed ret=%d\r\n", EMQX_LOG, ret);
        return ret;
    }
    if (EMQX_MQTT_QOS > 0) {
        ret = MQTTClient_waitForCompletion(g_emqx_client, token, EMQX_PUBLISH_TIMEOUT_MS);
    }
    osal_printk("%s publish %s\r\n", EMQX_LOG, payload);
    return ret;
}

static bool emqx_copy_json_string(cJSON *object, const char *key, char *dst, size_t dst_len)
{
    cJSON *item;
    const char *value;

    if ((object == NULL) || (key == NULL) || (dst == NULL) || (dst_len == 0)) {
        return false;
    }
    dst[0] = '\0';
    item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item)) {
        return false;
    }
    value = cJSON_GetStringValue(item);
    if (value == NULL) {
        return false;
    }
    if (strncpy_s(dst, dst_len, value, dst_len - 1) != EOK) {
        dst[0] = '\0';
        return false;
    }
    return true;
}

static bool emqx_copy_json_age(cJSON *object, char *dst, size_t dst_len)
{
    cJSON *item;
    int len;

    if ((object == NULL) || (dst == NULL) || (dst_len == 0)) {
        return false;
    }
    dst[0] = '\0';
    item = cJSON_GetObjectItemCaseSensitive(object, "age");
    if (cJSON_IsNumber(item)) {
        len = snprintf_s(dst, dst_len, dst_len - 1, "%d", (int)item->valueint);
        return (len > 0) && ((size_t)len < dst_len);
    }
    return emqx_copy_json_string(object, "age", dst, dst_len);
}

static bool emqx_parse_patient_payload(const char *payload, emqx_patient_info_t *patient)
{
    cJSON *root;
    cJSON *type;
    cJSON *patient_json;
    bool ok = false;

    if ((payload == NULL) || (patient == NULL)) {
        return false;
    }
    (void)memset_s(patient, sizeof(*patient), 0, sizeof(*patient));

    root = cJSON_Parse(payload);
    if (root == NULL) {
        osal_printk("%s patient json parse failed\r\n", EMQX_LOG);
        return false;
    }

    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    patient_json = cJSON_GetObjectItemCaseSensitive(root, "patient");
    if (cJSON_IsString(type) && (cJSON_GetStringValue(type) != NULL) &&
        (strcmp(cJSON_GetStringValue(type), "patientInfo") == 0) &&
        cJSON_IsObject(patient_json)) {
        ok = emqx_copy_json_string(patient_json, "name", patient->name, sizeof(patient->name));
        ok = emqx_copy_json_string(patient_json, "recordNo", patient->record_no, sizeof(patient->record_no)) && ok;
        (void)emqx_copy_json_string(patient_json, "gender", patient->gender, sizeof(patient->gender));
        (void)emqx_copy_json_age(patient_json, patient->age, sizeof(patient->age));
        (void)emqx_copy_json_string(patient_json, "phone", patient->phone, sizeof(patient->phone));
        (void)emqx_copy_json_string(patient_json, "note", patient->note, sizeof(patient->note));
    }

    cJSON_Delete(root);
    return ok;
}

static void emqx_handle_patient_msg(const emqx_patient_msg_t *msg)
{
    emqx_patient_info_t patient;

    if ((msg == NULL) || (msg->len == 0)) {
        return;
    }
    if (!emqx_parse_patient_payload(msg->payload, &patient)) {
        osal_printk("%s invalid patient payload\r\n", EMQX_LOG);
        return;
    }

    osal_printk("%s patient name=%s record=%s gender=%s age=%s phone=%s note=%s\r\n",
        EMQX_LOG, patient.name, patient.record_no, patient.gender, patient.age, patient.phone, patient.note);
#if defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_SERVER)
    tjc_display_send_patient_info(patient.name, patient.record_no, patient.gender, patient.age,
        patient.phone, patient.note);
#endif
}

static void emqx_process_patient_queue(void)
{
    emqx_patient_msg_t msg = {0};
    uint32_t msg_size = sizeof(msg);

    if (!g_emqx_patient_queue_ready) {
        return;
    }
    while (osal_msg_queue_read_copy(g_emqx_patient_queue_id, &msg, &msg_size, 0) == OSAL_SUCCESS) {
        emqx_handle_patient_msg(&msg);
        msg_size = sizeof(msg);
        (void)memset_s(&msg, sizeof(msg), 0, sizeof(msg));
    }
}

static void emqx_telemetry_task(void *arg)
{
    unused(arg);

    if (osal_msg_queue_create("emqx_patient", EMQX_PATIENT_QUEUE_LEN, &g_emqx_patient_queue_id, 0,
        sizeof(emqx_patient_msg_t)) == OSAL_SUCCESS) {
        g_emqx_patient_queue_ready = true;
    } else {
        osal_printk("%s create patient queue failed; downlink disabled\r\n", EMQX_LOG);
    }

    if (emqx_wifi_connect() != ERRCODE_SUCC) {
        osal_printk("%s wifi connect failed\r\n", EMQX_LOG);
        return;
    }
    if (emqx_mqtt_connect() != MQTTCLIENT_SUCCESS) {
        return;
    }

    while (1) {
        emqx_process_patient_queue();
        if (g_emqx_client_connected && (MQTTClient_isConnected(g_emqx_client) != 0)) {
            if (emqx_publish_once() != MQTTCLIENT_SUCCESS) {
                g_emqx_client_connected = false;
                osal_printk("%s mqtt disconnected, stop publishing\r\n", EMQX_LOG);
            }
        }
        osal_msleep(EMQX_PUBLISH_INTERVAL_MS);
    }

    return;
}

static void emqx_telemetry_entry(void)
{
    osThreadAttr_t attr = {0};

    attr.name = "EmqxTelemetryTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = EMQX_TASK_STACK_SIZE;
    attr.priority = EMQX_TASK_PRIO;

    if (osThreadNew((osThreadFunc_t)emqx_telemetry_task, NULL, &attr) == NULL) {
        osal_printk("%s create task failed\r\n", EMQX_LOG);
    }
}

app_run(emqx_telemetry_entry);


