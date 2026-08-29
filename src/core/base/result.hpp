#pragma once

#include <stdexcept>
#include <utility>
#include <variant>

namespace ministream {

template <class T, class E>
class Result {
 public:
  static Result ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }
  static Result err(E error) { return Result(std::in_place_index<1>, std::move(error)); }

  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  T& value() { return get<0>(); }
  const T& value() const { return get<0>(); }
  T& operator*() { return value(); }
  const T& operator*() const { return value(); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }
  E& error() { return get<1>(); }
  const E& error() const { return get<1>(); }

 private:
  template <std::size_t Index, class U>
  Result(std::in_place_index_t<Index>, U&& value)
      : storage_(std::in_place_index<Index>, std::forward<U>(value)) {}

  template <std::size_t Index>
  auto& get() {
    if (storage_.index() != Index) {
      throw std::logic_error("Result alternative is not active");
    }
    return std::get<Index>(storage_);
  }

  template <std::size_t Index>
  const auto& get() const {
    if (storage_.index() != Index) {
      throw std::logic_error("Result alternative is not active");
    }
    return std::get<Index>(storage_);
  }

  std::variant<T, E> storage_;
};

template <class E>
class Result<void, E> {
 public:
  static Result ok() { return Result(std::in_place_index<0>); }
  static Result err(E error) { return Result(std::in_place_index<1>, std::move(error)); }

  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  void value() const {
    if (!has_value()) {
      throw std::logic_error("Result value is not active");
    }
  }

  E& error() { return get_error(); }
  const E& error() const { return get_error(); }

 private:
  explicit Result(std::in_place_index_t<0>) : storage_(std::in_place_index<0>) {}
  Result(std::in_place_index_t<1>, E error)
      : storage_(std::in_place_index<1>, std::move(error)) {}

  E& get_error() {
    if (has_value()) {
      throw std::logic_error("Result error is not active");
    }
    return std::get<1>(storage_);
  }

  const E& get_error() const {
    if (has_value()) {
      throw std::logic_error("Result error is not active");
    }
    return std::get<1>(storage_);
  }

  std::variant<std::monostate, E> storage_;
};

}  // namespace ministream
