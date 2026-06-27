#include "ecg_sle_packet.h"

#include <stddef.h>

#define ECG_SLE_HEADER_LEN 6
#define ECG_SLE_CHECKSUM_LEN 2

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


errcode_t ecg_sle_encode_sample(const ecg_monitor_sample_t *sample, uint8_t *buffer, uint16_t buffer_len)
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

    ecg_sle_put_u32(&buffer[offset], sample->seq);
    offset += 4;
    ecg_sle_put_u32(&buffer[offset], sample->timestamp_ms);
    offset += 4;
    ecg_sle_put_u16(&buffer[offset], sample->voltage_mv);
    offset += 2;
    ecg_sle_put_i16(&buffer[offset], sample->baseline_mv);
    offset += 2;
    ecg_sle_put_i16(&buffer[offset], sample->ecg_mv);
    offset += 2;
    ecg_sle_put_i16(&buffer[offset], sample->filtered_mv);
    offset += 2;
    ecg_sle_put_i16(&buffer[offset], sample->display_mv);
    offset += 2;
    buffer[offset++] = sample->r_peak;
    buffer[offset++] = (uint8_t)sample->quality;
    ecg_sle_put_u16(&buffer[offset], sample->rr_interval_ms);
    offset += 2;
    ecg_sle_put_u16(&buffer[offset], sample->bpm);
    offset += 2;

    ecg_sle_put_u16(&buffer[offset], ecg_sle_checksum(buffer, ECG_SLE_PACKET_LEN - ECG_SLE_CHECKSUM_LEN));
    return ERRCODE_SUCC;
}

ecg_sle_packet_status_t ecg_sle_decode_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_monitor_sample_t *sample)
{
    uint16_t offset = 0;
    uint16_t checksum;

    if ((buffer == NULL) || (sample == NULL)) {
        return ECG_SLE_PACKET_ERR_NULL;
    }
    if (buffer_len != ECG_SLE_PACKET_LEN) {
        return ECG_SLE_PACKET_ERR_LEN;
    }
    if (ecg_sle_get_u16(&buffer[offset]) != ECG_SLE_PACKET_MAGIC) {
        return ECG_SLE_PACKET_ERR_MAGIC;
    }
    offset += 2;
    if (buffer[offset++] != ECG_SLE_PACKET_VERSION) {
        return ECG_SLE_PACKET_ERR_VERSION;
    }
    if (buffer[offset++] != ECG_SLE_PACKET_TYPE_SAMPLE) {
        return ECG_SLE_PACKET_ERR_TYPE;
    }
    if (ecg_sle_get_u16(&buffer[offset]) != ECG_SLE_PACKET_PAYLOAD_LEN) {
        return ECG_SLE_PACKET_ERR_PAYLOAD_LEN;
    }
    offset += 2;
    checksum = ecg_sle_checksum(buffer, ECG_SLE_PACKET_LEN - ECG_SLE_CHECKSUM_LEN);
    if (ecg_sle_get_u16(&buffer[ECG_SLE_PACKET_LEN - ECG_SLE_CHECKSUM_LEN]) != checksum) {
        return ECG_SLE_PACKET_ERR_CHECKSUM;
    }

    sample->seq = ecg_sle_get_u32(&buffer[offset]);
    offset += 4;
    sample->timestamp_ms = ecg_sle_get_u32(&buffer[offset]);
    offset += 4;
    sample->voltage_mv = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    sample->baseline_mv = ecg_sle_get_i16(&buffer[offset]);
    offset += 2;
    sample->ecg_mv = ecg_sle_get_i16(&buffer[offset]);
    offset += 2;
    sample->filtered_mv = ecg_sle_get_i16(&buffer[offset]);
    offset += 2;
    sample->display_mv = ecg_sle_get_i16(&buffer[offset]);
    offset += 2;
    sample->r_peak = buffer[offset++];
    sample->quality = (ecg_signal_quality_t)buffer[offset++];
    sample->rr_interval_ms = ecg_sle_get_u16(&buffer[offset]);
    offset += 2;
    sample->bpm = ecg_sle_get_u16(&buffer[offset]);

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
