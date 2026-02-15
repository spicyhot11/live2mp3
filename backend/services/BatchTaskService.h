#pragma once

#include "../repos/BatchTaskRepo.h"
#include "models/BatchModels.h"
#include "services/PendingFileService.h"
#include <drogon/drogon.h>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief 批次任务管理服务
 *
 * 管理 task_batches 和 task_batch_files 表的CRUD操作。
 * 负责批次创建、文件状态跟踪、批次状态流转。
 */
class BatchTaskService : public drogon::Plugin<BatchTaskService> {
public:
  BatchTaskService() = default;
  ~BatchTaskService() = default;
  BatchTaskService(const BatchTaskService &) = delete;
  BatchTaskService &operator=(const BatchTaskService &) = delete;

  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

public:
  /**
   * @brief 启动时恢复被中断的任务
   *
   * 将 encoding 状态的文件回滚到 pending，
   * 将 merging/extracting_mp3 状态的批次回滚到 encoding。
   */
  void recoverInterruptedTasks();
  /**
   * @brief 创建一个新批次及关联文件
   * @return batchId, -1 表示失败
   */
  int createBatch(const std::string &streamer, const std::string &outputDir,
                  const std::string &tmpDir,
                  const std::vector<BatchInputFile> &files);

  /**
   * @brief 标记文件开始编码
   */
  bool markFileEncoding(int batchId, const std::string &filepath);

  /**
   * @brief 标记文件编码完成
   */
  bool markFileEncoded(int batchId, const std::string &filepath,
                       const std::string &encodedPath,
                       const std::string &fingerprint);

  /**
   * @brief 标记文件编码失败
   */
  bool markFileFailed(int batchId, const std::string &filepath);

  /**
   * @brief 获取批次的所有文件
   */
  std::vector<BatchFile> getBatchFiles(int batchId);

  /**
   * @brief 获取批次中已编码的文件路径列表
   */
  std::vector<std::string> getEncodedPaths(int batchId);

  /**
   * @brief 检查批次内所有文件是否编码完成（成功或失败）
   */
  bool isBatchEncodingComplete(int batchId);

  /**
   * @brief 获取所有编码完成的批次 ID（status='encoding' 且所有文件非
   * pending/encoding）
   */
  std::vector<int> getEncodingCompleteBatchIds(int minAgeSeconds = 0);

  /**
   * @brief 更新批次状态
   */
  bool updateBatchStatus(int batchId, const std::string &status);

  /**
   * @brief 设置批次最终输出路径
   */
  bool setBatchFinalPaths(int batchId, const std::string &mp4Path,
                          const std::string &mp3Path);

  /**
   * @brief 获取批次信息
   */
  std::optional<BatchInfo> getBatch(int batchId);

  /**
   * @brief 获取所有未完成的批次（启动恢复用）
   */
  std::vector<BatchInfo> getIncompleteBatches();

  /**
   * @brief 获取指定主播的所有 encoding 状态批次
   */
  std::vector<BatchInfo>
  getEncodingBatchesByStreamer(const std::string &streamer);

  /**
   * @brief 获取批次中所有文件的时间点列表（从 filename 解析）
   */
  std::vector<std::chrono::system_clock::time_point>
  getBatchFileTimes(int batchId);

  /**
   * @brief 向已有批次追加文件
   */
  bool addFilesToBatch(int batchId, const std::vector<BatchInputFile> &files);

  /**
   * @brief 检查 PendingFile 是否已分配到任务中
   */
  bool isInBatch(int pendingFileId);

  /**
   * @brief 分组+合并主入口
   *
   * 对新 stable 文件按主播分组、按时间窗口分批，
   * 然后与已有 encoding 批次合并（可拆分）。
   *
   * @param stableFiles 新的稳定文件列表
   * @param mergeWindowSeconds 合并时间窗口（秒）
   * @return 批次分配结果列表
   */
  std::vector<BatchAssignment>
  groupAndAssignBatches(const std::vector<StableFile> &stableFiles,
                        int mergeWindowSeconds);

  /**
   * @brief 扫描所有不完整批次并重新提交处理任务
   */
  void reSubmitInterruptedTasks();

  /**
   * @brief 重新提交指定批次的转码任务
   */
  void processBatch(int batchId);

private:
  BatchTaskRepo repo_;
};
