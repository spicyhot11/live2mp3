#include "SchedulerService.h"
#include "../utils/CoroUtils.hpp"
#include "../utils/FileUtils.h"
#include <algorithm>
#include <drogon/drogon.h>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

void SchedulerService::initAndStart(const Json::Value &config) {
  configServicePtr_ = drogon::app().getSharedPlugin<ConfigService>();
  if (!configServicePtr_) {
    LOG_FATAL << "Failed to get ConfigService plugin";
    return;
  }

  mergerServicePtr_ = drogon::app().getSharedPlugin<MergerService>();
  if (!mergerServicePtr_) {
    LOG_FATAL << "Failed to get MergerService plugin";
    return;
  }

  scannerServicePtr_ = drogon::app().getSharedPlugin<ScannerService>();
  if (!scannerServicePtr_) {
    LOG_FATAL << "Failed to get ScannerService plugin";
    return;
  }

  converterServicePtr_ = drogon::app().getSharedPlugin<ConverterService>();
  if (!converterServicePtr_) {
    LOG_FATAL << "Failed to get ConverterService plugin";
    return;
  }

  pendingFileServicePtr_ = drogon::app().getSharedPlugin<PendingFileService>();
  if (!pendingFileServicePtr_) {
    LOG_FATAL << "Failed to get PendingFileService plugin";
    return;
  }

  ffmpegTaskServicePtr_ = drogon::app().getSharedPlugin<FfmpegTaskService>();
  if (!ffmpegTaskServicePtr_) {
    LOG_FATAL << "Failed to get FfmpegTaskService plugin";
    return;
  }

  commonThreadServicePtr_ =
      drogon::app().getSharedPlugin<CommonThreadService>();
  if (!commonThreadServicePtr_) {
    LOG_FATAL << "Failed to get CommonThreadService plugin";
    return;
  }

  batchTaskServicePtr_ = drogon::app().getSharedPlugin<BatchTaskService>();
  if (!batchTaskServicePtr_) {
    LOG_FATAL << "Failed to get BatchTaskService plugin";
    return;
  }

  start();
  LOG_INFO << "Scheduler init and start";
}

void SchedulerService::shutdown() {
  configServicePtr_.reset();
  mergerServicePtr_.reset();
  scannerServicePtr_.reset();
  converterServicePtr_.reset();
  pendingFileServicePtr_.reset();
  ffmpegTaskServicePtr_.reset();
  commonThreadServicePtr_.reset();
  batchTaskServicePtr_.reset();
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
  status["scan_running"] = scanRunning_.load();
  status["current_file"] = currentFile_;
  status["current_phase"] = currentPhase_;
  return status;
}

void SchedulerService::start() {
  drogon::app().registerBeginningAdvice([this]() {
    auto config = configServicePtr_->getConfig();
    int interval = config.scheduler.scan_interval_seconds;
    if (interval <= 0)
      interval = 60;

    drogon::app().getLoop()->runEvery(interval * 1.0, [this]() {
      drogon::async_run(
          [this]() -> drogon::Task<> { co_await this->runTaskAsync(false); });
    });

    LOG_INFO << "Scheduler started with interval " << interval << "s";
  });
}

void SchedulerService::triggerNow() {
  drogon::async_run(
      [this]() -> drogon::Task<> { co_await this->runTaskAsync(true); });
}

