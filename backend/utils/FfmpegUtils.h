#pragma once
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace live2mp3::utils {

/**
 * @brief FFmpeg 管道进度信息
 *
 * 存储从 FFmpeg 进度输出中解析的实时状态信息。
 */
struct FfmpegPipeInfo {
  int time;          ///< 已处理时长（毫秒）
  int frame;         ///< 已处理帧数
  int fps;           ///< 当前处理帧率
  int bitrate;       ///< 当前处理码率 (kbits/s)
  int size;          ///< 已输出文件大小（字节）
  int totalDuration; ///< 输入文件总时长（毫秒），0表示未知
  double progress;   ///< 进度百分比（0.0-100.0），-1表示未知
  pid_t pid;         ///< FFmpeg 进程 PID，0表示未启动
};

/**
 * @brief FFmpeg 进度回调类型
 *
 * 当 FFmpeg 输出新的进度信息时，调用此回调函数。
 * @param info 当前进度信息
 */
using FfmpegProgressCallback = std::function<void(const FfmpegPipeInfo &info)>;

/**
 * @brief 取消检查回调类型
 *
 * 用于在 FFmpeg 执行过程中检查是否应该取消任务。
 * @return true 表示任务应该取消
 * @return false 表示任务继续执行
 */
using CancelCheckCallback = std::function<bool()>;

/**
 * @brief 获取媒体文件时长
 *
 * 使用 ffprobe 获取媒体文件的时长信息。
 *
 * @param filePath 媒体文件路径
 * @return int 时长（毫秒），失败返回 -1
 */
int getMediaDuration(const std::string &filePath);

/**
 * @brief 获取多个媒体文件的总时长
 *
 * @param filePaths 媒体文件路径列表
 * @return int 总时长（毫秒），如有任何文件失败返回 -1
 */
int getTotalMediaDuration(const std::vector<std::string> &filePaths);

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
 * 使用 fork+exec 执行 FFmpeg 命令，解析其输出中的进度信息，
 * 并通过回调函数实时报告给调用者。支持取消和进度百分比计算。
 *
 * @param cmd 完整的 FFmpeg 命令行
 * @param callback 可选的进度回调函数，每次解析到新进度时调用
 * @param totalDuration 输入文件总时长（毫秒），用于计算进度百分比；0表示不计算
 * @param cancelCheck 可选的取消检查回调，返回 true 时终止 FFmpeg 进程
 * @param outPid 可选的输出参数，用于获取 FFmpeg 进程 PID
 * @return true 命令执行成功（返回码为0）
 * @return false 命令执行失败或被取消
 */
bool runFfmpegWithProgress(const std::string &cmd,
                           FfmpegProgressCallback callback = nullptr,
                           int totalDuration = 0,
                           CancelCheckCallback cancelCheck = nullptr,
                           pid_t *outPid = nullptr);

/**
 * @brief 终止 FFmpeg 进程
 *
 * 向指定 PID 的进程发送 SIGTERM 信号。
 *
 * @param pid 进程 PID
 * @return true 信号发送成功
 * @return false 信号发送失败
 */
bool terminateFfmpegProcess(pid_t pid);

} // namespace live2mp3::utils
