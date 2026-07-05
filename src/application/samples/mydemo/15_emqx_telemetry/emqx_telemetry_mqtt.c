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
#define EMQX_PATIENT_PAYLOAD_SIZE        768
#define EMQX_PATIENT_NAME_SIZE           64
#define EMQX_PATIENT_RECORD_SIZE         48
#define EMQX_PATIENT_GENDER_SIZE         16
#define EMQX_PATIENT_AGE_SIZE            8
#define EMQX_PATIENT_PHONE_SIZE          32
#define EMQX_PATIENT_NOTE_SIZE           160
#define EMQX_JSON_SEARCH_DEPTH           5
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

typedef struct {
    const char *bad_text;
    const char *fixed_text;
} emqx_text_fix_map_t;

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

static uint32_t emqx_latest_ts(const emqx_telemetry_snapshot_t *snapshot)
{
    uint32_t ts = snapshot->ecg.timestamp_ms;

    if (snapshot->spo2.timestamp_ms > ts) {
        ts = snapshot->spo2.timestamp_ms;
    }
    if (snapshot->gt_health.timestamp_ms > ts) {
        ts = snapshot->gt_health.timestamp_ms;
    }
    return ts;
}

static int emqx_build_payload(char *buffer, size_t buffer_len)
{
    emqx_telemetry_snapshot_t snapshot = {0};
    int32_t spo2_x10 = 0;
    int spo2_int = 0;
    int spo2_frac = 0;
    uint32_t heart_rate = 0;
    uint16_t microcirculation = 0;
    uint16_t systolic_bp = 0;
    uint16_t diastolic_bp = 0;
    bool heart_rate_valid = false;
    bool spo2_valid = false;
    bool microcirculation_valid = false;
    bool bp_valid = false;

    emqx_telemetry_get_snapshot(&snapshot);
    if (snapshot.gt_health.sample_valid && snapshot.gt_health.heart_rate_valid) {
        heart_rate = snapshot.gt_health.heart_rate;
        heart_rate_valid = true;
    } else if (snapshot.ecg.ecg_valid && (snapshot.ecg.heart_rate > 0)) {
        heart_rate = snapshot.ecg.heart_rate;
        heart_rate_valid = true;
    } else if (snapshot.spo2.hr_valid) {
        heart_rate = snapshot.spo2.hr_bpm;
        heart_rate_valid = true;
    }

    if (snapshot.gt_health.sample_valid && snapshot.gt_health.spo2_valid) {
        spo2_x10 = (int32_t)snapshot.gt_health.spo2 * 10;
        spo2_valid = true;
    } else if (snapshot.spo2.spo2_valid) {
        spo2_x10 = snapshot.spo2.spo2_x10;
        spo2_valid = true;
    }
    if (spo2_valid) {
        spo2_int = spo2_x10 / 10;
        spo2_frac = spo2_x10 % 10;
    }

    if (snapshot.gt_health.sample_valid && snapshot.gt_health.microcirculation_valid) {
        microcirculation = snapshot.gt_health.microcirculation;
        microcirculation_valid = true;
    }
    if (snapshot.gt_health.sample_valid && snapshot.gt_health.bp_valid) {
        systolic_bp = snapshot.gt_health.systolic_bp;
        diastolic_bp = snapshot.gt_health.diastolic_bp;
        bp_valid = true;
    }

    return snprintf_s(buffer, buffer_len, buffer_len - 1,
        "{\"deviceId\":\"bed01\",\"seq\":%lu,\"heartRate\":%lu,\"heartRateValid\":%s,"
        "\"spo2\":%d.%d,\"spo2_x10\":%ld,\"spo2Valid\":%s,"
        "\"microcirculation\":%u,\"microcirculationValid\":%s,"
        "\"systolicBp\":%u,\"diastolicBp\":%u,\"bpValid\":%s,"
        "\"gtHealthValid\":%s,\"ts\":%lu}",
        (unsigned long)g_emqx_publish_seq++,
        (unsigned long)heart_rate,
        heart_rate_valid ? "true" : "false",
        spo2_int,
        spo2_frac < 0 ? -spo2_frac : spo2_frac,
        (long)spo2_x10,
        spo2_valid ? "true" : "false",
        (unsigned int)microcirculation,
        microcirculation_valid ? "true" : "false",
        (unsigned int)systolic_bp,
        (unsigned int)diastolic_bp,
        bp_valid ? "true" : "false",
        snapshot.gt_health.sample_valid ? "true" : "false",
        (unsigned long)emqx_latest_ts(&snapshot));
}

