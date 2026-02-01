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
  LOG_INFO << "Starting scheduled task..."
           << (immediate ? " (immediate mode)" : "");

  // 阶段 1: 扫描阶段使用独立的并发控制
  // 防止扫描阶段重复执行（MD5 计算是 CPU 密集型操作）
  bool scanSkipped = scanRunning_.exchange(true);
  if (!scanSkipped) {
    setPhase("stability_scan");
    co_await live2mp3::utils::awaitFuture(commonThreadServicePtr_->runTaskAsync(
        [this]() { runStabilityScan(); }));
    scanRunning_ = false;

    // 扫描完成后，输出当前正在执行的任务（DEBUG 级别）
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
        // 格式化已处理时长 (mm:ss)
        int progressSec = task.progressTime / 1000;
        int progressMin = progressSec / 60;
        progressSec = progressSec % 60;
        char progressTimeStr[16];
        snprintf(progressTimeStr, sizeof(progressTimeStr), "%02d:%02d",
                 progressMin, progressSec);

        // 格式化速度倍率
        char speedStr[16];
        snprintf(speedStr, sizeof(speedStr), "%.2fx", task.speed);

        LOG_DEBUG << "  - [" << taskTypeStr << "] " << filesStr
                  << " | 进度: " << progressTimeStr
                  << " | fps: " << task.progressFps << " | 速度: " << speedStr;
      }
    }
  } else {
    LOG_DEBUG << "Scan already running, skipping scan phase";
  }

  // 阶段 2: 处理阶段
  // 多个批次可以并发提交到 FfmpegTaskService
  // 由 FfmpegTaskService 的信号量（SimpleCoroSemaphore）控制实际并发执行
  setPhase("merge_encode_output");
  co_await runMergeEncodeOutputAsync(immediate);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    currentFile_ = "";
    currentPhase_ = "";
  }

  LOG_INFO << "Task finished.";
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

    // 计算文件指纹
    std::string fingerprint = live2mp3::utils::calculateFileFingerprint(file);
    if (fingerprint.empty()) {
      LOG_WARN << "无法计算文件指纹: " << file;
      continue;
    }

    // 更新或添加到待处理文件列表
    int stableCount =
        pendingFileServicePtr_->addOrUpdateFile(file, fingerprint);

    if (stableCount >= requiredStableCount) {
      LOG_INFO << "File is stable (count=" << stableCount << "): " << file;
      // 标记文件为稳定状态，准备转换
      pendingFileServicePtr_->markAsStable(file);
    } else {
      LOG_DEBUG << "File stability count: " << stableCount << " for: " << file;
    }
  }
}

drogon::Task<void> SchedulerService::runMergeEncodeOutputAsync(bool immediate) {
  LOG_INFO << "Phase 2: Processing stable files for merge + encode..."
           << (immediate ? " (immediate mode)" : "");

  auto config = configServicePtr_->getConfig();
  int mergeWindowSeconds = config.scheduler.merge_window_seconds;
  int stopWaitingSeconds = config.scheduler.stop_waiting_seconds;

  // 1. 原子性地获取并标记所有稳定的原始文件为 processing
  // 这样可以防止并发任务获取相同的文件
  auto stableFiles = pendingFileServicePtr_->getAndClaimStableFiles();
  if (stableFiles.empty()) {
    LOG_DEBUG << "No stable files to process";
    co_return;
  }
  LOG_INFO << "Claimed " << stableFiles.size()
           << " stable files for processing";

  // 2. 按主播名分组（从文件名解析）
  std::map<std::string, std::vector<StableFile>> groupedByStreamer;
  for (const auto &pf : stableFiles) {
    if (!fs::exists(pf.filepath)) {
      LOG_WARN << "Source file no longer exists: " << pf.filepath;
      pendingFileServicePtr_->removeFile(pf.filepath);
      continue;
    }

    fs::path filePath(pf.filepath);
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
    // 按时间降序排列（最新的在前）
    std::sort(files.begin(), files.end(),
              [](const StableFile &a, const StableFile &b) {
                return a.time > b.time;
              });

    // 分配批次
    std::set<size_t> assigned; // 已分配的文件索引
    std::vector<std::vector<StableFile>> batches;

    for (size_t i = 0; i < files.size(); ++i) {
      if (assigned.count(i))
        continue; // 已分配则跳过

      // 创建新批次
      std::vector<StableFile> batch;
      batch.push_back(files[i]);
      assigned.insert(i);

      // 向前查找连续的录播片段
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
          break; // 间隔过大，结束本批次
        }
      }

      batches.push_back(std::move(batch));
    }

    // 4. 判断并处理每个批次
    auto now = std::chrono::system_clock::now();
    for (auto &batch : batches) {
      // 使用批次中最新文件的最后修改时间判断
      std::error_code ec;
      auto lastModTime = fs::last_write_time(batch[0].pf.filepath, ec);
      if (ec) {
        LOG_WARN << "Could not get last write time for: "
                 << batch[0].pf.filepath;
        continue;
      }
      auto lastModTimePoint = std::chrono::file_clock::to_sys(lastModTime);
      auto age = std::chrono::duration_cast<std::chrono::seconds>(
                     now - lastModTimePoint)
                     .count();

      if (!immediate && age <= stopWaitingSeconds) {
        LOG_DEBUG << "Batch for streamer '" << streamer
                  << "' not ready yet (age=" << age
                  << "s, threshold=" << stopWaitingSeconds << "s)";
        continue; // 未超过等待时间，跳过
      }

      // 按时间升序排列（合并时需要按时间顺序）
      std::reverse(batch.begin(), batch.end());

      // 处理这批文件
      co_await processBatchAsync(batch, config);
    }
  }
}

