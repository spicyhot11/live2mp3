#include "SchedulerService.h"
#include "../utils/FileUtils.h"
#include "ConfigService.h"
#include "ConverterService.h"
#include "MergerService.h"
#include "PendingFileService.h"
#include "ScannerService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

void SchedulerService::initAndStart(const Json::Value &config) {
  configServicePtr = drogon::app().getPlugin<ConfigService>();
  if (!configServicePtr) {
    LOG_FATAL << "Failed to get ConfigService plugin";
    return;
  }

  mergerServicePtr = drogon::app().getPlugin<MergerService>();
  if (!mergerServicePtr) {
    LOG_FATAL << "Failed to get MergerService plugin";
    return;
  }

  scannerServicePtr = drogon::app().getPlugin<ScannerService>();
  if (!scannerServicePtr) {
    LOG_FATAL << "Failed to get ScannerService plugin";
    return;
  }

  converterServicePtr = drogon::app().getPlugin<ConverterService>();
  if (!converterServicePtr) {
    LOG_FATAL << "Failed to get ConverterService plugin";
    return;
  }

  pendingFileServicePtr = drogon::app().getPlugin<PendingFileService>();
  if (!pendingFileServicePtr) {
    LOG_FATAL << "Failed to get PendingFileService plugin";
    return;
  }
}

void SchedulerService::shutdown() {
  configServicePtr = nullptr;
  mergerServicePtr = nullptr;
  scannerServicePtr = nullptr;
  converterServicePtr = nullptr;
  pendingFileServicePtr = nullptr;
}

std::string SchedulerService::getCurrentFile() {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentFile_;
}

std::string SchedulerService::getCurrentPhase() {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentPhase_;
}

void SchedulerService::setPhase(const std::string &phase) {
  std::lock_guard<std::mutex> lock(mutex_);
  currentPhase_ = phase;
}

nlohmann::json SchedulerService::getDetailedStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json status;
  status["running"] = running_.load();
  status["current_file"] = currentFile_;
  status["current_phase"] = currentPhase_;
  return status;
}

void SchedulerService::start() {
  drogon::app().registerBeginningAdvice([this]() {
    auto config = configServicePtr->getConfig();
    int interval = config.scheduler.scan_interval_seconds;
    if (interval <= 0)
      interval = 60;

    drogon::app().getLoop()->runEvery(interval * 1.0,
                                      [this]() { this->runTask(); });

    LOG_INFO << "Scheduler started with interval " << interval << "s";
  });
}

void SchedulerService::triggerNow() {
  // Immediate mode: process all staged files without waiting for time window
  drogon::app().getLoop()->queueInLoop([this]() { this->runTask(true); });
}

void SchedulerService::runTask(bool immediate) {
  if (running_) {
    LOG_INFO << "Task already running, skipping";
    return;
  }
  running_ = true;

  std::thread([this, immediate]() {
    LOG_INFO << "Starting scheduled task..."
             << (immediate ? " (immediate mode)" : "");

    // Phase 1: Stability scan - record MD5 hashes
    setPhase("stability_scan");
    runStabilityScan();

    // Phase 2: Convert stable files to AV1 MP4
    setPhase("conversion");
    runConversion();

    // Phase 3: Merge staged files and move to output
    setPhase("merge_output");
    runMergeAndOutput(immediate);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      currentFile_ = "";
      currentPhase_ = "";
    }

    LOG_INFO << "Task finished.";
    running_ = false;
  }).detach();
}

void SchedulerService::runStabilityScan() {
  LOG_INFO << "Phase 1: Running stability scan...";

  auto scanResult = scannerServicePtr->scan();
  LOG_INFO << "Found " << scanResult.files.size() << " files to check";

  auto config = configServicePtr->getConfig();
  int requiredStableCount = config.scheduler.stability_checks;

  for (const auto &file : scanResult.files) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      currentFile_ = file;
    }

    // Calculate MD5 of the file
    std::string md5 = live2mp3::utils::calculateMD5(file);
    if (md5.empty()) {
      LOG_WARN << "Failed to calculate MD5 for: " << file;
      continue;
    }

    // Update or add to pending files
    int stableCount = pendingFileServicePtr->addOrUpdateFile(file, md5);

    if (stableCount >= requiredStableCount) {
      LOG_INFO << "File is stable (count=" << stableCount << "): " << file;
      // Mark file as stable so it's ready for conversion
      pendingFileServicePtr->markAsStable(file);
    } else {
      LOG_DEBUG << "File stability count: " << stableCount << " for: " << file;
    }
  }
}

