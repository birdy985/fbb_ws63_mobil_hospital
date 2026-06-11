/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring_pulse.cpp
 * @brief Advanced IO functions implementation for Arduino compatibility layer
 * @version 4.0
 * @date 2026-04-29
 */

#include "Arduino.h"
#include "pins_arduino.h"

#include "driver/gpio.h"
#if defined(CONFIG_PWM_SUPPORT)
#include "driver/pwm.h"
#endif
#include "los_tick.h"
#include "los_hwi.h"

// ============================================================================
// Tone generation state
// ============================================================================

#define TONE_PWM_NONE 0xFF // Marker: not using PWM

#define PULSE_IN_DEFAULT_TIMEOUT_US 1000000UL
#define MICROSECONDS_PER_SECOND 1000000UL
#define MILLISECONDS_PER_SECOND 1000U
#define PWM_DEFAULT_CLOCK_FREQ 1000000U
#define PWM_MAX_AUTO_STOP_CYCLES 32767U
#define MAX_PWM_CHANNELS 16U
#define TONE_BURST_CYCLES 100U
#define TONE_UPDATE_BURST_CYCLES 10U
#define BYTE_BIT_COUNT 8U
#define BYTE_MSB_INDEX (BYTE_BIT_COUNT - 1U)

typedef struct {
    uint8_t pin;            // Arduino pin number
    uint8_t pwm_channel;    // PWM channel (or TONE_PWM_NONE for bit-bang)
    bool active;            // Tone is currently playing
    unsigned long end_time; // millis() when tone should stop (0 = continuous)
    bool has_duration;      // Whether tone has a finite duration
    unsigned int frequency; // Current frequency (for bit-bang timer)
} tone_state_t;

static tone_state_t s_tone_state = {
    0,             // pin
    TONE_PWM_NONE, // pwm_channel
    false,         // active
    0,             // end_time
    false,         // has_duration
    0              // frequency
};

// ==============================PULSE_IN_DEFAULT_TIMEOUT_US=======================================
// pulseIn / pulseInLong
// ============================================================================

/* *
 * @brief Measure the duration of a pulse on a pin
 * @param pin - Arduino pin number
 * @param state - State to measure (HIGH or LOW)
 * @param timeout - Timeout in microseconds (default 1000000)
 * @return Unsigned long - Pulse duration in microseconds, or 0 on timeout
 */
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return 0;
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return 0;
    }

    uapi_gpio_set_dir((pin_t)hw_pin, GPIO_DIRECTION_INPUT);

    unsigned long start_time = micros();

    // Wait for the pin to reach the desired state
    while (uapi_gpio_get_val((pin_t)hw_pin) != (state ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW)) {
        if ((micros() - start_time) > timeout) {
            return 0;
        }
    }

    unsigned long pulse_start = micros();

    // Wait for the pin to change state
    while (uapi_gpio_get_val((pin_t)hw_pin) == (state ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW)) {
        if ((micros() - start_time) > timeout) {
            return 0;
        }
    }

    return micros() - pulse_start;
}

/* *
 * @brief Measure the duration of a pulse with extended range using tick counts
 */
unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return 0;
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return 0;
    }

    uapi_gpio_set_dir((pin_t)hw_pin, GPIO_DIRECTION_INPUT);

    // Use SDK tick frequency instead of assuming 24MHz
    // LOS_TickCountGet() operates at LOSCFG_BASE_CORE_TICK_PER_SECOND (typically 1000 Hz)
    // For microsecond precision, use micros() instead
    unsigned long long timeout_us = (unsigned long long)timeout;

    unsigned long long start_us = micros();

    // Wait for the pin to reach the desired state
    while (uapi_gpio_get_val((pin_t)hw_pin) != (state ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW)) {
        if ((micros() - start_us) > timeout_us) {
            return 0;
        }
    }

    unsigned long long pulse_start_us = micros();

    // Wait for the pin to change state
    while (uapi_gpio_get_val((pin_t)hw_pin) == (state ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW)) {
        if ((micros() - pulse_start_us) > timeout_us) {
            return 0;
        }
    }

    return (unsigned long)(micros() - pulse_start_us);
}

// ============================================================================
// tone / noTone
// ============================================================================

