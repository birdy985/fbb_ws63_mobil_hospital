/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring_digital.cpp
 * @brief Digital IO functions implementation for Arduino compatibility layer
 * @version 3.0
 * @date 2026-04-21
 */

#include "Arduino.h"
#include "pins_arduino.h"

#include "driver/gpio.h"

/* *
 * @brief Configure a pin as input or output
 * @param pin - Arduino pin number
 * @param mode - INPUT, OUTPUT, INPUT_PULLUP, or INPUT_PULLDOWN
 */
void pinMode(uint8_t pin, uint8_t mode)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return; // Invalid pin
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return; // Invalid pin mapping
    }

    gpio_direction_t dir;

    switch (mode) {
        case INPUT:
        case INPUT_PULLUP:
        case INPUT_PULLDOWN:
            dir = GPIO_DIRECTION_INPUT;
            break;
        case OUTPUT:
        default:
            dir = GPIO_DIRECTION_OUTPUT;
            break;
    }

    uapi_gpio_set_dir((pin_t)hw_pin, dir);

    // Handle pull-up/pull-down if needed (ws63 specific)
    // Note: ws63 may handle pull-up/down through pinctrl
    (void)mode; // Suppress unused warning for now
}

/* *
 * @brief Write a HIGH or LOW value to a digital pin
 * @param pin - Arduino pin number
 * @param val - HIGH or LOW
 */
void digitalWrite(uint8_t pin, uint8_t val)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return; // Invalid pin
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return; // Invalid pin mapping
    }

    gpio_level_t level = (val == HIGH) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
    uapi_gpio_set_val((pin_t)hw_pin, level);
}

/* *
 * @brief Read the value from a digital pin
 * @param pin - Arduino pin number
 * @return HIGH or LOW
 */
int digitalRead(uint8_t pin)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return LOW; // Invalid pin
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return LOW; // Invalid pin mapping
    }

    gpio_level_t level = uapi_gpio_get_val((pin_t)hw_pin);
    return (level == GPIO_LEVEL_HIGH) ? HIGH : LOW;
}

/* *
 * @brief Toggle the state of a digital pin
 * @param pin - Arduino pin number
 */
void digitalToggle(uint8_t pin)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return; // Invalid pin
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return; // Invalid pin mapping
    }

    uapi_gpio_toggle((pin_t)hw_pin);
}
