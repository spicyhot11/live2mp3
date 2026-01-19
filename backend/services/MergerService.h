#pragma once

#include "ConfigService.h"
#include <chrono>
#include <drogon/plugins/Plugin.h>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief 文件合并服务类
 *
 * 负责将多个视频文件合并为一个文件。通常用于将同一时间段内的多个片段
 * 合并为一个完整的录播文件。
 */
class MergerService : public drogon::Plugin<MergerService> {
public:
  MergerService() = default;
  MergerService(const MergerService &) = delete;
  MergerService(MergerService &&) = delete;
  MergerService &operator=(const MergerService &) = delete;
  MergerService &operator=(MergerService &&) = delete;
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  /**
   * @brief 合并多个视频文件
   *
   * @param files 待合并的文件路径列表
   * @param outputDir 输出目录
   * @return std::optional<std::string>
   * 成功返回合并后的文件路径，失败返回nullopt
   */
  std::optional<std::string>
  mergeVideoFiles(const std::vector<std::string> &files,
                  const std::string &outputDir);

  /**
   * @brief 从文件名解析时间
   *
   * 尝试从文件名中解析出时间戳，用于文件排序和分组。
   *
   * @param filename 文件名
   * @return std::optional<std::chrono::system_clock::time_point> 解析出的时间点
   */
  static std::optional<std::chrono::system_clock::time_point>
  parseTime(const std::string &filename);

private:
  ConfigService *configServicePtr = nullptr;

  bool runFfmpegConcat(const std::string &listPath,
                       const std::string &outputPath);
};
