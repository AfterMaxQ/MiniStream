#include "windows/audio/wasapi_loopback.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace ministream {

struct WasapiLoopback::Impl {
  Microsoft::WRL::ComPtr<IAudioClient> client;
  Microsoft::WRL::ComPtr<IAudioCaptureClient> capture;
  bool com_owned{};
  bool started{};
};

WasapiLoopback::WasapiLoopback() : impl_(std::make_unique<Impl>()) {}

WasapiLoopback::~WasapiLoopback() {
  if (impl_->started) {
    impl_->client->Stop();
  }
  impl_->capture.Reset();
  impl_->client.Reset();
  if (impl_->com_owned) {
    CoUninitialize();
  }
}

WasapiLoopback::WasapiLoopback(WasapiLoopback&&) noexcept = default;
WasapiLoopback& WasapiLoopback::operator=(WasapiLoopback&&) noexcept = default;

Result<void, AudioCaptureError> WasapiLoopback::start() {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  impl_->com_owned = SUCCEEDED(initialized);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return Result<void, AudioCaptureError>::err(AudioCaptureError::Initialize);
  }

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  Microsoft::WRL::ComPtr<IMMDevice> endpoint;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator))) ||
      FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint)) ||
      FAILED(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(impl_->client.GetAddressOf())))) {
    return Result<void, AudioCaptureError>::err(AudioCaptureError::Initialize);
  }

  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  format.nChannels = 2;
  format.nSamplesPerSec = 48'000;
  format.wBitsPerSample = 32;
  format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK |
                      AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  if (FAILED(impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format,
                                       nullptr))) {
    return Result<void, AudioCaptureError>::err(AudioCaptureError::UnsupportedFormat);
  }
  if (FAILED(impl_->client->GetService(IID_PPV_ARGS(&impl_->capture))) ||
      FAILED(impl_->client->Start())) {
    return Result<void, AudioCaptureError>::err(AudioCaptureError::Start);
  }
  impl_->started = true;
  return Result<void, AudioCaptureError>::ok();
}

Result<PcmBlock, AudioCaptureError> WasapiLoopback::read() {
  if (!impl_->started) {
    if (auto started = start(); !started) {
      return Result<PcmBlock, AudioCaptureError>::err(started.error());
    }
  }

  UINT32 available{};
  if (FAILED(impl_->capture->GetNextPacketSize(&available))) {
    return Result<PcmBlock, AudioCaptureError>::err(AudioCaptureError::Read);
  }
  if (available == 0) {
    return Result<PcmBlock, AudioCaptureError>::err(AudioCaptureError::NoData);
  }

  BYTE* data{};
  UINT32 frames{};
  DWORD flags{};
  if (FAILED(impl_->capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
    return Result<PcmBlock, AudioCaptureError>::err(AudioCaptureError::Read);
  }
  PcmBlock block;
  block.host_timestamp_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  block.frames = frames;
  block.discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
  block.interleaved_stereo.resize(static_cast<std::size_t>(frames) * 2U);
  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
    std::memcpy(block.interleaved_stereo.data(), data,
                block.interleaved_stereo.size() * sizeof(float));
  }
  if (FAILED(impl_->capture->ReleaseBuffer(frames))) {
    return Result<PcmBlock, AudioCaptureError>::err(AudioCaptureError::Read);
  }
  return Result<PcmBlock, AudioCaptureError>::ok(std::move(block));
}

}  // namespace ministream
