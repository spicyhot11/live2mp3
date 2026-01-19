#include "HistoryController.h"

HistoryController::HistoryController() {
  LOG_INFO << "HistoryController initialized";
  lpConfigService_ = drogon::app().getPlugin<ConfigService>();

  if (lpConfigService_ == nullptr) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpPendingFileService_ = drogon::app().getPlugin<PendingFileService>();

  if (lpPendingFileService_ == nullptr) {
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
    item["filepath"] = r.filepath;
    // Extract filename from filepath
    size_t lastSlash = r.filepath.find_last_of("/\\");
    std::string filename = (lastSlash == std::string::npos)
                               ? r.filepath
                               : r.filepath.substr(lastSlash + 1);
    item["filename"] = filename;
    item["md5"] = r.current_md5;
    item["processed_at"] = r.updated_at;
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
