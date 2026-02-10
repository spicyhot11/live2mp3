#include "PendingFileService.h"
#include "ConfigService.h"
#include "DatabaseService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <sqlite3.h>

namespace fs = std::filesystem;

void to_json(nlohmann::json &j, const PendingFile &p) {
  j = nlohmann::json{{"id", p.id},
                     {"filepath", p.filepath},
                     {"fingerprint", p.fingerprint},
                     {"stable_count", p.stable_count},
                     {"status", p.status},
                     {"temp_mp4_path", p.temp_mp4_path},
                     {"temp_mp3_path", p.temp_mp3_path},
                     {"created_at", p.created_at},
                     {"updated_at", p.updated_at}};
}

void PendingFileService::initAndStart(const Json::Value &config) {
  LOG_INFO << "PendingFileService initialized";
  // 启动时清理操作
  cleanupOnStartup();
}

void PendingFileService::shutdown() {}

int PendingFileService::addOrUpdateFile(const std::string &filepath,
                                        const std::string &fingerprint) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[addOrUpdateFile] Database not available";
    return -1;
  }

  // Check if file exists
  auto existing = getFile(filepath);

  if (existing.has_value()) {
    // File exists, check if fingerprint matches
    if (existing->fingerprint == fingerprint) {
      // Fingerprint matches. Check status.
      if (existing->status != "pending") {
        // If file is already processed or processing, and fingerprint hasn't
        // changed, ignore it. This prevents re-processing completed files.
        LOG_DEBUG << "[addOrUpdateFile] File " << filepath << " is "
                  << existing->status << " with same fingerprint. Ignoring.";
        return -1;
      }

      // Same fingerprint and pending, increment stable_count
      std::string sql =
          "UPDATE pending_files SET stable_count = stable_count + 1, "
          "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
      sqlite3_stmt *stmt;
      if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR << "[addOrUpdateFile] Failed to prepare update: "
                  << sqlite3_errmsg(db);
        return -1;
      }
      sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);

      int result = -1;
      if (sqlite3_step(stmt) == SQLITE_DONE) {
        result = existing->stable_count + 1;
        LOG_DEBUG << "[addOrUpdateFile] Fingerprint same, incremented "
                     "stable_count to "
                  << result;
      } else {
        LOG_ERROR << "[addOrUpdateFile] Update failed: " << sqlite3_errmsg(db);
      }
      sqlite3_finalize(stmt);
      return result;
    } else {
      // Fingerprint changed, reset stable_count
      LOG_DEBUG
          << "[addOrUpdateFile] Fingerprint changed, resetting stable_count";
      std::string sql =
          "UPDATE pending_files SET fingerprint = ?, stable_count = 1, "
          "status = 'pending', updated_at = datetime('now', 'localtime') WHERE "
          "filepath = "
          "?";
      sqlite3_stmt *stmt;
      if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR << "[addOrUpdateFile] Failed to prepare reset: "
                  << sqlite3_errmsg(db);
        return -1;
      }
      sqlite3_bind_text(stmt, 1, fingerprint.c_str(), -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, filepath.c_str(), -1, SQLITE_STATIC);

      int result = -1;
      if (sqlite3_step(stmt) == SQLITE_DONE) {
        result = 1;
      } else {
        LOG_ERROR << "[addOrUpdateFile] Reset failed: " << sqlite3_errmsg(db);
      }
      sqlite3_finalize(stmt);
      return result;
    }
  } else {
    // New file, insert
    LOG_DEBUG << "[addOrUpdateFile] New file, inserting: " << filepath;
    std::string sql =
        "INSERT INTO pending_files (filepath, fingerprint, "
        "stable_count, status, created_at, updated_at) "
        "VALUES (?, ?, 1, 'pending', datetime('now', 'localtime'), "
        "datetime('now', 'localtime'))";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
      LOG_ERROR << "[addOrUpdateFile] Failed to prepare insert: "
                << sqlite3_errmsg(db);
      return -1;
    }
    sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fingerprint.c_str(), -1, SQLITE_STATIC);

    int result = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
      result = 1;
      LOG_DEBUG << "[addOrUpdateFile] Inserted successfully";
    } else {
      LOG_ERROR << "[addOrUpdateFile] Insert failed: " << sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    return result;
  }
}

