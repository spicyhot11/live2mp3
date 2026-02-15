#include "FileController.h"
#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

void FileController::listDirectories(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  Json::Value ret;
  std::string path;

  auto jsonPtr = req->getJsonObject();
  if (jsonPtr && jsonPtr->isMember("path")) {
    path = (*jsonPtr)["path"].asString();
  }

  // Default to root if not provided or empty
  if (path.empty()) {
    path = "/";
  }

  try {
    if (!fs::exists(path)) {
      ret["error"] = "Path does not exist";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      return;
    }

    if (!fs::is_directory(path)) {
      ret["error"] = "Path is not a directory";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      return;
    }

    Json::Value dirs(Json::arrayValue);
    std::vector<std::string> dirList;

    // Handle permission errors gracefully
    try {
      for (const auto &entry : fs::directory_iterator(
               path, fs::directory_options::skip_permission_denied)) {
        if (entry.is_directory()) {
          dirList.push_back(entry.path().filename().string());
        }
      }
    } catch (const fs::filesystem_error &ex) {
      ret["error"] = std::string("Filesystem error: ") + ex.what();
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      return;
    }

    std::sort(dirList.begin(), dirList.end());

    for (const auto &d : dirList) {
      dirs.append(d);
    }

    // Return canonical path to resolve ".." or "."
    // Use error_code to avoid throwing exception on canonical fail
    std::error_code ec;
    auto canonicalPath = fs::canonical(path, ec);
    if (ec) {
      ret["current_path"] = path;
    } else {
      ret["current_path"] = canonicalPath.string();
    }

    ret["directories"] = dirs;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);

  } catch (const std::exception &e) {
    ret["error"] = std::string("Internal error: ") + e.what();
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }
}
