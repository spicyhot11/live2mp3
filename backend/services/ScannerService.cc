#include "ScannerService.h"
#include "ConfigService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

// Helper for Glob matching
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
  // Glob usually matches the whole string
  // But for subdirs, maybe we match the name? Yes.
  try {
    std::regex re(reStr);
    return std::regex_match(str, re);
  } catch (...) {
    return false;
  }
}

static bool checkRule(const std::string &subDirName, const FilterRule &rule) {
  if (rule.type == "exact") {
    return subDirName == rule.pattern;
  } else if (rule.type == "regex") {
    try {
      std::regex re(rule.pattern);
      return std::regex_search(subDirName,
                               re); // Use search for flexibility or match?
      // "search" is more forgiving (partial match ok if regex not anchored).
    } catch (...) {
      return false;
    }
  } else if (rule.type == "glob") {
    return globMatch(subDirName, rule.pattern);
  }
  return false;
}

void ScannerService::initAndStart(const Json::Value &config) {
  configServicePtr = drogon::app().getSharedPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "Failed to get ConfigService plugin";
    return;
  }
}

void ScannerService::shutdown() { configServicePtr.reset(); }

ScannerService::ScanResult ScannerService::scan() {
  ScanResult result;
  auto config = configServicePtr->getConfig();
  auto scannerConfig = config.scanner;

  for (const auto &rootConfig : scannerConfig.video_roots) {
    auto rootPath = rootConfig.path;
    if (rootPath.empty())
      continue;
    if (!fs::exists(rootPath)) {
      LOG_WARN << "Video root does not exist: " << rootPath;
      continue;
    }

    try {
      for (const auto &entry : fs::recursive_directory_iterator(rootPath)) {
        if (entry.is_regular_file()) {
          std::string path = entry.path().string();
          // Pass rootConfig to shouldInclude
          if (shouldInclude(path, rootConfig, scannerConfig.extensions)) {
            result.files.push_back(path);
          }
        }
      }
    } catch (const std::exception &e) {
      LOG_ERROR << "Error scanning root " << rootPath << ": " << e.what();
    }
  }
  return result;
}

bool ScannerService::shouldInclude(const std::string &filepath,
                                   const VideoRootConfig &rootConfig,
                                   const std::vector<std::string> &extensions) {

  std::string filename = fs::path(filepath).filename().string();
  std::string extension = fs::path(filepath).extension().string();

  // 1. Check extension
  bool extMatch = false;
  for (const auto &ext : extensions) {
    if (extension == ext) {
      extMatch = true;
      break;
    }
  }
  if (!extMatch)
    return false;

  // 2. Check Subdirectory Rules
  // Get relative path from root
  try {
    fs::path p(filepath);
    fs::path root(rootConfig.path);
    fs::path relative = fs::relative(p, root);

    // Determine the "first level subdirectory"
    // If relative path is "a/b/c.mp4", first dir is "a".
    // If relative path is "c.mp4", first dir is empty/root.

    std::string firstDir = "";
    if (relative.has_parent_path()) {
      auto it = relative.begin();
      if (it != relative.end()) {
        firstDir = it->string();
        // If the file is directly in root, 'it' refers to filename?
        // fs::relative("/root/file.mp4", "/root") -> "file.mp4"
        // it points to "file.mp4".
        // We want directory name.
        // If relative path has strictly more than 1 component, the first
        // component is a dir. If it has 1 component, it is the file itself in
        // the root.

        // Check if 'root / *it' is a directory?
        // Or rely on logic: we filter "Subdirectories".
        // If file is in root, it has NO subdirectory.
        if (firstDir == relative.filename().string() &&
            relative.begin() == --relative.end()) {
          firstDir = ""; // File is in root
        }
      }
    }

    // Now apply rules to firstDir
    // If firstDir is empty (file in root), how to handle?
    // - Whitelist mode: User specifies "allow list". If list ["music"], implies
    // "only allow from music". Root file -> empty != music. Deny.
    // - Blacklist mode: User specifies "deny list". Root file -> empty != deny
    // rules (unless deny rule matches empty string?). Allow.

    bool isWhitelist = (rootConfig.filter_mode == "whitelist");

    if (rootConfig.rules.empty()) {
      // If whitelist empty: Allow nothing? Or allow everything? Usually
      // whitelist empty -> allow nothing. If blacklist empty: Allow everything.
      if (isWhitelist)
        return false;
      else
        return true;
    }

    bool ruleMatched = false;
    for (const auto &rule : rootConfig.rules) {
      if (checkRule(firstDir, rule)) {
        ruleMatched = true;
        break;
      }
    }

    if (isWhitelist) {
      // In whitelist mode, we include ONLY if rule matched
      return ruleMatched;
    } else {
      // In blacklist mode, we include ONLY if rule NOT matched
      return !ruleMatched;
    }

  } catch (const std::exception &) {
    return false;
  }

  return true;
}
