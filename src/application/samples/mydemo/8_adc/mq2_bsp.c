#include "mq2_bsp.h"
#include "soc_osal.h"   // OSAL接口
#include "cmsis_os2.h"  // CMSIS接口
#include "app_init.h"   // 应用初始化
#include "common_def.h" // 通用宏定义
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "adc.h"
#include "adc_porting.h"

#define MQ2_ADC_CHANNEL ADC_CHANNEL_2 // 对应 GPIO_09
#define MQ2_RL_KOHM 4.7f              // 负载电阻
#define MQ2_R0_KOHM 35.904f           // 基准电阻
#define SENSOR_VCC_V 5.0f             // 传感器供电电压 5.0V

static char str_vol[32] = {0};
static char str_rs[32] = {0};
static char str_ppm[32] = {0};

void mq2_init(void)
{
    errcode_t ret = ERRCODE_SUCC; // 错误码存储
    // 打印任务启动信息
    osal_printk("ADC0 Manual Sample Task Start!\r\n");

    // 初始化ADC：使用500KHz时钟（采样率=500KHz/16≈31.25KHz）
    ret = uapi_adc_init(ADC_CLOCK_500KHZ);
    if (ret != ERRCODE_SUCC) {
        osal_printk("ADC Init Failed! Error Code: 0x%x\r\n", ret);
    }

    // ADC上电：使用常规精度模式（AFE_GADC_MODE），true=上电
    uapi_adc_power_en(AFE_GADC_MODE, true);

    // 开启ADC0通道
    ret = uapi_adc_open_channel(ADC_CHANNEL_0);
    if (ret != ERRCODE_SUCC) {
        osal_printk("ADC0 Channel Open Failed! Error Code: 0x%x\r\n", ret);
    }
}

void mq2_get_ppm(void)
{
    float Rs = 0.0f;
    float ppm = 0.0f;
    uint16_t adc_sample_val = 0; // 存储ADC采样值 (mV)

    // 获取采样值 (mV)
    // adc_port_read 是 Porting 层接口，返回的是 mV
    adc_port_read(MQ2_ADC_CHANNEL, &adc_sample_val);

    // 转换为电压 (V)
    float adc_vol = (float)adc_sample_val / 1000.0f;

    // --- 安全保护：防止电压为0导致后续除法崩溃 ---
    if (adc_vol < 0.01f) {
        adc_vol = 0.01f;
    }
    // --- 安全保护：防止电压超过电源电压 ---
    if (adc_vol >= SENSOR_VCC_V) {
        adc_vol = SENSOR_VCC_V - 0.01f;
    }

    // 计算传感器电阻 Rs
    // 公式：Rs = (Vcc - Vout) / Vout * RL
    Rs = ((SENSOR_VCC_V - adc_vol) / adc_vol) * MQ2_RL_KOHM;

    // 计算 PPM
    // ppm = pow((Rs / (R0 * 11.5428)), -1.5278); 
    float base = Rs / (MQ2_R0_KOHM * 11.5428f);
    ppm = pow(base, -1.5278f);


    // 清空缓冲区
    memset(str_vol, 0, sizeof(str_vol));
    memset(str_rs, 0, sizeof(str_rs));
    memset(str_ppm, 0, sizeof(str_ppm));

    // 格式化数据：%.2f 表示保留两位小数
    sprintf(str_vol, "%.3f", adc_vol); // 电压保留3位
    sprintf(str_rs, "%.2f", Rs);       // 电阻保留2位
    sprintf(str_ppm, "%.2f", ppm);     // PPM保留2位

    // 打印最终结果 (%s)
    printf("[MQ2] Vol:%s V | Rs:%s k | PPM:%s \r\n", str_vol, str_rs, str_ppm);

    // 6. 延时
    osal_msleep(100);
}