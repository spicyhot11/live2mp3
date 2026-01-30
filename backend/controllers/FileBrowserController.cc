#include "FileBrowserController.h"
#include "../utils/FileUtils.h"
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

// Helper for Glob matching (copied from ScannerService for directory filtering)
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

static bool checkRule(const std::string &dirName, const FilterRule &rule) {
  if (rule.type == "exact") {
    return dirName == rule.pattern;
  } else if (rule.type == "regex") {
    try {
      std::regex re(rule.pattern);
      return std::regex_search(dirName, re);
    } catch (...) {
      return false;
    }
  } else if (rule.type == "glob") {
    return globMatch(dirName, rule.pattern);
  }
  return false;
}

// Check if a directory should be included based on rules
// This applies to ALL path components, not just the first level
static bool shouldIncludeDirectory(const std::string &dirName,
                                   const VideoRootConfig &rootConfig) {
  bool isWhitelist = (rootConfig.filter_mode == "whitelist");

  if (rootConfig.rules.empty()) {
    // Empty whitelist -> deny all, Empty blacklist -> allow all
    return !isWhitelist;
  }

  bool ruleMatched = false;
  for (const auto &rule : rootConfig.rules) {
    if (checkRule(dirName, rule)) {
      ruleMatched = true;
      break;
    }
  }

  return isWhitelist ? ruleMatched : !ruleMatched;
}

FileBrowserController::FileBrowserController() {
  LOG_INFO << "FileBrowserController initialized";

  lpConfigService_ = drogon::app().getSharedPlugin<ConfigService>();
  if (!lpConfigService_) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpPendingFileService_ = drogon::app().getSharedPlugin<PendingFileService>();
  if (!lpPendingFileService_) {
    LOG_FATAL << "PendingFileService not found";
    return;
  }

  lpScannerService_ = drogon::app().getSharedPlugin<ScannerService>();
  if (!lpScannerService_) {
    LOG_FATAL << "ScannerService not found";
    return;
  }

  lpSchedulerService_ = drogon::app().getSharedPlugin<SchedulerService>();
  if (!lpSchedulerService_) {
    LOG_FATAL << "SchedulerService not found";
    return;
  }
}

void FileBrowserController::browseFiles(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto config = lpConfigService_->getConfig();
  std::string pathParam = req->getParameter("path");

  Json::Value ret;

  // If no path specified, return list of root directories
  if (pathParam.empty()) {
    Json::Value rootsArr(Json::arrayValue);
    for (const auto &root : config.scanner.video_roots) {
      if (fs::exists(root.path) && fs::is_directory(root.path)) {
        Json::Value item;
        item["name"] = fs::path(root.path).filename().string();
        item["path"] = root.path;
        item["is_root"] = true;
        rootsArr.append(item);
      }
    }
    ret["current_path"] = "";
    ret["directories"] = rootsArr;
    ret["files"] = Json::Value(Json::arrayValue);
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
    return;
  }

  // Validate path exists and is a directory
  fs::path browsePath(pathParam);
  if (!fs::exists(browsePath) || !fs::is_directory(browsePath)) {
    ret["error"] = "Path does not exist or is not a directory";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  // Find which root this path belongs to
  const VideoRootConfig *matchedRoot = nullptr;
  for (const auto &root : config.scanner.video_roots) {
    if (pathParam.find(root.path) == 0) {
      matchedRoot = &root;
      break;
    }
  }

  if (!matchedRoot) {
    ret["error"] = "Path is not under any configured root";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  // Build parent path
  std::string parentPath = "";
  if (pathParam != matchedRoot->path) {
    parentPath = fs::path(pathParam).parent_path().string();
  }

  Json::Value dirsArr(Json::arrayValue);
  Json::Value filesArr(Json::arrayValue);

  try {
    for (const auto &entry : fs::directory_iterator(browsePath)) {
      std::string entryName = entry.path().filename().string();
      std::string entryPath = entry.path().string();

      if (entry.is_directory()) {
        // Apply filter rules to directory name (recursive filtering)
        if (!shouldIncludeDirectory(entryName, *matchedRoot)) {
          continue;
        }

        Json::Value dirItem;
        dirItem["name"] = entryName;
        dirItem["path"] = entryPath;
        dirsArr.append(dirItem);

      } else if (entry.is_regular_file()) {
        // Check file extension
        std::string ext = entry.path().extension().string();
        bool extMatch = false;
        for (const auto &allowedExt : config.scanner.extensions) {
          if (ext == allowedExt) {
            extMatch = true;
            break;
          }
        }
        if (!extMatch)
          continue;

        Json::Value fileItem;
        fileItem["filepath"] = entryPath;
        fileItem["filename"] = entryName;

        // Get file size
        auto fileSize = fs::file_size(entry.path());
        fileItem["size"] = static_cast<Json::Int64>(fileSize);

        // Get modification time
        auto ftime = fs::last_write_time(entry.path());
        auto sctp =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() +
                std::chrono::system_clock::now());
        auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
        char timebuf[64];
        std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&time_t_val));
        fileItem["modified_at"] = std::string(timebuf);

        std::string md5 = live2mp3::utils::calculateMD5(entryPath);
        fileItem["md5"] = md5;
        fileItem["processed"] = lpPendingFileService_->isProcessed(md5);

        filesArr.append(fileItem);
      }
    }
  } catch (const std::exception &e) {
    LOG_ERROR << "Error browsing directory " << pathParam << ": " << e.what();
    ret["error"] = std::string("Error browsing directory: ") + e.what();
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
    return;
  }

  ret["current_path"] = pathParam;
  ret["parent_path"] = parentPath;
  ret["root_path"] = matchedRoot->path;
  ret["directories"] = dirsArr;
  ret["files"] = filesArr;

  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}

void FileBrowserController::processDirectory(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto jsonPtr = req->getJsonObject();
  if (!jsonPtr || !jsonPtr->isMember("path")) {
    Json::Value ret;
    ret["error"] = "Missing 'path' parameter";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  std::string pathParam = (*jsonPtr)["path"].asString();
  fs::path browsePath(pathParam);

  if (!fs::exists(browsePath) || !fs::is_directory(browsePath)) {
    Json::Value ret;
    ret["error"] = "Path does not exist or is not a directory";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  // Validate path is under a configured root
  auto config = lpConfigService_->getConfig();
  bool isValidRoot = false;
  for (const auto &root : config.scanner.video_roots) {
    if (pathParam.find(root.path) == 0) {
      isValidRoot = true;
      break;
    }
  }

  if (!isValidRoot) {
    Json::Value ret;
    ret["error"] = "Path is not under any configured root";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  // Trigger processing
  lpSchedulerService_->triggerNow();

  Json::Value ret;
  ret["status"] = "processing";
  ret["message"] = "Processing triggered for: " + pathParam;
  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}
