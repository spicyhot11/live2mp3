#pragma once

#include "utils/ThreadSafe.hpp"
#include <drogon/plugins/Plugin.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

struct ScannerConfig {
  std::vector<std::string> video_roots;
  std::vector<std::string> extensions;
  std::vector<std::string> allow_list;        // Regex
  std::vector<std::string> deny_list;         // Regex
  std::vector<std::string> simple_allow_list; // Simple string match
  std::vector<std::string> simple_deny_list;  // Simple string match
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
  int server_port = 8080; // 端口配置
  ScannerConfig scanner;
  OutputConfig output;
  SchedulerConfig scheduler;
};

// JSON Serialization Declarations
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
