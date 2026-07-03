#include <stdint.h>

#include "ad8232_bsp.h"
#include "app_init.h"
#include "common_def.h"
#include "ecg_processor.h"
#include "soc_osal.h"
#include "tjc_display.h"
#include "tcxo.h"
#include <stdio.h>

#define AD8232_TASK_PRIO       26
#define AD8232_TASK_STACK_SIZE 0x1000
#define AD8232_SAMPLE_MS       10

static void ad8232_print_sample(const ecg_monitor_sample_t *sample)
{
    // printf("ECG,%lu,%lu,%u,%d,%d,%d,%d,%u,%u,%u,%u\r\n",
    //     (unsigned long)sample->seq,
    //     (unsigned long)sample->timestamp_ms,
    //     (unsigned int)sample->voltage_mv,
    //     (int)sample->baseline_mv,
    //     (int)sample->ecg_mv,
    //     (int)sample->filtered_mv,
    //     (int)sample->display_mv,
    //     (unsigned int)sample->r_peak,
    //     (unsigned int)sample->rr_interval_ms,
    //     (unsigned int)sample->bpm,
    //     (unsigned int)sample->quality);

     printf("%d\r\n",(int)sample->display_mv);
}

static void *ad8232_task(const char *arg)
{
    unused(arg);

    errcode_t ret = ad8232_init();
    if (ret != ERRCODE_SUCC) {
        return NULL;
    }

    ret = tjc_display_init();
    if (ret != ERRCODE_SUCC) {
        return NULL;
    }

    ecg_processor_init();
    tjc_display_clear_wave();
    tjc_display_send_test_pattern();
    printf("[AD8232] csv: ECG,seq,t_ms,raw_mv,base_mv,ecg_mv,filt_mv,disp_mv,r,rr_ms,bpm,q\r\n");
    printf("[AD8232] q: 0=GOOD,1=NOISY,2=SATURATED,3=FLATLINE,4=NO_R_PEAK\r\n");
    printf("[AD8232] sample target=%uHz, plot disp_mv or ecg_mv, not raw_mv\r\n",
        (unsigned int)(1000U / AD8232_SAMPLE_MS));

    uint32_t seq = 0;
    while (1) {
        uint16_t voltage_mv = 0;
        ret = ad8232_read_mv(&voltage_mv);
        if (ret == ERRCODE_SUCC) {
            ecg_input_sample_t input = {
                .seq = seq++,
                .timestamp_ms = (uint32_t)uapi_tcxo_get_ms(),
                .voltage_mv = voltage_mv,
            };
            ecg_monitor_sample_t output = {0};

            ecg_processor_process(&input, &output);
            tjc_display_send_sample(&output);
            if ((output.seq % 100U) == 0U) {
                ad8232_print_sample(&output);
            }
        } else {
            printf("[AD8232] read failed, ret=0x%x\r\n", (unsigned int)ret);
        }
        osal_msleep(AD8232_SAMPLE_MS);
    }

    return NULL;
}

static void ad8232_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)ad8232_task, NULL, "Ad8232Task", AD8232_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, AD8232_TASK_PRIO);
    } else {
        printf("[AD8232] create task failed\r\n");
    }
    osal_kthread_unlock();
}

app_run(ad8232_entry);
