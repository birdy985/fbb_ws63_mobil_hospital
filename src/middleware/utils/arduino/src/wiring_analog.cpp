/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring_analog.cpp
 * @brief Analog IO functions implementation for Arduino compatibility layer
 * @version 4.0
 * @date 2026-04-29
 */

#include "Arduino.h"
#include "pins_arduino.h"

/* Magic-number macros for ADC/PWM */
#define ADC_DEFAULT_RESOLUTION 10
#define ADC_HW_BITS 12
#define ADC_HW_MAX_VALUE ((1UL << ADC_HW_BITS) - 1)
#define ADC_HW_HALF_MAX (ADC_HW_MAX_VALUE / 2)
#define ADC_MIN_RES_BITS 1
#define ADC_MAX_RES_BITS 32

#define PWM_MAX_VALUE 255U
#define PWM_HALF_THRESHOLD 127
#define PWM_DEFAULT_FREQ 1000U
#define PWM_DEFAULT_CLOCK_FREQ 1000000U
#define PWM_MIN_TOTAL_CYCLES 1000U
#define MAX_PWM_CHANNELS 16U

#if defined(CONFIG_ADC_SUPPORT) || defined(CONFIG_PWM_SUPPORT)
#include "driver/adc.h"
#include "driver/pwm.h"
#include "driver/gpio.h"
#endif

// Default ADC resolution for standard Arduino compatibility (10-bit → 0-1023)
static uint8_t s_adc_resolution = ADC_DEFAULT_RESOLUTION;
// Stored analog reference mode (for compatibility tracking)
static uint8_t s_analog_reference = DEFAULT;

#if defined(CONFIG_ADC_SUPPORT)
static adc_clock_t s_adc_clock = ADC_CLOCK_250KHZ;
static bool s_adc_initialized = false;
#endif

#if defined(CONFIG_PWM_SUPPORT)
// PWM channel tracking for analogWrite
static bool s_pwm_channel_open[MAX_PWM_CHANNELS] = {false};
#endif

// ============================================================================
// ADC functions
// ============================================================================

#if defined(CONFIG_ADC_SUPPORT)

/* *
 * @brief Initialize ADC if not already initialized
 * @return true on success, false on failure
 */
static bool adc_init_if_needed(void)
{
    if (!s_adc_initialized) {
        errcode_t ret = uapi_adc_init(s_adc_clock);
        if (ret != ERRCODE_SUCC) {
            return false; // Init failed — do NOT set s_adc_initialized
        }
        s_adc_initialized = true;
    }
    return true;
}

/* *
 * @brief Reset ADC state (allows retry after init failure)
 * After calling this, adc_init_if_needed() will attempt to reinitialize
 */
static void reset_adc(void)
{
    if (s_adc_initialized) {
        uapi_adc_deinit();
        s_adc_initialized = false;
    }
}

/* *
 * @brief Read analog value from a pin
 * @param pin - Arduino analog pin number (A0-A7)
 * @return int - ADC value scaled to configured resolution
 */
int analogRead(uint8_t pin)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return 0; // Invalid pin
    }

    // Check if it's an analog pin
    if (!IS_ANALOG_PIN(pin)) {
        return 0; // Not an analog pin
    }

    uint8_t adc_channel = analogPinToChannel(pin);
    if (adc_channel == NOT_A_PIN) {
        return 0; // Invalid channel
    }

    // Initialize ADC if needed
    if (!adc_init_if_needed()) {
        return 0;
    }

    // Open channel
    errcode_t ret = uapi_adc_open_channel(adc_channel);
    if (ret != ERRCODE_SUCC) {
        return 0;
    }

    // Power on ADC in GADC mode for manual sample
    uapi_adc_power_en(AFE_GADC_MODE, true);

    // Read value (ws63 ADC is 12-bit: 0-4095)
    int32_t raw_value = uapi_adc_manual_sample(adc_channel);

    // Close channel (check return value)
    errcode_t close_ret = uapi_adc_close_channel(adc_channel);
    if (close_ret != ERRCODE_SUCC) {
        // Close failed, but we still have a valid sample; log and continue
    }

    // Power down ADC to save power in low-power scenarios
    uapi_adc_power_en(AFE_GADC_MODE, false);

    if (raw_value < 0) {
        return 0; // Error
    }

    // Scale to configured resolution
    // ws63 ADC is ADC_HW_BITS-bit (0-ADC_HW_MAX_VALUE), scale if different resolution requested
    if (s_adc_resolution < ADC_HW_BITS) {
        // Downscale: shift right to reduce resolution
        // e.g. 10-bit: shift right by (12-10)
        raw_value >>= (ADC_HW_BITS - s_adc_resolution);
    } else if (s_adc_resolution > ADC_HW_BITS) {
        // Upscale: multiply and shift to expand range
        // Formula: raw * (max_target / ADC_HW_MAX_VALUE) with rounding
        uint32_t max_target = (1UL << s_adc_resolution) - 1;
        raw_value = (int32_t)(((uint32_t)raw_value * max_target + ADC_HW_HALF_MAX) / ADC_HW_MAX_VALUE);
    }
    // s_adc_resolution == 12: return as-is

    return (int)raw_value;
}

