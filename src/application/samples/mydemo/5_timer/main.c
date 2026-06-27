#define USE_SW_TIMER 0

#if !USE_SW_TIMER
#include "los_memory.h"  //内存打印
#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

#define WORKER_COUNT 10

//内存打印辅助函数
void print_mem_info(void)
{
    UINT32 ret;
    LOS_MEM_POOL_STATUS status = {0};

    // 1. 获取系统默认内存池地址
    // m_aucSysMem0 是 LiteOS 内核定义的全局变量
    extern UINT8 *m_aucSysMem0;
    void *pool = (void *)m_aucSysMem0;

    // 2. 获取内存池总大小 (单独的API)
    UINT32 totalSize = LOS_MemPoolSizeGet(pool);

    // 3. 获取详细统计信息 (使用 LOS_MemInfoGet)
    ret = LOS_MemInfoGet(pool, &status);

    if (ret == LOS_OK) {
        printf("\r\n================ MEMORY INFO ================\r\n");
        printf("Total Pool Size  : %u bytes (%u KB)\r\n", totalSize, totalSize / 1024);
        printf("Total Used Size  : %u bytes (%u KB)\r\n", status.uwTotalUsedSize, status.uwTotalUsedSize / 1024);
        printf("Total Free Size  : %u bytes (%u KB)\r\n", status.uwTotalFreeSize, status.uwTotalFreeSize / 1024);
        printf("Max Free Node    : %u bytes \r\n", status.uwMaxFreeNodeSize);
        printf("Used Node Num    : %u\r\n", status.uwUsedNodeNum);
        printf("Free Node Num    : %u\r\n", status.uwFreeNodeNum);
        printf("=============================================\r\n\r\n");
    } else {
        printf("Failed to get memory info. Ret = 0x%x\r\n", ret);
    }
}
// 线程任务函数
static void thread_worker_task(void *arg)
{
    int id = (int)arg;

    while (1) {
        // 每个线程占用独立栈空间 (通常 4KB)
        printf("Thread Worker %d: Processing...\r\n", id);

        // 周期性延时
        osDelay(200 + (id * 10));
    }
}
static void main(void)
{
    print_mem_info();

    osThreadAttr_t attr = {0}; // 【重要】先清零，防止垃圾数据

    // 1. 设置所有线程共用的属性
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000; // 4KB
    attr.priority = osPriorityNormal;

    printf("Starting 6 Threads\r\n");

    // Thread 0
    attr.name = "Worker_0";
    if (osThreadNew((osThreadFunc_t)thread_worker_task, (void *)0, &attr) != NULL) {
        printf("Worker_0 OK\r\n");
    } else {
        printf("Worker_0 Fail\r\n");
    }

    // Thread 1
    attr.name = "Worker_1";
    if (osThreadNew((osThreadFunc_t)thread_worker_task, (void *)1, &attr) != NULL) {
        printf("Worker_1 OK\r\n");
    } else {
        printf("Worker_1 Fail\r\n");
    }

    // Thread 2
    attr.name = "Worker_2";
    if (osThreadNew((osThreadFunc_t)thread_worker_task, (void *)2, &attr) != NULL) {
        printf("Worker_2 OK\r\n");
    } else {
        printf("Worker_2 Fail\r\n");
    }

    // Thread 3
    attr.name = "Worker_3";
    if (osThreadNew((osThreadFunc_t)thread_worker_task, (void *)3, &attr) != NULL) {
        printf("Worker_3 OK\r\n");
    } else {
        printf("Worker_3 Fail\r\n");
    }

    // Thread 4
    attr.name = "Worker_4";
    if (osThreadNew((osThreadFunc_t)thread_worker_task, (void *)4, &attr) != NULL) {
        printf("Worker_4 OK\r\n");
    } else {
        printf("Worker_4 Fail\r\n");
    }

    // Thread 5
    attr.name = "Worker_5";
    if (osThreadNew((osThreadFunc_t)thread_worker_task, (void *)5, &attr) != NULL) {
        printf("Worker_5 OK\r\n");
    } else {
        printf("Worker_5 Fail\r\n");
    }
    print_mem_info();
}
#else

#include "los_memory.h" //内存打印
#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

#define WORKER_COUNT 15
// 内存打印辅助函数
void print_mem_info(void)
{
    UINT32 ret;
    LOS_MEM_POOL_STATUS status = {0};

    // 1. 获取系统默认内存池地址
    // m_aucSysMem0 是 LiteOS 内核定义的全局变量
    extern UINT8 *m_aucSysMem0;
    void *pool = (void *)m_aucSysMem0;

    // 2. 获取内存池总大小 (单独的API)
    UINT32 totalSize = LOS_MemPoolSizeGet(pool);

    // 3. 获取详细统计信息 (使用 LOS_MemInfoGet)
    ret = LOS_MemInfoGet(pool, &status);

    if (ret == LOS_OK) {
        printf("\r\n================ MEMORY INFO ================\r\n");
        printf("Total Pool Size  : %u bytes (%u KB)\r\n", totalSize, totalSize / 1024);
        printf("Total Used Size  : %u bytes (%u KB)\r\n", status.uwTotalUsedSize, status.uwTotalUsedSize / 1024);
        printf("Total Free Size  : %u bytes (%u KB)\r\n", status.uwTotalFreeSize, status.uwTotalFreeSize / 1024);
        printf("Max Free Node    : %u bytes \r\n", status.uwMaxFreeNodeSize);
        printf("Used Node Num    : %u\r\n", status.uwUsedNodeNum);
        printf("Free Node Num    : %u\r\n", status.uwFreeNodeNum);
        printf("=============================================\r\n\r\n");
    } else {
        printf("Failed to get memory info. Ret = 0x%x\r\n", ret);
    }
}
// 定时器回调函数
static void timer_worker_callback(void *arg)
{
    int id = (int)arg;

    // 回调执行，无额外栈开销
    printf("Timer Callback %d Executing\r\n", id);
}

static void main(void)
{
    print_mem_info();
    printf("Starting %d Software Timers...\r\n", WORKER_COUNT);

    for (int i = 0; i < WORKER_COUNT; i++) {
        // 1. 创建定时器
        osTimerId_t tid = osTimerNew(timer_worker_callback, osTimerPeriodic, (void *)i, NULL);

        if (tid != NULL) {
            // 2. 启动定时器
            // 错峰执行机制：避免所有回调在同一时刻触发
            // Timer 0: 200 ticks
            // Timer 1: 220 ticks
            // ...
            uint32_t interval = 200 + (i * 20);

            osTimerStart(tid, interval);
            printf("Timer %d Started. Interval: %d ticks\r\n", i, interval);
        } else {
            printf("Timer %d Create Failed!\r\n", i);
        }
    }
    printf("All Timers Running.\r\n");
    print_mem_info();
}

#endif

app_run(main);