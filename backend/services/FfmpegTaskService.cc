#include "FfmpegTaskService.h"
#include "../utils/CoroUtils.hpp"
#include "ConfigService.h"
#include "ConverterService.h"
#include "MergerService.h"
#include <chrono>
#include <drogon/drogon.h>

using namespace drogon;

// ============================================================
// FfmpegTaskProcDetail 实现
// ============================================================

FfmpegTaskProcDetail::FfmpegTaskProcDetail() {
  // 初始化 future，从 promise 获取
  future_ = promise_.get_future().share();

  // 初始化基础信息
  id = drogon::utils::getUuid();
  status = FfmpegTaskStatus::PENDING;
  createTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  startTime = 0;
  endTime = 0;
}

void FfmpegTaskProcDetail::setPipeInfo(
    const live2mp3::utils::FfmpegPipeInfo &info) {
  pipeInfo.set(info);
}

std::shared_ptr<live2mp3::utils::FfmpegPipeInfo>
FfmpegTaskProcDetail::getPipeInfo() {
  auto ptr = pipeInfo.get();
  return std::make_shared<live2mp3::utils::FfmpegPipeInfo>(*ptr);
}

FfmpegTaskResult FfmpegTaskProcDetail::getProcessResult() {
  std::lock_guard<std::mutex> lock(mutexStatic_);
  FfmpegTaskResult result;
  result.id = id;
  result.type = type;
  result.status = status;
  result.files = files;
  result.outputFiles = outputFiles;
  result.resultMessage = resultMessage;
  result.createTime = createTime;
  result.startTime = startTime;
  result.endTime = endTime;

  // 填充进度信息
  auto pipe = pipeInfo.get();
  if (pipe) {
    result.progressTime = pipe->time;
    result.progressFps = pipe->fps;
    result.progressBitrate = pipe->bitrate;
    result.totalDuration = pipe->totalDuration;
    result.progress = pipe->progress;
    if (startTime > 0) {
      auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      long long elapsed = now - startTime;
      if (elapsed > 0 && pipe->time > 0) {
        result.speed = static_cast<double>(pipe->time) / elapsed;
      } else {
        result.speed = 0.0;
      }
    } else {
      result.speed = 0.0;
    }
  } else {
    result.progressTime = 0;
    result.progressFps = 0;
    result.progressBitrate = 0;
    result.speed = 0.0;
    result.totalDuration = totalDuration_.load();
    result.progress = -1.0;
  }

  return result;
}

void FfmpegTaskProcDetail::setPid(pid_t pid) { pid_ = pid; }

pid_t FfmpegTaskProcDetail::getPid() const { return pid_.load(); }

void FfmpegTaskProcDetail::setTotalDuration(int duration) {
  totalDuration_ = duration;
}

int FfmpegTaskProcDetail::getTotalDuration() const {
  return totalDuration_.load();
}

void FfmpegTaskProcDetail::cancel() {
  cancelled_ = true;
  pid_t currentPid = pid_.load();
  if (currentPid > 0) {
    LOG_DEBUG << "取消任务，终止 FFmpeg 进程 " << currentPid;
    live2mp3::utils::terminateFfmpegProcess(currentPid);
  }
}

bool FfmpegTaskProcDetail::isCancelled() const { return cancelled_.load(); }

