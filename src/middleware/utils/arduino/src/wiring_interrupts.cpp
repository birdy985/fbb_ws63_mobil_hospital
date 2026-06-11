/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring_interrupts.cpp
 * @brief Interrupt functions implementation for Arduino compatibility layer
 * @version 3.0
 * @date 2026-04-21
 */

#include "Arduino.h"
#include "pins_arduino.h"

#include "driver/gpio.h"
#include "los_hwi.h"

// Maximum number of interrupt pins
#define MAX_INTERRUPT_PINS 16

// Interrupt handler storage
typedef struct {
    uint8_t pin;
    void (*handler)(void);
    bool active;
} interrupt_handler_t;

static interrupt_handler_t s_interrupt_handlers[MAX_INTERRUPT_PINS];
static uint8_t s_interrupt_count = 0;

// Global interrupt state tracking for nested interrupt control
static volatile uint32_t s_interrupt_lock_state = 0;
static volatile uint8_t s_interrupt_nest_count = 0;

/* *
 * @brief GPIO interrupt callback wrapper
 * @param pin - PIN that caused the interrupt
 * @param param - Parameter (unused, SDK doesn't support passing custom param)
 */
static void gpio_interrupt_callback(pin_t pin, uintptr_t param)
{
    (void)param; // SDK doesn't support custom param

    // Find handler by hw pin number
    for (uint8_t i = 0; i < s_interrupt_count; i++) {
        if (!s_interrupt_handlers[i].active) {
            continue;
        }

        uint8_t hw_pin = digitalPinToPin(s_interrupt_handlers[i].pin);
        if (hw_pin == (uint8_t)pin) {
            void (*handler)(void) = s_interrupt_handlers[i].handler;
            if (handler != NULL) {
                handler();
            }
            break;
        }
    }
}

/* *
 * @brief Find or allocate an interrupt handler slot
 * @param pin - Pin number
 * @return Index in handler array, or -1 if not found and no space
 */
static int find_handler_slot(uint8_t pin)
{
    // First, check if this pin already has a handler
    for (uint8_t i = 0; i < s_interrupt_count; i++) {
        if (s_interrupt_handlers[i].active && s_interrupt_handlers[i].pin == pin) {
            return i;
        }
    }

    // If not found, find an empty slot (reuse inactive slots)
    for (uint8_t i = 0; i < s_interrupt_count; i++) {
        if (!s_interrupt_handlers[i].active) {
            return i; // Reuse inactive slot
        }
    }

    // If no empty slot, allocate new one
    if (s_interrupt_count < MAX_INTERRUPT_PINS) {
        return s_interrupt_count++;
    }

    return -1; // No space
}

/* *
 * @brief Attach an interrupt handler to a pin
 * @param pin - Arduino pin number
 * @param callback - Function to call when interrupt triggers
 * @param mode - Trigger mode (CHANGE, RISING, FALLING)
 */
void attachInterrupt(uint8_t pin, void (*callback)(void), int mode)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return; // Invalid pin
    }

    if (callback == NULL) {
        return; // Invalid callback
    }

    // Validate mode (Arduino modes: RISING=4, FALLING=5, CHANGE=6)
    if (mode != RISING && mode != FALLING && mode != CHANGE) {
        return; // Invalid mode
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return; // Invalid pin mapping
    }

    // Find or allocate handler slot
    int slot = find_handler_slot(pin);
    if (slot < 0) {
        return; // No space for more interrupts
    }

    // Store handler
    s_interrupt_handlers[slot].pin = pin;
    s_interrupt_handlers[slot].handler = callback;
    s_interrupt_handlers[slot].active = true;

    // Map Arduino mode to ws63 trigger type
    // Arduino interrupt modes (RISING=4, FALLING=5, CHANGE=6)
    // ws63 trigger: 1=Rising, 2=Falling, 3=Both, 4=Low level, 8=High level
    uint32_t trigger;
    switch (mode) {
        case RISING:                              // 4
            trigger = GPIO_INTERRUPT_RISING_EDGE; // 1
            break;
        case FALLING:                              // 5
            trigger = GPIO_INTERRUPT_FALLING_EDGE; // 2
            break;
        case CHANGE:                        // 6
            trigger = GPIO_INTERRUPT_DEDGE; // 3 (both edges)
            break;
        default:
            return; // Already validated above, but safety check
    }

    // Set interrupt mode
    errcode_t ret = uapi_gpio_set_isr_mode((pin_t)hw_pin, trigger);
    if (ret != ERRCODE_SUCC) {
        s_interrupt_handlers[slot].active = false;
        return;
    }

    // Register interrupt callback
    ret = uapi_gpio_register_isr_func((pin_t)hw_pin, trigger, gpio_interrupt_callback);
    if (ret != ERRCODE_SUCC) {
        s_interrupt_handlers[slot].active = false;
        return;
    }

    // Enable the interrupt
    uapi_gpio_enable_interrupt((pin_t)hw_pin);
}

/* *
 * @brief Detach an interrupt handler from a pin
 * @param pin - Arduino pin number
 */
void detachInterrupt(uint8_t pin)
{
    if (pin >= NUM_DIGITAL_PINS) {
        return; // Invalid pin
    }

    uint8_t hw_pin = digitalPinToPin(pin);
    if (hw_pin == NOT_A_PIN) {
        return; // Invalid pin mapping
    }

    // Disable interrupt first
    uapi_gpio_disable_interrupt((pin_t)hw_pin);

    // Unregister the callback
    uapi_gpio_unregister_isr_func((pin_t)hw_pin);

    // Clear handler from our table
    for (uint8_t i = 0; i < s_interrupt_count; i++) {
        if (s_interrupt_handlers[i].pin == pin) {
            s_interrupt_handlers[i].active = false;
            s_interrupt_handlers[i].handler = NULL;
            break;
        }
    }
}

/* *
 * @brief Enable global interrupts
 * Note: Supports nested calls - only unlocks when nest count reaches 0
 */
void interrupts(void)
{
    if (s_interrupt_nest_count > 0) {
        s_interrupt_nest_count--;
    }

    if (s_interrupt_nest_count == 0 && s_interrupt_lock_state != 0) {
        // Restore interrupt state using LOS_IntRestore
        LOS_IntRestore(s_interrupt_lock_state);
        s_interrupt_lock_state = 0;
    }
}

/* *
 * @brief Disable global interrupts
 * Note: Supports nested calls - tracks lock state for proper restoration
 */
void noInterrupts(void)
{
    if (s_interrupt_nest_count == 0) {
        s_interrupt_lock_state = LOS_IntLock();
    }
    s_interrupt_nest_count++;
}

/* *
 * @brief Get current interrupt state
 * @return true if interrupts are enabled, false otherwise
 */
bool interrupts_enabled(void)
{
    return (s_interrupt_nest_count == 0);
}
