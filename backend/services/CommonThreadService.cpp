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