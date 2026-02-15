#pragma once

#include "../utils/FfmpegUtils.h"
#include "../utils/ThreadSafe.hpp"
#include "services/CommonThreadService.h"
#include "services/ConfigService.h"
#include <atomic>
#include <condition_variable>
#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

/*
 前向声明
*/
typedef struct FfmpegTaskProcess FfmpegTaskResult; /// < 任务执行结果
struct FfmpegTaskInput;
struct FfmpegTaskExecute;
class FfmpegThreadPool;
class FfmpegTaskProcDetail;

/**
 * @brief 任务状态枚举
 */
enum class FfmpegTaskStatus {
  PENDING = 0, ///< 等待中
  RUNNING,     ///< 执行中
  COMPLETED,   ///< 已完成
  FAILED = -1  ///< 失败
};

/**
 * @brief 任务类型枚举
 */
enum class FfmpegTaskType {
  CONVERT_MP4 = 0, ///< 转换mp4任务
  CONVERT_MP3,     ///< 转换mp3任务
  MERGE,           ///< 合并任务
  OTHER            ///< 其他任务
};

/**
 * @brief 任务基础信息
 */
struct FfmpegTaskBase {
  FfmpegTaskType type;                  ///< 任务类型
  std::vector<std::string> files;       ///< 关联的文件列表
  std::vector<std::string> outputFiles; ///< 输出文件列表
};

struct FfmpegTaskExecute {
  std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)>
      func; ///< 任务执行函数
  std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)>
      callback; ///< 任务完成回调
};

/**
 * @brief 任务输入
 */
struct FfmpegTaskInput : public FfmpegTaskBase, public FfmpegTaskExecute {};

/**
 * @brief 任务执行过程
 */
struct FfmpegTaskProcess : public FfmpegTaskBase {
  std::string id;            ///< 任务唯一ID
  FfmpegTaskStatus status;   ///< 当前状态
  std::string resultMessage; ///< 结果消息或错误信息
  long long createTime;      ///< 创建时间戳
  long long startTime;       ///< 开始时间戳
  long long endTime;         ///< 结束时间戳

  // 进度信息
  int progressTime;    ///< 已处理时长（毫秒）
  int progressFps;     ///< 当前处理帧率
  int progressBitrate; ///< 当前处理码率 (kbits/s)
  double speed;        ///< 处理速度倍率（相对于实时）
  int totalDuration;   ///< 输入文件总时长（毫秒），0表示未知
  double progress;     ///< 进度百分比（0.0-100.0），-1表示未知
};

/**
 * @brief 任务执行详情
 *
 * 支持通过 promise/future 机制等待任务完成。
 * 支持重试计数，失败后可重入队列重试。
 */
class FfmpegTaskProcDetail
    : private FfmpegTaskProcess,
      public ThreadTaskInterface,
      public std::enable_shared_from_this<FfmpegTaskProcDetail> {
public:
  FfmpegTaskProcDetail();

  // 禁用拷贝构造与=
  FfmpegTaskProcDetail(const FfmpegTaskProcDetail &) = delete;
  FfmpegTaskProcDetail &operator=(const FfmpegTaskProcDetail &) = delete;

  void setPipeInfo(const live2mp3::utils::FfmpegPipeInfo &pipeInfo);

  std::shared_ptr<live2mp3::utils::FfmpegPipeInfo> getPipeInfo();

  /**
   * @brief 设置当前 FFmpeg 进程 PID
   */
  void setPid(pid_t pid);

  /**
   * @brief 获取当前 FFmpeg 进程 PID
   */
  pid_t getPid() const;

  /**
   * @brief 设置输入文件总时长
   */
  void setTotalDuration(int duration);

  /**
   * @brief 获取输入文件总时长
   */
  int getTotalDuration() const;

  /**
   * @brief 取消任务
   */
  void cancel();

  /**
   * @brief 检查任务是否被取消
   */
  bool isCancelled() const;

  /**
   * @brief 获取任务结果
   */
  FfmpegTaskResult getProcessResult();

  /**
   * @brief 虚函数接口：线程池调用
   */
  void run() override;

  /**
   * @brief 执行任务
   */
  drogon::Task<FfmpegTaskResult> execute();

  /**
   * @brief 设置任务信息
   */
  void setInfo(const FfmpegTaskInput &input);

  /**
   * @brief 设置输出文件列表
   */
  void setOutputFiles(const std::vector<std::string> &outputFiles);

  /**
   * @brief 获取任务ID
   */
  std::string getId();

  /**
   * @brief 获取任务完成的 future
   */
  std::shared_future<FfmpegTaskResult> getFuture();

  /**
   * @brief 获取任务实例
   */
  static std::shared_ptr<FfmpegTaskProcDetail>
  getInstance(const FfmpegTaskInput &input);

  // ============================================================
  // 重试支持
  // ============================================================

  /**
   * @brief 获取当前重试计数
   */
  int getRetryCount() const;

  /**
   * @brief 递增重试计数并返回新值
   */
  int incrementRetry();

  /**
   * @brief 设置最大重试次数（从配置注入）
   */
  void setMaxRetries(int max);

  /**
   * @brief 获取最大重试次数
   */
  int getMaxRetries() const;

  /**
   * @brief 是否已超过最大重试次数
   */
  bool isRetryExhausted() const;

  /**
   * @brief 重置任务状态为 PENDING 以便重新执行
   */
  void resetForRetry();

private:
  std::mutex mutexStatic_;
  FfmpegTaskExecute executeFunc_;
  live2mp3::utils::ThreadSafe<live2mp3::utils::FfmpegPipeInfo> pipeInfo;
  std::promise<FfmpegTaskResult> promise_;
  std::shared_future<FfmpegTaskResult> future_;
  std::atomic<bool> cancelled_{false};
  std::atomic<pid_t> pid_{0};
  std::atomic<int> totalDuration_{0};

  // 重试支持
  std::atomic<int> retryCount_{0};
  std::atomic<int> maxRetries_{3}; // 从配置注入
};

