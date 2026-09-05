#include "core/video/annex_b.hpp"

#include <array>

namespace ministream {

std::vector<std::span<const std::byte>> split_annex_b(std::span<const std::byte> bytes) {
  const auto prefix = [&](std::size_t offset) -> std::size_t {
    if (offset + 3 > bytes.size() || bytes[offset] != std::byte{0} ||
        bytes[offset + 1] != std::byte{0}) return 0;
    if (bytes[offset + 2] == std::byte{1}) return 3;
    return offset + 4 <= bytes.size() && bytes[offset + 2] == std::byte{0} &&
                   bytes[offset + 3] == std::byte{1} ? 4 : 0;
  };
  std::vector<std::span<const std::byte>> units;
  std::size_t offset{};
  while (offset < bytes.size()) {
    const auto length = prefix(offset);
    if (length == 0) { ++offset; continue; }
    const auto start = offset + length;
    offset = start;
    while (offset < bytes.size() && prefix(offset) == 0) ++offset;
    auto end = offset;
    while (end > start && bytes[end - 1] == std::byte{0}) --end;
    if (end > start) units.push_back(bytes.subspan(start, end - start));
  }
  return units;
}

std::vector<std::byte> extract_parameter_sets(VideoCodec codec,
                                             std::span<const std::byte> bytes) {
  if (codec != VideoCodec::H264 && codec != VideoCodec::Hevc) return {};
  std::array<std::span<const std::byte>, 3> sets{};
  const auto count = codec == VideoCodec::H264 ? 2U : 3U;
  for (const auto unit : split_annex_b(bytes)) {
    if (codec == VideoCodec::Hevc && unit.size() < 2) continue;
    const auto header = std::to_integer<unsigned>(unit.front());
    const auto type = codec == VideoCodec::H264 ? header & 0x1FU : (header >> 1U) & 0x3FU;
    const auto first = codec == VideoCodec::H264 ? 7U : 32U;
    if (type >= first && type < first + count) sets[type - first] = unit;
  }
  std::vector<std::byte> result;
  for (unsigned index = 0; index < count; ++index) {
    if (sets[index].empty()) return {};
    result.insert(result.end(), {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}});
    result.insert(result.end(), sets[index].begin(), sets[index].end());
  }
  return result;
}

}  // namespace ministream
