#include "MergerService.h"
#include "ConfigService.h"
#include <algorithm>
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

void MergerService::shutdown() {
  configServicePtr = nullptr;
}

void MergerService::mergeAll() {
  auto config = configServicePtr->getConfig();
  std::string outputRoot = config.output.output_root;

  if (!fs::exists(outputRoot))
    return;

  // Process outputRoot recursivley or just subdirectories?
  // "Default is the folder name properly".
  // Iterate subdirectories in outputRoot
  try {
    for (const auto &entry : fs::directory_iterator(outputRoot)) {
      if (entry.is_directory()) {
        processDirectory(entry.path().string());
      }
    }
  } catch (...) {
    LOG_ERROR << "Error iterating output root";
  }
}

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

void MergerService::processDirectory(const std::string &dirPath) {
  std::vector<Mp3File> files;

  for (const auto &entry : fs::directory_iterator(dirPath)) {
    if (entry.is_regular_file() && entry.path().extension() == ".mp3") {
      auto t = parseTime(entry.path().filename().string());
      if (t) {
        files.push_back(
            {entry.path().string(), *t, entry.path().filename().string()});
      }
    }
  }

  if (files.empty())
    return;

  // Sort by time
  std::sort(files.begin(), files.end(),
            [](const Mp3File &a, const Mp3File &b) { return a.time < b.time; });

  // Group files
  auto config = configServicePtr->getConfig();
  int windowHours = config.scheduler.merge_window_hours; // e.g. 2 hours
  auto windowDuration = std::chrono::hours(windowHours);

  std::vector<Mp3File> currentGroup;

  for (const auto &file : files) {
    if (currentGroup.empty()) {
      currentGroup.push_back(file);
    } else {
      auto lastTime = currentGroup.back().time;
      auto diff = file.time - lastTime;

      // Check if adjacent are merged already? No, scanning raw files.
      // Assumption: Merged files might be re-scanned?
      // Need a way to distinguish raw vs merged?
      // Merged file takes "earliest time".
      // If we re-run, we might merge the merged file with others?
      // "Adjacent files within N hours".
      // Simple logic: if file.time - lastTime <= window, group it.

      if (diff <= windowDuration) {
        currentGroup.push_back(file);
      } else {
        if (currentGroup.size() > 1) {
          mergeFiles(currentGroup, dirPath);
        }
        currentGroup.clear();
        currentGroup.push_back(file);
      }
    }
  }

  if (currentGroup.size() > 1) {
    mergeFiles(currentGroup, dirPath);
  }
}

bool MergerService::mergeFiles(const std::vector<Mp3File> &files,
                               const std::string &outputDir) {
  if (files.empty())
    return false;

  // Output filename: Earliest time + parsed parts from filename
  // To preserve the complex filename structure, let's just use the first file's
  // name But we might overwrite it if we are not careful? Actually, ffmpeg
  // concat to a temporary file, then rename/replace? Or name it differently?
  // "FileName format... implied time... Filename format is..."
  // Let's use the first filename as the target, but we must delete others.

  std::string targetName = files[0].originalFilename;
  std::string outputTemp =
      (fs::path(outputDir) / ("merged_" + targetName)).string();
  std::string finalOutput = (fs::path(outputDir) / targetName).string();

  // Create list file
  std::string listPath = (fs::path(outputDir) / "concat_list.txt").string();
  std::ofstream listFile(listPath);
  for (const auto &f : files) {
    // Escape path? ffmpeg list syntax: file '/path/to/file'
    listFile << "file '" << f.path << "'\n";
  }
  listFile.close();

  LOG_INFO << "Merging " << files.size() << " files into " << finalOutput;

  if (runFfmpegConcat(listPath, outputTemp)) {
    // Success
    fs::remove(listPath); // Remove list

    // Remove input files EXCEPT if one of them is the target name (which we
    // overwrite later) Wait, if we overwrite, we lose data? We wrote to
    // outputTemp. So safe to delete inputs now.
    for (const auto &f : files) {
      try {
        fs::remove(f.path);
      } catch (...) {
      }
    }

    // Rename temp to final
    fs::rename(outputTemp, finalOutput);
    LOG_INFO << "Merge done: " << finalOutput;
    return true;
  } else {
    LOG_ERROR << "Merge failed";
    fs::remove(listPath);
    if (fs::exists(outputTemp))
      fs::remove(outputTemp);
    return false;
  }
}

bool MergerService::runFfmpegConcat(const std::string &listPath,
                                    const std::string &outputPath) {
  // ffmpeg -f concat -safe 0 -i list.txt -c copy output.mp3
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
