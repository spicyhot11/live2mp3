#include "BatchTaskService.h"
#include "DatabaseService.h"
#include <filesystem>
#include <sqlite3.h>

namespace fs = std::filesystem;

std::string BatchFile::getFilepath() const {
  if (dir_path.empty())
    return filename;
  if (dir_path.back() == '/')
    return dir_path + filename;
  return dir_path + "/" + filename;
}

void to_json(nlohmann::json &j, const BatchInfo &b) {
  j = nlohmann::json{{"id", b.id},
                     {"streamer", b.streamer},
                     {"status", b.status},
                     {"output_dir", b.output_dir},
                     {"tmp_dir", b.tmp_dir},
                     {"final_mp4_path", b.final_mp4_path},
                     {"final_mp3_path", b.final_mp3_path},
                     {"total_files", b.total_files},
                     {"encoded_count", b.encoded_count},
                     {"failed_count", b.failed_count}};
}

void to_json(nlohmann::json &j, const BatchFile &f) {
  j = nlohmann::json{{"id", f.id},
                     {"batch_id", f.batch_id},
                     {"dir_path", f.dir_path},
                     {"filename", f.filename},
                     {"filepath", f.getFilepath()},
                     {"fingerprint", f.fingerprint},
                     {"pending_file_id", f.pending_file_id},
                     {"status", f.status},
                     {"encoded_path", f.encoded_path},
                     {"retry_count", f.retry_count}};
}

// 辅助：从完整路径拆分
static void splitPath(const std::string &filepath, std::string &dir_path,
                      std::string &filename) {
  fs::path p(filepath);
  dir_path = p.parent_path().string();
  filename = p.filename().string();
}

void BatchTaskService::initAndStart(const Json::Value &config) {
  LOG_INFO << "BatchTaskService initialized";
}

void BatchTaskService::shutdown() {}

