#include "MergerService.h"
#include "../utils/FfmpegUtils.h"
#include "ConfigService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <fmt/format.h>
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
  std::smatch match;

  // 格式 1: [2026-01-06 09-47-38]
  // 正则: ^\[(\d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2})\]
  static std::regex re1(R"(^\[(\d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2})\])");
  if (std::regex_search(filename, match, re1)) {
    std::istringstream ss(match[1].str());
    ss >> std::get_time(&tm, "%Y-%m-%d %H-%M-%S");
    if (!ss.fail()) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
  }

  // 格式 2: 录制-房间号-YYYYMMDD-HHMMSS-...
  // 正则: ^录制-([^-]+)-(\d{8})-(\d{6})
  static std::regex re2(R"(^录制-([^-]+)-(\d{8})-(\d{6}))");
  if (std::regex_search(filename, match, re2)) {
    std::string datetime = match[2].str() + match[3].str(); // YYYYMMDDHHMMSS
    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y%m%d%H%M%S");
    if (!ss.fail()) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
  }

  // 格式 3: 通用兜底格式 (例如只有 YYYYMMDD-HHMMSS)
  // 仅在明确不是前两种格式时尝试，避免冲突
  static std::regex re3(R"((\d{8})-(\d{6}))");
  if (std::regex_search(filename, match, re3)) {
    // 检查这个匹配是否实际上是日期的一部分（由于前面已经处理了特定前缀，这里可以作为宽松匹配）
    std::string datetime = match[1].str() + match[2].str();
    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y%m%d%H%M%S");
    if (!ss.fail()) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
  }

  return std::nullopt;
}

std::string MergerService::parseTitle(const std::string &filename) {
  std::smatch match;

  // 格式1: [2026-01-06 12-16-34][主播名][标题].flv/ts
  // 提取第二个方括号内的主播名
  static std::regex re1(R"(^\[[^\]]+\]\[([^\]]+)\])");
  if (std::regex_search(filename, match, re1)) {
    return match[1].str();
  }

  // 格式2: 录制-主播名-20260125-111024-223-标题.flv
  // 提取"录制-"后、时间戳前的主播名
  static std::regex re2(R"(^录制-([^-]+)-\d{8}-\d{6})");
  if (std::regex_search(filename, match, re2)) {
    return match[1].str();
  }

  // 无法解析时返回空字符串
  return "";
}

std::optional<std::string> MergerService::mergeVideoFiles(
    const std::vector<std::string> &files, const std::string &outputDir,
    live2mp3::utils::FfmpegProgressCallback progressCallback,
    live2mp3::utils::CancelCheckCallback cancelCheck,
    std::function<void(pid_t)> pidCallback) {
  if (files.empty())
    return std::nullopt;

  // 获取配置
  auto config = configServicePtr->getConfig();

  // 单文件情况：无需合并，直接返回
  if (files.size() == 1) {
    LOG_INFO << "只有单个文件，跳过合并: " << files[0];
    return files[0];
  }

  // 使用第一个文件的文件名作为基础，添加 "_merged" 后缀
  fs::path firstPath(files[0]);
  std::string stem = firstPath.stem().string();
  // 使用配置的扩展名
  std::string extension = config.output.video_extension;
  std::string outputName = stem + "_merged" + extension;
  std::string outputPath = (fs::path(outputDir) / outputName).string();

  // 生成临时写入路径（添加 _writing 后缀）
  std::string writingName = stem + "_merged_writing" + extension;
  std::string writingPath = (fs::path(outputDir) / writingName).string();

  // 创建 concat 列表文件
  std::string listPath =
      (fs::path(outputDir) / (stem + "_concat_list.txt")).string();
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

  LOG_INFO << "开始合并 " << files.size() << " 个文件 -> " << writingPath
           << " (临时文件)";

  // FFmpeg 命令：使用配置中的命令模板
  // -c copy 表示直接复制流，不重新编码（要求所有文件编码格式一致）
  // 先输出到临时文件
  std::string cmdTemplate = config.ffmpeg.merge_command;
  std::string cmd;
  try {
    cmd = fmt::format(fmt::runtime(cmdTemplate), fmt::arg("input", listPath),
                      fmt::arg("output", writingPath));
  } catch (const std::exception &e) {
    LOG_ERROR << "Failed to format merge command: " << e.what();
    return std::nullopt;
  }

  // 获取所有输入文件的总时长用于计算进度百分比
  int totalDuration = live2mp3::utils::getTotalMediaDuration(files);
  if (totalDuration < 0) {
    LOG_WARN << "无法获取媒体总时长，进度百分比将不可用";
    totalDuration = 0;
  }

  bool success = live2mp3::utils::runFfmpegWithProgress(
      cmd, progressCallback, totalDuration, cancelCheck, nullptr, pidCallback);

  // 清理列表文件
  if (fs::exists(listPath)) {
    fs::remove(listPath);
  }

  if (success) {
    // 合并成功，重命名为最终文件名
    try {
      fs::rename(writingPath, outputPath);
      LOG_INFO << "合并成功: " << outputPath;
      return outputPath;
    } catch (const fs::filesystem_error &e) {
      LOG_ERROR << "重命名文件失败: " << writingPath << " -> " << outputPath
                << ", 错误: " << e.what();
      // 清理临时文件
      if (fs::exists(writingPath)) {
        fs::remove(writingPath);
      }
      return std::nullopt;
    }
  } else {
    LOG_ERROR << "合并失败";
    // 清理失败的临时文件
    if (fs::exists(writingPath)) {
      fs::remove(writingPath);
    }
    return std::nullopt;
  }
}
