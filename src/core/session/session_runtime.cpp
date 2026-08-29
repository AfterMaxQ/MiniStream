#include "core/session/session_runtime.hpp"

#include <array>
#include <utility>

namespace ministream {

SessionRuntime::SessionRuntime(SessionServices services) : services_(std::move(services)) {}

Result<void, RuntimeError> SessionRuntime::start() {
  if (state_ != SessionState::Idle) {
    return Result<void, RuntimeError>::err({"session"});
  }

  state_ = SessionState::Connecting;
  std::array<SessionService*, 4> ordered{
      &services_.transport, &services_.video, &services_.audio, &services_.input};
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    auto& service = *ordered[index];
    if (!service.start) {
      continue;
    }
    if (index == 1) {
      state_ = SessionState::Negotiating;
    }
    if (!service.start()) {
      stop_started();
      state_ = SessionState::Failed;
      return Result<void, RuntimeError>::err({service.name});
    }
    started_count_ = static_cast<unsigned>(index + 1);
  }

  state_ = SessionState::Streaming;
  return Result<void, RuntimeError>::ok();
}

Result<void, RuntimeError> SessionRuntime::stop() {
  if (state_ != SessionState::Streaming && state_ != SessionState::Recovering) {
    return Result<void, RuntimeError>::err({"session"});
  }
  state_ = SessionState::Disconnecting;
  stop_started();
  state_ = SessionState::Idle;
  return Result<void, RuntimeError>::ok();
}

bool SessionRuntime::reset_failure() noexcept {
  if (state_ != SessionState::Failed) {
    return false;
  }
  state_ = SessionState::Idle;
  return true;
}

SessionState SessionRuntime::state() const noexcept { return state_; }

void SessionRuntime::stop_started() noexcept {
  std::array<SessionService*, 4> ordered{
      &services_.transport, &services_.video, &services_.audio, &services_.input};
  while (started_count_ > 0) {
    --started_count_;
    if (ordered[started_count_]->stop) {
      ordered[started_count_]->stop();
    }
  }
}

}  // namespace ministream