void FfmpegTaskProcDetail::run() {
  if (cancelled_) {
    {
      std::lock_guard<std::mutex> lock(mutexStatic_);
      status = FfmpegTaskStatus::FAILED;
      resultMessage = "Task cancelled before execution";
      endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
    }
    try {
      promise_.set_value(getProcessResult());
    } catch (const std::future_error &e) {
      LOG_WARN << "FfmpegTaskProcDetail: promise already set (cancelled): "
               << e.what();
    }
    return;
  }

  // 在线程池中执行
  {
    std::lock_guard<std::mutex> lock(mutexStatic_);
    status = FfmpegTaskStatus::RUNNING;
    startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  }

  try {
    if (executeFunc_.func && !cancelled_) {
      executeFunc_.func(weak_from_this());
    }

    if (cancelled_) {
      {
        std::lock_guard<std::mutex> lock(mutexStatic_);
        status = FfmpegTaskStatus::FAILED;
        resultMessage = "Task cancelled during execution";
        endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
      }
      try {
        promise_.set_value(getProcessResult());
      } catch (const std::future_error &e) {
        LOG_WARN
            << "FfmpegTaskProcDetail: promise already set (cancelled during "
               "execution): "
            << e.what();
      }
      return;
    }

    if (executeFunc_.callback && !cancelled_) {
      executeFunc_.callback(weak_from_this());
    }

    {
      std::lock_guard<std::mutex> lock(mutexStatic_);
      status = FfmpegTaskStatus::COMPLETED;
      endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(mutexStatic_);
    status = FfmpegTaskStatus::FAILED;
    resultMessage = e.what();
    endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
  }

  try {
    promise_.set_value(getProcessResult());
  } catch (const std::future_error &e) {
    LOG_WARN << "FfmpegTaskProcDetail: promise already set: " << e.what();
  }
}

drogon::Task<FfmpegTaskResult> FfmpegTaskProcDetail::execute() {
  run();
  co_return getProcessResult();
}

void FfmpegTaskProcDetail::setOutputFiles(
    const std::vector<std::string> &outputFiles) {
  std::lock_guard<std::mutex> lock(mutexStatic_);
  this->outputFiles = outputFiles;
}

void FfmpegTaskProcDetail::setInfo(const FfmpegTaskInput &input) {
  std::lock_guard<std::mutex> lock(mutexStatic_);
  type = input.type;
  files = input.files;
  outputFiles = input.outputFiles;
  executeFunc_.func = input.func;
  executeFunc_.callback = input.callback;
}

std::string FfmpegTaskProcDetail::getId() {
  std::lock_guard<std::mutex> lock(mutexStatic_);
  return id;
}

std::shared_future<FfmpegTaskResult> FfmpegTaskProcDetail::getFuture() {
  return future_;
}

std::shared_ptr<FfmpegTaskProcDetail>
FfmpegTaskProcDetail::getInstance(const FfmpegTaskInput &input) {
  auto instance = std::make_shared<FfmpegTaskProcDetail>();
  instance->setInfo(input);
  return instance;
}

// 重试支持方法

int FfmpegTaskProcDetail::getRetryCount() const { return retryCount_.load(); }

int FfmpegTaskProcDetail::incrementRetry() { return ++retryCount_; }

void FfmpegTaskProcDetail::setMaxRetries(int max) { maxRetries_ = max; }

int FfmpegTaskProcDetail::getMaxRetries() const { return maxRetries_.load(); }

bool FfmpegTaskProcDetail::isRetryExhausted() const {
  return retryCount_.load() >= maxRetries_.load();
}

void FfmpegTaskProcDetail::resetForRetry() {
  std::lock_guard<std::mutex> lock(mutexStatic_);
  status = FfmpegTaskStatus::PENDING;
  resultMessage.clear();
  startTime = 0;
  endTime = 0;
  cancelled_ = false;
  pid_ = 0;

  // 重建 promise/future 以便再次使用
  promise_ = std::promise<FfmpegTaskResult>();
  future_ = promise_.get_future().share();
}

// ============================================================
// FfAsyncChannel 实现 (调度线程 + 队列驱动)
// ============================================================

FfAsyncChannel::FfAsyncChannel(
    size_t maxConcurrent, int maxRetries,
    std::shared_ptr<CommonThreadService> threadServicePtr)
    : maxConcurrent_(maxConcurrent), maxRetries_(maxRetries),
      threadServicePtr_(threadServicePtr) {
  // 启动调度线程
  schedulerThread_ = std::thread([this]() { schedulerLoop(); });
  LOG_INFO << "FfAsyncChannel: scheduler thread started, maxConcurrent="
           << maxConcurrent << ", maxRetries=" << maxRetries;
}

FfAsyncChannel::~FfAsyncChannel() { close(); }

void FfAsyncChannel::close() {
  bool expected = false;
  if (!closed_.compare_exchange_strong(expected, true)) {
    return; // 已经关闭
  }

  LOG_INFO << "FfAsyncChannel::close() - 正在关闭...";

  // 唤醒调度线程
  cv_.notify_all();

  // 等待调度线程退出
  if (schedulerThread_.joinable()) {
    schedulerThread_.join();
  }

  // 取消所有正在运行的任务
  {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO << "FfAsyncChannel::close() - 取消 " << taskMap_.size()
             << " 个运行中任务";
    for (auto &[id, task] : taskMap_) {
      if (task) {
        task->cancel();
      }
    }
    // 注意：不清理 taskMap_，让 onTaskFinished 正常递减 runningCount_

    // 清空待处理队列
    while (!pendingQueue_.empty()) {
      pendingQueue_.pop();
    }
  }

  // 等待所有运行中的任务完成（onTaskFinished 会递减 runningCount_）
  {
    std::unique_lock<std::mutex> lock(mutex_);
    drainCv_.wait(lock, [this]() { return runningCount_ == 0; });
  }

  LOG_INFO << "FfAsyncChannel::close() - 所有任务已结束";
}

std::vector<FfmpegTaskProcess> FfAsyncChannel::getRunningTasks() {
  std::vector<FfmpegTaskProcess> tasks;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &[id, task] : taskMap_) {
    if (task) {
      tasks.push_back(task->getProcessResult());
    }
  }
  return tasks;
}

