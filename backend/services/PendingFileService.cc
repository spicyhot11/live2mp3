#include "PendingFileService.h"
#include "../utils/FfmpegUtils.h"
#include "ConfigService.h"
#include "DatabaseService.h"
#include "MergerService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <iomanip>
#include <sqlite3.h>
#include <sstream>

namespace fs = std::filesystem;

// PendingFile 便捷方法
std::string PendingFile::getFilepath() const {
  if (dir_path.empty())
    return filename;
  if (dir_path.back() == '/')
    return dir_path + filename;
  return dir_path + "/" + filename;
}

void to_json(nlohmann::json &j, const PendingFile &p) {
  j = nlohmann::json{
      {"id", p.id},
      {"dir_path", p.dir_path},
      {"filename", p.filename},
      {"filepath", p.getFilepath()},
      {"fingerprint", p.fingerprint},
      {"stable_count", p.stable_count},
      {"status", p.status},
      {"temp_mp4_path", p.temp_mp4_path},
      {"temp_mp3_path", p.temp_mp3_path},
      {"start_time", p.start_time},
      {"end_time", p.end_time},
  };
}

// 辅助：从完整路径拆分为 dir_path + filename
static void splitPath(const std::string &filepath, std::string &dir_path,
                      std::string &filename) {
  fs::path p(filepath);
  dir_path = p.parent_path().string();
  filename = p.filename().string();
}

