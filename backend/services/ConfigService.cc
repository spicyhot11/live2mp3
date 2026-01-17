#include "ConfigService.h"
#include <drogon/drogon.h>
#include <fstream>

using json = nlohmann::json;

// ============================================================
// JSON Serialization helpers (kept for API responses)
// ============================================================
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

// ============================================================
// TOML Helper: Extract string array from TOML array
// ============================================================
static std::vector<std::string> tomlArrayToStringVec(const toml::array *arr) {
  std::vector<std::string> result;
  if (arr) {
    for (const auto &elem : *arr) {
      if (auto str = elem.value<std::string>()) {
        result.push_back(*str);
      }
    }
  }
  return result;
}

// ============================================================
// TOML Helper: Convert string vector to TOML array
// ============================================================
static toml::array stringVecToTomlArray(const std::vector<std::string> &vec) {
  toml::array arr;
  for (const auto &s : vec) {
    arr.push_back(s);
  }
  return arr;
}

// ============================================================
// ConfigService: Load config from TOML file
// ============================================================
void ConfigService::loadConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  auto lpPath = configPath_.get();
  try {
    toml::table tbl = toml::parse_file(*lpPath);

    // Scanner config
    if (auto scanner = tbl["scanner"].as_table()) {
      currentConfig_.scanner.video_roots =
          tomlArrayToStringVec((*scanner)["video_roots"].as_array());
      currentConfig_.scanner.extensions =
          tomlArrayToStringVec((*scanner)["extensions"].as_array());
      currentConfig_.scanner.allow_list =
          tomlArrayToStringVec((*scanner)["allow_list"].as_array());
      currentConfig_.scanner.deny_list =
          tomlArrayToStringVec((*scanner)["deny_list"].as_array());
      currentConfig_.scanner.simple_allow_list =
          tomlArrayToStringVec((*scanner)["simple_allow_list"].as_array());
      currentConfig_.scanner.simple_deny_list =
          tomlArrayToStringVec((*scanner)["simple_deny_list"].as_array());
    }

    // Output config
    if (auto output = tbl["output"].as_table()) {
      currentConfig_.output.output_root =
          (*output)["output_root"].value_or(std::string("./output"));
      currentConfig_.output.keep_original =
          (*output)["keep_original"].value_or(false);
    }

    // Scheduler config
    if (auto scheduler = tbl["scheduler"].as_table()) {
      currentConfig_.scheduler.scan_interval_seconds =
          (*scheduler)["scan_interval_seconds"].value_or(60);
      currentConfig_.scheduler.merge_window_hours =
          (*scheduler)["merge_window_hours"].value_or(2);
    }

    // Server port (optional in user config)
    if (tbl.contains("server_port")) {
      currentConfig_.server_port = tbl["server_port"].value_or(8080);
    }

    LOG_INFO << "TOML config loaded successfully from: " << *lpPath;
  } catch (const toml::parse_error &e) {
    LOG_ERROR << "TOML parse error in " << *lpPath << ": " << e.description();
  } catch (const std::exception &e) {
    LOG_ERROR << "Error loading config: " << e.what();
  }
}

// ============================================================
// ConfigService: Save config to TOML file
// ============================================================
void ConfigService::saveConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  auto lpPath = configPath_.get();
  try {
    toml::table tbl;

    // Scanner section
    tbl.insert_or_assign(
        "scanner",
        toml::table{
            {"video_roots",
             stringVecToTomlArray(currentConfig_.scanner.video_roots)},
            {"extensions",
             stringVecToTomlArray(currentConfig_.scanner.extensions)},
            {"allow_list",
             stringVecToTomlArray(currentConfig_.scanner.allow_list)},
            {"deny_list",
             stringVecToTomlArray(currentConfig_.scanner.deny_list)},
            {"simple_allow_list",
             stringVecToTomlArray(currentConfig_.scanner.simple_allow_list)},
            {"simple_deny_list",
             stringVecToTomlArray(currentConfig_.scanner.simple_deny_list)}});

    // Output section
    tbl.insert_or_assign(
        "output",
        toml::table{{"output_root", currentConfig_.output.output_root},
                    {"keep_original", currentConfig_.output.keep_original}});

    // Scheduler section
    tbl.insert_or_assign(
        "scheduler",
        toml::table{{"scan_interval_seconds",
                     currentConfig_.scheduler.scan_interval_seconds},
                    {"merge_window_hours",
                     currentConfig_.scheduler.merge_window_hours}});

    std::ofstream file(*lpPath);
    if (!file.is_open()) {
      LOG_ERROR << "Could not open config file for writing: " << *lpPath;
      return;
    }
    file << tbl;
    LOG_INFO << "TOML config saved successfully to: " << *lpPath;
  } catch (const std::exception &e) {
    LOG_ERROR << "Error saving config: " << e.what();
  }
}

// ============================================================
// ConfigService: Accessors
// ============================================================
AppConfig ConfigService::getConfig() {
  std::lock_guard<std::mutex> lock(configMutex_);
  return currentConfig_;
}

void ConfigService::updateConfig(const AppConfig &newConfig) {
  std::lock_guard<std::mutex> lock(configMutex_);
  currentConfig_ = newConfig;
}

// ============================================================
// ConfigService: Drogon Plugin Interface
// ============================================================
void ConfigService::initAndStart(const Json::Value &config) {
  std::string path = config["config_path"].asString();

  if (path.empty()) {
    path = "./user_config.toml";
    LOG_DEBUG << "Config path not found in config, use default path: " << path;
  }
  configPath_.set(path);

  loadConfig();

  LOG_INFO << "ConfigService initialized, config loaded from: " << path;
}

void ConfigService::shutdown() { saveConfig(); }

// ============================================================
// ConfigService: JSON export for API responses
// ============================================================
nlohmann::json ConfigService::toJson() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  json j;
  j["server_port"] = currentConfig_.server_port;
  j["scanner"] = currentConfig_.scanner;
  j["output"] = currentConfig_.output;
  j["scheduler"] = currentConfig_.scheduler;
  return j;
}
