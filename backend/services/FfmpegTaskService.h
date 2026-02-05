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
 *
 * 存放任务的基础信息，包括任务类型、输入文件列表、输出文件列表等。
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
 *
 * 存放任务的输入信息，包括任务类型、输入文件列表、输出文件列表等。
 */
struct FfmpegTaskInput : public FfmpegTaskBase, public FfmpegTaskExecute {};

/**
 * @brief 任务执行过程
 *
 * 存放任务的执行过程信息，包括任务ID、进度、状态、结果消息等。
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
 * 存放任务的执行详情，包括任务ID、进度、状态、结果消息等。
 * 支持通过 promise/future 机制等待任务完成。
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
   *
   * 设置取消标志并尝试终止FFmpeg进程
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
   * 执行任务并在完成时设置 promise
   */
  void run() override;

  /**
   * @brief 执行任务
   * 调用函数与回调函数
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
   *
   * 用于协程等待任务完成。调用者可以通过轮询或阻塞等待获取结果。
   * @return std::shared_future<FfmpegTaskResult> 任务结果的 shared_future
   */
  std::shared_future<FfmpegTaskResult> getFuture();

  /**
   * @brief 获取任务实例
   */
  static std::shared_ptr<FfmpegTaskProcDetail>
  getInstance(const FfmpegTaskInput &input);

private:
  /**
   * @brief 基本信息锁
   * 锁除管道外的内容
   */
  std::mutex mutexStatic_;

  /**
   * @brief 执行函数
   */
  FfmpegTaskExecute executeFunc_;

  /**
   * @brief 管道信息
   * 管道信息是线程安全的
   */
  live2mp3::utils::ThreadSafe<live2mp3::utils::FfmpegPipeInfo> pipeInfo;

  /**
   * @brief 任务完成通知 promise
   */
  std::promise<FfmpegTaskResult> promise_;

  /**
   * @brief 任务完成的 shared_future
   * 用于多个消费者等待同一个任务完成
   */
  std::shared_future<FfmpegTaskResult> future_;

  /**
   * @brief 取消标志
   */
  std::atomic<bool> cancelled_{false};

  /**
   * @brief 当前 FFmpeg 进程 PID
   */
  std::atomic<pid_t> pid_{0};

  /**
   * @brief 输入文件总时长（毫秒）
   */
  std::atomic<int> totalDuration_{0};
};

/**
 * @brief 协程安全的任务通道
 *
 * 用于在协程之间传递任务，支持非阻塞发送和协程等待接收。
 */
class FfAsyncChannel {
public:
  /**
   * @brief 发送一个元素到通道 (协程版本，等待任务完成)
   * @param item 任务输入
   * @return drogon::Task<std::optional<FfmpegTaskResult>>
   * 成功返回任务结果，失败返回 nullopt
   */
  drogon::Task<std::optional<FfmpegTaskResult>> send(FfmpegTaskInput item);

  /**
   * @brief 发送一个元素到通道 (非等待版本，发射后不管)
   *
   * 任务会在后台异步执行，调用者无需等待任务完成。
   * @param item 任务输入
   * @param callback 可选的完成回调，传入任务ID（成功时）或 nullopt（失败时）
   */
  void
  sendAsync(FfmpegTaskInput item,
            std::function<void(std::optional<std::string>)> callback = nullptr);

  /**
   * @brief 关闭通道
   */
  void close();

  /**
   * @brief 获取当前正在运行的任务列表
   * @return 任务进度信息列表
   */
  std::vector<FfmpegTaskProcess> getRunningTasks();

  /**
   * @brief 构造函数
   * @param capacity 最大并发任务数
   * @param maxWaiting 最大等待队列长度
   * @param threadServicePtr 线程池服务指针
   */
  FfAsyncChannel(size_t capacity, size_t maxWaiting,
                 std::shared_ptr<CommonThreadService> threadServicePtr);

