#pragma once

#include "core/base/result.hpp"
#include "core/transport/packetizer.hpp"
#include "core/video/codec_config.hpp"
#include "windows/video/dxgi_capture.hpp"

#include <cstdint>
#include <memory>

namespace ministream {

enum class NvencError {
  Unavailable,
  InvalidConfig,
  Initialize,
  UnsupportedFormat,
  RegisterResource,
  Encode,
  LockBitstream,
  Reconfigure,
};

struct NvencConfig {
  VideoCodec codec{VideoCodec::H264};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps{60};
  std::uint32_t bitrate_bps{20'000'000};
  bool hdr10{};
};

class NvencEncoder {
 public:
  NvencEncoder();
  ~NvencEncoder();
  NvencEncoder(NvencEncoder&&) noexcept;
  NvencEncoder& operator=(NvencEncoder&&) noexcept;
  NvencEncoder(const NvencEncoder&) = delete;
  NvencEncoder& operator=(const NvencEncoder&) = delete;

  Result<void, NvencError> initialize(ID3D11Device* device,
                                      ID3D11DeviceContext* context,
                                      NvencConfig config);
  Result<EncodedFrame, NvencError> encode(const CapturedFrame& frame,
                                          std::uint64_t timestamp_us,
                                          bool force_idr = false);
  Result<void, NvencError> reconfigure_bitrate(std::uint32_t bitrate_bps);
  void request_idr() noexcept;
  void stop() noexcept;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] const CodecConfig& codec_config() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
