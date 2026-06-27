#define LED_KEY 0

#if !LED_KEY

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>
#include "pinctrl.h"
#include "gpio.h"
#include "platform_core_rom.h"

static void blink_task(const char *arg)
{
    unused(arg);
    // 配置引脚为普通GPIO模式
    uapi_pin_set_mode(GPIO_02, HAL_PIO_FUNC_GPIO);
    // 配置GPIO为输出模式 低电平
    uapi_gpio_set_dir(GPIO_02, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(GPIO_02, GPIO_LEVEL_LOW);
    while (1) {
        uapi_gpio_toggle(GPIO_02); // 电平反转
        osDelay(50);               // 延时500ms
    }
}

static void main(void)
{
    printf("Enter blink_entry()!\r\n");

    osThreadAttr_t attr;
    attr.name = "blink_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)blink_task, NULL, &attr) != NULL) {
        printf("blink_task Created\r\n");
    }
}

app_run(main);

#else

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>
#include "pinctrl.h"
#include "gpio.h"
#include "platform_core_rom.h"

// 硬件引脚定义
#define BSP_LED     7   // RED LED
#define GREEN_LED   11  // GREEN LED
#define BUTTON_GPIO 14  // 按键 GPIO

// 全局状态变量
static volatile int g_ledState = 0;

// 中断回调函数
static void gpio_callback_func(pin_t pin, uintptr_t param)
{
    UNUSED(pin);
    UNUSED(param);
    // 翻转状态
    g_ledState = !g_ledState;
    printf("Button pressed. State: %d\r\n", g_ledState);
}

// 任务逻辑
static void button_task(void *arg)
{
    unused(arg);

    // 初始化 LED (RED)
    uapi_pin_set_mode(BSP_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(BSP_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);

    // 初始化 LED (GREEN)
    uapi_pin_set_mode(GREEN_LED, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(GREEN_LED, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(GREEN_LED, GPIO_LEVEL_LOW);

    // 初始化 按键 (Input + Interrupt)
    uapi_pin_set_mode(BUTTON_GPIO, HAL_PIO_FUNC_GPIO);
    gpio_select_core(BUTTON_GPIO, CORES_APPS_CORE);
    uapi_gpio_set_dir(BUTTON_GPIO, GPIO_DIRECTION_INPUT);
    
    // 注册中断：下降沿触发
    errcode_t ret = uapi_gpio_register_isr_func(BUTTON_GPIO, GPIO_INTERRUPT_FALLING_EDGE, gpio_callback_func);
    if (ret != 0) {
        printf("Register ISR Failed!\r\n");
        uapi_gpio_unregister_isr_func(BUTTON_GPIO);
    }

    printf("Button Task Init OK.\r\n");

    while (1) {
        // 根据中断改变的状态控制 LED
        if (g_ledState) {
            uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_HIGH);
        } else {
            uapi_gpio_set_val(BSP_LED, GPIO_LEVEL_LOW);
        }
        osDelay(10); 
    }
}

// 统一入口 Main 函数
static void main(void)
{
    printf("Enter button_entry !\r\n");

    osThreadAttr_t attr = {0}; 

    attr.name = "button_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;    
    attr.stack_size = 0x1000;  
    attr.priority = osPriorityNormal;

    // 创建任务
    if (osThreadNew((osThreadFunc_t)button_task, NULL, &attr) != NULL) {
        printf("button_task Created Successfully\r\n");
    } else {
        printf("button_task Create Failed!\r\n");
    }
}

app_run(main);

#endif