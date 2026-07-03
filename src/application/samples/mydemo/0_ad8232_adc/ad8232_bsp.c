#include "ad8232_bsp.h"

#include "adc.h"
#include "adc_porting.h"
#include "common_def.h"
#include "soc_osal.h"
#include <stdio.h>

#define AD8232_ADC_CHANNEL ADC_CHANNEL_2

errcode_t ad8232_init(void)
{
    errcode_t ret;

    ret = uapi_adc_init(ADC_CLOCK_NONE);
    if (ret != ERRCODE_SUCC) {
        printf("[AD8232] ADC init failed, ret=0x%x\r\n", (unsigned int)ret);
        return ret;
    }

    ret = uapi_adc_open_channel(AD8232_ADC_CHANNEL);
    if (ret != ERRCODE_SUCC) {
        printf("[AD8232] open ADC channel %u failed, ret=0x%x\r\n",
            (unsigned int)AD8232_ADC_CHANNEL, (unsigned int)ret);
        (void)uapi_adc_deinit();
        return ret;
    }

    printf("[AD8232] ADC ready: channel=%u, HH-D02 J6-7/GPIO_09/ADC2\r\n",
        (unsigned int)AD8232_ADC_CHANNEL);
    return ERRCODE_SUCC;
}

errcode_t ad8232_read_mv(uint16_t *voltage_mv)
{
    if (voltage_mv == NULL) {
        return ERRCODE_INVALID_PARAM;
    }

    return adc_port_read(AD8232_ADC_CHANNEL, voltage_mv);
}