// 辅助：从 SQLite 行读取 PendingFile
// 列顺序: id, dir_path, filename, fingerprint, stable_count, status,
//          temp_mp4_path, temp_mp3_path, start_time, end_time
static PendingFile readRow(sqlite3_stmt *stmt) {
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

static const char *SELECT_COLS =
    "id, dir_path, filename, fingerprint, stable_count, status, "
    "temp_mp4_path, temp_mp3_path, start_time, end_time";

void PendingFileService::initAndStart(const Json::Value &config) {
  LOG_INFO << "PendingFileService initialized";
  // 启动时清理操作  移交给scheduler处理
  // cleanupOnStartup();
}

void PendingFileService::shutdown() {}

int PendingFileService::addOrUpdateFile(const std::string &filepath,
                                        const std::string &fingerprint) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[addOrUpdateFile] Database not available";
    return -1;
  }

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  // Check if file exists
  auto existing = getFile(filepath);

  if (existing.has_value()) {
    // File exists, check if fingerprint matches
    if (existing->fingerprint == fingerprint) {
      // Fingerprint matches. Check status.
      if (existing->status != "pending") {
        LOG_DEBUG << "[addOrUpdateFile] File " << filepath << " is "
                  << existing->status << " with same fingerprint. Ignoring.";
        return -1;
      }

      // Same fingerprint and pending, increment stable_count
      std::string sql =
          "UPDATE pending_files SET stable_count = stable_count + 1, "
          "updated_at = datetime('now', 'localtime') "
          "WHERE dir_path = ? AND filename = ?";
      sqlite3_stmt *stmt;
      if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR << "[addOrUpdateFile] Failed to prepare update: "
                  << sqlite3_errmsg(db);
        return -1;
      }
      sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);

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
          "status = 'pending', updated_at = datetime('now', 'localtime') "
          "WHERE dir_path = ? AND filename = ?";
      sqlite3_stmt *stmt;
      if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR << "[addOrUpdateFile] Failed to prepare reset: "
                  << sqlite3_errmsg(db);
        return -1;
      }
      sqlite3_bind_text(stmt, 1, fingerprint.c_str(), -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, dirPath.c_str(), -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 3, fname.c_str(), -1, SQLITE_STATIC);

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
        "INSERT INTO pending_files (dir_path, filename, fingerprint, "
        "stable_count, status) "
        "VALUES (?, ?, ?, 1, 'pending')";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
      LOG_ERROR << "[addOrUpdateFile] Failed to prepare insert: "
                << sqlite3_errmsg(db);
      return -1;
    }
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, fingerprint.c_str(), -1, SQLITE_STATIC);

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
      std::string("SELECT ") + SELECT_COLS +
      " FROM pending_files WHERE stable_count >= ? AND status = 'pending'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare statement";
    return files;
  }

  sqlite3_bind_int(stmt, 1, minCount);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files.push_back(readRow(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

bool PendingFileService::markAsStable(const std::string &filepath) {
  sqlite3 *db = DatabaseService::getInstance().getDb();

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  // 1. Parse start_time from filename
  std::string startTimeStr;
  std::string endTimeStr;

  auto parsedTime = MergerService::parseTime(fname);

  if (parsedTime.has_value()) {
    // 2. Get media duration
    int durationMs = live2mp3::utils::getMediaDuration(filepath);
    if (durationMs == -1) {
      LOG_WARN << "[markAsStable] Cannot get duration for " << filepath
               << ", marking as deprecated";
      markAsDeprecated(filepath);
      return false;
    }

    // Format start_time
    auto startTp = parsedTime.value();
    std::time_t startTt = std::chrono::system_clock::to_time_t(startTp);
    std::tm startTm = *std::localtime(&startTt);
    std::ostringstream startSs;
    startSs << std::put_time(&startTm, "%Y-%m-%d %H:%M:%S");
    startTimeStr = startSs.str();

    // Calculate end_time = start_time + duration
    auto endTp = startTp + std::chrono::milliseconds(durationMs);
    std::time_t endTt = std::chrono::system_clock::to_time_t(endTp);
    std::tm endTm = *std::localtime(&endTt);
    std::ostringstream endSs;
    endSs << std::put_time(&endTm, "%Y-%m-%d %H:%M:%S");
    endTimeStr = endSs.str();
  }

  // 3. Update DB
  std::string sql = "UPDATE pending_files SET status = 'stable', "
                    "start_time = ?, end_time = ?, "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsStable] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }

  if (startTimeStr.empty()) {
    sqlite3_bind_null(stmt, 1);
  } else {
    sqlite3_bind_text(stmt, 1, startTimeStr.c_str(), -1, SQLITE_TRANSIENT);
  }
  if (endTimeStr.empty()) {
    sqlite3_bind_null(stmt, 2);
  } else {
    sqlite3_bind_text(stmt, 2, endTimeStr.c_str(), -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_text(stmt, 3, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, fname.c_str(), -1, SQLITE_STATIC);

  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  if (success) {
    LOG_DEBUG << "[markAsStable] Marked as stable: " << filepath
              << " (start_time=" << startTimeStr << ", end_time=" << endTimeStr
              << ")";
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

  std::string sql = std::string("SELECT ") + SELECT_COLS +
                    " FROM pending_files WHERE status = 'stable'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[getAllStableFiles] Failed to prepare query";
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files.push_back(readRow(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<PendingFile> PendingFileService::getAndClaimStableFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    LOG_DEBUG << "[getAndClaimStableFiles] 无法获取数据库锁，可能存在并发任务: "
              << sqlite3_errmsg(db);
    return files;
  }

  // 1. 查询所有 stable 状态的文件
  std::string selectSql = std::string("SELECT ") + SELECT_COLS +
                          " FROM pending_files WHERE status = 'stable'";
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
    files.push_back(readRow(selectStmt));
    fileIds.push_back(files.back().id);
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

  for (auto &f : files) {
    f.status = "processing";
  }

  return files;
}

bool PendingFileService::markAsProcessing(const std::string &filepath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = 'processing', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessing] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }
  sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
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

  if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                   nullptr) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessingBatch] Failed to begin transaction: "
              << sqlite3_errmsg(db);
    return false;
  }

  std::string sql = "UPDATE pending_files SET status = 'processing', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ? AND status = 'stable'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsProcessingBatch] Failed to prepare: "
              << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
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
      LOG_ERROR << "[markAsProcessingBatch] Failed to update: " << filepath;
      sqlite3_finalize(stmt);
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      return false;
    }
    int changes = sqlite3_changes(db);
    if (changes == 0) {
      LOG_WARN << "[markAsProcessingBatch] File not in stable state, "
                  "skipping: "
               << filepath;
    }
    totalUpdated += changes;
  }

  sqlite3_finalize(stmt);

  if (totalUpdated == 0) {
    LOG_WARN << "[markAsProcessingBatch] No files were marked as processing";
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

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

  if (sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) !=
      SQLITE_OK) {
    LOG_ERROR << "[rollbackToStable] Failed to begin transaction";
    return false;
  }

  std::string sql = "UPDATE pending_files SET status = 'stable', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[rollbackToStable] Failed to prepare: " << sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }

  for (const auto &filepath : filepaths) {
    std::string dirPath, fname;
    splitPath(filepath, dirPath, fname);

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERROR << "[rollbackToStable] Failed to rollback: " << filepath;
      sqlite3_finalize(stmt);
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      return false;
    }
  }

  sqlite3_finalize(stmt);

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
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = 'converting', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool PendingFileService::markAsStaged(const std::string &filepath,
                                      const std::string &tempMp4Path) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql =
      "UPDATE pending_files SET status = 'staged', temp_mp4_path = ?, "
      "updated_at = datetime('now', 'localtime') "
      "WHERE dir_path = ? AND filename = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, tempMp4Path.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, fname.c_str(), -1, SQLITE_STATIC);
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return success;
}