void FfAsyncChannel::submit(FfmpegTaskInput item,
                            std::function<void(FfmpegTaskResult)> onComplete) {
  if (closed_) {
    LOG_WARN << "FfAsyncChannel::submit: channel is closed";
    return;
  }

  // 创建任务实例
  auto taskProcDetail = FfmpegTaskProcDetail::getInstance(item);
  taskProcDetail->setMaxRetries(maxRetries_);
  std::string taskId = taskProcDetail->getId();

  LOG_DEBUG << "FfAsyncChannel::submit: queued task id=" << taskId
            << " type=" << static_cast<int>(item.type);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingQueue_.push(std::make_shared<QueueItem>(
        QueueItem{taskProcDetail, std::move(onComplete)}));
  }

  // 唤醒调度线程
  cv_.notify_one();
}

void FfAsyncChannel::schedulerLoop() {
  LOG_INFO << "FfAsyncChannel: scheduler loop started";

  while (true) {
    std::shared_ptr<QueueItem> itemPtr;
    bool hasItem = false;

    {
      std::unique_lock<std::mutex> lock(mutex_);

      // 等待条件：有任务 && 有空闲槽位，或者通道关闭
      cv_.wait(lock, [this]() {
        return closed_ ||
               (!pendingQueue_.empty() && runningCount_ < maxConcurrent_);
      });

      if (closed_ && pendingQueue_.empty()) {
        break; // 通道关闭且无待处理任务
      }

      if (closed_) {
        // 通道关闭但还有待处理任务，丢弃
        LOG_INFO << "FfAsyncChannel: discarding " << pendingQueue_.size()
                 << " pending tasks on close";
        while (!pendingQueue_.empty()) {
          pendingQueue_.pop();
        }
        break;
      }

      if (!pendingQueue_.empty() && runningCount_ < maxConcurrent_) {
        itemPtr = std::move(pendingQueue_.front());
        pendingQueue_.pop();
        hasItem = true;
        runningCount_++;

        // 注册到任务映射表
        std::string taskId = itemPtr->task->getId();
        taskMap_[taskId] = itemPtr->task;
      }
    }

    if (hasItem) {
      std::string taskId = itemPtr->task->getId();

      // 提交到线程池执行
      threadServicePtr_->runTask([this, itemPtr, taskId]() {
        itemPtr->task->run();
        onTaskFinished(taskId, itemPtr);
      });
    }
  }

  LOG_INFO << "FfAsyncChannel: scheduler loop exited";
}

