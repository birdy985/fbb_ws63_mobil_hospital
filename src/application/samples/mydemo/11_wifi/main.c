#include "lwip/netifapi.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"
#include "td_base.h"
#include "td_type.h"
#include "stdlib.h"
#include "uart.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "soc_osal.h"

#define WIFI_IFNAME_MAX_SIZE 16
#define WIFI_MAX_SSID_LEN 33
#define WIFI_SCAN_AP_LIMIT 64 // 扫描结果最大保存数量
#define WIFI_MAC_LEN 6
#define WIFI_STA_SAMPLE_LOG "[WIFI_STA_SAMPLE]"

// 【配置项】目标 Wi-Fi 的 SSID 和密码
#define WIFI_SCAN_EXPECT_SSID "BIR"
#define WIFI_CONNECT_KEY "12345678"

#define WIFI_NOT_AVAILABLE 0
#define WIFI_AVAILABLE 1
#define WIFI_GET_IP_MAX_COUNT 300 // DHCP 最大等待次数 (300 * 10ms = 3s)

enum {
    WIFI_STA_SAMPLE_INIT = 0,     /* 0: 初始态 (准备扫描) */
    WIFI_STA_SAMPLE_SCANING,      /* 1: 扫描中 (等待扫描完成回调) */
    WIFI_STA_SAMPLE_SCAN_DONE,    /* 2: 扫描完成 (开始匹配目标AP) */
    WIFI_STA_SAMPLE_FOUND_TARGET, /* 3: 匹配成功 (准备发起连接) */
    WIFI_STA_SAMPLE_CONNECTING,   /* 4: 连接中 (等待关联回调) */
    WIFI_STA_SAMPLE_CONNECT_DONE, /* 5: 关联成功 (准备启动DHCP) */
    WIFI_STA_SAMPLE_GET_IP,       /* 6: 获取IP中 (等待IP分配) */
} wifi_state_enum;

// 全局状态变量
static td_u8 g_wifi_state = WIFI_STA_SAMPLE_INIT;

// 前向声明
static td_void wifi_scan_state_changed(td_s32 state, td_s32 size);
static td_void wifi_connection_changed(td_s32 state, const wifi_linked_info_stru *info, td_s32 reason_code);

// 注册 Wi-Fi 事件回调结构体
wifi_event_stru wifi_event_cb = {
    .wifi_event_connection_changed = wifi_connection_changed, // 连接状态改变回调
    .wifi_event_scan_state_changed = wifi_scan_state_changed, // 扫描状态改变回调
};

/*****************************************************************************
 * 函数名称: wifi_scan_state_changed
 * 功能描述: STA 扫描完成后的回调函数
 * 输入参数: state - 扫描状态, size - 扫描到的AP数量
 *****************************************************************************/
static td_void wifi_scan_state_changed(td_s32 state, td_s32 size)
{
    UNUSED(state);
    UNUSED(size);
    // 只有在扫描状态下才处理该事件，避免重复触发
    if (g_wifi_state == WIFI_STA_SAMPLE_SCANING) {
        printf("%s::Scan done! Found %d APs.\r\n", WIFI_STA_SAMPLE_LOG, size);
        g_wifi_state = WIFI_STA_SAMPLE_SCAN_DONE; // 状态流转 -> 扫描完成
    }
}

/*****************************************************************************
 * 函数名称: wifi_connection_changed
 * 功能描述: STA 连接/断开 事件回调函数
 * 输入参数: state - 连接状态 (WIFI_AVAILABLE/WIFI_NOT_AVAILABLE)
 *****************************************************************************/
static td_void wifi_connection_changed(td_s32 state, const wifi_linked_info_stru *info, td_s32 reason_code)
{
    UNUSED(info);
    UNUSED(reason_code);

    if (state == WIFI_NOT_AVAILABLE) {
        // 连接失败或断开连接
        printf("%s::Connect fail or disconnected! Reason:%d. Retrying...\r\n", WIFI_STA_SAMPLE_LOG, reason_code);
        g_wifi_state = WIFI_STA_SAMPLE_INIT; // 状态重置 -> 重新开始扫描
    } else {
        // 连接成功
        printf("%s::Connect success!\r\n", WIFI_STA_SAMPLE_LOG);
        g_wifi_state = WIFI_STA_SAMPLE_CONNECT_DONE; // 状态流转 -> 连接完成
    }
}

