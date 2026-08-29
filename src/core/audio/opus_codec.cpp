#include "core/audio/opus_codec.hpp"

#include <opus.h>

#include <array>

namespace ministream {

OpusEncoder48kStereo::OpusEncoder48kStereo() {
  int error = OPUS_OK;
  encoder_ = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
  if (error != OPUS_OK) {
    encoder_ = nullptr;
    return;
  }
  opus_encoder_ctl(static_cast<OpusEncoder*>(encoder_), OPUS_SET_BITRATE(256000));
  opus_encoder_ctl(static_cast<OpusEncoder*>(encoder_), OPUS_SET_COMPLEXITY(5));
  opus_encoder_ctl(static_cast<OpusEncoder*>(encoder_), OPUS_SET_INBAND_FEC(1));
  opus_encoder_ctl(static_cast<OpusEncoder*>(encoder_), OPUS_SET_PACKET_LOSS_PERC(5));
}

OpusEncoder48kStereo::~OpusEncoder48kStereo() {
  if (encoder_) {
    opus_encoder_destroy(static_cast<OpusEncoder*>(encoder_));
  }
}

bool OpusEncoder48kStereo::ready() const noexcept { return encoder_ != nullptr; }

Result<std::vector<std::byte>, AudioCodecError> OpusEncoder48kStereo::encode(
    std::span<const float> interleaved_stereo) {
  if (!encoder_) {
    return Result<std::vector<std::byte>, AudioCodecError>::err(
        AudioCodecError::Initialization);
  }
  if (interleaved_stereo.size() != kOpusFrameSamplesPerChannel * 2) {
    return Result<std::vector<std::byte>, AudioCodecError>::err(
        AudioCodecError::InvalidFrame);
  }
  std::vector<std::byte> packet(1200);
  const auto bytes = opus_encode_float(
      static_cast<OpusEncoder*>(encoder_), interleaved_stereo.data(),
      static_cast<int>(kOpusFrameSamplesPerChannel),
      reinterpret_cast<unsigned char*>(packet.data()), static_cast<opus_int32>(packet.size()));
  if (bytes < 0) {
    return Result<std::vector<std::byte>, AudioCodecError>::err(AudioCodecError::Encode);
  }
  packet.resize(static_cast<std::size_t>(bytes));
  return Result<std::vector<std::byte>, AudioCodecError>::ok(std::move(packet));
}

OpusDecoder48kStereo::OpusDecoder48kStereo() {
  int error = OPUS_OK;
  decoder_ = opus_decoder_create(48000, 2, &error);
  if (error != OPUS_OK) {
    decoder_ = nullptr;
  }
}

OpusDecoder48kStereo::~OpusDecoder48kStereo() {
  if (decoder_) {
    opus_decoder_destroy(static_cast<OpusDecoder*>(decoder_));
  }
}

bool OpusDecoder48kStereo::ready() const noexcept { return decoder_ != nullptr; }

Result<std::vector<float>, AudioCodecError> OpusDecoder48kStereo::decode(
    std::span<const std::byte> packet) {
  if (packet.empty()) {
    return Result<std::vector<float>, AudioCodecError>::err(AudioCodecError::InvalidFrame);
  }
  return decode_impl(
      reinterpret_cast<const unsigned char*>(packet.data()), packet.size(), false);
}

Result<std::vector<float>, AudioCodecError> OpusDecoder48kStereo::decode_loss() {
  return decode_impl(nullptr, 0, false);
}

Result<std::vector<float>, AudioCodecError> OpusDecoder48kStereo::decode_impl(
    const unsigned char* packet, std::size_t bytes, bool fec) {
  if (!decoder_) {
    return Result<std::vector<float>, AudioCodecError>::err(AudioCodecError::Initialization);
  }
  std::vector<float> output(kOpusFrameSamplesPerChannel * 2);
  const auto frames = opus_decode_float(
      static_cast<OpusDecoder*>(decoder_), packet, static_cast<opus_int32>(bytes),
      output.data(), static_cast<int>(kOpusFrameSamplesPerChannel), fec ? 1 : 0);
  if (frames < 0) {
    return Result<std::vector<float>, AudioCodecError>::err(AudioCodecError::Decode);
  }
  output.resize(static_cast<std::size_t>(frames) * 2);
  return Result<std::vector<float>, AudioCodecError>::ok(std::move(output));
}

}  // namespace ministream
