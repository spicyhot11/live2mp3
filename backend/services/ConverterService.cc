#include "ConverterService.h"
#include <array>
#include <cstdio>
#include <drogon/drogon.h>
#include <filesystem>
#include <fmt/core.h>

#include "../utils/FileUtils.h"
#include "HistoryService.h"
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