/*****************************************************************************
 * 函数名称: example_get_match_network
 * 功能描述: 获取扫描结果并匹配目标 SSID
 * 输出参数: expected_bss - 匹配到的网络配置信息
 * 返回值: 0 成功, -1 失败
 *****************************************************************************/
td_s32 example_get_match_network(wifi_sta_config_stru *expected_bss)
{
    td_s32 ret;
    td_u32 num = WIFI_SCAN_AP_LIMIT;
    td_char expected_ssid[] = WIFI_SCAN_EXPECT_SSID;
    td_char key[] = WIFI_CONNECT_KEY;
    td_bool find_ap = TD_FALSE;
    td_u8 bss_index = 0;

    // 申请内存存放扫描结果 (该结构体较大，建议使用动态内存)
    td_u32 scan_len = sizeof(wifi_scan_info_stru) * WIFI_SCAN_AP_LIMIT;
    wifi_scan_info_stru *result = osal_kmalloc(scan_len, OSAL_GFP_ATOMIC);
    if (result == TD_NULL) {
        printf("%s::Malloc failed!\r\n", WIFI_STA_SAMPLE_LOG);
        return -1;
    }
    memset_s(result, scan_len, 0, scan_len);

    // 获取实际的扫描列表
    ret = wifi_sta_get_scan_info(result, &num);
    if (ret != 0) {
        printf("%s::Get scan info failed!\r\n", WIFI_STA_SAMPLE_LOG);
        osal_kfree(result);
        return -1;
    }

    // 遍历扫描结果，查找目标 SSID
    for (bss_index = 0; bss_index < num; bss_index++) {
        // 先判断长度，再判断内容，提高匹配效率和安全性
        if (strlen(expected_ssid) == strlen(result[bss_index].ssid)) {
            if (memcmp(expected_ssid, result[bss_index].ssid, strlen(expected_ssid)) == 0) {
                find_ap = TD_TRUE;
                printf("%s::Target AP found: %s\r\n", WIFI_STA_SAMPLE_LOG, result[bss_index].ssid);
                break;
            }
        }
    }

    if (find_ap == TD_FALSE) {
        osal_kfree(result);
        return -1; // 未找到目标 AP
    }

    // 找到目标 AP，填充连接配置结构体
    memset_s(expected_bss, sizeof(wifi_sta_config_stru), 0, sizeof(wifi_sta_config_stru));

    // 复制 SSID
    if (memcpy_s(expected_bss->ssid, WIFI_MAX_SSID_LEN, expected_ssid, strlen(expected_ssid)) != 0) {
        osal_kfree(result);
        return -1;
    }
    // 复制 BSSID (MAC地址)
    if (memcpy_s(expected_bss->bssid, WIFI_MAC_LEN, result[bss_index].bssid, WIFI_MAC_LEN) != 0) {
        osal_kfree(result);
        return -1;
    }
    // 复制加密方式 (必须与扫描结果一致)
    expected_bss->security_type = result[bss_index].security_type;

    // 复制密码
    if (memcpy_s(expected_bss->pre_shared_key, WIFI_MAX_SSID_LEN, key, strlen(key)) != 0) {
        osal_kfree(result);
        return -1;
    }

    expected_bss->ip_type = 1; /* 1：IP类型为动态 DHCP 获取 */

    osal_kfree(result); // 释放内存
    return 0;
}

/*****************************************************************************
 * 函数名称: example_check_dhcp_status
 * 功能描述: 检查 DHCP 获取 IP 是否成功
 *****************************************************************************/
