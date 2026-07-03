#ifndef TJC_DISPLAY_H
#define TJC_DISPLAY_H

#include <stdint.h>

#include "common_def.h"
#include "../9_ad8232_adc/ecg_processor.h"
#include "ecg_sle_packet.h"
#include "errcode.h"

#define TJC_DISPLAY_TEXT_MAX_LEN 160

typedef struct {
    const char *name;
    const char *case_no;
    const char *gender;
    uint8_t age;
    const char *phone;
    const char *remark;
} tjc_display_patient_info_t;

typedef struct {
    ecg_monitor_sample_t ecg;
    ecg_sle_spo2_sample_t spo2;
    uint16_t systolic_bp;
    uint16_t diastolic_bp;
    uint16_t microcirculation;
    uint8_t ecg_valid;
    uint8_t bp_valid;
    uint8_t microcirculation_valid;
} tjc_display_vitals_t;

errcode_t tjc_display_init(void);
void tjc_display_clear_wave(void);
void tjc_display_send_sample(const ecg_monitor_sample_t *sample);
void tjc_display_send_bpm(uint16_t bpm);
void tjc_display_set_patient_info(const tjc_display_patient_info_t *patient);
void tjc_display_send_patient_info(const char *name, const char *record_no, const char *gender,
    const char *age, const char *phone, const char *note);
void tjc_display_update_vitals(const tjc_display_vitals_t *vitals);
void tjc_display_update_gt_health(const ecg_sle_gt_health_sample_t *sample);
void tjc_display_update_from_sle_sample(const ecg_sle_vital_sample_t *sample);
void tjc_display_send_test_pattern(void);

#endif
