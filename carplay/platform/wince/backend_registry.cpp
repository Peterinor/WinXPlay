#include "media_backend.h"
#include "../../devices/profile.h"
#include "../../devices/wince.h"
namespace cp {
VideoBackend *createVideoBackend(const std::wstring &name, FrameSink sink, void *context,
                                 unsigned w, unsigned h) {
    require(false, "Selected video backend is not compiled: configure CARPLAY_VIDEO_BACKEND");
    return 0;
}
AudioOutputBackend *createAudioOutputBackend(const std::wstring &name, unsigned rate,
                                             unsigned channels) {
    return createDeviceAudioOutput(name,rate,channels);
}
AudioInputBackend *createAudioInputBackend(const std::wstring &name, unsigned rate,
                                           unsigned channels, unsigned samples) {
    require(name == L"wave", "Unknown audio input backend");
    return createWaveInput(rate, channels, samples);
}
FramePresenter *createFramePresenter(const std::wstring &name) {
    require(name == L"gdi", "Selected presenter is not compiled");
    return createGdiPresenter();
}
Value mediaBackendConfiguration() {
    Value v = Value::dictionary();
    v.dict["video_backend"] =
        Value::str(utf8(environment(L"CARPLAY_VIDEO_BACKEND", deviceProfile().videoBackend)));
    v.dict["audio_output_backend"] =
        Value::str(utf8(environment(L"CARPLAY_AUDIO_OUT_BACKEND", L"wave")));
    v.dict["audio_input_backend"] =
        Value::str(utf8(environment(L"CARPLAY_AUDIO_IN_BACKEND", L"wave")));
    v.dict["presenter"] = Value::str(utf8(environment(L"CARPLAY_PRESENTER", L"gdi")));
    describeDeviceMedia(v);
    v.dict["hardware_rendering_verified"] = Value::flag(false);
    return v;
}
} // namespace cp
