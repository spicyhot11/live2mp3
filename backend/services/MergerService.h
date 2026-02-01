#pragma once

#include "../utils/FfmpegUtils.h"
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
 * 合并为一个完整的录播文件。支持进度回调。
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
   * 使用 FFmpeg concat 协议将多个视频文件合并为一个文件。
   * 要求所有输入文件具有相同的编码格式。
   *
   * @param files 待合并的文件路径列表
   * @param outputDir 输出目录
   * @param progressCallback 可选的进度回调，实时报告合并进度
   * @return std::optional<std::string>
   * 成功返回合并后的文件路径，失败返回nullopt
   */
  std::optional<std::string> mergeVideoFiles(
      const std::vector<std::string> &files, const std::string &outputDir,
      live2mp3::utils::FfmpegProgressCallback progressCallback = nullptr);

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

  /**
   * @brief 从文件名解析主播名（用于分组）
   *
   * 支持两种格式：
   * - 格式1: "[时间戳][主播名][标题].flv/ts" -> 提取主播名
   * - 格式2: "录制-主播名-时间戳-xxx-标题.flv" -> 提取主播名
   *
   * @param filename 文件名
   * @return std::string 主播名，无法解析时返回空字符串
   */
  static std::string parseTitle(const std::string &filename);

private:
  std::shared_ptr<ConfigService> configServicePtr;
};
