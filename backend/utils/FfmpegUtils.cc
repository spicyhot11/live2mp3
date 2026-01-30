/**
 * @file FfmpegUtils.cc
 * @brief FFmpeg 工具函数实现
 *
 * 提供 FFmpeg 命令执行和进度解析的公共功能。
 */

#include "FfmpegUtils.h"
#include <array>
#include <cstdio>
#include <drogon/drogon.h>
#include <regex>

namespace live2mp3::utils {

FfmpegPipeInfo parseFfmpegProgressLine(const std::string &line) {
  FfmpegPipeInfo info = {};

  // 解析 frame=XXX
  // 匹配格式: frame=  123 或 frame=123
  static std::regex frameRe(R"(frame=\s*(\d+))");
  std::smatch match;
  if (std::regex_search(line, match, frameRe)) {
    info.frame = std::stoi(match[1].str());
  }

  // 解析 fps=XXX
  // 匹配格式: fps= 25.0 或 fps=25
  static std::regex fpsRe(R"(fps=\s*([\d.]+))");
  if (std::regex_search(line, match, fpsRe)) {
    info.fps = static_cast<int>(std::stof(match[1].str()));
  }

  // 解析 size=XXXkB
  // 匹配格式: size=   1234kB
  static std::regex sizeRe(R"(size=\s*(\d+)kB)");
  if (std::regex_search(line, match, sizeRe)) {
    info.size = std::stoi(match[1].str()) * 1024; // 转换为字节
  }

  // 解析 time=HH:MM:SS.xx -> 转换为毫秒
  // 匹配格式: time=00:00:05.00
  static std::regex timeRe(R"(time=(\d{2}):(\d{2}):(\d{2})\.(\d{2}))");
  if (std::regex_search(line, match, timeRe)) {
    int hours = std::stoi(match[1].str());
    int minutes = std::stoi(match[2].str());
    int seconds = std::stoi(match[3].str());
    int centiseconds = std::stoi(match[4].str());
    info.time =
        (hours * 3600 + minutes * 60 + seconds) * 1000 + centiseconds * 10;
  }

  // 解析 bitrate=XXX.Xkbits/s
  // 匹配格式: bitrate= 123.4kbits/s
  static std::regex bitrateRe(R"(bitrate=\s*([\d.]+)kbits/s)");
  if (std::regex_search(line, match, bitrateRe)) {
    info.bitrate = static_cast<int>(std::stof(match[1].str()));
  }

  return info;
}

bool runFfmpegWithProgress(const std::string &cmd,
                           FfmpegProgressCallback callback) {
  std::array<char, 256> buffer;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    LOG_ERROR << "popen() 执行 FFmpeg 命令失败";
    return false;
  }

  // 用于累积不完整的行
  std::string lineBuffer;

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    std::string chunk(buffer.data());
    lineBuffer += chunk;

    // FFmpeg 的进度输出以 \r 或 \n 结尾
    // 逐行处理，避免管道缓冲区被填满
    size_t pos;
    while ((pos = lineBuffer.find_first_of("\r\n")) != std::string::npos) {
      std::string line = lineBuffer.substr(0, pos);
      lineBuffer.erase(0, pos + 1);

      // 如果有回调且行非空，解析并报告进度
      if (callback && !line.empty()) {
        FfmpegPipeInfo info = parseFfmpegProgressLine(line);
        // 只有解析到有效数据时才调用回调
        if (info.frame > 0 || info.time > 0) {
          callback(info);
        }
      }
    }
  }

  return pclose(pipe) == 0;
}

} // namespace live2mp3::utils