std::vector<PendingFile> PendingFileService::getStableFiles(int minCount) {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql =
      "SELECT id, filepath, fingerprint, stable_count, status, "
      "temp_mp4_path, temp_mp3_path, created_at, updated_at "
      "FROM pending_files WHERE stable_count >= ? AND status = 'pending'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare statement";
    return files;
  }

  sqlite3_bind_int(stmt, 1, minCount);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

bool PendingFileService::markAsStable(const std::string &filepath) {
  std::string sql =
      "UPDATE pending_files SET status = 'stable', "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsStable] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }
  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  if (success) {
    LOG_DEBUG << "[markAsStable] Marked as stable: " << filepath;
    // 检测并处理同名不同扩展名的文件
    resolveDuplicateExtensions(filepath);
  }
  return success;
}

std::vector<PendingFile> PendingFileService::getAllStableFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql = "SELECT id, filepath, fingerprint, stable_count, status, "
                    "temp_mp4_path, temp_mp3_path, created_at, updated_at "
                    "FROM pending_files WHERE status = 'stable'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[getAllStableFiles] Failed to prepare query";
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<PendingFile> PendingFileService::getAndClaimStableFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  // 使用 IMMEDIATE 事务，在开始时就获取写锁
  // 这样可以防止其他线程同时读取和修改
  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    // 可能是其他线程正在持有锁，这是正常的并发情况
    LOG_DEBUG << "[getAndClaimStableFiles] 无法获取数据库锁，可能存在并发任务: "
              << sqlite3_errmsg(db);
    return files;
  }

  // 1. 查询所有 stable 状态的文件
  std::string selectSql =
      "SELECT id, filepath, fingerprint, stable_count, status, "
      "temp_mp4_path, temp_mp3_path, created_at, updated_at "
      "FROM pending_files WHERE status = 'stable'";
  sqlite3_stmt *selectStmt;

  if (sqlite3_prepare_v2(db, selectSql.c_str(), -1, &selectStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[getAndClaimStableFiles] Failed to prepare select: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return files;
  }

  std::vector<int> fileIds;
  while (sqlite3_step(selectStmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(selectStmt, 0);
    f.filepath =
        reinterpret_cast<const char *>(sqlite3_column_text(selectStmt, 1));
    auto md5Text = sqlite3_column_text(selectStmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(selectStmt, 3);
    f.status =
        reinterpret_cast<const char *>(sqlite3_column_text(selectStmt, 4));
    auto mp4Text = sqlite3_column_text(selectStmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(selectStmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at =
        reinterpret_cast<const char *>(sqlite3_column_text(selectStmt, 7));
    f.updated_at =
        reinterpret_cast<const char *>(sqlite3_column_text(selectStmt, 8));
    files.push_back(f);
    fileIds.push_back(f.id);
  }
  sqlite3_finalize(selectStmt);

  if (files.empty()) {
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    return files;
  }

  // 2. 原子性地更新所有文件状态为 processing
  std::string updateSql =
      "UPDATE pending_files SET status = 'processing', "
      "updated_at = datetime('now', 'localtime') WHERE id = ?";
  sqlite3_stmt *updateStmt;

  if (sqlite3_prepare_v2(db, updateSql.c_str(), -1, &updateStmt, 0) !=
      SQLITE_OK) {
    LOG_ERROR << "[getAndClaimStableFiles] Failed to prepare update: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return {};
  }

  for (int id : fileIds) {
    sqlite3_reset(updateStmt);
    sqlite3_bind_int(updateStmt, 1, id);
    if (sqlite3_step(updateStmt) != SQLITE_DONE) {
      LOG_ERROR << "[getAndClaimStableFiles] Failed to update file id=" << id
                << ": " << sqlite3_errmsg(db);
      sqlite3_finalize(updateStmt);
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      return {};
    }
  }
  sqlite3_finalize(updateStmt);

  // 3. 提交事务
  if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    LOG_ERROR << "[getAndClaimStableFiles] Failed to commit: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return {};
  }

  LOG_INFO << "[getAndClaimStableFiles] Atomically claimed " << files.size()
           << " stable files";

  // 更新返回结果中的状态（因为我们已经更新了数据库）
  for (auto &f : files) {
    f.status = "processing";
  }

  return files;
}

bool PendingFileService::markAsProcessing(const std::string &filepath) {
  std::string sql =
      "UPDATE pending_files SET status = 'processing', "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessing] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }
  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  if (success) {
    LOG_DEBUG << "[markAsProcessing] Marked as processing: " << filepath;
  }
  return success;
}

bool PendingFileService::markAsProcessingBatch(
    const std::vector<std::string> &filepaths) {
  if (filepaths.empty())
    return true;

  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[markAsProcessingBatch] Database not available";
    return false;
  }

  // 开始事务（使用 IMMEDIATE 锁定数据库，防止并发修改）
  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessingBatch] Failed to begin transaction: "
              << sqlite3_errmsg(db);
    return false;
  }

  // 条件更新：只有状态为 'stable' 时才能更新为 'processing'
  // 这样如果另一个线程已经拿走了这个文件，更新将不会生效
  std::string sql = "UPDATE pending_files SET status = 'processing', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE filepath = ? AND status = 'stable'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessingBatch] Failed to prepare: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  int totalUpdated = 0;
  for (const auto &filepath : filepaths) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR << "[markAsProcessingBatch] Failed to update: " << filepath;
      sqlite3_finalize(stmt);
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      return false;
    }
    // 检查实际更新的行数
    int changes = sqlite3_changes(db);
    if (changes == 0) {
      // 文件状态不是 stable，可能已被其他线程处理
      LOG_WARN << "[markAsProcessingBatch] File not in stable state, "
                  "skipping: "
               << filepath;
    }
    totalUpdated += changes;
  }

  sqlite3_finalize(stmt);

  // 验证：如果没有任何文件被成功标记，回滚事务
  if (totalUpdated == 0) {
    LOG_WARN << "[markAsProcessingBatch] No files were marked as processing "
                "(all may have been claimed by another thread)";
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  // 提交事务
  if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessingBatch] Failed to commit transaction";
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  LOG_DEBUG << "[markAsProcessingBatch] Marked " << totalUpdated << "/"
            << filepaths.size() << " files as processing";
  return true;
}

