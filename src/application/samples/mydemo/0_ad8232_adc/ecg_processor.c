#include "ecg_processor.h"

#define ECG_ADC_MIN_MV                 0
#define ECG_ADC_MAX_MV                 3300
#define ECG_ADC_RAIL_MARGIN_MV         50
#define ECG_BASELINE_IIR_SHIFT         8
#define ECG_FILTER_IIR_SHIFT           2
#define ECG_DISPLAY_IIR_SHIFT          1
#define ECG_DISPLAY_STEP_LIMIT_MV      80
#define ECG_INITIAL_THRESHOLD_MV       35
#define ECG_MIN_THRESHOLD_MV           18
#define ECG_MIN_R_PEAK_MV              25
#define ECG_REFRACTORY_MS              300
#define ECG_MIN_RR_MS                  400
#define ECG_MAX_RR_MS                  2000
#define ECG_MAX_QRS_WIDTH_MS           180
#define ECG_NO_R_PEAK_MS               2500
#define ECG_STARTUP_GRACE_MS           3000
#define ECG_WINDOW_SAMPLES             100
#define ECG_FLATLINE_RANGE_MV          8
#define ECG_NOISY_JUMP_MV              120
#define ECG_NOISY_COUNT_LIMIT          20

typedef struct {
    uint8_t initialized;
    uint32_t first_timestamp_ms;
    int32_t baseline_q8_mv;
    int32_t filtered_mv;
    int32_t display_mv;
    int16_t previous_filtered_mv;
    uint8_t peak_candidate_active;
    uint16_t candidate_level_mv;
    int16_t candidate_filtered_mv;
    uint32_t candidate_timestamp_ms;
    uint32_t last_r_timestamp_ms;
    uint16_t last_rr_interval_ms;
    uint16_t bpm;
    uint16_t threshold_mv;
    uint16_t peak_level_mv;
    uint16_t noise_level_mv;
    uint16_t window_min_mv;
    uint16_t window_max_mv;
    uint16_t window_count;
    uint16_t noisy_count;
    uint16_t previous_voltage_mv;
} ecg_processor_state_t;

static ecg_processor_state_t g_ecg_state;

static uint16_t ecg_abs_i16(int16_t value)
{
    return (uint16_t)((value < 0) ? -value : value);
}

static int16_t ecg_clamp_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static uint16_t ecg_clamp_u16(uint32_t value)
{
    if (value > UINT16_MAX) {
        return UINT16_MAX;
    }
    return (uint16_t)value;
}

static int32_t ecg_limit_step_i32(int32_t current, int32_t target, int32_t step_limit)
{
    int32_t delta = target - current;

    if (delta > step_limit) {
        return current + step_limit;
    }
    if (delta < -step_limit) {
        return current - step_limit;
    }
    return target;
}

static uint16_t ecg_update_threshold(uint16_t peak_level_mv, uint16_t noise_level_mv)
{
    uint16_t threshold = ECG_INITIAL_THRESHOLD_MV;

    if (peak_level_mv > noise_level_mv) {
        threshold = noise_level_mv + (uint16_t)(((peak_level_mv - noise_level_mv) * 5U) / 8U);
    }
    if (threshold < ECG_MIN_THRESHOLD_MV) {
        threshold = ECG_MIN_THRESHOLD_MV;
    }
    return threshold;
}

static void ecg_reset_peak_detector(void)
{
    g_ecg_state.peak_candidate_active = 0;
    g_ecg_state.peak_level_mv = ECG_INITIAL_THRESHOLD_MV * 2;
    g_ecg_state.noise_level_mv = ECG_INITIAL_THRESHOLD_MV / 2;
    g_ecg_state.threshold_mv = ECG_INITIAL_THRESHOLD_MV;
}

