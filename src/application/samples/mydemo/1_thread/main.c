#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

// 线程1任务函数
static void thread1_task(void *arg)
{
    unused(arg);
    int count = 0;
    while (1) {
        printf("[Thread1] Hello World! count=%d\r\n", count++);
        // 1 tick = 10ms，这里延时 100 ticks = 1秒
        osDelay(100);
    }
}

// 线程2任务函数
static void thread2_task(void *arg)
{
    unused(arg);
    while (1) {
        printf("[Thread2] I am running...\r\n");
        // 延时 200 ticks = 2秒
        osDelay(200);
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
