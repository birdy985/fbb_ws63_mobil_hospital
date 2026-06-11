/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file TimerClass.cpp
 * @brief Hardware Timer class implementation for Arduino compatibility layer
 * @version 2.0
 * @date 2026-04-30
 *
 * Hardware timer implementation using SDK Timer API
 * Supports multiple timer instances with interrupt callbacks
 *
 * Note: Timer callbacks execute in interrupt context
 * - Keep callbacks short and fast
 * - Do not use blocking functions (delay, Serial.print, etc.)
 * - Use volatile variables for shared data
 * - Consider using semaphores/queues for thread-safe communication
 */

#include "TimerClass.h"
#include "timer.h"
#include "chip_core_irq.h"

// Default timer instance for C-style API
static TimerClass *g_default_timer = nullptr;

// Track which timer indices have been adapted (interrupt registered)
static bool g_timer_adapted[CONFIG_TIMER_MAX_NUM] = {false};

/* *
 * @brief Get IRQ number for a given timer index
 *
 * Uses switch instead of array to avoid initialization mismatch
 * when CONFIG_TIMER_MAX_NUM differs from actual entries.
 *
 * @param index Timer index
 * @return IRQ number, or 0 if invalid
 */
static uint32_t get_timer_irqn(uint8_t index)
{
    switch (index) {
#if CONFIG_TIMER_MAX_NUM >= 1
        case 0: // 0: only 1 timer
            return TIMER_0_IRQN;
#endif
#if CONFIG_TIMER_MAX_NUM >= 2
        case 1: // 1: two timer
            return TIMER_1_IRQN;
#endif
#if CONFIG_TIMER_MAX_NUM >= 3
        case 2: // 2: three timer
            return TIMER_2_IRQN;
#endif
        default:
            return 0; // Invalid index
    }
}

/* *
 * @brief Timer interrupt priority (same as SDK timer_demo.c)
 *
 * SDK demo uses TIMER_PRIO = 1.
 * main.c uses irq_prio(TIMER_1_IRQN) which reads from the priority table.
 * We use priority 1 to match the SDK convention.
 */
#define ARDUINO_TIMER_IRQ_PRIORITY 1

/* *
 * @brief Ensure a specific timer index has its interrupt adapter registered
 *
 * Each timer instance MUST call uapi_timer_adapter() before uapi_timer_start()
 * to register the interrupt handler. Without this, the hardware timer fires
 * but the interrupt dispatch path dereferences a null pointer → Load Access Fault.
 *
 * main.c already adapts Timer1, but Timer0 and Timer2 are never adapted.
 * This function ensures each index is adapted exactly once.
 *
 * @param index Timer index (0, 1, or 2)
 * @return true if adapter registered successfully (or already registered)
 */
static bool _timer_adapter_ensure(timer_index_t index)
{
    if (index >= CONFIG_TIMER_MAX_NUM) {
        return false;
    }

    // The reserved timer (ARDUINO_TIMER_RESERVED, default TIMER_INDEX_0) is used by system.
    // Calling uapi_timer_adapter() on it would overwrite the systick
    // IRQ handler with the generic timer IRQ handler, breaking all delay(),
    // millis(), and scheduler functionality, leading to NMI crashes
    // (mcause=0x8000000c, ccause=0x8) in rtc_read_once().
    if (index == ARDUINO_TIMER_RESERVED) {
        return false;
    }

    if (g_timer_adapted[index]) {
        return true; // Already adapted
    }

    // Register interrupt handler for this timer index.
    // Uses priority 1, matching SDK timer_demo.c convention.
    errcode_t ret = uapi_timer_adapter(index, get_timer_irqn(index), ARDUINO_TIMER_IRQ_PRIORITY);
    if (ret != ERRCODE_SUCC) {
        return false;
    }

    g_timer_adapted[index] = true;
    return true;
}

/* *
 * @brief Internal callback wrapper
 *
 * Converts SDK callback signature to user callback
 * Called in interrupt context
 *
 * @param data User data (pointer to TimerClass instance)
 */
void TimerClass::callbackWrapper(uintptr_t data)
{
    TimerClass *timer = reinterpret_cast<TimerClass *>(data);
    if (timer && timer->m_callback) {
        // Call user callback
        timer->m_callback();
    }
}

/* *
 * @brief Construct a TimerClass object
 *
 * @param instance Timer instance number (0, 1, 2)
 */
TimerClass::TimerClass(uint8_t instance) noexcept
    : m_timer_handle(nullptr),
      m_instance(instance),
      m_period_us(0),
      m_running(false),
      m_callback(nullptr),
      m_callback_data(0)
{
    // Validate instance number
    if (instance >= CONFIG_TIMER_MAX_NUM) {
        return;
    }

    // Set as default timer if first instance created
    if (g_default_timer == nullptr) {
        g_default_timer = this;
    }
}

/* *
 * @brief Destroy the TimerClass object
 *
 * Stops timer and releases resources
 */
TimerClass::~TimerClass()
{
    stop();
    detachInterrupt();

    if (m_timer_handle) {
        uapi_timer_delete(m_timer_handle);
        m_timer_handle = nullptr;
    }
}

/* *
 * @brief Initialize timer with specified period
 *
 * Creates timer instance and configures period
 * Does not start timer - call start() separately
 *
 * @param microseconds Timer period in microseconds
 * @return true if initialization successful
 */
