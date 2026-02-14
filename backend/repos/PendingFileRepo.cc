#include "PendingFileRepo.h"
#include <filesystem>
#include <sqlite3.h>

namespace fs = std::filesystem;

// ============ 静态辅助 ============

void PendingFileRepo::splitPath(const std::string &filepath,
                                std::string &dirPath, std::string &filename) {
  fs::path p(filepath);
  dirPath = p.parent_path().string();
  filename = p.filename().string();
}

const char *PendingFileRepo::selectCols() {
  return "id, dir_path, filename, fingerprint, stable_count, status, "
         "temp_mp4_path, temp_mp3_path, start_time, end_time";
}

PendingFile PendingFileRepo::readRow(sqlite3_stmt *stmt) {
  PendingFile f;
  f.id = sqlite3_column_int(stmt, 0);
  f.dir_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  f.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  auto fpText = sqlite3_column_text(stmt, 3);
  f.fingerprint = fpText ? reinterpret_cast<const char *>(fpText) : "";
  f.stable_count = sqlite3_column_int(stmt, 4);
  f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  auto mp4Text = sqlite3_column_text(stmt, 6);
  f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
  auto mp3Text = sqlite3_column_text(stmt, 7);
  f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
  auto stText = sqlite3_column_text(stmt, 8);
  f.start_time = stText ? reinterpret_cast<const char *>(stText) : "";
  auto etText = sqlite3_column_text(stmt, 9);
  f.end_time = etText ? reinterpret_cast<const char *>(etText) : "";
  return f;
}

DatabaseService &PendingFileRepo::db() {
  return DatabaseService::getInstance();
}

std::string PendingFileRepo::ProcessingRecord::getFilepath() const {
  if (dir_path.empty())
    return filename;
  if (dir_path.back() == '/')
    return dir_path + filename;
  return dir_path + "/" + filename;
}

// ============ 查询方法 ============

std::optional<PendingFile>
PendingFileRepo::findByPath(const std::string &filepath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = std::string("SELECT ") + selectCols() +
                    " FROM pending_files WHERE dir_path = ? AND filename = ?";
  return db().queryOne<PendingFile>(sql, readRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_TRANSIENT);
  });
}

std::vector<PendingFile> PendingFileRepo::findAll() {
  std::string sql = std::string("SELECT ") + selectCols() +
                    " FROM pending_files ORDER BY updated_at DESC";
  return db().queryAll<PendingFile>(sql, readRow);
}

std::vector<PendingFile>
PendingFileRepo::findByStatus(const std::string &status) {
  std::string sql = std::string("SELECT ") + selectCols() +
                    " FROM pending_files WHERE status = ?";
  if (status == "completed") {
    sql += " ORDER BY updated_at DESC";
  }
  return db().queryAll<PendingFile>(sql, readRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
  });
}

std::vector<PendingFile> PendingFileRepo::findStableWithMinCount(int minCount) {
  std::string sql =
      std::string("SELECT ") + selectCols() +
      " FROM pending_files WHERE stable_count >= ? AND status = 'pending'";
  return db().queryAll<PendingFile>(sql, readRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_int(stmt, 1, minCount);
  });
}

std::vector<PendingFile> PendingFileRepo::findStagedOlderThan(int seconds) {
  std::string sql = std::string("SELECT ") + selectCols() +
                    " FROM pending_files WHERE status = 'staged' "
                    "AND datetime(updated_at, '+' || ? || ' seconds') <= "
                    "datetime('now', 'localtime')";
  return db().queryAll<PendingFile>(sql, readRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_int(stmt, 1, seconds);
  });
}

bool PendingFileRepo::existsByFingerprint(const std::string &fingerprint) {
  std::string sql = "SELECT COUNT(*) FROM pending_files WHERE fingerprint = ? "
                    "AND status = 'completed'";
  int count = db().queryScalar(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
  });
  return count > 0;
}

std::vector<PendingFile>
PendingFileRepo::findByDirAndStemLike(const std::string &dir,
                                      const std::string &pattern,
                                      const std::string &status) {
  std::string sql =
      std::string("SELECT ") + selectCols() +
      " FROM pending_files WHERE dir_path = ? AND filename LIKE ? "
      "AND status = ?";
  return db().queryAll<PendingFile>(sql, readRow, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, dir.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
  });
}

// ============ 插入/更新方法 ============

