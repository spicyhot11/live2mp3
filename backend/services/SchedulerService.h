#pragma once

#include "BatchTaskService.h"
#include "CommonThreadService.h"
#include "ConfigService.h"
#include "ConverterService.h"
#include "FfmpegTaskService.h"
#include "MergerService.h"
#include "PendingFileService.h"
#include "ScannerService.h"
#include "utils/ThreadSafe.hpp"
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
 * 使用回调驱动模式，不阻塞等待 FFmpeg 任务完成。
 */
class SchedulerService : public drogon::Plugin<SchedulerService> {
public:
  SchedulerService() = default;
  ~SchedulerService() = default;
  SchedulerService(const SchedulerService &) = delete;
  SchedulerService &operator=(const SchedulerService &) = delete;

  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  // 初始化原子配置
  void initAtomicConfig();

  // 启动定期任务
  void start();

  // 立即触发一次任务运行
  void triggerNow();

  bool isRunning() const { return scanRunning_; }
  std::string getCurrentFile();
  std::string getCurrentPhase();

  nlohmann::json getDetailedStatus();

  /**
   * @brief 单文件转码完成回调
   */
  void onFileEncoded(int batchId, const std::string &filepath,
                     const FfmpegTaskResult &result);

  /**
   * @brief 批次内所有文件转码完成
   */
  void onBatchEncodingComplete(int batchId);

  /**
   * @brief 合并完成回调
   */
  void onMergeComplete(int batchId, const FfmpegTaskResult &result);

  /**
   * @brief MP3 提取完成回调
   */
  void onMp3Complete(int batchId, const FfmpegTaskResult &result);

private:
  /**
   * @brief 主任务协程
   * 编排扫描和批次创建阶段（不等待后续阶段）
   */
  drogon::Task<void> runTaskAsync(bool immediate = false);

  /**
   * @brief 阶段 1: 扫描文件并更新 pending_files 中的指纹
   */
  void runStabilityScan();

  /**
   * @brief 阶段 2: 分批创建批次并提交转码任务（不等待）
   */
  void runMergeEncodeOutput(bool immediate = false);

  /**
   * @brief 阶段 3: 轮询数据库，找到编码完成的批次，触发合并
   */
  void checkEncodedBatches();

  /**
   * @brief 将文件移动到输出目录（降级处理）
   */
  std::vector<std::string>
  moveFilesToOutputDir(const std::vector<std::string> &files,
                       const std::string &outputDir);

  /**
   * @brief 批次完成后标记原始文件为 completed
   */
  void markBatchFilesCompleted(int batchId);

  /**
   * @brief 批次失败后回滚原始文件为 stable
   */
  void rollbackBatchFiles(int batchId);

  void setPhase(const std::string &phase);

  // 原子配置变量
  struct AtomicConfig {
    std::atomic<int> scan_interval_seconds{60};
    std::atomic<int> merge_window_seconds{7200};
    std::atomic<int> stop_waiting_seconds{600};
    std::atomic<int> stability_checks{2};
    live2mp3::utils::ThreadSafeString output_root;

    void loadFrom(const AppConfig &config) {
      scan_interval_seconds.store(config.scheduler.scan_interval_seconds);
      merge_window_seconds.store(config.scheduler.merge_window_seconds);
      stop_waiting_seconds.store(config.scheduler.stop_waiting_seconds);
      stability_checks.store(config.scheduler.stability_checks);
      output_root.set(config.output.output_root);
    }

    std::string getOutputRoot() const { return *output_root.get(); }

    AppConfig getAtomicConfig() const {
      AppConfig config;
      config.scheduler.scan_interval_seconds = scan_interval_seconds.load();
      config.scheduler.merge_window_seconds = merge_window_seconds.load();
      config.scheduler.stop_waiting_seconds = stop_waiting_seconds.load();
      config.scheduler.stability_checks = stability_checks.load();
      config.output.output_root = *output_root.get();
      return config;
    }
  };

  // 服务指针
  std::shared_ptr<ConfigService> configServicePtr_;
  std::shared_ptr<MergerService> mergerServicePtr_;
  std::shared_ptr<ScannerService> scannerServicePtr_;
  std::shared_ptr<ConverterService> converterServicePtr_;
  std::shared_ptr<PendingFileService> pendingFileServicePtr_;
  std::shared_ptr<FfmpegTaskService> ffmpegTaskServicePtr_;
  std::shared_ptr<CommonThreadService> commonThreadServicePtr_;
  std::shared_ptr<BatchTaskService> batchTaskServicePtr_;

  std::atomic<bool> scanRunning_{false};
  AtomicConfig atomicConfig_;
  std::string currentFile_;
  std::string currentPhase_;
  std::mutex mutex_;
};