drogon::Task<void>
SchedulerService::processBatchAsync(const std::vector<StableFile> &batch,
                                    const AppConfig &config) {
  if (batch.empty())
    co_return;

  LOG_INFO << "Processing batch of " << batch.size() << " files";

  // 准备原始 FLV 文件路径列表
  std::vector<std::string> flvPaths;
  for (const auto &f : batch) {
    flvPaths.push_back(f.pf.filepath);
  }

  // 注意：文件已在 getAndClaimStableFiles 中被原子性标记为 processing
  // 无需在此再次调用 markAsProcessingBatch

  // 使用批次中最新的文件名作为输出文件名基础（batch
  // 已按时间升序排列，最后一个是最新的）
  fs::path latestFilePath(batch.back().pf.filepath);
  std::string baseOutputName = latestFilePath.stem().string();

  // 根据主播名确定输出目录
  std::string streamer =
      MergerService::parseTitle(latestFilePath.filename().string());
  fs::path outputDir = fs::path(config.output.output_root) / streamer;
  fs::path tmpDir = fs::path(config.output.output_root) / "tmp";

  // 确保输出目录和临时目录存在
  try {
    fs::create_directories(outputDir);
    fs::create_directories(tmpDir);
  } catch (...) {
  }

  // ============================================================
  // 阶段 1: 统一编码到 tmp 目录
  // ============================================================
  LOG_INFO << "阶段 1: 统一编码 " << flvPaths.size() << " 个文件到 tmp 目录...";

  auto encodeResult = co_await encodeFilesToTmpAsync(
      flvPaths, tmpDir.string(), config.scheduler.convert_retry_count);

  if (encodeResult.successCount == 0) {
    LOG_ERROR << "所有文件编码失败，批次处理终止";
    // 处理失败，回滚状态到 stable
    pendingFileServicePtr_->rollbackToStable(flvPaths);
    co_return;
  }

  LOG_INFO << "编码完成: 成功 " << encodeResult.successCount << " 个, 失败 "
           << encodeResult.failCount << " 个";

  // ============================================================
  // 阶段 2: 合并处理
  // ============================================================
  std::string finalMp4Path;
  bool mergeSuccess = false;

  if (encodeResult.successPaths.size() == 1) {
    // 单文件：直接移动到输出目录
    LOG_INFO << "只有单个成功编码的文件，直接移动到输出目录";
    auto movedFiles =
        moveFilesToOutputDir(encodeResult.successPaths, outputDir.string());
    if (!movedFiles.empty()) {
      finalMp4Path = movedFiles[0];
      mergeSuccess = true;
    }
  } else {
    // 多文件：合并
    LOG_INFO << "阶段 2: 合并 " << encodeResult.successPaths.size()
             << " 个编码文件...";

    auto mergeResult = co_await mergeWithRetryAsync(
        encodeResult.successPaths, outputDir.string(),
        config.scheduler.merge_retry_count);

    if (mergeResult.has_value()) {
      finalMp4Path = *mergeResult;
      mergeSuccess = true;

      // 合并成功，清理 tmp 中的源文件
      for (const auto &path : encodeResult.successPaths) {
        try {
          if (fs::exists(path)) {
            fs::remove(path);
            LOG_DEBUG << "清理 tmp 文件: " << path;
          }
        } catch (...) {
        }
      }
    } else {
      // ============================================================
      // 阶段 2.5: 合并失败降级处理
      // ============================================================
      LOG_WARN << "合并失败，执行降级处理：将所有编码文件移动到输出目录";
      auto movedFiles =
          moveFilesToOutputDir(encodeResult.successPaths, outputDir.string());

      // 为每个移动的文件提取 MP3
      for (const auto &mp4Path : movedFiles) {
        LOG_INFO << "为降级文件提取 MP3: " << mp4Path;
        auto mp3Result = co_await ffmpegTaskServicePtr_->submitTask(
            FfmpegTaskType::CONVERT_MP3, {mp4Path}, {outputDir.string()});

        if (mp3Result.has_value() && !mp3Result->outputFiles.empty()) {
          LOG_INFO << "MP3 提取成功: " << mp3Result->outputFiles[0];
        } else {
          LOG_WARN << "MP3 提取失败: " << mp4Path;
        }
      }

      // 标记所有原始 FLV 为已完成（即使降级处理）
      for (const auto &f : batch) {
        pendingFileServicePtr_->markAsCompleted(f.pf.filepath);
      }

      LOG_INFO << "降级处理完成，批次结束";
      co_return;
    }
  }

  if (!mergeSuccess || finalMp4Path.empty()) {
    LOG_ERROR << "合并/移动失败";
    pendingFileServicePtr_->rollbackToStable(flvPaths);
    co_return;
  }

  LOG_INFO << "AV1 MP4 created: " << finalMp4Path;

  // ============================================================
  // 阶段 3: 提取 MP3
  // ============================================================
  LOG_INFO << "阶段 3: 提取 MP3...";
  auto mp3Result = co_await ffmpegTaskServicePtr_->submitTask(
      FfmpegTaskType::CONVERT_MP3, {finalMp4Path}, {outputDir.string()});

  if (mp3Result.has_value() && !mp3Result->outputFiles.empty()) {
    LOG_INFO << "MP3 created: " << mp3Result->outputFiles[0];
  } else {
    LOG_WARN << "MP3 extraction failed";
  }

  // 标记所有原始 FLV 为已完成
  for (const auto &f : batch) {
    pendingFileServicePtr_->markAsCompleted(f.pf.filepath);
  }

  LOG_INFO << "Batch processing completed";
}