// ============================================================
// FfAsyncChannel: 调度线程 + 队列驱动的任务通道
// ============================================================

/**
 * @brief 任务通道
 *
 * 使用专用调度线程检查队列，在线程池空闲且限额允许时提交任务。
 * 失败的任务会自动重入队列末尾进行重试。
 */
class FfAsyncChannel {
public:
  /**
   * @brief 提交任务到队列（非阻塞）
   * @param item 任务输入
   * @param onComplete 任务完成或最终失败时的回调
   */
  void submit(FfmpegTaskInput item,
              std::function<void(FfmpegTaskResult)> onComplete = nullptr);

  /**
   * @brief 关闭通道
   */
  void close();

  /**
   * @brief 获取当前正在运行的任务列表
   */
  std::vector<FfmpegTaskProcess> getRunningTasks();

  /**
   * @brief 构造函数
   * @param maxConcurrent 最大并发任务数
   * @param maxRetries 最大重试次数
   * @param threadServicePtr 线程池服务指针
   */
  FfAsyncChannel(size_t maxConcurrent, int maxRetries,
                 std::shared_ptr<CommonThreadService> threadServicePtr);

  ~FfAsyncChannel();

private:
  struct QueueItem {
    std::shared_ptr<FfmpegTaskProcDetail> task;
    std::function<void(FfmpegTaskResult)> onComplete;
  };

  std::mutex mutex_;
  std::queue<std::shared_ptr<QueueItem>> pendingQueue_;
  std::unordered_map<std::string, std::shared_ptr<FfmpegTaskProcDetail>>
      taskMap_;

  size_t maxConcurrent_;
  int maxRetries_;
  size_t runningCount_{0};
  std::atomic<bool> closed_{false};
  std::thread schedulerThread_;
  std::condition_variable cv_;
  std::condition_variable drainCv_;
  std::shared_ptr<CommonThreadService> threadServicePtr_;

  /**
   * @brief 调度线程主循环
   */
  void schedulerLoop();

  /**
   * @brief 任务完成回调（在线程池线程中调用）
   */
  void onTaskFinished(const std::string &taskId,
                      std::shared_ptr<QueueItem> itemPtr);
};

// ============================================================
// FfmpegTaskService: 任务队列服务类
// ============================================================

/**
 * @brief 任务队列服务类
 *
 * 使用调度线程 + 队列驱动模式，所有任务提交均为非阻塞。
 */
class FfmpegTaskService : public drogon::Plugin<FfmpegTaskService> {
public:
  FfmpegTaskService() = default;

  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  /**
   * @brief 提交任务（非阻塞，fire-and-forget）
   *
   * @param type 任务类型
   * @param files 输入文件列表
   * @param outputFiles 输出目录/文件列表
   * @param onComplete 任务完成/最终失败时的回调
   * @param callback 可选的任务详情回调（在任务创建后立即调用）
   * @param customFunc 仅当 type 为 OTHER 时使用的自定义处理函数
   */
  void submitTask(
      FfmpegTaskType type, const std::vector<std::string> &files,
      const std::vector<std::string> &outputFiles,
      std::function<void(FfmpegTaskResult)> onComplete = nullptr,
      std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> callback =
          nullptr,
      std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> customFunc =
          nullptr);

  // 任务处理静态函数
  static void ConvertMp4Task(std::weak_ptr<FfmpegTaskProcDetail> item);
  static void ConvertMp3Task(std::weak_ptr<FfmpegTaskProcDetail> item);
  static void MergeTask(std::weak_ptr<FfmpegTaskProcDetail> item);

  /**
   * @brief 获取当前正在运行的任务列表
   */
  std::vector<FfmpegTaskProcess> getRunningTasks();

private:
  static std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)>
  getTaskFunc(FfmpegTaskType type);

  std::unique_ptr<FfAsyncChannel> channel_;
  std::shared_ptr<CommonThreadService> threadServicePtr_;
  std::shared_ptr<ConfigService> configService_;
};
