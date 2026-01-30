#pragma once
#include <functional>
#include <string>

namespace live2mp3::utils {

/**
 * @brief FFmpeg 管道进度信息
 *
 * 存储从 FFmpeg 进度输出中解析的实时状态信息。
 */
struct FfmpegPipeInfo {
  int time;    ///< 已处理时长（毫秒）
  int frame;   ///< 已处理帧数
  int fps;     ///< 当前处理帧率
  int bitrate; ///< 当前处理码率 (kbits/s)
  int size;    ///< 已输出文件大小（字节）
};

/**
 * @brief FFmpeg 进度回调类型
 *
 * 当 FFmpeg 输出新的进度信息时，调用此回调函数。
 * @param info 当前进度信息
 */
using FfmpegProgressCallback = std::function<void(const FfmpegPipeInfo &info)>;

/**
 * @brief 解析 FFmpeg 进度输出行
 *
 * 从 FFmpeg 的进度输出行中提取 frame、fps、size、time、bitrate 等信息。
 * 典型输出格式: frame=  123 fps= 25 q=-0.0 size=   1234kB time=00:00:05.00
 * bitrate= 123.4kbits/s
 *
 * @param line FFmpeg 输出的一行文本
 * @return FfmpegPipeInfo 解析后的进度信息
 */
FfmpegPipeInfo parseFfmpegProgressLine(const std::string &line);

/**
 * @brief 执行 FFmpeg 命令并实时报告进度
 *
 * 通过 popen 执行 FFmpeg 命令，解析其输出中的进度信息，
 * 并通过回调函数实时报告给调用者。
 *
 * @param cmd 完整的 FFmpeg 命令行（应包含 2>&1 重定向）
 * @param callback 可选的进度回调函数，每次解析到新进度时调用
 * @return true 命令执行成功（返回码为0）
 * @return false 命令执行失败
 */
bool runFfmpegWithProgress(const std::string &cmd,
                           FfmpegProgressCallback callback = nullptr);

} // namespace live2mp3::utils