drogon::Task<void> SchedulerService::runTaskAsync(bool immediate) {
  LOG_INFO << "Starting scheduled task..."
           << (immediate ? " (immediate mode)" : "");

  // 阶段 1: 扫描
  bool scanSkipped = scanRunning_.exchange(true);
  if (!scanSkipped) {
    setPhase("stability_scan");
    co_await live2mp3::utils::awaitFuture(commonThreadServicePtr_->runTaskAsync(
        [this]() { runStabilityScan(); }));
    scanRunning_ = false;

    // DEBUG: 打印正在执行的任务
    auto runningTasks = ffmpegTaskServicePtr_->getRunningTasks();
    if (!runningTasks.empty()) {
      LOG_DEBUG << "当前正在执行的任务数: " << runningTasks.size();
      for (const auto &task : runningTasks) {
        std::string taskTypeStr;
        switch (task.type) {
        case FfmpegTaskType::CONVERT_MP4:
          taskTypeStr = "CONVERT_MP4";
          break;
        case FfmpegTaskType::CONVERT_MP3:
          taskTypeStr = "CONVERT_MP3";
          break;
        case FfmpegTaskType::MERGE:
          taskTypeStr = "MERGE";
          break;
        default:
          taskTypeStr = "OTHER";
          break;
        }
        std::string filesStr;
        for (const auto &f : task.files) {
          if (!filesStr.empty())
            filesStr += ", ";
          filesStr += fs::path(f).filename().string();
        }
        int progressSec = task.progressTime / 1000;
        int progressMin = progressSec / 60;
        progressSec = progressSec % 60;
        char progressTimeStr[16];
        snprintf(progressTimeStr, sizeof(progressTimeStr), "%02d:%02d",
                 progressMin, progressSec);

        int totalSec = task.totalDuration / 1000;
        int totalMin = totalSec / 60;
        totalSec = totalSec % 60;
        char totalTimeStr[16];
        snprintf(totalTimeStr, sizeof(totalTimeStr), "%02d:%02d", totalMin,
                 totalSec);

        char progressPercentStr[16];
        if (task.progress >= 0) {
          snprintf(progressPercentStr, sizeof(progressPercentStr), "%.1f%%",
                   task.progress);
        } else {
          snprintf(progressPercentStr, sizeof(progressPercentStr), "N/A");
        }

        char speedStr[16];
        snprintf(speedStr, sizeof(speedStr), "%.2fx", task.speed);

        LOG_DEBUG << "  - [" << taskTypeStr << "] " << filesStr
                  << " | 进度: " << progressPercentStr << " ("
                  << progressTimeStr << "/" << totalTimeStr << ")"
                  << " | fps: " << task.progressFps << " | 速度: " << speedStr;
      }
    }
  } else {
    LOG_DEBUG << "Scan already running, skipping scan phase";
  }

  // 阶段 2: 分批处理（非阻塞）
  setPhase("merge_encode_output");
  runMergeEncodeOutput(immediate);

  // 阶段 3: 检查是否有编码完成的批次可以开始合并
  setPhase("check_encoded_batches");
  checkEncodedBatches();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    currentFile_ = "";
    currentPhase_ = "";
  }

  LOG_INFO << "Task scheduling finished (processing continues in background).";
}

void SchedulerService::runStabilityScan() {
  LOG_INFO << "Phase 1: Running stability scan...";

  auto scanResult = scannerServicePtr_->scan();
  LOG_INFO << "Found " << scanResult.files.size() << " files to check";

  auto config = configServicePtr_->getConfig();
  int requiredStableCount = config.scheduler.stability_checks;

  for (const auto &file : scanResult.files) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      currentFile_ = file;
    }

    std::string fingerprint = live2mp3::utils::calculateFileFingerprint(file);
    if (fingerprint.empty()) {
      LOG_WARN << "无法计算文件指纹: " << file;
      continue;
    }

    int stableCount =
        pendingFileServicePtr_->addOrUpdateFile(file, fingerprint);

    if (stableCount >= requiredStableCount) {
      LOG_INFO << "File is stable (count=" << stableCount << "): " << file;
      pendingFileServicePtr_->markAsStable(file);
    } else {
      LOG_DEBUG << "File stability count: " << stableCount << " for: " << file;
    }
  }
}

