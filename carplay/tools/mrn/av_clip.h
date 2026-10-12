#ifndef CP_AV_CLIP_H
#define CP_AV_CLIP_H
#include "../../core/protocol.h"
namespace cp {
// Finite diagnostic fixture, not a new CarPlay wire format. H264 is Annex-B,
// one access unit per frame. RGB565 is used only to test the source on emulator.
struct AvFrame {
    Bytes bytes;
    bool key;
};
struct AvClip {
    unsigned codec, width, height, fps, maxFrame;
    std::vector<AvFrame> frames;
    explicit AvClip(const Bytes &file);
};
} // namespace cp
#endif
