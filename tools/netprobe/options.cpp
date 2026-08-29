#include "netprobe/options.hpp"

#include <charconv>
#include <limits>
#include <string_view>

namespace ministream::netprobe {
namespace {
template <class T>
bool parse_number(std::string_view text, T& output) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}  // namespace

std::optional<Options> parse_options(std::span<const char* const> arguments) {
  if (arguments.size() < 3) {
    return std::nullopt;
  }
  Options options;
  bool mode_seen = false;
  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string_view argument = arguments[i];
    if (argument == "--listen" && i + 1 < arguments.size() && !mode_seen) {
      mode_seen = true;
      options.mode = Mode::Listen;
      if (!parse_number(std::string_view{arguments[++i]}, options.port) || options.port == 0) {
        return std::nullopt;
      }
    } else if (argument == "--connect" && i + 1 < arguments.size() && !mode_seen) {
      mode_seen = true;
      options.mode = Mode::Connect;
      options.host = arguments[++i];
      if (options.host.empty()) {
        return std::nullopt;
      }
    } else if (argument == "--rate-mbps" && i + 1 < arguments.size()) {
      if (!parse_number(std::string_view{arguments[++i]}, options.rate_mbps) ||
          options.rate_mbps == 0 || options.rate_mbps > 1000) {
        return std::nullopt;
      }
    } else if (argument == "--duration" && i + 1 < arguments.size()) {
      if (!parse_number(std::string_view{arguments[++i]}, options.duration_seconds) ||
          options.duration_seconds == 0 || options.duration_seconds > 3600) {
        return std::nullopt;
      }
    } else if (argument == "--port" && i + 1 < arguments.size()) {
      if (!parse_number(std::string_view{arguments[++i]}, options.port) || options.port == 0) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  return mode_seen ? std::optional<Options>{std::move(options)} : std::nullopt;
}

}  // namespace ministream::netprobe
