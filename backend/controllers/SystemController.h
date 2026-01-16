#pragma once
#include "../services/ConfigService.h"
#include "services/SchedulerService.h"
#include <drogon/HttpController.h>

using namespace drogon;

class SystemController : public drogon::HttpController<SystemController> {
public:
  METHOD_LIST_BEGIN
  // Map REST API endpoints
  ADD_METHOD_TO(SystemController::getStatus, "/api/status", Get);
  ADD_METHOD_TO(SystemController::getConfig, "/api/config", Get);
  ADD_METHOD_TO(SystemController::updateConfig, "/api/config", Post);
  ADD_METHOD_TO(SystemController::triggerTask, "/api/trigger", Post);
  METHOD_LIST_END

  SystemController();

  void getStatus(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);
  void getConfig(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);
  void updateConfig(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);
  void triggerTask(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

private:
  ConfigService *lpConfigService_ = nullptr;
  SchedulerService *lpSchedulerService_ = nullptr;
};