bool PendingFileService::rollbackToStable(
    const std::vector<std::string> &filepaths) {
  if (filepaths.empty())
    return true;

  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[rollbackToStable] Database not available";
    return false;
  }

  // 开始事务
  if (sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) !=
      SQLITE_OK) {
    LOG_ERROR << "[rollbackToStable] Failed to begin transaction";
    return false;
  }

  std::string sql =
      "UPDATE pending_files SET status = 'stable', "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[rollbackToStable] Failed to prepare: " << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  for (const auto &filepath : filepaths) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR << "[rollbackToStable] Failed to rollback: " << filepath;
      sqlite3_finalize(stmt);
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      return false;
    }
  }

  sqlite3_finalize(stmt);

  // 提交事务
  if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
    LOG_ERROR << "[rollbackToStable] Failed to commit transaction";
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  LOG_WARN << "[rollbackToStable] Rolled back " << filepaths.size()
           << " files to stable status";
  return true;
}

bool PendingFileService::markAsConverting(const std::string &filepath) {
  std::string sql =
      "UPDATE pending_files SET status = 'converting', "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool PendingFileService::markAsStaged(const std::string &filepath,
                                      const std::string &tempMp4Path) {
  std::string sql =
      "UPDATE pending_files SET status = 'staged', temp_mp4_path = ?, "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, tempMp4Path.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool PendingFileService::markAsCompleted(const std::string &filepath) {
  std::string sql =
      "UPDATE pending_files SET status = 'completed', "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

std::vector<PendingFile>
PendingFileService::getStagedFilesOlderThan(int seconds) {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  // Get staged files where updated_at is older than N seconds ago
  std::string sql =
      "SELECT id, filepath, fingerprint, stable_count, status, "
      "temp_mp4_path, temp_mp3_path, created_at, updated_at "
      "FROM pending_files WHERE status = 'staged' "
      "AND datetime(updated_at, '+' || ? || ' seconds') <= datetime('now')";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare staged files query";
    return files;
  }

  sqlite3_bind_int(stmt, 1, seconds);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<PendingFile> PendingFileService::getAllStagedFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  // Get all staged files regardless of time
  std::string sql = "SELECT id, filepath, fingerprint, stable_count, status, "
                    "temp_mp4_path, temp_mp3_path, created_at, updated_at "
                    "FROM pending_files WHERE status = 'staged'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare all staged files query";
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

bool PendingFileService::removeFile(const std::string &filepath) {
  std::string sql = "DELETE FROM pending_files WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool PendingFileService::removeFileById(int id) {
  std::string sql = "DELETE FROM pending_files WHERE id = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(stmt, 1, id);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

std::optional<PendingFile>
PendingFileService::getFile(const std::string &filepath) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return std::nullopt;

  std::string sql = "SELECT id, filepath, fingerprint, stable_count, status, "
                    "temp_mp4_path, temp_mp3_path, created_at, updated_at "
                    "FROM pending_files WHERE filepath = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    sqlite3_finalize(stmt);
    return f;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

bool PendingFileService::isProcessed(const std::string &md5) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string sql = "SELECT COUNT(*) FROM pending_files WHERE fingerprint = ? "
                    "AND status = 'completed'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[isProcessed] Failed to prepare statement: "
              << sqlite3_errmsg(db);
    return false;
  }

  sqlite3_bind_text(stmt, 1, md5.c_str(), -1, SQLITE_STATIC);

  bool exists = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int count = sqlite3_column_int(stmt, 0);
    exists = (count > 0);
  }

  sqlite3_finalize(stmt);
  return exists;
}

std::vector<PendingFile> PendingFileService::getCompletedFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql = "SELECT id, filepath, fingerprint, stable_count, status, "
                    "temp_mp4_path, temp_mp3_path, created_at, updated_at "
                    "FROM pending_files WHERE status = 'completed' ORDER BY "
                    "updated_at DESC";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[getCompletedFiles] Failed to prepare query";
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<PendingFile> PendingFileService::getAll() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql = "SELECT id, filepath, fingerprint, stable_count, status, "
                    "temp_mp4_path, temp_mp3_path, created_at, updated_at "
                    "FROM pending_files ORDER BY updated_at DESC";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PendingFile f;
    f.id = sqlite3_column_int(stmt, 0);
    f.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    auto md5Text = sqlite3_column_text(stmt, 2);
    f.fingerprint = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
    f.stable_count = sqlite3_column_int(stmt, 3);
    f.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    auto mp4Text = sqlite3_column_text(stmt, 5);
    f.temp_mp4_path = mp4Text ? reinterpret_cast<const char *>(mp4Text) : "";
    auto mp3Text = sqlite3_column_text(stmt, 6);
    f.temp_mp3_path = mp3Text ? reinterpret_cast<const char *>(mp3Text) : "";
    f.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    f.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    files.push_back(f);
  }

  sqlite3_finalize(stmt);
  return files;
}

