#pragma once

#include <nlohmann/json.hpp>
#include <string>

// 状态值: "pending"(待处理), "stable"(稳定), "processing"(处理中),
// "staged"(已暂存), "completed"(已完成), "deprecated"(已废弃-同名文件中较小者)
struct PendingFile {
  int id;
  std::string dir_path;
  std::string filename;
  std::string fingerprint;
  int stable_count;
  std::string status;
  std::string temp_mp4_path;
  std::string temp_mp3_path;
  std::string start_time;
  std::string end_time;

  /// 获取完整文件路径
  std::string getFilepath() const;
};

void to_json(nlohmann::json &j, const PendingFile &p);
