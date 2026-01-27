#pragma once
#include <atomic>
#include <coroutine>
#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace live2mp3::utils {

#define MAX_WAIT_COUNT 20000
/**
 * @brief 一个通用的线程安全包装器
 * 适用于读多写少的场景
 */
template <typename T> class ThreadSafe {
public:
  ThreadSafe() = default;

  // 构造函数：需要包装成 shared_ptr 并存入 atomic
  explicit ThreadSafe(T value)
      : data_(std::make_shared<const T>(std::move(value))) {}

  /**
   * @brief 获取当前数据的快照
   *
   * 利用 atomic 的原子加载，获取当前数据的 shared_ptr。
   * 这是一个无锁操作，非常快。
   * 返回的指针保证在持有期间数据有效，即使其他线程进行了 set 操作。
   *
   * @return std::shared_ptr<const T> 指向数据的常量指针
   */
  std::shared_ptr<const T> get() const {
    auto ptr = data_.load(); // 原子读取指针
    if (ptr) {
      return ptr;
    }
    return getDefaultPtr();
  }

  /**
   * @brief 更新数据
   *
   * 创建新的数据对象，并原子地“替换”掉旧指针。
   * 这是一个写时复制（CoW）风格的操作。
   * 旧数据的内存会在最后一个 reader 释放 shared_ptr 后自动回收。
   *
   * @param value 新的数据值
   */
  void set(T value) {
    auto new_ptr = std::make_shared<const T>(std::move(value));
    data_.store(new_ptr); // 原子替换，旧指针如果没有其他地方引用，会自动释放
  }

private:
  // 存放默认的返回值的智能指针，防止空指针产生
  std::shared_ptr<const T> getDefaultPtr() const {
    if constexpr (std::is_same_v<T, std::string>) {
      // 针对 std::string 的特定逻辑
      static const auto kEmpty = std::make_shared<const std::string>("");
      return kEmpty;
    } else if constexpr (std::is_same_v<T, int>) {
      // 针对 int 的特定逻辑（例如返回 -1 而不是 0）
      static const auto kDefaultInt = std::make_shared<const int>(0);
      return kDefaultInt;
    }

    // 通用逻辑：调用 T 的默认构造函数
    static const auto kGenericDefault = std::make_shared<const T>();
    return kGenericDefault;
  }

  // 用于存储数据的原子指针
  // 使用 shared_ptr 是为了实现写时复制 (Copy-On-Write) 语义，
  // 原子操作保证了多线程下的读写安全。
  std::atomic<std::shared_ptr<const T>> data_ = nullptr;
};

// 为常用的 string 提供一个别名
using ThreadSafeString = ThreadSafe<std::string>;

class SimpleCoroSemaphore;

/**
 * @brief 实现一个简单的协程信号量 (Semaphore)
 *
 * 用于限制从该信号量获取“许可”的并发协程数量。
 * 典型用途是限制同时运行的重型任务（如 FFmpeg 转换）的数量。
 *
 * 特性：
 * 1. 限制并发执行数 (maxProcCount_)
 * 2. 限制等待队列长度 (maxWaitCount_)
 * 3. 支持 C++20 协程异步等待 (co_await)
 */
class SimpleCoroSemaphore {
public:
  /**
   * @brief 协程等待器 (Awaiter)
   * used by `co_await sem.acquire()`
   */
  struct CoroAwaiter {
    SimpleCoroSemaphore *sem;
    std::coroutine_handle<> handler;
    bool isNotFull = true; ///< 标记是否成功获取到了资源

    /**
     * @brief 检查是否需要挂起
     *
     * 在协程挂起前调用。如果返回 true，表示不需要挂起，直接获取到了资源。
     * 这是一个快速路径检查。
     */
    bool await_ready() const noexcept {
      std::lock_guard<std::mutex> lock(sem->procMutex_);
      if (sem->procCount_ < sem->maxProcCount_) {
        ++sem->procCount_;
        return true; // 不需要挂起，直接继续执行
      } else {
        return false; // 需要挂起
      }
    }

