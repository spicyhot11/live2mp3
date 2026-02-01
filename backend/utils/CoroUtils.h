#pragma once

#include <drogon/drogon.h>
#include <functional>
#include <future>

namespace live2mp3::utils {

/**
 * CallbackAwaiter - 将回调式 API 转换为协程可等待对象
 *
 * @tparam T 回调返回的类型
 */
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

/**
 * FutureAwaiter - 将 std::future 转换为协程可等待对象 (非阻塞)
 *
 * 注意：这个实现会在后台线程等待 future，然后恢复协程
 */
template <typename T> class FutureAwaiter {
public:
  explicit FutureAwaiter(std::future<T> future) : future_(std::move(future)) {}

  bool await_ready() const noexcept {
    return future_.wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> handle) {
    std::thread([this, handle]() mutable {
      result_ = future_.get();
      handle.resume();
    }).detach();
  }

  T await_resume() { return std::move(result_); }

private:
  std::future<T> future_;
  T result_;
};

/**
 * FutureAwaiter<void> 特化版本 - 用于 std::future<void>
 */
template <> class FutureAwaiter<void> {
public:
  explicit FutureAwaiter(std::future<void> future)
      : future_(std::move(future)) {}

  bool await_ready() const noexcept {
    return future_.wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> handle) {
    std::thread([this, handle]() mutable {
      future_.get();
      handle.resume();
    }).detach();
  }

  void await_resume() {}

private:
  std::future<void> future_;
};

/**
 * awaitFuture - 将 std::future 转换为 co_await 可等待对象
 */
template <typename T> FutureAwaiter<T> awaitFuture(std::future<T> future) {
  return FutureAwaiter<T>(std::move(future));
}

} // namespace live2mp3::utils
