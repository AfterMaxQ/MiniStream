#pragma once

namespace ministream {

enum class SessionState {
  Idle,
  Connecting,
  Negotiating,
  Streaming,
  Recovering,
  Disconnecting,
  Failed,
};

constexpr bool can_transition(SessionState from, SessionState to) noexcept {
  switch (from) {
    case SessionState::Idle:
      return to == SessionState::Connecting;
    case SessionState::Connecting:
      return to == SessionState::Negotiating || to == SessionState::Failed;
    case SessionState::Negotiating:
      return to == SessionState::Streaming || to == SessionState::Failed;
    case SessionState::Streaming:
      return to == SessionState::Recovering || to == SessionState::Disconnecting ||
             to == SessionState::Failed;
    case SessionState::Recovering:
      return to == SessionState::Streaming || to == SessionState::Disconnecting ||
             to == SessionState::Failed;
    case SessionState::Disconnecting:
      return to == SessionState::Idle || to == SessionState::Failed;
    case SessionState::Failed:
      return to == SessionState::Idle;
  }
  return false;
}

}  // namespace ministream
