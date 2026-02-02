#include "FfmpegTaskService.h"
#include "../utils/CoroUtils.hpp"
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
    // 计算速度倍率：已处理时长 / 实际耗时
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
  // 终止 FFmpeg 进程
  pid_t currentPid = pid_.load();
  if (currentPid > 0) {
    LOG_DEBUG << "取消任务，终止 FFmpeg 进程 " << currentPid;
    live2mp3::utils::terminateFfmpegProcess(currentPid);
  }
}

bool FfmpegTaskProcDetail::isCancelled() const { return cancelled_.load(); }

void FfmpegTaskProcDetail::run() {
  // 检查是否已被取消
  if (cancelled_) {
    std::lock_guard<std::mutex> lock(mutexStatic_);
    status = FfmpegTaskStatus::FAILED;
    resultMessage = "Task cancelled before execution";
    endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    promise_.set_value(getProcessResult());
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
    // 执行任务函数
    if (executeFunc_.func && !cancelled_) {
      executeFunc_.func(weak_from_this());
    }

    // 再次检查取消标志
    if (cancelled_) {
      std::lock_guard<std::mutex> lock(mutexStatic_);
      status = FfmpegTaskStatus::FAILED;
      resultMessage = "Task cancelled during execution";
      endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
      promise_.set_value(getProcessResult());
      return;
    }

    // 执行回调函数
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

  // 设置 promise 值，通知等待者任务已完成
  try {
    promise_.set_value(getProcessResult());
  } catch (const std::future_error &e) {
    // promise 可能已经被设置过，忽略
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

// ============================================================
// FfAsyncChannel 实现
// ============================================================

FfAsyncChannel::FfAsyncChannel(
    size_t capacity, size_t maxWaiting,
    std::shared_ptr<CommonThreadService> threadServicePtr)
    : semaphore_(capacity, maxWaiting), threadServicePtr_(threadServicePtr) {}

FfAsyncChannel::~FfAsyncChannel() { close(); }

void FfAsyncChannel::close() {
  // 取消所有正在运行的任务
  std::lock_guard<std::mutex> lock(mutex_);
  LOG_INFO << "FfAsyncChannel::close() - 取消 " << taskMap_.size() << " 个任务";
  for (auto &[id, task] : taskMap_) {
    if (task) {
      task->cancel();
    }
  }
  taskMap_.clear();
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

drogon::Task<std::optional<FfmpegTaskResult>>
FfAsyncChannel::send(FfmpegTaskInput item) {
  // 1. 获取信号量许可
  bool acquired = co_await semaphore_.acquire();
  if (!acquired) {
    LOG_WARN << "FfmpegTaskService: failed to acquire semaphore, type: "
             << static_cast<int>(item.type);
    co_return std::nullopt; // 返回空optional表示失败
  }

  // 2. 创建任务并获取 ID
  std::shared_ptr<FfmpegTaskProcDetail> taskProcDetail =
      FfmpegTaskProcDetail::getInstance(item);
  std::string taskId = taskProcDetail->getId();

  // 3. 注册到任务映射表，保持 shared_ptr 活跃
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taskMap_[taskId] = taskProcDetail;
  }

  // 4. 提交任务到线程池并等待完成
  try {
    auto future = threadServicePtr_->runTaskAsync(
        [taskProcDetail]() { taskProcDetail->run(); });

    // 使用FutureAwaiter等待任务完成 - 零轮询！
    co_await live2mp3::utils::awaitFuture(std::move(future));
  } catch (const std::exception &e) {
    LOG_ERROR << "FfmpegTaskService: task execution failed: " << e.what();

    // 释放信号量
    semaphore_.release();

    // 从任务映射表中移除
    {
      std::lock_guard<std::mutex> lock(mutex_);
      taskMap_.erase(taskId);
    }

    co_return std::nullopt; // 返回失败
  }

  // 5. 任务完成，释放信号量
  semaphore_.release();

  // 获取执行结果
  FfmpegTaskResult result = taskProcDetail->getProcessResult();

  // 6. 从任务映射表中移除
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taskMap_.erase(taskId);
  }

  LOG_DEBUG << "FfmpegTaskService: task completed, id: " << taskId;
  co_return result; // 返回成功的结果
}

void FfAsyncChannel::sendAsync(
    FfmpegTaskInput item,
    std::function<void(std::optional<std::string>)> callback) {
  // 使用 drogon::async_run 启动协程，实现发射后不管
  drogon::async_run(
      [this, item = std::move(item),
       callback = std::move(callback)]() mutable -> drogon::Task<> {
        auto result = co_await this->send(std::move(item));

        // 如果提供了回调函数，则调用它
        if (callback) {
          if (result.has_value()) {
            callback(result->id);
          } else {
            callback(std::nullopt);
          }
        }
      });
}

// ============================================================
// FfmpegTaskService 实现
// ============================================================

void FfmpegTaskService::initAndStart(const Json::Value &config) {
  // 读取配置
  size_t maxConcurrent = 8; // 默认值
  size_t maxWaiting = 100;

  if (config.isMember("maxConcurrentTasks") &&
      config["maxConcurrentTasks"].isUInt()) {
    maxConcurrent = config["maxConcurrentTasks"].asUInt();
  }

  if (config.isMember("maxWaitingTasks") &&
      config["maxWaitingTasks"].isUInt()) {
    maxWaiting = config["maxWaitingTasks"].asUInt();
  }

  // 获取 CommonThreadService
  threadServicePtr_ = drogon::app().getSharedPlugin<CommonThreadService>();
  if (!threadServicePtr_) {
    LOG_FATAL << "FfmpegTaskService: CommonThreadService not found";
    return;
  }

  size_t threadCount = threadServicePtr_->getThreadCount();

  // 验证：容量不超过线程数
  if (maxConcurrent > threadCount) {
    LOG_WARN << "FfmpegTaskService: maxConcurrentTasks (" << maxConcurrent
             << ") exceeds thread pool size (" << threadCount
             << "), clamping to " << threadCount;
    maxConcurrent = threadCount;
  }

  // 创建任务通道
  channel_ = std::make_unique<FfAsyncChannel>(maxConcurrent, maxWaiting,
                                              threadServicePtr_);

  LOG_INFO << "FfmpegTaskService initialized: "
           << "maxConcurrent=" << maxConcurrent << ", maxWaiting=" << maxWaiting
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

  // 获取 ConverterService
  auto converterService = drogon::app().getSharedPlugin<ConverterService>();
  if (!converterService) {
    LOG_ERROR
        << "FfmpegTaskService::ConvertMp4Task: 获取 ConverterService 失败";
    return;
  }

  // 获取任务信息
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

  // 创建进度回调，用于更新任务状态
  auto progressCallback = [item](const live2mp3::utils::FfmpegPipeInfo &info) {
    if (auto detail = item.lock()) {
      // LOG_DEBUG << "FfmpegTaskService::ConvertMp4Task: 进度回调 FPS: " <<
      // info.fps
      //           << " BITRATE: " << info.bitrate << " TIME: " << info.time;
      detail->setPipeInfo(info);
    }
  };

  for (const auto &inputPath : inputFiles) {
    if (detail->isCancelled()) {
      resultMessage = "任务已取消";
      hasError = true;
      break;
    }

    // 调用 ConverterService 进行转换
    auto outputPath = converterService->convertToAv1Mp4(inputPath, outputDir,
                                                        progressCallback);
    if (outputPath) {
      successCount++;
      detail->setOutputFiles({*outputPath});
      LOG_INFO << "ConvertMp4Task: 转换成功 " << inputPath << " -> "
               << *outputPath;
    } else {
      hasError = true;
      // 转换失败，确保 outputFiles 为空
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

  // 获取 ConverterService
  auto converterService = drogon::app().getSharedPlugin<ConverterService>();
  if (!converterService) {
    LOG_ERROR
        << "FfmpegTaskService::ConvertMp3Task: 获取 ConverterService 失败";
    return;
  }

  // 获取任务信息
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

  // 检查输入文件是否有效（确保不抛出异常）
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

  // 创建进度回调
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

    // 调用 ConverterService 提取 MP3
    auto outputPath = converterService->extractMp3FromVideo(
        inputPath, outputDir, progressCallback);
    if (outputPath) {
      successCount++;
      detail->setOutputFiles({*outputPath});
      LOG_INFO << "ConvertMp3Task: 提取成功 " << inputPath << " -> "
               << *outputPath;
    } else {
      hasError = true;
      // 提取失败，确保 outputFiles 为空
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

  // 获取 MergerService
  auto mergerService = drogon::app().getSharedPlugin<MergerService>();
  if (!mergerService) {
    LOG_ERROR << "FfmpegTaskService::MergeTask: 获取 MergerService 失败";
    return;
  }

  // 获取任务信息
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

  // 创建进度回调
  auto progressCallback = [item](const live2mp3::utils::FfmpegPipeInfo &info) {
    if (auto detail = item.lock()) {
      detail->setPipeInfo(info);
    }
  };

  // 调用 MergerService 进行合并
  auto outputPath =
      mergerService->mergeVideoFiles(inputFiles, outputDir, progressCallback);
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
// FfmpegTaskService 新增接口实现
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

drogon::Task<std::optional<FfmpegTaskResult>> FfmpegTaskService::submitTask(
    FfmpegTaskType type, const std::vector<std::string> &files,
    const std::vector<std::string> &outputFiles,
    std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> callback,
    std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> customFunc) {

  if (!channel_) {
    LOG_ERROR << "FfmpegTaskService::submitTask: channel_ 未初始化";
    co_return std::nullopt;
  }

  // 根据类型获取处理函数
  auto taskFunc = getTaskFunc(type);
  if (!taskFunc) {
    // 如果不是内置类型，使用自定义函数
    if (customFunc) {
      taskFunc = customFunc;
    } else {
      LOG_ERROR
          << "FfmpegTaskService::submitTask: 未知任务类型且未提供自定义函数";
      co_return std::nullopt;
    }
  }

  // 构造任务输入
  FfmpegTaskInput input;
  input.type = type;
  input.files = files;
  input.outputFiles = outputFiles;
  input.func = taskFunc;
  input.callback = callback;

  // 通过 channel 发送任务并等待完成
  auto result = co_await channel_->send(std::move(input));
  co_return result;
}

void FfmpegTaskService::submitTaskAsync(
    FfmpegTaskType type, const std::vector<std::string> &files,
    const std::vector<std::string> &outputFiles,
    std::function<void(std::optional<std::string>)> resultCallback,
    std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> callback,
    std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> customFunc) {

  if (!channel_) {
    LOG_ERROR << "FfmpegTaskService::submitTaskAsync: channel_ 未初始化";
    if (resultCallback) {
      resultCallback(std::nullopt);
    }
    return;
  }

  // 根据类型获取处理函数
  auto taskFunc = getTaskFunc(type);
  if (!taskFunc) {
    if (customFunc) {
      taskFunc = customFunc;
    } else {
      LOG_ERROR << "FfmpegTaskService::submitTaskAsync: "
                   "未知任务类型且未提供自定义函数";
      if (resultCallback) {
        resultCallback(std::nullopt);
      }
      return;
    }
  }

  // 构造任务输入
  FfmpegTaskInput input;
  input.type = type;
  input.files = files;
  input.outputFiles = outputFiles;
  input.func = taskFunc;
  input.callback = callback;

  // 通过 channel 异步发送任务
  channel_->sendAsync(std::move(input), resultCallback);
}

std::vector<FfmpegTaskProcess> FfmpegTaskService::getRunningTasks() {
  if (!channel_) {
    return {};
  }
  return channel_->getRunningTasks();
}
