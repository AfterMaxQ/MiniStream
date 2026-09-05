#include "windows/platform/host_capabilities.hpp"

#include "core/net/udp_endpoint.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dxgi1_6.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ministream {
namespace {

using Microsoft::WRL::ComPtr;

CapabilityStatus inspect_nvenc() {
#if !MINISTREAM_HAVE_NVENC_SDK
  return {false, "NVENC SDK headers unavailable"};
#else
  bool nvidia_adapter = false;
  ComPtr<IDXGIFactory1> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    for (UINT index = 0;; ++index) {
      ComPtr<IDXGIAdapter1> adapter;
      if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
        break;
      }
      DXGI_ADAPTER_DESC1 description{};
      if (SUCCEEDED(adapter->GetDesc1(&description)) && description.VendorId == 0x10DEU) {
        nvidia_adapter = true;
        break;
      }
    }
  }

  const auto library = LoadLibraryW(L"nvEncodeAPI64.dll");
  if (library == nullptr || !nvidia_adapter) {
    if (library != nullptr) {
      FreeLibrary(library);
    }
    return {false, nvidia_adapter ? "NVENC runtime unavailable" : "NVIDIA GPU unavailable"};
  }

  using GetMaxVersion = int(__stdcall*)(std::uint32_t*);
  const auto get_version = reinterpret_cast<GetMaxVersion>(
      GetProcAddress(library, "NvEncodeAPIGetMaxSupportedVersion"));
  std::uint32_t version{};
  const bool ready = get_version != nullptr && get_version(&version) == 0;
  FreeLibrary(library);
  return {ready, ready ? "NVENC runtime detected" : "NVENC API unavailable"};
#endif
}

CapabilityStatus inspect_wasapi() {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  CapabilityStatus result{false, "No default audio output"};
  {
    ComPtr<IMMDeviceEnumerator> enumerator;
    const auto created = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
    ComPtr<IMMDevice> endpoint;
    const auto selected = SUCCEEDED(created)
                              ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint)
                              : created;
    const bool ready = SUCCEEDED(selected);
    result = {ready, ready ? "WASAPI default output detected" : "No default audio output"};
  }
  if (SUCCEEDED(initialized)) {
    CoUninitialize();
  }
  return result;
}

CapabilityStatus inspect_vigem() {
  const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager == nullptr) {
    return {false, "Cannot inspect virtual controller driver"};
  }
  const auto service = OpenServiceW(manager, L"ViGEmBus", SERVICE_QUERY_STATUS);
  const bool installed = service != nullptr;
  if (service != nullptr) {
    CloseServiceHandle(service);
  }
  CloseServiceHandle(manager);
  return {installed, installed ? "ViGEmBus installed" : "Virtual controller driver required"};
}

CapabilityStatus inspect_network() {
  UdpEndpoint endpoint;
  const bool ready = endpoint.bind(0).has_value();
  return {ready, ready ? "UDP available" : "UDP socket unavailable"};
}

CapabilityStatus inspect_input() {
  return {true, "Keyboard and mouse available"};
}

std::pair<std::uint32_t, std::uint32_t> inspect_capture_size() {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return {0, 0};
  }
  ComPtr<IDXGIAdapter1> adapter;
  if (factory->EnumAdapters1(0, &adapter) != S_OK) {
    return {0, 0};
  }
  ComPtr<IDXGIOutput> output;
  if (adapter->EnumOutputs(0, &output) != S_OK) {
    return {0, 0};
  }
  DXGI_OUTPUT_DESC description{};
  if (FAILED(output->GetDesc(&description))) {
    return {0, 0};
  }
  const auto width = static_cast<std::uint32_t>(
      std::max<LONG>(0, description.DesktopCoordinates.right -
                            description.DesktopCoordinates.left));
  const auto height = static_cast<std::uint32_t>(
      std::max<LONG>(0, description.DesktopCoordinates.bottom -
                            description.DesktopCoordinates.top));
  return {std::min(width, 3840U), std::min(height, 2160U)};
}

}  // namespace

HostCapabilities inspect_host_capabilities() {
  const auto video = inspect_nvenc();
  const auto [max_width, max_height] = inspect_capture_size();
  bool hdr = false;
  ComPtr<IDXGIFactory1> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput> output;
  ComPtr<IDXGIOutput6> output6;
  DXGI_OUTPUT_DESC1 display{};
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->EnumAdapters1(0, &adapter)) && SUCCEEDED(adapter->EnumOutputs(0, &output)) &&
      SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&display)))
    hdr = display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  const auto capture_video = video.ready && max_width != 0 && max_height != 0
                                 ? video
                                 : CapabilityStatus{false, video.ready
                                                               ? "Display capture unavailable"
                                                               : video.detail};
  return {capture_video,
          inspect_wasapi(),
          inspect_input(),
          inspect_vigem(),
          inspect_network(),
          capture_video.ready,
          capture_video.ready,
          hdr,
          capture_video.ready ? max_width : 0U,
          capture_video.ready ? max_height : 0U,
          capture_video.ready ? 60U : 0U};
}

}  // namespace ministream
