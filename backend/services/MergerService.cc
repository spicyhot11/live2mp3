#include "MergerService.h"
#include "ConfigService.h"
#include <array>
#include <cstdio>
#include <drogon/drogon.h>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

void MergerService::initAndStart(const Json::Value &config) {
  configServicePtr = drogon::app().getPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "Failed to get ConfigService plugin";
    return;
  }
}

void MergerService::shutdown() { configServicePtr = nullptr; }

// Removed legacy mergeAll
// New logic uses SchedulerService to call mergeVideoFiles directly

std::optional<std::chrono::system_clock::time_point>
MergerService::parseTime(const std::string &filename) {
  std::tm tm = {};

  // Format 1: [2026-01-06 09-47-38]
  // Regex: ^\[(\d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2})\]
  static std::regex re1(R"(^\[(\d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2})\])");
  std::smatch match;

  if (std::regex_search(filename, match, re1)) {
    std::istringstream ss(match[1].str());
    ss >> std::get_time(&tm, "%Y-%m-%d %H-%M-%S");
    if (!ss.fail()) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
  }

  // Format 2: ...-20240801-151938-...
  // Regex: .*(\d{8})-(\d{6}).*
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

std::optional<std::string>
MergerService::mergeVideoFiles(const std::vector<std::string> &files,
                               const std::string &outputDir) {
  if (files.empty())
    return std::nullopt;

  if (files.size() == 1) {
    LOG_INFO << "Only one file, skipping merge: " << files[0];
    return files[0];
  }

  // Use the first file's name as base, but prefix with "merged_" temporarily?
  // Or just generate a new name based on time?
  // Let's use the first file's original filename.
  fs::path firstPath(files[0]);
  std::string filename = firstPath.filename().string();
  std::string outputName = "merged_" + filename;
  std::string outputTemp = (fs::path(outputDir) / outputName).string();

  // Create list file
  std::string listPath = (fs::path(outputDir) / "concat_list.txt").string();
  std::ofstream listFile(listPath);
  for (const auto &f : files) {
    listFile << "file '" << f << "'\n";
  }
  listFile.close();

  LOG_INFO << "Merging " << files.size() << " files into " << outputTemp;

  if (runFfmpegConcat(listPath, outputTemp)) {
    fs::remove(listPath);
    return outputTemp;
  } else {
    LOG_ERROR << "Merge failed";
    fs::remove(listPath);
    if (fs::exists(outputTemp))
      fs::remove(outputTemp);
    return std::nullopt;
  }
}

bool MergerService::runFfmpegConcat(const std::string &listPath,
                                    const std::string &outputPath) {
  // ffmpeg -f concat -safe 0 -i list.txt -c copy output.mp4
  // We use -c copy for fast merging of same-codec (AV1) files
  std::string cmd =
      fmt::format("ffmpeg -f concat -safe 0 -i \"{}\" -c copy -y \"{}\" 2>&1",
                  listPath, outputPath);

  std::array<char, 128> buffer;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return false;

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
  }

  return pclose(pipe) == 0;
}
