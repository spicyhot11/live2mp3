#pragma once
#include <string>
#include <atomic>
#include <memory>


namespace utils {

/**
 * @brief 一个通用的线程安全包装器
 * 适用于读多写少的场景
 */
template <typename T>
class ThreadSafe {
public:
    ThreadSafe() = default;

    // 构造函数：需要包装成 shared_ptr 并存入 atomic
    explicit ThreadSafe(T value) 
        : data_(std::make_shared<const T>(std::move(value))) {}

    // 读取：利用 atomic 的原子加载，拿到的是一个安全的快照指针
    std::shared_ptr<const T> get() const {
        auto ptr = data_.load(); // 原子读取指针
        if (ptr) {
            return ptr;
        }
        return getDefaultPtr(); 
    }

    // 写入：创建新对象，然后原子地“替换”掉旧指针
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
        } 
        else if constexpr (std::is_same_v<T, int>) {
            // 针对 int 的特定逻辑（例如返回 -1 而不是 0）
            static const auto kDefaultInt = std::make_shared<const int>(0);
            return kDefaultInt;
        }
        
        // 通用逻辑：调用 T 的默认构造函数
        static const auto kGenericDefault = std::make_shared<const T>();
        return kGenericDefault;
        
    }


    // 用于存储数据的原子指针
    std::atomic<std::shared_ptr<const T>> data_ = nullptr;
};

// 为常用的 string 提供一个别名
using ThreadSafeString = ThreadSafe<std::string>;

} // namespace my_app::utils