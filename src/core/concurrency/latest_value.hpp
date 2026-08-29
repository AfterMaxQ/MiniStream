#pragma once

#include <mutex>
#include <optional>
#include <utility>

namespace ministream {

template <class T>
class LatestValue {
 public:
  void publish(T value) {
    std::scoped_lock lock(mutex_);
    value_ = std::move(value);
  }

  std::optional<T> take() {
    std::scoped_lock lock(mutex_);
    auto value = std::move(value_);
    value_.reset();
    return value;
  }

 private:
  std::mutex mutex_;
  std::optional<T> value_;
};

}  // namespace ministream
