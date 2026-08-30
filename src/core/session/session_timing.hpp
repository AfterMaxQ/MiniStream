#pragma once

#include "core/time/clock.hpp"

#include <chrono>

namespace ministream {

struct SessionTiming {
  Microseconds handshake_lease{std::chrono::seconds{2}};
  Microseconds pairing_lease{std::chrono::seconds{60}};
  Microseconds confirmation_retry_interval{std::chrono::milliseconds{250}};
  Microseconds confirmation_grace{std::chrono::seconds{1}};
  Microseconds confirmation_grace_interval{std::chrono::milliseconds{100}};
  Microseconds heartbeat_interval{std::chrono::milliseconds{500}};
  Microseconds liveness_timeout{std::chrono::seconds{3}};
};

}  // namespace ministream
