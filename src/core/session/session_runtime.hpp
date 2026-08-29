#pragma once

#include "core/base/result.hpp"
#include "core/session/session_state.hpp"

#include <functional>
#include <string>

namespace ministream {

struct SessionService {
  std::string name;
  std::function<bool()> start;
  std::function<void()> stop;
};

struct SessionServices {
  SessionService transport;
  SessionService video;
  SessionService audio;
  SessionService input;
};

struct RuntimeError {
  std::string component;
};

class SessionRuntime {
 public:
  explicit SessionRuntime(SessionServices services);

  Result<void, RuntimeError> start();
  Result<void, RuntimeError> stop();
  bool reset_failure() noexcept;
  [[nodiscard]] SessionState state() const noexcept;

 private:
  void stop_started() noexcept;

  SessionServices services_;
  SessionState state_{SessionState::Idle};
  unsigned started_count_{};
};

}  // namespace ministream
