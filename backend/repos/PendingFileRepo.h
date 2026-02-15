#pragma once

#include "../models/PendingFile.h"
#include "../services/DatabaseService.h"
#include <optional>
#include <string>
#include <vector>

/**
 * @brief pending_files 表的数据访问层
 *
 * 纯数据库操作类，不包含业务逻辑。
 * 所有方法使用 DatabaseService 的通用方法。
 */
class PendingFileRepo {
public:
  // ============ 行映射 ============

  /// 从 SQLite 行读取 PendingFile 的标准映射器
  static PendingFile readRow(sqlite3_stmt *stmt);

  /// 标准 SELECT 列
  static const char *selectCols();

  // ============ 查询方法 ============

  std::optional<PendingFile> findByPath(const std::string &filepath);
  std::vector<PendingFile> findAll();
  std::vector<PendingFile> findByStatus(const std::string &status);
  std::vector<PendingFile> findStableWithMinCount(int minCount);
  std::vector<PendingFile> findStagedOlderThan(int seconds);
  bool existsByFingerprint(const std::string &fingerprint);

  /// 查询同目录下 filename LIKE pattern 且指定状态的文件
  std::vector<PendingFile> findByDirAndStemLike(const std::string &dir,
                                                const std::string &pattern,
                                                const std::string &status);

  // ============ 插入/更新方法 ============

  bool insert(const std::string &dirPath, const std::string &filename,
              const std::string &fingerprint);
  bool incrementStableCount(const std::string &dirPath,
                            const std::string &filename);
  bool resetFingerprint(const std::string &dirPath, const std::string &filename,
                        const std::string &fingerprint);

  /// 通用状态更新
  bool updateStatus(const std::string &filepath, const std::string &status);

  /// 更新状态并设置 start_time / end_time
  bool updateStatusWithStartEnd(const std::string &filepath,
                                const std::string &status,
                                const std::string &startTime,
                                const std::string &endTime);

  /// 更新状态并设置 temp_mp4_path
  bool updateStatusWithTempPath(const std::string &filepath,
                                const std::string &status,
                                const std::string &tempPath);

  bool updateStatusById(int id, const std::string &status);

  // ============ 删除方法 ============

  bool deleteByPath(const std::string &filepath);
  bool deleteById(int id);

  // ============ 事务性操作 ============

  /// 原子性地获取 stable 文件并标记为 processing
  std::vector<PendingFile> claimStableFiles();

  /// 批量标记文件为 processing（事务）
  bool markProcessingBatch(const std::vector<std::string> &filepaths);

  /// 批量回滚文件状态到 stable（事务）
  bool rollbackToStable(const std::vector<std::string> &filepaths);

  // ============ 恢复相关 ============

  struct ProcessingRecord {
    int id;
    std::string dir_path;
    std::string filename;
    std::string getFilepath() const;
  };

  std::vector<ProcessingRecord> findProcessingRecords();

private:
  DatabaseService &db();
  static void splitPath(const std::string &filepath, std::string &dirPath,
                        std::string &filename);
};
