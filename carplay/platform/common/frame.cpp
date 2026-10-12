#include "frame.h"
#include <algorithm>
namespace cp {
static U8 clamp(int x) {
    return U8(x < 0 ? 0 : x > 255 ? 255 : x);
}
Bytes frameToBgr(const DecodedFrame &f) {
    require(f.width > 0 && f.height > 0 && f.width <= 4096 && f.height <= 2160,
            "Frame dimensions outside limit");
    require(f.format == PixelBgr24 || f.format == PixelYuv420p,
            "Native surfaces require a compatible presenter");
    require(f.plane[0] != 0, "Missing frame plane");
    Bytes out(f.width * f.height * 3);
    if (f.format == PixelBgr24) {
        require(f.stride[0] >= f.width * 3, "BGR stride too short");
        for (unsigned y = 0; y < f.height; ++y)
            memcpy(&out[y * f.width * 3], f.plane[0] + y * f.stride[0], f.width * 3);
        return out;
    }
    require(f.plane[1] && f.plane[2] && f.stride[0] >= f.width &&
                f.stride[1] >= (f.width + 1) / 2 && f.stride[2] >= (f.width + 1) / 2,
            "YUV plane/stride invalid");
    for (unsigned y = 0; y < f.height; ++y)
        for (unsigned x = 0; x < f.width; ++x) {
            int yy = int(f.plane[0][y * f.stride[0] + x]) - 16,
                uu = int(f.plane[1][(y / 2) * f.stride[1] + x / 2]) - 128,
                vv = int(f.plane[2][(y / 2) * f.stride[2] + x / 2]) - 128;
            unsigned pos = (y * f.width + x) * 3;
            out[pos] = clamp((298 * yy + 516 * uu + 128) >> 8);
            out[pos + 1] = clamp((298 * yy - 100 * uu - 208 * vv + 128) >> 8);
            out[pos + 2] = clamp((298 * yy + 409 * vv + 128) >> 8);
        }
    return out;
}
} // namespace cp
