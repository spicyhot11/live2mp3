#include "MergerService.h"
#include "../utils/FfmpegUtils.h"
#include "ConfigService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

void MergerService::initAndStart(const Json::Value &config) {
  configServicePtr = drogon::app().getSharedPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "获取 ConfigService 插件失败";
    return;
  }
}

void MergerService::shutdown() { configServicePtr.reset(); }

std::optional<std::chrono::system_clock::time_point>
MergerService::parseTime(const std::string &filename) {
  std::tm tm = {};

  // 格式 1: [2026-01-06 09-47-38]
  // 正则: ^\[(\d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2})\]
  static std::regex re1(R"(^\[(\d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2})\])");
  std::smatch match;

  if (std::regex_search(filename, match, re1)) {
    std::istringstream ss(match[1].str());
    ss >> std::get_time(&tm, "%Y-%m-%d %H-%M-%S");
    if (!ss.fail()) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
  }

  // 格式 2: ...-20240801-151938-...
  // 正则: .*(\d{8})-(\d{6}).*
  static std::regex re2(R"((\d{8})-(\d{6}))");
  if (std::regex_search(filename, match, re2)) {
    std::string datePart = match[1].str();      // YYYYMMDD
    std::string timePart = match[2].str();      // HHMMSS
    std::string datetime = datePart + timePart; // YYYYMMDDHHMMSS

    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y%m%d%H%M%S");
    if (!ss.fail()) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
  }

  return std::nullopt;
}

std::optional<std::string> MergerService::mergeVideoFiles(
    const std::vector<std::string> &files, const std::string &outputDir,
    live2mp3::utils::FfmpegProgressCallback progressCallback) {
  if (files.empty())
    return std::nullopt;

  // 单文件情况：无需合并，直接返回
  if (files.size() == 1) {
    LOG_INFO << "只有单个文件，跳过合并: " << files[0];
    return files[0];
  }

  // 使用第一个文件的文件名作为基础，添加 "merged_" 前缀
  fs::path firstPath(files[0]);
  std::string filename = firstPath.filename().string();
  std::string outputName = "merged_" + filename;
  std::string outputPath = (fs::path(outputDir) / outputName).string();

  // 创建 concat 列表文件
  std::string listPath = (fs::path(outputDir) / "concat_list.txt").string();
  {
    std::ofstream listFile(listPath);
    if (!listFile.is_open()) {
      LOG_ERROR << "创建列表文件失败: " << listPath;
      return std::nullopt;
    }
    for (const auto &f : files) {
      listFile << "file '" << f << "'\n";
    }
  }

  LOG_INFO << "开始合并 " << files.size() << " 个文件 -> " << outputPath;

  // FFmpeg 命令：使用 concat 协议合并文件
  // -c copy 表示直接复制流，不重新编码（要求所有文件编码格式一致）
  std::string cmd =
      fmt::format("ffmpeg -f concat -safe 0 -i \"{}\" -c copy -y \"{}\" 2>&1",
                  listPath, outputPath);

  bool success = live2mp3::utils::runFfmpegWithProgress(cmd, progressCallback);

  // 清理列表文件
  if (fs::exists(listPath)) {
    fs::remove(listPath);
  }

  if (success) {
    LOG_INFO << "合并成功: " << outputPath;
    return outputPath;
  } else {
    LOG_ERROR << "合并失败";
    // 清理失败的输出文件
    if (fs::exists(outputPath)) {
      fs::remove(outputPath);
    }
    return std::nullopt;
  }
}