int BatchTaskService::createBatch(const std::string &streamer,
                                  const std::string &outputDir,
                                  const std::string &tmpDir,
                                  const std::vector<BatchInputFile> &files) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[createBatch] Database not available";
    return -1;
  }

  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    LOG_ERROR << "[createBatch] Failed to begin transaction: "
              << sqlite3_errmsg(db);
    return -1;
  }

  // 1. 插入批次记录
  std::string batchSql =
      "INSERT INTO task_batches (streamer, status, output_dir, tmp_dir, "
      "total_files) VALUES (?, 'encoding', ?, ?, ?)";
  sqlite3_stmt *batchStmt;

  if (sqlite3_prepare_v2(db, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[createBatch] Failed to prepare batch insert: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return -1;
  }

  sqlite3_bind_text(batchStmt, 1, streamer.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(batchStmt, 2, outputDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(batchStmt, 3, tmpDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(batchStmt, 4, static_cast<int>(files.size()));

  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    LOG_ERROR << "[createBatch] Failed to insert batch: " << sqlite3_errmsg(db);
    sqlite3_finalize(batchStmt);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return -1;
  }
  sqlite3_finalize(batchStmt);

  int batchId = static_cast<int>(sqlite3_last_insert_rowid(db));

  // 2. 插入批次文件记录
  std::string fileSql =
      "INSERT INTO task_batch_files (batch_id, dir_path, filename, "
      "fingerprint, pending_file_id, status) VALUES (?, ?, ?, ?, ?, 'pending')";
  sqlite3_stmt *fileStmt;

  if (sqlite3_prepare_v2(db, fileSql.c_str(), -1, &fileStmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[createBatch] Failed to prepare file insert: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return -1;
  }

  for (const auto &file : files) {
    std::string dirPath, fname;
    splitPath(file.filepath, dirPath, fname);

    sqlite3_reset(fileStmt);
    sqlite3_bind_int(fileStmt, 1, batchId);
    sqlite3_bind_text(fileStmt, 2, dirPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(fileStmt, 3, fname.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(fileStmt, 4, file.fingerprint.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(fileStmt, 5, file.pending_file_id);

    if (sqlite3_step(fileStmt) != SQLITE_DONE) {
      LOG_ERROR << "[createBatch] Failed to insert file: " << file.filepath
                << " error: " << sqlite3_errmsg(db);
      sqlite3_finalize(fileStmt);
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      return -1;
    }
  }
  sqlite3_finalize(fileStmt);

  // 3. 提交事务
  if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    LOG_ERROR << "[createBatch] Failed to commit: " << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return -1;
  }

  LOG_INFO << "[createBatch] Created batch id=" << batchId
           << " streamer=" << streamer << " files=" << files.size();
  return batchId;
}

bool BatchTaskService::markFileEncoding(int batchId,
                                        const std::string &filepath) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE task_batch_files SET status = 'encoding', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE batch_id = ? AND dir_path = ? AND filename = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markFileEncoding] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }

  sqlite3_bind_int(stmt, 1, batchId);
  sqlite3_bind_text(stmt, 2, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, fname.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool BatchTaskService::markFileEncoded(int batchId, const std::string &filepath,
                                       const std::string &encodedPath,
                                       const std::string &fingerprint) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    LOG_ERROR << "[markFileEncoded] Failed to begin transaction";
    return false;
  }

  // 更新文件状态
  std::string fileSql =
      "UPDATE task_batch_files SET status = 'encoded', encoded_path = ?, "
      "fingerprint = ?, updated_at = datetime('now', 'localtime') "
      "WHERE batch_id = ? AND dir_path = ? AND filename = ?";
  sqlite3_stmt *fileStmt;

  if (sqlite3_prepare_v2(db, fileSql.c_str(), -1, &fileStmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markFileEncoded] Failed to prepare file update: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  sqlite3_bind_text(fileStmt, 1, encodedPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(fileStmt, 2, fingerprint.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(fileStmt, 3, batchId);
  sqlite3_bind_text(fileStmt, 4, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(fileStmt, 5, fname.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(fileStmt) != SQLITE_DONE) {
    LOG_ERROR << "[markFileEncoded] Failed to update file: "
              << sqlite3_errmsg(db);
    sqlite3_finalize(fileStmt);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }
  sqlite3_finalize(fileStmt);

  // 递增批次 encoded_count
  std::string batchSql =
      "UPDATE task_batches SET encoded_count = encoded_count + 1, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *batchStmt;

  if (sqlite3_prepare_v2(db, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[markFileEncoded] Failed to prepare batch update: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  sqlite3_bind_int(batchStmt, 1, batchId);
  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    LOG_ERROR << "[markFileEncoded] Failed to update batch: "
              << sqlite3_errmsg(db);
    sqlite3_finalize(batchStmt);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }
  sqlite3_finalize(batchStmt);

  if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    LOG_ERROR << "[markFileEncoded] Failed to commit: " << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  return true;
}

bool BatchTaskService::markFileFailed(int batchId,
                                      const std::string &filepath) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    return false;
  }

  // 更新文件状态
  std::string fileSql = "UPDATE task_batch_files SET status = 'failed', "
                        "updated_at = datetime('now', 'localtime') "
                        "WHERE batch_id = ? AND dir_path = ? AND filename = ?";
  sqlite3_stmt *fileStmt;

  if (sqlite3_prepare_v2(db, fileSql.c_str(), -1, &fileStmt, 0) != SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  sqlite3_bind_int(fileStmt, 1, batchId);
  sqlite3_bind_text(fileStmt, 2, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(fileStmt, 3, fname.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(fileStmt) != SQLITE_DONE) {
    sqlite3_finalize(fileStmt);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }
  sqlite3_finalize(fileStmt);

  // 递增批次 failed_count
  std::string batchSql =
      "UPDATE task_batches SET failed_count = failed_count + 1, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *batchStmt;

  if (sqlite3_prepare_v2(db, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  sqlite3_bind_int(batchStmt, 1, batchId);
  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    sqlite3_finalize(batchStmt);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }
  sqlite3_finalize(batchStmt);

  if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  return true;
}

std::vector<BatchFile> BatchTaskService::getBatchFiles(int batchId) {
  std::vector<BatchFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql =
      "SELECT id, batch_id, dir_path, filename, fingerprint, pending_file_id, "
      "status, encoded_path, retry_count FROM task_batch_files "
      "WHERE batch_id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[getBatchFiles] Failed to prepare: " << sqlite3_errmsg(db);
    return files;
  }

  sqlite3_bind_int(stmt, 1, batchId);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    BatchFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.batch_id = sqlite3_column_int(stmt, 1);
    f.dir_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    f.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    auto fpText = sqlite3_column_text(stmt, 4);
    f.fingerprint = fpText ? reinterpret_cast<const char *>(fpText) : "";
    f.pending_file_id = sqlite3_column_int(stmt, 5);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    auto encodedText = sqlite3_column_text(stmt, 7);
    f.encoded_path =
        encodedText ? reinterpret_cast<const char *>(encodedText) : "";
    f.retry_count = sqlite3_column_int(stmt, 8);
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<std::string> BatchTaskService::getEncodedPaths(int batchId) {
  std::vector<std::string> paths;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return paths;

  std::string sql = "SELECT encoded_path FROM task_batch_files "
                    "WHERE batch_id = ? AND status = 'encoded' "
                    "ORDER BY id";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return paths;
  }

  sqlite3_bind_int(stmt, 1, batchId);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto text = sqlite3_column_text(stmt, 0);
    if (text) {
      paths.push_back(reinterpret_cast<const char *>(text));
    }
  }

  sqlite3_finalize(stmt);
  return paths;
}

bool BatchTaskService::isBatchEncodingComplete(int batchId) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  // 检查是否还有 pending 或 encoding 状态的文件
  std::string sql = "SELECT COUNT(*) FROM task_batch_files "
                    "WHERE batch_id = ? AND status IN ('pending', 'encoding')";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(stmt, 1, batchId);

  bool complete = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int remaining = sqlite3_column_int(stmt, 0);
    complete = (remaining == 0);
  }

  sqlite3_finalize(stmt);
  return complete;
}

std::vector<int>
BatchTaskService::getEncodingCompleteBatchIds(int minAgeSeconds) {
  std::vector<int> ids;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return ids;

  // 查找 status='encoding' 且所有文件都不在 pending/encoding 状态的批次
  // 且最后一个文件更新时间距今超过 minAgeSeconds 秒
  std::string sql =
      "SELECT b.id FROM task_batches b "
      "WHERE b.status = 'encoding' "
      "AND NOT EXISTS ("
      "  SELECT 1 FROM task_batch_files f "
      "  WHERE f.batch_id = b.id AND f.status IN ('pending', 'encoding')"
      ") "
      "AND (cast(strftime('%s', 'now', 'localtime') as integer) - "
      "     cast(strftime('%s', ("
      "       SELECT MAX(f2.updated_at) FROM task_batch_files f2 "
      "       WHERE f2.batch_id = b.id"
      "     )) as integer)) > ? "
      "ORDER BY b.id";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[getEncodingCompleteBatchIds] Failed to prepare: "
              << sqlite3_errmsg(db);
    return ids;
  }

  sqlite3_bind_int(stmt, 1, minAgeSeconds);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ids.push_back(sqlite3_column_int(stmt, 0));
  }

  sqlite3_finalize(stmt);
  return ids;
}

bool BatchTaskService::updateBatchStatus(int batchId,
                                         const std::string &status) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string sql = "UPDATE task_batches SET status = ?, "
                    "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, batchId);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool BatchTaskService::setBatchFinalPaths(int batchId,
                                          const std::string &mp4Path,
                                          const std::string &mp3Path) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string sql =
      "UPDATE task_batches SET final_mp4_path = ?, final_mp3_path = ?, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, mp4Path.c_str(), -1, SQLITE_STATIC);
  if (mp3Path.empty()) {
    sqlite3_bind_null(stmt, 2);
  } else {
    sqlite3_bind_text(stmt, 2, mp3Path.c_str(), -1, SQLITE_STATIC);
  }
  sqlite3_bind_int(stmt, 3, batchId);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

std::optional<BatchInfo> BatchTaskService::getBatch(int batchId) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return std::nullopt;

  std::string sql =
      "SELECT id, streamer, status, output_dir, tmp_dir, final_mp4_path, "
      "final_mp3_path, total_files, encoded_count, failed_count "
      "FROM task_batches WHERE id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return std::nullopt;
  }

  sqlite3_bind_int(stmt, 1, batchId);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    BatchInfo b;
    b.id = sqlite3_column_int(stmt, 0);
    b.streamer = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    b.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    auto od = sqlite3_column_text(stmt, 3);
    b.output_dir = od ? reinterpret_cast<const char *>(od) : "";
    auto td = sqlite3_column_text(stmt, 4);
    b.tmp_dir = td ? reinterpret_cast<const char *>(td) : "";
    auto mp4 = sqlite3_column_text(stmt, 5);
    b.final_mp4_path = mp4 ? reinterpret_cast<const char *>(mp4) : "";
    auto mp3 = sqlite3_column_text(stmt, 6);
    b.final_mp3_path = mp3 ? reinterpret_cast<const char *>(mp3) : "";
    b.total_files = sqlite3_column_int(stmt, 7);
    b.encoded_count = sqlite3_column_int(stmt, 8);
    b.failed_count = sqlite3_column_int(stmt, 9);
    sqlite3_finalize(stmt);
    return b;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

