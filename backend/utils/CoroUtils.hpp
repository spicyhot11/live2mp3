#pragma once
#include <coroutine>
#include <drogon/drogon.h>
#include <functional>
#include <future>

namespace live2mp3::utils {


static constexpr double CHECK_INTERVAL = 0.05;
/**
 * @brief Future到协程的适配器
 *
 * 允许在协程中使用 co_await 等待 std::future
 * 通过轮询 + sleepCoro 的方式检查 future 状态，但提供了统一的接口
 */
template <typename T> class FutureAwaiter {
public:
  explicit FutureAwaiter(std::future<T> &&future)
      : future_(std::move(future)) {}

  // 禁止拷贝
  FutureAwaiter(const FutureAwaiter &) = delete;
  FutureAwaiter &operator=(const FutureAwaiter &) = delete;

  // 允许移动
  FutureAwaiter(FutureAwaiter &&) = default;
  FutureAwaiter &operator=(FutureAwaiter &&) = default;

  /**
   * @brief 检查是否已经完成
   */
  bool await_ready() const noexcept {
    return future_.wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  }

  /**
   * @brief 挂起协程并启动轮询
   *
   * 使用sleepCoro轮询future状态，完成后恢复协程
   */
  void await_suspend(std::coroutine_handle<> h) {
    // 启动异步轮询任务
    drogon::async_run([this, h]() -> drogon::Task<> {
      // 轮询检查future状态
      while (future_.wait_for(std::chrono::milliseconds(0)) !=
             std::future_status::ready) {
        co_await drogon::sleepCoro(drogon::app().getLoop(), CHECK_INTERVAL);
      }
      // Future完成，恢复协程
      h.resume();
    });
  }

  /**
   * @brief 获取结果
   */
  T await_resume() { return future_.get(); }

private:
  std::future<T> future_;
};

// void特化版本
template <> class FutureAwaiter<void> {
public:
  explicit FutureAwaiter(std::future<void> &&future)
      : future_(std::move(future)) {}

  FutureAwaiter(const FutureAwaiter &) = delete;
  FutureAwaiter &operator=(const FutureAwaiter &) = delete;
  FutureAwaiter(FutureAwaiter &&) = default;
  FutureAwaiter &operator=(FutureAwaiter &&) = default;

  bool await_ready() const noexcept {
    return future_.wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> h) {
    drogon::async_run([this, h]() -> drogon::Task<> {
      while (future_.wait_for(std::chrono::milliseconds(0)) !=
             std::future_status::ready) {
        co_await drogon::sleepCoro(drogon::app().getLoop(), CHECK_INTERVAL);
      }
      h.resume();
    });
  }

  void await_resume() { future_.get(); }

private:
  std::future<void> future_;
};

/**
 * @brief 辅助函数：将std::future转换为可co_await的awaiter
 */
template <typename T> FutureAwaiter<T> awaitFuture(std::future<T> &&future) {
  return FutureAwaiter<T>(std::move(future));
}


template <typename T> class CallbackAwaiter {
public:
  using CallbackType = std::function<void(T)>;
  using InitiatorType = std::function<void(CallbackType)>;

  explicit CallbackAwaiter(InitiatorType initiator)
      : initiator_(std::move(initiator)) {}

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) {
    initiator_([this, handle](T result) mutable {
      result_ = std::move(result);
      handle.resume();
    });
  }

  T await_resume() { return std::move(result_); }

private:
  InitiatorType initiator_;
  T result_;
};

/**
 * awaitCallback - 将回调式操作转换为 co_await 可等待对象
 *
 * @tparam T 回调返回的类型
 * @param initiator 接受回调函数的初始化器
 * @return CallbackAwaiter<T>
 *
 * 使用示例:
 *   auto result = co_await awaitCallback<int>([](std::function<void(int)> cb)
 * { someAsyncOp([cb](int val) { cb(val); });
 *   });
 */
template <typename T>
CallbackAwaiter<T>
awaitCallback(typename CallbackAwaiter<T>::InitiatorType initiator) {
  return CallbackAwaiter<T>(std::move(initiator));
}

} // namespace live2mp3::utils
