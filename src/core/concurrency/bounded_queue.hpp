#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace ministream {

enum class OverflowPolicy { DropOldest, RejectNewest };

template <class T>
class BoundedQueue {
 public:
  BoundedQueue(std::size_t capacity, OverflowPolicy policy)
      : capacity_(capacity), policy_(policy) {}

  bool push(T value) {
    {
      std::scoped_lock lock(mutex_);
      if (capacity_ == 0) {
        return false;
      }
      if (queue_.size() == capacity_) {
        if (policy_ == OverflowPolicy::RejectNewest) {
          return false;
        }
        queue_.pop_front();
      }
      queue_.push_back(std::move(value));
    }
    ready_.notify_one();
    return true;
  }

  std::optional<T> try_pop() {
    std::scoped_lock lock(mutex_);
    return pop_locked();
  }

  template <class Rep, class Period>
  std::optional<T> wait_pop_for(std::chrono::duration<Rep, Period> timeout) {
    std::unique_lock lock(mutex_);
    if (!ready_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
      return std::nullopt;
    }
    return pop_locked();
  }

  [[nodiscard]] std::size_t size() const {
    std::scoped_lock lock(mutex_);
    return queue_.size();
  }

 private:
  std::optional<T> pop_locked() {
    if (queue_.empty()) {
      return std::nullopt;
    }
    auto value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  std::size_t capacity_;
  OverflowPolicy policy_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<T> queue_;
};

}  // namespace ministream