#else
/* !CONFIG_ADC_SUPPORT */

/* *
 * @brief Stub: ADC not supported, return 0
 */
int analogRead(uint8_t pin)
{
    (void)pin;
    return 0;
}

#endif
/* CONFIG_ADC_SUPPORT */

// ============================================================================
// analogWrite (PWM)
// ============================================================================

#if defined(CONFIG_PWM_SUPPORT)

/* *
 * @brief Write analog value to a pin (PWM)
 * @param pin - Arduino pin number (must support PWM)
 * @param val - PWM value (0-255)
 */
void analogWrite(uint8_t pin, int val)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return; // Invalid pin
    }

    // Check if pin supports PWM
    if (!pinSupportsPWM(pin)) {
        // Fallback: GPIO digital output (0 or 1)
        uint8_t hw_pin = digitalPinToPin(pin);
        if (hw_pin == NOT_A_PIN)
            return;
#if defined(CONFIG_GPIO_SUPPORT)
        uapi_gpio_set_dir((pin_t)hw_pin, GPIO_DIRECTION_OUTPUT);
        uapi_gpio_set_val((pin_t)hw_pin, (val > PWM_HALF_THRESHOLD) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
#else
        (void)val; // No GPIO support, cannot fallback
#endif
        return;
    }

    uint8_t pwm_channel = digitalPinToPWMChannel(pin);
    if (pwm_channel == NOT_ON_TIMER || pwm_channel >= MAX_PWM_CHANNELS) {
        return; // Invalid PWM channel
    }

    // Clamp value to 0-PWM_MAX_VALUE
    if (val < 0)
        val = 0;
    if (val > (int)PWM_MAX_VALUE)
        val = (int)PWM_MAX_VALUE;

    // For analogWrite, use a fixed frequency (~1kHz for Arduino compatibility)
    // and vary the duty cycle based on val (0-PWM_MAX_VALUE)
    uint32_t target_freq = PWM_DEFAULT_FREQ; // 1kHz default

    // Get PWM working frequency (Hz) — this returns the PWM clock frequency
    uint32_t pwm_freq = uapi_pwm_get_frequency(pwm_channel);
    if (pwm_freq == 0) {
        pwm_freq = PWM_DEFAULT_CLOCK_FREQ; // Default 1MHz PWM clock if not configured
    }

    // Calculate clock cycle counts
    // NOTE: Integer division may produce slight frequency inaccuracies.
    // For example, pwm_freq=1000000, target_freq=440 → total_cycles=2272
    // Actual frequency = 1000000/2272 ≈ 440.14 Hz (close but not exact).
    // This is inherent to integer math and acceptable for Arduino PWM use.
    uint32_t total_cycles = pwm_freq / target_freq;
    if (total_cycles == 0) {
        total_cycles = PWM_MIN_TOTAL_CYCLES; // Safety minimum
    }

    // High time cycles = (val / PWM_MAX_VALUE) * total_cycles
    uint32_t high_time = ((uint32_t)val * total_cycles) / PWM_MAX_VALUE;
    uint32_t low_time = total_cycles - high_time;

    if (!s_pwm_channel_open[pwm_channel]) {
        // First time: open and start PWM
        pwm_config_t cfg;
        cfg.high_time = high_time;
        cfg.low_time = low_time;
        cfg.offset_time = 0;
        cfg.cycles = 0; // Continuous
        cfg.repeat = true;

        errcode_t ret = uapi_pwm_open(pwm_channel, &cfg);
        if (ret != ERRCODE_SUCC) {
            return;
        }
        s_pwm_channel_open[pwm_channel] = true;
        uapi_pwm_start(pwm_channel);
    } else {
        // Channel already open: update duty cycle
#if defined(CONFIG_PWM_USING_V150)
        // V150 API: use stop + update_duty_ratio + start
        uapi_pwm_stop(pwm_channel);
        uapi_pwm_update_duty_ratio(pwm_channel, low_time, high_time);
        uapi_pwm_start(pwm_channel);
#elif defined(CONFIG_PWM_USING_V151)
        // V151 API: use update_cfg
        pwm_config_t cfg;
        cfg.high_time = high_time;
        cfg.low_time = low_time;
        cfg.offset_time = 0;
        cfg.cycles = 0;
        cfg.repeat = true;
        uapi_pwm_update_cfg(pwm_channel, &cfg);
#else
        // No update API: close and reopen
        uapi_pwm_close(pwm_channel);
        s_pwm_channel_open[pwm_channel] = false;

        pwm_config_t cfg;
        cfg.high_time = high_time;
        cfg.low_time = low_time;
        cfg.offset_time = 0;
        cfg.cycles = 0;
        cfg.repeat = true;

        errcode_t ret = uapi_pwm_open(pwm_channel, &cfg);
        if (ret != ERRCODE_SUCC) {
            return;
        }
        s_pwm_channel_open[pwm_channel] = true;
        uapi_pwm_start(pwm_channel);
#endif
    }
}

