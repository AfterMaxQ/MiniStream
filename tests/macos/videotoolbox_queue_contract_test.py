from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/macos/video/videotoolbox_encoder.mm").read_text(encoding="utf-8")

required_fragments = (
    "std::deque<EncodedFrame> ready_frames;",
    "std::atomic_bool force_next_idr{false};",
    "bool awaiting_idr{};",
    "impl->ready_frames.clear();",
    "impl->awaiting_idr = true;",
    "if (impl->awaiting_idr && !keyframe)",
    "impl->force_next_idr.store(false, std::memory_order_release);",
    "impl_->force_next_idr.load(std::memory_order_acquire)",
    "std::optional<EncodedFrame> VideoToolboxEncoder::take_next()",
    "void VideoToolboxEncoder::request_idr() noexcept",
)

missing = [fragment for fragment in required_fragments if fragment not in SOURCE]
if missing:
    raise SystemExit("missing VideoToolbox queue contract: " + ", ".join(missing))

if "EncodedFrame latest{};" in SOURCE or "bool has_latest{};" in SOURCE:
    raise SystemExit("VideoToolbox encoder still has a single latest-frame slot")

if SOURCE.count("int header_length = 0;") != 2 or "size_t header_length = 0;" in SOURCE:
    raise SystemExit("VideoToolbox parameter-set header length must match CoreMedia int API")

print("VideoToolbox queue contract check passed")
