#pragma once
#include "../services/ConfigService.h"
#include "../services/SchedulerService.h"
#include <drogon/HttpController.h>

using namespace drogon;

class DashboardController : public drogon::HttpController<DashboardController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(DashboardController::getStats, "/api/dashboard/stats", Get);
  ADD_METHOD_TO(DashboardController::triggerDiskScan,
                "/api/dashboard/disk_scan", Post);
  METHOD_LIST_END

  DashboardController();

  void getStats(const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback);

  void triggerDiskScan(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

private:
  void runDiskScan();

  ConfigService *lpConfigService_ = nullptr;
  SchedulerService *lpSchedulerService_ = nullptr;

  std::atomic<bool> isScanningDisk_{false};
  std::mutex diskStatsMutex_;
  Json::Value cachedDiskStats_;
};
