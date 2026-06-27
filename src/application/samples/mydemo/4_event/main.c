#define USE_EVENT_FLAGS 1

#if !USE_EVENT_FLAGS

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

// 全局变量标志位
volatile int g_flag0_ready = 0;
volatile int g_flag1_ready = 0;

// Thread 1: 低优先级任务
static void thread1_task(void *arg)
{
    unused(arg);
    while (1) {
        printf("    [Thread1] Low priority task is running... (I am alive!)\r\n");
        osDelay(50);
    }
}

// Thread 2: 生产者 A
static void thread2_task(void *arg)
{
    unused(arg);
    osDelay(200); // 模拟耗时
    g_flag0_ready = 1;
    printf("[Thread2] Task 2 Done! (Flag0 Set)\r\n");
}

// Thread 3: 生产者 B
static void thread3_task(void *arg)
{
    unused(arg);
    osDelay(400); // 模拟耗时
    g_flag1_ready = 1;
    printf("[Thread3] Task 3 Done! (Flag1 Set)\r\n");
}

// Thread 4: 高优先级消费者
static void thread4_task(void *arg)
{
    unused(arg);
    printf("[Thread4] Waiting for threads 2 & 3...\r\n");

    while (1) {
        if (g_flag0_ready && g_flag1_ready) {
            break;
        }
        // osDelay(100); 
    }

    printf("[Thread4] All Done! System Start.\r\n");
}

static void main(void)
{
    osThreadAttr_t attr = {0};
    attr.stack_size = 0x1000;

    // 1. 创建低优先级任务
    attr.name = "Thread1";
    attr.priority = osPriorityNormal - 1;
    osThreadNew((osThreadFunc_t)thread1_task, NULL, &attr);

    // 2. 创建生产者
    attr.name = "Thread2";
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)thread2_task, NULL, &attr);

    attr.name = "Thread3";
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)thread3_task, NULL, &attr);

    // 3. 创建高优先级任务
    attr.name = "Thread4";
    attr.priority = osPriorityNormal+1;
    osThreadNew((osThreadFunc_t)thread4_task, NULL, &attr);
}

app_run(main);

#else

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

// 定义事件位掩码
#define FLAG_BIT_0 (1U << 0) // 代表 Task 2 完成
#define FLAG_BIT_1 (1U << 1) // 代表 Task 3 完成

// 事件标志组句柄
osEventFlagsId_t g_evt_id;

// Thread 1: 低优先级任务
static void thread1_task(void *arg)
{
    unused(arg);
    while (1) {
        printf("    [Thread1] Low priority task is running... (CPU is free!)\r\n");
        osDelay(50);
    }
}

// Thread 2: 生产者 A
static void thread2_task(void *arg)
{
    unused(arg);
    osDelay(200);
    printf("[Thread2] Task 2 Done! -> Set Event Bit 0\r\n");
    // 发送事件
    osEventFlagsSet(g_evt_id, FLAG_BIT_0);
}

// Thread 3: 生产者 B
static void thread3_task(void *arg)
{
    unused(arg);
    osDelay(400);
    printf("[Thread3] Task 3 Done! -> Set Event Bit 1\r\n");
    // 发送事件
    osEventFlagsSet(g_evt_id, FLAG_BIT_1);
}

// Thread 4: 高优先级消费者
static void thread4_task(void *arg)
{
    unused(arg);
    uint32_t flags;
    printf("[Thread4] Blocking wait for threads 2 & 3...\r\n");

    // 阻塞等待 (Wait All)
    // 任务进入 Blocked 状态，CPU 自动转交给低优先级任务 (Thread 1)
    flags = osEventFlagsWait(g_evt_id,
                             FLAG_BIT_0 | FLAG_BIT_1, // 等待 Bit0 和 Bit1
                             osFlagsWaitAll,          // 逻辑与 (AND)
                             osWaitForever);          // 死等

    printf("[Thread4] Events Received (0x%02X)! System Start.\r\n", flags);
}

static void main(void)
{
    osThreadAttr_t attr = {0};
    attr.stack_size = 0x1000;

    // 创建事件标志组对象
    g_evt_id = osEventFlagsNew(NULL);

    // 1. 创建低优先级任务
    attr.name = "Thread1";
    attr.priority = osPriorityNormal - 1;
    osThreadNew((osThreadFunc_t)thread1_task, NULL, &attr);

    // 2. 创建生产者
    attr.name = "Thread2";
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)thread2_task, NULL, &attr);

    attr.name = "Thread3";
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)thread3_task, NULL, &attr);

    // 3. 创建高优先级任务
    attr.name = "Thread4";
    attr.priority = osPriorityAboveNormal;
    osThreadNew((osThreadFunc_t)thread4_task, NULL, &attr);
}

app_run(main);

#endif

