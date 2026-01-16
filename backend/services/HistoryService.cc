#include "HistoryService.h"
#include "DatabaseService.h"
#include <drogon/drogon.h>
#include <sqlite3.h>

void to_json(nlohmann::json &j, const HistoryRecord &p) {
  j = nlohmann::json{{"id", p.id},
                     {"filepath", p.filepath},
                     {"filename", p.filename},
                     {"md5", p.md5},
                     {"processed_at", p.processed_at}};
}


bool HistoryService::hasProcessed(const std::string &md5) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string sql = "SELECT COUNT(*) FROM processed_files WHERE md5 = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare statement: " << sqlite3_errmsg(db);
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

bool HistoryService::addRecord(const std::string &filepath,
                               const std::string &filename,
                               const std::string &md5) {
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return false;

  std::string sql =
      "INSERT INTO processed_files (filepath, filename, md5) VALUES (?, ?, ?)";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare statement: " << sqlite3_errmsg(db);
    return false;
  }

  sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, md5.c_str(), -1, SQLITE_STATIC);

  bool success = false;
  if (sqlite3_step(stmt) == SQLITE_DONE) {
    success = true;
  } else {
    LOG_ERROR << "Failed to insert record: " << sqlite3_errmsg(db);
  }

  sqlite3_finalize(stmt);
  return success;
}

bool HistoryService::removeRecord(int id) {
  std::string sql =
      "DELETE FROM processed_files WHERE id = " + std::to_string(id);
  return DatabaseService::getInstance().executeQuery(sql);
}

std::vector<HistoryRecord> HistoryService::getAll() {
  std::vector<HistoryRecord> records;
  sqlite3 *db = DatabaseService::getInstance().getDb();
  if (!db)
    return records;

  std::string sql = "SELECT id, filepath, filename, md5, processed_at FROM "
                    "processed_files ORDER BY processed_at DESC";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
    LOG_ERROR << "Failed to prepare statement: " << sqlite3_errmsg(db);
    return records;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    HistoryRecord record;
    record.id = sqlite3_column_int(stmt, 0);
    record.filepath =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    record.filename =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    record.md5 = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    record.processed_at =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    records.push_back(record);
  }

  sqlite3_finalize(stmt);
  return records;
}

void HistoryService::initAndStart(const Json::Value &config) {
  
}

void HistoryService::shutdown() {}