td_bool example_check_dhcp_status(struct netif *netif_p, td_u32 *wait_count)
{
    // 检查网卡 IP 地址是否不为 0 (ip_addr_isany 返回 0 表示不是 0.0.0.0，即获取到了 IP)
    if ((ip_addr_isany(&(netif_p->ip_addr)) == 0) && (*wait_count <= WIFI_GET_IP_MAX_COUNT)) {
        printf("%s::DHCP success. IP:%s\r\n", WIFI_STA_SAMPLE_LOG, ip4addr_ntoa(netif_ip4_addr(netif_p)));
        return 0; // 成功
    }

    // 超时判断
    if (*wait_count > WIFI_GET_IP_MAX_COUNT) {
        printf("%s::DHCP timeout! Resetting to INIT state.\r\n", WIFI_STA_SAMPLE_LOG);
        *wait_count = 0;
        g_wifi_state = WIFI_STA_SAMPLE_INIT; // 状态重置 -> 重新开始
    }
    return -1; // 还在获取中或失败
}

/*****************************************************************************
 * 函数名称: example_sta_scan
 * 功能描述: 发起 Wi-Fi 扫描
 *****************************************************************************/
td_u32 example_sta_scan(td_void)
{
    td_u32 ret;
    wifi_scan_params_stru scan_params = {0};

    // 构造扫描参数，指定 SSID 可以加快扫描特定网络的速度
    ret =
        memcpy_s(scan_params.ssid, sizeof(scan_params.ssid), WIFI_SCAN_EXPECT_SSID, strlen(WIFI_SCAN_EXPECT_SSID) + 1);
    if (ret != EOK) {
        return ERRCODE_FAIL;
    }

    scan_params.scan_type = WIFI_SSID_SCAN; // 指定 SSID 扫描
    scan_params.ssid_len = strlen(WIFI_SCAN_EXPECT_SSID);

    // 调用底层 API 发起扫描
    ret = wifi_sta_scan_advance(&scan_params);
    return ret;
}

/*****************************************************************************
 * 函数名称: example_sta_function
 * 功能描述: STA 主业务逻辑循环 (状态机处理)
 *****************************************************************************/
