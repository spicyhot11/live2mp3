#include "CommonThreadService.h"
#include "ConfigService.h"
#include <drogon/drogon.h>

void CommonThreadService::initAndStart(const Json::Value &config) {
  // 获取 ConfigService
  configService_ = drogon::app().getSharedPlugin<ConfigService>();
  if (!configService_) {
    LOG_ERROR << "CommonThreadService: ConfigService not found, using defaults";
    // Defaults are already set in members if not overwritten, or we can use
    // hardcoded defaults here
  } else {
    auto appConfig = configService_->getConfig();
    threadCount_ = appConfig.common_thread.threadCount;
    name_ = appConfig.common_thread.name;
    LOG_INFO << "CommonThreadService: Loaded config from ConfigService";
  }

  // 兼容旧的 JSON 配置（如果 ConfigService 没准备好或者作为 fallback）
  // 但既然我们要迁移，主要依靠 ConfigService。
  // 如果 JSON 中明确指定了（可能是旧的 config.json 还未迁完），可以覆盖？
  // 策略：优先使用 ConfigService (TOML)，忽略传进来的 config (JSON)，除非 TOML
  // 没加载成功？ 实际上 ConfigService 应该在 CommonThreadService 之前初始化。

  // 创建 trantor 线程池
  threadPool_ =
      std::make_unique<trantor::ConcurrentTaskQueue>(threadCount_, name_);

  LOG_INFO << "CommonThreadService initialized with " << threadCount_
           << " threads, name: " << name_;
}

void CommonThreadService::shutdown() {
  if (threadPool_) {
    threadPool_->stop();
    threadPool_.reset();
  }
  LOG_INFO << "CommonThreadService shutdown complete";
}

void CommonThreadService::runTask(const std::function<void()> &task) {
  if (threadPool_) {
    threadPool_->runTaskInQueue(task);
  }
}

void CommonThreadService::runTask(std::function<void()> &&task) {
  if (threadPool_) {
    threadPool_->runTaskInQueue(std::move(task));
  }
}

size_t CommonThreadService::getTaskCount() {
  if (threadPool_) {
    return threadPool_->getTaskCount();
  }
  return 0;
}

std::string CommonThreadService::getName() const {
  if (threadPool_) {
    return threadPool_->getName();
  }
  return name_;
}

size_t CommonThreadService::getThreadCount() const { return threadCount_; }

void CommonThreadService::runTask(std::weak_ptr<ThreadTaskInterface> task) {
  if (threadPool_) {
    threadPool_->runTaskInQueue([task]() {
      if (auto sp = task.lock()) {
        sp->run();
      }
    });
  }
}

std::future<void>
CommonThreadService::runTaskAsync(std::function<void()> task) {
  auto promise = std::make_shared<std::promise<void>>();
  auto future = promise->get_future();

  if (threadPool_) {
    threadPool_->runTaskInQueue([promise, task = std::move(task)]() {
      try {
        task();
        // 关键修改：在EventLoop中设置promise，确保协程在EventLoop线程恢复
        drogon::app().getLoop()->queueInLoop(
            [promise]() { promise->set_value(); });
      } catch (...) {
        // 异常也在EventLoop中设置
        drogon::app().getLoop()->queueInLoop(
            [promise, ex = std::current_exception()]() {
              promise->set_exception(ex);
            });
      }
    });
  } else {
    promise->set_value(); // 如果没有线程池，直接完成
  }

  return future;
}