  ~FfAsyncChannel();

private:
  /**
   * @brief 锁
   * 范围：queue_与taskMap_
   */
  std::mutex mutex_;

  /**
   * @brief 任务队列
   */
  std::queue<std::shared_ptr<FfmpegTaskProcDetail>> queue_;

  /**
   * @brief 任务映射表
   */
  std::unordered_map<std::string, std::shared_ptr<FfmpegTaskProcDetail>>
      taskMap_;

  /**
   * @brief 信号量
   * 用于控制并发任务数量
   */
  live2mp3::utils::SimpleCoroSemaphore semaphore_;

  /**
   * @brief 线程池
   * 用于执行任务
   */
  std::shared_ptr<CommonThreadService> threadServicePtr_;
};

// ============================================================
// FfmpegTaskService: 任务队列服务类
// ============================================================

/**
 * @brief 任务队列服务类
 *
 * 该类作为单例运行，维护一个内部任务队列。
 * 使用协程进行任务调度，自建线程池执行 FFmpeg 阻塞操作。
 * 核心功能：
 * 1. 接收新任务并通过 Channel 传递。
 * 2. 多个 Worker 协程消费 Channel 并分发到线程池执行。
 * 3. 提供接口通过文件名或MD5查询任务状态。
 */
class FfmpegTaskService : public drogon::Plugin<FfmpegTaskService> {
public:
  FfmpegTaskService() = default;

  /**
   * @brief 初始化服务
   *
   * 创建线程池并启动 Worker 协程。
   */
  void initAndStart(const Json::Value &config) override;

  /**
   * @brief 关闭服务
   *
   * 关闭 Channel 和线程池。
   */
  void shutdown() override;

  /**
   * @brief 提交任务的统一接口
   *
   * 根据任务类型自动选择处理函数：
   * - CONVERT_MP4 → ConvertMp4Task
   * - CONVERT_MP3 → ConvertMp3Task
   * - MERGE → MergeTask
   * - OTHER → 需要通过 customFunc 参数传入
   *
   * @param type 任务类型
   * @param files 输入文件列表
   * @param outputFiles 输出目录/文件列表
   * @param callback 可选的完成回调
   * @param customFunc 仅当 type 为 OTHER 时使用的自定义处理函数
   * @return drogon::Task<std::optional<FfmpegTaskResult>>
   * 成功返回任务结果，失败返回 nullopt
   */
  drogon::Task<std::optional<FfmpegTaskResult>> submitTask(
      FfmpegTaskType type, const std::vector<std::string> &files,
      const std::vector<std::string> &outputFiles,
      std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> callback =
          nullptr,
      std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> customFunc =
          nullptr);

  /**
   * @brief 提交任务（非等待版本，发射后不管）
   *
   * @param type 任务类型
   * @param files 输入文件列表
   * @param outputFiles 输出目录/文件列表
   * @param resultCallback 可选的结果回调，传入任务ID或nullopt
   * @param callback 可选的完成回调
   * @param customFunc 仅当 type 为 OTHER 时使用的自定义处理函数
   */
  void submitTaskAsync(
      FfmpegTaskType type, const std::vector<std::string> &files,
      const std::vector<std::string> &outputFiles,
      std::function<void(std::optional<std::string>)> resultCallback = nullptr,
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
   * @return 任务进度信息列表
   */
  std::vector<FfmpegTaskProcess> getRunningTasks();

private:
  /**
   * @brief 根据任务类型获取对应的处理函数
   */
  static std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)>
  getTaskFunc(FfmpegTaskType type);

  /**
   * @brief 任务通道
   */
  std::unique_ptr<FfAsyncChannel> channel_;

  /**
   * @brief 线程池服务
   */
  std::shared_ptr<CommonThreadService> threadServicePtr_;

  /**
   * @brief 配置服务
   */
  std::shared_ptr<ConfigService> configService_;
};