// ============================================================
// 辅助方法：统一编码到 tmp 目录
// ============================================================
drogon::Task<SchedulerService::BatchEncodeResult>
SchedulerService::encodeFilesToTmpAsync(const std::vector<std::string> &files,
                                        const std::string &tmpDir,
                                        int maxRetries) {

  BatchEncodeResult result;
  result.successCount = 0;
  result.failCount = 0;

  for (const auto &inputPath : files) {
    ConvertResult convertResult;
    convertResult.originalPath = inputPath;
    convertResult.success = false;
    convertResult.retryCount = 0;

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
      convertResult.retryCount = attempt;
      LOG_INFO << "编码文件 (尝试 " << attempt << "/" << maxRetries
               << "): " << inputPath;

      auto encodeTaskResult = co_await ffmpegTaskServicePtr_->submitTask(
          FfmpegTaskType::CONVERT_MP4, {inputPath}, {tmpDir});

      if (encodeTaskResult.has_value() &&
          !encodeTaskResult->outputFiles.empty()) {
        convertResult.success = true;
        convertResult.convertedPath = encodeTaskResult->outputFiles[0];
        LOG_INFO << "编码成功: " << inputPath << " -> "
                 << convertResult.convertedPath;
        break;
      } else {
        LOG_WARN << "编码失败 (尝试 " << attempt << "/" << maxRetries
                 << "): " << inputPath;
      }
    }

    result.results.push_back(convertResult);
    if (convertResult.success) {
      result.successCount++;
      result.successPaths.push_back(convertResult.convertedPath);
    } else {
      result.failCount++;
      LOG_ERROR << "文件编码失败（已达最大重试次数）: " << inputPath;
    }
  }

  co_return result;
}

// ============================================================
// 辅助方法：带重试的合并操作
// ============================================================
drogon::Task<std::optional<std::string>>
SchedulerService::mergeWithRetryAsync(const std::vector<std::string> &files,
                                      const std::string &outputDir,
                                      int maxRetries) {

  for (int attempt = 1; attempt <= maxRetries; ++attempt) {
    LOG_INFO << "合并文件 (尝试 " << attempt << "/" << maxRetries
             << "): " << files.size() << " 个文件";

    auto mergeResult = co_await ffmpegTaskServicePtr_->submitTask(
        FfmpegTaskType::MERGE, files, {outputDir});

    if (mergeResult.has_value() && !mergeResult->outputFiles.empty()) {
      LOG_INFO << "合并成功: " << mergeResult->outputFiles[0];
      co_return mergeResult->outputFiles[0];
    } else {
      LOG_WARN << "合并失败 (尝试 " << attempt << "/" << maxRetries << ")";
    }
  }

  LOG_ERROR << "合并失败（已达最大重试次数）";
  co_return std::nullopt;
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

      // 如果目标已存在，添加时间戳后缀
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
