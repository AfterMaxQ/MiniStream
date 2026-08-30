#pragma once

#include "core/base/result.hpp"
#include "core/video/codec_config.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace ministream {

enum class MfDecodeError {
  Unavailable,
  InvalidConfig,
  UnsupportedCodec,
  Initialize,
  Input,
  Output,
};

class MfDecoder {
 public:
  MfDecoder();
  ~MfDecoder();
  MfDecoder(MfDecoder&&) noexcept;
  MfDecoder& operator=(MfDecoder&&) noexcept;
  MfDecoder(const MfDecoder&) = delete;
  MfDecoder& operator=(const MfDecoder&) = delete;

  static bool hardware_available(VideoCodec codec) noexcept;
  Result<void, MfDecodeError> start();
  Result<void, MfDecodeError> configure(const CodecConfig& config);
  Result<void, MfDecodeError> decode(std::span<const std::byte> encoded,
                                     std::uint64_t timestamp_us);
  struct TextureFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    std::uint64_t timestamp_us{};
    std::uint32_t width{};
    std::uint32_t height{};
  };
  std::optional<TextureFrame> take_latest();
  void stop() noexcept;
  [[nodiscard]] bool ready() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
