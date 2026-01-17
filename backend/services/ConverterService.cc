#include "ConverterService.h"
#include <array>
#include <cstdio>
#include <drogon/drogon.h>
#include <filesystem>
#include <fmt/core.h>

#include "../utils/FileUtils.h"
#include "HistoryService.h"

namespace fs = std::filesystem;

std::optional<std::string>
ConverterService::convertToMp3(const std::string &inputPath) {
  // 0. Check History
  std::string md5 = utils::calculateMD5(inputPath);
  if (md5.empty()) {
    LOG_ERROR << "Failed to calculate MD5 for: " << inputPath;
    return std::nullopt;
  }

  if (historyServicePtr->hasProcessed(md5)) {
    LOG_INFO << "File already processed (MD5 match): " << inputPath;
    return std::nullopt; // Or return existing path if we knew it? For now
                         // nullopt implies "nothing done"
  }

  auto config = configServicePtr->getConfig();
  std::string outputRoot = config.output.output_root;

  std::string outputPath = determineOutputPath(inputPath, outputRoot);

  // Ensure output directory exists
  try {
    fs::create_directories(fs::path(outputPath).parent_path());
  } catch (const fs::filesystem_error &e) {
    LOG_ERROR << "Failed to create output directory: " << e.what();
    return std::nullopt;
  }

  std::string cmd = fmt::format(
      "ffmpeg -y -i \"{}\" -vn -acodec libmp3lame -q:a 2 \"{}\" 2>&1",
      inputPath, outputPath);

  LOG_INFO << "Starting conversion: " << cmd;

  if (runFfmpeg(cmd)) {
    LOG_INFO << "Conversion successful: " << outputPath;

    // Handle "Keep Original"
    if (!config.output.keep_original) {
      try {
        fs::remove(inputPath);
        LOG_INFO << "Deleted original file: " << inputPath;
      } catch (const std::exception &e) {
        LOG_WARN << "Failed to delete original file: " << e.what();
      }
    }

    // Record in history
    std::string filename = fs::path(inputPath).filename().string();
    historyServicePtr->addRecord(inputPath, filename, md5);

    return outputPath;
  } else {
    LOG_ERROR << "Conversion failed for " << inputPath;
    // Cleanup failed output if exists
    if (fs::exists(outputPath)) {
      fs::remove(outputPath);
    }
    return std::nullopt;
  }
}

std::string
ConverterService::determineOutputPath(const std::string &inputPath,
                                      const std::string &outputRoot) {
  fs::path p(inputPath);
  std::string parentDir = p.parent_path().filename().string();
  std::string filename = p.stem().string() + ".mp3";

  return (fs::path(outputRoot) / parentDir / filename).string();
}

bool ConverterService::runFfmpeg(const std::string &cmd) {
  std::array<char, 128> buffer;
  std::string result;
  FILE *pipe = popen(cmd.c_str(), "r");

  if (!pipe) {
    LOG_ERROR << "popen() failed!";
    return false;
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }

  int returnCode = pclose(pipe);

  if (returnCode != 0) {
    LOG_ERROR << "FFmpeg error output:\n" << result;
    return false;
  }

  return true;
}

void ConverterService::initAndStart(const Json::Value &config) {
  LOG_DEBUG << "ConverterService initAndStart";
  if (system("ffmpeg -version > /dev/null 2>&1") != 0) {
    LOG_FATAL << "系统未检测到 ffmpeg，ConverterService 无法工作！";
    return;
  }

  configServicePtr = drogon::app().getPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  historyServicePtr = drogon::app().getPlugin<HistoryService>();
  if (!historyServicePtr) {
    LOG_FATAL << "HistoryService not found";
    return;
  }
}

void ConverterService::shutdown() { LOG_DEBUG << "ConverterService shutdown"; }

