#include "SchedulerService.h"
#include "../utils/CoroUtils.hpp"
#include "../utils/FileUtils.h"
#include <drogon/drogon.h>
#include <filesystem>

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
    auto config = configServicePtr_->getConfig();
    int interval = config.scheduler.scan_interval_seconds;
    if (interval <= 0)
      interval = 60;

    // 使用协程定时任务
    drogon::app().getLoop()->runEvery(interval * 1.0, [this]() {
      drogon::async_run(
          [this]() -> drogon::Task<> { co_await this->runTaskAsync(false); });
    });

    LOG_INFO << "Scheduler started with interval " << interval << "s";
  });
}

void SchedulerService::triggerNow() {
  // 使用协程立即触发任务
  drogon::async_run(
      [this]() -> drogon::Task<> { co_await this->runTaskAsync(true); });
}

drogon::Task<void> SchedulerService::runTaskAsync(bool immediate) {
  // 使用原子交换防止并发执行
  if (running_.exchange(true)) {
    LOG_DEBUG << "Task already running, skipping";
    co_return;
  }

  LOG_INFO << "Starting scheduled task..."
           << (immediate ? " (immediate mode)" : "");

  // 阶段 1: 在线程池中执行 MD5 计算（CPU 密集型操作）
  setPhase("stability_scan");
  co_await live2mp3::utils::awaitFuture(
      commonThreadServicePtr_->runTaskAsync([this]() { runStabilityScan(); }));

  // 阶段 2: 协程发送转换任务
  setPhase("conversion");
  co_await runConversionAsync();

  // 阶段 3: 协程发送合并任务
  setPhase("merge_output");
  co_await runMergeAndOutputAsync(immediate);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    currentFile_ = "";
    currentPhase_ = "";
  }

  LOG_INFO << "Task finished.";
  running_ = false;
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

    // 计算文件 MD5
    std::string md5 = live2mp3::utils::calculateMD5(file);
    if (md5.empty()) {
      LOG_WARN << "Failed to calculate MD5 for: " << file;
      continue;
    }

    // 更新或添加到待处理文件列表
    int stableCount = pendingFileServicePtr_->addOrUpdateFile(file, md5);

    if (stableCount >= requiredStableCount) {
      LOG_INFO << "File is stable (count=" << stableCount << "): " << file;
      // 标记文件为稳定状态，准备转换
      pendingFileServicePtr_->markAsStable(file);
    } else {
      LOG_DEBUG << "File stability count: " << stableCount << " for: " << file;
    }
  }
}

drogon::Task<void> SchedulerService::runConversionAsync() {
  LOG_INFO << "Phase 2: Converting files with 'stable' status...";

  auto config = configServicePtr_->getConfig();

  // 获取所有稳定状态的文件
  auto stableFiles = pendingFileServicePtr_->getAllStableFiles();
  LOG_INFO << "Found " << stableFiles.size() << " stable files to convert";

  for (const auto &pf : stableFiles) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      currentFile_ = pf.filepath;
    }

    // 检查源文件是否存在
    if (!fs::exists(pf.filepath)) {
      LOG_WARN << "Source file no longer exists: " << pf.filepath;
      pendingFileServicePtr_->removeFile(pf.filepath);
      continue;
    }

    // 标记为转换中
    pendingFileServicePtr_->markAsConverting(pf.filepath);

    // 检查临时空间
    uint64_t fileSize = 0;
    try {
      fileSize = fs::file_size(pf.filepath);
    } catch (...) {
      fileSize = 100 * 1024 * 1024; // 无法获取大小时假设 100MB
    }

    std::string outputDir;
    bool useTempDir = false;

    if (!config.temp.temp_dir.empty() &&
        converterServicePtr_->hasTempSpace(fileSize)) {
      outputDir = config.temp.temp_dir;
      useTempDir = true;
    } else {
      outputDir = config.output.output_root;
      LOG_INFO << "Temp space insufficient or not configured, using output dir "
                  "directly";
    }

    // 通过 FfmpegTaskService 提交转换任务
    auto taskResult = co_await ffmpegTaskServicePtr_->submitTask(
        FfmpegTaskType::CONVERT_MP4, {pf.filepath}, {outputDir});

    if (taskResult.has_value() && !taskResult->outputFiles.empty()) {
      std::string outputPath = taskResult->outputFiles[0];
      if (useTempDir) {
        // 标记为已暂存（包含临时路径）
        pendingFileServicePtr_->markAsStaged(pf.filepath, outputPath);
        LOG_INFO << "File staged in temp: " << outputPath;
      } else {
        // 直接输出 - 同时提取 MP3
        auto mp3TaskResult = co_await ffmpegTaskServicePtr_->submitTask(
            FfmpegTaskType::CONVERT_MP3, {outputPath},
            {config.output.output_root});
        if (mp3TaskResult.has_value()) {
          LOG_INFO << "MP3 extraction task submitted";
        }
        pendingFileServicePtr_->markAsCompleted(pf.filepath);
      }
    } else {
      LOG_ERROR << "Failed to convert: " << pf.filepath;
      // 重置为待处理状态，以便重试
      pendingFileServicePtr_->addOrUpdateFile(pf.filepath, pf.current_md5);
    }
  }
}

