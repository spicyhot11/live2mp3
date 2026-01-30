#include "FfmpegTaskService.h"
#include "../utils/CoroUtils.hpp"
#include "ConverterService.h"
#include "MergerService.h"
#include <algorithm>
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
  return result;
}

void FfmpegTaskProcDetail::cancel() {
  cancelled_ = true;
  // TODO: 终止FFmpeg进程
  // 需要在FfmpegPipeInfo中添加进程PID和terminate方法
  if (auto pipe = pipeInfo.get()) {
    // pipe->terminate(); // 将来实现
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
  // TODO: 补充关闭逻辑
  // 可能需要取消等待中的任务
}

drogon::Task<std::optional<std::string>>
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

  // 6. 从任务映射表中移除
  {
    std::lock_guard<std::mutex> lock(mutex_);
    taskMap_.erase(taskId);
  }

  LOG_DEBUG << "FfmpegTaskService: task completed, id: " << taskId;
  co_return taskId; // 返回成功的optional
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
          callback(result);
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

  // 获取CommonThreadService的线程数
  auto threadService = drogon::app().getPlugin<CommonThreadService>();
  size_t threadCount = threadService->getThreadCount();

  // 验证：容量不超过线程数
  if (maxConcurrent > threadCount) {
    LOG_WARN << "FfmpegTaskService: maxConcurrentTasks (" << maxConcurrent
             << ") exceeds thread pool size (" << threadCount
             << "), clamping to " << threadCount;
    maxConcurrent = threadCount;
  }

  LOG_INFO << "FfmpegTaskService initialized: "
           << "maxConcurrent=" << maxConcurrent << ", maxWaiting=" << maxWaiting
           << ", threadPoolSize=" << threadCount;
}

void FfmpegTaskService::shutdown() { LOG_INFO << "FfmpegTaskService shutdown"; }

void FfmpegTaskService::ConvertMp4Task(FfmpegTaskInput item) {
  // TODO: 实现 MP4 转换任务
}
