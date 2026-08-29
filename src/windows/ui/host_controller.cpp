#include "windows/ui/host_controller.hpp"

#include "core/config/stream_profile.hpp"
#include "core/session/handshake.hpp"
#include "windows/audio/wasapi_loopback.hpp"
#include "windows/input/virtual_gamepad.hpp"
#include "windows/video/dxgi_capture.hpp"

#include <QSysInfo>

#include <algorithm>
#include <random>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

QString text(const CapabilityStatus& capability) {
  return QString::fromStdString(capability.detail);
}

}  // namespace

HostController::HostController(QObject* parent) : QObject(parent) {
  connect(&poll_timer_, &QTimer::timeout, this, &HostController::pollNetwork);
  poll_timer_.setInterval(10);
  refresh();
}

HostController::~HostController() { stopHost(); }

bool HostController::ready() const noexcept { return capabilities_.ready(); }
bool HostController::videoReady() const noexcept { return capabilities_.video.ready; }
bool HostController::audioReady() const noexcept { return capabilities_.audio.ready; }
bool HostController::controllerReady() const noexcept {
  return capabilities_.controller.ready;
}
bool HostController::networkReady() const noexcept { return capabilities_.network.ready; }
QString HostController::videoDetail() const { return text(capabilities_.video); }
QString HostController::audioDetail() const { return text(capabilities_.audio); }
QString HostController::controllerDetail() const { return text(capabilities_.controller); }
QString HostController::networkDetail() const { return text(capabilities_.network); }
bool HostController::hosting() const noexcept { return hosting_; }
bool HostController::pairing() const noexcept { return pairing_; }
QString HostController::pairingCode() const { return pairing_code_; }

void HostController::refresh() {
  capabilities_ = inspect_host_capabilities();
  emit capabilitiesChanged();
}

void HostController::startHost() {
  if (hosting_ || !ready()) {
    return;
  }
  capture_ = std::make_unique<DxgiCapture>();
  audio_ = std::make_unique<WasapiLoopback>();
  gamepad_ = std::make_unique<VirtualGamepad>();
  discovery_ = std::make_unique<DiscoveryHost>();
  session_ = std::make_unique<UdpEndpoint>();
  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!capture_->initialize() || !audio_->start() || !gamepad_->start() ||
      !discovery_->start() || !session_->bind(48000) || !identity || !ephemeral) {
    stopHost();
    refresh();
    return;
  }
  identity_ = *identity;
  ephemeral_ = *ephemeral;
  hosting_ = true;
  poll_timer_.start();
  emit hostingChanged();
}

void HostController::stopHost() {
  poll_timer_.stop();
  if (gamepad_) {
    gamepad_->stop();
  }
  session_.reset();
  discovery_.reset();
  gamepad_.reset();
  audio_.reset();
  capture_.reset();
  identity_.reset();
  ephemeral_.reset();
  resetPairing();
  if (hosting_) {
    hosting_ = false;
    emit hostingChanged();
  }
}

void HostController::confirmPairing() {
  if (!pairing_ || !session_) {
    return;
  }
  confirmation_.confirm_local();
  const auto message = encode_pairing_confirmation(true);
  session_->reply(message);
  if (confirmation_.ready()) {
    if (ephemeral_ && peer_offer_) {
      const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, false);
      if (keys) {
        session_keys_ = *keys;
      }
    }
    resetPairing();
  }
}

void HostController::cancelPairing() {
  if (session_) {
    const auto message = encode_pairing_confirmation(false);
    session_->reply(message);
  }
  resetPairing();
}

void HostController::pollNetwork() {
  auto name = QSysInfo::machineHostName().toStdString();
  name.resize(std::min<std::size_t>(name.size(), 48));
  discovery_->poll({name.empty() ? "Windows PC" : name, 48000});

  const auto incoming = session_->try_receive();
  if (!incoming) {
    return;
  }
  if (const auto hello = decode_hello(incoming->datagram.bytes)) {
    session_->reply(encode_accept(
        {1, hello->codec, hello->width, hello->height, hello->fps,
         hello->target_bitrate_bps, hello->nonce}));
    return;
  }
  if (const auto offer = decode_pairing_offer(incoming->datagram.bytes);
      offer && offer->role == PairingRole::Initiator && identity_ && ephemeral_) {
    peer_offer_ = *offer;
    local_offer_ = PairingOffer{PairingRole::Responder, random_nonce(),
                                identity_->public_key, ephemeral_->public_key};
    const auto transcript = pairing_transcript(*peer_offer_, *local_offer_);
    if (!transcript) {
      return;
    }
    pairing_code_ = QStringLiteral("%1").arg(
        compute_pairing_sas(*transcript), 6, 10, QLatin1Char('0'));
    pairing_ = true;
    session_->reply(encode_pairing_offer(*local_offer_));
    emit pairingChanged();
    return;
  }
  if (const auto accepted = decode_pairing_confirmation(incoming->datagram.bytes)) {
    if (*accepted) {
      confirmation_.confirm_peer();
      if (confirmation_.ready()) {
        if (ephemeral_ && peer_offer_) {
          const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, false);
          if (keys) {
            session_keys_ = *keys;
          }
        }
        resetPairing();
      }
    } else {
      resetPairing();
    }
  }
}

void HostController::resetPairing() {
  const bool was_pairing = pairing_;
  pairing_ = false;
  pairing_code_.clear();
  peer_offer_.reset();
  local_offer_.reset();
  confirmation_ = PairingConfirmation{};
  if (was_pairing) {
    emit pairingChanged();
  }
}

}  // namespace ministream