/* *
 * @brief Generate a square wave tone on a pin using PWM
 *
 * PWM path: uses uapi_pwm_open/start with 50% duty cycle at target frequency.
 * CONFIG_PWM_USING_V150/V151 APIs used when available for stop/update.
 */
#if defined(CONFIG_PWM_SUPPORT)
static bool tone_pwm_start(uint8_t pin, uint8_t pwm_channel, unsigned int frequency, unsigned long duration)
{
    // Get PWM working frequency (Hz) — this is the PWM clock frequency
    uint32_t pwm_freq = uapi_pwm_get_frequency(pwm_channel);
    if (pwm_freq == 0) {
        pwm_freq = PWM_DEFAULT_CLOCK_FREQ; // Default 1MHz PWM clock
    }

    // Calculate clock cycle counts for target frequency
    // For frequency F_tone, one period = F_pwm / F_tone cycles
    uint32_t total_cycles = pwm_freq / frequency;
    if (total_cycles == 0) {
        return false; // Frequency too high for PWM clock
    }

    // 50% duty cycle for square wave
    uint32_t high_time = total_cycles / 2;
    uint32_t low_time = total_cycles - high_time;

    pwm_config_t cfg;
    cfg.high_time = high_time;
    cfg.low_time = low_time;
    cfg.offset_time = 0;
    cfg.cycles = 0; // Continuous (will be updated if duration is set)
    cfg.repeat = true;

    // For finite duration, use PWM cycles parameter for auto-stop
    // cycles = duration_ms * frequency_hz / 1000
    if (duration > 0) {
        uint32_t cycles_needed = ((uint32_t)duration * frequency) / 1000;
        if (cycles_needed > 0 && cycles_needed <= PWM_MAX_AUTO_STOP_CYCLES) {
            cfg.cycles = (uint16_t)cycles_needed;
            cfg.repeat = false;
        }
        // If cycles exceeds 32767, we rely on tone_update() polling
    }

    errcode_t ret = uapi_pwm_open(pwm_channel, &cfg);
    if (ret != ERRCODE_SUCC) {
        return false;
    }

    uapi_pwm_start(pwm_channel);
    return true;
}
#endif /* CONFIG_PWM_SUPPORT */

/* *
 * @brief Generate a square wave tone on a pin
 * @param pin - Arduino pin number
 * @param frequency - Frequency in Hz
 * @param duration - Duration in milliseconds (0 = continuous)
 */
void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return;
    }

    if (frequency == 0) {
        noTone(pin);
        return;
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return;
    }

    // Stop any existing tone on this or another pin (with critical section)
    {
        uint32_t int_save = LOS_IntLock();
        if (s_tone_state.active) {
            uint8_t old_pin = s_tone_state.pin;
            LOS_IntRestore(int_save);
            noTone(old_pin);
            int_save = LOS_IntLock();
        }
        LOS_IntRestore(int_save);
    }

#if defined(CONFIG_PWM_SUPPORT)
    // --- PWM path (preferred) ---
    if (pinSupportsPWM(pin)) {
        uint8_t pwm_channel = digitalPinToPWMChannel(pin);
        if (pwm_channel != NOT_ON_TIMER) {
            if (tone_pwm_start(pin, pwm_channel, frequency, duration)) {
                uint32_t int_save = LOS_IntLock();
                s_tone_state.pin = pin;
                s_tone_state.pwm_channel = pwm_channel;
                s_tone_state.active = true;
                s_tone_state.frequency = frequency;

                if (duration > 0) {
                    s_tone_state.has_duration = true;
                    s_tone_state.end_time = millis() + duration;
                } else {
                    s_tone_state.has_duration = false;
                    s_tone_state.end_time = 0;
                }
                LOS_IntRestore(int_save);
                return;
            }
        }
    }
