#pragma once

#include <drogon/drogon.h>
#include <mutex>
#include <sqlite3.h>
#include <string>

class DatabaseService : public drogon::Plugin<DatabaseService> {
public:
  DatabaseService() = default;
  ~DatabaseService() = default;
  DatabaseService(const DatabaseService &) = delete;
  DatabaseService &operator=(const DatabaseService &) = delete;

  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  static DatabaseService &getInstance();

  void init(const std::string &dbPath = "live2mp3.db");
  sqlite3 *getDb();

  // Helper to execute simple queries
  bool executeQuery(const std::string &query);

private:
  void initSchema();

  sqlite3 *db_ = nullptr;
  std::mutex mutex_;
};
