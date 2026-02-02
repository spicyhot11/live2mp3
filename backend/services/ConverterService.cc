#include "ConverterService.h"
#include "../utils/FfmpegUtils.h"
#include "../utils/FileUtils.h"
#include "PendingFileService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <fmt/core.h>
#include <regex>

namespace fs = std::filesystem;

// Helper for Glob matching (same as ScannerService)
static bool globMatch(const std::string &str, const std::string &pattern) {
  std::string reStr;
  for (size_t i = 0; i < pattern.size(); ++i) {
    char c = pattern[i];
    if (c == '*')
      reStr += ".*";
    else if (c == '?')
      reStr += ".";
    else if (std::string(".^+|{}()[]\\").find(c) != std::string::npos) {
      reStr += "\\";
      reStr += c;
    } else {
      reStr += c;
    }
  }
  try {
    std::regex re(reStr);
    return std::regex_match(str, re);
  } catch (...) {
    return false;
  }
}

static bool checkDeleteRule(const std::string &subDirName,
                            const FilterRule &rule) {
  if (rule.type == "exact") {
    return subDirName == rule.pattern;
  } else if (rule.type == "regex") {
    try {
      std::regex re(rule.pattern);
      return std::regex_search(subDirName, re);
    } catch (...) {
      return false;
    }
  } else if (rule.type == "glob") {
    return globMatch(subDirName, rule.pattern);
  }
  return false;
}