bool TimerClass::initialize(uint32_t microseconds)
{
    // Stop if already running
    if (m_running) {
        stop();
    }

    // Delete existing timer handle if any
    if (m_timer_handle) {
        uapi_timer_delete(m_timer_handle);
        m_timer_handle = nullptr;
    }

    // Validate period
    uint32_t max_period = uapi_timer_get_max_us();
    if (microseconds == 0 || microseconds > max_period) {
        // Invalid period, use maximum
        microseconds = max_period;
    }

    m_period_us = microseconds;

    // Determine timer index
    timer_index_t index = static_cast<timer_index_t>(m_instance);

    // CRITICAL: Register interrupt adapter for this timer index.
    // Without uapi_timer_adapter(), the hardware timer fires but the
    // interrupt dispatch path has no handler registered → null pointer
    // dereference → Load Access Fault at mtval=0x14.
    if (!_timer_adapter_ensure(index)) {
        // Timer may be reserved by system (e.g., Timer0 used by RTOS tick)
        return false;
    }

    // Create timer instance
    errcode_t ret = uapi_timer_create(index, &m_timer_handle);
    if (ret != ERRCODE_SUCC) {
        m_timer_handle = nullptr;
        return false;
    }

    return true;
}

/* *
 * @brief Set timer period
 *
 * Updates timer period. Timer must be stopped to change period.
 *
 * @param microseconds New period in microseconds
 * @return true if period updated successfully
 */
/* *
 * @brief Set timer period
 *
 * Updates timer period. If timer is running, it will be temporarily stopped,
 * period updated, and then restarted.
 *
 * @param microseconds New period in microseconds
 * @return true if period updated successfully
 */
bool TimerClass::setPeriod(uint32_t microseconds)
{
    // Validate period
    uint32_t max_period = uapi_timer_get_max_us();
    if (microseconds == 0 || microseconds > max_period) {
        microseconds = max_period;
    }

    // Check if timer is running
    bool was_running = m_running;

    // If timer is running, stop it temporarily
    if (was_running) {
        if (!stop()) {
            return false; // Failed to stop
        }
    }

    // Update period
    m_period_us = microseconds;

    // If timer was running, restart it with new period
    if (was_running) {
        if (!start()) {
            // Failed to restart, keep period updated but timer stopped
            return false;
        }
    }

    return true;
}

/* *
 * @brief Start the timer
 *
 * Starts timer with configured period and callback
 *
 * @return true if started successfully
 */
bool TimerClass::start()
{
    if (!m_timer_handle) {
        return false;
    }

    if (m_running) {
        // Already running, stop first
        stop();
    }

    // Start timer with callback
    errcode_t ret = uapi_timer_start(m_timer_handle, m_period_us, callbackWrapper, reinterpret_cast<uintptr_t>(this));
    if (ret == ERRCODE_SUCC) {
        m_running = true;
        return true;
    }

    // Ensure m_running is false on failure
    m_running = false;
    return false;
}

/* *
 * @brief Stop the timer
 *
 * Stops timer without deleting handle
 * Timer can be restarted with start()
 *
 * @return true if stopped successfully
 */
bool TimerClass::stop()
{
    if (!m_timer_handle || !m_running) {
        m_running = false;
        return true;
    }

    errcode_t ret = uapi_timer_stop(m_timer_handle);
    if (ret == ERRCODE_SUCC) {
        m_running = false;
        return true;
    }

    m_running = false;
    return false;
}

/* *
 * @brief Attach interrupt callback
 *
 * Sets callback function to be called on timer interrupt
 * Callback executes in interrupt context - keep it short!
 *
 * @param callback Function to call on timer interrupt
 * @return true if callback attached successfully
 */
bool TimerClass::attachInterrupt(void (*callback)())
{
    if (!callback) {
        return false;
    }

    m_callback = callback;
    return true;
}

/* *
 * @brief Detach interrupt callback
 *
 * Removes callback function
 * Timer will continue running but won't call callback
 *
 * @return true if callback detached successfully
 */
bool TimerClass::detachInterrupt()
{
    m_callback = nullptr;
    return true;
}

/* *
 * @brief Get maximum supported period
 *
 * @return Maximum period in microseconds
 */
uint32_t TimerClass::getMaxPeriod()
{
    return uapi_timer_get_max_us();
}

// ============================================================================
// Pre-defined timer instances
// ============================================================================

TimerClass Timer0(TIMER_INSTANCE_0); // /< Timer instance 0
TimerClass Timer1(TIMER_INSTANCE_1); // /< Timer instance 1
TimerClass Timer2(TIMER_INSTANCE_2); // /< Timer instance 2

// ============================================================================
// C-style API implementation
// ============================================================================

/* *
 * @brief Initialize timer with period and callback
 *
 * Uses Timer1 as default timer instance
 *
 * @param period_us Period in microseconds
 * @param callback Callback function
 */
void timerInit(uint32_t period_us, void (*callback)())
{
    if (callback == nullptr) {
        return;
    }

    if (g_default_timer == nullptr) {
        g_default_timer = &Timer1;
    }

    g_default_timer->initialize(period_us);
    g_default_timer->attachInterrupt(callback);
}

/* *
 * @brief Start the timer
 */
void timerStart()
{
    if (g_default_timer) {
        g_default_timer->start();
    }
}

/* *
 * @brief Stop the timer
 */
void timerStop()
{
    if (g_default_timer) {
        g_default_timer->stop();
    }
}
