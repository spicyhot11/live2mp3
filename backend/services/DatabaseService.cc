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
  // filepath 拆分为 dir_path + filename
  const char *pendingSql =
      "CREATE TABLE IF NOT EXISTS pending_files ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "dir_path TEXT NOT NULL,"
      "filename TEXT NOT NULL,"
      "fingerprint TEXT,"
      "stable_count INTEGER DEFAULT 0,"
      "status TEXT DEFAULT 'pending',"
      "temp_mp4_path TEXT,"
      "temp_mp3_path TEXT,"
      "updated_at DATETIME DEFAULT (datetime('now', 'localtime')),"
      "start_time TEXT,"
      "end_time TEXT,"
      "UNIQUE(dir_path, filename)"
      ");";
  if (!executeQuery(pendingSql)) {
    LOG_FATAL << "Failed to initialize pending_files schema";
  }

  // 批次表：管理转码/合并/MP3提取的整个流程
  const char *batchesSql =
      "CREATE TABLE IF NOT EXISTS task_batches ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "streamer TEXT NOT NULL,"
      "status TEXT DEFAULT 'encoding',"
      "output_dir TEXT,"
      "tmp_dir TEXT,"
      "final_mp4_path TEXT,"
      "final_mp3_path TEXT,"
      "total_files INTEGER DEFAULT 0,"
      "encoded_count INTEGER DEFAULT 0,"
      "failed_count INTEGER DEFAULT 0,"
      "created_at DATETIME DEFAULT (datetime('now', 'localtime')),"
      "updated_at DATETIME DEFAULT (datetime('now', 'localtime'))"
      ");";
  if (!executeQuery(batchesSql)) {
    LOG_FATAL << "Failed to initialize task_batches schema";
  }

  // 批次文件表：跟踪批次中每个文件的转码状态
  const char *batchFilesSql =
      "CREATE TABLE IF NOT EXISTS task_batch_files ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "batch_id INTEGER NOT NULL,"
      "dir_path TEXT NOT NULL,"
      "filename TEXT NOT NULL,"
      "fingerprint TEXT NOT NULL,"
      "pending_file_id INTEGER,"
      "status TEXT DEFAULT 'pending',"
      "encoded_path TEXT,"
      "retry_count INTEGER DEFAULT 0,"
      "created_at DATETIME DEFAULT (datetime('now', 'localtime')),"
      "updated_at DATETIME DEFAULT (datetime('now', 'localtime')),"
      "FOREIGN KEY (batch_id) REFERENCES task_batches(id)"
      ");";
  if (!executeQuery(batchFilesSql)) {
    LOG_FATAL << "Failed to initialize task_batch_files schema";
  }

  // fingerprint 唯一索引
  executeQuery("CREATE UNIQUE INDEX IF NOT EXISTS idx_batch_files_fingerprint "
               "ON task_batch_files(fingerprint)");
}

void DatabaseService::initAndStart(const Json::Value &config) { init(); }

void DatabaseService::shutdown() {
  if (db_) {
    sqlite3_close(db_);
  }
}
