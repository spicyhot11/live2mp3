#include "HistoryController.h"

HistoryController::HistoryController() {
  LOG_INFO << "HistoryController initialized";
  lpConfigService_ = drogon::app().getSharedPlugin<ConfigService>();
  if (!lpConfigService_) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpPendingFileService_ = drogon::app().getSharedPlugin<PendingFileService>();
  if (!lpPendingFileService_) {
    LOG_FATAL << "PendingFileService not found";
    return;
  }
}

void HistoryController::getAll(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto records = lpPendingFileService_->getCompletedFiles();
  Json::Value ret;
  Json::Value arr(Json::arrayValue);

  for (const auto &r : records) {
    Json::Value item;
    item["id"] = r.id;
    item["filepath"] = r.getFilepath();
    item["filename"] = r.filename;
    item["fingerprint"] = r.fingerprint;
    item["start_time"] = r.start_time;
    item["end_time"] = r.end_time;
    arr.append(item);
  }

  ret["data"] = arr;
  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}

void HistoryController::removeRecord(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, int id) {
  bool success = lpPendingFileService_->removeFileById(id);
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
