#pragma once

#include "CommonThreadService.h"
#include "ConfigService.h"
#include "ConverterService.h"
#include "FfmpegTaskService.h"
#include "MergerService.h"
#include "PendingFileService.h"
#include "ScannerService.h"
#include <atomic>
#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <mutex>
#include <string>

/**
 * @brief 任务调度服务类
 *
 * 协调其他服务（扫描、转换、合并）的工作流程。
 * 周期性执行任务，维护整个文件处理的生命周期。
 * 使用协程进行异步调度，通过 FfmpegTaskService 发送任务。
 */
class SchedulerService : public drogon::Plugin<SchedulerService> {
public:
  SchedulerService() = default;
  ~SchedulerService() = default;
  SchedulerService(const SchedulerService &) = delete;
  SchedulerService &operator=(const SchedulerService &) = delete;

  // 初始化并启动插件
  void initAndStart(const Json::Value &config) override;
  // 关闭插件
  void shutdown() override;

  // 启动定期任务
  void start();

  // 立即触发一次任务运行 (手动触发) - 使用所有已暂存的文件
  void triggerNow();

  /// @brief 检查扫描阶段是否正在运行
  bool isRunning() const { return scanRunning_; }
  std::string getCurrentFile();
  std::string getCurrentPhase();

  // 获取详细状态，包括当前阶段和处理中的文件
  nlohmann::json getDetailedStatus();

private:
  /**
   * @brief 主任务协程
   * 编排所有阶段的执行
   */
  drogon::Task<void> runTaskAsync(bool immediate = false);

  /**
   * @brief 阶段 1: 扫描文件并更新 pending_files 中的 MD5
   * 这是 CPU 密集型操作，在线程池中执行
   */
  void runStabilityScan();

  /**
   * @brief 阶段 2: 合并稳定文件并编码输出
   * 按主播名分组，批次分配，判断结束条件后执行合并+编码
   * @param immediate 立即模式，跳过等待判断
   */
  drogon::Task<void> runMergeEncodeOutputAsync(bool immediate = false);

  // 分组辅助结构
  struct StableFile {
    PendingFile pf;
    std::chrono::system_clock::time_point time;
  };

  /**
   * @brief 处理确认的一批文件
   * 通过 FfmpegTaskService 发送合并、编码和MP3提取任务
   */
  drogon::Task<void> processBatchAsync(const std::vector<StableFile> &batch,
                                       const AppConfig &config);

  void setPhase(const std::string &phase);

  // 服务指针
  std::shared_ptr<ConfigService> configServicePtr_;
  std::shared_ptr<MergerService> mergerServicePtr_;
  std::shared_ptr<ScannerService> scannerServicePtr_;
  std::shared_ptr<ConverterService> converterServicePtr_;
  std::shared_ptr<PendingFileService> pendingFileServicePtr_;
  std::shared_ptr<FfmpegTaskService> ffmpegTaskServicePtr_;
  std::shared_ptr<CommonThreadService> commonThreadServicePtr_;

  /// @brief 扫描阶段运行标志（防止扫描阶段并发）
  std::atomic<bool> scanRunning_{false};
  std::string currentFile_;
  std::string currentPhase_;
  std::mutex mutex_;
};
