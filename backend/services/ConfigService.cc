#include "ConfigService.h"
#include <drogon/drogon.h>
#include <fstream>

using json = nlohmann::json;

// ============================================================
// JSON Serialization helpers (kept for API responses)
// ============================================================
// ============================================================
// JSON Serialization helpers (kept for API responses)
// ============================================================
void to_json(json &j, const FilterRule &p) {
  j = json{{"pattern", p.pattern}, {"type", p.type}};
}

void from_json(const json &j, FilterRule &p) {
  j.at("pattern").get_to(p.pattern);
  j.at("type").get_to(p.type);
}

void to_json(json &j, const VideoRootConfig &p) {
  j = json{{"path", p.path},
           {"filter_mode", p.filter_mode},
           {"rules", p.rules},
           {"enable_delete", p.enable_delete},
           {"delete_mode", p.delete_mode},
           {"delete_rules", p.delete_rules}};
}

void from_json(const json &j, VideoRootConfig &p) {
  j.at("path").get_to(p.path);
  j.at("filter_mode").get_to(p.filter_mode);
  if (j.contains("rules"))
    j.at("rules").get_to(p.rules);

  if (j.contains("enable_delete"))
    j.at("enable_delete").get_to(p.enable_delete);
  if (j.contains("delete_mode"))
    j.at("delete_mode").get_to(p.delete_mode);
  if (j.contains("delete_rules"))
    j.at("delete_rules").get_to(p.delete_rules);
}

void to_json(json &j, const ScannerConfig &p) {
  j = json{{"video_roots", p.video_roots}, {"extensions", p.extensions}};
}

void from_json(const json &j, ScannerConfig &p) {
  if (j.contains("video_roots"))
    j.at("video_roots").get_to(p.video_roots);
  if (j.contains("extensions"))
    j.at("extensions").get_to(p.extensions);
}

void to_json(json &j, const OutputConfig &p) {
  j = json{
      {"output_root", p.output_root},
      {"keep_original", p.keep_original},
      {"video_extension", p.video_extension},
      {"audio_extension", p.audio_extension},
  };
}

void from_json(const json &j, OutputConfig &p) {
  if (j.contains("output_root"))
    j.at("output_root").get_to(p.output_root);
  if (j.contains("keep_original"))
    j.at("keep_original").get_to(p.keep_original);
  if (j.contains("video_extension"))
    j.at("video_extension").get_to(p.video_extension);
  if (j.contains("audio_extension"))
    j.at("audio_extension").get_to(p.audio_extension);
}

void to_json(json &j, const SchedulerConfig &p) {
  j = json{{"scan_interval_seconds", p.scan_interval_seconds},
           {"merge_window_seconds", p.merge_window_seconds},
           {"stop_waiting_seconds", p.stop_waiting_seconds},
           {"stability_checks", p.stability_checks},
           {"ffmpeg_worker_count", p.ffmpeg_worker_count},
           {"ffmpeg_retry_count", p.ffmpeg_retry_count}};
}

void from_json(const json &j, SchedulerConfig &p) {
  j.at("scan_interval_seconds").get_to(p.scan_interval_seconds);
  j.at("merge_window_seconds").get_to(p.merge_window_seconds);
  if (j.contains("stop_waiting_seconds"))
    j.at("stop_waiting_seconds").get_to(p.stop_waiting_seconds);
  if (j.contains("stability_checks"))
    j.at("stability_checks").get_to(p.stability_checks);
  if (j.contains("ffmpeg_worker_count"))
    j.at("ffmpeg_worker_count").get_to(p.ffmpeg_worker_count);
  if (j.contains("ffmpeg_retry_count"))
    j.at("ffmpeg_retry_count").get_to(p.ffmpeg_retry_count);
}

void to_json(json &j, const TempConfig &p) {
  j = json{{"temp_dir", p.temp_dir}, {"size_limit_mb", p.size_limit_mb}};
}

void from_json(const json &j, TempConfig &p) {
  if (j.contains("temp_dir"))
    j.at("temp_dir").get_to(p.temp_dir);
  if (j.contains("size_limit_mb"))
    j.at("size_limit_mb").get_to(p.size_limit_mb);
}

void to_json(json &j, const FfmpegConfig &p) {
  j = json{{"video_convert_command", p.video_convert_command},
           {"audio_convert_command", p.audio_convert_command},
           {"merge_command", p.merge_command}};
}

