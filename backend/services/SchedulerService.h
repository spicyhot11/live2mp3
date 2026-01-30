#pragma once

#include "ConfigService.h"
#include "ConverterService.h"
#include "MergerService.h"
#include "PendingFileService.h"
#include "ScannerService.h"
#include <atomic>
#include <drogon/plugins/Plugin.h>
#include <mutex>
#include <string>

/**
 * @brief 任务调度服务类
 *
 * 协调其他服务（扫描、转换、合并）的工作流程。
 * 周期性执行任务，维护整个文件处理的生命周期。
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

  bool isRunning() const { return running_; }
  std::string getCurrentFile();
  std::string getCurrentPhase();

  // 获取详细状态，包括当前阶段和处理中的文件
  nlohmann::json getDetailedStatus();

private:
  void runTask(bool immediate = false);

  // 阶段 1: 扫描文件并更新 pending_files 中的 MD5
  void runStabilityScan();

  // 阶段 2: 将稳定状态的文件转换为 AV1 MP4
  void runConversion();

  // 阶段 3: 处理已暂存的文件 (合并并移动到输出目录)
  void runMergeAndOutput(bool immediate = false);

  // 分组辅助结构
  struct StagedFile {
    PendingFile pf;
    std::chrono::system_clock::time_point time;
  };

  // 处理确认的一批文件
  void processBatch(const std::vector<StagedFile> &batch,
                    const AppConfig &config);

  void setPhase(const std::string &phase);

  std::shared_ptr<ConfigService> configServicePtr;
  std::shared_ptr<MergerService> mergerServicePtr;
  std::shared_ptr<ScannerService> scannerServicePtr;
  std::shared_ptr<ConverterService> converterServicePtr;
  std::shared_ptr<PendingFileService> pendingFileServicePtr;

  std::atomic<bool> running_{false};
  std::string currentFile_;
  std::string currentPhase_;
  std::mutex mutex_;
};
