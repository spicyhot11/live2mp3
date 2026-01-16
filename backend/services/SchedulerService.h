#pragma once

#include <atomic>
#include <drogon/plugins/Plugin.h>
#include <mutex>
#include <string>
#include "ConfigService.h"
#include "MergerService.h"
#include "ScannerService.h"
#include "ConverterService.h" 


class SchedulerService : public drogon::Plugin<SchedulerService>  {
public:
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  // Start the periodic task
  void start();

  // Trigger run immediately (manual trigger)
  void triggerNow();

  bool isRunning() const { return running_; }
  std::string getCurrentFile();

private:
  SchedulerService() = default;
  void runTask();

  ConfigService *configServicePtr = nullptr;
  MergerService *mergerServicePtr = nullptr;
  ScannerService *scannerServicePtr = nullptr;
  ConverterService *converterServicePtr = nullptr;

  std::atomic<bool> running_{false};
  std::string currentFile_;
  std::mutex mutex_;
};
