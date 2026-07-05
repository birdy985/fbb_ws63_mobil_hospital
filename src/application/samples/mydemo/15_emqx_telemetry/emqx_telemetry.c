#include "emqx_telemetry.h"
#include "securec.h"
#include "soc_osal.h"

static osal_mutex g_emqx_telemetry_lock;
static uint8_t g_emqx_telemetry_lock_ready;
static emqx_telemetry_snapshot_t g_emqx_telemetry_snapshot;

static void emqx_telemetry_ensure_lock(void)
{
    if (g_emqx_telemetry_lock_ready != 0) {
        return;
    }
    if (osal_mutex_init(&g_emqx_telemetry_lock) == OSAL_SUCCESS) {
        g_emqx_telemetry_lock_ready = 1;
    }
}

void emqx_telemetry_update_ecg(const emqx_ecg_telemetry_t *sample)
{
    if (sample == NULL) {
        return;
    }
    emqx_telemetry_ensure_lock();
    if (g_emqx_telemetry_lock_ready != 0) {
        (void)osal_mutex_lock(&g_emqx_telemetry_lock);
    }
    g_emqx_telemetry_snapshot.ecg = *sample;
    if (g_emqx_telemetry_lock_ready != 0) {
        osal_mutex_unlock(&g_emqx_telemetry_lock);
    }
}

void emqx_telemetry_update_spo2(const emqx_spo2_telemetry_t *sample)
{
    if (sample == NULL) {
        return;
    }
    emqx_telemetry_ensure_lock();
    if (g_emqx_telemetry_lock_ready != 0) {
        (void)osal_mutex_lock(&g_emqx_telemetry_lock);
    }
    g_emqx_telemetry_snapshot.spo2 = *sample;
    if (g_emqx_telemetry_lock_ready != 0) {
        osal_mutex_unlock(&g_emqx_telemetry_lock);
    }
}

void emqx_telemetry_update_gt_health(const emqx_gt_health_telemetry_t *sample)
{
    if (sample == NULL) {
        return;
    }
    emqx_telemetry_ensure_lock();
    if (g_emqx_telemetry_lock_ready != 0) {
        (void)osal_mutex_lock(&g_emqx_telemetry_lock);
    }
    g_emqx_telemetry_snapshot.gt_health = *sample;
    if (g_emqx_telemetry_lock_ready != 0) {
        osal_mutex_unlock(&g_emqx_telemetry_lock);
    }
}

void emqx_telemetry_get_snapshot(emqx_telemetry_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    emqx_telemetry_ensure_lock();
    if (g_emqx_telemetry_lock_ready != 0) {
        (void)osal_mutex_lock(&g_emqx_telemetry_lock);
    }
    if (memcpy_s(snapshot, sizeof(*snapshot), &g_emqx_telemetry_snapshot, sizeof(g_emqx_telemetry_snapshot)) != EOK) {
        (void)memset_s(snapshot, sizeof(*snapshot), 0, sizeof(*snapshot));
    }
    if (g_emqx_telemetry_lock_ready != 0) {
        osal_mutex_unlock(&g_emqx_telemetry_lock);
    }
}
