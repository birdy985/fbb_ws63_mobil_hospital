#include "ecg_sle_packet.h"

#include <stddef.h>

#include "common_def.h"

#define ECG_SLE_HEADER_LEN 6
#define ECG_SLE_CHECKSUM_LEN 2

#define ECG_SLE_SPO2_FLAG_SAMPLE_VALID 0x01
#define ECG_SLE_SPO2_FLAG_SPO2_VALID   0x02
#define ECG_SLE_SPO2_FLAG_HR_VALID     0x04

#define ECG_SLE_GT_FLAG_HR_VALID       0x01
#define ECG_SLE_GT_FLAG_SPO2_VALID     0x02
#define ECG_SLE_GT_FLAG_MICRO_VALID    0x04
#define ECG_SLE_GT_FLAG_BP_VALID       0x08

static void ecg_sle_put_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void ecg_sle_put_i16(uint8_t *buffer, int16_t value)
{
    ecg_sle_put_u16(buffer, (uint16_t)value);
}

static void ecg_sle_put_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
    buffer[2] = (uint8_t)((value >> 16) & 0xFF);
    buffer[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void ecg_sle_put_i32(uint8_t *buffer, int32_t value)
{
    ecg_sle_put_u32(buffer, (uint32_t)value);
}

static uint16_t ecg_sle_get_u16(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

static int16_t ecg_sle_get_i16(const uint8_t *buffer)
{
    return (int16_t)ecg_sle_get_u16(buffer);
}

static uint32_t ecg_sle_get_u32(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
        ((uint32_t)buffer[1] << 8) |
        ((uint32_t)buffer[2] << 16) |
        ((uint32_t)buffer[3] << 24);
}

static int32_t ecg_sle_get_i32(const uint8_t *buffer)
{
    return (int32_t)ecg_sle_get_u32(buffer);
}

static uint16_t ecg_sle_checksum(const uint8_t *buffer, uint16_t len)
{
    uint16_t checksum = 0;
    uint16_t i;

    if (buffer == NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        checksum = (uint16_t)(checksum + buffer[i]);
    }
    return checksum;
}

static uint8_t ecg_sle_spo2_flags(const ecg_sle_spo2_sample_t *sample)
{
    uint8_t flags = 0;

    if (sample->sample_valid) {
        flags |= ECG_SLE_SPO2_FLAG_SAMPLE_VALID;
    }
    if (sample->spo2_valid) {
        flags |= ECG_SLE_SPO2_FLAG_SPO2_VALID;
    }
    if (sample->hr_valid) {
        flags |= ECG_SLE_SPO2_FLAG_HR_VALID;
    }
    return flags;
}

static uint8_t ecg_sle_gt_health_flags(const ecg_sle_gt_health_sample_t *sample)
{
    uint8_t flags = 0;

    if (sample->heart_rate_valid) {
        flags |= ECG_SLE_GT_FLAG_HR_VALID;
    }
    if (sample->spo2_valid) {
        flags |= ECG_SLE_GT_FLAG_SPO2_VALID;
    }
    if (sample->microcirculation_valid) {
        flags |= ECG_SLE_GT_FLAG_MICRO_VALID;
    }
    if (sample->bp_valid) {
        flags |= ECG_SLE_GT_FLAG_BP_VALID;
    }
    return flags;
}

static void ecg_sle_encode_ecg_payload(const ecg_monitor_sample_t *sample, uint8_t *buffer, uint16_t *offset)
{
    ecg_sle_put_u32(&buffer[*offset], sample->seq);
    *offset += 4;
    ecg_sle_put_u32(&buffer[*offset], sample->timestamp_ms);
    *offset += 4;
    ecg_sle_put_u16(&buffer[*offset], sample->voltage_mv);
    *offset += 2;
    ecg_sle_put_i16(&buffer[*offset], sample->baseline_mv);
    *offset += 2;
    ecg_sle_put_i16(&buffer[*offset], sample->ecg_mv);
    *offset += 2;
    ecg_sle_put_i16(&buffer[*offset], sample->filtered_mv);
    *offset += 2;
    ecg_sle_put_i16(&buffer[*offset], sample->display_mv);
    *offset += 2;
    buffer[(*offset)++] = sample->r_peak;
    buffer[(*offset)++] = (uint8_t)sample->quality;
    ecg_sle_put_u16(&buffer[*offset], sample->rr_interval_ms);
    *offset += 2;
    ecg_sle_put_u16(&buffer[*offset], sample->bpm);
    *offset += 2;
}

static void ecg_sle_decode_ecg_payload(const uint8_t *buffer, uint16_t *offset, ecg_monitor_sample_t *sample)
{
    sample->seq = ecg_sle_get_u32(&buffer[*offset]);
    *offset += 4;
    sample->timestamp_ms = ecg_sle_get_u32(&buffer[*offset]);
    *offset += 4;
    sample->voltage_mv = ecg_sle_get_u16(&buffer[*offset]);
    *offset += 2;
    sample->baseline_mv = ecg_sle_get_i16(&buffer[*offset]);
    *offset += 2;
    sample->ecg_mv = ecg_sle_get_i16(&buffer[*offset]);
    *offset += 2;
    sample->filtered_mv = ecg_sle_get_i16(&buffer[*offset]);
    *offset += 2;
    sample->display_mv = ecg_sle_get_i16(&buffer[*offset]);
    *offset += 2;
    sample->r_peak = buffer[(*offset)++];
    sample->quality = (ecg_signal_quality_t)buffer[(*offset)++];
    sample->rr_interval_ms = ecg_sle_get_u16(&buffer[*offset]);
    *offset += 2;
    sample->bpm = ecg_sle_get_u16(&buffer[*offset]);
    *offset += 2;
}

static ecg_sle_packet_status_t ecg_sle_validate_header(const uint8_t *buffer, uint16_t buffer_len,
    uint8_t *version, uint8_t *packet_type, uint16_t *payload_len)
{
    uint16_t checksum;

    if ((buffer == NULL) || (version == NULL) || (packet_type == NULL) || (payload_len == NULL)) {
        return ECG_SLE_PACKET_ERR_NULL;
    }
    if ((buffer_len != ECG_SLE_PACKET_LEGACY_LEN) && (buffer_len != ECG_SLE_PACKET_LEN) &&
        (buffer_len != ECG_SLE_PACKET_GT_LEN)) {
        return ECG_SLE_PACKET_ERR_LEN;
    }
    if (ecg_sle_get_u16(&buffer[0]) != ECG_SLE_PACKET_MAGIC) {
        return ECG_SLE_PACKET_ERR_MAGIC;
    }
    *version = buffer[2];
    if ((*version != ECG_SLE_PACKET_VERSION_LEGACY) && (*version != ECG_SLE_PACKET_VERSION)) {
        return ECG_SLE_PACKET_ERR_VERSION;
    }
    *packet_type = buffer[3];
    if ((*packet_type != ECG_SLE_PACKET_TYPE_SAMPLE) && (*packet_type != ECG_SLE_PACKET_TYPE_GT_HEALTH)) {
        return ECG_SLE_PACKET_ERR_TYPE;
    }
    *payload_len = ecg_sle_get_u16(&buffer[4]);
    if (*packet_type == ECG_SLE_PACKET_TYPE_GT_HEALTH) {
        if ((*version != ECG_SLE_PACKET_VERSION) || (buffer_len != ECG_SLE_PACKET_GT_LEN) ||
            (*payload_len != ECG_SLE_PACKET_GT_PAYLOAD_LEN)) {
            return ECG_SLE_PACKET_ERR_PAYLOAD_LEN;
        }
        checksum = ecg_sle_checksum(buffer, buffer_len - ECG_SLE_CHECKSUM_LEN);
        if (ecg_sle_get_u16(&buffer[buffer_len - ECG_SLE_CHECKSUM_LEN]) != checksum) {
            return ECG_SLE_PACKET_ERR_CHECKSUM;
        }
        return ECG_SLE_PACKET_OK;
    }
    if ((*version == ECG_SLE_PACKET_VERSION_LEGACY) &&
        ((buffer_len != ECG_SLE_PACKET_LEGACY_LEN) || (*payload_len != ECG_SLE_PACKET_ECG_PAYLOAD_LEN))) {
        return ECG_SLE_PACKET_ERR_PAYLOAD_LEN;
    }
    if ((*version == ECG_SLE_PACKET_VERSION) &&
        ((buffer_len != ECG_SLE_PACKET_LEN) || (*payload_len != ECG_SLE_PACKET_PAYLOAD_LEN))) {
        return ECG_SLE_PACKET_ERR_PAYLOAD_LEN;
    }
    checksum = ecg_sle_checksum(buffer, buffer_len - ECG_SLE_CHECKSUM_LEN);
    if (ecg_sle_get_u16(&buffer[buffer_len - ECG_SLE_CHECKSUM_LEN]) != checksum) {
        return ECG_SLE_PACKET_ERR_CHECKSUM;
    }
    return ECG_SLE_PACKET_OK;
}

errcode_t ecg_sle_encode_vital_sample(const ecg_sle_vital_sample_t *sample, uint8_t *buffer, uint16_t buffer_len)
{
    uint16_t offset = 0;

    if ((sample == NULL) || (buffer == NULL)) {
        return ERRCODE_FAIL;
    }
    if (buffer_len < ECG_SLE_PACKET_LEN) {
        return ERRCODE_FAIL;
    }

    ecg_sle_put_u16(&buffer[offset], ECG_SLE_PACKET_MAGIC);
    offset += 2;
    buffer[offset++] = ECG_SLE_PACKET_VERSION;
    buffer[offset++] = ECG_SLE_PACKET_TYPE_SAMPLE;
    ecg_sle_put_u16(&buffer[offset], ECG_SLE_PACKET_PAYLOAD_LEN);
    offset += 2;

    ecg_sle_encode_ecg_payload(&sample->ecg, buffer, &offset);
    ecg_sle_put_u32(&buffer[offset], sample->spo2.seq);
    offset += 4;
    ecg_sle_put_u32(&buffer[offset], sample->spo2.timestamp_ms);
    offset += 4;
    ecg_sle_put_i32(&buffer[offset], sample->spo2.spo2_x10);
    offset += 4;
    ecg_sle_put_u16(&buffer[offset], sample->spo2.hr_bpm);
    offset += 2;
    buffer[offset++] = ecg_sle_spo2_flags(&sample->spo2);
    buffer[offset++] = (uint8_t)sample->spo2.quality;

    ecg_sle_put_u16(&buffer[offset], ecg_sle_checksum(buffer, ECG_SLE_PACKET_LEN - ECG_SLE_CHECKSUM_LEN));
    return ERRCODE_SUCC;
}

ecg_sle_packet_status_t ecg_sle_decode_vital_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_sle_vital_sample_t *sample)
{
    uint16_t offset = ECG_SLE_HEADER_LEN;
    uint16_t payload_len = 0;
    uint8_t version = 0;
    uint8_t packet_type = 0;
    uint8_t flags;
    ecg_sle_packet_status_t status;

    if (sample == NULL) {
        return ECG_SLE_PACKET_ERR_NULL;
    }
    status = ecg_sle_validate_header(buffer, buffer_len, &version, &packet_type, &payload_len);
    if (status != ECG_SLE_PACKET_OK) {
        return status;
    }
    if (packet_type != ECG_SLE_PACKET_TYPE_SAMPLE) {
        return ECG_SLE_PACKET_ERR_TYPE;
    }

    ecg_sle_decode_ecg_payload(buffer, &offset, &sample->ecg);
    sample->spo2.seq = 0;
    sample->spo2.timestamp_ms = 0;
    sample->spo2.spo2_x10 = 0;
    sample->spo2.hr_bpm = 0;
    sample->spo2.spo2_valid = false;
    sample->spo2.hr_valid = false;
    sample->spo2.sample_valid = false;
    sample->spo2.quality = ECG_SLE_SPO2_QUALITY_UNKNOWN;

    if (version == ECG_SLE_PACKET_VERSION_LEGACY) {
        return ECG_SLE_PACKET_OK;
    }

    sample->spo2.seq = ecg_sle_get_u32(&buffer[offset]);
    offset += 4;
    sample->spo2.timestamp_ms = ecg_sle_get_u32(&buffer[offset]);
    offset += 4;
    sample->spo2.spo2_x10 = ecg_sle_get_i32(&buffer[offset]);
    offset += 4;
    sample->spo2.hr_bpm = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    flags = buffer[offset++];
    sample->spo2.quality = (ecg_sle_spo2_quality_t)buffer[offset++];
    sample->spo2.sample_valid = (flags & ECG_SLE_SPO2_FLAG_SAMPLE_VALID) != 0;
    sample->spo2.spo2_valid = (flags & ECG_SLE_SPO2_FLAG_SPO2_VALID) != 0;
    sample->spo2.hr_valid = (flags & ECG_SLE_SPO2_FLAG_HR_VALID) != 0;

    return ECG_SLE_PACKET_OK;
}

errcode_t ecg_sle_encode_gt_health_sample(const ecg_sle_gt_health_sample_t *sample, uint8_t *buffer,
    uint16_t buffer_len)
{
    uint16_t offset = 0;

    if ((sample == NULL) || (buffer == NULL)) {
        return ERRCODE_FAIL;
    }
    if (buffer_len < ECG_SLE_PACKET_GT_LEN) {
        return ERRCODE_FAIL;
    }

    ecg_sle_put_u16(&buffer[offset], ECG_SLE_PACKET_MAGIC);
    offset += 2;
    buffer[offset++] = ECG_SLE_PACKET_VERSION;
    buffer[offset++] = ECG_SLE_PACKET_TYPE_GT_HEALTH;
    ecg_sle_put_u16(&buffer[offset], ECG_SLE_PACKET_GT_PAYLOAD_LEN);
    offset += 2;
    ecg_sle_put_u32(&buffer[offset], sample->seq);
    offset += 4;
    ecg_sle_put_u32(&buffer[offset], sample->timestamp_ms);
    offset += 4;
    ecg_sle_put_u16(&buffer[offset], sample->heart_rate);
    offset += 2;
    ecg_sle_put_u16(&buffer[offset], sample->spo2);
    offset += 2;
    ecg_sle_put_u16(&buffer[offset], sample->microcirculation);
    offset += 2;
    ecg_sle_put_u16(&buffer[offset], sample->systolic_bp);
    offset += 2;
    ecg_sle_put_u16(&buffer[offset], sample->diastolic_bp);
    offset += 2;
    buffer[offset++] = ecg_sle_gt_health_flags(sample);
    buffer[offset++] = 0;
    ecg_sle_put_u16(&buffer[offset], ecg_sle_checksum(buffer, ECG_SLE_PACKET_GT_LEN - ECG_SLE_CHECKSUM_LEN));
    return ERRCODE_SUCC;
}

ecg_sle_packet_status_t ecg_sle_decode_gt_health_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_sle_gt_health_sample_t *sample)
{
    uint16_t offset = ECG_SLE_HEADER_LEN;
    uint16_t payload_len = 0;
    uint8_t version = 0;
    uint8_t packet_type = 0;
    uint8_t flags;
    ecg_sle_packet_status_t status;

    if (sample == NULL) {
        return ECG_SLE_PACKET_ERR_NULL;
    }
    status = ecg_sle_validate_header(buffer, buffer_len, &version, &packet_type, &payload_len);
    if (status != ECG_SLE_PACKET_OK) {
        return status;
    }
    if (packet_type != ECG_SLE_PACKET_TYPE_GT_HEALTH) {
        return ECG_SLE_PACKET_ERR_TYPE;
    }

    sample->seq = ecg_sle_get_u32(&buffer[offset]);
    offset += 4;
    sample->timestamp_ms = ecg_sle_get_u32(&buffer[offset]);
    offset += 4;
    sample->heart_rate = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    sample->spo2 = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    sample->microcirculation = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    sample->systolic_bp = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    sample->diastolic_bp = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    flags = buffer[offset];
    sample->heart_rate_valid = (flags & ECG_SLE_GT_FLAG_HR_VALID) != 0;
    sample->spo2_valid = (flags & ECG_SLE_GT_FLAG_SPO2_VALID) != 0;
    sample->microcirculation_valid = (flags & ECG_SLE_GT_FLAG_MICRO_VALID) != 0;
    sample->bp_valid = (flags & ECG_SLE_GT_FLAG_BP_VALID) != 0;
    unused(version);
    unused(payload_len);
    return ECG_SLE_PACKET_OK;
}

