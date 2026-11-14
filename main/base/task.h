/*
 * @Description: 按用途创建任务，统一诊断分配失败并回收临时任务。
 * @Author: LILYGO_L
 * @Date: 2026-11-13 12:10:17
 * @LastEditTime: 2026-11-13 12:24:24
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lilygo_box::tasks {

// 涉及 NVS、OTA、Flash 擦写的任务必须明确选择内部栈。
enum class StackRegion { kPsram, kInternal };

/**
 * @brief 启动静态内部栈的回收任务，在应用初始化前调用一次
 * @return 初始化成功返回 true；不申请动态栈或队列
 */
bool Initialize();

/**
 * @brief 创建必须保留内部栈的任务，并统一输出分配失败诊断
 * @param stack_bytes 栈大小，单位为字节
 * @param handle 可选输出句柄，为空时不保存句柄
 * @return 成功返回 pdPASS，失败返回 pdFAIL，参数仍由调用方管理
 * @note 保留 xTaskCreate 的生命周期语义，入口不允许返回，使用 vTaskDelete 退出。
 *       用于 Flash 写入、系统回调和暂未验证外部栈兼容性的任务。
 */
BaseType_t CreateInternal(TaskFunction_t entry, const char* name,
    uint32_t stack_bytes, void* context, UBaseType_t priority,
    TaskHandle_t* handle);

/**
 * @brief 创建由调用方管理生命周期的任务，失败时记录堆余量与最大连续块
 * @param stack_bytes 栈大小，单位为字节
 * @param handle 必须保存返回句柄，结束后从其他任务调用 DeleteManaged
 * @param region 栈所在区域，不允许运行时跨区域回退
 * @return 成功返回 pdPASS，失败返回 pdFAIL 并将输出句柄置空
 * @note 入口不能返回，也不能使用 vTaskDelete 删除本任务；无运行时内部栈回退。
 */
BaseType_t CreateManaged(TaskFunction_t entry, const char* name,
    uint32_t stack_bytes, void* context, UBaseType_t priority,
    TaskHandle_t* handle, StackRegion region = StackRegion::kPsram);

/**
 * @brief 从其他任务回收 CreateManaged 创建且已完成业务工作的任务
 * @param handle 本接口管理的非空任务句柄，由调用方保证没有其他持有者回收
 * @note 调用方负责同步停止和所有权；禁止传入空句柄或当前任务。
 */
void DeleteManaged(TaskHandle_t handle);

/**
 * @brief 创建执行完入口后自动回收的临时任务，默认将栈放入 PSRAM
 * @param entry 执行完业务后正常返回的入口函数
 * @param stack_bytes 栈大小，单位为字节
 * @param context 创建成功后交给入口处理，失败时仍归调用方所有
 * @param region 默认 PSRAM，不在运行时回退到内部栈
 * @return 成功返回 pdPASS，失败返回 pdFAIL，不执行入口
 * @note 入口必须正常返回，让局部对象先析构；禁止自行删除、外部删除或永久挂起。
 *       初始化回收服务后才可调用；完成后无需再申请清理任务。
 */
BaseType_t CreateDetached(TaskFunction_t entry, const char* name,
    uint32_t stack_bytes, void* context, UBaseType_t priority,
    StackRegion region = StackRegion::kPsram);

}  // namespace lilygo_box::tasks