#endif /* CONFIG_PWM_SUPPORT */

    // --- Bit-banging fallback (for non-PWM pins or when PWM fails) ---
    uapi_gpio_set_dir((pin_t)hw_pin, GPIO_DIRECTION_OUTPUT);

    {
        uint32_t int_save = LOS_IntLock();
        s_tone_state.pin = pin;
        s_tone_state.pwm_channel = TONE_PWM_NONE;
        s_tone_state.active = true;
        s_tone_state.frequency = frequency;
        LOS_IntRestore(int_save);
    }

    if (duration > 0) {
        uint32_t int_save = LOS_IntLock();
        s_tone_state.has_duration = true;
        s_tone_state.end_time = millis() + duration;
        LOS_IntRestore(int_save);

        // Blocking bit-banging loop for duration mode
        unsigned long half_period_us = 500000UL / frequency;
        unsigned long end = millis() + duration;

        while (millis() < end) {
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_HIGH);
            delayMicroseconds(half_period_us);
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_LOW);
            delayMicroseconds(half_period_us);
            yield(); // Allow RTOS scheduler to run, prevent watchdog timeout
        }

        // Done — clean up
        uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_LOW);
        {
            uint32_t int_save = LOS_IntLock();
            s_tone_state.active = false;
            LOS_IntRestore(int_save);
        }
    } else {
        // Continuous mode on non-PWM pin: start a cooperative bit-banging loop
        // This uses a non-blocking approach: generate a burst of cycles,
        // then yield to allow other tasks to run, and continue in tone_update().
        uint32_t int_save = LOS_IntLock();
        s_tone_state.has_duration = false;
        s_tone_state.end_time = 0;
        LOS_IntRestore(int_save);

        // Generate initial burst of cycles (cooperative)
        unsigned long half_period_us = 500000UL / frequency;
        for (int i = 0; i < 100; i++) { // 100 cycles burst
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_HIGH);
            delayMicroseconds(half_period_us);
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_LOW);
            delayMicroseconds(half_period_us);
        }
        // tone_update() will continue generating bursts
    }
}

/* *
 * @brief Stop tone generation on a pin
 * @param pin - Arduino pin number
 */
void noTone(uint8_t pin)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return;
    }

    // Check if this pin has an active tone (with critical section)
    uint8_t pwm_ch = TONE_PWM_NONE;
    {
        uint32_t int_save = LOS_IntLock();
        if (!s_tone_state.active || s_tone_state.pin != pin) {
            LOS_IntRestore(int_save);
            return;
        }
        pwm_ch = s_tone_state.pwm_channel;
        LOS_IntRestore(int_save);
    }

#if defined(CONFIG_PWM_SUPPORT)
    if (pwm_ch != TONE_PWM_NONE && pwm_ch < MAX_PWM_CHANNELS) {
        // Stop PWM
#if defined(CONFIG_PWM_USING_V150)
        uapi_pwm_stop(pwm_ch);
#endif
        uapi_pwm_close(pwm_ch);
    } else
#endif /* CONFIG_PWM_SUPPORT */
    {
        // Bit-banging mode — set pin low
        (void)pwm_ch; // Suppress unused warning when CONFIG_PWM_SUPPORT is undefined
        uint8_t hw_pin = digitalPinToPin(pin);
        if (hw_pin != NOT_A_PIN) {
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_LOW);
        }
    }

    // Clear state (with critical section)
    {
        uint32_t int_save = LOS_IntLock();
        s_tone_state.active = false;
        s_tone_state.pin = 0;
        s_tone_state.active = false;
        s_tone_state.pin = 0;
        s_tone_state.pwm_channel = TONE_PWM_NONE;
        s_tone_state.has_duration = false;
        s_tone_state.end_time = 0;
        s_tone_state.frequency = 0;
        LOS_IntRestore(int_save);
    }
}

/* *
 * @brief Check and handle tone duration / continuous bit-banging
 *
 * Call this periodically (e.g. from loop() or a system tick hook) to:
 * 1. Stop tones that have exceeded their duration
 * 2. Continue continuous bit-banging on non-PWM pins (cooperative approach)
 */