uint8_t ecg_sle_get_packet_type(const uint8_t *buffer, uint16_t buffer_len)
{
    if ((buffer == NULL) || (buffer_len < ECG_SLE_HEADER_LEN)) {
        return 0;
    }
    if (ecg_sle_get_u16(&buffer[0]) != ECG_SLE_PACKET_MAGIC) {
        return 0;
    }
    return buffer[3];
}

errcode_t ecg_sle_encode_sample(const ecg_monitor_sample_t *sample, uint8_t *buffer, uint16_t buffer_len)
{
    ecg_sle_vital_sample_t vital = {0};

    if (sample == NULL) {
        return ERRCODE_FAIL;
    }
    vital.ecg = *sample;
    vital.spo2.quality = ECG_SLE_SPO2_QUALITY_UNKNOWN;
    return ecg_sle_encode_vital_sample(&vital, buffer, buffer_len);
}

ecg_sle_packet_status_t ecg_sle_decode_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_monitor_sample_t *sample)
{
    ecg_sle_vital_sample_t vital = {0};
    ecg_sle_packet_status_t status;

    if (sample == NULL) {
        return ECG_SLE_PACKET_ERR_NULL;
    }
    status = ecg_sle_decode_vital_sample(buffer, buffer_len, &vital);
    if (status != ECG_SLE_PACKET_OK) {
        return status;
    }
    *sample = vital.ecg;
    return ECG_SLE_PACKET_OK;
}

