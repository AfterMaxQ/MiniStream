#include "core/audio/audio_packet.hpp"
#include "core/media/media_pipeline.hpp"
#include "core/protocol/wire.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>

using namespace ministream;
using namespace std::chrono_literals;

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

TEST_CASE("media pipeline recovers one dropped video shard through authenticated FEC") {
  const auto key = test_key();
  SessionCrypto tx{8, key, key, 21, 21};
  SessionCrypto rx{8, key, key, 21, 21};
  PacketScheduler scheduler;
  MediaSender sender{8, tx, scheduler};
  MediaReceiver receiver{8, rx};
  sender.set_fec_ratio(0.2);
  scheduler.set_video_rate(1'000'000'000);

  EncodedFrame frame{99, 4567, true,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 4U + 3U)};
  for (std::size_t index = 0; index < frame.bytes.size(); ++index) {
    frame.bytes[index] = static_cast<std::byte>((index * 3U) & 0xFFU);
  }
  REQUIRE(sender.enqueue_video(frame, SteadyClock::time_point{}, 1s) == 6);

  std::size_t video_index = 0;
  std::size_t delivered = 0;
  std::size_t parity_delivered = 0;
  const auto delivery_time = SteadyClock::time_point{};
  std::optional<EncodedFrame> restored;
  for (int attempt = 0; attempt < 100 && !restored; ++attempt) {
    const auto packet = scheduler.next(delivery_time + attempt * 1ms);
    if (!packet) {
      continue;
    }
    REQUIRE(packet->bytes.size() <= kMaxDatagramBytes);
    const auto common = decode_common_header(
        std::span<const std::byte>{packet->bytes}.first<kCommonHeaderBytes>());
    REQUIRE(common.has_value());
    if (common->type == PacketType::Video) {
      if (video_index++ == 1) {
        continue;
      }
      ++delivered;
      restored = receiver.receive_video(*packet, delivery_time);
    } else if (common->type == PacketType::VideoFec) {
      ++parity_delivered;
      restored = receiver.receive_video_fec(*packet, delivery_time);
    }
  }
  REQUIRE(restored.has_value());
  REQUIRE(restored->bytes == frame.bytes);
  REQUIRE(receiver.fec_recovered_frames() == 1);
}

TEST_CASE("media pipeline reports an unrecoverable frame after loss exceeds parity") {
  EncodedFrame frame{101, 8901, false,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 4U + 3U)};
  const auto generated = VideoFecEncoder{10}.encode_frame(frame, 0.2);
  REQUIRE(generated.video_datagrams.size() == 5);
  REQUIRE(generated.fec_datagrams.size() == 1);

  VideoFecReassembler reassembler;
  for (std::size_t index = 0; index < generated.video_datagrams.size(); ++index) {
    if (index == 1 || index == 2) {
      continue;
    }
    const auto shard = parse_video_datagram(generated.video_datagrams[index]);
    REQUIRE(shard.has_value());
    REQUIRE_FALSE(reassembler.push_data(*shard, SteadyClock::time_point{}).has_value());
  }
  const auto parity_payload = std::span<const std::byte>{generated.fec_datagrams.front().bytes}
                                  .subspan(kCommonHeaderBytes);
  REQUIRE_FALSE(reassembler.push_parity(parity_payload, SteadyClock::time_point{}).has_value());
  REQUIRE(reassembler.expire(SteadyClock::time_point{} + 26ms).size() == 1);
  REQUIRE(reassembler.unrecoverable_frames() == 1);
}

TEST_CASE("video FEC reassembler recovers a raw shard before encryption") {
  EncodedFrame frame{100, 7890, true,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 4U + 3U)};
  const auto generated = VideoFecEncoder{9}.encode_frame(frame, 0.2);
  REQUIRE(generated.video_datagrams.size() == 5);
  REQUIRE(generated.fec_datagrams.size() == 1);

  VideoFecReassembler reassembler;
  std::optional<EncodedFrame> restored;
  for (std::size_t index = 0; index < generated.video_datagrams.size(); ++index) {
    if (index == 1) {
      continue;
    }
    const auto shard = parse_video_datagram(generated.video_datagrams[index]);
    REQUIRE(shard.has_value());
    restored = reassembler.push_data(*shard, SteadyClock::time_point{});
  }
  const auto common = decode_common_header(
      std::span<const std::byte>{generated.fec_datagrams.front().bytes}.first<kCommonHeaderBytes>());
  REQUIRE(common.has_value());
  const auto parity = decode_video_fec_payload(std::span<const std::byte>{
      generated.fec_datagrams.front().bytes}.subspan(kCommonHeaderBytes));
  REQUIRE(parity.has_value());
  restored = reassembler.push_parity(
      std::span<const std::byte>{generated.fec_datagrams.front().bytes}.subspan(kCommonHeaderBytes),
      SteadyClock::time_point{});
  REQUIRE(restored.has_value());
  REQUIRE(restored->bytes == frame.bytes);
}

