#ifndef TJC_DISPLAY_H
#define TJC_DISPLAY_H

#include <stdint.h>

#include "common_def.h"
#include "../9_ad8232_adc/ecg_processor.h"
#include "errcode.h"

errcode_t tjc_display_init(void);
void tjc_display_clear_wave(void);
void tjc_display_send_sample(const ecg_monitor_sample_t *sample);
void tjc_display_send_bpm(uint16_t bpm);
void tjc_display_send_patient_info(const char *name, const char *record_no, const char *gender,
    const char *age, const char *phone, const char *note);
void tjc_display_send_test_pattern(void);

#endif
