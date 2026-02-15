#include "BatchTaskRepo.h"
#include <filesystem>
#include <sqlite3.h>

namespace fs = std::filesystem;

// ============ 静态辅助 ============

void BatchTaskRepo::splitPath(const std::string &filepath, std::string &dirPath,
                              std::string &filename) {
  fs::path p(filepath);
  dirPath = p.parent_path().string();
  filename = p.filename().string();
}

const char *BatchTaskRepo::batchSelectCols() {
  return "id, streamer, status, output_dir, tmp_dir, final_mp4_path, "
         "final_mp3_path, total_files, encoded_count, failed_count";
}

const char *BatchTaskRepo::batchFileSelectCols() {
  return "id, batch_id, dir_path, filename, fingerprint, pending_file_id, "
         "status, encoded_path, retry_count";
}

BatchInfo BatchTaskRepo::readBatchRow(sqlite3_stmt *stmt) {
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
  return b;
}

BatchFile BatchTaskRepo::readBatchFileRow(sqlite3_stmt *stmt) {
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
  return f;
}

DatabaseService &BatchTaskRepo::db() { return DatabaseService::getInstance(); }

// ============ 批次 CRUD ============

std::optional<BatchInfo> BatchTaskRepo::findBatch(int batchId) {
  std::string sql = std::string("SELECT ") + batchSelectCols() +
                    " FROM task_batches WHERE id = ?";
  return db().queryOne<BatchInfo>(sql, readBatchRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_int(stmt, 1, batchId);
  });
}

std::vector<BatchInfo> BatchTaskRepo::findIncompleteBatches() {
  std::string sql =
      std::string("SELECT ") + batchSelectCols() +
      " FROM task_batches WHERE status NOT IN ('completed', 'failed') "
      "ORDER BY id";
  return db().queryAll<BatchInfo>(sql, readBatchRow);
}

std::vector<BatchInfo>
BatchTaskRepo::findEncodingByStreamer(const std::string &streamer) {
  std::string sql =
      std::string("SELECT ") + batchSelectCols() +
      " FROM task_batches WHERE streamer = ? AND status = 'encoding' "
      "ORDER BY id";
  return db().queryAll<BatchInfo>(sql, readBatchRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, streamer.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool BatchTaskRepo::updateBatchStatus(int batchId, const std::string &status) {
  std::string sql = "UPDATE task_batches SET status = ?, "
                    "updated_at = datetime('now', 'localtime') WHERE id = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, batchId);
  });
}

bool BatchTaskRepo::setBatchFinalPaths(int batchId, const std::string &mp4Path,
                                       const std::string &mp3Path) {
  std::string sql =
      "UPDATE task_batches SET final_mp4_path = ?, final_mp3_path = ?, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, mp4Path.c_str(), -1, SQLITE_TRANSIENT);
    if (mp3Path.empty()) {
      sqlite3_bind_null(stmt, 2);
    } else {
      sqlite3_bind_text(stmt, 2, mp3Path.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, 3, batchId);
  });
}

// ============ 批次文件 CRUD ============

std::vector<BatchFile> BatchTaskRepo::findBatchFiles(int batchId) {
  std::string sql = std::string("SELECT ") + batchFileSelectCols() +
                    " FROM task_batch_files WHERE batch_id = ?";
  return db().queryAll<BatchFile>(
      sql, readBatchFileRow,
      [&](sqlite3_stmt *stmt) { sqlite3_bind_int(stmt, 1, batchId); });
}

std::vector<std::string> BatchTaskRepo::findEncodedPaths(int batchId) {
  std::string sql = "SELECT encoded_path FROM task_batch_files "
                    "WHERE batch_id = ? AND status = 'encoded' ORDER BY id";
  return db().queryAll<std::string>(
      sql,
      [](sqlite3_stmt *stmt) -> std::string {
        auto text = sqlite3_column_text(stmt, 0);
        return text ? reinterpret_cast<const char *>(text) : "";
      },
      [&](sqlite3_stmt *stmt) { sqlite3_bind_int(stmt, 1, batchId); });
}

std::vector<std::string> BatchTaskRepo::findBatchFilenames(int batchId) {
  std::string sql = "SELECT filename FROM task_batch_files WHERE batch_id = ?";
  return db().queryAll<std::string>(
      sql,
      [](sqlite3_stmt *stmt) -> std::string {
        auto text = sqlite3_column_text(stmt, 0);
        return text ? reinterpret_cast<const char *>(text) : "";
      },
      [&](sqlite3_stmt *stmt) { sqlite3_bind_int(stmt, 1, batchId); });
}