std::optional<std::string>
ConverterService::convertToMp3(const std::string &inputPath) {
  // 0. Check History
  std::string fingerprint =
      live2mp3::utils::calculateFileFingerprint(inputPath);
  if (fingerprint.empty()) {
    LOG_ERROR << "无法计算文件指纹: " << inputPath;
    return std::nullopt;
  }

  if (pendingFileServicePtr->isProcessed(fingerprint)) {
    LOG_INFO << "文件已处理 (fingerprint match): " << inputPath;
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

  // 获取输入文件时长用于计算进度百分比
  int totalDuration = live2mp3::utils::getMediaDuration(inputPath);
  if (totalDuration < 0) {
    totalDuration = 0;
  }

  if (live2mp3::utils::runFfmpegWithProgress(cmd, nullptr, totalDuration)) {
    LOG_INFO << "Conversion successful: " << outputPath;

    // Handle source file deletion based on per-root settings
    bool shouldDelete = false;

    // Find which root this file belongs to
    for (const auto &rootConfig : config.scanner.video_roots) {
      fs::path rootPath(rootConfig.path);
      fs::path inputP(inputPath);

      // Check if inputPath is under this root
      auto rel = fs::relative(inputP, rootPath);
      if (!rel.empty() && rel.native()[0] != '.') {
        // File is under this root
        if (!rootConfig.enable_delete) {
          // Deletion not enabled for this root
          shouldDelete = false;
          break;
        }

        // Get first subdirectory
        std::string firstDir = "";
        if (rel.has_parent_path()) {
          auto it = rel.begin();
          if (it != rel.end()) {
            firstDir = it->string();
            if (firstDir == rel.filename().string() &&
                rel.begin() == --rel.end()) {
              firstDir = ""; // File is in root directly
            }
          }
        }

        // Apply delete rules
        bool isWhitelist = (rootConfig.delete_mode == "whitelist");

        if (rootConfig.delete_rules.empty()) {
          // Empty whitelist = delete nothing, Empty blacklist = delete all
          shouldDelete = !isWhitelist;
        } else {
          bool ruleMatched = false;
          for (const auto &rule : rootConfig.delete_rules) {
            if (checkDeleteRule(firstDir, rule)) {
              ruleMatched = true;
              break;
            }
          }

          if (isWhitelist) {
            shouldDelete = ruleMatched; // Delete only matched
          } else {
            shouldDelete = !ruleMatched; // Delete all except matched (blacklist
                                         // = keep these)
          }
        }
        break; // Found the root, done
      }
    }

    // Fallback to global setting if no root matched or no per-root config
    if (!shouldDelete && !config.output.keep_original) {
      // Global setting says don't keep = delete
      // But per-root settings take precedence if enable_delete is configured
      // Actually, let's check: if no root matched enable_delete, use global
      bool anyRootHasDeleteEnabled = false;
      for (const auto &rc : config.scanner.video_roots) {
        if (rc.enable_delete) {
          anyRootHasDeleteEnabled = true;
          break;
        }
      }
      if (!anyRootHasDeleteEnabled && !config.output.keep_original) {
        shouldDelete = true;
      }
    }

    if (shouldDelete) {
      try {
        fs::remove(inputPath);
        LOG_INFO << "Deleted original file: " << inputPath;
      } catch (const std::exception &e) {
        LOG_WARN << "Failed to delete original file: " << e.what();
      }
    }

    // Record in history (mark as completed)
    pendingFileServicePtr->markAsCompleted(inputPath);

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
  return determineOutputPathWithExt(inputPath, outputRoot, ".mp3");
}

std::string
ConverterService::determineOutputPathWithExt(const std::string &inputPath,
                                             const std::string &outputRoot,
                                             const std::string &ext) {
  fs::path p(inputPath);
  std::string parentDir = p.parent_path().filename().string();
  std::string filename = p.stem().string() + ext;

  return (fs::path(outputRoot) / parentDir / filename).string();
}

std::optional<std::string> ConverterService::convertToAv1Mp4(
    const std::string &inputPath, const std::string &outputDir,
    live2mp3::utils::FfmpegProgressCallback progressCallback) {
  auto config = configServicePtr->getConfig();

  // 确定输出路径
  std::string outputPath;
  if (outputDir.empty()) {
    // 未指定输出目录时，使用默认逻辑（会拼接父目录名）
    std::string targetDir = config.temp.temp_dir.empty()
                                ? config.output.output_root
                                : config.temp.temp_dir;
    outputPath = determineOutputPathWithExt(inputPath, targetDir, ".mp4");
  } else {
    // 明确指定输出目录时，直接在该目录下输出
    fs::path p(inputPath);
    std::string filename = p.stem().string() + ".mp4";
    outputPath = (fs::path(outputDir) / filename).string();
  }

  // 生成临时写入路径（添加 _writing 后缀）
  fs::path outputFsPath(outputPath);
  std::string writingPath =
      (outputFsPath.parent_path() / (outputFsPath.stem().string() + "_writing" +
                                     outputFsPath.extension().string()))
          .string();

  // 确保输出目录存在
  try {
    fs::create_directories(fs::path(outputPath).parent_path());
  } catch (const fs::filesystem_error &e) {
    LOG_ERROR << "创建输出目录失败: " << e.what();
    return std::nullopt;
  }

  // FFmpeg 命令：使用 SVT-AV1 编码器进行 AV1 转码
  // CRF 30 提供较好的质量/大小平衡
  // 先输出到临时文件
  std::string cmd = fmt::format("ffmpeg -y -i \"{}\" -c:v libsvtav1 -crf 30 "
                                "-preset 6 -c:a aac -b:a 128k \"{}\" 2>&1",
                                inputPath, writingPath);

  LOG_INFO << "开始 AV1 转换: " << inputPath << " -> " << writingPath
           << " (临时文件)";

  // 获取输入文件时长用于计算进度百分比
  int totalDuration = live2mp3::utils::getMediaDuration(inputPath);
  if (totalDuration < 0) {
    LOG_WARN << "无法获取媒体时长，进度百分比将不可用: " << inputPath;
    totalDuration = 0;
  }

  if (live2mp3::utils::runFfmpegWithProgress(cmd, progressCallback, totalDuration)) {
    // 转换成功，重命名为最终文件名
    try {
      fs::rename(writingPath, outputPath);
      LOG_INFO << "AV1 转换成功: " << outputPath;
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
    LOG_ERROR << "AV1 转换失败: " << inputPath;
    // 清理失败的临时文件
    if (fs::exists(writingPath)) {
      fs::remove(writingPath);
    }
    return std::nullopt;
  }
}

std::optional<std::string> ConverterService::extractMp3FromVideo(
    const std::string &videoPath, const std::string &outputDir,
    live2mp3::utils::FfmpegProgressCallback progressCallback) {
  auto config = configServicePtr->getConfig();

  // 确定输出路径
  std::string outputPath;
  if (outputDir.empty()) {
    // 未指定输出目录时，使用默认逻辑（会拼接父目录名）
    outputPath = determineOutputPathWithExt(videoPath,
                                            config.output.output_root, ".mp3");
  } else {
    // 明确指定输出目录时，直接在该目录下输出
    fs::path p(videoPath);
    std::string filename = p.stem().string() + ".mp3";
    outputPath = (fs::path(outputDir) / filename).string();
  }

  // 生成临时写入路径（添加 _writing 后缀）
  fs::path outputFsPath(outputPath);
  std::string writingPath =
      (outputFsPath.parent_path() / (outputFsPath.stem().string() + "_writing" +
                                     outputFsPath.extension().string()))
          .string();

  // 确保输出目录存在
  try {
    fs::create_directories(fs::path(outputPath).parent_path());
  } catch (const fs::filesystem_error &e) {
    LOG_ERROR << "创建输出目录失败: " << e.what();
    return std::nullopt;
  }

  // FFmpeg 命令：提取音频并转为 MP3
  // 先输出到临时文件
  std::string cmd = fmt::format(
      "ffmpeg -y -i \"{}\" -vn -acodec libmp3lame -q:a 2 \"{}\" 2>&1",
      videoPath, writingPath);

  LOG_INFO << "开始提取 MP3: " << videoPath << " -> " << writingPath
           << " (临时文件)";

  // 获取输入文件时长用于计算进度百分比
  int totalDuration = live2mp3::utils::getMediaDuration(videoPath);
  if (totalDuration < 0) {
    LOG_WARN << "无法获取媒体时长，进度百分比将不可用: " << videoPath;
    totalDuration = 0;
  }

  if (live2mp3::utils::runFfmpegWithProgress(cmd, progressCallback, totalDuration)) {
    // 提取成功，重命名为最终文件名
    try {
      fs::rename(writingPath, outputPath);
      LOG_INFO << "MP3 提取成功: " << outputPath;
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
    LOG_ERROR << "MP3 提取失败: " << videoPath;
    // 清理失败的临时文件
    if (fs::exists(writingPath)) {
      fs::remove(writingPath);
    }
    return std::nullopt;
  }
}

uint64_t ConverterService::getTempDirUsage() {
  auto config = configServicePtr->getConfig();
  std::string tempDir = config.temp.temp_dir;

  if (tempDir.empty() || !fs::exists(tempDir)) {
    return 0;
  }

  uint64_t size = 0;
  try {
    for (const auto &entry : fs::recursive_directory_iterator(
             tempDir, fs::directory_options::skip_permission_denied)) {
      if (fs::is_regular_file(entry.status())) {
        size += fs::file_size(entry);
      }
    }
  } catch (const std::exception &e) {
    LOG_ERROR << "Error calculating temp dir size: " << e.what();
  }
  return size;
}

bool ConverterService::hasTempSpace(uint64_t requiredBytes) {
  auto config = configServicePtr->getConfig();

  // If no temp dir configured, or no limit set, always return true
  if (config.temp.temp_dir.empty() || config.temp.size_limit_mb <= 0) {
    return true;
  }

  uint64_t limitBytes =
      static_cast<uint64_t>(config.temp.size_limit_mb) * 1024 * 1024;
  uint64_t currentUsage = getTempDirUsage();

  return (currentUsage + requiredBytes) <= limitBytes;
}

void ConverterService::initAndStart(const Json::Value &config) {
  LOG_DEBUG << "ConverterService initAndStart";
  if (system("ffmpeg -version > /dev/null 2>&1") != 0) {
    LOG_FATAL << "系统未检测到 ffmpeg，ConverterService 无法工作！";
    return;
  }

  configServicePtr = drogon::app().getSharedPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  pendingFileServicePtr = drogon::app().getSharedPlugin<PendingFileService>();
  if (!pendingFileServicePtr) {
    LOG_FATAL << "PendingFileService not found";
    return;
  }
}

void ConverterService::shutdown() { LOG_DEBUG << "ConverterService shutdown"; }
