#pragma once

#include "PendingFile.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * @brief 批次信息结构体
 */
struct BatchInfo {
  int id;
  std::string streamer;
  std::string
      status; // encoding / merging / extracting_mp3 / completed / failed
  std::string output_dir;
  std::string tmp_dir;
  std::string final_mp4_path;
  std::string final_mp3_path;
  int total_files;
  int encoded_count;
  int failed_count;
};

/**
 * @brief 批次文件结构体
 */
struct BatchFile {
  int id;
  int batch_id;
  std::string dir_path;
  std::string filename;
  std::string fingerprint;
  int pending_file_id;
  std::string status; // pending / encoding / encoded / failed
  std::string encoded_path;
  int retry_count;

  /// 获取完整文件路径
  std::string getFilepath() const;
};

void to_json(nlohmann::json &j, const BatchInfo &b);
void to_json(nlohmann::json &j, const BatchFile &f);

/**
 * @brief 批次输入文件
 */
struct BatchInputFile {
  std::string filepath;
  std::string fingerprint;
  int pending_file_id;
};

/**
 * @brief 稳定文件（带解析后的时间）
 */
struct StableFile {
  PendingFile pf;
  std::chrono::system_clock::time_point time;
};

/**
 * @brief 批次分配结果
 */
struct BatchAssignment {
  int batchId; // -1 表示需要新建
  std::string streamer;
  std::vector<StableFile> files;
};
