#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_init.h"
#include "common_def.h"
#include "gt_health_sensor.h"
#include "soc_osal.h"

#define GT_HEALTH_TASK_PRIO             26
#define GT_HEALTH_TASK_STACK_SIZE       0x1000
#define GT_HEALTH_READ_TIMEOUT_MS       1000
#define GT_HEALTH_READ_FAIL_DELAY_MS    200
#define GT_HEALTH_BP_MIN_SYS            60
#define GT_HEALTH_BP_MAX_SYS            250
#define GT_HEALTH_BP_MIN_DIA            40
#define GT_HEALTH_BP_MAX_DIA            180

static uint8_t g_last_systolic_bp;
static uint8_t g_last_diastolic_bp;
static uint8_t g_last_microcirculation;

static bool gt_health_is_valid_bp(uint8_t systolic_bp, uint8_t diastolic_bp)
{
    if (systolic_bp < GT_HEALTH_BP_MIN_SYS || systolic_bp > GT_HEALTH_BP_MAX_SYS) {
        return false;
    }
    if (diastolic_bp < GT_HEALTH_BP_MIN_DIA || diastolic_bp > GT_HEALTH_BP_MAX_DIA) {
        return false;
    }
    return systolic_bp > diastolic_bp;
}

static void gt_health_print_data(const gt_health_data_t *data)
{
    bool bp_fresh = false;
    bool microcirculation_fresh = false;
    uint8_t microcirculation = 0;
    uint8_t systolic_bp = 0;
    uint8_t diastolic_bp = 0;

    if (data == NULL) {
        return;
    }

    microcirculation_fresh = data->microcirculation != 0;
    if (microcirculation_fresh) {
        g_last_microcirculation = data->microcirculation;
    }
    microcirculation = g_last_microcirculation;

    bp_fresh = gt_health_is_valid_bp(data->systolic_bp, data->diastolic_bp);
    if (bp_fresh) {
        g_last_systolic_bp = data->systolic_bp;
        g_last_diastolic_bp = data->diastolic_bp;
    }
    systolic_bp = g_last_systolic_bp;
    diastolic_bp = g_last_diastolic_bp;

    printf("[GT_HEALTH] HR=%u bpm, SpO2=%u%%, MC=%u%s, BP=%u/%u mmHg%s, wave0=%d wave31=%d wave63=%d\r\n",
        (unsigned int)data->heart_rate,
        (unsigned int)data->spo2,
        (unsigned int)microcirculation,
        microcirculation_fresh ? " fresh" : " cached",
        (unsigned int)systolic_bp,
        (unsigned int)diastolic_bp,
        bp_fresh ? " fresh" : " cached",
        (int)data->wave[0],
        (int)data->wave[31],
        (int)data->wave[63]);
}

static void *gt_health_task(const char *arg)
{
    unused(arg);

    errcode_t ret = gt_health_sensor_init();
    if (ret != ERRCODE_SUCC) {
        return NULL;
    }

    while (1) {
        gt_health_data_t data = {0};
        ret = gt_health_sensor_read(&data, GT_HEALTH_READ_TIMEOUT_MS);
        if (ret == ERRCODE_SUCC) {
            gt_health_print_data(&data);
        } else {
            printf("[GT_HEALTH] read timeout or frame lost\r\n");
            osal_msleep(GT_HEALTH_READ_FAIL_DELAY_MS);
        }
    }

    return NULL;
}

static void gt_health_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)gt_health_task, NULL,
        "GtHealthTask", GT_HEALTH_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, GT_HEALTH_TASK_PRIO);
    } else {
        printf("[GT_HEALTH] create task failed\r\n");
    }
    osal_kthread_unlock();
}

app_run(gt_health_entry);
