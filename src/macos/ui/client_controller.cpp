#include "macos/ui/client_controller.hpp"

#include "core/config/stream_profile.hpp"
#include "core/session/handshake.hpp"

#include <chrono>
#include <random>
#include <utility>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

}  // namespace

ClientController::ClientController(QObject* parent) : QObject(parent) {
  poll_timer_.setInterval(20);
  connect(&poll_timer_, &QTimer::timeout, this, &ClientController::pollConfirmation);
}

ClientController::~ClientController() { disconnectSession(); }

QStringList ClientController::hosts() const { return host_labels_; }
bool ClientController::searching() const noexcept { return searching_; }
bool ClientController::connected() const noexcept { return connected_; }
bool ClientController::pairing() const noexcept { return pairing_; }
bool ClientController::remoteInputActive() const noexcept {
  return input_capture_.remote();
}
QString ClientController::pairingCode() const { return pairing_code_; }

void ClientController::refreshHosts() {
  if (searching_) {
    return;
  }
  searching_ = true;
  emit searchingChanged();
  host_labels_.clear();
  discovered_.clear();
  if (const auto result = discover_hosts(std::chrono::milliseconds{250})) {
    discovered_ = *result;
    for (const auto& host : discovered_) {
      host_labels_.push_back(QStringLiteral("%1  %2")
                                 .arg(QString::fromStdString(host.name),
                                      QString::fromStdString(host.address)));
    }
  }
  searching_ = false;
  emit hostsChanged();
  emit searchingChanged();
}

void ClientController::connectToHost(int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= discovered_.size()) {
    return;
  }
  disconnectSession();
  session_ = std::make_unique<UdpEndpoint>();
  const auto& host = discovered_[static_cast<std::size_t>(index)];
  if (!session_->bind(0) || !session_->set_remote(host.address, host.session_port)) {
    disconnectSession();
    return;
  }
  const auto profile = stream_profile(StreamProfileId::Quality4K);
  const Hello hello{profile.codec, static_cast<std::uint16_t>(profile.width),
                    static_cast<std::uint16_t>(profile.height),
                    static_cast<std::uint16_t>(profile.fps),
                    static_cast<std::uint32_t>(profile.initial_bitrate_bps), random_nonce()};
  const auto hello_bytes = encode_hello(hello);
  if (!session_->send(hello_bytes)) {
    disconnectSession();
    return;
  }
  const auto accepted = session_->receive(std::chrono::seconds{1});
  const auto decoded_accept = accepted ? decode_accept(accepted->datagram.bytes)
                                       : std::nullopt;
  if (!decoded_accept || decoded_accept->hello_nonce != hello.nonce ||
      decoded_accept->codec != hello.codec || decoded_accept->width != hello.width ||
      decoded_accept->height != hello.height || decoded_accept->fps != hello.fps) {
    disconnectSession();
    return;
  }
  connected_ = true;
  emit connectedChanged();

  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!identity || !ephemeral) {
    disconnectSession();
    return;
  }
  identity_ = *identity;
  ephemeral_ = *ephemeral;
  local_offer_ = PairingOffer{PairingRole::Initiator, hello.nonce,
                              identity_->public_key, ephemeral_->public_key};
  const auto offer_bytes = encode_pairing_offer(*local_offer_);
  if (!session_->send(offer_bytes)) {
    disconnectSession();
    return;
  }
  const auto response = session_->receive(std::chrono::seconds{1});
  if (!response || !(peer_offer_ = decode_pairing_offer(response->datagram.bytes))) {
    disconnectSession();
    return;
  }
  const auto transcript = pairing_transcript(*local_offer_, *peer_offer_);
  if (!transcript) {
    disconnectSession();
    return;
  }
  pairing_code_ = QStringLiteral("%1").arg(
      compute_pairing_sas(*transcript), 6, 10, QLatin1Char('0'));
  pairing_ = true;
  emit pairingChanged();
}

void ClientController::toggleRemoteInput() {
  if (input_capture_.remote()) {
    releaseRemoteInput();
    return;
  }
  if (!connected_ || !input_capture_.enter_remote()) {
    return;
  }
  auto keyboard = input_capture_.capture(InputDevice::Keyboard);
  auto mouse = input_capture_.capture(InputDevice::Mouse);
  auto gamepad = input_capture_.capture(InputDevice::Gamepad);
  if (!keyboard || !mouse || !gamepad) {
    input_capture_.leave_remote();
    return;
  }
  keyboard_lease_ = std::move(*keyboard);
  mouse_lease_ = std::move(*mouse);
  gamepad_lease_ = std::move(*gamepad);
  emit remoteInputChanged();
}

void ClientController::releaseRemoteInput() {
  const bool was_active = input_capture_.remote();
  keyboard_lease_.reset();
  mouse_lease_.reset();
  gamepad_lease_.reset();
  input_capture_.leave_remote();
  if (was_active) {
    emit remoteInputChanged();
  }
}

void ClientController::confirmPairing() {
  if (!pairing_ || !session_) {
    return;
  }
  confirmation_.confirm_local();
  const auto message = encode_pairing_confirmation(true);
  session_->send(message);
  poll_timer_.start();
}

void ClientController::cancelPairing() {
  if (session_) {
    const auto message = encode_pairing_confirmation(false);
    session_->send(message);
  }
  disconnectSession();
}

void ClientController::pollConfirmation() {
  const auto incoming = session_ ? session_->try_receive() : std::nullopt;
  if (!incoming) {
    return;
  }
  const auto accepted = decode_pairing_confirmation(incoming->datagram.bytes);
  if (!accepted || !*accepted) {
    disconnectSession();
    return;
  }
  confirmation_.confirm_peer();
  if (confirmation_.ready() && ephemeral_ && peer_offer_) {
    const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, true);
    if (keys) {
      session_keys_ = *keys;
    }
    resetPairing();
  }
}

void ClientController::disconnectSession() {
  releaseRemoteInput();
  poll_timer_.stop();
  session_.reset();
  const bool was_connected = connected_;
  connected_ = false;
  if (was_connected) {
    emit connectedChanged();
  }
  resetPairing();
}

void ClientController::resetPairing() {
  poll_timer_.stop();
  const bool was_pairing = pairing_;
  pairing_ = false;
  pairing_code_.clear();
  local_offer_.reset();
  peer_offer_.reset();
  confirmation_ = PairingConfirmation{};
  if (was_pairing) {
    emit pairingChanged();
  }
}

}  // namespace ministream
