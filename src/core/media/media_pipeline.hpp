#pragma once

#include "core/audio/audio_packet.hpp"
#include "core/fec/video_fec.hpp"
#include "core/security/session_crypto.hpp"
#include "core/transport/packet_scheduler.hpp"
#include "core/transport/reassembler.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ministream {

// Returns the approximate wire bitrate required for an encoder target after
// media headers, authenticated encryption, and the configured FEC parity are
// included in the scheduler budget.
std::uint64_t required_video_wire_rate(std::uint64_t encoder_bitrate,
                                       double fec_ratio) noexcept;

// The media pipeline owns the boundary between codec output and the wire.
// Codecs hand it encoded frames, it authenticates each packet, and the
// scheduler applies the same priority/deadline policy to every stream.
class MediaSender {
 public:
  MediaSender(SessionId session_id, SessionCrypto& crypto, PacketScheduler& scheduler);

  std::size_t enqueue_video(const EncodedFrame& frame, SteadyClock::time_point now,
                            Microseconds deadline = Microseconds{25'000});
  void set_fec_ratio(double ratio) noexcept;
  [[nodiscard]] double fec_ratio() const noexcept;
  bool enqueue_audio(const AudioPacket& packet, SteadyClock::time_point now,
                     Microseconds deadline = Microseconds{40'000});

 private:
  SessionId session_id_;
  SessionCrypto& crypto_;
  PacketScheduler& scheduler_;
  double fec_ratio_{};
};

class MediaReceiver {
 public:
  explicit MediaReceiver(SessionId session_id, SessionCrypto& crypto,
                         ReassemblyConfig reassembly = {});

  std::optional<EncodedFrame> receive_video(const Datagram& encrypted,
                                            SteadyClock::time_point now);
  std::optional<EncodedFrame> receive_video_fec(const Datagram& encrypted,
                                                SteadyClock::time_point now);
  std::optional<AudioPacket> receive_audio(const Datagram& encrypted);
  std::vector<std::uint32_t> expire_video(SteadyClock::time_point now);
  [[nodiscard]] std::uint64_t fec_recovered_frames() const noexcept;
  [[nodiscard]] std::uint64_t fec_unrecoverable_frames() const noexcept;
  [[nodiscard]] std::uint64_t received_video_packets() const noexcept;
  [[nodiscard]] std::uint64_t lost_video_packets() const noexcept;
  [[nodiscard]] std::uint64_t recovered_video_packets() const noexcept;

 private:
  std::optional<std::vector<std::byte>> open(PacketType expected,
                                             const Datagram& encrypted);

  SessionId session_id_;
  SessionCrypto& crypto_;
  VideoFecReassembler reassembler_;
};

}  // namespace ministream