bool PendingFileService::markAsCompleted(const std::string &filepath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = 'completed', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
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

  std::string sql = std::string("SELECT ") + SELECT_COLS +
                    " FROM pending_files WHERE status = 'staged' "
                    "AND datetime(updated_at, '+' || ? || ' seconds') <= "
                    "datetime('now', 'localtime')";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare staged files query";
    return files;
  }

  sqlite3_bind_int(stmt, 1, seconds);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files.push_back(readRow(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<PendingFile> PendingFileService::getAllStagedFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql = std::string("SELECT ") + SELECT_COLS +
                    " FROM pending_files WHERE status = 'staged'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare all staged files query";
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files.push_back(readRow(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

bool PendingFileService::removeFile(const std::string &filepath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql =
      "DELETE FROM pending_files WHERE dir_path = ? AND filename = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
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

  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = std::string("SELECT ") + SELECT_COLS +
                    " FROM pending_files WHERE dir_path = ? AND filename = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    auto f = readRow(stmt);
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

  std::string sql = std::string("SELECT ") + SELECT_COLS +
                    " FROM pending_files WHERE status = 'completed' ORDER BY "
                    "updated_at DESC";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[getCompletedFiles] Failed to prepare query";
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files.push_back(readRow(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

std::vector<PendingFile> PendingFileService::getAll() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql = std::string("SELECT ") + SELECT_COLS +
                    " FROM pending_files ORDER BY updated_at DESC";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    return files;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    files.push_back(readRow(stmt));
  }

  sqlite3_finalize(stmt);
  return files;
}

bool PendingFileService::markAsDeprecated(const std::string &filepath) {
  std::string dirPath, fname;
  splitPath(filepath, dirPath, fname);

  std::string sql = "UPDATE pending_files SET status = 'deprecated', "
                    "updated_at = datetime('now', 'localtime') "
                    "WHERE dir_path = ? AND filename = ?";
  sqlite3 *db = DatabaseService::getInstance().getDb();
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[markAsDeprecated] Failed to prepare: " << sqlite3_errmsg(db);
    return false;
  }
  sqlite3_bind_text(stmt, 1, dirPath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, fname.c_str(), -1, SQLITE_STATIC);
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
  // 使用 LIKE 模式匹配 filename: stem.%
  std::string pattern = stem + ".%";
  std::string sql =
      std::string("SELECT ") + SELECT_COLS +
      " FROM pending_files WHERE dir_path = ? AND filename LIKE ? "
      "AND status = 'stable'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[resolveDuplicateExtensions] Failed to prepare query: "
              << sqlite3_errmsg(db);
    return;
  }

  sqlite3_bind_text(stmt, 1, dir.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_STATIC);

  // 收集所有匹配的 stable 文件
  struct FileInfo {
    std::string filepath;
    uintmax_t size;
  };
  std::vector<FileInfo> candidates;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto pf = readRow(stmt);
    std::string candidatePath = pf.getFilepath();

    // 验证：确保 stem 完全匹配
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
    LOG_WARN << "[cleanupOnStartup] 无法获取 ConfigService，跳过清理操作";
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
  std::string sql = "SELECT id, dir_path, filename FROM pending_files WHERE "
                    "status = 'processing'";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "[recoverProcessingRecords] 查询失败: " << sqlite3_errmsg(db);
    return;
  }

  struct ProcessingRecord {
    int id;
    std::string dir_path;
    std::string filename;
    std::string getFilepath() const {
      if (dir_path.empty())
        return filename;
      if (dir_path.back() == '/')
        return dir_path + filename;
      return dir_path + "/" + filename;
    }
  };
  std::vector<ProcessingRecord> records;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ProcessingRecord rec;
    rec.id = sqlite3_column_int(stmt, 0);
    rec.dir_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    rec.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
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
    std::string fullPath = rec.getFilepath();
    std::error_code ec;
    bool fileExists = fs::exists(fullPath, ec);

    if (ec) {
      LOG_WARN << "[recoverProcessingRecords] 检查文件存在性失败: " << fullPath
               << ", 错误: " << ec.message();
      continue;
    }

    if (fileExists) {
      /*
        考虑不恢复stable，因为processing后状态由batchtask接管，在另一个数据表中维护
        如果恢复stable，batchtask会重新处理，导致重复处理
      */

      // // 文件存在，恢复为 stable 状态
      // std::string updateSql =
      //     "UPDATE pending_files SET status = 'stable', "
      //     "updated_at = datetime('now', 'localtime') WHERE id = ?";
      // sqlite3_stmt *updateStmt;

      // if (sqlite3_prepare_v2(db, updateSql.c_str(), -1, &updateStmt, 0) ==
      //     SQLITE_OK) {
      //   sqlite3_bind_int(updateStmt, 1, rec.id);
      //   if (sqlite3_step(updateStmt) == SQLITE_DONE) {
      //     LOG_INFO << "[recoverProcessingRecords] 恢复为 stable: " << fullPath;
      //     recoveredCount++;
      //   } else {
      //     LOG_ERROR << "[recoverProcessingRecords] 更新失败: " << fullPath;
      //   }
      //   sqlite3_finalize(updateStmt);
      // }
    } else {
      // 文件不存在，删除记录
      std::string deleteSql = "DELETE FROM pending_files WHERE id = ?";
      sqlite3_stmt *deleteStmt;

      if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &deleteStmt, 0) ==
          SQLITE_OK) {
        sqlite3_bind_int(deleteStmt, 1, rec.id);
        if (sqlite3_step(deleteStmt) == SQLITE_DONE) {
          LOG_WARN << "[recoverProcessingRecords] 文件不存在，删除记录: "
                   << fullPath;
          deletedCount++;
        } else {
          LOG_ERROR << "[recoverProcessingRecords] 删除失败: " << fullPath;
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
