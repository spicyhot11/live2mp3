#pragma once
#include <drogon/HttpController.h>
#include "../services/ConfigService.h"
#include "../services/SchedulerService.h"

using namespace drogon;

class DashboardController : public drogon::HttpController<DashboardController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(DashboardController::getStats, "/api/dashboard/stats", Get);
  METHOD_LIST_END

  DashboardController();

  void getStats(const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback);


private:
  ConfigService *lpConfigService_ = nullptr;
  SchedulerService *lpSchedulerService_ = nullptr;
};