td_s32 example_sta_function(td_void)
{
    td_char ifname[WIFI_IFNAME_MAX_SIZE + 1] = "wlan0";
    wifi_sta_config_stru expected_bss = {0};
    struct netif *netif_p = TD_NULL;
    td_u32 wait_count = 0;

    // 1. 使能 STA 模式
    if (wifi_sta_enable() != 0) {
        printf("%s::Enable STA failed!\r\n", WIFI_STA_SAMPLE_LOG);
        return -1;
    }
    printf("%s::STA enabled success.\r\n", WIFI_STA_SAMPLE_LOG);

    // 2. 进入主循环处理状态机
    do {
        // 稍微延时，避免空转占用 CPU
        (void)osDelay(10);

        switch (g_wifi_state) {
            case WIFI_STA_SAMPLE_INIT:
                printf("%s::State: INIT -> Start Scan\r\n", WIFI_STA_SAMPLE_LOG);
                g_wifi_state = WIFI_STA_SAMPLE_SCANING;
                if (example_sta_scan() != 0) {
                    printf("%s::Scan failed, retrying...\r\n", WIFI_STA_SAMPLE_LOG);
                    g_wifi_state = WIFI_STA_SAMPLE_INIT; // 失败重试
                    (void)osDelay(100);
                }
                break;

            case WIFI_STA_SAMPLE_SCANING:
                // 等待回调函数将状态改为 SCAN_DONE
                // 这里可以加一个超时判断，防止回调丢失导致死锁
                break;

            case WIFI_STA_SAMPLE_SCAN_DONE:
                printf("%s::State: SCAN_DONE -> Finding Target AP\r\n", WIFI_STA_SAMPLE_LOG);
                if (example_get_match_network(&expected_bss) != 0) {
                    printf("%s::Target AP not found, rescanning...\r\n", WIFI_STA_SAMPLE_LOG);
                    g_wifi_state = WIFI_STA_SAMPLE_INIT; // 没找到，重新扫描
                    (void)osDelay(1000);                 // 没找到时延时久一点再扫
                } else {
                    g_wifi_state = WIFI_STA_SAMPLE_FOUND_TARGET;
                }
                break;

            case WIFI_STA_SAMPLE_FOUND_TARGET:
                printf("%s::State: FOUND -> Start Connect\r\n", WIFI_STA_SAMPLE_LOG);
                g_wifi_state = WIFI_STA_SAMPLE_CONNECTING;
                if (wifi_sta_connect(&expected_bss) != 0) {
                    printf("%s::Connect API call failed.\r\n", WIFI_STA_SAMPLE_LOG);
                    g_wifi_state = WIFI_STA_SAMPLE_INIT;
                }
                break;

            case WIFI_STA_SAMPLE_CONNECTING:
                // 等待连接回调函数将状态修改
                break;

            case WIFI_STA_SAMPLE_CONNECT_DONE:
                printf("%s::State: CONNECTED -> Start DHCP\r\n", WIFI_STA_SAMPLE_LOG);
                g_wifi_state = WIFI_STA_SAMPLE_GET_IP;
                netif_p = netifapi_netif_find(ifname);
                if (netif_p == TD_NULL || netifapi_dhcp_start(netif_p) != 0) {
                    printf("%s::DHCP Start failed.\r\n", WIFI_STA_SAMPLE_LOG);
                    g_wifi_state = WIFI_STA_SAMPLE_INIT;
                }
                wait_count = 0; // 重置 DHCP 等待计数器
                break;

            case WIFI_STA_SAMPLE_GET_IP:
                if (example_check_dhcp_status(netif_p, &wait_count) == 0) {
                    // 获取 IP 成功，任务结束或进入保持连接状态
                    // 这里为了演示，获取IP后就跳出循环，实际业务可能需要保持心跳
                    printf("%s::WiFi Connection Established Completely!\r\n", WIFI_STA_SAMPLE_LOG);
                    return 0;
                }
                wait_count++;
                (void)osDelay(10); // DHCP 检查间隔 10ms + 10ms (loop)
                break;

            default:
                break;
        }
    } while (1);

    return 0;
}

/*****************************************************************************
 * 函数名称: sta_sample_init
 * 功能描述: 任务入口函数，负责初始化和启动流程
 *****************************************************************************/
int sta_sample_init(void *param)
{
    UNUSED(param);

    // 1. 注册 Wi-Fi 事件回调
    if (wifi_register_event_cb(&wifi_event_cb) != 0) {
        printf("%s::Register event cb failed.\r\n", WIFI_STA_SAMPLE_LOG);
        return -1;
    }
    printf("%s::Register event cb success.\r\n", WIFI_STA_SAMPLE_LOG);

    // 2. 等待 Wi-Fi 硬件初始化完成
    // 增加超时保护，避免死循环
    int timeout = 100; // 10s
    while (wifi_is_wifi_inited() == 0 && timeout > 0) {
        (void)osDelay(100);
        timeout--;
    }
    if (timeout <= 0) {
        printf("%s::Wait wifi init timeout.\r\n", WIFI_STA_SAMPLE_LOG);
        return -1;
    }
    printf("%s::WiFi hardware init success.\r\n", WIFI_STA_SAMPLE_LOG);

    // 3. 开始执行 STA 业务
    if (example_sta_function() != 0) {
        printf("%s::STA function exited with error.\r\n", WIFI_STA_SAMPLE_LOG);
        return -1;
    }
    return 0;
}

// 主入口
static void main(void)
{
    osThreadAttr_t attr;
    attr.name = "sta_sample_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000;
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)sta_sample_init, NULL, &attr) == NULL) {
        printf("%s::Create task failed.\r\n", WIFI_STA_SAMPLE_LOG);
    }
    printf("%s::Create task success.\r\n", WIFI_STA_SAMPLE_LOG);
}

app_run(main);