void SchedulerService::runMergeEncodeOutput(bool immediate) {
  LOG_INFO << "Phase 2: Processing stable files for merge + encode..."
           << (immediate ? " (immediate mode)" : "");

  auto config = configServicePtr_->getConfig();
  int mergeWindowSeconds = config.scheduler.merge_window_seconds;
  int stopWaitingSeconds = config.scheduler.stop_waiting_seconds;

  // 1. 原子性地获取并标记所有稳定的原始文件为 processing
  auto stableFiles = pendingFileServicePtr_->getAndClaimStableFiles();
  if (stableFiles.empty()) {
    LOG_DEBUG << "No stable files to process";
    return;
  }
  LOG_INFO << "Claimed " << stableFiles.size()
           << " stable files for processing";

  // 2. 按主播名分组
  std::map<std::string, std::vector<StableFile>> groupedByStreamer;
  for (const auto &pf : stableFiles) {
    std::string filepath = pf.getFilepath();
    if (!fs::exists(filepath)) {
      LOG_WARN << "Source file no longer exists: " << filepath;
      pendingFileServicePtr_->removeFile(filepath);
      continue;
    }

    fs::path filePath(filepath);
    std::string filename = filePath.filename().string();
    std::string streamer = MergerService::parseTitle(filename);
    auto time = MergerService::parseTime(filename);

    if (streamer.empty() || !time) {
      LOG_WARN << "Could not parse streamer/time for file: " << filename;
      continue;
    }

    groupedByStreamer[streamer].push_back({pf, *time});
  }

  // 3. 处理每个主播分组
  for (auto &[streamer, files] : groupedByStreamer) {
    // 按时间降序排列
    std::sort(files.begin(), files.end(),
              [](const StableFile &a, const StableFile &b) {
                return a.time > b.time;
              });

    // 分配批次
    std::set<size_t> assigned;
    std::vector<std::vector<StableFile>> batches;

    for (size_t i = 0; i < files.size(); ++i) {
      if (assigned.count(i))
        continue;

      std::vector<StableFile> batch;
      batch.push_back(files[i]);
      assigned.insert(i);

      for (size_t j = i + 1; j < files.size(); ++j) {
        if (assigned.count(j))
          continue;
        auto gap = std::chrono::duration_cast<std::chrono::seconds>(
                       batch.back().time - files[j].time)
                       .count();
        if (gap <= mergeWindowSeconds) {
          batch.push_back(files[j]);
          assigned.insert(j);
        } else {
          break;
        }
      }

      batches.push_back(std::move(batch));
    }

    // 4. 判断并处理每个批次
    auto now = std::chrono::system_clock::now();
    for (auto &batch : batches) {
      // 使用批次中最新录播的结束时间判断
      auto age =
          std::chrono::duration_cast<std::chrono::seconds>(now - batch[0].time)
              .count();

      if (!immediate && age <= stopWaitingSeconds) {
        LOG_DEBUG << "Batch for streamer '" << streamer
                  << "' not ready yet (age=" << age
                  << "s, threshold=" << stopWaitingSeconds << "s)";
        continue;
      }

      // 按时间升序排列（合并时需要按时间顺序）
      std::reverse(batch.begin(), batch.end());

      // 非阻塞处理批次
      processBatch(batch, config);
    }
  }
}

void SchedulerService::processBatch(const std::vector<StableFile> &batch,
                                    const AppConfig &config) {
  if (batch.empty())
    return;

  LOG_INFO << "Processing batch of " << batch.size() << " files";

  // 使用批次中最新的文件名作为输出文件名基础
  std::string latestFilepath = batch.back().pf.getFilepath();
  fs::path latestFilePath(latestFilepath);
  std::string streamer =
      MergerService::parseTitle(latestFilePath.filename().string());
  fs::path outputDir = fs::path(config.output.output_root) / streamer;
  fs::path tmpDir = fs::path(config.output.output_root) / "tmp";

  try {
    fs::create_directories(outputDir);
    fs::create_directories(tmpDir);
  } catch (...) {
  }

  // 构建 BatchInputFile 列表
  std::vector<BatchInputFile> batchInputFiles;
  for (const auto &f : batch) {
    BatchInputFile bif;
    bif.filepath = f.pf.getFilepath();
    bif.fingerprint = f.pf.fingerprint;
    bif.pending_file_id = f.pf.id;
    batchInputFiles.push_back(bif);
  }

  // 创建数据库批次记录
  int batchId = batchTaskServicePtr_->createBatch(
      streamer, outputDir.string(), tmpDir.string(), batchInputFiles);

  if (batchId < 0) {
    LOG_ERROR << "创建批次记录失败，回滚文件状态";
    std::vector<std::string> flvPaths;
    for (const auto &f : batch) {
      flvPaths.push_back(f.pf.getFilepath());
    }
    pendingFileServicePtr_->rollbackToStable(flvPaths);
    return;
  }

  LOG_INFO << "Created batch id=" << batchId << " streamer=" << streamer
           << " files=" << batch.size();

  // 为每个文件提交转码任务（非阻塞）
  for (const auto &f : batch) {
    std::string filepath = f.pf.getFilepath();

    batchTaskServicePtr_->markFileEncoding(batchId, filepath);

    // 使用 onComplete 回调 - 转码完成时自动触发下一阶段
    ffmpegTaskServicePtr_->submitTask(
        FfmpegTaskType::CONVERT_MP4, {filepath}, {tmpDir.string()},
        [this, batchId, filepath](FfmpegTaskResult result) {
          onFileEncoded(batchId, filepath, result);
        });
  }
}

