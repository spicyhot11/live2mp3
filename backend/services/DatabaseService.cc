#include "DatabaseService.h"

DatabaseService &DatabaseService::getInstance() {
  auto instance = drogon::app().getSharedPlugin<DatabaseService>();
  if (instance) {
    return *instance;
  }
  // Fallback to static instance (shouldn't happen in production)
  static DatabaseService fallback;
  LOG_WARN << "DatabaseService plugin not found, using fallback instance";
  return fallback;
}

void DatabaseService::init(const std::string &dbPath) {
  // No lock needed during single-threaded initialization
  if (db_)
    return;

  int rc = sqlite3_open(dbPath.c_str(), &db_);
  if (rc) {
    LOG_ERROR << "Can't open database: " << sqlite3_errmsg(db_);
    return;
  }
  LOG_INFO << "Opened database: " << dbPath;
  initSchema();
}

sqlite3 *DatabaseService::getDb() { return db_; }

bool DatabaseService::executeQuery(const std::string &query) {
  std::lock_guard<std::mutex> lock(mutex_);
  char *zErrMsg = 0;
  int rc = sqlite3_exec(db_, query.c_str(), 0, 0, &zErrMsg);
  if (rc != SQLITE_OK) {
    LOG_ERROR << "SQL error: " << zErrMsg;
    sqlite3_free(zErrMsg);
    return false;
  }
  return true;
}

void DatabaseService::initSchema() {
  // Pending files table for stability tracking
  // We use datetime('now', 'localtime') to match system timezone (Beijing Time)
  const char *pendingSql =
      "CREATE TABLE IF NOT EXISTS pending_files ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "filepath TEXT UNIQUE NOT NULL,"
      "fingerprint TEXT,"
      "stable_count INTEGER DEFAULT 0,"
      "status TEXT DEFAULT 'pending',"
      "temp_mp4_path TEXT,"
      "temp_mp3_path TEXT,"
      "created_at DATETIME DEFAULT (datetime('now', 'localtime')),"
      "updated_at DATETIME DEFAULT (datetime('now', 'localtime'))"
      ");";
  if (!executeQuery(pendingSql)) {
    LOG_FATAL << "Failed to initialize pending_files schema";
  }
}

void DatabaseService::initAndStart(const Json::Value &config) { init(); }

void DatabaseService::shutdown() {
  if (db_) {
    sqlite3_close(db_);
  }
}
