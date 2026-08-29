#include "core/fec/fec_codec.hpp"

#include <leopard.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

namespace ministream {
namespace {

class AlignedBuffer {
 public:
  explicit AlignedBuffer(std::size_t bytes)
      : bytes_(bytes), data_(static_cast<std::byte*>(
                           ::operator new[](bytes, std::align_val_t{64}))) {
    std::memset(data_, 0, bytes_);
  }

  ~AlignedBuffer() { ::operator delete[](data_, std::align_val_t{64}); }
  AlignedBuffer(const AlignedBuffer&) = delete;
  AlignedBuffer& operator=(const AlignedBuffer&) = delete;
  AlignedBuffer(AlignedBuffer&& other) noexcept
      : bytes_(std::exchange(other.bytes_, 0)), data_(std::exchange(other.data_, nullptr)) {}
  AlignedBuffer& operator=(AlignedBuffer&&) = delete;

  std::byte* data() noexcept { return data_; }
  const std::byte* data() const noexcept { return data_; }

 private:
  std::size_t bytes_;
  std::byte* data_;
};

bool ensure_leopard() {
  static std::once_flag flag;
  static bool initialized = false;
  std::call_once(flag, [] { initialized = leo_init() == Leopard_Success; });
  return initialized;
}

std::size_t align_64(std::size_t bytes) { return (bytes + 63U) & ~std::size_t{63U}; }

std::vector<AlignedBuffer> allocate_buffers(std::size_t count, std::size_t bytes) {
  std::vector<AlignedBuffer> buffers;
  buffers.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    buffers.emplace_back(bytes);
  }
  return buffers;
}

std::vector<void*> mutable_pointers(std::vector<AlignedBuffer>& buffers) {
  std::vector<void*> pointers;
  pointers.reserve(buffers.size());
  for (auto& buffer : buffers) {
    pointers.push_back(buffer.data());
  }
  return pointers;
}

}  // namespace

FecBlock FecCodec::encode(
    std::span<const Datagram> data, std::uint16_t parity_shards) const {
  if (!ensure_leopard() || data.empty() || parity_shards == 0 ||
      parity_shards > data.size() || data.size() + parity_shards > 65536U) {
    return {};
  }

  std::size_t maximum = 0;
  for (const auto& datagram : data) {
    maximum = std::max(maximum, datagram.bytes.size());
  }
  if (maximum == 0 || maximum > std::numeric_limits<std::uint16_t>::max()) {
    return {};
  }
  const auto shard_bytes = align_64(maximum);
  auto data_storage = allocate_buffers(data.size(), shard_bytes);
  std::vector<const void*> data_pointers;
  data_pointers.reserve(data.size());
  for (std::size_t i = 0; i < data.size(); ++i) {
    std::memcpy(data_storage[i].data(), data[i].bytes.data(), data[i].bytes.size());
    data_pointers.push_back(data_storage[i].data());
  }

  const auto work_count = leo_encode_work_count(
      static_cast<unsigned>(data.size()), static_cast<unsigned>(parity_shards));
  auto work_storage = allocate_buffers(work_count, shard_bytes);
  auto work_pointers = mutable_pointers(work_storage);
  const auto result = leo_encode(
      shard_bytes, static_cast<unsigned>(data.size()), parity_shards, work_count,
      data_pointers.data(), work_pointers.data());
  if (result != Leopard_Success) {
    return {};
  }

  FecBlock block;
  block.layout = {
      static_cast<std::uint16_t>(data.size()), parity_shards, shard_bytes};
  block.shards.reserve(data.size() + parity_shards);
  for (std::size_t i = 0; i < data.size(); ++i) {
    block.shards.push_back(FecShard{
        static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(data[i].bytes.size()),
        true, data[i].bytes});
  }
  for (std::uint16_t i = 0; i < parity_shards; ++i) {
    FecShard parity;
    parity.index = static_cast<std::uint16_t>(data.size() + i);
    parity.original_payload_bytes = static_cast<std::uint16_t>(shard_bytes);
    parity.present = true;
    parity.bytes.assign(work_storage[i].data(), work_storage[i].data() + shard_bytes);
    block.shards.push_back(std::move(parity));
  }
  return block;
}

bool FecCodec::recover(FecBlock& block) const {
  const auto data_count = static_cast<std::size_t>(block.layout.data_shards);
  const auto parity_count = static_cast<std::size_t>(block.layout.parity_shards);
  if (!ensure_leopard() || data_count == 0 || parity_count == 0 ||
      block.layout.shard_bytes == 0 || block.layout.shard_bytes % 64 != 0 ||
      block.shards.size() != data_count + parity_count) {
    return false;
  }
  const auto present_count = std::count_if(
      block.shards.begin(), block.shards.end(), [](const FecShard& shard) {
        return shard.present;
      });
  if (present_count < static_cast<std::ptrdiff_t>(data_count)) {
    return false;
  }

  auto data_storage = allocate_buffers(data_count, block.layout.shard_bytes);
  auto parity_storage = allocate_buffers(parity_count, block.layout.shard_bytes);
  std::vector<const void*> data_pointers(data_count, nullptr);
  std::vector<const void*> parity_pointers(parity_count, nullptr);
  for (std::size_t i = 0; i < data_count; ++i) {
    if (block.shards[i].present) {
      std::memcpy(
          data_storage[i].data(), block.shards[i].bytes.data(), block.shards[i].bytes.size());
      data_pointers[i] = data_storage[i].data();
    }
  }
  for (std::size_t i = 0; i < parity_count; ++i) {
    const auto& shard = block.shards[data_count + i];
    if (shard.present) {
      std::memcpy(parity_storage[i].data(), shard.bytes.data(), shard.bytes.size());
      parity_pointers[i] = parity_storage[i].data();
    }
  }

  const auto work_count = leo_decode_work_count(
      static_cast<unsigned>(data_count), static_cast<unsigned>(parity_count));
  auto work_storage = allocate_buffers(work_count, block.layout.shard_bytes);
  auto work_pointers = mutable_pointers(work_storage);
  const auto result = leo_decode(
      block.layout.shard_bytes, static_cast<unsigned>(data_count),
      static_cast<unsigned>(parity_count), work_count, data_pointers.data(),
      parity_pointers.data(), work_pointers.data());
  if (result != Leopard_Success) {
    return false;
  }

  for (std::size_t i = 0; i < data_count; ++i) {
    auto& shard = block.shards[i];
    if (!shard.present) {
      shard.bytes.assign(
          work_storage[i].data(), work_storage[i].data() + shard.original_payload_bytes);
      shard.present = true;
    }
  }
  return true;
}

}  // namespace ministream