std::vector<BatchInfo> BatchTaskService::getIncompleteBatches() {
  std::vector<BatchInfo> batches;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return batches;

  std::string sql =
      "SELECT id, streamer, status, output_dir, tmp_dir, final_mp4_path, "
      "final_mp3_path, total_files, encoded_count, failed_count "
      "FROM task_batches WHERE status NOT IN ('completed', 'failed') "
      "ORDER BY id";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return batches;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    BatchInfo b;
    b.id = sqlite3_column_int(stmt, 0);
    b.streamer = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    b.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    auto od = sqlite3_column_text(stmt, 3);
    b.output_dir = od ? reinterpret_cast<const char *>(od) : "";
    auto td = sqlite3_column_text(stmt, 4);
    b.tmp_dir = td ? reinterpret_cast<const char *>(td) : "";
    auto mp4 = sqlite3_column_text(stmt, 5);
    b.final_mp4_path = mp4 ? reinterpret_cast<const char *>(mp4) : "";
    auto mp3 = sqlite3_column_text(stmt, 6);
    b.final_mp3_path = mp3 ? reinterpret_cast<const char *>(mp3) : "";
    b.total_files = sqlite3_column_int(stmt, 7);
    b.encoded_count = sqlite3_column_int(stmt, 8);
    b.failed_count = sqlite3_column_int(stmt, 9);
    batches.push_back(b);
  }

  sqlite3_finalize(stmt);
  return batches;
}
