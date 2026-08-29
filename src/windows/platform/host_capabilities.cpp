#include "windows/platform/host_capabilities.hpp"

#include "core/net/udp_endpoint.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dxgi.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdint>

namespace ministream {
namespace {

using Microsoft::WRL::ComPtr;

CapabilityStatus inspect_nvenc() {
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

}  // namespace

HostCapabilities inspect_host_capabilities() {
  return {inspect_nvenc(), inspect_wasapi(), inspect_vigem(), inspect_network()};
}

}  // namespace ministream
