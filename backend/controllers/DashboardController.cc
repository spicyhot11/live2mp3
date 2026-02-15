#include "DashboardController.h"
#include "../services/ConfigService.h"
#include "../services/SchedulerService.h"
#include <filesystem>

namespace fs = std::filesystem;

// Helper for directory size
static uint64_t getDirectorySize(const fs::path &dirPath) {
  uint64_t size = 0;
  try {
    if (!fs::exists(dirPath))
      return 0;
    for (const auto &entry : fs::recursive_directory_iterator(
             dirPath, fs::directory_options::skip_permission_denied)) {
      if (fs::is_regular_file(entry.status())) {
        size += fs::file_size(entry);
      }
    }
  } catch (const std::exception &e) {
    LOG_ERROR << "Error calculating size for " << dirPath.string() << ": "
              << e.what();
  }
  return size;
}

DashboardController::DashboardController() {
  LOG_INFO << "DashboardController initialized";
  lpConfigService_ = drogon::app().getSharedPlugin<ConfigService>();
  if (!lpConfigService_) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpSchedulerService_ = drogon::app().getSharedPlugin<SchedulerService>();
  if (!lpSchedulerService_) {
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

  // Disk Usage (return cached + status)
  {
    std::lock_guard<std::mutex> lock(diskStatsMutex_);
    ret["disk"] = cachedDiskStats_;
  }
  ret["disk"]["is_scanning"] = isScanningDisk_.load();

  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}

void DashboardController::triggerDiskScan(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  bool expected = false;
  if (isScanningDisk_.compare_exchange_strong(expected, true)) {
    // Start background thread
    std::thread([this]() { this->runDiskScan(); }).detach();

    Json::Value ret;
    ret["status"] = "started";
    callback(HttpResponse::newHttpJsonResponse(ret));
  } else {
    Json::Value ret;
    ret["status"] = "busy";
    callback(HttpResponse::newHttpJsonResponse(ret));
  }
}

void DashboardController::runDiskScan() {
  LOG_INFO << "Starting background disk scan...";
  Json::Value newStats;
  try {
    auto config = lpConfigService_->getConfig();

    // 1. Output Root
    std::string outPath = config.output.output_root;
    {
      Json::Value outStat;
      outStat["path"] = outPath;
      outStat["label"] = "Output";
      if (fs::exists(outPath)) {
        auto space = fs::space(outPath);
        outStat["total_space"] = (Json::UInt64)space.capacity;
        outStat["free_space"] = (Json::UInt64)space.available;
        outStat["used_size"] = (Json::UInt64)getDirectorySize(outPath);
      } else {
        outStat["error"] = "Path not found";
      }
      newStats["locations"].append(outStat);
    }

    // 2. Video Roots
    for (const auto &root : config.scanner.video_roots) {
      Json::Value rootStat;
      rootStat["path"] = root.path;
      rootStat["label"] = "Source";
      if (fs::exists(root.path)) {
        // partition space might be same as output if on same drive, but we
        // query it anyway
        auto space = fs::space(root.path);
        rootStat["total_space"] = (Json::UInt64)space.capacity;
        rootStat["free_space"] = (Json::UInt64)space.available;
        rootStat["used_size"] = (Json::UInt64)getDirectorySize(root.path);
      } else {
        rootStat["error"] = "Path not found";
      }
      newStats["locations"].append(rootStat);
    }

    // 3. Temp Directory (if configured)
    if (!config.temp.temp_dir.empty()) {
      Json::Value tempStat;
      tempStat["path"] = config.temp.temp_dir;
      tempStat["label"] = "Temp";
      tempStat["size_limit_mb"] = (Json::Int64)config.temp.size_limit_mb;
      if (fs::exists(config.temp.temp_dir)) {
        auto space = fs::space(config.temp.temp_dir);
        tempStat["total_space"] = (Json::UInt64)space.capacity;
        tempStat["free_space"] = (Json::UInt64)space.available;
        tempStat["used_size"] =
            (Json::UInt64)getDirectorySize(config.temp.temp_dir);
      } else {
        tempStat["error"] = "Path not found";
      }
      newStats["locations"].append(tempStat);
    }

  } catch (const std::exception &e) {
    LOG_ERROR << "Disk scan failed: " << e.what();
    newStats["error"] = e.what();
  }

  {
    std::lock_guard<std::mutex> lock(diskStatsMutex_);
    cachedDiskStats_ = newStats;
  }
  isScanningDisk_ = false;
  LOG_INFO << "Disk scan completed.";
}
