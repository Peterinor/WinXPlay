#ifndef CP_FRAME_H
#define CP_FRAME_H
#include "../../core/protocol.h"
namespace cp {
// Borrowed until FrameSink returns. A hardware frame is never assumed to be RGB.
enum PixelFormat { PixelBgr24, PixelYuv420p, PixelNativeSurface };
struct DecodedFrame {
    PixelFormat format;
    unsigned width, height, stride[3];
    const U8 *plane[3];
    void *nativeSurface;
    DecodedFrame() : format(PixelBgr24), width(0), height(0), nativeSurface(0) {
        for (unsigned i = 0; i < 3; ++i) {
            stride[i] = 0;
            plane[i] = 0;
        }
    }
};
typedef void (*FrameSink)(void *, const DecodedFrame &);
Bytes frameToBgr(const DecodedFrame &frame);
} // namespace cp
#endif