void SchedulerService::runConversion() {
  LOG_INFO << "Phase 2: Converting files with 'stable' status...";

  auto config = configServicePtr->getConfig();

  // Get all files marked as stable (passed stability check)
  auto stableFiles = pendingFileServicePtr->getAllStableFiles();
  LOG_INFO << "Found " << stableFiles.size() << " stable files to convert";

  for (const auto &pf : stableFiles) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      currentFile_ = pf.filepath;
    }

    // Check if source file still exists
    if (!fs::exists(pf.filepath)) {
      LOG_WARN << "Source file no longer exists: " << pf.filepath;
      pendingFileServicePtr->removeFile(pf.filepath);
      continue;
    }

    // Mark as converting
    pendingFileServicePtr->markAsConverting(pf.filepath);

    // Check temp space
    uint64_t fileSize = 0;
    try {
      fileSize = fs::file_size(pf.filepath);
    } catch (...) {
      fileSize = 100 * 1024 * 1024; // Assume 100MB if can't get size
    }

    std::string outputDir;
    bool useTempDir = false;

    if (!config.temp.temp_dir.empty() &&
        converterServicePtr->hasTempSpace(fileSize)) {
      outputDir = config.temp.temp_dir;
      useTempDir = true;
    } else {
      outputDir = config.output.output_root;
      LOG_INFO << "Temp space insufficient or not configured, using output dir "
                  "directly";
    }

    // Convert to AV1 MP4
    auto mp4Result =
        converterServicePtr->convertToAv1Mp4(pf.filepath, outputDir);

    if (mp4Result.has_value()) {
      if (useTempDir) {
        // Mark as staged with temp path
        pendingFileServicePtr->markAsStaged(pf.filepath, *mp4Result);
        LOG_INFO << "File staged in temp: " << *mp4Result;
      } else {
        // Directly output - also extract MP3
        auto mp3Result = converterServicePtr->extractMp3FromVideo(
            *mp4Result, config.output.output_root);
        if (mp3Result.has_value()) {
          LOG_INFO << "MP3 extracted: " << *mp3Result;
        }
        pendingFileServicePtr->markAsCompleted(pf.filepath);
      }
    } else {
      LOG_ERROR << "Failed to convert: " << pf.filepath;
      // Reset to pending status so it can be retried
      pendingFileServicePtr->addOrUpdateFile(pf.filepath, pf.current_md5);
    }
  }
}

void SchedulerService::runMergeAndOutput(bool immediate) {
  LOG_INFO << "Phase 3: Processing staged files..."
           << (immediate ? " (immediate mode)" : "");

  auto config = configServicePtr->getConfig();
  int mergeWindowSeconds = config.scheduler.merge_window_seconds;

  // Get staged files
  std::vector<PendingFile> filesToProcess;
  if (immediate) {
    filesToProcess = pendingFileServicePtr->getAllStagedFiles();
    LOG_INFO << "Immediate mode: processing all " << filesToProcess.size()
             << " staged files";
  } else {
    // In normal mode, we still need to check "break points" or timeouts.
    // For simplicity in this new logic, we fetch ALL staged files and decide
    // grouping based on time. We only process a group if the GAP after it is
    // large enough OR if the last file is old enough.
    // Actually, getting all staged files is safer to build complete groups.
    filesToProcess = pendingFileServicePtr->getAllStagedFiles();
  }

  if (filesToProcess.empty()) {
    return;
  }

  // 1. Organize by parent directory (Streamer/Category)
  std::map<std::string, std::vector<StagedFile>> groupedFiles;

  for (const auto &pf : filesToProcess) {
    if (pf.temp_mp4_path.empty() || !fs::exists(pf.temp_mp4_path)) {
      LOG_WARN << "Staged file missing: " << pf.temp_mp4_path;
      pendingFileServicePtr->removeFile(pf.filepath);
      continue;
    }

    // Parse time from original filename
    fs::path originalPath(pf.filepath);
    auto t = MergerService::parseTime(originalPath.filename().string());

    if (!t) {
      LOG_WARN << "Could not parse time for file, treating as standalone: "
               << pf.filepath;
      // If no time, use current time? No, just process standalone.
      // Or maybe put in a "unknown_time" group?
      // For now, let's just make a dummy time point or handle separately.
      // We'll skip complex grouping for these and process immediately.
      // TODO: Handle no-timestamp files better.
      continue;
    }

    std::string parentDir = originalPath.parent_path().string();
    groupedFiles[parentDir].push_back({pf, *t});
  }

  // 2. Process each directory group
  for (auto &[parentDir, files] : groupedFiles) {
    // Sort by time
    std::sort(files.begin(), files.end(),
              [](const StagedFile &a, const StagedFile &b) {
                return a.time < b.time;
              });

    std::vector<StagedFile> currentBatch;

    for (size_t i = 0; i < files.size(); ++i) {
      const auto &file = files[i];
      bool isLast = (i == files.size() - 1);

      if (currentBatch.empty()) {
        currentBatch.push_back(file);
      } else {
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(
                        file.time - currentBatch.back().time)
                        .count();

        if (diff <= mergeWindowSeconds) {
          currentBatch.push_back(file);
        } else {
          // Gap detected, process previous batch
          // But wait, if we are NOT in immediate mode, we need to be sure the
          // batch is "done". If we see a gap (new file started long after), the
          // previous batch is definitely done.
          processBatch(currentBatch, config);
          currentBatch.clear();
          currentBatch.push_back(file);
        }
      }

      // Check if we should process the current/last batch
      if (isLast) {
        // It's the end of what we have.
        // If immediate mode, process everything.
        if (immediate) {
          processBatch(currentBatch, config);
        } else {
          // Check if the last file is old enough to consider the stream "ended"
          // or "broken" If the last file started > mergeWindowSeconds ago +
          // some margin? Or better: check if file modification time (stopped
          // writing) was long ago. Since these are already converted MP4s, they
          // are static. We need to check if enough time passed since the
          // *start* of the last file? Let's use system clock.
          auto now = std::chrono::system_clock::now();
          auto age = std::chrono::duration_cast<std::chrono::seconds>(
                         now - currentBatch.back().time)
                         .count();
          // We add a safety margin (e.g. 2 * window) to secure we don't merge
          // too early if streamer reconnects? Or just modify logic: "OlderThan"
          // applies to the GROUP.
          if (age > mergeWindowSeconds) {
            processBatch(currentBatch, config);
          }
          // Else: keep waiting for potential future fragments
        }
      }
    }
  }
}