void FfAsyncChannel::onTaskFinished(const std::string &taskId,
                                    std::shared_ptr<QueueItem> itemPtr) {
  FfmpegTaskResult result = itemPtr->task->getProcessResult();

  // 关闭中：跳过重试和回调，只做计数递减
  if (closed_) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      taskMap_.erase(taskId);
      runningCount_--;
    }
    drainCv_.notify_one();
    return;
  }

  if (result.status == FfmpegTaskStatus::FAILED &&
      !itemPtr->task->isCancelled() && !itemPtr->task->isRetryExhausted()) {
    // 任务失败，尚未耗尽重试次数，重新入队
    int retryNum = itemPtr->task->incrementRetry();
    LOG_WARN << "FfAsyncChannel: task " << taskId << " failed, retry "
             << retryNum << "/" << itemPtr->task->getMaxRetries();

    itemPtr->task->resetForRetry();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      taskMap_.erase(taskId);
      runningCount_--;
      pendingQueue_.push(std::move(itemPtr));
    }
    cv_.notify_one();
    return;
  }

  // 任务最终完成（成功或重试耗尽）
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taskMap_.erase(taskId);
    runningCount_--;
  }

  LOG_DEBUG << "FfAsyncChannel: task " << taskId
            << " finished, status=" << static_cast<int>(result.status);

  // 调用完成回调
  if (itemPtr->onComplete) {
    try {
      itemPtr->onComplete(result);
    } catch (const std::exception &e) {
      LOG_ERROR << "FfAsyncChannel: onComplete callback error: " << e.what();
    }
  }

  // 唤醒调度线程处理下一个任务
  cv_.notify_one();
}

// ============================================================
// FfmpegTaskService 实现
// ============================================================

void FfmpegTaskService::initAndStart(const Json::Value &config) {
  size_t maxConcurrent = 2;
  int maxRetries = 3;

  configService_ = drogon::app().getSharedPlugin<ConfigService>();
  if (configService_) {
    auto appConfig = configService_->getConfig();
    maxConcurrent = appConfig.ffmpeg_task.maxConcurrentTasks;
    maxRetries = appConfig.scheduler.ffmpeg_retry_count;
  } else {
    LOG_ERROR << "FfmpegTaskService: ConfigService not found, using defaults";
  }

  threadServicePtr_ = drogon::app().getSharedPlugin<CommonThreadService>();
  if (!threadServicePtr_) {
    LOG_FATAL << "FfmpegTaskService: CommonThreadService not found";
    return;
  }

  size_t threadCount = threadServicePtr_->getThreadCount();

  if (maxConcurrent > threadCount) {
    LOG_WARN << "FfmpegTaskService: maxConcurrentTasks (" << maxConcurrent
             << ") exceeds thread pool size (" << threadCount
             << "), clamping to " << threadCount;
    maxConcurrent = threadCount;
  }

  channel_ = std::make_unique<FfAsyncChannel>(maxConcurrent, maxRetries,
                                              threadServicePtr_);

  LOG_INFO << "FfmpegTaskService initialized: "
           << "maxConcurrent=" << maxConcurrent << ", maxRetries=" << maxRetries
           << ", threadPoolSize=" << threadCount;
}

void FfmpegTaskService::shutdown() {
  LOG_INFO << "FfmpegTaskService shutdown";
  if (channel_) {
    channel_->close();
    channel_.reset();
  }
  threadServicePtr_.reset();
}

