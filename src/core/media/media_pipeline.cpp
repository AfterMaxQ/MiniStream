#include "core/media/media_pipeline.hpp"

#include "core/protocol/wire.hpp"
#include "core/transport/packetizer.hpp"

#include <algorithm>

namespace ministream {

MediaSender::MediaSender(SessionId session_id, SessionCrypto& crypto,
                         PacketScheduler& scheduler)
    : session_id_(session_id), crypto_(crypto), scheduler_(scheduler) {}

std::size_t MediaSender::enqueue_video(const EncodedFrame& frame,
                                       SteadyClock::time_point now,
                                       Microseconds deadline) {
  std::size_t queued = 0;
  for (const auto& packet : packetize_video(frame, session_id_)) {
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
  return reassembler_.push(packet, now);
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

}  // namespace ministream
