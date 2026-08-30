#include "core/audio/audio_packet.hpp"
#include "core/media/media_pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>

using namespace ministream;

namespace {
std::array<std::byte, 32> test_key() {
  std::array<std::byte, 32> key{};
  for (std::size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<std::byte>(i + 1U);
  }
  return key;
}
}  // namespace

TEST_CASE("media sender authenticates video and audio without exceeding MTU") {
  const auto key = test_key();
  SessionCrypto tx{7, key, key, 11, 11};
  SessionCrypto rx{7, key, key, 11, 11};
  PacketScheduler scheduler;
  MediaSender sender{7, tx, scheduler};
  MediaReceiver receiver{7, rx};
  const auto now = SteadyClock::now();
  scheduler.set_video_rate(1'000'000'000);

  EncodedFrame frame{42, 1234, true, std::vector<std::byte>(kVideoShardPayloadBytes * 2 + 3)};
  for (std::size_t i = 0; i < frame.bytes.size(); ++i) {
    frame.bytes[i] = static_cast<std::byte>(i & 0xFFU);
  }
  REQUIRE(sender.enqueue_video(frame, now, std::chrono::seconds{1}) == 3);

  std::optional<EncodedFrame> restored;
  for (int attempt = 0; attempt < 8 && !restored; ++attempt) {
    auto packet = scheduler.next(now + std::chrono::milliseconds{attempt * 20});
    if (!packet) {
      continue;
    }
    REQUIRE(packet->bytes.size() <= kMaxDatagramBytes);
    if (auto decoded = receiver.receive_video(*packet, now)) {
      restored = std::move(decoded);
    }
  }
  REQUIRE(restored.has_value());
  REQUIRE(restored->bytes == frame.bytes);

  AudioPacket audio{9, 555, 480, std::vector<std::byte>{std::byte{1}, std::byte{2}}};
  REQUIRE(sender.enqueue_audio(audio, now));
  auto audio_datagram = scheduler.next(now);
  REQUIRE(audio_datagram.has_value());
  REQUIRE(audio_datagram->bytes.size() <= kMaxDatagramBytes);
  const auto decoded_audio = receiver.receive_audio(*audio_datagram);
  REQUIRE(decoded_audio.has_value());
  REQUIRE(decoded_audio->sequence == audio.sequence);
  REQUIRE(decoded_audio->sample_count == audio.sample_count);
  REQUIRE(decoded_audio->opus == audio.opus);
}
