#pragma once
#include "../services/ConfigService.h"
#include <drogon/HttpController.h>
#include "../services/HistoryService.h"

using namespace drogon;

class HistoryController : public drogon::HttpController<HistoryController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(HistoryController::getAll, "/api/history", Get);
  ADD_METHOD_TO(HistoryController::removeRecord, "/api/history/{id}", Delete);
  METHOD_LIST_END

  HistoryController();

  void getAll(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);
  void removeRecord(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback,
                    int id);

private:
  ConfigService *lpConfigService_ = nullptr;
  HistoryService *lpHistoryService_ = nullptr;
};
