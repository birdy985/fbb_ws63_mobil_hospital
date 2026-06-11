/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file TimerClass.h
 * @brief Hardware Timer class for Arduino compatibility layer
 * @version 1.0
 * @date 2026-04-22
 *
 * Hardware timer implementation using SDK Timer API
 * Supports multiple timer instances with interrupt callbacks
 */

#ifndef TIMERCLASS_H
#define TIMERCLASS_H

#include <stdint.h>

// Timer instance IDs - 引用 SDK 定义的枚举，保持跨平台一致
// timer_porting.h 定义了 timer_index_t 枚举和 TIMER_INDEX_0/1/2
#include "timer_porting.h"

// 通过宏映射，保持 Arduino API 兼容性
#define TIMER_INSTANCE_0 ((uint8_t)TIMER_INDEX_0)
#define TIMER_INSTANCE_1 ((uint8_t)TIMER_INDEX_1)
#define TIMER_INSTANCE_2 ((uint8_t)TIMER_INDEX_2)

// 系统保留的 timer 索引（默认保留 Timer0，与 LiteOS 系统 tick 冲突）
// 其他芯片平台可通过定义此宏覆盖默认值
#ifndef ARDUINO_TIMER_RESERVED
#define ARDUINO_TIMER_RESERVED TIMER_INDEX_0
#endif

/* *
 * @class TimerClass
 * @brief Hardware timer class for periodic interrupts
 *
 * Provides Arduino TimerOne-style interface for hardware timers
 * Uses ws63 SDK uapi_timer_* APIs
 */
class TimerClass {
private:
    void *_timer_handle;      // /< Timer handle from SDK
    uint8_t _instance;        // /< Timer instance number
    uint32_t _period_us;      // /< Current period in microseconds
    bool _running;            // /< Timer running state
    void (*_callback)();      // /< User callback function
    uintptr_t _callback_data; // /< Data passed to callback

    /* *
     * @brief Internal callback wrapper
     * @param data User data passed to callback
     */
    static void _callback_wrapper(uintptr_t data);

public:
    /* *
     * @brief Construct a TimerClass object
     * @param instance Timer instance number (0, 1, 2)
     */
    TimerClass(uint8_t instance = TIMER_INSTANCE_0) noexcept;

    /* *
     * @brief Destroy the TimerClass object
     */
    ~TimerClass();

    /* *
     * @brief Initialize timer with specified period
     * @param microseconds Timer period in microseconds
     * @return true if initialization successful
     */
    bool initialize(uint32_t microseconds);

    /* *
     * @brief Set timer period
     * @param microseconds New period in microseconds
     * @return true if period updated successfully
     */
    bool setPeriod(uint32_t microseconds);

    /* *
     * @brief Get current period
     * @return Current period in microseconds
     */
    uint32_t getPeriod() const
    {
        return _period_us;
    }

    /* *
     * @brief Start the timer
     * @return true if started successfully
     */
    bool start();

    /* *
     * @brief Stop the timer
     * @return true if stopped successfully
     */
    bool stop();

    /* *
     * @brief Check if timer is running
     * @return true if timer is running
     */
    bool isRunning() const
    {
        return _running;
    }

    /* *
     * @brief Attach interrupt callback
     * @param callback Function to call on timer interrupt
     * @return true if callback attached successfully
     */
    bool attachInterrupt(void (*callback)());

    /* *
     * @brief Detach interrupt callback
     * @return true if callback detached successfully
     */
    bool detachInterrupt();

    /* *
     * @brief Check if callback is attached
     * @return true if callback is attached
     */
    bool hasCallback() const
    {
        return _callback != nullptr;
    }

    /* *
     * @brief Get timer instance number
     * @return Instance number
     */
    uint8_t getInstance() const
    {
        return _instance;
    }

    /* *
     * @brief Get maximum supported period
     * @return Maximum period in microseconds
     */
    static uint32_t getMaxPeriod();
};

// Pre-defined timer instances
#if CONFIG_TIMER_MAX_NUM > 0
extern TimerClass Timer0; // /< Timer instance 0
#endif

#if CONFIG_TIMER_MAX_NUM > 1
extern TimerClass Timer1; // /< Timer instance 1
#endif

#if CONFIG_TIMER_MAX_NUM > 2
extern TimerClass Timer2; // /< Timer instance 2
#endif

// C-style API declarations are in Arduino.h (timerInit, timerStart, timerStop)

#endif // TIMERCLASS_H