bool PendingFileService::markAsDeprecated(const std::string &filepath) {
  std::string sql =
      "UPDATE pending_files SET status = 'deprecated', "
      "updated_at = datetime('now', 'localtime') WHERE filepath = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsDeprecated] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }
  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  if (success) {
    LOG_INFO << "[markAsDeprecated] 文件标记为废弃: " << filepath;
  }
  return success;
}

void PendingFileService::resolveDuplicateExtensions(
    const std::string &filepath) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return;

  // 1. 提取文件 stem（不含扩展名）和目录
  fs::path path(filepath);
  std::string stem = path.stem().string();
  std::string dir = path.parent_path().string();

  // 2. 查询同目录下同 stem 但不同扩展名的 stable 文件
  // 使用 LIKE 模式匹配：目录/stem.%
  std::string pattern = (fs::path(dir) / (stem + ".%")).string();
  std::string sql =
      "SELECT id, filepath, fingerprint, stable_count, status, "
      "temp_mp4_path, temp_mp3_path, created_at, updated_at "
      "FROM pending_files WHERE filepath LIKE ? AND status = 'stable'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[resolveDuplicateExtensions] Failed to prepare query: "
              << sqlite3_errmsg(db);
    return;
  }

  sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_STATIC);

  // 收集所有匹配的 stable 文件
  struct FileInfo {
    std::string filepath;
    uintmax_t size;
  };
  std::vector<FileInfo> candidates;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::string candidatePath =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));

    // 验证：确保 stem 完全匹配（避免 "video.flv" 匹配 "video_1.flv"）
    fs::path candidateFsPath(candidatePath);
    if (candidateFsPath.stem().string() != stem) {
      continue;
    }

    // 获取文件大小
    std::error_code ec;
    uintmax_t fileSize = fs::file_size(candidatePath, ec);
    if (ec) {
      LOG_WARN << "[resolveDuplicateExtensions] 无法获取文件大小: "
               << candidatePath << ", 错误: " << ec.message();
      continue;
    }

    candidates.push_back({candidatePath, fileSize});
  }
  sqlite3_finalize(stmt);

  // 3. 如果只有一个文件或没有文件，无需处理
  if (candidates.size() <= 1) {
    return;
  }

  LOG_INFO << "[resolveDuplicateExtensions] 发现 " << candidates.size()
           << " 个同名不同扩展名的文件 (stem=" << stem << ")";

  // 4. 找出最大的文件
  auto maxIt = std::max_element(
      candidates.begin(), candidates.end(),
      [](const FileInfo &a, const FileInfo &b) { return a.size < b.size; });

  // 5. 将非最大文件标记为 deprecated
  for (const auto &file : candidates) {
    if (file.filepath != maxIt->filepath) {
      LOG_INFO << "[resolveDuplicateExtensions] 标记为废弃: " << file.filepath
               << " (size=" << file.size << " < " << maxIt->filepath
               << " size=" << maxIt->size << ")";
      markAsDeprecated(file.filepath);
    }
  }
}

