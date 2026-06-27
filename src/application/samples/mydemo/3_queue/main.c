#define USE_QUEUE 1

#if USE_QUEUE

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

// 队列深度设为 10，能暂存10个数据
#define MSG_QUEUE_LEN 10
#define MSG_ITEM_SIZE sizeof(uint32_t)

static osMessageQueueId_t g_msg_queue_id = NULL;

// 线程1：生产者
static void thread1_task(void *arg)
{
    unused(arg);
    uint32_t send_data = 0;
    osStatus_t status;

    while (1) {
        send_data++;
        printf("[Thread1] >>> Enqueue: %d\r\n", send_data);

        // 发送消息
        // osWaitForever 参数的作用：如果队列满了（10个数据没被取走），Thread1 会在此阻塞（暂停）。
        // 直到 Thread2 取走数据腾出空间，Thread1 才会继续运行。
        // 这实现了自动的“流量控制”，防止数据丢失。
        status = osMessageQueuePut(g_msg_queue_id, &send_data, 0U, osWaitForever);

        if (status != osOK) {
            printf("[Thread1] Queue Put Failed!\r\n");
        }

        // 发送频率：100ms 一次
        osDelay(10);
    }
}

// 线程2：消费者
static void thread2_task(void *arg)
{
    unused(arg);
    uint32_t recv_data = 0;
    osStatus_t status;

    while (1) {
        // 从队列取数据
        // osWaitForever 参数的作用：如果队列空了，Thread2 会在此阻塞（等待），不消耗 CPU。
        status = osMessageQueueGet(g_msg_queue_id, &recv_data, NULL, osWaitForever);

        if (status == osOK) {
            printf("[Thread2] <<< Dequeue: %d\r\n", recv_data);
        }

        // 处理频率：500ms 一次
        // 现象：虽然处理慢，但数据都在队列里排队，不会丢失。
        osDelay(50);
    }
}

static void main(void)
{
    // 创建消息队列
    g_msg_queue_id = osMessageQueueNew(MSG_QUEUE_LEN, MSG_ITEM_SIZE, NULL);
    if (g_msg_queue_id == NULL) {
        printf("Queue Create Failed!\r\n");
        return;
    }

    osThreadAttr_t attr;
    // 创建线程 1
    attr.name = "thread1_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000; // 4KB stack
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)thread1_task, NULL, &attr) != NULL) {
        printf("thread1_task Create is OK!\r\n");
    }

    // 创建线程 2 (复用 attr 结构体，修改名字即可)
    attr.name = "thread2_task";
    if (osThreadNew((osThreadFunc_t)thread2_task, NULL, &attr) != NULL) {
        printf("thread2_task Create is OK!\r\n");
    }
}

app_run(main);

#else

#include "soc_osal.h"
#include "cmsis_os2.h"
#include "app_init.h"
#include "common_def.h"
#include <stdio.h>

#define BUFFER_SIZE 10

// 全局数组：模拟一个环形缓冲区
volatile uint32_t g_buffer[BUFFER_SIZE] = {0};
// 读写索引
volatile uint32_t g_write_idx = 0;
volatile uint32_t g_read_idx = 0;

// 线程1：生产者
static void thread1_task(void *arg)
{
    unused(arg);
    uint32_t send_data = 0;
    while (1) {
        // 计算写入位置 (0~9 循环)
        uint32_t current_idx = g_write_idx % BUFFER_SIZE;

        // 没有检查消费者是否读完，直接覆盖写入！
        // 因为没有 OS 的阻塞机制，生产者停不下来。
        g_buffer[current_idx] = send_data;

        printf("[Thread1] Wrote Arr[%d] = %d\r\n", current_idx, send_data);

        g_write_idx++; // 索引继续增加
        send_data++;
        osDelay(10); // 发得快：100ms
    }
}

// 线程2：消费者
static void thread2_task(void *arg)
{
    unused(arg);
    uint32_t recv_data = 0;
    while (1) {
        // 如果有新数据才读
        if (g_read_idx < g_write_idx) {
            uint32_t current_idx = g_read_idx % BUFFER_SIZE;

            // 读取数据
            recv_data = g_buffer[current_idx];

            printf("[Thread2] Read  Arr[%d] = %d\r\n", current_idx, recv_data);

            g_read_idx++;
        } else {
            
        }

        osDelay(50); // 收得慢：500ms
    }
}

static void main(void)
{
    osThreadAttr_t attr;
    // 创建线程 1
    attr.name = "thread1_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x1000; // 4KB stack
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)thread1_task, NULL, &attr) != NULL) {
        printf("thread1_task Create is OK!\r\n");
    }

    // 创建线程 2 (复用 attr 结构体，修改名字即可)
    attr.name = "thread2_task";
    if (osThreadNew((osThreadFunc_t)thread2_task, NULL, &attr) != NULL) {
        printf("thread2_task Create is OK!\r\n");
    }
}

app_run(main);

#endif