#define USE_MUTEX 1

#if USE_MUTEX

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

osMutexId_t g_print_mutex;

void safe_print_slowly(const char *str)
{
    // 申请锁
    osMutexAcquire(g_print_mutex, osWaitForever);

    // --- 临界区开始  ---
    while (*str) {
        printf("%c", *str++);
        osDelay(1);
    }
    printf("\r\n");
    // --- 临界区结束 ---

    // 释放锁
    osMutexRelease(g_print_mutex);
}

static void thread1_task(void *arg)
{
    unused(arg);
    while (1) {
        safe_print_slowly("[Thread1]");
        osDelay(100);
    }
}

static void thread2_task(void *arg)
{
    unused(arg);
    while (1) {
        safe_print_slowly("[Thread2]");
        osDelay(100);
    }
}

static void main(void)
{

    // 创建互斥锁
    g_print_mutex = osMutexNew(NULL);

    osThreadAttr_t attr;
    // 创建线程 1
    attr.name = "thread1_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000; // 4KB stack
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)thread1_task, NULL, &attr) != NULL) {
        printf("thread1_task Create is OK!\r\n");
    }

    // 创建线程 2 (复用 attr 结构体，修改名字即可)
    attr.name = "thread2_task";
    if (osThreadNew((osThreadFunc_t)thread2_task, NULL, &attr) != NULL) {
        printf("thread2_task Create is OK!\r\n");
    }
}
app_run(main);

#else


#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

void unsafe_print_slowly(const char *str)
{
    while (*str) {
        printf("%c", *str++);
        osDelay(1); //给其他线程插队，制造打印混乱
    }
    printf("\r\n");
}

static void thread1_task(void *arg)
{
    unused(arg);
    while (1) {
        unsafe_print_slowly("[Thread1]");
        osDelay(100);
    }
}

static void thread2_task(void *arg)
{
    unused(arg);
    while (1) {
        unsafe_print_slowly("[Thread2]");
        osDelay(100);
    }
}

static void main(void)
{
    osThreadAttr_t attr;
    // 创建线程 1
    attr.name = "thread1_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000; // 4KB stack
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)thread1_task, NULL, &attr) != NULL) {
        printf("thread1_task Create is OK!\r\n");
    }

    // 创建线程 2 (复用 attr 结构体，修改名字即可)
    attr.name = "thread2_task";
    if (osThreadNew((osThreadFunc_t)thread2_task, NULL, &attr) != NULL) {
        printf("thread2_task Create is OK!\r\n");
    }
}

app_run(main);

#endif