drogon::Task<void> SchedulerService::runMergeAndOutputAsync(bool immediate) {
  LOG_INFO << "Phase 3: Processing staged files..."
           << (immediate ? " (immediate mode)" : "");

  auto config = configServicePtr_->getConfig();
  int mergeWindowSeconds = config.scheduler.merge_window_seconds;

  // 获取已暂存的文件
  std::vector<PendingFile> filesToProcess;
  if (immediate) {
    filesToProcess = pendingFileServicePtr_->getAllStagedFiles();
    LOG_INFO << "Immediate mode: processing all " << filesToProcess.size()
             << " staged files";
  } else {
    filesToProcess = pendingFileServicePtr_->getAllStagedFiles();
  }

  if (filesToProcess.empty()) {
    co_return;
  }

  // 1. 按父目录分组（主播/分类）
  std::map<std::string, std::vector<StagedFile>> groupedFiles;

  for (const auto &pf : filesToProcess) {
    if (pf.temp_mp4_path.empty() || !fs::exists(pf.temp_mp4_path)) {
      LOG_WARN << "Staged file missing: " << pf.temp_mp4_path;
      pendingFileServicePtr_->removeFile(pf.filepath);
      continue;
    }

    // 从原始文件名解析时间
    fs::path originalPath(pf.filepath);
    auto t = MergerService::parseTime(originalPath.filename().string());

    if (!t) {
      LOG_WARN << "Could not parse time for file, treating as standalone: "
               << pf.filepath;
      continue;
    }

    std::string parentDir = originalPath.parent_path().string();
    groupedFiles[parentDir].push_back({pf, *t});
  }

  // 2. 处理每个目录分组
  for (auto &[parentDir, files] : groupedFiles) {
    // 按时间排序
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
          // 检测到时间间隔，处理之前的批次
          co_await processBatchAsync(currentBatch, config);
          currentBatch.clear();
          currentBatch.push_back(file);
        }
      }

      // 检查是否应处理当前/最后一个批次
      if (isLast) {
        if (immediate) {
          co_await processBatchAsync(currentBatch, config);
        } else {
          auto now = std::chrono::system_clock::now();
          auto age = std::chrono::duration_cast<std::chrono::seconds>(
                         now - currentBatch.back().time)
                         .count();
          if (age > mergeWindowSeconds) {
            co_await processBatchAsync(currentBatch, config);
          }
        }
      }
    }
  }
}

drogon::Task<void>
SchedulerService::processBatchAsync(const std::vector<StagedFile> &batch,
                                    const AppConfig &config) {
  if (batch.empty())
    co_return;

  LOG_INFO << "Processing batch of " << batch.size() << " files";

  // 准备文件路径
  std::vector<std::string> videoPaths;
  for (const auto &f : batch) {
    videoPaths.push_back(f.pf.temp_mp4_path);
  }

  // 根据第一个文件的父文件夹名确定输出目录
  fs::path firstOrigPath(batch[0].pf.filepath);
  std::string streamerName = firstOrigPath.parent_path().filename().string();
  fs::path outputDir = fs::path(config.output.output_root) / streamerName;

  // 确保输出目录存在
  try {
    fs::create_directories(outputDir);
  } catch (...) {
  }

  // 使用临时目录进行合并操作
  fs::path tempDir(fs::path(batch[0].pf.temp_mp4_path).parent_path());

  // 1. 通过 FfmpegTaskService 提交合并任务
  auto mergeResult = co_await ffmpegTaskServicePtr_->submitTask(
      FfmpegTaskType::MERGE, videoPaths, {tempDir.string()});

  if (!mergeResult.has_value() || mergeResult->outputFiles.empty()) {
    LOG_ERROR << "Failed to merge batch or no output file produced";
    co_return;
  }

  // 合并成功后，使用任务返回的实际路径
  std::string mergedMp4Path = mergeResult->outputFiles[0];
  fs::path finalMp4Path = outputDir / fs::path(mergedMp4Path).filename();

  // 2. 移动合并后的 MP4 到输出目录
  try {
    if (fs::exists(finalMp4Path))
      fs::remove(finalMp4Path);
    fs::rename(mergedMp4Path, finalMp4Path);
    LOG_INFO << "Moved final MP4 to: " << finalMp4Path;
  } catch (const std::exception &e) {
    LOG_ERROR << "Failed to move output MP4: " << e.what();
    co_return;
  }

  // 3. 通过 FfmpegTaskService 提交 MP3 提取任务
  auto mp3Result = co_await ffmpegTaskServicePtr_->submitTask(
      FfmpegTaskType::CONVERT_MP3, {finalMp4Path.string()},
      {config.output.output_root});

  if (mp3Result.has_value()) {
    LOG_INFO << "MP3 extraction task completed";
  }

  // 4. 标记所有原始 FLV 为已完成
  for (const auto &f : batch) {
    pendingFileServicePtr_->markAsCompleted(f.pf.filepath);
  }

  // 5. 清理临时目录中的组件 MP4
  for (const auto &f : batch) {
    if (f.pf.temp_mp4_path != mergedMp4Path) {
      try {
        fs::remove(f.pf.temp_mp4_path);
      } catch (...) {
      }
    }
  }
}
