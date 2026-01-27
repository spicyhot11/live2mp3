#include "PendingFileService.h"
#include "DatabaseService.h"
#include <drogon/drogon.h>
#include <sqlite3.h>

void to_json(nlohmann::json &j, const PendingFile &p) {
  j = nlohmann::json{{"id", p.id},
                     {"filepath", p.filepath},
                     {"current_md5", p.current_md5},
                     {"stable_count", p.stable_count},
                     {"status", p.status},
                     {"temp_mp4_path", p.temp_mp4_path},
                     {"temp_mp3_path", p.temp_mp3_path},
                     {"created_at", p.created_at},
                     {"updated_at", p.updated_at}};
}

void PendingFileService::initAndStart(const Json::Value &config) {
  LOG_INFO << "PendingFileService initialized";
}

void PendingFileService::shutdown() {}

int PendingFileService::addOrUpdateFile(const std::string &filepath,
                                        const std::string &md5) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db) {
    LOG_ERROR << "[addOrUpdateFile] Database not available";
    return -1;
  }

  // Check if file exists
  auto existing = getFile(filepath);

  if (existing.has_value()) {
    // File exists, check if MD5 matches
    if (existing->current_md5 == md5) {
      // MD5 matches. Check status.
      if (existing->status != "pending") {
        // If file is already processed or processing, and MD5 hasn't changed,
        // ignore it. This prevents re-processing completed files.
        LOG_DEBUG << "[addOrUpdateFile] File " << filepath << " is "
                  << existing->status << " with same MD5. Ignoring.";
        return -1;
      }

      // Same MD5 and pending, increment stable_count
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
        LOG_DEBUG << "[addOrUpdateFile] MD5 same, incremented stable_count to "
                  << result;
      } else {
        LOG_ERROR << "[addOrUpdateFile] Update failed: " << sqlite3_errmsg(db);
      }
      sqlite3_finalize(stmt);
      return result;
    } else {
      // MD5 changed, reset stable_count
      LOG_DEBUG << "[addOrUpdateFile] MD5 changed, resetting stable_count";
      std::string sql =
          "UPDATE pending_files SET current_md5 = ?, stable_count = 1, "
          "status = 'pending', updated_at = datetime('now', 'localtime') WHERE "
          "filepath = "
          "?";
      sqlite3_stmt *stmt;
      if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR << "[addOrUpdateFile] Failed to prepare reset: "
                  << sqlite3_errmsg(db);
        return -1;
      }
      sqlite3_bind_text(stmt, 1, md5.c_str(), -1, SQLITE_STATIC);
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
        "INSERT INTO pending_files (filepath, current_md5, "
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
    sqlite3_bind_text(stmt, 2, md5.c_str(), -1, SQLITE_STATIC);

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
      "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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
  }
  return success;
}

std::vector<PendingFile> PendingFileService::getAllStableFiles() {
  std::vector<PendingFile> files;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return files;

  std::string sql = "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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
  drogon::app().getThreadPool();
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
      "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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
  std::string sql = "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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

  std::string sql = "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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

  std::string sql = "SELECT COUNT(*) FROM pending_files WHERE current_md5 = ? "
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

  std::string sql = "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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

  std::string sql = "SELECT id, filepath, current_md5, stable_count, status, "
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
    f.current_md5 = md5Text ? reinterpret_cast<const char *>(md5Text) : "";
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