void PendingFileService::cleanupOnStartup() {
  LOG_INFO << "[cleanupOnStartup] 开始启动清理操作...";

  // 1. 恢复 processing 状态的记录
  recoverProcessingRecords();

  // 2. 清理输出目录中的临时文件和 _writing 文件
  auto configService = drogon::app().getPlugin<ConfigService>();
  if (configService) {
    AppConfig cfg = configService->getConfig();
    cleanupTempDirectory(cfg.output.output_root);
    cleanupWritingFiles(cfg.output.output_root);
  } else {
    LOG_WARN
        << "[cleanupOnStartup] 无法获取 ConfigService，跳过清理操作";
  }

  LOG_INFO << "[cleanupOnStartup] 启动清理操作完成";
}

void PendingFileService::recoverProcessingRecords() {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[recoverProcessingRecords] 数据库不可用";
    return;
  }

  // 查询所有 processing 状态的记录
  std::string sql =
      "SELECT id, filepath FROM pending_files WHERE status = 'processing'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[recoverProcessingRecords] 查询失败: " << sqlite3_errmsg(db);
    return;
  }

  struct ProcessingRecord {
    int id;
    std::string filepath;
  };
  std::vector<ProcessingRecord> records;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ProcessingRecord rec;
    rec.id = sqlite3_column_int(stmt, 0);
    rec.filepath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    records.push_back(rec);
  }
  sqlite3_finalize(stmt);

  if (records.empty()) {
    LOG_INFO << "[recoverProcessingRecords] 没有需要恢复的 processing 记录";
    return;
  }

  LOG_INFO << "[recoverProcessingRecords] 发现 " << records.size()
           << " 条 processing 记录，开始恢复...";

  int recoveredCount = 0;
  int deletedCount = 0;

  for (const auto &rec : records) {
    // 检查文件是否存在
    std::error_code ec;
    bool fileExists = fs::exists(rec.filepath, ec);

    if (ec) {
      LOG_WARN << "[recoverProcessingRecords] 检查文件存在性失败: "
               << rec.filepath << ", 错误: " << ec.message();
      continue;
    }

    if (fileExists) {
      // 文件存在，恢复为 stable 状态
      std::string updateSql =
          "UPDATE pending_files SET status = 'stable', "
          "updated_at = datetime('now', 'localtime') WHERE id = ?";
      sqlite3_stmt *updateStmt;

      if (sqlite3_prepare_v2(db, updateSql.c_str(), -1, &updateStmt, 0) ==
          SQLITE_OK) {
        sqlite3_bind_int(updateStmt, 1, rec.id);
        if (sqlite3_step(updateStmt) == SQLITE_DONE) {
          LOG_INFO << "[recoverProcessingRecords] 恢复为 stable: "
                   << rec.filepath;
          recoveredCount++;
        } else {
          LOG_ERROR << "[recoverProcessingRecords] 更新失败: " << rec.filepath;
        }
        sqlite3_finalize(updateStmt);
      }
    } else {
      // 文件不存在，删除记录
      std::string deleteSql = "DELETE FROM pending_files WHERE id = ?";
      sqlite3_stmt *deleteStmt;

      if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &deleteStmt, 0) ==
          SQLITE_OK) {
        sqlite3_bind_int(deleteStmt, 1, rec.id);
        if (sqlite3_step(deleteStmt) == SQLITE_DONE) {
          LOG_WARN << "[recoverProcessingRecords] 文件不存在，删除记录: "
                   << rec.filepath;
          deletedCount++;
        } else {
          LOG_ERROR << "[recoverProcessingRecords] 删除失败: " << rec.filepath;
        }
        sqlite3_finalize(deleteStmt);
      }
    }
  }

  LOG_INFO << "[recoverProcessingRecords] 恢复完成: 恢复 " << recoveredCount
           << " 条，删除 " << deletedCount << " 条";
}

