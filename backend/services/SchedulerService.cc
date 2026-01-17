#include "SchedulerService.h"
#include "ConfigService.h"
#include "ConverterService.h"
#include "MergerService.h"
#include "ScannerService.h"
#include <drogon/drogon.h>
#include <thread>

void SchedulerService::initAndStart(const Json::Value &config) {
  configServicePtr = drogon::app().getPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "Failed to get ConfigService plugin";
    return;
  }

  mergerServicePtr = drogon::app().getPlugin<MergerService>();
  if (!mergerServicePtr) {
    LOG_FATAL << "Failed to get MergerService plugin";
    return;
  }

  scannerServicePtr = drogon::app().getPlugin<ScannerService>();
  if (!scannerServicePtr) {
    LOG_FATAL << "Failed to get ScannerService plugin";
    return;
  }

  converterServicePtr = drogon::app().getPlugin<ConverterService>();
  if (!converterServicePtr) {
    LOG_FATAL << "Failed to get ConverterService plugin";
    return;
  }
}

void SchedulerService::shutdown() {
  configServicePtr = nullptr;
  mergerServicePtr = nullptr;
  scannerServicePtr = nullptr;
  converterServicePtr = nullptr;
}

std::string SchedulerService::getCurrentFile() {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentFile_;
}

void SchedulerService::start() {
  drogon::app().registerBeginningAdvice([this]() {
    auto config = configServicePtr->getConfig();
    int interval = config.scheduler.scan_interval_seconds;
    if (interval <= 0)
      interval = 60;

    drogon::app().getLoop()->runEvery(interval * 1.0,
                                      [this]() { this->runTask(); });

    LOG_INFO << "Scheduler started with interval " << interval << "s";
  });
}

void SchedulerService::triggerNow() {
  drogon::app().getLoop()->queueInLoop([this]() { this->runTask(); });
}

void SchedulerService::runTask() {
  if (running_) {
    LOG_INFO << "Task already running, skipping";
    return;
  }
  running_ = true;

  std::thread([this]() {
    LOG_INFO << "Starting scheduled task...";

    // 1. Scan
    auto scanResult = scannerServicePtr->scan();
    LOG_INFO << "Found " << scanResult.files.size() << " files to convert";

    // 2. Convert
    for (const auto &file : scanResult.files) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        currentFile_ = file;
      }
      converterServicePtr->convertToMp3(file);
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      currentFile_ = "";
    }

    // 3. Merge
    // Only run merge if conversions happened? Or always?
    // Always check for mergeable files.
    mergerServicePtr->mergeAll();

    LOG_INFO << "Task finished.";
    running_ = false;
  }).detach();
}

