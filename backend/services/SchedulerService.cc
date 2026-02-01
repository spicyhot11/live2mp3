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

drogon::Task<void> SchedulerService::runMergeEncodeOutputAsync(bool immediate) {
  LOG_INFO << "Phase 2: Processing stable files for merge + encode..."
           << (immediate ? " (immediate mode)" : "");

  auto config = configServicePtr_->getConfig();
  int mergeWindowSeconds = config.scheduler.merge_window_seconds;
  int stopWaitingSeconds = config.scheduler.stop_waiting_seconds;

  // 1. 获取所有稳定的原始 FLV 文件
  auto stableFiles = pendingFileServicePtr_->getAllStableFiles();
  if (stableFiles.empty()) {
    LOG_DEBUG << "No stable files to process";
    co_return;
  }
  LOG_INFO << "Found " << stableFiles.size() << " stable files";

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

  // 开始处理前，将批次内所有文件标记为 processing 状态
  // 这样后续扫描会跳过这些文件
  if (!pendingFileServicePtr_->markAsProcessingBatch(flvPaths)) {
    LOG_ERROR << "Failed to mark batch as processing, aborting";
    co_return;
  }

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

  std::string mergedFlvPath;

  if (flvPaths.size() > 1) {
    // 多文件：先合并原始 FLV，临时文件输出到 output_root/tmp 目录
    LOG_INFO << "Merging " << flvPaths.size() << " FLV files...";

    auto mergeResult = co_await ffmpegTaskServicePtr_->submitTask(
        FfmpegTaskType::MERGE, flvPaths, {tmpDir.string()});

    if (!mergeResult.has_value() || mergeResult->outputFiles.empty()) {
      LOG_ERROR << "Failed to merge FLV files";
      // 处理失败，回滚状态到 stable
      pendingFileServicePtr_->rollbackToStable(flvPaths);
      co_return;
    }
    mergedFlvPath = mergeResult->outputFiles[0];
  } else {
    // 单文件：直接使用
    mergedFlvPath = flvPaths[0];
  }

  // 2. 将合并后的 FLV 编码为 AV1 MP4，直接输出到最终目录
  LOG_INFO << "Encoding to AV1 MP4...";
  auto encodeResult = co_await ffmpegTaskServicePtr_->submitTask(
      FfmpegTaskType::CONVERT_MP4, {mergedFlvPath}, {outputDir.string()});

  if (!encodeResult.has_value() || encodeResult->outputFiles.empty()) {
    LOG_ERROR << "Failed to encode to AV1 MP4";
    // 清理合并产生的临时文件
    if (flvPaths.size() > 1 && fs::exists(mergedFlvPath)) {
      try {
        fs::remove(mergedFlvPath);
      } catch (...) {
      }
    }
    // 处理失败，回滚状态到 stable
    pendingFileServicePtr_->rollbackToStable(flvPaths);
    co_return;
  }

  std::string finalMp4Path = encodeResult->outputFiles[0];
  LOG_INFO << "AV1 MP4 created: " << finalMp4Path;

  // 3. 提取 MP3
  LOG_INFO << "Extracting MP3...";
  auto mp3Result = co_await ffmpegTaskServicePtr_->submitTask(
      FfmpegTaskType::CONVERT_MP3, {finalMp4Path}, {outputDir.string()});

  if (mp3Result.has_value() && !mp3Result->outputFiles.empty()) {
    LOG_INFO << "MP3 created: " << mp3Result->outputFiles[0];
  } else {
    LOG_WARN << "MP3 extraction failed";
  }

  // 4. 标记所有原始 FLV 为已完成
  for (const auto &f : batch) {
    pendingFileServicePtr_->markAsCompleted(f.pf.filepath);
  }

  // 5. 清理：删除合并产生的临时 FLV（如果有）
  if (flvPaths.size() > 1 && fs::exists(mergedFlvPath)) {
    try {
      fs::remove(mergedFlvPath);
      LOG_DEBUG << "Cleaned up merged FLV: " << mergedFlvPath;
    } catch (...) {
    }
  }

  LOG_INFO << "Batch processing completed";
}
