#pragma once

#include "ConfigService.h"
#include <chrono>
#include <drogon/plugins/Plugin.h>
#include <optional>
#include <string>
#include <vector>

class MergerService : public drogon::Plugin<MergerService> {
public:
  MergerService() = default;
  MergerService(const MergerService &) = delete;
  MergerService(MergerService &&) = delete;
  MergerService &operator=(const MergerService &) = delete;
  MergerService &operator=(MergerService &&) = delete;
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  // Scan output directories and merge split files
  void mergeAll();

private:
  struct Mp3File {
    std::string path;
    std::chrono::system_clock::time_point time;
    std::string originalFilename;
  };

  ConfigService *configServicePtr = nullptr;

  std::optional<std::chrono::system_clock::time_point>
  parseTime(const std::string &filename);
  void processDirectory(const std::string &dirPath);
  bool mergeFiles(const std::vector<Mp3File> &files,
                  const std::string &outputDir);
  bool runFfmpegConcat(const std::string &listPath,
                       const std::string &outputPath);
};
