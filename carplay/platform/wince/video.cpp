#include "../common/legacy_media.h"
#include "media_backend.h"
#include "../../devices/profile.h"
namespace cp {
struct LiveVideo::Impl {
    VideoBackend *backend;
    VideoCallback callback;
    void *context;
    static void output(void *opaque, const DecodedFrame &frame) {
        Impl &self = *(Impl *)opaque;
        if (self.callback) {
            Bytes b = frameToBgr(frame);
            self.callback(self.context, b);
        }
    }
    Impl(VideoCallback cb, void *ctx, unsigned w, unsigned h)
        : backend(0), callback(cb), context(ctx) {
        backend =
            createVideoBackend(environment(L"CARPLAY_VIDEO_BACKEND", deviceProfile().videoBackend),
                               output, this, w, h);
    }
    ~Impl() {
        delete backend;
    }
};
LiveVideo::LiveVideo(VideoCallback cb, void *ctx, unsigned audioRate, unsigned, unsigned w,
                     unsigned h)
    : p_(0) {
    require(audioRate == 0, "CE requires PCM; AAC backend not enabled");
    p_ = new Impl(cb, ctx, w, h);
}
LiveVideo::~LiveVideo() {
    delete p_;
}
void LiveVideo::feed(const Bytes &b) {
    p_->backend->feed(b);
}
void LiveVideo::finish() {
    p_->backend->finish();
}
unsigned LiveVideo::frames() const {
    return p_->backend->frames();
}
} // namespace cp