const char *ecg_sle_packet_status_to_string(ecg_sle_packet_status_t status)
{
    switch (status) {
        case ECG_SLE_PACKET_OK:
            return "OK";
        case ECG_SLE_PACKET_ERR_NULL:
            return "NULL";
        case ECG_SLE_PACKET_ERR_LEN:
            return "LEN";
        case ECG_SLE_PACKET_ERR_MAGIC:
            return "MAGIC";
        case ECG_SLE_PACKET_ERR_VERSION:
            return "VERSION";
        case ECG_SLE_PACKET_ERR_TYPE:
            return "TYPE";
        case ECG_SLE_PACKET_ERR_PAYLOAD_LEN:
            return "PAYLOAD_LEN";
        case ECG_SLE_PACKET_ERR_CHECKSUM:
            return "CHECKSUM";
        default:
            return "UNKNOWN";
    }
}

const char *ecg_sle_spo2_quality_to_string(ecg_sle_spo2_quality_t quality)
{
    switch (quality) {
        case ECG_SLE_SPO2_QUALITY_WARMUP:
            return "WARMUP";
        case ECG_SLE_SPO2_QUALITY_OK:
            return "OK";
        case ECG_SLE_SPO2_QUALITY_NO_FINGER:
            return "NO_FINGER";
        case ECG_SLE_SPO2_QUALITY_LOW_SIGNAL:
            return "LOW_SIGNAL";
        case ECG_SLE_SPO2_QUALITY_SATURATED:
            return "SATURATED";
        case ECG_SLE_SPO2_QUALITY_NO_PULSE:
            return "NO_PULSE";
        case ECG_SLE_SPO2_QUALITY_UNSTABLE:
            return "UNSTABLE";
        case ECG_SLE_SPO2_QUALITY_SENSOR_OFF:
            return "SENSOR_OFF";
        case ECG_SLE_SPO2_QUALITY_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}
