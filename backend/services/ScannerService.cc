#include "ScannerService.h"
#include "ConfigService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

void ScannerService::initAndStart(const Json::Value &config) {
  configServicePtr = drogon::app().getPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "Failed to get ConfigService plugin";
    return;
  }
}

void ScannerService::shutdown() { configServicePtr = nullptr; }

ScannerService::ScanResult ScannerService::scan() {
  ScanResult result;
  auto config = configServicePtr->getConfig();
  auto scannerConfig = config.scanner;

  for (const auto &root : scannerConfig.video_roots) {
    if (!fs::exists(root)) {
      LOG_WARN << "Video root does not exist: " << root;
      continue;
    }

    try {
      for (const auto &entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
          std::string path = entry.path().string();
          if (shouldInclude(path)) {
            result.files.push_back(path);
          }
        }
      }
    } catch (const std::exception &e) {
      LOG_ERROR << "Error scanning root " << root << ": " << e.what();
    }
  }
  return result;
}

bool ScannerService::shouldInclude(const std::string &filepath) {
  auto config = configServicePtr->getConfig();
  auto scannerConfig = config.scanner;
  std::string filename = fs::path(filepath).filename().string();
  std::string extension = fs::path(filepath).extension().string();

  // 1. Check extension
  bool extMatch = false;
  for (const auto &ext : scannerConfig.extensions) {
    if (extension == ext) {
      extMatch = true;
      break;
    }
  }
  if (!extMatch)
    return false;

  // 2. Check Allow Lists (Regex and Simple)
  // If ANY allow list is not empty, we default to "deny unless allowed".
  // If BOTH allow lists are empty, we default to "allow unless denied".
  bool hasAllowList = !scannerConfig.allow_list.empty() ||
                      !scannerConfig.simple_allow_list.empty();

  if (hasAllowList) {
    bool allowMatch = false;

    // Check Regex Allow List
    for (const auto &pattern : scannerConfig.allow_list) {
      try {
        std::regex re(pattern);
        if (std::regex_search(filepath, re)) {
          allowMatch = true;
          break;
        }
      } catch (...) {
        // Ignore invalid regex
      }
    }

    // Check Simple Allow List (Substring)
    if (!allowMatch) {
      for (const auto &pattern : scannerConfig.simple_allow_list) {
        if (!pattern.empty() && filepath.find(pattern) != std::string::npos) {
          allowMatch = true;
          break;
        }
      }
    }

    if (!allowMatch)
      return false;
  }

  // 3. Check Deny Lists (Regex and Simple)

  // Check Regex Deny List
  if (!scannerConfig.deny_list.empty()) {
    for (const auto &pattern : scannerConfig.deny_list) {
      try {
        std::regex re(pattern);
        if (std::regex_search(filepath, re)) {
          return false; // Matched deny list
        }
      } catch (...) {
        // Ignore
      }
    }
  }

  // Check Simple Deny List
  if (!scannerConfig.simple_deny_list.empty()) {
    for (const auto &pattern : scannerConfig.simple_deny_list) {
      if (!pattern.empty() && filepath.find(pattern) != std::string::npos) {
        return false; // Matched simple deny list
      }
    }
  }

  return true;
}