void from_json(const json &j, FfmpegConfig &p) {
  if (j.contains("video_convert_command"))
    j.at("video_convert_command").get_to(p.video_convert_command);
  if (j.contains("audio_convert_command"))
    j.at("audio_convert_command").get_to(p.audio_convert_command);
  if (j.contains("merge_command"))
    j.at("merge_command").get_to(p.merge_command);
}

void to_json(json &j, const CommonThreadConfig &p) {
  j = json{{"threadCount", p.threadCount}, {"name", p.name}};
}
void from_json(const json &j, CommonThreadConfig &p) {
  if (j.contains("threadCount"))
    j.at("threadCount").get_to(p.threadCount);
  if (j.contains("name"))
    j.at("name").get_to(p.name);
}

void to_json(json &j, const FfmpegTaskConfig &p) {
  j = json{{"maxConcurrentTasks", p.maxConcurrentTasks},
           {"maxWaitingTasks", p.maxWaitingTasks},
           {"taskTimeoutSeconds", p.taskTimeoutSeconds}};
}
void from_json(const json &j, FfmpegTaskConfig &p) {
  if (j.contains("maxConcurrentTasks"))
    j.at("maxConcurrentTasks").get_to(p.maxConcurrentTasks);
  if (j.contains("maxWaitingTasks"))
    j.at("maxWaitingTasks").get_to(p.maxWaitingTasks);
  if (j.contains("taskTimeoutSeconds"))
    j.at("taskTimeoutSeconds").get_to(p.taskTimeoutSeconds);
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
    currentConfig_.scanner.video_roots.clear();

    // Scanner config
    if (auto scanner = tbl["scanner"].as_table()) {
      auto roots_node = (*scanner)["video_roots"];

      if (auto roots_arr = roots_node.as_array()) {
        // Check for legacy format (array of strings)
        bool isLegacy = false;
        if (!roots_arr->empty() && roots_arr->front().is_string()) {
          isLegacy = true;
        }

        if (isLegacy) {
          LOG_INFO << "Detected legacy config format, migrating...";
          auto paths = tomlArrayToStringVec(roots_arr);
          for (const auto &path : paths) {
            VideoRootConfig videoRoot;
            videoRoot.path = path;
            videoRoot.filter_mode = "blacklist";
            // In legacy migration, we don't migrate complex rules as they
            // didn't exist in this structure
            currentConfig_.scanner.video_roots.push_back(videoRoot);
          }
        } else {
          // New format: array of tables
          for (const auto &elem : *roots_arr) {
            if (auto rootTable = elem.as_table()) {
              VideoRootConfig rc;
              rc.path = rootTable->at("path").value_or("");
              rc.filter_mode =
                  rootTable->at("filter_mode").value_or("blacklist");

              if (auto rulesArr = rootTable->at("rules").as_array()) {
                for (const auto &r : *rulesArr) {
                  if (auto ruleTbl = r.as_table()) {
                    FilterRule rule;
                    rule.pattern = ruleTbl->at("pattern").value_or("");
                    rule.type = ruleTbl->at("type").value_or("exact");
                    rc.rules.push_back(rule);
                  }
                }
              }

              rc.enable_delete = rootTable->at("enable_delete").value_or(false);
              rc.delete_mode =
                  rootTable->at("delete_mode").value_or("blacklist");

              if (auto dRulesArr = rootTable->at("delete_rules").as_array()) {
                for (const auto &r : *dRulesArr) {
                  if (auto ruleTbl = r.as_table()) {
                    FilterRule rule;
                    rule.pattern = ruleTbl->at("pattern").value_or("");
                    rule.type = ruleTbl->at("type").value_or("exact");
                    rc.delete_rules.push_back(rule);
                  }
                }
              }
              currentConfig_.scanner.video_roots.push_back(rc);
            }
          }
        }
      }

      currentConfig_.scanner.extensions =
          tomlArrayToStringVec((*scanner)["extensions"].as_array());
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
      currentConfig_.scheduler.merge_window_seconds =
          (*scheduler)["merge_window_seconds"].value_or(7200);
      currentConfig_.scheduler.stop_waiting_seconds =
          (*scheduler)["stop_waiting_seconds"].value_or(600);
      currentConfig_.scheduler.stability_checks =
          (*scheduler)["stability_checks"].value_or(2);
      currentConfig_.scheduler.ffmpeg_worker_count =
          (*scheduler)["ffmpeg_worker_count"].value_or(4);
      currentConfig_.scheduler.ffmpeg_retry_count =
          (*scheduler)["ffmpeg_retry_count"].value_or(3);
    }

    // Temp config
    if (auto temp = tbl["temp"].as_table()) {
      currentConfig_.temp.temp_dir =
          (*temp)["temp_dir"].value_or(std::string(""));
      currentConfig_.temp.size_limit_mb =
          (*temp)["size_limit_mb"].value_or(static_cast<int64_t>(0));
    }

    // Ffmpeg config
    if (auto ffmpeg = tbl["ffmpeg"].as_table()) {
      currentConfig_.ffmpeg.video_convert_command =
          (*ffmpeg)["video_convert_command"].value_or(
              std::string("ffmpeg -y -i \"{input}\" -c:v libsvtav1 -crf 30 "
                          "-preset 6 -c:a aac -b:a 128k \"{output}\" 2>&1"));
      currentConfig_.ffmpeg.audio_convert_command =
          (*ffmpeg)["audio_convert_command"].value_or(std::string(
              "ffmpeg -y -i \"{input}\" -vn -acodec libmp3lame -q:a 2 "
              "\"{output}\" 2>&1"));
      currentConfig_.ffmpeg.merge_command = (*ffmpeg)["merge_command"].value_or(
          std::string("ffmpeg -f concat -safe 0 -i \"{input}\" -c copy -y "
                      "\"{output}\" 2>&1"));

      // Validation: Check for {input} and {output} placeholders
      auto validateCommand = [](std::string &cmd, const std::string &defaultCmd,
                                const std::string &name) {
        if (cmd.find("{input}") == std::string::npos ||
            cmd.find("{output}") == std::string::npos) {
          LOG_ERROR << "Invalid FFmpeg command format for " << name
                    << ": missing {input} or {output} placeholder. Reverting "
                       "to default.";
          cmd = defaultCmd;
        }
        LOG_DEBUG << "FFmpeg command for " << name << ": " << cmd;
      };

      std::string defaultVideo =
          "ffmpeg -y -i \"{input}\" -c:v libsvtav1 -crf 30 -preset 6 -c:a aac "
          "-b:a 128k \"{output}\" 2>&1";
      std::string defaultAudio =
          "ffmpeg -y -i \"{input}\" -vn -acodec libmp3lame -q:a 2 \"{output}\" "
          "2>&1";
      std::string defaultMerge =
          "ffmpeg -f concat -safe 0 -i \"{input}\" -c copy -y \"{output}\" "
          "2>&1";

      validateCommand(currentConfig_.ffmpeg.video_convert_command, defaultVideo,
                      "video_convert_command");
      validateCommand(currentConfig_.ffmpeg.audio_convert_command, defaultAudio,
                      "audio_convert_command");
      validateCommand(currentConfig_.ffmpeg.merge_command, defaultMerge,
                      "merge_command");

    } else {
      // Default configurations if [ffmpeg] section is missing
      currentConfig_.ffmpeg.video_convert_command =
          "ffmpeg -y -i \"{input}\" -c:v libsvtav1 -crf 30 -preset 6 -c:a aac "
          "-b:a 128k \"{output}\" 2>&1";
      currentConfig_.ffmpeg.audio_convert_command =
          "ffmpeg -y -i \"{input}\" -vn -acodec libmp3lame -q:a 2 \"{output}\" "
          "2>&1";
      currentConfig_.ffmpeg.merge_command =
          "ffmpeg -f concat -safe 0 -i \"{input}\" -c copy -y \"{output}\" "
          "2>&1";

      LOG_DEBUG << "No [ffmpeg] section found in config. Using default FFmpeg "
                   "commands.";
      LOG_DEBUG << "video_convert_command: "
                << currentConfig_.ffmpeg.video_convert_command;
      LOG_DEBUG << "audio_convert_command: "
                << currentConfig_.ffmpeg.audio_convert_command;
      LOG_DEBUG << "merge_command: " << currentConfig_.ffmpeg.merge_command;
    }

    // CommonThread config
    if (auto ct = tbl["common_thread"].as_table()) {
      currentConfig_.common_thread.threadCount =
          (*ct)["threadCount"].value_or(8);
      currentConfig_.common_thread.name =
          (*ct)["name"].value_or("CommonThreadPool");
    }

    // FfmpegTask config
    if (auto ft = tbl["ffmpeg_task"].as_table()) {
      currentConfig_.ffmpeg_task.maxConcurrentTasks =
          (*ft)["maxConcurrentTasks"].value_or(2);
      currentConfig_.ffmpeg_task.maxWaitingTasks =
          (*ft)["maxWaitingTasks"].value_or(10000);
      currentConfig_.ffmpeg_task.taskTimeoutSeconds =
          (*ft)["taskTimeoutSeconds"].value_or(600);
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
    toml::array rootsArr;
    for (const auto &root : currentConfig_.scanner.video_roots) {
      toml::table rootTable;
      rootTable.insert("path", root.path);
      rootTable.insert("filter_mode", root.filter_mode);

      toml::array rulesArr;
      for (const auto &rule : root.rules) {
        toml::table ruleTable;
        ruleTable.insert("pattern", rule.pattern);
        ruleTable.insert("type", rule.type);
        rulesArr.push_back(ruleTable);
      }
      rootTable.insert("rules", rulesArr);

      rootTable.insert("enable_delete", root.enable_delete);
      rootTable.insert("delete_mode", root.delete_mode);

      toml::array deleteRulesArr;
      for (const auto &rule : root.delete_rules) {
        toml::table ruleTable;
        ruleTable.insert("pattern", rule.pattern);
        ruleTable.insert("type", rule.type);
        deleteRulesArr.push_back(ruleTable);
      }
      rootTable.insert("delete_rules", deleteRulesArr);

      rootsArr.push_back(rootTable);
    }

    tbl.insert_or_assign(
        "scanner",
        toml::table{{"video_roots", rootsArr},
                    {"extensions",
                     stringVecToTomlArray(currentConfig_.scanner.extensions)}});

    // Output section
    tbl.insert_or_assign(
        "output",
        toml::table{{"output_root", currentConfig_.output.output_root},
                    {"keep_original", currentConfig_.output.keep_original}});

    // Scheduler section
    tbl.insert_or_assign(
        "scheduler",
        toml::table{
            {"scan_interval_seconds",
             currentConfig_.scheduler.scan_interval_seconds},
            {"merge_window_seconds",
             currentConfig_.scheduler.merge_window_seconds},
            {"stop_waiting_seconds",
             currentConfig_.scheduler.stop_waiting_seconds},
            {"stability_checks", currentConfig_.scheduler.stability_checks},
            {"ffmpeg_worker_count",
             currentConfig_.scheduler.ffmpeg_worker_count},
            {"ffmpeg_retry_count",
             currentConfig_.scheduler.ffmpeg_retry_count}});

    // Temp section
    tbl.insert_or_assign(
        "temp",
        toml::table{{"temp_dir", currentConfig_.temp.temp_dir},
                    {"size_limit_mb", currentConfig_.temp.size_limit_mb}});

    // Ffmpeg section
    tbl.insert_or_assign(
        "ffmpeg",
        toml::table{{"video_convert_command",
                     currentConfig_.ffmpeg.video_convert_command},
                    {"audio_convert_command",
                     currentConfig_.ffmpeg.audio_convert_command},
                    {"merge_command", currentConfig_.ffmpeg.merge_command}});

    // CommonThread section
    tbl.insert_or_assign(
        "common_thread",
        toml::table{{"threadCount", currentConfig_.common_thread.threadCount},
                    {"name", currentConfig_.common_thread.name}});

    // FfmpegTask section
    tbl.insert_or_assign(
        "ffmpeg_task",
        toml::table{
            {"maxConcurrentTasks",
             currentConfig_.ffmpeg_task.maxConcurrentTasks},
            {"maxWaitingTasks", currentConfig_.ffmpeg_task.maxWaitingTasks},
            {"taskTimeoutSeconds",
             currentConfig_.ffmpeg_task.taskTimeoutSeconds}});

    // Server port
    tbl.insert("server_port", currentConfig_.server_port);

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
  j["temp"] = currentConfig_.temp;
  j["ffmpeg"] = currentConfig_.ffmpeg;
  return j;
}