void tone_update(void)
{
    uint32_t int_save = LOS_IntLock();
    if (!s_tone_state.active) {
        LOS_IntRestore(int_save);
        return;
    }

    // Handle timed tone expiry
    if (s_tone_state.has_duration && millis() >= s_tone_state.end_time) {
        uint8_t pin = s_tone_state.pin;
        LOS_IntRestore(int_save);
        noTone(pin);
        return;
    }

    // Handle continuous bit-banging on non-PWM pins
    if (s_tone_state.pwm_channel == TONE_PWM_NONE && !s_tone_state.has_duration) {
        uint8_t hw_pin = digitalPinToPin(s_tone_state.pin);
        unsigned int freq = s_tone_state.frequency;
        LOS_IntRestore(int_save);

        if (hw_pin == NOT_A_PIN) {
            int_save = LOS_IntLock();
            uint8_t pin = s_tone_state.pin;
            LOS_IntRestore(int_save);
            noTone(pin);
            return;
        }

        if (freq == 0) {
            int_save = LOS_IntLock();
            uint8_t pin = s_tone_state.pin;
            LOS_IntRestore(int_save);
            noTone(pin);
            return;
        }

        unsigned long half_period_us = (MICROSECONDS_PER_SECOND / 2U) / freq;

        // Generate a burst of cycles, then yield
        // Reduced burst size (TONE_UPDATE_BURST_CYCLES cycles) to prevent RTOS scheduler starvation
        for (unsigned int i = 0; i < TONE_UPDATE_BURST_CYCLES; i++) {
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_HIGH);
            delayMicroseconds(half_period_us);
            uapi_gpio_set_val((pin_t)hw_pin, GPIO_LEVEL_LOW);
            delayMicroseconds(half_period_us);
        }
        yield(); // Allow RTOS scheduler to run
        return;
    }

    LOS_IntRestore(int_save);
}

// ============================================================================
// shiftOut / shiftIn
// ============================================================================

/* *
 * @brief Shift out a byte of data serially
 */
void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val)
{
    if (dataPin >= NUM_DIGITAL_PINS || clockPin >= NUM_DIGITAL_PINS) {
        return;
    }

    // Validate bitOrder (only LSBFIRST=0 or MSBFIRST=1 are valid)
    if (bitOrder != LSBFIRST && bitOrder != MSBFIRST) {
        return;
    }

    uint8_t data_hw_pin = digitalPinToPin(dataPin);
    uint8_t clock_hw_pin = digitalPinToPin(clockPin);
    if (data_hw_pin == NOT_A_PIN || clock_hw_pin == NOT_A_PIN) {
        return;
    }

    uapi_gpio_set_dir((pin_t)data_hw_pin, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir((pin_t)clock_hw_pin, GPIO_DIRECTION_OUTPUT);

    for (unsigned int i = 0; i < BYTE_BIT_COUNT; i++) {
        uint8_t bit;
        if (bitOrder == LSBFIRST) {
            bit = val & (1 << i);
        } else {
            bit = val & (1 << (BYTE_MSB_INDEX - i));
        }

        uapi_gpio_set_val((pin_t)data_hw_pin, bit ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
        uapi_gpio_set_val((pin_t)clock_hw_pin, GPIO_LEVEL_HIGH);
        uapi_gpio_set_val((pin_t)clock_hw_pin, GPIO_LEVEL_LOW);
    }
}

/* *
 * @brief Shift in a byte of data serially
 */
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
    if (dataPin >= NUM_DIGITAL_PINS || clockPin >= NUM_DIGITAL_PINS) {
        return 0;
    }

    // Validate bitOrder (only LSBFIRST=0 or MSBFIRST=1 are valid)
    if (bitOrder != LSBFIRST && bitOrder != MSBFIRST) {
        return 0;
    }

    uint8_t data_hw_pin = digitalPinToPin(dataPin);
    uint8_t clock_hw_pin = digitalPinToPin(clockPin);
    if (data_hw_pin == NOT_A_PIN || clock_hw_pin == NOT_A_PIN) {
        return 0;
    }

    uapi_gpio_set_dir((pin_t)data_hw_pin, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_dir((pin_t)clock_hw_pin, GPIO_DIRECTION_OUTPUT);

    uint8_t result = 0;
    for (unsigned int i = 0; i < BYTE_BIT_COUNT; i++) {
        uapi_gpio_set_val((pin_t)clock_hw_pin, GPIO_LEVEL_HIGH);

        gpio_level_t level = uapi_gpio_get_val((pin_t)data_hw_pin);

        uapi_gpio_set_val((pin_t)clock_hw_pin, GPIO_LEVEL_LOW);

        if (level == GPIO_LEVEL_HIGH) {
            if (bitOrder == LSBFIRST) {
                result |= (1 << i);
            } else {
                result |= (1 << (BYTE_MSB_INDEX - i));
            }
        }
    }

    return result;
}
