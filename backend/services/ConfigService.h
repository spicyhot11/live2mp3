#pragma once

#include "utils/ThreadSafe.hpp"
#include <drogon/plugins/Plugin.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

struct FilterRule {
  std::string pattern;
  std::string type; // "exact", "regex", "glob"
};

struct VideoRootConfig {
  std::string path;
  std::string filter_mode; // "whitelist", "blacklist"
  std::vector<FilterRule> rules;

  // Source deletion config
  bool enable_delete = false;
  std::string delete_mode; // "whitelist", "blacklist"
  std::vector<FilterRule> delete_rules;
};

struct ScannerConfig {
  std::vector<VideoRootConfig> video_roots; // Replaces simple string vector
  std::vector<std::string> extensions;
  // Legacy fields removed or deprecated.
  // We will migarte them during load if present, but not keep them in runtime
  // struct
};

struct OutputConfig {
  std::string output_root;
  bool keep_original;
};

struct SchedulerConfig {
  int scan_interval_seconds;
  int merge_window_hours;
};

struct AppConfig {
  int server_port = 8080;
  ScannerConfig scanner;
  OutputConfig output;
  SchedulerConfig scheduler;
};

// JSON Serialization
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

class ConfigService : public drogon::Plugin<ConfigService> {
public:
  ConfigService() = default;
  ~ConfigService() = default;
  ConfigService(const ConfigService &) = delete;
  ConfigService &operator=(const ConfigService &) = delete;

  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  void loadConfig();
  void saveConfig();

  AppConfig getConfig();
  void updateConfig(const AppConfig &newConfig);

  // Helper for serialization
  nlohmann::json toJson() const;

private:
  mutable std::mutex configMutex_;
  AppConfig currentConfig_;

  // Thread-safe config path   从本地加载的文件路径
  utils::ThreadSafeString configPath_;
};
