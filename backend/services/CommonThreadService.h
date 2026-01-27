#pragma once

#include <drogon/plugins/Plugin.h>
#include <functional>
#include <memory>
#include <string>
#include <trantor/utils/ConcurrentTaskQueue.h>


class ThreadTaskInterface {
public:
    ThreadTaskInterface() = default;
    virtual ~ThreadTaskInterface() = default;
    virtual void run() = 0;
};


/**
 * @brief CommonThreadService
 * 包装 trantor::ConcurrentTaskQueue 为 Drogon 单例插件
 * 提供通用任务线程池功能
 */
class CommonThreadService : public drogon::Plugin<CommonThreadService> {
public:
  CommonThreadService() = default;
  ~CommonThreadService() override = default;

  /**
   * @brief 初始化线程池
   * @param config JSON 配置，可包含 "threadCount" 和 "name" 字段
   */
  void initAndStart(const Json::Value &config) override;

  /**
   * @brief 关闭线程池
   */
  void shutdown() override;

  /**
   * @brief 在线程池中运行任务
   * @param task 要执行的任务函数
   */
  void runTask(const std::function<void()> &task);

  /**
   * @brief 在线程池中运行任务（移动语义版本）
   * @param task 要执行的任务函数
   */
  void runTask(std::function<void()> &&task);

  /**
   * @brief 在线程池中运行任务
   * @param task 要执行的任务
   */
  void runTask(std::weak_ptr<ThreadTaskInterface> task);

  /**
   * @brief 获取当前待执行的任务数量
   * @return 任务数量
   */
  size_t getTaskCount();

  /**
   * @brief 获取线程池名称
   * @return 线程池名称
   */
  std::string getName() const;

private:
  std::unique_ptr<trantor::ConcurrentTaskQueue> threadPool_; ///< 底层线程池
  std::string name_{"CommonThreadPool"};                     ///< 线程池名称
  size_t threadCount_{4};                                    ///< 线程数量
};