void SchedulerService::onFileEncoded(int batchId, const std::string &filepath,
                                     const FfmpegTaskResult &result) {
  if (result.status == FfmpegTaskStatus::COMPLETED &&
      !result.outputFiles.empty()) {
    std::string encodedPath = result.outputFiles[0];
    std::string fingerprint =
        live2mp3::utils::calculateFileFingerprint(encodedPath);
    batchTaskServicePtr_->markFileEncoded(batchId, filepath, encodedPath,
                                          fingerprint);
    LOG_INFO << "Batch " << batchId << ": file encoded " << filepath << " -> "
             << encodedPath;
  } else {
    batchTaskServicePtr_->markFileFailed(batchId, filepath);
    LOG_ERROR << "Batch " << batchId << ": file encoding failed " << filepath;
  }
}

void SchedulerService::checkEncodedBatches() {
  auto config = configServicePtr_->getConfig();
  int stopWaitingSeconds = config.scheduler.stop_waiting_seconds;

  auto batchIds =
      batchTaskServicePtr_->getEncodingCompleteBatchIds(stopWaitingSeconds);
  if (batchIds.empty()) {
    LOG_DEBUG << "No encoding-complete batches ready to merge";
    return;
  }

  LOG_INFO << "Found " << batchIds.size()
           << " encoding-complete batches, starting merge phase";
  for (int batchId : batchIds) {
    onBatchEncodingComplete(batchId);
  }
}

void SchedulerService::onBatchEncodingComplete(int batchId) {
  LOG_INFO << "Batch " << batchId << ": all files encoded, starting merge...";

  auto batchOpt = batchTaskServicePtr_->getBatch(batchId);
  if (!batchOpt) {
    LOG_ERROR << "Batch " << batchId << ": batch not found";
    return;
  }
  auto &batch = *batchOpt;

  auto encodedPaths = batchTaskServicePtr_->getEncodedPaths(batchId);
  if (encodedPaths.empty()) {
    LOG_ERROR << "Batch " << batchId
              << ": no successfully encoded files, marking failed";
    batchTaskServicePtr_->updateBatchStatus(batchId, "failed");
    rollbackBatchFiles(batchId);
    return;
  }

  LOG_INFO << "Batch " << batchId << ": " << encodedPaths.size()
           << " encoded files, " << batch.failed_count << " failed";

  batchTaskServicePtr_->updateBatchStatus(batchId, "merging");

  if (encodedPaths.size() == 1) {
    // 单文件：直接移动到输出目录
    LOG_INFO << "Batch " << batchId
             << ": single file, moving to output directory";
    auto movedFiles = moveFilesToOutputDir(encodedPaths, batch.output_dir);
    if (!movedFiles.empty()) {
      std::string finalMp4 = movedFiles[0];
      batchTaskServicePtr_->setBatchFinalPaths(batchId, finalMp4, "");

      // 继续提取 MP3
      batchTaskServicePtr_->updateBatchStatus(batchId, "extracting_mp3");
      ffmpegTaskServicePtr_->submitTask(
          FfmpegTaskType::CONVERT_MP3, {finalMp4}, {batch.output_dir},
          [this, batchId](FfmpegTaskResult result) {
            onMp3Complete(batchId, result);
          });
    } else {
      LOG_ERROR << "Batch " << batchId << ": failed to move file";
      batchTaskServicePtr_->updateBatchStatus(batchId, "failed");
      rollbackBatchFiles(batchId);
    }
  } else {
    // 多文件：合并
    LOG_INFO << "Batch " << batchId << ": merging " << encodedPaths.size()
             << " files...";
    ffmpegTaskServicePtr_->submitTask(
        FfmpegTaskType::MERGE, encodedPaths, {batch.output_dir},
        [this, batchId, encodedPaths](FfmpegTaskResult result) {
          onMergeComplete(batchId, result);
        });
  }
}