static void ecg_update_window(uint16_t voltage_mv)
{
    if (g_ecg_state.window_count == 0) {
        g_ecg_state.window_min_mv = voltage_mv;
        g_ecg_state.window_max_mv = voltage_mv;
    } else {
        if (voltage_mv < g_ecg_state.window_min_mv) {
            g_ecg_state.window_min_mv = voltage_mv;
        }
        if (voltage_mv > g_ecg_state.window_max_mv) {
            g_ecg_state.window_max_mv = voltage_mv;
        }
    }

    if (g_ecg_state.initialized != 0) {
        uint16_t jump_mv = (voltage_mv > g_ecg_state.previous_voltage_mv) ?
            (voltage_mv - g_ecg_state.previous_voltage_mv) : (g_ecg_state.previous_voltage_mv - voltage_mv);
        if (jump_mv > ECG_NOISY_JUMP_MV) {
            g_ecg_state.noisy_count++;
        }
    }

    g_ecg_state.previous_voltage_mv = voltage_mv;
    g_ecg_state.window_count++;
}

static ecg_signal_quality_t ecg_assess_quality(uint16_t voltage_mv, uint32_t timestamp_ms)
{
    uint16_t range_mv = g_ecg_state.window_max_mv - g_ecg_state.window_min_mv;
    uint8_t window_ready = (g_ecg_state.window_count >= ECG_WINDOW_SAMPLES) ? 1 : 0;
    uint32_t elapsed_ms = timestamp_ms - g_ecg_state.first_timestamp_ms;
    uint32_t since_last_peak_ms = timestamp_ms - g_ecg_state.last_r_timestamp_ms;
    ecg_signal_quality_t quality = ECG_QUALITY_GOOD;

    if ((voltage_mv <= (ECG_ADC_MIN_MV + ECG_ADC_RAIL_MARGIN_MV)) ||
        (voltage_mv >= (ECG_ADC_MAX_MV - ECG_ADC_RAIL_MARGIN_MV))) {
        quality = ECG_QUALITY_SATURATED;
    } else if ((window_ready != 0) && (range_mv < ECG_FLATLINE_RANGE_MV)) {
        quality = ECG_QUALITY_FLATLINE;
    } else if ((window_ready != 0) && (g_ecg_state.noisy_count > ECG_NOISY_COUNT_LIMIT)) {
        quality = ECG_QUALITY_NOISY;
    } else if ((elapsed_ms > ECG_STARTUP_GRACE_MS) &&
        ((g_ecg_state.last_r_timestamp_ms == 0) || (since_last_peak_ms > ECG_NO_R_PEAK_MS))) {
        quality = ECG_QUALITY_NO_R_PEAK;
    }

    if (window_ready != 0) {
        g_ecg_state.window_count = 0;
        g_ecg_state.noisy_count = 0;
        if (quality == ECG_QUALITY_NO_R_PEAK) {
            ecg_reset_peak_detector();
        }
    }

    return quality;
}

void ecg_processor_init(void)
{
    g_ecg_state.initialized = 0;
    g_ecg_state.first_timestamp_ms = 0;
    g_ecg_state.baseline_q8_mv = 0;
    g_ecg_state.filtered_mv = 0;
    g_ecg_state.display_mv = 0;
    g_ecg_state.previous_filtered_mv = 0;
    g_ecg_state.peak_candidate_active = 0;
    g_ecg_state.candidate_level_mv = 0;
    g_ecg_state.candidate_filtered_mv = 0;
    g_ecg_state.candidate_timestamp_ms = 0;
    g_ecg_state.last_r_timestamp_ms = 0;
    g_ecg_state.last_rr_interval_ms = 0;
    g_ecg_state.bpm = 0;
    g_ecg_state.threshold_mv = ECG_INITIAL_THRESHOLD_MV;
    g_ecg_state.peak_level_mv = ECG_INITIAL_THRESHOLD_MV * 2;
    g_ecg_state.noise_level_mv = ECG_INITIAL_THRESHOLD_MV / 2;
    g_ecg_state.window_min_mv = ECG_ADC_MAX_MV;
    g_ecg_state.window_max_mv = ECG_ADC_MIN_MV;
    g_ecg_state.window_count = 0;
    g_ecg_state.noisy_count = 0;
    g_ecg_state.previous_voltage_mv = 0;
}

