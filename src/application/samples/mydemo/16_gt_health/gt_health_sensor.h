#ifndef GT_HEALTH_SENSOR_H
#define GT_HEALTH_SENSOR_H

#include <stdint.h>

#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GT_HEALTH_WAVE_SAMPLE_COUNT 64

typedef struct {
    int8_t wave[GT_HEALTH_WAVE_SAMPLE_COUNT];
    uint8_t heart_rate;
    uint8_t spo2;
    uint8_t microcirculation;
    uint8_t systolic_bp;
    uint8_t diastolic_bp;
} gt_health_data_t;

errcode_t gt_health_sensor_init(void);
errcode_t gt_health_sensor_read(gt_health_data_t *data, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