void SchedulerService::onMergeComplete(int batchId,
                                       const FfmpegTaskResult &result) {
  auto batchOpt = batchTaskServicePtr_->getBatch(batchId);
  if (!batchOpt) {
    LOG_ERROR << "Batch " << batchId << ": batch not found in onMergeComplete";
    return;
  }
  auto &batch = *batchOpt;

  if (result.status == FfmpegTaskStatus::COMPLETED &&
      !result.outputFiles.empty()) {
    std::string finalMp4 = result.outputFiles[0];
    LOG_INFO << "Batch " << batchId << ": merge successful -> " << finalMp4;

    batchTaskServicePtr_->setBatchFinalPaths(batchId, finalMp4, "");

    // 清理 tmp 中的源编码文件
    auto encodedPaths = batchTaskServicePtr_->getEncodedPaths(batchId);
    for (const auto &path : encodedPaths) {
      try {
        if (fs::exists(path)) {
          fs::remove(path);
          LOG_DEBUG << "清理 tmp 文件: " << path;
        }
      } catch (...) {
      }
    }

    // 继续提取 MP3
    batchTaskServicePtr_->updateBatchStatus(batchId, "extracting_mp3");
    ffmpegTaskServicePtr_->submitTask(FfmpegTaskType::CONVERT_MP3, {finalMp4},
                                      {batch.output_dir},
                                      [this, batchId](FfmpegTaskResult result) {
                                        onMp3Complete(batchId, result);
                                      });
  } else {
    // 合并失败：降级处理，移动所有编码文件到输出目录
    LOG_WARN << "Batch " << batchId
             << ": merge failed, fallback to individual files";

    auto encodedPaths = batchTaskServicePtr_->getEncodedPaths(batchId);
    auto movedFiles = moveFilesToOutputDir(encodedPaths, batch.output_dir);

    // 为每个移动的文件提取 MP3
    for (const auto &mp4Path : movedFiles) {
      ffmpegTaskServicePtr_->submitTask(FfmpegTaskType::CONVERT_MP3, {mp4Path},
                                        {batch.output_dir});
    }

    // 标记为完成（降级处理也视为完成）
    markBatchFilesCompleted(batchId);
    batchTaskServicePtr_->updateBatchStatus(batchId, "completed");
    LOG_INFO << "Batch " << batchId << ": fallback processing completed";
  }
}

void SchedulerService::onMp3Complete(int batchId,
                                     const FfmpegTaskResult &result) {
  auto batchOpt = batchTaskServicePtr_->getBatch(batchId);
  if (!batchOpt) {
    LOG_ERROR << "Batch " << batchId << ": batch not found in onMp3Complete";
    return;
  }

  if (result.status == FfmpegTaskStatus::COMPLETED &&
      !result.outputFiles.empty()) {
    std::string mp3Path = result.outputFiles[0];
    LOG_INFO << "Batch " << batchId << ": MP3 created -> " << mp3Path;
    batchTaskServicePtr_->setBatchFinalPaths(batchId, batchOpt->final_mp4_path,
                                             mp3Path);
  } else {
    LOG_WARN << "Batch " << batchId << ": MP3 extraction failed";
  }

  // 标记所有原始文件为 completed
  markBatchFilesCompleted(batchId);
  batchTaskServicePtr_->updateBatchStatus(batchId, "completed");
  LOG_INFO << "Batch " << batchId << ": processing completed";
}

void SchedulerService::markBatchFilesCompleted(int batchId) {
  auto batchFiles = batchTaskServicePtr_->getBatchFiles(batchId);
  for (const auto &bf : batchFiles) {
    pendingFileServicePtr_->markAsCompleted(bf.getFilepath());
  }
}

void SchedulerService::rollbackBatchFiles(int batchId) {
  auto batchFiles = batchTaskServicePtr_->getBatchFiles(batchId);
  std::vector<std::string> paths;
  for (const auto &bf : batchFiles) {
    paths.push_back(bf.getFilepath());
  }
  pendingFileServicePtr_->rollbackToStable(paths);
}

// ============================================================
// 辅助方法：移动文件到输出目录（降级处理）
// ============================================================
std::vector<std::string>
SchedulerService::moveFilesToOutputDir(const std::vector<std::string> &files,
                                       const std::string &outputDir) {

  std::vector<std::string> movedFiles;

  for (const auto &srcPath : files) {
    try {
      fs::path src(srcPath);
      fs::path dst = fs::path(outputDir) / src.filename();

      if (fs::exists(dst)) {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(epoch)
                .count();
        std::string newName = dst.stem().string() + "_" +
                              std::to_string(millis) + dst.extension().string();
        dst = fs::path(outputDir) / newName;
      }

      fs::rename(src, dst);
      movedFiles.push_back(dst.string());
      LOG_INFO << "移动文件: " << srcPath << " -> " << dst.string();
    } catch (const std::exception &e) {
      LOG_ERROR << "移动文件失败: " << srcPath << ", 错误: " << e.what();
    }
  }

  return movedFiles;
}
