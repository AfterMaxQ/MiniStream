#include "windows/platform/host_capabilities.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <string>

namespace {

std::string line(const char* name, const ministream::CapabilityStatus& capability) {
  std::ostringstream out;
  out << name << "\t" << (capability.ready ? "READY" : "ACTION REQUIRED")
      << "\t" << capability.detail << "\r\n";
  return out.str();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const auto capabilities = ministream::inspect_host_capabilities();
  std::ostringstream message;
  message << "MiniStream Host\r\n\r\n"
          << line("Video", capabilities.video)
          << line("Audio", capabilities.audio)
          << line("Controller", capabilities.controller)
          << line("Network", capabilities.network) << "\r\n";
  message << (capabilities.ready()
                  ? "This PC is ready to host."
                  : "Resolve the items marked ACTION REQUIRED, then start MiniStream again.");
  MessageBoxA(nullptr, message.str().c_str(), "MiniStream Host", MB_OK | MB_ICONINFORMATION);
  return capabilities.ready() ? 0 : 1;
}
