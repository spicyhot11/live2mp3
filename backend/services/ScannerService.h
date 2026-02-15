#pragma once

#include "ConfigService.h"
#include <drogon/plugins/Plugin.h>
#include <string>
#include <vector>

/**
 * @brief 文件扫描服务类
 *
 * 负责扫描配置的目录，查找符合条件的文件。
 * 支持多种过滤模式（白名单、黑名单、正则、通配符等）。
 */
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

  /**
   * @brief 扫描所有配置的根目录
   *
   * @return ScanResult 包含所有发现的文件路径
   */
  ScanResult scan();

private:
  std::shared_ptr<ConfigService> configServicePtr;

  bool shouldInclude(const std::string &filepath,
                     const VideoRootConfig &rootConfig,
                     const std::vector<std::string> &extensions);
};