TEST_CASE("media pipeline ignores parity that arrives after a complete frame") {
  const auto key = test_key();
  SessionCrypto tx{12, key, key, 41, 41};
  SessionCrypto rx{12, key, key, 41, 41};
  PacketScheduler scheduler;
  MediaSender sender{12, tx, scheduler};
  MediaReceiver receiver{12, rx};
  sender.set_fec_ratio(0.5);
  scheduler.set_video_rate(1'000'000'000);

  EncodedFrame frame{102, 9012, true,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 2U + 7U)};
  REQUIRE(sender.enqueue_video(frame, SteadyClock::time_point{}, 1s) == 5);

  std::optional<EncodedFrame> restored;
  std::size_t delivered = 0;
  for (unsigned attempt = 0; attempt < 8U; ++attempt) {
    const auto packet = scheduler.next(SteadyClock::time_point{} + attempt * 1ms);
    if (!packet) {
      continue;
    }
    ++delivered;
    const auto common = decode_common_header(
        std::span<const std::byte>{packet->bytes}.first<kCommonHeaderBytes>());
    REQUIRE(common.has_value());
    if (common->type == PacketType::Video) {
      if (const auto result = receiver.receive_video(*packet, SteadyClock::time_point{});
          result) {
        restored = result;
      }
    } else if (common->type == PacketType::VideoFec) {
      if (const auto result = receiver.receive_video_fec(*packet, SteadyClock::time_point{});
          result) {
        restored = result;
      }
    }
  }
  REQUIRE(delivered == 5);
  REQUIRE(restored.has_value());
  REQUIRE(restored->bytes == frame.bytes);
  REQUIRE(receiver.fec_recovered_frames() == 0);
  REQUIRE(receiver.fec_unrecoverable_frames() == 0);
}

TEST_CASE("video FEC rejects a data shard larger than the parity layout") {
  EncodedFrame frame{103, 9013, true,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 2U + 7U)};
  const auto generated = VideoFecEncoder{13}.encode_frame(frame, 0.5);
  REQUIRE(generated.video_datagrams.size() == 3);
  REQUIRE(generated.fec_datagrams.size() == 2);

  auto oversized = parse_video_datagram(generated.video_datagrams.front());
  REQUIRE(oversized.has_value());
  oversized->payload.resize(kVideoFecShardPayloadBytes + 1U);

  VideoFecReassembler reassembler;
  REQUIRE_FALSE(reassembler.push_data(*oversized, SteadyClock::time_point{}).has_value());
  const auto parity_payload = std::span<const std::byte>{generated.fec_datagrams.front().bytes}
                                  .subspan(kCommonHeaderBytes);
  REQUIRE_FALSE(reassembler.push_parity(parity_payload, SteadyClock::time_point{}).has_value());
}

TEST_CASE("media receiver does not count the frame sequence stride as packet loss") {
  const auto key = test_key();
  SessionCrypto tx{11, key, key, 31, 31};
  SessionCrypto rx{11, key, key, 31, 31};
  PacketScheduler scheduler;
  scheduler.set_video_rate(1'000'000'000);
  MediaSender sender{11, tx, scheduler};
  MediaReceiver receiver{11, rx};
  const auto now = SteadyClock::time_point{};
  REQUIRE(sender.enqueue_video({1, 1, false, {std::byte{1}}}, now, 1s) == 1);
  REQUIRE(sender.enqueue_video({2, 2, false, {std::byte{2}}}, now, 1s) == 1);
  for (unsigned index = 0; index < 2; ++index) {
    const auto datagram = scheduler.next(now + index * 1ms);
    REQUIRE(datagram.has_value());
    REQUIRE(receiver.receive_video(*datagram, now).has_value());
  }
  REQUIRE(receiver.lost_video_packets() == 0);
}

TEST_CASE("media receiver keeps packet ordering across sequence wraparound") {
  const auto key = test_key();
  SessionCrypto tx{13, key, key, 51, 51};
  SessionCrypto rx{13, key, key, 51, 51};
  PacketScheduler scheduler;
  scheduler.set_video_rate(1'000'000'000);
  MediaSender sender{13, tx, scheduler};
  MediaReceiver receiver{13, rx};
  const auto now = SteadyClock::time_point{};

  REQUIRE(sender.enqueue_video({131'071U, 1, false, {std::byte{1}}}, now, 1s) == 1);
  const auto before_wrap = scheduler.next(now);
  REQUIRE(before_wrap.has_value());
  REQUIRE(receiver.receive_video(*before_wrap, now).has_value());

  REQUIRE(sender.enqueue_video({131'072U, 2, false, {std::byte{2}}}, now, 1s) == 1);
  const auto after_wrap = scheduler.next(now + 1ms);
  REQUIRE(after_wrap.has_value());
  REQUIRE(receiver.receive_video(*after_wrap, now).has_value());

  REQUIRE(receiver.received_video_packets() == 2);
  REQUIRE(receiver.lost_video_packets() == 0);
}
