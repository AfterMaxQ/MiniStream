#pragma once

#include "core/net/udp_endpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace ministream {

inline bool loopback_payloads_in_order(const std::vector<ReceivedDatagram>& packets,
                                       std::uint8_t first_value) {
  for (std::size_t index = 0; index < packets.size(); ++index) {
    const auto& bytes = packets[index].datagram.bytes;
    if (bytes.size() != 1 || bytes.front() != static_cast<std::byte>(
                                     static_cast<std::uint8_t>(first_value + index))) {
      return false;
    }
  }
  return true;
}

}  // namespace ministream
