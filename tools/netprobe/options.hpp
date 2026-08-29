#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ministream::netprobe {

enum class Mode { Listen, Connect };

struct Options {
  Mode mode{Mode::Listen};
  std::string host;
  std::uint16_t port{47990};
  std::uint32_t rate_mbps{20};
  std::uint32_t duration_seconds{30};
};

std::optional<Options> parse_options(std::span<const char* const> arguments);

}  // namespace ministream::netprobe
