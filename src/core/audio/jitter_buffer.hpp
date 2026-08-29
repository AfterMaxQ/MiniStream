#pragma once

#include "core/audio/audio_packet.hpp"
#include "core/time/clock.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>

namespace ministream {

struct AudioJitterConfig {
  Microseconds target{10000};
  Microseconds max{20000};
};

enum class AudioPlayoutKind { Packet, Plc };

struct AudioPlayoutResult {
  AudioPlayoutKind kind{AudioPlayoutKind::Plc};
  std::optional<AudioPacket> packet;
};

class AudioJitterBuffer {
 public:
  explicit AudioJitterBuffer(AudioJitterConfig config = {});
  void push(AudioPacket packet);
  AudioPlayoutResult pop(std::uint32_t expected_sequence);
  [[nodiscard]] Microseconds buffered_duration() const;

 private:
  void erase(std::uint32_t sequence);

  AudioJitterConfig config_;
  std::unordered_map<std::uint32_t, AudioPacket> packets_;
  std::deque<std::uint32_t> arrival_order_;
  std::uint64_t buffered_samples_{};
};

}  // namespace ministream
