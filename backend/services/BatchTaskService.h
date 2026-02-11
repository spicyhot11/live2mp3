#pragma once

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief 批次信息结构体
 */
struct BatchInfo {
  int id;
  std::string streamer;
  std::string
      status; // encoding / merging / extracting_mp3 / completed / failed
  std::string output_dir;
  std::string tmp_dir;
  std::string final_mp4_path;
  std::string final_mp3_path;
  int total_files;
  int encoded_count;
  int failed_count;
};

/**
 * @brief 批次文件结构体
 */
struct BatchFile {
  int id;
  int batch_id;
  std::string dir_path;
  std::string filename;
  std::string fingerprint;
  int pending_file_id;
  std::string status; // pending / encoding / encoded / failed
  std::string encoded_path;
  int retry_count;

  /// 获取完整文件路径
  std::string getFilepath() const;
};

void to_json(nlohmann::json &j, const BatchInfo &b);
void to_json(nlohmann::json &j, const BatchFile &f);

/**
 * @brief 批次输入文件
 */
struct BatchInputFile {
  std::string filepath;
  std::string fingerprint;
  int pending_file_id;
};

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
};
