/* src/application/samples/mydemo/0_helloworld/main.c */
#include "soc_osal.h"    // OSAL接口
#include "cmsis_os2.h"   // CMSIS接口
#include "app_init.h"    // 应用初始化
#include "common_def.h"  // 通用宏定义
#include <stdio.h>

// 定义宏并赋值（1=使用CMSIS，0=使用OSAL）
// #define USE_CMSIS_RTOS
#define USE_CMSIS_RTOS 1 // 改为0则切换到OSAL方式

static void helloworld_task(void *arg)
{
    unused(arg);
    while (1) 
    { 
// #ifdef USE_CMSIS_RTOS
#if USE_CMSIS_RTOS
        printf("hello world (CMSIS)!\n");
        osDelay(10);  // CMSIS延时10个tick，每个tick是10ms
#else
        osal_printk("hello world (OSAL)!\n");
        osal_udelay(100);  // OSAL延时
#endif
    }
    
}
// #ifdef USE_CMSIS_RTOS
#if USE_CMSIS_RTOS

static void main(void)
{
    osThreadAttr_t attr;
    attr.name = "helloworld_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)helloworld_task, NULL, &attr) != NULL) 
    {
        printf("helloworld_task Create is OK!\r\n");
    }
}
#else
// OSAL方式实现
static void main(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)helloworld_task,
                                      0, "helloworld_task", 0x1000);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, 24);
        osal_kfree(task_handle);
    }

    osal_kthread_unlock();
}

#endif

app_run(main);
