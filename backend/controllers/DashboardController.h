#pragma once
#include "../services/ConfigService.h"
#include "../services/SchedulerService.h"
#include <drogon/HttpController.h>

using namespace drogon;

/**
 * @brief 仪表盘控制器类
 *
 * 专门用于处理Web前端仪表盘(Dashboard)相关的数据请求，
 * 包括统计信息获取、磁盘扫描触发等。
 */
class DashboardController : public drogon::HttpController<DashboardController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(DashboardController::getStats, "/api/dashboard/stats", Get);
  ADD_METHOD_TO(DashboardController::triggerDiskScan,
                "/api/dashboard/disk_scan", Post);
  METHOD_LIST_END

  DashboardController();

  /**
   * @brief 获取仪表盘统计数据
   *
   * 返回包括文件计数、状态分布、CPU/内存使用情况等概览数据。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void getStats(const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 触发磁盘扫描
   *
   * 计算输出目录和临时目录的实际磁盘占用大小。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void triggerDiskScan(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

private:
  void runDiskScan();

  std::shared_ptr<ConfigService> lpConfigService_;
  std::shared_ptr<SchedulerService> lpSchedulerService_;

  std::atomic<bool> isScanningDisk_{false};
  std::mutex diskStatsMutex_;
  Json::Value cachedDiskStats_;
};