void PendingFileService::cleanupTempDirectory(const std::string &outputRoot) {
  const std::string tmpDir = outputRoot + "/tmp";
  std::error_code ec;

  if (!fs::exists(tmpDir, ec) || !fs::is_directory(tmpDir, ec)) {
    LOG_WARN << "[cleanupTempDirectory] 临时目录不存在或不是目录: " << tmpDir;
    return;
  }

  LOG_INFO << "[cleanupTempDirectory] 开始清理临时目录: " << tmpDir;

  int deletedCount = 0;
  for (const auto &entry : fs::directory_iterator(tmpDir, ec)) {
    if (ec) {
      LOG_WARN << "[cleanupTempDirectory] 遍历目录失败: " << ec.message();
      break;
    }

    if (entry.is_regular_file(ec)) {
      std::error_code removeEc;
      fs::remove(entry.path(), removeEc);
      if (!removeEc) {
        LOG_DEBUG << "[cleanupTempDirectory] 删除: " << entry.path().string();
        deletedCount++;
      } else {
        LOG_WARN << "[cleanupTempDirectory] 删除失败: " << entry.path().string()
                 << ", 错误: " << removeEc.message();
      }
    }
  }

  LOG_INFO << "[cleanupTempDirectory] 清理完成，删除 " << deletedCount
           << " 个文件";
}

void PendingFileService::cleanupWritingFiles(const std::string &outputRoot) {
  std::error_code ec;

  if (!fs::exists(outputRoot, ec) || !fs::is_directory(outputRoot, ec)) {
    LOG_WARN << "[cleanupWritingFiles] 输出目录不存在或不是目录: "
             << outputRoot;
    return;
  }

  LOG_INFO << "[cleanupWritingFiles] 开始清理输出目录中的 _writing 文件: "
           << outputRoot;

  int deletedCount = 0;
  for (const auto &entry : fs::recursive_directory_iterator(outputRoot, ec)) {
    if (ec) {
      LOG_WARN << "[cleanupWritingFiles] 遍历目录失败: " << ec.message();
      break;
    }

    if (entry.is_regular_file(ec)) {
      std::string filename = entry.path().filename().string();
      // 检查文件名是否以 _writing 结尾（.mp4 或 .mp3 之前）
      if (filename.find("_writing.mp4") != std::string::npos ||
          filename.find("_writing.mp3") != std::string::npos) {
        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (!removeEc) {
          LOG_INFO << "[cleanupWritingFiles] 删除 _writing 文件: "
                   << entry.path().string();
          deletedCount++;
        } else {
          LOG_WARN << "[cleanupWritingFiles] 删除失败: "
                   << entry.path().string() << ", 错误: " << removeEc.message();
        }
      }
    }
  }

  LOG_INFO << "[cleanupWritingFiles] 清理完成，删除 " << deletedCount
           << " 个 _writing 文件";
}
