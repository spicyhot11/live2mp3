#include "CommonThreadService.h"
#include <drogon/drogon.h>

void CommonThreadService::initAndStart(const Json::Value &config) {
  // 从配置读取线程数量
  if (config.isMember("threadCount") && config["threadCount"].isInt()) {
    threadCount_ = static_cast<size_t>(config["threadCount"].asInt());
  }

  // 从配置读取线程池名称
  if (config.isMember("name") && config["name"].isString()) {
    name_ = config["name"].asString();
  }

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