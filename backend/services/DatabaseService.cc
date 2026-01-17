#include "DatabaseService.h"

DatabaseService &DatabaseService::getInstance() {
  static DatabaseService instance;
  return instance;
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
  const char *sql = "CREATE TABLE IF NOT EXISTS processed_files ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "filepath TEXT NOT NULL,"
                    "filename TEXT,"
                    "md5 TEXT UNIQUE,"
                    "processed_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                    ");";
  if (!executeQuery(sql)) {
    LOG_FATAL << "Failed to initialize schema";
  }
}

void DatabaseService::initAndStart(const Json::Value &config) { init(); }

void DatabaseService::shutdown() {
  if (db_) {
    sqlite3_close(db_);
  }
}