#else
/* !CONFIG_PWM_SUPPORT */

/* *
 * @brief Stub/fallback: PWM not supported, use GPIO
 */
void analogWrite(uint8_t pin, int val)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return;
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return;
    }

// GPIO fallback: treat as digital output (HIGH for val > 127, LOW otherwise)
#if defined(CONFIG_GPIO_SUPPORT)
    uapi_gpio_set_dir((pin_t)hw_pin, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val((pin_t)hw_pin, (val > PWM_HALF_THRESHOLD) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
#else
    (void)val;
#endif
}

#endif /* CONFIG_PWM_SUPPORT */

// ============================================================================
// analogReference
// ============================================================================

/* *
 * @brief Set analog reference voltage
 * @param mode - DEFAULT, EXTERNAL, or INTERNAL
 *
 * ws63 ADC has fixed reference voltage. This function:
 * - Records the mode for compatibility
 * - When CONFIG_ADC_SUPPORT_REF_VOLTAGE is defined, attempts to configure
 * - Otherwise, no-op stub
 */
void analogReference(uint8_t mode)
{
    if (mode != DEFAULT && mode != EXTERNAL && mode != INTERNAL) {
        return; // Invalid mode
    }

    s_analog_reference = mode;

#if defined(CONFIG_ADC_SUPPORT) && defined(CONFIG_ADC_SUPPORT_REF_VOLTAGE)
    // When hardware supports configurable reference voltage
    // Implementation depends on specific ws63 ADC reference config API
    // Currently ws63 ADC reference is fixed, this block is reserved
    // for future hardware variants that support reference configuration
    // Example (if API exists):
    uapi_adc_set_reference_voltage(mode);
#endif
}

// ============================================================================
// analogReadResolution
// ============================================================================

/* *
 * @brief Set analog read resolution
 * @param bits - Resolution in bits (1-32)
 *
 * ws63 ADC is 12-bit hardware. This function sets the software resolution:
 * - 1-11 bits: readings are right-shifted (downscaled)
 * - 12 bits: native resolution (0-4095)
 * - 13-32 bits: readings are upscaled by multiplication
 *
 * Default is 10 bits (standard Arduino: 0-1023)
 */
void analogReadResolution(uint8_t bits)
{
    if (bits < ADC_MIN_RES_BITS) {
        bits = ADC_MIN_RES_BITS;
    } else if (bits > ADC_MAX_RES_BITS) {
        bits = ADC_MAX_RES_BITS;
    }

    s_adc_resolution = bits;
}