void FfmpegTaskService::ConvertMp4Task(
    std::weak_ptr<FfmpegTaskProcDetail> item) {
  auto detail = item.lock();
  if (!detail) {
    LOG_WARN << "FfmpegTaskService::ConvertMp4Task: 任务详情已过期";
    return;
  }

  auto converterService = drogon::app().getSharedPlugin<ConverterService>();
  if (!converterService) {
    LOG_ERROR
        << "FfmpegTaskService::ConvertMp4Task: 获取 ConverterService 失败";
    return;
  }

  auto result = detail->getProcessResult();
  const auto &inputFiles = result.files;
  const auto &outputDirs = result.outputFiles;

  if (inputFiles.empty()) {
    LOG_WARN << "FfmpegTaskService::ConvertMp4Task: 无输入文件";
    return;
  }

  if (outputDirs.empty()) {
    LOG_ERROR << "FfmpegTaskService::ConvertMp4Task: 未指定输出目录";
    return;
  }

  std::string outputDir = outputDirs[0];
  std::string resultMessage;
  bool hasError = false;
  int successCount = 0;

  auto progressCallback = [item](const live2mp3::utils::FfmpegPipeInfo &info) {
    if (auto detail = item.lock()) {
      detail->setPipeInfo(info);
    }
  };

  for (const auto &inputPath : inputFiles) {
    if (detail->isCancelled()) {
      resultMessage = "任务已取消";
      hasError = true;
      break;
    }

    auto cancelCheck = [detail]() {
      return detail->isCancelled() || !drogon::app().isRunning();
    };

    auto pidCallback = [detail](pid_t pid) { detail->setPid(pid); };

    auto outputPath = converterService->convertToAv1Mp4(
        inputPath, outputDir, progressCallback, cancelCheck, pidCallback);
    if (outputPath) {
      successCount++;
      detail->setOutputFiles({*outputPath});
      LOG_INFO << "ConvertMp4Task: 转换成功 " << inputPath << " -> "
               << *outputPath;
    } else {
      hasError = true;
      detail->setOutputFiles({});
      resultMessage += "转换失败: " + inputPath + "; ";
      LOG_ERROR << "ConvertMp4Task: 转换失败 " << inputPath;
    }
  }

  if (!hasError) {
    resultMessage = "成功转换 " + std::to_string(successCount) + " 个文件";
  }

  LOG_DEBUG << "ConvertMp4Task 完成: " << resultMessage;
}

void FfmpegTaskService::ConvertMp3Task(
    std::weak_ptr<FfmpegTaskProcDetail> item) {
  auto detail = item.lock();
  if (!detail) {
    LOG_WARN << "FfmpegTaskService::ConvertMp3Task: 任务详情已过期";
    return;
  }

  auto converterService = drogon::app().getSharedPlugin<ConverterService>();
  if (!converterService) {
    LOG_ERROR
        << "FfmpegTaskService::ConvertMp3Task: 获取 ConverterService 失败";
    return;
  }

  auto result = detail->getProcessResult();
  const auto &inputFiles = result.files;
  const auto &outputDirs = result.outputFiles;

  if (inputFiles.empty()) {
    LOG_WARN << "FfmpegTaskService::ConvertMp3Task: 无输入文件";
    detail->setOutputFiles({});
    return;
  }

  if (outputDirs.empty()) {
    LOG_ERROR << "FfmpegTaskService::ConvertMp3Task: 未指定输出目录";
    detail->setOutputFiles({});
    return;
  }

  for (const auto &file : inputFiles) {
    if (file.empty()) {
      LOG_ERROR << "FfmpegTaskService::ConvertMp3Task: 输入文件路径为空";
      detail->setOutputFiles({});
      return;
    }
  }

  std::string outputDir = outputDirs[0];
  std::string resultMessage;
  bool hasError = false;
  int successCount = 0;

  auto progressCallback = [item](const live2mp3::utils::FfmpegPipeInfo &info) {
    if (auto detail = item.lock()) {
      detail->setPipeInfo(info);
    }
  };

  for (const auto &inputPath : inputFiles) {
    if (detail->isCancelled()) {
      resultMessage = "任务已取消";
      hasError = true;
      break;
    }

    auto cancelCheck = [detail]() {
      return detail->isCancelled() || !drogon::app().isRunning();
    };

    auto pidCallback = [detail](pid_t pid) { detail->setPid(pid); };

    auto outputPath = converterService->extractMp3FromVideo(
        inputPath, outputDir, progressCallback, cancelCheck, pidCallback);
    if (outputPath) {
      successCount++;
      detail->setOutputFiles({*outputPath});
      LOG_INFO << "ConvertMp3Task: 提取成功 " << inputPath << " -> "
               << *outputPath;
    } else {
      hasError = true;
      detail->setOutputFiles({});
      resultMessage += "提取失败: " + inputPath + "; ";
      LOG_ERROR << "ConvertMp3Task: 提取失败 " << inputPath;
    }
  }

  if (!hasError) {
    resultMessage = "成功提取 " + std::to_string(successCount) + " 个文件";
  }

  LOG_DEBUG << "ConvertMp3Task 完成: " << resultMessage;
}

