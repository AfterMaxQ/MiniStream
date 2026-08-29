#include "windows/input/vigem_gamepad.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ViGEmClient.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace ministream {

struct VigemGamepad::Impl {
  PVIGEM_CLIENT client{};
  PVIGEM_TARGET target{};
  std::function<void(RumbleState)> rumble_callback;
  bool connected{};
};

namespace {

std::mutex callback_mutex;
std::unordered_map<PVIGEM_TARGET, std::function<void(RumbleState)>*> callback_targets;

void rumble_notification(PVIGEM_CLIENT, PVIGEM_TARGET target, UCHAR large_motor,
                         UCHAR small_motor, UCHAR) {
  std::scoped_lock lock(callback_mutex);
  const auto found = callback_targets.find(target);
  if (found != callback_targets.end() && *found->second) {
    (*found->second)(
        {static_cast<std::uint16_t>(large_motor * 257U),
         static_cast<std::uint16_t>(small_motor * 257U)});
  }
}

}  // namespace

VigemGamepad::VigemGamepad() : impl_(std::make_unique<Impl>()) {}
VigemGamepad::~VigemGamepad() { stop(); }
VigemGamepad::VigemGamepad(VigemGamepad&&) noexcept = default;
VigemGamepad& VigemGamepad::operator=(VigemGamepad&&) noexcept = default;

Result<void, InputError> VigemGamepad::start() {
  if (impl_->connected) {
    return Result<void, InputError>::ok();
  }
  impl_->client = vigem_alloc();
  if (impl_->client == nullptr) {
    return Result<void, InputError>::err(InputError::Connection);
  }
  const auto connection = vigem_connect(impl_->client);
  if (!VIGEM_SUCCESS(connection)) {
    stop();
    return Result<void, InputError>::err(
        connection == VIGEM_ERROR_BUS_NOT_FOUND ? InputError::DriverMissing
                                                : InputError::Connection);
  }
  impl_->target = vigem_target_x360_alloc();
  if (impl_->target == nullptr || !VIGEM_SUCCESS(vigem_target_add(impl_->client, impl_->target))) {
    stop();
    return Result<void, InputError>::err(InputError::Target);
  }
  impl_->connected = true;
  {
    std::scoped_lock lock(callback_mutex);
    callback_targets.emplace(impl_->target, &impl_->rumble_callback);
  }
  if (!VIGEM_SUCCESS(vigem_target_x360_register_notification(
          impl_->client, impl_->target, rumble_notification))) {
    stop();
    return Result<void, InputError>::err(InputError::Target);
  }
  return Result<void, InputError>::ok();
}

Result<void, InputError> VigemGamepad::submit(const GamepadState& state) {
  if (!impl_->connected) {
    return Result<void, InputError>::err(InputError::Connection);
  }
  XUSB_REPORT report{};
  report.wButtons = static_cast<USHORT>(state.buttons & 0xFFFFU);
  report.bLeftTrigger = static_cast<BYTE>(state.left_trigger >> 8U);
  report.bRightTrigger = static_cast<BYTE>(state.right_trigger >> 8U);
  report.sThumbLX = state.left_x;
  report.sThumbLY = state.left_y;
  report.sThumbRX = state.right_x;
  report.sThumbRY = state.right_y;
  return VIGEM_SUCCESS(vigem_target_x360_update(impl_->client, impl_->target, report))
             ? Result<void, InputError>::ok()
             : Result<void, InputError>::err(InputError::Submit);
}

void VigemGamepad::set_rumble_callback(std::function<void(RumbleState)> callback) {
  impl_->rumble_callback = std::move(callback);
}

void VigemGamepad::stop() {
  if (impl_->target != nullptr) {
    {
      std::scoped_lock lock(callback_mutex);
      callback_targets.erase(impl_->target);
    }
    if (impl_->connected) {
      vigem_target_x360_unregister_notification(impl_->target);
      vigem_target_remove(impl_->client, impl_->target);
    }
    vigem_target_free(impl_->target);
    impl_->target = nullptr;
  }
  if (impl_->client != nullptr) {
    vigem_disconnect(impl_->client);
    vigem_free(impl_->client);
    impl_->client = nullptr;
  }
  impl_->connected = false;
}

}  // namespace ministream
