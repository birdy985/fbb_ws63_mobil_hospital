#ifndef ECG_PROCESSOR_H
#define ECG_PROCESSOR_H

#include <stdint.h>

typedef enum {
    ECG_QUALITY_GOOD = 0,
    ECG_QUALITY_NOISY,
    ECG_QUALITY_SATURATED,
    ECG_QUALITY_FLATLINE,
    ECG_QUALITY_NO_R_PEAK,
} ecg_signal_quality_t;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t voltage_mv;
} ecg_input_sample_t;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t voltage_mv;
    int16_t baseline_mv;
    int16_t ecg_mv;
    int16_t filtered_mv;
    int16_t display_mv;
    uint8_t r_peak;
    uint16_t rr_interval_ms;
    uint16_t bpm;
    ecg_signal_quality_t quality;
} ecg_monitor_sample_t;

void ecg_processor_init(void);
void ecg_processor_process(const ecg_input_sample_t *input, ecg_monitor_sample_t *output);
const char *ecg_quality_to_string(ecg_signal_quality_t quality);

#endif
