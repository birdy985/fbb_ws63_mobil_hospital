/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file pins_arduino.h
 * @brief Pin mapping for Arduino compatibility on ws63
 * @version 3.0
 * @date 2026-04-21
 */

#ifndef PINS_ARDUINO_H
#define PINS_ARDUINO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Total Pin Count
 * NOTE: NUM_DIGITAL_PINS and NUM_ANALOG_INPUTS are defined in Arduino.h
 * Do not redefine here to avoid conflicts.
 * ============================================================================ */

// Use values from Arduino.h (32 digital pins, 8 analog inputs)
// NUM_DIGITAL_PINS = 32 (defined in Arduino.h)
// NUM_ANALOG_INPUTS = 8 (defined in Arduino.h)

// Alias for compatibility with some Arduino libraries
#define NUM_ANALOG_PINS NUM_ANALOG_INPUTS

/* ============================================================================
 * Pin Number Definitions
 * ============================================================================ */

/* Digital Pins (D0-D31) */
#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7
#define D8 8
#define D9 9
#define D10 10
#define D11 11
#define D12 12
#define D13 13
#define D14 14
#define D15 15
#define D16 16
#define D17 17
#define D18 18
#define D19 19
#define D20 20
#define D21 21
#define D22 22
#define D23 23
#define D24 24
#define D25 25
#define D26 26
#define D27 27
#define D28 28
#define D29 29
#define D30 30
#define D31 31

/* Analog Pins (A0-A7) */
#define A0 24
#define A1 25
#define A2 26
#define A3 27
#define A4 28
#define A5 29
#define A6 30
#define A7 31

/* Special Pins */
#define PIN_SERIAL_TX D0
#define PIN_SERIAL_RX D1
#define PIN_WIRE_SDA A4
#define PIN_WIRE_SCL A5
#define PIN_SPI_SS D10
#define PIN_SPI_MOSI D11
#define PIN_SPI_MISO D12
#define PIN_SPI_SCK D13
#define LED_BUILTIN D13

/* PWM support - ws63 specific pins */
#define HAS_PWM(pin) ((pin) == D3 || (pin) == D5 || (pin) == D6 || (pin) == D9 || (pin) == D10 || (pin) == D11)

/* Analog pin check */
#define IS_ANALOG_PIN(pin) ((pin) >= A0 && (pin) <= A7)

/* Invalid values - NOT_ON_TIMER defined in Arduino.h as 0xFF */
// NOT_A_PIN is unique, no conflict with Arduino.h
#define NOT_A_PIN 0xFF

/* ============================================================================
 * Pin Mapping Tables (extern declarations)
 * These arrays are defined in a separate source file (if needed)
 * ============================================================================ */

extern const uint8_t arduino_pin_map[];
extern const uint8_t arduino_pwm_map[];
extern const uint8_t arduino_adc_map[];

/* ============================================================================
 * Pin Mapping Helper Functions
 * ============================================================================ */

static inline uint8_t digitalPinToPin(uint8_t arduino_pin)
{
    if (arduino_pin >= NUM_DIGITAL_PINS) {
        return NOT_A_PIN;
    }
    // For ws63, direct pin mapping (Arduino pin = ws63 pin)
    return arduino_pin;
}

static inline uint8_t digitalPinToPWMChannel(uint8_t arduino_pin)
{
    if (arduino_pin >= NUM_DIGITAL_PINS || !HAS_PWM(arduino_pin)) {
        return NOT_ON_TIMER;
    }

    // PWM channel mapping for ws63
    return arduino_pin; // Direct mapping for simplicity
}

static inline uint8_t analogPinToChannel(uint8_t arduino_pin)
{
    if (!IS_ANALOG_PIN(arduino_pin)) {
        return NOT_A_PIN;
    }
    // ADC channel mapping for ws63
    return arduino_pin - A0; // Channel 0-7
}

static inline bool pinSupportsPWM(uint8_t arduino_pin)
{
    if (arduino_pin >= NUM_DIGITAL_PINS) {
        return false;
    }
    return HAS_PWM(arduino_pin);
}

#ifdef __cplusplus
}
#endif

#endif /* PINS_ARDUINO_H */