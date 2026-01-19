#pragma once

#include <drogon/drogon.h>
#include <mutex>
#include <sqlite3.h>
#include <string>

/**
 * @brief 数据库服务类
 *
 * 管理SQLite数据库连接，处理数据库初始化和基础查询执行。
 */
class DatabaseService : public drogon::Plugin<DatabaseService> {
public:
  DatabaseService() = default;
  ~DatabaseService() = default;
  DatabaseService(const DatabaseService &) = delete;
  DatabaseService &operator=(const DatabaseService &) = delete;

  // 初始化并启动插件
  void initAndStart(const Json::Value &config) override;
  // 关闭插件
  void shutdown() override;

  // 获取单例实例
  static DatabaseService &getInstance();

  /**
   * @brief 初始化数据库
   *
   * @param dbPath 数据库文件路径，默认为 "live2mp3.db"
   */
  void init(const std::string &dbPath = "live2mp3.db");

  // 获取原始sqlite3连接指针
  sqlite3 *getDb();

  /**
   * @brief 执行简单的SQL查询
   *
   * @param query SQL查询字符串
   * @return true 执行成功
   * @return false 执行失败
   */
  bool executeQuery(const std::string &query);

private:
  // 初始化数据库Schema
  void initSchema();

  sqlite3 *db_ = nullptr;
  std::mutex mutex_;
};
