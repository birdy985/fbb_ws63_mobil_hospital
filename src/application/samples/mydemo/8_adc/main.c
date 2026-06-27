#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>
#include "mq2_bsp.h"

// 任务函数
static void mq2_task(void *arg)
{
    (void)arg;

    // 硬件初始化
    mq2_init();

    printf("MQ2 Task Started...\r\n");

    // 主循环
    while (1) {
        // 获取数据并打印
        mq2_get_ppm();
        // 延时 100ms
        osDelay(10);
    }
}

static void main(void)
{
    osThreadAttr_t attr = {0};

    attr.name = "mq2_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)mq2_task, NULL, &attr) != NULL) {
        printf("mq2_task Create is OK!\r\n");
    } else {
        printf("mq2_task Create Failed!\r\n");
    }
}

app_run(main);