bool BatchTaskRepo::updateBatchFileStatus(int batchId,
                                          const std::string &filepath,
                                          const std::string &status) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);
  std::string sql = "UPDATE task_batch_files SET status = ?, "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE batch_id = ? AND dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, batchId);
    sqlite3_bind_text(stmt, 3, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fname.c_str(), -1, SQLITE_TRANSIENT);
  });
}

int BatchTaskRepo::countPendingOrEncoding(int batchId) {
  std::string sql = "SELECT COUNT(*) FROM task_batch_files "
                    "WHERE batch_id = ? AND status IN ('pending', 'encoding')";
  return db().queryScalar(
      sql, [&](sqlite3_stmt *stmt) { sqlite3_bind_int(stmt, 1, batchId); });
}

std::vector<int> BatchTaskRepo::findCompleteBatchIds(int minAgeSeconds) {
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
  return db().queryAll<int>(
      sql,
      [](sqlite3_stmt *stmt) -> int { return sqlite3_column_int(stmt, 0); },
      [&](sqlite3_stmt *stmt) { sqlite3_bind_int(stmt, 1, minAgeSeconds); });
}

// ============ 事务性操作 ============

int BatchTaskRepo::createBatchWithFiles(
    const std::string &streamer, const std::string &outputDir,
    const std::string &tmpDir, const std::vector<BatchInputFile> &files) {
  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return -1;

  ScopedTransaction txn(rawDb);
  if (!txn.begin())
    return -1;

  // 插入批次记录
  std::string batchSql =
      "INSERT INTO task_batches (streamer, status, output_dir, tmp_dir, "
      "total_files) VALUES (?, 'encoding', ?, ?, ?)";
  sqlite3_stmt *batchStmt;
  if (sqlite3_prepare_v2(rawDb, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[createBatchWithFiles] Failed to prepare batch insert: "
              << sqlite3_errmsg(rawDb);
    return -1;
  }

  sqlite3_bind_text(batchStmt, 1, streamer.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(batchStmt, 2, outputDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(batchStmt, 3, tmpDir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(batchStmt, 4, static_cast<int>(files.size()));

  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    LOG_ERROR << "[createBatchWithFiles] Failed to insert batch: "
              << sqlite3_errmsg(rawDb);
    sqlite3_finalize(batchStmt);
    return -1;
  }
  sqlite3_finalize(batchStmt);

  int batchId = static_cast<int>(sqlite3_last_insert_rowid(rawDb));

  // 插入批次文件记录
  std::string fileSql =
      "INSERT INTO task_batch_files (batch_id, dir_path, filename, "
      "fingerprint, pending_file_id, status) VALUES (?, ?, ?, ?, ?, 'pending')";
  sqlite3_stmt *fileStmt;
  if (sqlite3_prepare_v2(rawDb, fileSql.c_str(), -1, &fileStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[createBatchWithFiles] Failed to prepare file insert: "
              << sqlite3_errmsg(rawDb);
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
      LOG_ERROR << "[createBatchWithFiles] Failed to insert file: "
                << file.filepath << " error: " << sqlite3_errmsg(rawDb);
      sqlite3_finalize(fileStmt);
      return -1;
    }
  }
  sqlite3_finalize(fileStmt);

  if (!txn.commit())
    return -1;

  LOG_INFO << "[createBatchWithFiles] Created batch id=" << batchId
           << " streamer=" << streamer << " files=" << files.size();
  return batchId;
}

bool BatchTaskRepo::addFilesToBatch(int batchId,
                                    const std::vector<BatchInputFile> &files) {
  if (files.empty())
    return true;

  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return false;

  ScopedTransaction txn(rawDb);
  if (!txn.begin())
    return false;

  std::string fileSql =
      "INSERT INTO task_batch_files (batch_id, dir_path, filename, "
      "fingerprint, pending_file_id, status) VALUES (?, ?, ?, ?, ?, 'pending')";
  sqlite3_stmt *fileStmt;
  if (sqlite3_prepare_v2(rawDb, fileSql.c_str(), -1, &fileStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[addFilesToBatch] Failed to prepare: "
              << sqlite3_errmsg(rawDb);
    return false;
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
      LOG_ERROR << "[addFilesToBatch] Failed to insert file: " << file.filepath
                << " error: " << sqlite3_errmsg(rawDb);
      sqlite3_finalize(fileStmt);
      return false;
    }
  }
  sqlite3_finalize(fileStmt);

  // 更新 total_files 计数
  std::string batchSql =
      "UPDATE task_batches SET total_files = total_files + ?, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *batchStmt;
  if (sqlite3_prepare_v2(rawDb, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK)
    return false;

  sqlite3_bind_int(batchStmt, 1, static_cast<int>(files.size()));
  sqlite3_bind_int(batchStmt, 2, batchId);
  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    sqlite3_finalize(batchStmt);
    return false;
  }
  sqlite3_finalize(batchStmt);

  if (!txn.commit())
    return false;

  LOG_INFO << "[addFilesToBatch] Added " << files.size()
           << " files to batch id=" << batchId;
  return true;
}

bool BatchTaskRepo::markFileEncoded(int batchId, const std::string &filepath,
                                    const std::string &encodedPath,
                                    const std::string &fingerprint) {
  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return false;

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  ScopedTransaction txn(rawDb);
  if (!txn.begin())
    return false;

  // 更新文件状态
  std::string fileSql =
      "UPDATE task_batch_files SET status = 'encoded', encoded_path = ?, "
      "fingerprint = ?, updated_at = datetime('now', 'localtime') "
      "WHERE batch_id = ? AND dir_path = ? AND filename = ?";
  sqlite3_stmt *fileStmt;
  if (sqlite3_prepare_v2(rawDb, fileSql.c_str(), -1, &fileStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[markFileEncoded] Failed to prepare: "
              << sqlite3_errmsg(rawDb);
    return false;
  }

  sqlite3_bind_text(fileStmt, 1, encodedPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(fileStmt, 2, fingerprint.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(fileStmt, 3, batchId);
  sqlite3_bind_text(fileStmt, 4, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(fileStmt, 5, fname.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(fileStmt) != SQLITE_DONE) {
    LOG_ERROR << "[markFileEncoded] Failed to update file: "
              << sqlite3_errmsg(rawDb);
    sqlite3_finalize(fileStmt);
    return false;
  }
  sqlite3_finalize(fileStmt);

  // 递增批次 encoded_count
  std::string batchSql =
      "UPDATE task_batches SET encoded_count = encoded_count + 1, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *batchStmt;
  if (sqlite3_prepare_v2(rawDb, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK)
    return false;

  sqlite3_bind_int(batchStmt, 1, batchId);
  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    sqlite3_finalize(batchStmt);
    return false;
  }
  sqlite3_finalize(batchStmt);

  if (!txn.commit())
    return false;

  return true;
}

bool BatchTaskRepo::deleteBatchFileAndIncrFailed(int batchId,
                                                 const std::string &filepath) {
  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return false;

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  ScopedTransaction txn(rawDb);
  if (!txn.begin())
    return false;

  // 删除批次文件记录
  std::string fileSql = "DELETE FROM task_batch_files "
                        "WHERE batch_id = ? AND dir_path = ? AND filename = ?";
  sqlite3_stmt *fileStmt;
  if (sqlite3_prepare_v2(rawDb, fileSql.c_str(), -1, &fileStmt, 0) != SQLITE_OK)
    return false;

  sqlite3_bind_int(fileStmt, 1, batchId);
  sqlite3_bind_text(fileStmt, 2, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(fileStmt, 3, fname.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(fileStmt) != SQLITE_DONE) {
    sqlite3_finalize(fileStmt);
    return false;
  }
  sqlite3_finalize(fileStmt);

  // 递增 failed_count
  std::string batchSql =
      "UPDATE task_batches SET failed_count = failed_count + 1, "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *batchStmt;
  if (sqlite3_prepare_v2(rawDb, batchSql.c_str(), -1, &batchStmt, 0) !=
      SQLITE_OK)
    return false;

  sqlite3_bind_int(batchStmt, 1, batchId);
  if (sqlite3_step(batchStmt) != SQLITE_DONE) {
    sqlite3_finalize(batchStmt);
    return false;
  }
  sqlite3_finalize(batchStmt);

  if (!txn.commit())
    return false;

  return true;
}

// ============ 恢复操作 ============

int BatchTaskRepo::rollbackEncodingFiles() {
  std::string sql = "UPDATE task_batch_files SET status = 'pending', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE status = 'encoding'";
  return db().executeUpdateCount(sql);
}

int BatchTaskRepo::rollbackBatchStatus() {
  std::string sql = "UPDATE task_batches SET status = 'encoding', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE status IN ('merging', 'extracting_mp3')";
  return db().executeUpdateCount(sql);
}

bool BatchTaskRepo::isInBatch(int pendingFileId) {
  std::string sql =
      "SELECT COUNT(*) FROM task_batch_files WHERE pending_file_id = ?";
  int count = db().queryScalar(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_int(stmt, 1, pendingFileId);
  });
  return count > 0;
}
