#include "core/net/udp_endpoint.hpp"
#include "netprobe/options.hpp"
#include "netprobe/report.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             ministream::SteadyClock::now().time_since_epoch())
      .count();
}

void put_u64(std::span<std::byte, 8> output, std::uint64_t value) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    output[i] = static_cast<std::byte>((value >> ((7U - i) * 8U)) & 0xFFU);
  }
}

std::uint64_t get_u64(std::span<const std::byte, 8> input) {
  std::uint64_t value = 0;
  for (auto byte : input) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

int listen(const ministream::netprobe::Options& options) {
  ministream::UdpEndpoint endpoint;
  if (!endpoint.bind(options.port)) {
    std::cerr << "Unable to bind UDP port " << options.port << "\n";
    return 2;
  }
  std::cout << "Listening on UDP " << options.port << "\n";
  for (;;) {
    const auto packet = endpoint.receive(1s);
    if (!packet) {
      if (packet.error() == ministream::NetError::Timeout) {
        continue;
      }
      return 3;
    }
    if (!endpoint.reply(packet->datagram.bytes)) {
      return 4;
    }
  }
}

int connect(const ministream::netprobe::Options& options) {
  ministream::UdpEndpoint endpoint;
  if (!endpoint.bind(0) || !endpoint.set_remote(options.host, options.port)) {
    std::cerr << "Unable to open UDP path to " << options.host << ':' << options.port << "\n";
    return 2;
  }

  constexpr std::size_t kPayloadBytes = 1000;
  const auto interval = std::chrono::duration<double>(
      static_cast<double>(kPayloadBytes * 8U) /
      (static_cast<double>(options.rate_mbps) * 1'000'000.0));
  const auto start = ministream::SteadyClock::now();
  const auto finish = start + std::chrono::seconds{options.duration_seconds};
  auto next_send = start;
  ministream::netprobe::Report report;

  while (ministream::SteadyClock::now() < finish) {
    const auto now = ministream::SteadyClock::now();
    if (now >= next_send) {
      std::vector<std::byte> payload(kPayloadBytes);
      put_u64(std::span<std::byte, 8>{payload.data(), 8}, report.sent);
      put_u64(
          std::span<std::byte, 8>{payload.data() + 8, 8},
          static_cast<std::uint64_t>(now_us()));
      if (endpoint.send(payload)) {
        ++report.sent;
      }
      next_send += std::chrono::duration_cast<ministream::SteadyClock::duration>(interval);
    }
    while (const auto echoed = endpoint.try_receive()) {
      if (echoed->datagram.bytes.size() >= 16) {
        const auto sent_at = get_u64(
            std::span<const std::byte, 8>{echoed->datagram.bytes.data() + 8, 8});
        report.rtt_ms.push_back(
            static_cast<double>(now_us() - static_cast<std::int64_t>(sent_at)) / 1000.0);
        ++report.received;
        report.payload_bytes += echoed->datagram.bytes.size();
      }
    }
    if (now < next_send) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::microseconds>(next_send - now);
      std::this_thread::sleep_for(std::min(200us, remaining));
    }
  }

  const auto drain_until = ministream::SteadyClock::now() + 100ms;
  while (ministream::SteadyClock::now() < drain_until) {
    if (const auto echoed = endpoint.try_receive(); echoed && echoed->datagram.bytes.size() >= 16) {
      const auto sent_at = get_u64(
          std::span<const std::byte, 8>{echoed->datagram.bytes.data() + 8, 8});
      report.rtt_ms.push_back(
          static_cast<double>(now_us() - static_cast<std::int64_t>(sent_at)) / 1000.0);
      ++report.received;
      report.payload_bytes += echoed->datagram.bytes.size();
    }
  }
  report.elapsed_seconds = std::chrono::duration<double>(
                               ministream::SteadyClock::now() - start)
                               .count();
  report.print();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = ministream::netprobe::parse_options(
      std::span<const char* const>{argv, static_cast<std::size_t>(argc)});
  if (!options) {
    std::cerr << "Usage:\n  ministream-netprobe --listen <port>\n"
                 "  ministream-netprobe --connect <host> [--port <port>] "
                 "[--rate-mbps <1..1000>] [--duration <seconds>]\n";
    return 1;
  }
  return options->mode == ministream::netprobe::Mode::Listen
             ? listen(*options)
             : connect(*options);
}