// Helper to process a confirmed batch of files
void SchedulerService::processBatch(const std::vector<StagedFile> &batch,
                                    const AppConfig &config) {
  if (batch.empty())
    return;

  LOG_INFO << "Processing batch of " << batch.size() << " files";

  // Prepare paths
  std::vector<std::string> videoPaths;
  for (const auto &f : batch) {
    videoPaths.push_back(f.pf.temp_mp4_path);
  }

  // Determine output directory based on the first file's parent folder name
  // e.g. /data/rec/StreamerA -> /data/output/StreamerA
  fs::path firstOrigPath(batch[0].pf.filepath);
  std::string streamerName = firstOrigPath.parent_path().filename().string();
  fs::path outputDir = fs::path(config.output.output_root) / streamerName;

  // Ensure output dir
  try {
    fs::create_directories(outputDir);
  } catch (...) {
  }

  // 1. Merge MP4s in Temp (or just use the single one)
  // We use the temp dir of the first file for intermediate operations if
  // needed? Actually MergerService::mergeVideoFiles takes outputDir. We should
  // merge DIRECTLY to a temp file, then move? Let's assume we merge into the
  // SAME directory where the MP4s are (temp), then move to final.
  fs::path tempDir(fs::path(batch[0].pf.temp_mp4_path).parent_path());

  auto mergedPathOpt =
      mergerServicePtr->mergeVideoFiles(videoPaths, tempDir.string());

  if (!mergedPathOpt) {
    LOG_ERROR << "Failed to merge batch";
    return;
  }

  std::string mergedMp4Path = *mergedPathOpt;
  fs::path finalMp4Path = outputDir / fs::path(mergedMp4Path).filename();

  // 2. Move Merged MP4 to Output
  try {
    if (fs::exists(finalMp4Path))
      fs::remove(finalMp4Path);
    fs::rename(mergedMp4Path, finalMp4Path);
    LOG_INFO << "Moved final MP4 to: " << finalMp4Path;
  } catch (const std::exception &e) {
    LOG_ERROR << "Failed to move output MP4: " << e.what();
    return;
  }

  // 3. Extract MP3 from Final MP4
  auto mp3Result = converterServicePtr->extractMp3FromVideo(
      finalMp4Path.string(), config.output.output_root);

  if (mp3Result) {
    LOG_INFO << "MP3 Extracted: " << *mp3Result;
  }

  // 4. Mark all original FLVs as completed
  for (const auto &f : batch) {
    pendingFileServicePtr->markAsCompleted(f.pf.filepath);
  }

  // 5. Cleanup Component MP4s in Temp
  for (const auto &f : batch) {
    // Don't delete if it was the same file as merged (single file case)
    // mergeVideoFiles returns the original path if count==1
    if (f.pf.temp_mp4_path != mergedMp4Path) {
      try {
        fs::remove(f.pf.temp_mp4_path);
      } catch (...) {
      }
    }
  }
}
