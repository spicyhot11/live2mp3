#include "HistoryController.h"


HistoryController::HistoryController() {
  LOG_INFO << "HistoryController initialized";
  lpConfigService_ = drogon::app().getPlugin<ConfigService>();

  if (lpConfigService_ == nullptr) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpHistoryService_ = drogon::app().getPlugin<HistoryService>();

  if (lpHistoryService_ == nullptr) {
    LOG_FATAL << "HistoryService not found";
    return;
  }
}

void HistoryController::getAll(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto records = lpHistoryService_->getAll();
  Json::Value ret;
  Json::Value arr(Json::arrayValue);

  for (const auto &r : records) {
    Json::Value item;
    item["id"] = r.id;
    item["filepath"] = r.filepath;
    item["filename"] = r.filename;
    item["md5"] = r.md5;
    item["processed_at"] = r.processed_at;
    arr.append(item);
  }

  ret["data"] = arr;
  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}

void HistoryController::removeRecord(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, int id) {
  bool success = lpHistoryService_->removeRecord(id);
  Json::Value ret;
  if (success) {
    ret["status"] = "deleted";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } else {
    ret["error"] = "Failed to delete record";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }
}
