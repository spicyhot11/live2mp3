#pragma once

#include <drogon/drogon.h>
#include <functional>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

/**
 * @brief RAII 事务管理
 *
 * 自动在析构时回滚未提交的事务，避免遗漏 ROLLBACK。
 */
class ScopedTransaction {
public:
  explicit ScopedTransaction(sqlite3 *db) : db_(db) {}
  ~ScopedTransaction() {
    if (active_) {
      rollback();
    }
  }

  ScopedTransaction(const ScopedTransaction &) = delete;
  ScopedTransaction &operator=(const ScopedTransaction &) = delete;

  bool begin() {
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr,
                     nullptr) != SQLITE_OK) {
      LOG_ERROR << "[ScopedTransaction] Failed to begin: "
                << sqlite3_errmsg(db_);
      return false;
    }
    active_ = true;
    return true;
  }

  bool commit() {
    if (!active_)
      return false;
    if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
      LOG_ERROR << "[ScopedTransaction] Failed to commit: "
                << sqlite3_errmsg(db_);
      return false;
    }
    active_ = false;
    return true;
  }

  void rollback() {
    if (!active_)
      return;
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    active_ = false;
  }

  bool isActive() const { return active_; }

private:
  sqlite3 *db_;
  bool active_ = false;
};

/**
 * @brief 数据库服务类
 *
 * 管理SQLite数据库连接，处理数据库初始化和基础查询执行。
 * 提供通用的查询和更新方法，减少样板代码。
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
   * @brief 执行简单的SQL查询（无参数无返回值）
   */
  bool executeQuery(const std::string &query);

  // ============ 通用查询方法 ============

  /**
   * @brief 查询多行，通过 rowMapper 映射为对象
   */
  template <typename T>
  std::vector<T>
  queryAll(const std::string &sql, std::function<T(sqlite3_stmt *)> rowMapper,
           std::function<void(sqlite3_stmt *)> binder = nullptr) {
    std::vector<T> results;
    if (!db_)
      return results;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
      LOG_ERROR << "[queryAll] Failed to prepare: " << sqlite3_errmsg(db_);
      return results;
    }

    if (binder) {
      binder(stmt);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      results.push_back(rowMapper(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
  }

  /**
   * @brief 查询单行，通过 rowMapper 映射为对象
   */
  template <typename T>
  std::optional<T>
  queryOne(const std::string &sql, std::function<T(sqlite3_stmt *)> rowMapper,
           std::function<void(sqlite3_stmt *)> binder = nullptr) {
    if (!db_)
      return std::nullopt;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
      LOG_ERROR << "[queryOne] Failed to prepare: " << sqlite3_errmsg(db_);
      return std::nullopt;
    }

    if (binder) {
      binder(stmt);
    }

    std::optional<T> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      result = rowMapper(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
  }

  /**
   * @brief 查询标量值（如 COUNT）
   */
  int queryScalar(const std::string &sql,
                  std::function<void(sqlite3_stmt *)> binder = nullptr,
                  int defaultValue = 0);

  // ============ 通用更新方法 ============

  /**
   * @brief 执行参数化更新，返回是否成功
   */
  bool executeUpdate(const std::string &sql,
                     std::function<void(sqlite3_stmt *)> binder = nullptr);

  /**
   * @brief 执行参数化更新，返回受影响行数（-1 表示失败）
   */
  int executeUpdateCount(const std::string &sql,
                         std::function<void(sqlite3_stmt *)> binder = nullptr);

  /**
   * @brief 获取最后插入行的 ID
   */
  int lastInsertId();

private:
  // 初始化数据库Schema
  void initSchema();

  sqlite3 *db_ = nullptr;
  std::mutex mutex_;
};