bool PendingFileRepo::insert(const std::string &dirPath,
                             const std::string &filename,
                             const std::string &fingerprint) {
  std::string sql =
      "INSERT INTO pending_files (dir_path, filename, fingerprint, "
      "stable_count, status) VALUES (?, ?, ?, 1, 'pending')";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::incrementStableCount(const std::string &dirPath,
                                           const std::string &filename) {
  std::string sql = "UPDATE pending_files SET stable_count = stable_count + 1, "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::resetFingerprint(const std::string &dirPath,
                                       const std::string &filename,
                                       const std::string &fingerprint) {
  std::string sql =
      "UPDATE pending_files SET fingerprint = ?, stable_count = 1, "
      "status = 'pending', updated_at = datetime('now', 'localtime') "
      "WHERE dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, filename.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::updateStatus(const std::string &filepath,
                                   const std::string &status) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = ?, "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, fname.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::updateStatusWithStartEnd(const std::string &filepath,
                                               const std::string &status,
                                               const std::string &startTime,
                                               const std::string &endTime) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = ?, "
                    "start_time = ?, end_time = ?, "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    if (startTime.empty()) {
      sqlite3_bind_null(stmt, 2);
    } else {
      sqlite3_bind_text(stmt, 2, startTime.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (endTime.empty()) {
      sqlite3_bind_null(stmt, 3);
    } else {
      sqlite3_bind_text(stmt, 3, endTime.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt, 4, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, fname.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::updateStatusWithTempPath(const std::string &filepath,
                                               const std::string &status,
                                               const std::string &tempPath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = ?, temp_mp4_path = ?, "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tempPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fname.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::updateStatusById(int id, const std::string &status) {
  std::string sql = "UPDATE pending_files SET status = ?, "
                    "updated_at = datetime('now', 'localtime') WHERE id = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
  });
}

// ============ 删除方法 ============

bool PendingFileRepo::deleteByPath(const std::string &filepath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql =
      "DELETE FROM pending_files WHERE dir_path = ? AND filename = ?";
  return db().executeUpdate(sql, [&](sqlite3_stmt *stmt) {
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_TRANSIENT);
  });
}

bool PendingFileRepo::deleteById(int id) {
  std::string sql = "DELETE FROM pending_files WHERE id = ?";
  return db().executeUpdate(
      sql, [&](sqlite3_stmt *stmt) { sqlite3_bind_int(stmt, 1, id); });
}

// ============ 事务性操作 ============

std::vector<PendingFile> PendingFileRepo::claimStableFiles() {
  std::vector<PendingFile> files;
  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return files;

  ScopedTransaction txn(rawDb);
  if (!txn.begin()) {
    LOG_DEBUG << "[claimStableFiles] 无法获取数据库锁，可能存在并发任务";
    return files;
  }

  std::string selectSql = std::string("SELECT ") + selectCols() +
                          " FROM pending_files WHERE status = 'stable'";
  sqlite3_stmt *selectStmt;
  if (sqlite3_prepare_v2(rawDb, selectSql.c_str(), -1, &selectStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[claimStableFiles] Failed to prepare select: "
              << sqlite3_errmsg(rawDb);
    return files;
  }

  std::vector<int> fileIds;
  while (sqlite3_step(selectStmt) == SQLITE_ROW) {
    files.push_back(readRow(selectStmt));
    fileIds.push_back(files.back().id);
  }
  sqlite3_finalize(selectStmt);

  if (files.empty()) {
    txn.commit();
    return files;
  }

  std::string updateSql =
      "UPDATE pending_files SET status = 'processing', "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *updateStmt;
  if (sqlite3_prepare_v2(rawDb, updateSql.c_str(), -1, &updateStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[claimStableFiles] Failed to prepare update: "
              << sqlite3_errmsg(rawDb);
    return {};
  }

  for (int id : fileIds) {
    sqlite3_reset(updateStmt);
    sqlite3_bind_int(updateStmt, 1, id);
    if (sqlite3_step(updateStmt) != SQLITE_DONE) {
      LOG_ERROR << "[claimStableFiles] Failed to update file id=" << id << ": "
                << sqlite3_errmsg(rawDb);
      sqlite3_finalize(updateStmt);
      return {};
    }
  }
  sqlite3_finalize(updateStmt);

  if (!txn.commit()) {
    return {};
  }

  LOG_INFO << "[claimStableFiles] Atomically claimed " << files.size()
           << " stable files";
  for (auto &f : files) {
    f.status = "processing";
  }
  return files;
}

bool PendingFileRepo::markProcessingBatch(
    const std::vector<std::string> &filepaths) {
  if (filepaths.empty())
    return true;

  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return false;

  ScopedTransaction txn(rawDb);
  if (!txn.begin())
    return false;

  std::string sql = "UPDATE pending_files SET status = 'processing', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ? AND status = 'stable'";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(rawDb, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markProcessingBatch] Failed to prepare: "
              << sqlite3_errmsg(rawDb);
    return false;
  }

  int totalUpdated = 0;
  for (const auto &filepath : filepaths) {
    std::string dirPath, fname;
    splitPath(filepath, dirPath, fname);
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR << "[markProcessingBatch] Failed to update: " << filepath;
      sqlite3_finalize(stmt);
      return false;
    }
    int changes = sqlite3_changes(rawDb);
    if (changes == 0) {
      LOG_WARN << "[markProcessingBatch] File not in stable state, skipping: "
               << filepath;
    }
    totalUpdated += changes;
  }
  sqlite3_finalize(stmt);

  if (totalUpdated == 0) {
    LOG_WARN << "[markProcessingBatch] No files were marked as processing";
    txn.rollback();
    return false;
  }

  if (!txn.commit())
    return false;

  LOG_DEBUG << "[markProcessingBatch] Marked " << totalUpdated << "/"
            << filepaths.size() << " files as processing";
  return true;
}

bool PendingFileRepo::rollbackToStable(
    const std::vector<std::string> &filepaths) {
  if (filepaths.empty())
    return true;

  sqlite3 *rawDb = db().getDb();
  if (!rawDb)
    return false;

  ScopedTransaction txn(rawDb);
  if (!txn.begin())
    return false;

  std::string sql = "UPDATE pending_files SET status = 'stable', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(rawDb, sql.c_str(), -1, &stmt, 0) != SQLITE_OK)
    return false;

  for (const auto &filepath : filepaths) {
    std::string dirPath, fname;
    splitPath(filepath, dirPath, fname);
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return false;
    }
  }
  sqlite3_finalize(stmt);

  if (!txn.commit())
    return false;

  LOG_WARN << "[rollbackToStable] Rolled back " << filepaths.size()
           << " files to stable status";
  return true;
}

// ============ 恢复相关 ============

std::vector<PendingFileRepo::ProcessingRecord>
PendingFileRepo::findProcessingRecords() {
  std::string sql = "SELECT id, dir_path, filename FROM pending_files WHERE "
                    "status = 'processing'";
  return db().queryAll<ProcessingRecord>(
      sql, [](sqlite3_stmt *stmt) -> ProcessingRecord {
        ProcessingRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.dir_path =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        rec.filename =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        return rec;
      });
}
