#include "mf_video_adapter.h"
#include "mf_video.h"
#include <sstream>
namespace cp {
namespace {
void checkMf(HRESULT hr, const char *stage) {
    if(FAILED(hr)) { std::ostringstream s; s<<stage<<" (HRESULT 0x"<<std::hex<<(unsigned long)hr<<")";
        throw std::runtime_error(s.str()); }
}
class SystemVideo : public VideoPipeline {
    void *handle_; VideoCallback callback_; void *context_; DWORD saved_;
    static void frame(void *p) {
        SystemVideo &v=*(SystemVideo*)p;
        // Empty callback means the platform has presented a GPU frame. The
        // Windows UI must not repaint that frame with its legacy GDI path.
        if(v.callback_) v.callback_(v.context_,Bytes());
    }
    void save() const {
        CpMfStats s; cpMfStats(handle_,&s); Value v=Value::dictionary();
        v.dict["backend"]=Value::str("media_foundation_d3d11");
        v.dict["adapter"]=Value::str(utf8(s.adapter)); v.dict["stage"]=Value::str(utf8(s.stage));
        v.dict["frames_presented"]=Value::num(s.frames); v.dict["gpu_surface_samples"]=Value::num(s.gpuSamples);
        v.dict["decoder_surface_samples"]=Value::num(s.decoderSurfaces); v.dict["cpu_surface_samples"]=Value::num(s.cpuSamples);
        v.dict["submitted_packets"]=Value::num(s.submitted); v.dict["queued_bytes"]=Value::num(s.queuedBytes);
        v.dict["max_queue_bytes"]=Value::num(s.maxQueueBytes); v.dict["width"]=Value::num(s.width); v.dict["height"]=Value::num(s.height);
        v.dict["max_fps"]=Value::num(s.maxFPS);
        v.dict["present_call_total_us"]=Value::num(s.totalPresentUs);
        // Output can be delayed inside MFT. These ages refer to the input that
        // triggered draining, not a timestamp-matched decoded frame.
        v.dict["input_trigger_age_total_us"]=Value::num(s.totalQueueUs); v.dict["input_trigger_age_max_us"]=Value::num(s.maxQueueUs);
        v.dict["latency_is_end_to_end"]=Value::flag(false);
        v.dict["error_hresult"]=Value::num((unsigned long)s.error);
        writeDiagnosticFile(executableDirectory()+L"/video-backend.plist",xmlPlist(v));
    }
  public:
    SystemVideo(HWND window,VideoCallback f,void *ctx,unsigned w,unsigned h,unsigned maxFPS)
        :handle_(0),callback_(f),context_(ctx),saved_(GetTickCount()) {
        checkMf(cpMfCreate(window,w,h,maxFPS,frame,this,&handle_),"Cannot initialize system video decoder");
    }
    ~SystemVideo() { cpMfDestroy(handle_); }
    void submit(const Bytes &b,U64) {
        checkMf(cpMfSubmit(handle_,b.empty()?0:&b[0],unsigned(b.size())),"System video decode failed; see video-backend.plist");
        if(DWORD(GetTickCount()-saved_)>=1000) { save(); saved_=GetTickCount(); }
    }
    void finish() { HRESULT hr=cpMfFinish(handle_); save(); checkMf(hr,"System video completion failed"); }
    unsigned frames() const { CpMfStats s; cpMfStats(handle_,&s); return s.frames; }
};
}
VideoPipeline *createSystemVideo(HWND window,VideoCallback cb,void *ctx,unsigned w,unsigned h,unsigned maxFPS) {
    return new SystemVideo(window,cb,ctx,w,h,maxFPS);
}
}
