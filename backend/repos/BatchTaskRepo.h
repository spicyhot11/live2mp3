#pragma once

#include "../models/BatchModels.h"
#include "../services/DatabaseService.h"
#include <optional>
#include <string>
#include <vector>

/**
 * @brief task_batches / task_batch_files 表的数据访问层
 *
 * 纯数据库操作类，不包含业务逻辑。
 */
class BatchTaskRepo {
public:
  // ============ 行映射 ============

  static BatchInfo readBatchRow(sqlite3_stmt *stmt);
  static BatchFile readBatchFileRow(sqlite3_stmt *stmt);
  static const char *batchSelectCols();
  static const char *batchFileSelectCols();

  // ============ 批次 CRUD ============

  std::optional<BatchInfo> findBatch(int batchId);
  std::vector<BatchInfo> findIncompleteBatches();
  std::vector<BatchInfo> findEncodingByStreamer(const std::string &streamer);
  bool updateBatchStatus(int batchId, const std::string &status);
  bool setBatchFinalPaths(int batchId, const std::string &mp4Path,
                          const std::string &mp3Path);

  // ============ 批次文件 CRUD ============

  std::vector<BatchFile> findBatchFiles(int batchId);
  std::vector<std::string> findEncodedPaths(int batchId);
  std::vector<std::string> findBatchFilenames(int batchId);
  bool updateBatchFileStatus(int batchId, const std::string &filepath,
                             const std::string &status);
  int countPendingOrEncoding(int batchId);
  std::vector<int> findCompleteBatchIds(int minAgeSeconds);

  // ============ 事务性操作 ============

  int createBatchWithFiles(const std::string &streamer,
                           const std::string &outputDir,
                           const std::string &tmpDir,
                           const std::vector<BatchInputFile> &files);
  bool addFilesToBatch(int batchId, const std::vector<BatchInputFile> &files);
  bool markFileEncoded(int batchId, const std::string &filepath,
                       const std::string &encodedPath,
                       const std::string &fingerprint);
  bool deleteBatchFileAndIncrFailed(int batchId, const std::string &filepath);

  // ============ 恢复操作 ============

  int rollbackEncodingFiles();
  int rollbackBatchStatus();

  bool isInBatch(int pendingFileId);

private:
  DatabaseService &db();
  static void splitPath(const std::string &filepath, std::string &dirPath,
                        std::string &filename);
};
