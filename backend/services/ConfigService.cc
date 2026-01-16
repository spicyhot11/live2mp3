#include "ConfigService.h"
#include <drogon/drogon.h>
#include <fstream>

using json = nlohmann::json;

// JSON Serialization helpers
void to_json(json &j, const ScannerConfig &p) {
  j = json{{"video_roots", p.video_roots},
           {"extensions", p.extensions},
           {"allow_list", p.allow_list},
           {"deny_list", p.deny_list},
           {"simple_allow_list", p.simple_allow_list},
           {"simple_deny_list", p.simple_deny_list}};
}

void from_json(const json &j, ScannerConfig &p) {
  if (j.contains("video_roots"))
    j.at("video_roots").get_to(p.video_roots);
  if (j.contains("extensions"))
    j.at("extensions").get_to(p.extensions);
  if (j.contains("allow_list"))
    j.at("allow_list").get_to(p.allow_list);
  if (j.contains("deny_list"))
    j.at("deny_list").get_to(p.deny_list);
  if (j.contains("simple_allow_list"))
    j.at("simple_allow_list").get_to(p.simple_allow_list);
  if (j.contains("simple_deny_list"))
    j.at("simple_deny_list").get_to(p.simple_deny_list);
}

void to_json(json &j, const OutputConfig &p) {
  j = json{{"output_root", p.output_root}, {"keep_original", p.keep_original}};
}

void from_json(const json &j, OutputConfig &p) {
  j.at("output_root").get_to(p.output_root);
  j.at("keep_original").get_to(p.keep_original);
}

void to_json(json &j, const SchedulerConfig &p) {
  j = json{{"scan_interval_seconds", p.scan_interval_seconds},
           {"merge_window_hours", p.merge_window_hours}};
}

void from_json(const json &j, SchedulerConfig &p) {
  j.at("scan_interval_seconds").get_to(p.scan_interval_seconds);
  j.at("merge_window_hours").get_to(p.merge_window_hours);
}

void ConfigService::loadConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  auto lpPath = configPath_.get();
  try {
    std::ifstream i(*lpPath);
    if (!i.is_open()) {
      LOG_ERROR << "Could not open config file: " << *lpPath;
      return;
    }
    json j;
    i >> j;

    if (j.contains("app")) {
      auto app = j.at("app");
      if (app.contains("scanner"))
        app.at("scanner").get_to(currentConfig_.scanner);
      if (app.contains("output"))
        app.at("output").get_to(currentConfig_.output);
      if (app.contains("scheduler"))
        app.at("scheduler").get_to(currentConfig_.scheduler);

      // Also check for server_port in 'app' section
      if (app.contains("server_port")) {
        currentConfig_.server_port = app.at("server_port").get<int>();
      }
    }

    // Fallback: Load port from listeners if not specified in 'app'
    if (currentConfig_.server_port == 8080 && j.contains("listeners") &&
        j["listeners"].is_array() && !j["listeners"].empty()) {
      currentConfig_.server_port = j["listeners"][0].value("port", 8080);
    }
    LOG_INFO << "Config loaded successfully, server_port: "
             << currentConfig_.server_port;
  } catch (const std::exception &e) {
    LOG_ERROR << "Error loading config: " << e.what();
  }
}

void ConfigService::saveConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  auto lpPath = configPath_.get();
  try {
    // Read existing config first to preserve other fields (like listeners,
    // logging)
    json j;
    std::ifstream i(*lpPath);
    if (i.is_open()) {
      i >> j;
    }
    i.close();

    // Update the 'app' section
    j["app"]["scanner"] = currentConfig_.scanner;
    j["app"]["output"] = currentConfig_.output;
    j["app"]["scheduler"] = currentConfig_.scheduler;
    j["app"]["server_port"] = currentConfig_.server_port;

    // Remove old listeners format if it exists to keep it clean
    j.erase("listeners");

    std::ofstream o(*lpPath);
    o << j.dump(4);
    LOG_INFO << "Config saved successfully";
  } catch (const std::exception &e) {
    LOG_ERROR << "Error saving config: " << e.what();
  }
}

AppConfig ConfigService::getConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  return currentConfig_;
}

void ConfigService::updateConfig(const AppConfig &newConfig) {
  std::lock_guard<std::mutex> lock(configMutex_);
  currentConfig_ = newConfig;
  // Optionally trigger save here or let caller do it
}

void ConfigService::initAndStart(const Json::Value &config) {
  std::string path = config["config_path"].asString();

  if (path.empty()) {
    path = "./config.json";
    LOG_DEBUG << "Config path not found in config, use default path: " << path;
  }
  configPath_.set(path);

  loadConfig(); // Use the path from config

  // Bind port as requested
  LOG_INFO << "Server binding to 0.0.0.0:" << currentConfig_.server_port;
  drogon::app().addListener("0.0.0.0", currentConfig_.server_port);
}
void ConfigService::shutdown() { saveConfig(); }

nlohmann::json ConfigService::toJson() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  json j;
  j["server_port"] = currentConfig_.server_port;
  j["scanner"] = currentConfig_.scanner;
  j["output"] = currentConfig_.output;
  j["scheduler"] = currentConfig_.scheduler;
  return j;
}
