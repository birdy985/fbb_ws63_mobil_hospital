#ifndef EMQX_TELEMETRY_H
#define EMQX_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    int16_t display_mv;
    uint16_t heart_rate;
    uint8_t ecg_quality;
    bool ecg_valid;
} emqx_ecg_telemetry_t;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    int32_t spo2_x10;
    uint32_t hr_bpm;
    bool spo2_valid;
    bool hr_valid;
    const char *quality;
} emqx_spo2_telemetry_t;

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
    bool sample_valid;
} emqx_gt_health_telemetry_t;

typedef struct {
    emqx_ecg_telemetry_t ecg;
    emqx_spo2_telemetry_t spo2;
    emqx_gt_health_telemetry_t gt_health;
} emqx_telemetry_snapshot_t;

void emqx_telemetry_update_ecg(const emqx_ecg_telemetry_t *sample);
void emqx_telemetry_update_spo2(const emqx_spo2_telemetry_t *sample);
void emqx_telemetry_update_gt_health(const emqx_gt_health_telemetry_t *sample);
void emqx_telemetry_get_snapshot(emqx_telemetry_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
