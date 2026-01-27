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
  PENDING = 0,   ///< 等待中
  RUNNING,   ///< 执行中
  COMPLETED, ///< 已完成
  FAILED= -1    ///< 失败
};

/**
 * @brief 任务类型枚举
 */
enum class FfmpegTaskType {
  CONVERT_MP4 = 0, ///< 转换mp4任务
  CONVERT_MP3, ///< 转换mp3任务
  MERGE,       ///< 合并任务
  OTHER        ///< 其他任务
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
  std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> func;      ///< 任务执行函数
  std::function<void(std::weak_ptr<FfmpegTaskProcDetail>)> callback; ///< 任务完成回调
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
};

/**
 * @brief 任务执行详情
 *
 * 存放任务的执行详情，包括任务ID、进度、状态、结果消息等。
 */
class FfmpegTaskProcDetail : private FfmpegTaskProcess,
                             public ThreadTaskInterface {
public:
  FfmpegTaskProcDetail() = default;

  void setPipeInfo(const live2mp3::utils::FfmpegPipeInfo &pipeInfo);

  std::shared_ptr<live2mp3::utils::FfmpegPipeInfo> getPipeInfo();

  /**
   * @brief 获取任务结果
   */
  FfmpegTaskResult getProcessResult();
  
  /**
   * @brief 虚函数接口：线程池调用
   * 
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
   * @brief 获取任务ID
   */
  std::string getId();

  /**
   * @brief 获取任务实例
   */
  static std::shared_ptr<FfmpegTaskProcDetail> getInstance(const FfmpegTaskInput &input);

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
};

/**
 * @brief 协程安全的任务通道
 *
 * 用于在协程之间传递任务，支持非阻塞发送和协程等待接收。
 */
class FfAsyncChannel {
public:

  /**
   * @brief 发送一个元素到通道 (非阻塞）
   */
  drogon::Task<std::string> send(FfmpegTaskInput item);

  /**
   * @brief 关闭通道
   */
  void close();


  FfAsyncChannel(size_t capacity, std::shared_ptr<CommonThreadService> threadServicePtr);

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
  std::unordered_map<std::string, std::shared_ptr<FfmpegTaskProcDetail>> taskMap_;

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



  static void ConvertMp4Task(FfmpegTaskInput item);

private:
  
 
};

