#include "media_backend.h"
namespace cp {
class GdiPresenter : public FramePresenter {
  public:
    void paint(HDC dc, const RECT &p, const DecodedFrame &frame) {
        require(frame.width > 0 && frame.width <= 4096 && frame.height > 0 && frame.height <= 2160,
                "Invalid presentation dimensions");
        const unsigned pitch = (frame.width * 3 + 3) & ~3u;
        Bytes packed, padded;
        const U8 *pixels = frame.plane[0];
        if (frame.format != PixelBgr24 || frame.stride[0] != pitch) {
            packed = frameToBgr(frame);
            pixels = &packed[0];
            if (pitch != frame.width * 3) {
                padded.resize(pitch * frame.height);
                for (unsigned row = 0; row < frame.height; ++row)
                    memcpy(&padded[row * pitch], &packed[row * frame.width * 3], frame.width * 3);
                pixels = &padded[0];
            }
        }
        require(pixels != 0, "Missing presentation pixels");
        BITMAPINFO info;
        ZeroMemory(&info, sizeof(info));
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frame.width;
        info.bmiHeader.biHeight = -LONG(frame.height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 24;
        StretchDIBits(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, 0, 0, frame.width,
                      frame.height, pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    }
};
FramePresenter *createGdiPresenter() {
    return new GdiPresenter;
}
} // namespace cp
