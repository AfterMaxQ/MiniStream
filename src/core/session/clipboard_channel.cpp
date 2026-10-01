#include "core/session/clipboard_channel.hpp"
#include <algorithm>
#include <array>

namespace ministream {
namespace {
constexpr std::array<std::byte, 4> magic{std::byte{'M'}, std::byte{'S'}, std::byte{'C'}, std::byte{'B'}};
constexpr std::size_t header = 17;
void put(std::byte* out, std::uint32_t value, unsigned count) {
  for (unsigned i = 0; i < count; ++i) out[i] = static_cast<std::byte>(value >> ((count - i - 1) * 8));
}
std::uint32_t get(const std::byte* in, unsigned count) {
  std::uint32_t value{};
  for (unsigned i = 0; i < count; ++i) value = (value << 8) | std::to_integer<std::uint32_t>(in[i]);
  return value;
}
std::vector<std::byte> packet(std::uint8_t kind, std::uint32_t id, std::uint32_t index,
                             std::uint32_t count, std::uint32_t size) {
  std::vector<std::byte> result(header);
  std::copy(magic.begin(), magic.end(), result.begin());
  result[4] = static_cast<std::byte>(kind);
  put(result.data() + 5, id, 4); put(result.data() + 9, index, 2);
  put(result.data() + 11, count, 2); put(result.data() + 13, size, 4);
  return result;
}
}

bool ClipboardChannel::queue(std::string text) {
  if (text.size() > kMaxTextBytes) return false;
  if (next_id_ == 0) ++next_id_;
  const auto chunks = std::max<std::size_t>(1, (text.size() + kChunk - 1) / kChunk);
  outgoing_ = Outgoing{next_id_++, std::move(text), std::vector<bool>(chunks),
                      std::vector<std::optional<SteadyClock::time_point>>(chunks), {}};
  return true;
}

void ClipboardChannel::tick(SteadyClock::time_point now, const Sender& send) {
  constexpr auto lifetime = std::chrono::seconds{10};
  if (!outgoing_) return;
  auto& transfer = *outgoing_;
  if (!transfer.started) transfer.started = now;
  if (now - *transfer.started > lifetime ||
      std::all_of(transfer.acknowledged.begin(), transfer.acknowledged.end(), [](bool v) { return v; })) {
    outgoing_.reset(); return;
  }
  std::size_t in_flight{};
  for (std::size_t i = 0; i < transfer.sent.size(); ++i)
    if (transfer.sent[i] && !transfer.acknowledged[i]) ++in_flight;
  unsigned budget = 4;
  for (std::size_t i = 0; i < transfer.sent.size() && budget; ++i) {
    if (transfer.acknowledged[i]) continue;
    if (transfer.sent[i] && now - *transfer.sent[i] < std::chrono::milliseconds{200}) continue;
    if (!transfer.sent[i] && in_flight >= 8) continue;
    auto bytes = packet(1, transfer.id, static_cast<std::uint32_t>(i),
                        static_cast<std::uint32_t>(transfer.sent.size()),
                        static_cast<std::uint32_t>(transfer.text.size()));
    const auto start = i * kChunk;
    const auto length = std::min(kChunk, transfer.text.size() - start);
    const auto* data = reinterpret_cast<const std::byte*>(transfer.text.data() + start);
    bytes.insert(bytes.end(), data, data + length);
    if (send(bytes)) {
      if (!transfer.sent[i]) ++in_flight;
      transfer.sent[i] = now;
    }
    --budget;
  }
}

bool ClipboardChannel::receive(std::span<const std::byte> bytes, SteadyClock::time_point now,
                               const Sender& send, const Receiver& deliver) {
  if (bytes.size() < magic.size() || !std::equal(magic.begin(), magic.end(), bytes.begin())) return false;
  if (bytes.size() < header) return true;
  const auto kind = std::to_integer<unsigned>(bytes[4]);
  const auto id = get(bytes.data() + 5, 4), index = get(bytes.data() + 9, 2);
  const auto count = get(bytes.data() + 11, 2), size = get(bytes.data() + 13, 4);
  if (size > kMaxTextBytes || count != std::max<std::size_t>(1, (size + kChunk - 1) / kChunk) ||
      index >= count) return true;
  if (kind == 2) {
    if (bytes.size() == header && outgoing_ && outgoing_->id == id &&
        outgoing_->text.size() == size && outgoing_->acknowledged.size() == count)
      outgoing_->acknowledged[index] = true;
    return true;
  }
  if (kind != 1 || bytes.size() != header + std::min(kChunk, size - index * kChunk)) return true;
  if (static_cast<std::int32_t>(id - last_incoming_id_) < 0) return true;
  if (id == last_incoming_id_ && !incoming_) {
    send(packet(2, id, index, count, size)); return true;
  }
  if (id != last_incoming_id_) {
    last_incoming_id_ = id;
    incoming_ = Incoming{id, std::string(size, '\0'), std::vector<bool>(count), now};
  }
  if (!incoming_ || incoming_->text.size() != size || incoming_->received.size() != count) return true;
  auto& transfer = *incoming_;
  std::copy(bytes.begin() + header, bytes.end(),
            reinterpret_cast<std::byte*>(transfer.text.data()) + index * kChunk);
  transfer.received[index] = true;
  send(packet(2, id, index, count, size));
  if (std::all_of(transfer.received.begin(), transfer.received.end(), [](bool v) { return v; })) {
    const auto text = std::move(transfer.text);
    incoming_.reset();
    if (deliver) deliver(text);
  }
  return true;
}

void ClipboardChannel::reset() {
  outgoing_.reset(); incoming_.reset(); last_incoming_id_ = 0; next_id_ = 1;
}
}  // namespace ministream
