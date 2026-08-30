#include "windows/audio/wasapi_output.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <deque>

namespace ministream {

using Microsoft::WRL::ComPtr;

struct WasapiOutput::Impl {
  ComPtr<IAudioClient> client;
  ComPtr<IAudioRenderClient> render;
  std::deque<float> pending;
  UINT buffer_frames{};
  bool com_owned{};
  bool started{};
};

WasapiOutput::WasapiOutput() : impl_(std::make_unique<Impl>()) {}
WasapiOutput::~WasapiOutput() { stop(); }
WasapiOutput::WasapiOutput(WasapiOutput&&) noexcept = default;
WasapiOutput& WasapiOutput::operator=(WasapiOutput&&) noexcept = default;

Result<void, AudioOutputError> WasapiOutput::start() {
  stop();
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  impl_->com_owned = SUCCEEDED(initialized);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return Result<void, AudioOutputError>::err(AudioOutputError::Initialize);
  }
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> endpoint;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator))) ||
      FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint)) ||
      FAILED(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(impl_->client.GetAddressOf())))) {
    stop();
    return Result<void, AudioOutputError>::err(AudioOutputError::Initialize);
  }
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  format.nChannels = 2;
  format.nSamplesPerSec = 48'000;
  format.wBitsPerSample = 32;
  format.nBlockAlign = sizeof(float) * 2;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  if (FAILED(impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 10'000'000, 0,
                                       &format, nullptr)) ||
      FAILED(impl_->client->GetBufferSize(&impl_->buffer_frames)) ||
      FAILED(impl_->client->GetService(IID_PPV_ARGS(&impl_->render))) ||
      FAILED(impl_->client->Start())) {
    stop();
    return Result<void, AudioOutputError>::err(AudioOutputError::Start);
  }
  impl_->pending.clear();
  impl_->pending.resize(0);
  impl_->started = true;
  return Result<void, AudioOutputError>::ok();
}

Result<void, AudioOutputError> WasapiOutput::push(std::span<const float> samples) {
  if (!impl_->started) {
    return Result<void, AudioOutputError>::err(AudioOutputError::Stopped);
  }
  if (samples.empty() || samples.size() % 2 != 0) {
    return Result<void, AudioOutputError>::err(AudioOutputError::Format);
  }
  constexpr std::size_t max_samples = 48'000U * 2U / 5U;
  if (impl_->pending.size() + samples.size() > max_samples) {
    return Result<void, AudioOutputError>::err(AudioOutputError::BufferFull);
  }
  impl_->pending.insert(impl_->pending.end(), samples.begin(), samples.end());

  UINT padding{};
  if (FAILED(impl_->client->GetCurrentPadding(&padding))) {
    return Result<void, AudioOutputError>::err(AudioOutputError::Start);
  }
  const auto available = impl_->buffer_frames > padding ? impl_->buffer_frames - padding : 0;
  const auto frames = std::min<std::size_t>(available, impl_->pending.size() / 2U);
  if (frames == 0) {
    return Result<void, AudioOutputError>::ok();
  }
  BYTE* destination{};
  if (FAILED(impl_->render->GetBuffer(static_cast<UINT32>(frames), &destination))) {
    return Result<void, AudioOutputError>::err(AudioOutputError::Start);
  }
  auto* out_samples = reinterpret_cast<float*>(destination);
  std::copy_n(impl_->pending.begin(), frames * 2U, out_samples);
  if (FAILED(impl_->render->ReleaseBuffer(static_cast<UINT32>(frames), 0))) {
    return Result<void, AudioOutputError>::err(AudioOutputError::Start);
  }
  impl_->pending.erase(impl_->pending.begin(),
                       impl_->pending.begin() + static_cast<std::ptrdiff_t>(frames * 2U));
  return Result<void, AudioOutputError>::ok();
}

void WasapiOutput::stop() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->client && impl_->started) {
    impl_->client->Stop();
  }
  impl_->started = false;
  impl_->pending.clear();
  impl_->render.Reset();
  impl_->client.Reset();
  impl_->buffer_frames = 0;
  if (impl_->com_owned) {
    CoUninitialize();
    impl_->com_owned = false;
  }
}

bool WasapiOutput::started() const noexcept { return impl_ && impl_->started; }

}  // namespace ministream
