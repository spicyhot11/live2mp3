#pragma once

#include "ConfigService.h"
#include "services/PendingFileService.h"
#include <drogon/plugins/Plugin.h>
#include <optional>
#include <string>

/**
 * @brief 媒体转换服务类
 *
 * 负责音频和视频文件的转换操作，包括视频转AV1格式MP4、从视频提取MP3等。
 * 主要对FFmpeg命令进行封装。
 */
class ConverterService : public drogon::Plugin<ConverterService> {
public:
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  ConverterService() = default;
  ConverterService(const ConverterService &) = delete;
  ConverterService(ConverterService &&) = delete;
  ConverterService &operator=(const ConverterService &) = delete;
  ConverterService &operator=(ConverterService &&) = delete;

  /**
   * @brief 将单个文件转换为MP3
   *
   * @param inputPath 输入文件路径
   * @return std::optional<std::string>
   * 转换成功返回输出文件路径，失败返回nullopt
   */
  std::optional<std::string> convertToMp3(const std::string &inputPath);

  /**
   * @brief 将视频文件转换为AV1编码的MP4
   *
   * @param inputPath 输入视频路径
   * @param outputDir 输出目录，留空则使用配置的临时目录或输出根目录
   * @return std::optional<std::string>
   * 转换成功返回输出文件路径，失败返回nullopt
   */
  std::optional<std::string> convertToAv1Mp4(const std::string &inputPath,
                                             const std::string &outputDir = "");

  /**
   * @brief 从视频文件中提取MP3
   *
   * 通常在合并操作完成后调用。
   *
   * @param videoPath 视频文件路径
   * @param outputDir 输出目录
   * @return std::optional<std::string> 成功返回MP3文件路径，失败返回nullopt
   */
  std::optional<std::string>
  extractMp3FromVideo(const std::string &videoPath,
                      const std::string &outputDir = "");

  /**
   * @brief 获取临时目录使用量(字节)
   *
   * @return uint64_t 已使用的字节数
   */
  uint64_t getTempDirUsage();

  /**
   * @brief 检查临时目录是否有足够空间
   *
   * @param requiredBytes 需要的字节数
   * @return true 空间足够
   * @return false 空间不足
   */
  bool hasTempSpace(uint64_t requiredBytes);

private:
  ConfigService *configServicePtr = nullptr;
  PendingFileService *pendingFileServicePtr = nullptr;

  std::string determineOutputPath(const std::string &inputPath,
                                  const std::string &outputRoot);
  std::string determineOutputPathWithExt(const std::string &inputPath,
                                         const std::string &outputRoot,
                                         const std::string &ext);
  bool runFfmpeg(const std::string &cmd);
};
