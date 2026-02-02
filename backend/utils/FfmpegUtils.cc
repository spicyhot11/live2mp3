/**
 * @file FfmpegUtils.cc
 * @brief FFmpeg 工具函数实现
 *
 * 提供 FFmpeg 命令执行和进度解析的公共功能。
 */

#include "FfmpegUtils.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <drogon/drogon.h>
#include <fcntl.h>
#include <regex>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace live2mp3::utils {

int getMediaDuration(const std::string &filePath) {
  // 使用 ffprobe 获取媒体时长
  // ffprobe -v error -show_entries format=duration -of
  // default=noprint_wrappers=1:nokey=1 <file>
  std::string cmd = "ffprobe -v error -show_entries format=duration -of "
                    "default=noprint_wrappers=1:nokey=1 \"" +
                    filePath + "\" 2>/dev/null";

  std::array<char, 128> buffer;
  std::string result;

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    LOG_ERROR << "getMediaDuration: popen 失败";
    return -1;
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }

  int exitCode = pclose(pipe);
  if (exitCode != 0) {
    LOG_ERROR << "getMediaDuration: ffprobe 执行失败，文件: " << filePath;
    return -1;
  }

  // 解析时长（秒，可能有小数）
  try {
    double durationSec = std::stod(result);
    return static_cast<int>(durationSec * 1000); // 转换为毫秒
  } catch (...) {
    LOG_ERROR << "getMediaDuration: 解析时长失败，输出: " << result;
    return -1;
  }
}

int getTotalMediaDuration(const std::vector<std::string> &filePaths) {
  int totalDuration = 0;
  for (const auto &path : filePaths) {
    int duration = getMediaDuration(path);
    if (duration < 0) {
      return -1; // 任何一个文件失败则返回 -1
    }
    totalDuration += duration;
  }
  return totalDuration;
}

bool terminateFfmpegProcess(pid_t pid) {
  if (pid <= 0) {
    return false;
  }

  // 先发送 SIGTERM 让进程优雅退出
  if (kill(pid, SIGTERM) == 0) {
    LOG_DEBUG << "已向 FFmpeg 进程 " << pid << " 发送 SIGTERM 信号";

    // 等待一小段时间让进程退出
    for (int i = 0; i < 10; ++i) {
      int status;
      pid_t result = waitpid(pid, &status, WNOHANG);
      if (result == pid) {
        LOG_DEBUG << "FFmpeg 进程 " << pid << " 已正常退出";
        return true;
      }
      usleep(100000); // 等待 100ms
    }

    // 如果还没退出，发送 SIGKILL 强制终止
    LOG_WARN << "FFmpeg 进程 " << pid << " 未响应 SIGTERM，发送 SIGKILL";
    if (kill(pid, SIGKILL) == 0) {
      waitpid(pid, nullptr, 0); // 回收僵尸进程
      return true;
    }
  }

  LOG_ERROR << "无法终止 FFmpeg 进程 " << pid;
  return false;
}

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
                           FfmpegProgressCallback callback, int totalDuration,
                           CancelCheckCallback cancelCheck, pid_t *outPid) {
  // 创建管道用于读取子进程输出
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    LOG_ERROR << "runFfmpegWithProgress: pipe() 创建失败";
    return false;
  }

  pid_t pid = fork();
  if (pid == -1) {
    LOG_ERROR << "runFfmpegWithProgress: fork() 失败";
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) {
    // 子进程
    close(pipefd[0]); // 关闭读端

    // 重定向 stdout 和 stderr 到管道写端
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    // 使用 /bin/sh -c 执行命令
    execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);

    // 如果 exec 失败则退出
    _exit(127);
  }

  // 父进程
  close(pipefd[1]); // 关闭写端

  // 输出 PID（如果请求）
  if (outPid) {
    *outPid = pid;
  }

  LOG_DEBUG << "FFmpeg 进程已启动，PID: " << pid;

  // 设置非阻塞读取
  int flags = fcntl(pipefd[0], F_GETFL, 0);
  fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

  bool cancelled = false;
  std::string lineBuffer;
  std::array<char, 256> buffer;

  while (true) {
    // 检查是否需要取消
    if (cancelCheck && cancelCheck()) {
      LOG_INFO << "FFmpeg 任务被取消，终止进程 " << pid;
      terminateFfmpegProcess(pid);
      cancelled = true;
      break;
    }

    // 非阻塞读取
    ssize_t bytesRead = read(pipefd[0], buffer.data(), buffer.size() - 1);
    if (bytesRead > 0) {
      buffer[bytesRead] = '\0';
      lineBuffer += buffer.data();

      // 逐行处理
      size_t pos;
      while ((pos = lineBuffer.find_first_of("\r\n")) != std::string::npos) {
        std::string line = lineBuffer.substr(0, pos);
        lineBuffer.erase(0, pos + 1);

        // 如果有回调且行非空，解析并报告进度
        if (callback && !line.empty()) {
          FfmpegPipeInfo info = parseFfmpegProgressLine(line);
          // 只有解析到有效数据时才调用回调
          if (info.frame > 0 || info.time > 0) {
            // 填充额外信息
            info.pid = pid;
            info.totalDuration = totalDuration;

            // 计算进度百分比
            if (totalDuration > 0 && info.time > 0) {
              info.progress =
                  std::min(100.0, (static_cast<double>(info.time) /
                                   static_cast<double>(totalDuration)) *
                                      100.0);
            } else {
              info.progress = -1.0; // 未知进度
            }

            callback(info);
          }
        }
      }
    } else if (bytesRead == 0) {
      // EOF - 子进程已关闭管道
      break;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      // 读取错误
      LOG_ERROR << "runFfmpegWithProgress: read() 错误: " << strerror(errno);
      break;
    }

    // 检查子进程是否已退出
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      // 子进程已退出，读取剩余输出
      while ((bytesRead = read(pipefd[0], buffer.data(), buffer.size() - 1)) >
             0) {
        buffer[bytesRead] = '\0';
        lineBuffer += buffer.data();
      }
      break;
    }

    // 短暂休眠避免 CPU 占用过高
    usleep(10000); // 10ms
  }

  close(pipefd[0]);

  // 等待子进程退出并获取返回码
  int status;
  if (!cancelled) {
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
      int exitCode = WEXITSTATUS(status);
      if (exitCode != 0) {
        LOG_ERROR << "FFmpeg 进程退出码: " << exitCode;
        return false;
      }
      return true;
    } else if (WIFSIGNALED(status)) {
      LOG_ERROR << "FFmpeg 进程被信号终止: " << WTERMSIG(status);
      return false;
    }
  }

  return !cancelled;
}

} // namespace live2mp3::utils
