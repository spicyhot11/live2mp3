#pragma once

#include "ConfigService.h"
#include "services/HistoryService.h"
#include <drogon/plugins/Plugin.h>
#include <optional>
#include <string>

class ConverterService : public drogon::Plugin<ConverterService> {
public:
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  ConverterService() = delete;
  ConverterService(const ConverterService &) = delete;
  ConverterService(ConverterService &&) = delete;
  ConverterService &operator=(const ConverterService &) = delete;
  ConverterService &operator=(ConverterService &&) = delete;

  // Convert a single file to MP3 in the output directory.
  // Returns the path to the converted MP3 if successful, std::nullopt
  // otherwise.
  std::optional<std::string> convertToMp3(const std::string &inputPath);

private:
  ConfigService *configServicePtr = nullptr;
  HistoryService *historyServicePtr = nullptr;

  std::string determineOutputPath(const std::string &inputPath,
                                  const std::string &outputRoot);
  bool runFfmpeg(const std::string &cmd);
};