void ecg_processor_process(const ecg_input_sample_t *input, ecg_monitor_sample_t *output)
{
    int32_t baseline_mv;
    int32_t ecg_mv;
    int32_t display_target_mv;
    uint16_t signal_level_mv;
    uint32_t elapsed_from_peak_ms;
    uint32_t candidate_width_ms;
    uint8_t r_peak = 0;
    uint8_t candidate_is_positive_peak = 0;
    uint8_t candidate_is_negative_peak = 0;
    uint8_t peak_accepted = 0;
    ecg_signal_quality_t quality;

    if ((input == 0) || (output == 0)) {
        return;
    }

    if (g_ecg_state.initialized == 0) {
        g_ecg_state.initialized = 1;
        g_ecg_state.first_timestamp_ms = input->timestamp_ms;
        g_ecg_state.baseline_q8_mv = ((int32_t)input->voltage_mv) << ECG_BASELINE_IIR_SHIFT;
        g_ecg_state.filtered_mv = 0;
        g_ecg_state.display_mv = 0;
        g_ecg_state.previous_filtered_mv = 0;
        g_ecg_state.previous_voltage_mv = input->voltage_mv;
    }

    g_ecg_state.baseline_q8_mv +=
        ((((int32_t)input->voltage_mv) << ECG_BASELINE_IIR_SHIFT) - g_ecg_state.baseline_q8_mv) >>
        ECG_BASELINE_IIR_SHIFT;
    baseline_mv = g_ecg_state.baseline_q8_mv >> ECG_BASELINE_IIR_SHIFT;
    ecg_mv = (int32_t)input->voltage_mv - baseline_mv;
    g_ecg_state.filtered_mv += (ecg_mv - g_ecg_state.filtered_mv) >> ECG_FILTER_IIR_SHIFT;
    display_target_mv = ecg_limit_step_i32(g_ecg_state.display_mv, ecg_mv, ECG_DISPLAY_STEP_LIMIT_MV);
    g_ecg_state.display_mv += (display_target_mv - g_ecg_state.display_mv) >> ECG_DISPLAY_IIR_SHIFT;
    signal_level_mv = ecg_abs_i16(ecg_clamp_i16(g_ecg_state.filtered_mv));

    elapsed_from_peak_ms = input->timestamp_ms - g_ecg_state.last_r_timestamp_ms;
    if ((signal_level_mv > g_ecg_state.threshold_mv) &&
        ((g_ecg_state.last_r_timestamp_ms == 0) || (elapsed_from_peak_ms >= ECG_REFRACTORY_MS)) &&
        ((g_ecg_state.peak_candidate_active == 0) ||
        (signal_level_mv >= ecg_abs_i16(g_ecg_state.previous_filtered_mv)))) {
        if (g_ecg_state.peak_candidate_active == 0) {
            g_ecg_state.peak_candidate_active = 1;
            g_ecg_state.candidate_level_mv = signal_level_mv;
            g_ecg_state.candidate_filtered_mv = ecg_clamp_i16(g_ecg_state.filtered_mv);
            g_ecg_state.candidate_timestamp_ms = input->timestamp_ms;
        } else if (signal_level_mv > g_ecg_state.candidate_level_mv) {
            g_ecg_state.candidate_level_mv = signal_level_mv;
            g_ecg_state.candidate_filtered_mv = ecg_clamp_i16(g_ecg_state.filtered_mv);
            g_ecg_state.candidate_timestamp_ms = input->timestamp_ms;
        }
    } else if (g_ecg_state.peak_candidate_active != 0) {
        candidate_width_ms = input->timestamp_ms - g_ecg_state.candidate_timestamp_ms;
        candidate_is_positive_peak = (g_ecg_state.candidate_filtered_mv >= ECG_MIN_R_PEAK_MV) &&
            (g_ecg_state.filtered_mv < g_ecg_state.previous_filtered_mv);
        candidate_is_negative_peak = (g_ecg_state.candidate_filtered_mv <= -ECG_MIN_R_PEAK_MV) &&
            (g_ecg_state.filtered_mv > g_ecg_state.previous_filtered_mv);
        if (candidate_is_positive_peak || candidate_is_negative_peak || (signal_level_mv < (g_ecg_state.threshold_mv / 2)) ||
            (candidate_width_ms > ECG_MAX_QRS_WIDTH_MS)) {
            if ((candidate_width_ms <= ECG_MAX_QRS_WIDTH_MS) &&
                ((candidate_is_positive_peak != 0) || (candidate_is_negative_peak != 0))) {
                if (g_ecg_state.last_r_timestamp_ms == 0) {
                    peak_accepted = 1;
                } else {
                    uint32_t rr_ms = g_ecg_state.candidate_timestamp_ms - g_ecg_state.last_r_timestamp_ms;
                    if ((rr_ms >= ECG_MIN_RR_MS) && (rr_ms <= ECG_MAX_RR_MS)) {
                        g_ecg_state.last_rr_interval_ms = (uint16_t)rr_ms;
                        g_ecg_state.bpm = ecg_clamp_u16(60000U / rr_ms);
                        peak_accepted = 1;
                    }
                }
                if (peak_accepted != 0) {
                    r_peak = 1;
                    g_ecg_state.peak_level_mv =
                        (uint16_t)(((uint32_t)g_ecg_state.peak_level_mv * 7 + g_ecg_state.candidate_level_mv) / 8);
                    g_ecg_state.last_r_timestamp_ms = g_ecg_state.candidate_timestamp_ms;
                } else {
                    g_ecg_state.noise_level_mv =
                        (uint16_t)(((uint32_t)g_ecg_state.noise_level_mv * 7 + g_ecg_state.candidate_level_mv) / 8);
                }
            } else {
                g_ecg_state.noise_level_mv =
                    (uint16_t)(((uint32_t)g_ecg_state.noise_level_mv * 7 + g_ecg_state.candidate_level_mv) / 8);
            }
            g_ecg_state.peak_candidate_active = 0;
        }
    } else {
        g_ecg_state.noise_level_mv = (uint16_t)(((uint32_t)g_ecg_state.noise_level_mv * 15 + signal_level_mv) / 16);
    }

    g_ecg_state.threshold_mv = ecg_update_threshold(g_ecg_state.peak_level_mv, g_ecg_state.noise_level_mv);
    ecg_update_window(input->voltage_mv);
    g_ecg_state.previous_filtered_mv = ecg_clamp_i16(g_ecg_state.filtered_mv);
    quality = ecg_assess_quality(input->voltage_mv, input->timestamp_ms);

    output->seq = input->seq;
    output->timestamp_ms = input->timestamp_ms;
    output->voltage_mv = input->voltage_mv;
    output->baseline_mv = ecg_clamp_i16(baseline_mv);
    output->ecg_mv = ecg_clamp_i16(ecg_mv);
    output->filtered_mv = ecg_clamp_i16(g_ecg_state.filtered_mv);
    output->display_mv = ecg_clamp_i16(g_ecg_state.display_mv);
    output->r_peak = r_peak;
    output->rr_interval_ms = g_ecg_state.last_rr_interval_ms;
    output->bpm = g_ecg_state.bpm;
    output->quality = quality;
}

const char *ecg_quality_to_string(ecg_signal_quality_t quality)
{
    switch (quality) {
        case ECG_QUALITY_GOOD:
            return "GOOD";
        case ECG_QUALITY_NOISY:
            return "NOISY";
        case ECG_QUALITY_SATURATED:
            return "SATURATED";
        case ECG_QUALITY_FLATLINE:
            return "FLATLINE";
        case ECG_QUALITY_NO_R_PEAK:
            return "NO_R_PEAK";
        default:
            return "UNKNOWN";
    }
}