static int emqx_publish_once(void)
{
    char payload[512];
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

static uint8_t emqx_utf8_char_len(const uint8_t *text)
{
    if (text == NULL) {
        return 0;
    }
    if (text[0] < 0x80) {
        return 1;
    }
    if (((text[0] & 0xE0) == 0xC0) && ((text[1] & 0xC0) == 0x80)) {
        return 2;
    }
    if (((text[0] & 0xF0) == 0xE0) && ((text[1] & 0xC0) == 0x80) && ((text[2] & 0xC0) == 0x80)) {
        return 3;
    }
    if (((text[0] & 0xF8) == 0xF0) && ((text[1] & 0xC0) == 0x80) &&
        ((text[2] & 0xC0) == 0x80) && ((text[3] & 0xC0) == 0x80)) {
        return 4;
    }
    return 1;
}

static bool emqx_copy_text_value(char *dst, size_t dst_len, const char *value)
{
    const uint8_t *src = (const uint8_t *)value;
    size_t out = 0;

    if ((dst == NULL) || (dst_len == 0) || (value == NULL)) {
        return false;
    }
    while ((*src != '\0') && (out + 1U < dst_len)) {
        uint8_t char_len = emqx_utf8_char_len(src);
        if ((char_len == 0) || (out + char_len >= dst_len)) {
            break;
        }
        for (uint8_t i = 0; i < char_len; i++) {
            dst[out++] = (char)src[i];
        }
        src += char_len;
    }
    dst[out] = '\0';
    return out > 0;
}

static const emqx_text_fix_map_t g_emqx_text_fix_map[] = {
    {"\xE5\xAF\xAE\xE7\x8A\xB1\xE7\xAC\x81\xE9\x8D\xA5\x3F", "\xE5\xBC\xA0\xE4\xB8\x89\xE5\x9B\x9B"},
    {"\xE5\xAF\xAE\xE7\x8A\xB1\xE7\xAC\x81", "\xE5\xBC\xA0\xE4\xB8\x89"},
    {"\xE9\x90\xA2\x3F", "\xE7\x94\xB7"},
    {"\xE6\xBF\x82\x3F", "\xE5\xA5\xB3"},
    {"\xE9\x8D\x8F\xE6\x9C\xB5\xE7\xB2\xAC", "\xE5\x85\xB6\xE4\xBB\x96"},
    {"\xE7\xBC\x81\xE8\x83\xAF\xE5\xA3\x8A", "\xE7\xBB\xBF\xE8\x89\xB2"},
    {"\xE6\xA6\x9B\xE5\x8B\xAE\xE5\xA3\x8A", "\xE9\xBB\x84\xE8\x89\xB2"},
    {"\xE7\xBB\xBE\xE3\x88\xA3\xE5\xA3\x8A", "\xE7\xBA\xA2\xE8\x89\xB2"},
};

static void emqx_repair_mojibake_text(char *text, size_t text_len)
{
    char fixed[EMQX_PATIENT_NOTE_SIZE];
    size_t src = 0;
    size_t out = 0;

    if ((text == NULL) || (text_len == 0) || (text[0] == '\0') || (text_len > sizeof(fixed))) {
        return;
    }

    while ((text[src] != '\0') && (out + 1U < sizeof(fixed))) {
        const char *replacement = NULL;
        size_t bad_len = 0;

        for (uint32_t i = 0; i < (sizeof(g_emqx_text_fix_map) / sizeof(g_emqx_text_fix_map[0])); i++) {
            size_t candidate_len = strlen(g_emqx_text_fix_map[i].bad_text);
            if ((candidate_len > bad_len) &&
                (strncmp(&text[src], g_emqx_text_fix_map[i].bad_text, candidate_len) == 0)) {
                replacement = g_emqx_text_fix_map[i].fixed_text;
                bad_len = candidate_len;
            }
        }

        if (replacement != NULL) {
            size_t replacement_len = strlen(replacement);
            if (out + replacement_len >= sizeof(fixed)) {
                break;
            }
            (void)memcpy(&fixed[out], replacement, replacement_len);
            out += replacement_len;
            src += bad_len;
            continue;
        }

        fixed[out++] = text[src++];
    }
    fixed[out] = '\0';
    (void)memcpy_s(text, text_len, fixed, strlen(fixed) + 1);
}

static void emqx_normalize_gender(char *gender, size_t gender_len)
{
    const char *normalized = NULL;

    if ((gender == NULL) || (gender_len == 0) || (gender[0] == '\0')) {
        return;
    }

    if ((strcmp(gender, "male") == 0) || (strcmp(gender, "Male") == 0) || (strcmp(gender, "MALE") == 0) ||
        (strcmp(gender, "\xE7\x94\xB7") == 0)) {
        normalized = "male";
    } else if ((strcmp(gender, "female") == 0) || (strcmp(gender, "Female") == 0) ||
        (strcmp(gender, "FEMALE") == 0) || (strcmp(gender, "\xE5\xA5\xB3") == 0)) {
        normalized = "female";
    }

    if (normalized != NULL) {
        (void)memcpy_s(gender, gender_len, normalized, strlen(normalized) + 1);
    }
}

static bool emqx_copy_json_value(cJSON *item, char *dst, size_t dst_len)
{
    const char *value;
    int len;

    if ((item == NULL) || (dst == NULL) || (dst_len == 0)) {
        return false;
    }
    if (cJSON_IsString(item)) {
        value = cJSON_GetStringValue(item);
        if (value == NULL) {
            return false;
        }
        return emqx_copy_text_value(dst, dst_len, value);
    }
    if (cJSON_IsNumber(item)) {
        len = snprintf_s(dst, dst_len, dst_len - 1, "%d", (int)item->valueint);
        return (len > 0) && ((size_t)len < dst_len);
    }
    return false;
}

static bool emqx_copy_json_string(cJSON *object, const char *key, char *dst, size_t dst_len)
{
    cJSON *item;

    if ((object == NULL) || (key == NULL) || (dst == NULL) || (dst_len == 0)) {
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, key);
    return emqx_copy_json_value(item, dst, dst_len);
}

static bool emqx_copy_json_value_any(cJSON *object, const char * const *keys, char *dst, size_t dst_len)
{
    if ((keys == NULL) || (dst == NULL) || (dst_len == 0)) {
        return false;
    }

    for (uint32_t i = 0; keys[i] != NULL; i++) {
        if (emqx_copy_json_string(object, keys[i], dst, dst_len)) {
            return true;
        }
    }
    return false;
}

static bool emqx_copy_json_value_deep(cJSON *node, const char * const *keys,
    char *dst, size_t dst_len, uint8_t depth)
{
    cJSON *child;

    if ((node == NULL) || (keys == NULL) || (dst == NULL) || (dst_len == 0) || (depth == 0)) {
        return false;
    }
    if (cJSON_IsObject(node) && emqx_copy_json_value_any(node, keys, dst, dst_len)) {
        return true;
    }
    if (!cJSON_IsObject(node) && !cJSON_IsArray(node)) {
        return false;
    }

    cJSON_ArrayForEach(child, node) {
        if (emqx_copy_json_value_deep(child, keys, dst, dst_len, depth - 1)) {
            return true;
        }
    }
    return false;
}

static bool emqx_parse_patient_payload(const char *payload, emqx_patient_info_t *patient)
{
    static const char * const name_keys[] = {
        "name", "Name", "patientName", "patient_name", "t7", "姓名", "病人姓名", NULL
    };
    static const char * const record_keys[] = {
        "recordNo", "record_no", "caseNo", "case_no", "caseNumber", "medicalRecordNo", "medicalRecord",
        "t8", "病历号", NULL
    };
    static const char * const gender_keys[] = {
        "gender", "Gender", "sex", "Sex", "genderText", "genderValue", "sexText", "sexValue",
        "patientGender", "patientSex", "t9", "性别", NULL
    };
    static const char * const age_keys[] = {"age", "Age", "patientAge", "t10", "年龄", NULL};
    static const char * const phone_keys[] = {
        "phone", "Phone", "telephone", "tel", "mobile", "contactPhone", "phoneNumber", "t11", "联系电话",
        "电话", NULL
    };
    static const char * const note_keys[] = {
        "note", "Note", "remark", "Remark", "remarks", "memo", "comment", "comments", "description", "desc",
        "patientNote", "patient_note", "patientRemark", "patient_remark", "t12", "备注", NULL
    };
    cJSON *root;
    bool has_name;
    bool has_record;
    bool has_gender;
    bool has_age;
    bool has_phone;
    bool has_note;

    if ((payload == NULL) || (patient == NULL)) {
        return false;
    }
    (void)memset_s(patient, sizeof(*patient), 0, sizeof(*patient));

    root = cJSON_Parse(payload);
    if (root == NULL) {
        osal_printk("%s patient json parse failed\r\n", EMQX_LOG);
        return false;
    }

    has_name = emqx_copy_json_value_deep(root, name_keys, patient->name, sizeof(patient->name),
        EMQX_JSON_SEARCH_DEPTH);
    has_record = emqx_copy_json_value_deep(root, record_keys, patient->record_no, sizeof(patient->record_no),
        EMQX_JSON_SEARCH_DEPTH);
    has_gender = emqx_copy_json_value_deep(root, gender_keys, patient->gender, sizeof(patient->gender),
        EMQX_JSON_SEARCH_DEPTH);
    has_age = emqx_copy_json_value_deep(root, age_keys, patient->age, sizeof(patient->age), EMQX_JSON_SEARCH_DEPTH);
    has_phone = emqx_copy_json_value_deep(root, phone_keys, patient->phone, sizeof(patient->phone),
        EMQX_JSON_SEARCH_DEPTH);
    has_note = emqx_copy_json_value_deep(root, note_keys, patient->note, sizeof(patient->note),
        EMQX_JSON_SEARCH_DEPTH);

    emqx_repair_mojibake_text(patient->name, sizeof(patient->name));
    emqx_repair_mojibake_text(patient->record_no, sizeof(patient->record_no));
    emqx_repair_mojibake_text(patient->gender, sizeof(patient->gender));
    emqx_repair_mojibake_text(patient->phone, sizeof(patient->phone));
    emqx_repair_mojibake_text(patient->note, sizeof(patient->note));
    emqx_normalize_gender(patient->gender, sizeof(patient->gender));

    cJSON_Delete(root);
    return has_name || has_record || has_gender || has_age || has_phone || has_note;
}

static const char *emqx_patient_text_or_dash(const char *text)
{
    if ((text == NULL) || (text[0] == '\0')) {
        return "--";
    }
    return text;
}

static void emqx_handle_patient_msg(const emqx_patient_msg_t *msg)
{
    emqx_patient_info_t patient;
    const char *name;
    const char *record_no;
    const char *gender;
    const char *age;
    const char *phone;
    const char *note;

    if ((msg == NULL) || (msg->len == 0)) {
        return;
    }
    osal_printk("%s patient raw=%s\r\n", EMQX_LOG, msg->payload);
    if (!emqx_parse_patient_payload(msg->payload, &patient)) {
        osal_printk("%s invalid patient payload\r\n", EMQX_LOG);
        return;
    }

    name = emqx_patient_text_or_dash(patient.name);
    record_no = emqx_patient_text_or_dash(patient.record_no);
    gender = emqx_patient_text_or_dash(patient.gender);
    age = emqx_patient_text_or_dash(patient.age);
    phone = emqx_patient_text_or_dash(patient.phone);
    note = emqx_patient_text_or_dash(patient.note);
    osal_printk("%s patient name=%s record=%s gender=%s age=%s phone=%s note=%s\r\n",
        EMQX_LOG, name, record_no, gender, age, phone, note);
#if defined(CONFIG_SAMPLE_SUPPORT_AD8232_SLE_SERVER)
    osal_printk("[TJC_PATIENT] t7=%s t8=%s t9=%s t10=%s t11=%s t12=%s\r\n",
        name, record_no, gender, age, phone, note);
    tjc_display_send_patient_info(name, record_no, gender, age, phone, note);
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


