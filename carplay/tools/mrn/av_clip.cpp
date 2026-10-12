#include "av_clip.h"
#include <algorithm>
namespace cp {
namespace {
unsigned take(const Bytes &b, size_t &at) {
    require(at <= b.size() && b.size() - at >= 4, "Truncated AV test fixture");
    unsigned v = unsigned(b[at]) | (unsigned(b[at + 1]) << 8) | (unsigned(b[at + 2]) << 16) |
                 (unsigned(b[at + 3]) << 24);
    at += 4;
    return v;
}
} // namespace
AvClip::AvClip(const Bytes &b) : maxFrame(0) {
    require(b.size() <= 16 * 1024 * 1024, "AV fixture too large");
    size_t at = 0;
    require(take(b, at) == 0x31545641, "Expected AVT1 fixture");
    codec = take(b, at);
    width = take(b, at);
    height = take(b, at);
    fps = take(b, at);
    unsigned count = take(b, at);
    require((codec == 1 || codec == 2) && width >= 16 && width <= 800 && height >= 16 &&
                height <= 480 && width % 2 == 0 && fps >= 1 && fps <= 30 && count >= 1 &&
                count <= fps * 12,
            "Invalid AV fixture dimensions/duration");
    for (unsigned i = 0; i < count; ++i) {
        unsigned size = take(b, at), flags = take(b, at);
        require(size > 0 && size <= 1024 * 1024 && flags <= 1 && size <= b.size() - at,
                "Invalid AV frame length/flags");
        if (codec == 2)
            require(size == ((width * 2 + 3) & ~3) * height, "Invalid RGB565 stride");
        else
            require(size >= 5 && b[at] == 0 && b[at + 1] == 0 && b[at + 2] == 0 && b[at + 3] == 1,
                    "H264 fixture requires Annex-B access units");
        AvFrame f;
        f.bytes = slice(b, at, size);
        f.key = flags != 0;
        frames.push_back(f);
        at += size;
        maxFrame = std::max(maxFrame, size);
    }
    require(at == b.size() && frames[0].key, "Trailing data or missing initial key frame");
}
} // namespace cp
