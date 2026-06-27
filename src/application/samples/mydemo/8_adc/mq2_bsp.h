#ifndef _MQ2_BSP_H_
#define _MQ2_BSP_H_

#include <stdint.h>

// 初始化 MQ2 传感器 (ADC初始化)
void mq2_init(void);

// 获取并打印 PPM 值 (核心逻辑)
void mq2_get_ppm(void);

#endif