    /**
     * @brief 挂起协程并入队
     *
     * 当 await_ready 返回 false 时调用。
     * 这里需要再次加锁检查（Double-Checked Locking）。
     */
    bool await_suspend(std::coroutine_handle<> h) {
      CoroAwaiter *old = nullptr;
      handler = h;
      {
        std::lock_guard<std::mutex> lock(sem->procMutex_);
        // Double check: 如果此时恰好有资源释放了，直接抢占，不要入队。
        if (sem->procCount_ < sem->maxProcCount_) {
          ++sem->procCount_;
          return false; // 返回 false 立即恢复协程，不经挂起
        }
        // 如果等待队列已满，执行简单的流控策略：踢出最老的等待者
        else if (sem->waiters_.size() >= sem->maxWaitCount_) {
          old = sem->waiters_.front();
          sem->waiters_.pop();
          old->isNotFull = false; // 标记那个不幸的任务失败
        }
        sem->waiters_.push(this);
      }

      // 如果有被踢出的任务，在这个锁外唤醒它，让它去处理失败逻辑
      if (old) {
        old->handler.resume();
      }
      return true; // 返回 true 确认挂起
    }

    /**
     * @brief 协程恢复时的返回值
     * @return true 成功获取资源; false 等待队列已满被踢出，获取失败
     */
    bool await_resume() { return isNotFull; }
  };

  /**
   * @brief 请求获取一个信号量许可
   *
   * 返回一个 CoroAwaiter 对象，用于挂起协程直到获取到许可。
   * 协程可以使用 `co_await sem.acquire()` 的方式等待许可。
   * 强制检查
   */
  [[nodiscard]] CoroAwaiter acquire() { return CoroAwaiter{this}; }

  /**
   * @brief 释放一个信号量许可
   *
   * 如果有等待者，直接唤醒队列头部的等待者（将许可转交给它）。
   * 如果没有等待者，减少计数。
   *
   * @return std::coroutine_handle<> 下一个要运行的协程句柄，或者 noop
   */
  void release() {
    CoroAwaiter *lpAwaiter = nullptr;
    {
      std::lock_guard<std::mutex> lock(procMutex_);
      if (waiters_.empty()) {
        if (procCount_ > 0)
          --procCount_;
        return;
      }
      lpAwaiter = waiters_.front();
      waiters_.pop();
    }
    // 唤醒等待的协程
    if (lpAwaiter)
      lpAwaiter->handler.resume();
  }

  size_t getMaxProcCount() { 
    std::lock_guard<std::mutex> lock(procMutex_);
    return maxProcCount_; 
  }

  size_t getMaxWaitCount() { 
    std::lock_guard<std::mutex> lock(procMutex_);
    return maxWaitCount_; 
  }

  SimpleCoroSemaphore(size_t maxProcCount, size_t maxWaitCount)
      : maxProcCount_(maxProcCount), maxWaitCount_(maxWaitCount),
        procCount_(0) {}

  SimpleCoroSemaphore(size_t maxProcCount)
      : maxProcCount_(maxProcCount), maxWaitCount_(MAX_WAIT_COUNT),
        procCount_(0) {}

  SimpleCoroSemaphore(const SimpleCoroSemaphore &other) = delete;
  SimpleCoroSemaphore &operator=(const SimpleCoroSemaphore &other) = delete;

private:
  size_t procCount_;     ///< 当前已获取的许可数量 (正在运行的任务数)
  size_t maxProcCount_;  ///< 最大允许的并发数量
  std::mutex procMutex_; ///< 保护内部状态的互斥锁

  std::queue<CoroAwaiter *> waiters_; ///< 等待队列，存储正在等待许可的协程
  size_t maxWaitCount_;               ///< 最大等待队列长度，超过将丢弃新请求

  friend struct CoroAwaiter;
};

} // namespace live2mp3::utils