void FfmpegTaskService::MergeTask(std::weak_ptr<FfmpegTaskProcDetail> item) {
  auto detail = item.lock();
  if (!detail) {
    LOG_WARN << "FfmpegTaskService::MergeTask: 任务详情已过期";
    return;
  }

  auto mergerService = drogon::app().getSharedPlugin<MergerService>();
  if (!mergerService) {
    LOG_ERROR << "FfmpegTaskService::MergeTask: 获取 MergerService 失败";
    return;
  }

  auto result = detail->getProcessResult();
  const auto &inputFiles = result.files;
  const auto &outputDirs = result.outputFiles;

  if (inputFiles.empty()) {
    LOG_WARN << "FfmpegTaskService::MergeTask: 无输入文件";
    return;
  }

  if (outputDirs.empty()) {
    LOG_ERROR << "FfmpegTaskService::MergeTask: 未指定输出目录";
    return;
  }

  if (detail->isCancelled()) {
    LOG_INFO << "FfmpegTaskService::MergeTask: 任务已取消";
    return;
  }

  std::string outputDir = outputDirs[0];

  auto progressCallback = [item](const live2mp3::utils::FfmpegPipeInfo &info) {
    if (auto detail = item.lock()) {
      detail->setPipeInfo(info);
    }
  };

  auto cancelCheck = [detail]() {
    return detail->isCancelled() || !drogon::app().isRunning();
  };

  auto pidCallback = [detail](pid_t pid) { detail->setPid(pid); };

  auto outputPath = mergerService->mergeVideoFiles(
      inputFiles, outputDir, progressCallback, cancelCheck, pidCallback);
  if (outputPath) {
    detail->setOutputFiles({*outputPath});
    LOG_INFO << "MergeTask: 合并成功 " << inputFiles.size() << " 个文件 -> "
             << *outputPath;
  } else {
    detail->setOutputFiles({});
    LOG_ERROR << "MergeTask: 合并失败 " << inputFiles.size() << " 个文件";
  }

  LOG_DEBUG << "MergeTask 完成";
}

// ============================================================
// FfmpegTaskService 接口实现
// ============================================================

std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)>
FfmpegTaskService::getTaskFunc(FfmpegTaskType type) {
  switch (type) {
  case FfmpegTaskType::CONVERT_MP4:
    return ConvertMp4Task;
  case FfmpegTaskType::CONVERT_MP3:
    return ConvertMp3Task;
  case FfmpegTaskType::MERGE:
    return MergeTask;
  case FfmpegTaskType::OTHER:
  default:
    return nullptr;
  }
}

void FfmpegTaskService::submitTask(
    FfmpegTaskType type, const std::vector<std::string> &files,
    const std::vector<std::string> &outputFiles,
    std::function<void(FfmpegTaskResult)> onComplete,
    std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> callback,
    std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> customFunc) {

  if (!channel_) {
    LOG_ERROR << "FfmpegTaskService::submitTask: channel_ 未初始化";
    return;
  }

  auto taskFunc = getTaskFunc(type);
  if (!taskFunc) {
    if (customFunc) {
      taskFunc = customFunc;
    } else {
      LOG_ERROR
          << "FfmpegTaskService::submitTask: 未知任务类型且未提供自定义函数";
      return;
    }
  }

  FfmpegTaskInput input;
  input.type = type;
  input.files = files;
  input.outputFiles = outputFiles;
  input.func = taskFunc;
  input.callback = callback;

  channel_->submit(std::move(input), std::move(onComplete));
}

std::vector<FfmpegTaskProcess> FfmpegTaskService::getRunningTasks() {
  if (!channel_) {
    return {};
  }
  return channel_->getRunningTasks();
}
