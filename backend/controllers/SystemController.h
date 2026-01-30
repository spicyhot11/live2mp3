#pragma once
#include "../services/ConfigService.h"
#include "services/SchedulerService.h"
#include <drogon/HttpController.h>

using namespace drogon;

/**
 * @brief 系统控制器类
 *
 * 负责系统级的操作，如状态查询、配置管理和任务触发。
 */
class SystemController : public drogon::HttpController<SystemController> {
public:
  METHOD_LIST_BEGIN
  // Map REST API endpoints
  ADD_METHOD_TO(SystemController::getStatus, "/api/status", Get);
  ADD_METHOD_TO(SystemController::getDetailedStatus, "/api/status/detailed",
                Get);
  ADD_METHOD_TO(SystemController::getConfig, "/api/config", Get);
  ADD_METHOD_TO(SystemController::updateConfig, "/api/config", Post);
  ADD_METHOD_TO(SystemController::triggerTask, "/api/trigger", Post);
  METHOD_LIST_END

  SystemController();

  /**
   * @brief 获取简要系统状态
   *
   * 是否正在运行、当前处理文件等基础信息。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void getStatus(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 获取详细系统状态
   *
   * 包括各个阶段的详细信息，用于调试或详细监控。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void
  getDetailedStatus(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 获取当前系统配置
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void getConfig(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 更新系统配置
   *
   * 接收JSON格式的配置并更新系统设置。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void updateConfig(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 触发一次任务执行
   *
   * 手动触发调度器的任务循环。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void triggerTask(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

private:
  std::shared_ptr<ConfigService> lpConfigService_;
  std::shared_ptr<SchedulerService> lpSchedulerService_;
};
