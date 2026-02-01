#pragma once

#include "utils/ThreadSafe.hpp"
#include <drogon/plugins/Plugin.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

/**
 * @brief 文件过滤规则结构体
 */
struct FilterRule {
  std::string pattern;
  std::string type; // "exact"(精确), "regex"(正则), "glob"(通配符)
};

/**
 * @brief 视频根目录配置结构体
 */
struct VideoRootConfig {
  std::string path;
  std::string filter_mode; // "whitelist"(白名单), "blacklist"(黑名单)
  std::vector<FilterRule> rules;

  // 源文件删除配置
  bool enable_delete = false;
  std::string delete_mode; // "whitelist"(白名单), "blacklist"(黑名单)
  std::vector<FilterRule> delete_rules;
};

/**
 * @brief 扫描器配置结构体
 */
struct ScannerConfig {
  std::vector<VideoRootConfig> video_roots; // 视频根目录列表
  std::vector<std::string> extensions;      // 关心的文件扩展名
};

/**
 * @brief 输出配置结构体
 */
struct OutputConfig {
  std::string output_root;
  bool keep_original;
};

/**
 * @brief 调度器配置结构体
 */
struct SchedulerConfig {
  int scan_interval_seconds;
  int merge_window_seconds = 7200; // 合并时间窗口(秒)，同一录播相邻片段最大间隔
  int stop_waiting_seconds =
      600;                     // 结束等待时间(秒)，最后片段超过此时间则开始合并
  int stability_checks = 2;    // 稳定性检查次数(连续MD5一致次数)
  int ffmpeg_worker_count = 4; // FFmpeg 并发 Worker 数量
};

/**
 * @brief 临时文件配置结构体
 */
struct TempConfig {
  std::string temp_dir;      // 临时目录路径
  int64_t size_limit_mb = 0; // 大小限制(MB)，0表示无限制
};

/**
 * @brief 应用全局配置结构体
 */
struct AppConfig {
  int server_port = 8080;
  ScannerConfig scanner;
  OutputConfig output;
  SchedulerConfig scheduler;
  TempConfig temp;
};

// JSON 序列化辅助函数
void to_json(nlohmann::json &j, const FilterRule &p);
void from_json(const nlohmann::json &j, FilterRule &p);
void to_json(nlohmann::json &j, const VideoRootConfig &p);
void from_json(const nlohmann::json &j, VideoRootConfig &p);
void to_json(nlohmann::json &j, const ScannerConfig &p);
void from_json(const nlohmann::json &j, ScannerConfig &p);
void to_json(nlohmann::json &j, const OutputConfig &p);
void from_json(const nlohmann::json &j, OutputConfig &p);
void to_json(nlohmann::json &j, const SchedulerConfig &p);
void from_json(const nlohmann::json &j, SchedulerConfig &p);
void to_json(nlohmann::json &j, const TempConfig &p);
void from_json(const nlohmann::json &j, TempConfig &p);

/**
 * @brief 配置管理服务类
 *
 * 负责加载、保存和提供应用程序的配置信息。
 * 也是整个应用配置的单一数据源。
 */
class ConfigService : public drogon::Plugin<ConfigService> {
public:
  ConfigService() = default;
  ~ConfigService() = default;
  ConfigService(const ConfigService &) = delete;
  ConfigService &operator=(const ConfigService &) = delete;

  // 初始化并启动插件
  void initAndStart(const Json::Value &config) override;
  // 关闭插件
  void shutdown() override;

  /**
   * @brief 从磁盘加载配置
   */
  void loadConfig();

  /**
   * @brief 保存配置到磁盘
   */
  void saveConfig();

  /**
   * @brief 获取当前配置副本
   *
   * @return AppConfig 当前配置对象
   */
  AppConfig getConfig();

  /**
   * @brief 更新配置
   *
   * 更新内存中的配置并保存到磁盘。
   *
   * @param newConfig 新的配置对象
   */
  void updateConfig(const AppConfig &newConfig);

  /**
   * @brief 序列化配置为JSON
   *
   * @return nlohmann::json JSON格式的配置
   */
  nlohmann::json toJson() const;

private:
  mutable std::mutex configMutex_;
  AppConfig currentConfig_;

  // 线程安全的配置路径管理，从本地加载的文件路径
  live2mp3::utils::ThreadSafeString configPath_;
};
