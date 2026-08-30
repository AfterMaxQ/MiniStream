#include "core/media/media_pipeline.hpp"

#include "core/protocol/wire.hpp"
#include "core/transport/packetizer.hpp"

#include <algorithm>

namespace ministream {
namespace {

bool sequence_newer(std::uint32_t current, std::uint32_t previous) noexcept {
  return static_cast<std::int32_t>(current - previous) > 0;
}

}  // namespace


MediaSender::MediaSender(SessionId session_id, SessionCrypto& crypto,
                         PacketScheduler& scheduler)
    : session_id_(session_id), crypto_(crypto), scheduler_(scheduler) {}

void MediaSender::set_fec_ratio(double ratio) noexcept {
  fec_ratio_ = std::clamp(ratio, 0.0, 1.0);
}

double MediaSender::fec_ratio() const noexcept { return fec_ratio_; }

std::size_t MediaSender::enqueue_video(const EncodedFrame& frame,
                                       SteadyClock::time_point now,
                                       Microseconds deadline) {
  std::size_t queued = 0;
  const auto fec_frame = VideoFecEncoder{session_id_}.encode_frame(frame, fec_ratio_);
  for (const auto& packet : fec_frame.video_datagrams) {
    const auto bytes = std::span<const std::byte>{packet.bytes};
    const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
    if (!common || bytes.size() != kCommonHeaderBytes + common->payload_bytes) {
      continue;
    }
    const auto sealed = crypto_.seal(PacketType::Video,
                                     bytes.subspan(kCommonHeaderBytes));
    if (!sealed || !scheduler_.enqueue(Priority::Video, *sealed, now + deadline)) {
      continue;
    }
    ++queued;
  }
  for (const auto& packet : fec_frame.fec_datagrams) {
    const auto bytes = std::span<const std::byte>{packet.bytes};
    if (bytes.size() < kCommonHeaderBytes) {
      continue;
    }
    const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
    if (!common || common->type != PacketType::VideoFec ||
        bytes.size() != kCommonHeaderBytes + common->payload_bytes) {
      continue;
    }
    const auto sealed = crypto_.seal(PacketType::VideoFec,
                                     bytes.subspan(kCommonHeaderBytes));
    if (sealed && scheduler_.enqueue(Priority::Video, *sealed, now + deadline)) {
      ++queued;
    }
  }
  return queued;
}

bool MediaSender::enqueue_audio(const AudioPacket& packet,
                                SteadyClock::time_point now,
                                Microseconds deadline) {
  const auto payload = encode_audio_packet(packet);
  if (payload.empty()) {
    return false;
  }
  const auto sealed = crypto_.seal(PacketType::Audio, payload);
  return sealed && scheduler_.enqueue(Priority::Audio, *sealed, now + deadline);
}

MediaReceiver::MediaReceiver(SessionId session_id, SessionCrypto& crypto,
                             ReassemblyConfig reassembly)
    : session_id_(session_id), crypto_(crypto), reassembler_(reassembly) {}

std::optional<std::vector<std::byte>> MediaReceiver::open(
    PacketType expected, const Datagram& encrypted) {
  const auto bytes = std::span<const std::byte>{encrypted.bytes};
  if (bytes.size() < kCommonHeaderBytes) {
    return std::nullopt;
  }
  const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
  if (!common || common->session_id != session_id_ || common->type != expected) {
    return std::nullopt;
  }
  auto plaintext = crypto_.open(encrypted);
  if (!plaintext) {
    return std::nullopt;
  }
  return *plaintext;
}

std::optional<EncodedFrame> MediaReceiver::receive_video(
    const Datagram& encrypted, SteadyClock::time_point now) {
  auto plaintext = open(PacketType::Video, encrypted);
  if (!plaintext) {
    return std::nullopt;
  }
  Datagram packet;
  const auto header = encode_common_header(
      {PacketType::Video, session_id_, static_cast<std::uint16_t>(plaintext->size())});
  packet.bytes.reserve(header.size() + plaintext->size());
  packet.bytes.insert(packet.bytes.end(), header.begin(), header.end());
  packet.bytes.insert(packet.bytes.end(), plaintext->begin(), plaintext->end());
  const auto shard = parse_video_datagram(packet);
  if (!shard) {
    return std::nullopt;
  }
  ++received_video_packets_;
  if (!last_video_sequence_ || sequence_newer(shard->header.packet_seq, *last_video_sequence_)) {
    if (last_video_frame_id_ && *last_video_frame_id_ == shard->header.frame_id) {
      const auto gap = shard->header.packet_seq - *last_video_sequence_;
      if (gap > 1U && gap <= kMaxVideoShards) {
        lost_video_packets_ += gap - 1U;
      }
    }
    last_video_sequence_ = shard->header.packet_seq;
    last_video_frame_id_ = shard->header.frame_id;
  }
  return reassembler_.push_data(*shard, now);
}

std::optional<EncodedFrame> MediaReceiver::receive_video_fec(
    const Datagram& encrypted, SteadyClock::time_point now) {
  auto plaintext = open(PacketType::VideoFec, encrypted);
  return plaintext ? reassembler_.push_parity(*plaintext, now) : std::nullopt;
}

std::optional<AudioPacket> MediaReceiver::receive_audio(const Datagram& encrypted) {
  auto plaintext = open(PacketType::Audio, encrypted);
  if (!plaintext) {
    return std::nullopt;
  }
  return decode_audio_packet(*plaintext);
}

std::vector<std::uint32_t> MediaReceiver::expire_video(SteadyClock::time_point now) {
  return reassembler_.expire(now);
}

std::uint64_t MediaReceiver::fec_recovered_frames() const noexcept {
  return reassembler_.recovered_frames();
}

std::uint64_t MediaReceiver::fec_unrecoverable_frames() const noexcept {
  return reassembler_.unrecoverable_frames();
}

std::uint64_t MediaReceiver::received_video_packets() const noexcept {
  return received_video_packets_;
}

std::uint64_t MediaReceiver::lost_video_packets() const noexcept {
  return lost_video_packets_;
}

}  // namespace ministream
