#pragma once

#include "ConfigService.h"
#include <drogon/plugins/Plugin.h>
#include <string>
#include <vector>

class ScannerService : public drogon::Plugin<ScannerService> {
public:
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  struct ScanResult {
    std::vector<std::string> files;
  };

  ScannerService() = default;
  ScannerService(const ScannerService &) = delete;
  ScannerService(ScannerService &&) = delete;
  ScannerService &operator=(const ScannerService &) = delete;
  ScannerService &operator=(ScannerService &&) = delete;

  // Scan all configured roots and return list of file paths
  ScanResult scan();

private:
  ConfigService *configServicePtr = nullptr;

  bool shouldInclude(const std::string &filepath);
};
