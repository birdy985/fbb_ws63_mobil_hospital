#ifndef ECG_SLE_PACKET_H
#define ECG_SLE_PACKET_H

#include <stdbool.h>
#include <stdint.h>
#include "../9_ad8232_adc/ecg_processor.h"
#include "errcode.h"

#define ECG_SLE_PACKET_MAGIC        0xEC9D
#define ECG_SLE_PACKET_VERSION_LEGACY 1
#define ECG_SLE_PACKET_VERSION      2
#define ECG_SLE_PACKET_TYPE_SAMPLE  1
#define ECG_SLE_PACKET_TYPE_GT_HEALTH  2
#define ECG_SLE_PACKET_ECG_PAYLOAD_LEN  24
#define ECG_SLE_PACKET_PAYLOAD_LEN      40
#define ECG_SLE_PACKET_GT_PAYLOAD_LEN   20
#define ECG_SLE_PACKET_GT_LEN           28
#define ECG_SLE_PACKET_LEGACY_LEN       32
#define ECG_SLE_PACKET_LEN              48

typedef enum {
    ECG_SLE_SPO2_QUALITY_UNKNOWN = 0,
    ECG_SLE_SPO2_QUALITY_WARMUP,
    ECG_SLE_SPO2_QUALITY_OK,
    ECG_SLE_SPO2_QUALITY_NO_FINGER,
    ECG_SLE_SPO2_QUALITY_LOW_SIGNAL,
    ECG_SLE_SPO2_QUALITY_SATURATED,
    ECG_SLE_SPO2_QUALITY_NO_PULSE,
    ECG_SLE_SPO2_QUALITY_UNSTABLE,
    ECG_SLE_SPO2_QUALITY_SENSOR_OFF,
} ecg_sle_spo2_quality_t;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    int32_t spo2_x10;
    uint16_t hr_bpm;
    bool spo2_valid;
    bool hr_valid;
    bool sample_valid;
    ecg_sle_spo2_quality_t quality;
} ecg_sle_spo2_sample_t;

typedef struct {
    ecg_monitor_sample_t ecg;
    ecg_sle_spo2_sample_t spo2;
} ecg_sle_vital_sample_t;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t heart_rate;
    uint16_t spo2;
    uint16_t microcirculation;
    uint16_t systolic_bp;
    uint16_t diastolic_bp;
    bool heart_rate_valid;
    bool spo2_valid;
    bool microcirculation_valid;
    bool bp_valid;
} ecg_sle_gt_health_sample_t;

typedef enum {
    ECG_SLE_PACKET_OK = 0,
    ECG_SLE_PACKET_ERR_NULL,
    ECG_SLE_PACKET_ERR_LEN,
    ECG_SLE_PACKET_ERR_MAGIC,
    ECG_SLE_PACKET_ERR_VERSION,
    ECG_SLE_PACKET_ERR_TYPE,
    ECG_SLE_PACKET_ERR_PAYLOAD_LEN,
    ECG_SLE_PACKET_ERR_CHECKSUM,
} ecg_sle_packet_status_t;

errcode_t ecg_sle_encode_vital_sample(const ecg_sle_vital_sample_t *sample, uint8_t *buffer, uint16_t buffer_len);
ecg_sle_packet_status_t ecg_sle_decode_vital_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_sle_vital_sample_t *sample);
errcode_t ecg_sle_encode_gt_health_sample(const ecg_sle_gt_health_sample_t *sample, uint8_t *buffer,
    uint16_t buffer_len);
ecg_sle_packet_status_t ecg_sle_decode_gt_health_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_sle_gt_health_sample_t *sample);
uint8_t ecg_sle_get_packet_type(const uint8_t *buffer, uint16_t buffer_len);
errcode_t ecg_sle_encode_sample(const ecg_monitor_sample_t *sample, uint8_t *buffer, uint16_t buffer_len);
ecg_sle_packet_status_t ecg_sle_decode_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_monitor_sample_t *sample);
const char *ecg_sle_packet_status_to_string(ecg_sle_packet_status_t status);
const char *ecg_sle_spo2_quality_to_string(ecg_sle_spo2_quality_t quality);

#endif
