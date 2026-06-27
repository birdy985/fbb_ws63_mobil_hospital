#ifndef ECG_SLE_PACKET_H
#define ECG_SLE_PACKET_H

#include <stdint.h>
#include "../9_ad8232_adc/ecg_processor.h"
#include "errcode.h"

#define ECG_SLE_PACKET_MAGIC        0xEC9D
#define ECG_SLE_PACKET_VERSION      1
#define ECG_SLE_PACKET_TYPE_SAMPLE  1
#define ECG_SLE_PACKET_PAYLOAD_LEN  24
#define ECG_SLE_PACKET_LEN          32

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

errcode_t ecg_sle_encode_sample(const ecg_monitor_sample_t *sample, uint8_t *buffer, uint16_t buffer_len);
ecg_sle_packet_status_t ecg_sle_decode_sample(const uint8_t *buffer, uint16_t buffer_len,
    ecg_monitor_sample_t *sample);
const char *ecg_sle_packet_status_to_string(ecg_sle_packet_status_t status);

#endif

