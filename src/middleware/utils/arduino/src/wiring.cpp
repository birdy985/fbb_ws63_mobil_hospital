/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring.cpp
 * @brief Time functions implementation for Arduino compatibility layer
 * @version 3.0
 * @date 2026-04-21
 */

#include "Arduino.h"
#include "los_tick.h"
#include "los_task.h"
#include "hal_timer.h"
#include "los_config.h"
#include "systick.h"
#include "chip_io.h"

#if (CHIP_WS63 == 1)
#define SYS_CPU_FREQ_HZ (160 * 1000000UL)
#else
#define SYS_CPU_FREQ_HZ (GET_SYS_CLOCK())
#endif

/* *
 * @brief Get milliseconds since system startup
 * @return Unsigned long - milliseconds
 */
unsigned long millis()
{
    return LOS_Tick2MS(LOS_TickCountGet());
}

/* *
 * @brief Get microseconds since system startup
 * @return Unsigned long - microseconds
 */
unsigned long micros()
{
    return uapi_systick_get_us();
}

/* *
 * @brief Delay for specified milliseconds
 * @param ms - Milliseconds to delay
 */
void delay(unsigned long ms)
{
    if (ms == 0) {
        return;
    }

    LOS_TaskDelay(LOS_MS2Tick(ms));
}

/* *
 * @brief Delay for specified microseconds
 * @param us - Microseconds to delay
 */
void delayMicroseconds(unsigned int us)
{
    if (us == 0) {
        return;
    }

    uapi_systick_delay_us(us);
}

/* *
 * @brief Yield function for cooperative multitasking
 */
void yield(void)
{
    // In LiteOS, this can trigger a task switch if needed
    // For now, it's a no-op as LiteOS handles scheduling automatically
}
