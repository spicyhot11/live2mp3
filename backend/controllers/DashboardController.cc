#include "DashboardController.h"
#include "../services/ConfigService.h"
#include "../services/SchedulerService.h"
#include <filesystem>

namespace fs = std::filesystem;

DashboardController::DashboardController() {
  LOG_INFO << "DashboardController initialized";
  lpConfigService_ = drogon::app().getPlugin<ConfigService>();

  if (lpConfigService_ == nullptr) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpSchedulerService_ = drogon::app().getPlugin<SchedulerService>();

  if (lpSchedulerService_ == nullptr) {
    LOG_FATAL << "SchedulerService not found";
    return;
  }
}

void DashboardController::getStats(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  Json::Value ret;

  // Status
  ret["status"]["running"] = lpSchedulerService_->isRunning();
  ret["status"]["current_file"] = lpSchedulerService_->getCurrentFile();

  // Disk Usage
  try {
    auto config = lpConfigService_->getConfig();
    std::string path = config.output.output_root;

    // If path doesn't exist, try to create or just use current path if creation
    // checks fail (not doing creation here) Just check if exists.
    if (fs::exists(path)) {
      fs::space_info space = fs::space(path);
      ret["disk"]["capacity"] = (Json::UInt64)space.capacity;
      ret["disk"]["free"] = (Json::UInt64)space.free;
      ret["disk"]["available"] = (Json::UInt64)space.available;
    } else {
      ret["disk"]["error"] = "Output directory does not exist";
    }
  } catch (const std::exception &e) {
    ret["disk"]["error"] = e.what();
  }

  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}
