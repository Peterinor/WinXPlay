#include "airinfo.h"
#include "input.h"
namespace cp {
Value airplayInfo(unsigned width, unsigned height, const MediaCapabilities &capabilities) {
    require(width >= 800 && width <= 1920 && height >= 480 && height <= 1080 && width % 4 == 0 &&
                height % 2 == 0,
            "Invalid display dimensions");
    Value v = Value::dictionary();
    require(!capabilities.deviceId.empty() && !capabilities.displayUuid.empty(),"Missing accessory identity");
    v.dict["name"] = Value::str(capabilities.displayName);
    v.dict["model"] = Value::str(capabilities.model);
    v.dict["manufacturer"] = Value::str("WinXPlay");
    v.dict["sourceVersion"] = Value::str("320.17");
    v.dict["deviceID"] = Value::str(capabilities.deviceId);
    Value bt = Value::list();
    bt.array.push_back(Value::str(capabilities.deviceId));
    v.dict["bluetoothIDs"] = bt;
    v.dict["features"] = Value::num(0x615653aee2ULL);
    v.dict["statusFlags"] = Value::num(4);
    v.dict["rightHandDrive"] = Value::flag(false);
    v.dict["keepAliveLowPower"] = Value::flag(false);
    v.dict["keepAliveSendStatsAsBody"] = Value::flag(false);
    Value modes = Value::dictionary(), resources = Value::list(), states = Value::list();
    for (unsigned i = 1; i <= 1; ++i) {
        Value r = Value::dictionary();
        r.dict["resourceID"] = Value::num(i);
        r.dict["transferType"] = Value::num(1);
        const char *keys[] = {"transferPriority", "takeConstraint", "borrowConstraint",
                              "unborrowConstraint"};
        for (unsigned j = 0; j < 4; ++j)
            r.dict[keys[j]] = Value::num(100);
        resources.array.push_back(r);
    }
    for (unsigned i = 1; i <= 3; ++i) {
        Value s = Value::dictionary();
        s.dict["appStateID"] = Value::num(i);
        if (i == 1) {
            s.dict["speechMode"] = Value::num(U64(-1));
            s.dict["speechMode"].negative = true;
        } else
            s.dict["state"] = Value::flag(false);
        states.array.push_back(s);
    }
    modes.dict["resources"] = resources;
    modes.dict["appStates"] = states;
    v.dict["modes"] = modes;
    Value formats = Value::list(), latencies = Value::list();
    const char *audioTypes[] = {"compatibility", "default",           "media",
                                "telephony",     "speechRecognition", "alert"};
    // PCM for voice/legacy music and AAC-LC for high-quality/buffered music.
    for (unsigned type = 100; type <= 101; ++type)
        for (unsigned i = 0; i < (type == 101 ? 2u : 6u); ++i) {
            Value f = Value::dictionary();
            f.dict["type"] = Value::num(type);
            f.dict["audioType"] = Value::str(audioTypes[i]);
            f.dict["audioOutputFormats"] = Value::num(i == 3 || i == 4 ? 0x4154 : 0xc3fc);
            formats.array.push_back(f);
            if (capabilities.microphone && type == 100 && (i == 0 || i == 1 || i == 3 || i == 4))
                formats.array.back().dict["audioInputFormats"] = Value::num(0x4154);
            f.dict.erase("audioOutputFormats");
            f.dict["inputLatencyMicros"] = Value::num(0);
            f.dict["outputLatencyMicros"] = Value::num(50000);
            latencies.array.push_back(f);
        }
    if (capabilities.aac) {
        Value aac = Value::dictionary();
        aac.dict["type"] = Value::num(102);
        aac.dict["audioType"] = Value::str("media");
        aac.dict["audioOutputFormats"] = Value::num(0x800000);
        formats.array.push_back(aac);
        if (capabilities.bufferedAac) {
            aac.dict["type"] = Value::num(103);
            formats.array.push_back(aac);
            v.dict["mainBufferedInfo"] = Value::dictionary();
        }
    }
    v.dict["audioFormats"] = formats;
    for (unsigned t = 100; t <= 102; ++t) {
        Value latency = Value::dictionary();
        latency.dict["type"] = Value::num(t);
        latency.dict["inputLatencyMicros"] = Value::num(0);
        latency.dict["outputLatencyMicros"] = Value::num(50000);
        latencies.array.push_back(latency);
    }
    v.dict["audioLatencies"] = latencies;
    Value features = Value::list();
    features.array.push_back(Value::str("vocoderInfo"));
    features.array.push_back(Value::str("enhancedRequestCarUI"));
    v.dict["extendedFeatures"] = features;
    const std::string &uuid = capabilities.displayUuid;
    Value display = Value::dictionary();
    display.dict["uuid"] = Value::str(uuid);
    display.dict["type"] = Value::num(110);
    display.dict["maxFPS"] = Value::num(30);
    display.dict["widthPixels"] = Value::num(width);
    display.dict["heightPixels"] = Value::num(height);
    display.dict["widthPhysical"] = Value::num(180);
    display.dict["heightPhysical"] = Value::num(180 * height / width);
    display.dict["features"] = Value::num(8);
    display.dict["primaryInputDevice"] = Value::num(1);
    Value area = Value::dictionary();
    area.dict["widthPixels"] = Value::num(width);
    area.dict["heightPixels"] = Value::num(height);
    area.dict["originXPixels"] = Value::num(0);
    area.dict["originYPixels"] = Value::num(0);
    area.dict["safeArea"] = area;
    Value areas = Value::list();
    areas.array.push_back(area);
    display.dict["viewAreas"] = areas;
    display.dict["initialViewArea"] = Value::num(0);
    Value displays = Value::list();
    displays.array.push_back(display);
    v.dict["displays"] = displays;
    v.dict["hidDevices"] = inputDevices(width,height,capabilities.touch,capabilities.controls);
    for(size_t i=0;i<v.dict["hidDevices"].array.size();++i)
        v.dict["hidDevices"].array[i].dict["displayUUID"]=Value::str(uuid);
    return v;
}
} // namespace cp
