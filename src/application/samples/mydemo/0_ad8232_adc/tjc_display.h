#ifndef TJC_DISPLAY_H
#define TJC_DISPLAY_H

#include <stdint.h>

#include "common_def.h"
#include "ecg_processor.h"
#include "errcode.h"

errcode_t tjc_display_init(void);
void tjc_display_clear_wave(void);
void tjc_display_send_sample(const ecg_monitor_sample_t *sample);
void tjc_display_send_bpm(uint16_t bpm);
void tjc_display_send_test_pattern(void);

#endif
