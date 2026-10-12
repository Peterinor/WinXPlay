#include "receiver.h"
#include "pairing.h"
#include "buffered_audio.h"
#include <sstream>
#include <memory>
#include <algorithm>
namespace cp {
namespace {
std::string decimal(U64 n) {
    std::ostringstream s;
    s << n;
    return s.str();
}
unsigned number(const Value &v, const char *key, unsigned fallback = 0) {
    if (!v.dict.count(key))
        return fallback;
    require(v.at(key).type == Value::Integer && v.at(key).integer <= 0xffffffffULL,
            "Invalid numeric stream option");
    return unsigned(v.at(key).integer);
}
struct Endpoint {
    Transport &network;
    Connection socket;
    unsigned role, type, channels;
    Bytes key;
    std::string audioType;
    ControlCipher *cipher;
    ScreenParser *screen;
    AudioOutput *audio;
    BufferedAudio *buffered;
    AudioInput *microphone;
    RtspParser parser;
    Bytes pending, microphoneKey;
    unsigned microphonePort;
    U64 microphoneCounter;
    U32 microphoneTime;
    Endpoint(Transport &n, Connection s, unsigned r)
        : network(n), socket(s), role(r), type(0), channels(0), cipher(0), screen(0), audio(0),
          buffered(0), microphone(0), microphonePort(0), microphoneCounter(0), microphoneTime(0) {}
    ~Endpoint() {
        if (socket != InvalidConnection)
            network.close(socket);
        delete cipher;
        delete screen;
        delete audio;
        delete buffered;
        delete microphone;
    }
};
} // namespace
struct Receiver::Impl {
    CryptoProvider &crypto;
    Transport &network;
    SessionRuntime &runtime;
    MediaFactory &media;
    MediaCapabilities capabilities;
    Pairing pairing;
    AuthProvider *auth;
    StatusCallback statusCb;
    ControlCallback controlCb;
    void* controlContext;
    void *context;
    std::vector<Endpoint *> endpoints;
    InputQueue inputs;
    VideoPipeline *video;
    Value stats;
    Connection controlSocket, eventSocket;
    unsigned boundPort, width, height;
    std::string publicHex;
    U64 commandSequence;
    bool peerKnown, audioReleased, physicalDevice;
    bool firstVideo, startupHomeDone, userInputSeen;
    U32 firstVideoAt;
    PeerAddress peerSocketAddress;
    U32 timingSent;
    Bytes pendingTiming;
    I64 timingOffset;
    explicit Impl(ReceiverServices &services, StatusCallback sc, void *ctx, unsigned port,
                  bool physical, unsigned w, unsigned h)
        : crypto(services.crypto), network(services.transport), runtime(services.runtime),
          media(services.media), capabilities(media.capabilities()),
          pairing(crypto, services.store), auth(services.identity), statusCb(sc), controlCb(0), controlContext(0), context(ctx),
          inputs(w,h,capabilities.touch,capabilities.controls),
          video(0), stats(Value::dictionary()), controlSocket(InvalidConnection),
          eventSocket(InvalidConnection), boundPort(0), width(w), height(h), commandSequence(1),
          peerKnown(false), audioReleased(false), physicalDevice(physical),
          firstVideo(false), startupHomeDone(false), userInputSeen(false), firstVideoAt(0),
          timingSent(0), timingOffset(0) {
        try {
            publicHex = hex(pairing.publicKey());
            capabilities.deviceId=pairing.identifier();
            capabilities.serialNumber=pairing.serialNumber();
            capabilities.displayUuid=pairing.displayUuid();
            Connection s = network.bind(port, true);
            endpoints.push_back(new Endpoint(network, s, 1));
            boundPort = network.localPort(s);
            airplayInfo(width, height, capabilities);
            stats.dict["video_width"] = Value::num(width);
            stats.dict["video_height"] = Value::num(height);
            stats.dict["port"] = Value::num(boundPort);
            stats.dict["device_id"]=Value::str(capabilities.deviceId);
            stats.dict["accessory_serial"]=Value::str(capabilities.serialNumber);
            stats.dict["display_uuid"]=Value::str(capabilities.displayUuid);
            stats.dict["video_frames"] = Value::num(0);
            stats.dict["audio_packets"] = Value::num(0);
            stats.dict["video_only"] = Value::flag(!capabilities.audioOutput);
            stats.dict["audio_output_enabled"] = Value::flag(capabilities.audioOutput);
            stats.dict["microphone_enabled"] = Value::flag(capabilities.microphone);
            stats.dict["touch_enabled"] = Value::flag(capabilities.touch);
            stats.dict["physical_media_verified"] = Value::flag(false);
            stats.dict["retired_audio_played_bytes"] = Value::num(0);
            stats.dict["audio_played_bytes"] = Value::num(0);
        } catch (...) {
            for (size_t i = 0; i < endpoints.size(); ++i)
                delete endpoints[i];
            throw;
        }
    }
    ~Impl() {
        runtime.cancel();
        runtime.join();
        for (size_t i = 0; i < endpoints.size(); ++i)
            delete endpoints[i];
        delete video;
    }
    void note(const std::string &message) {
        {
            SessionLock guard(runtime);
            stats.dict["stage"] = Value::str(message);
        }
        if (statusCb)
            statusCb(context, message);
    }
    void flag(const char *name) {
        {
            SessionLock guard(runtime);
            stats.dict[name] = Value::flag(true);
        }
    }
    void count(const char *name, U64 add = 1) {
        {
            SessionLock guard(runtime);
            stats.dict[name] = Value::num(stats.dict[name].integer + add);
        }
    }
    Value snapshot() {
        SessionLock guard(runtime);
        Value result = stats;
        if (video)
            result.dict["video_frames"] = Value::num(video->frames());
        // Delivered compressed samples and completed PCM buffers cannot prove
        // that the physical screen/speakers produced the intended output.
        result.dict["physical_media_verified"] = Value::flag(false);
        return result;
    }
    unsigned open(unsigned role, bool tcp, Bytes key = Bytes(), unsigned type = 0) {
        require(endpoints.size() < 24, "AirPlay endpoint limit");
        Endpoint *e = new Endpoint(network, network.bind(0, tcp), role);
        e->key = key;
        e->type = type;
        endpoints.push_back(e);
        return network.localPort(e->socket);
    }
    Bytes streamKey(const Value &stream) {
        require(!pairing.sharedSecret.empty() &&
                    stream.at("streamConnectionID").type == Value::Integer,
                "Missing stream key context");
        return crypto.hkdf(pairing.sharedSecret,
                           "DataStream-Salt" + decimal(stream.at("streamConnectionID").integer),
                           "DataStream-Output-Encryption-Key");
    }
    Value setup(const Value &body) {
        Value out = Value::dictionary();
        if (!body.dict.count("streams")) {
            {
                SessionLock guard(runtime);
                const char *diagnosticKeys[] = {"features", "osVersion", "sourceVersion"};
                for (unsigned k = 0; k < 3; ++k)
                    if (body.dict.count(diagnosticKeys[k]))
                        stats.dict[std::string("peer_") + diagnosticKeys[k]] =
                            body.at(diagnosticKeys[k]);
            }
            out.dict["timingPort"] = Value::num(open(9, false));
            endpoints.back()->type = number(body, "timingPort");
            out.dict["eventPort"] = Value::num(open(3, true));
            out.dict["keepAlivePort"] = Value::num(open(10, false));
            Value features = Value::list();
            features.array.push_back(Value::str("viewAreas"));
            if (body.dict.count("features")) {
                const Value &proposed = body.at("features");
                if (capabilities.bufferedAac)
                    for (size_t k = 0; k < proposed.array.size(); ++k)
                        if (proposed.array[k].string == "mainBuffered")
                            features.array.push_back(Value::str("mainBuffered"));
            }
            out.dict["enabledFeatures"] = features;
            flag("session_setup");
            return out;
        }
        const Value &streams = body.at("streams");
        require(streams.type == Value::Array && streams.array.size() <= 8, "Invalid streams");
        Value results = Value::list();
        for (size_t i = 0; i < streams.array.size(); ++i) {
            const Value &s = streams.array[i];
            unsigned type = number(s, "type");
            Value r = Value::dictionary();
            r.dict["type"] = Value::num(type);
            if (type == 110) {
                Bytes key = streamKey(s);
                r.dict["dataPort"] = Value::num(open(5, true, key, type));
                flag("video_setup");
                note("Video negotiated; waiting for data");
            } else if (type == 100 || type == 101 || type == 102) {
                unsigned bits = number(s, "audioFormat"), rate = 0, channels = 0;
                const unsigned rates[] = {8000, 16000, 24000, 32000, 44100, 48000};
                const unsigned masks[] = {4, 16, 64, 256, 1024, 16384};
                for (unsigned j = 0; j < 6; ++j) {
                    if (bits == masks[j]) {
                        rate = rates[j];
                        channels = 1;
                    }
                    if (bits == masks[j] * 2) {
                        rate = rates[j];
                        channels = 2;
                    }
                }
                bool aac = bits == 0x800000 || bits == 0x400000;
                if (aac) {
                    require(capabilities.aac, "AAC was not offered by receiver");
                    rate = bits == 0x800000 ? 48000 : 44100;
                    channels = 2;
                }
                require(rate != 0, "Phone selected audio format not offered by receiver");
                {
                    SessionLock guard(runtime);
                    stats.dict["audio_sample_rate"] = Value::num(rate);
                    stats.dict["audio_channels"] = Value::num(channels);
                    stats.dict["audio_codec"] = Value::str(aac ? "AAC-LC" : "PCM16");
                }
                r.dict["dataPort"] = Value::num(open(7, false, streamKey(s), type));
                Endpoint *e = endpoints.back();
                e->channels = channels;
                e->audio = media.audio(rate, channels, aac);
                if (s.dict.count("audioType"))
                    e->audioType = s.at("audioType").string;
                if (type == 100 && number(s, "dataPort")) {
                    require(capabilities.microphone && !aac && channels == 1 && number(s, "dataPort") <= 65535,
                            "Invalid microphone stream");
                    e->microphonePort = number(s, "dataPort");
                    e->microphoneKey =
                        crypto.hkdf(pairing.sharedSecret,
                                    "DataStream-Salt" + decimal(s.at("streamConnectionID").integer),
                                    "DataStream-Input-Encryption-Key");
                    e->microphone =
                        media.microphone(rate, channels, number(s, "framesPerPacket", rate / 50));
                    flag("microphone_requested");
                }
                r.dict["streamConnectionID"] = s.at("streamConnectionID");
                r.dict["controlPort"] = Value::num(open(8, false, Bytes(), type));
                r.dict["audioBufferSize"] = Value::num(rate * channels * 2 / 4);
                flag("audio_setup");
                note("Audio negotiated; waiting for data");
            } else if (type == 103) {
                require(capabilities.bufferedAac, "Buffered AAC was not offered by receiver");
                unsigned bits = number(s, "audioFormat");
                require(bits == 0x800000 || bits == 0x400000, "Unsupported buffered audio format");
                require(number(s, "ct", 4) == 4 && number(s, "spf", 1024) == 1024,
                        "Unsupported buffered audio codec");
                require(s.at("shk").type == Value::Data && s.at("shk").data.size() == 32,
                        "Invalid buffered audio key");
                r.dict["dataPort"] = Value::num(open(11, true, s.at("shk").data, type));
                endpoints.back()->buffered = new BufferedAudio(
                    media, bits == 0x800000 ? 48000 : 44100, s.at("streamConnectionID").integer);
                r.dict["audioBufferSize"] = Value::num(8 * 1024 * 1024);
                flag("audio_setup");
                flag("buffered_audio_setup");
            } else
                continue;
            results.array.push_back(r);
        }
        out.dict["streams"] = results;
        return out;
    }
    void control(Endpoint &e, const Bytes &data) {
        Bytes plain = e.cipher ? e.cipher->feed(data) : data;
        std::vector<Request> requests = e.parser.feed(plain);
        for (size_t i = 0; i < requests.size(); ++i) {
            Request &r = requests[i];
            Bytes body;
            std::string mime = "application/x-apple-binary-plist";
            unsigned status = 200;
            bool teardown = false;
            if (r.path.find("/feedback") == std::string::npos &&
                r.path.find("/command") == std::string::npos)
                note("AirPlay " + r.method + " " + r.path);
            if (r.path.find("/pair-setup") != std::string::npos) {
                require(!e.cipher, "Cannot re-pair encrypted connection");
                body = pairing.handle(false, r.body);
                mime = "application/pairing+tlv8";
            } else if (r.path.find("/pair-verify") != std::string::npos) {
                require(!e.cipher, "Cannot reverify encrypted connection");
                body = pairing.handle(true, r.body);
                mime = "application/pairing+tlv8";
            } else if (r.path.find("/info") != std::string::npos)
                body = binaryPlist(airplayInfo(width, height, capabilities));
            else {
                require(e.cipher != 0, "Unauthenticated AirPlay command rejected");
                if (r.path.find("/auth-setup") != std::string::npos) {
                    require(auth != 0, "Accessory identity unavailable");
                    body = pairing.mfiSap(*auth, r.body);
                    mime = "application/octet-stream";
                    flag("mfi_sap_answered");
                } else if (r.method == "SETUP")
                    body = binaryPlist(setup(parsePlist(r.body)));
                else if (r.path.find("/command") != std::string::npos) {
                    Value command = parsePlist(r.body);
                    if (command.dict.count("type")) {
                        {
                            SessionLock guard(runtime);
                            stats.dict["last_command"] = command.at("type");
                            if (command.at("type").string == "modesChanged")
                                stats.dict["modes"] = command;
                        }
                        ControlCallback callback;void* callbackContext;
                        { SessionLock guard(runtime);callback=controlCb;callbackContext=controlContext; }
                        if(callback)callback(callbackContext,command);
                    }
                } else if (r.method == "RECORD") {
                    flag("record_received");
                    note("RECORD accepted; waiting for media");
                } else if (r.method == "SETRATE" || r.method == "SETRATEANCHORTIME" ||
                           r.method == "GETANCHOR" || r.method == "FLUSHBUFFERED") {
                    Value command = r.body.empty() ? Value::dictionary() : parsePlist(r.body);
                    Value result;
                    for (size_t j = 0; j < endpoints.size(); ++j)
                        if (endpoints[j]->buffered)
                            result = endpoints[j]->buffered->control(r.method, command, nowNtp());
                    if (result.type == Value::Dict)
                        body = binaryPlist(result);
                    count("buffered_audio_controls");
                } else if (r.path.find("/feedback") != std::string::npos) {
                    Value streams = Value::list();
                    for (size_t j = 0; j < endpoints.size(); ++j)
                        if (endpoints[j]->buffered)
                            streams.array.push_back(
                                endpoints[j]->buffered->feedback(nowNtp(), runtime.milliseconds()));
                    if (!streams.array.empty()) {
                        Value feedback = Value::dictionary();
                        feedback.dict["streams"] = streams;
                        body = binaryPlist(feedback);
                    }
                } else if (r.method == "TEARDOWN") {
                    Value command = r.body.empty() ? Value::dictionary() : parsePlist(r.body);
                    if (command.dict.count("streams") && !command.at("streams").array.empty()) {
                        const Value &streams = command.at("streams");
                        for (size_t k = 0; k < streams.array.size(); ++k) {
                            unsigned type = number(streams.array[k], "type");
                            for (size_t j = 0; j < endpoints.size(); ++j)
                                if (type && endpoints[j]->type == type) {
                                    Endpoint &retired = *endpoints[j];
                                    if (retired.audio)
                                        count("retired_audio_played_bytes",
                                              retired.audio->playedBytes());
                                    if (retired.buffered)
                                        count("retired_audio_played_bytes",
                                              retired.buffered->playedBytes());
                                    // Stream teardown ends microphone capture immediately;
                                    // retaining it until session exit would keep recording.
                                    delete retired.microphone;retired.microphone=0;
                                    delete retired.audio;retired.audio=0;
                                    delete retired.buffered;retired.buffered=0;
                                    retired.role = 0;
                                    network.close(retired.socket);
                                    retired.socket = InvalidConnection;
                                }
                            if (type == 110 && video) {
                                VideoPipeline *retired;
                                {
                                    SessionLock guard(runtime);
                                    retired = video;
                                    video = 0;
                                }
                                std::auto_ptr<VideoPipeline> release(retired);
                                retired->finish();
                            }
                        }
                        count("stream_teardowns");
                    } else {
                        teardown = true;
                        flag("phone_teardown");
                    }
                } else if (r.method == "GET_PARAMETER")
                    mime = "text/parameters";
            }
            Bytes out = response(r, status, body, mime);
            if (e.cipher)
                out = e.cipher->encrypt(out);
            network.sendAll(e.socket, out);
            if (!e.cipher && !pairing.readKey.empty()) {
                e.cipher = new ControlCipher(crypto, pairing.readKey, pairing.writeKey);
                flag("pair_verified");
            }
            if (teardown)
                runtime.cancel();
        }
    }
    void eventCommands() {
        Endpoint *event = 0;
        for (size_t i = 0; i < endpoints.size(); ++i)
            if (endpoints[i]->role == 4)
                event = endpoints[i];
        if (!event)
            return;
        if (capabilities.audioOutput && !audioReleased && stats.dict.count("record_received") && stats.dict.count("modes")) {
            Value resource = Value::dictionary(), resources = Value::list(),
                  params = Value::dictionary(), command = Value::dictionary();
            resource.dict["resourceID"] = Value::num(2);
            resource.dict["transferType"] = Value::num(2);
            resources.array.push_back(resource);
            params.dict["resources"] = resources;
            command.dict["type"] = Value::str("changeModes");
            command.dict["params"] = params;
            Bytes body = binaryPlist(command);
            std::ostringstream header;
            header << "POST /command RTSP/1.0\r\nCSeq: " << commandSequence++
                   << "\r\nContent-Type: application/x-apple-binary-plist\r\nContent-Length: "
                   << body.size() << "\r\n\r\n";
            network.sendAll(event->socket, event->cipher->encrypt(join(bytes(header.str()), body)));
            audioReleased = true;
            flag("audio_release_requested");
        }
        std::vector<Value> commands;
        {
            SessionLock guard(runtime);
            // One short Home press per connection, after media has started.
            // Never steal the page back after the user has begun interacting.
            if (!startupHomeDone && firstVideo && stats.dict.count("record_received") &&
                U32(runtime.milliseconds()-firstVideoAt)>=500) {
                if (userInputSeen || !capabilities.controls) {
                    startupHomeDone=true;
                    stats.dict["startup_home_skipped"]=Value::flag(true);
                } else if (inputs.pending()==0) {
                    startupHomeDone=true;
                    bool down=inputs.key(InputHome,true);
                    bool up=inputs.key(InputHome,false);
                    stats.dict["startup_home_queued"]=Value::flag(down && up);
                }
            }
            inputs.drain(commands);
            stats.dict["input_queue_overflows"]=Value::num(inputs.overflows());
        }
        for (size_t i = 0; i < commands.size(); ++i) {
            Bytes b = binaryPlist(commands[i]);
            std::ostringstream h;
            h << "POST /command RTSP/1.0\r\nCSeq: " << commandSequence++
              << "\r\nContent-Type: application/x-apple-binary-plist\r\nContent-Length: "
              << b.size() << "\r\n\r\n";
            network.sendAll(event->socket, event->cipher->encrypt(join(bytes(h.str()), b)));
            count("input_commands_sent");
        }
    }
    void accept(Endpoint &e) {
        PeerAddress addr;
        Connection s = network.accept(e.socket, addr);
        if ((e.role == 1 && controlSocket != InvalidConnection) ||
            (e.role == 3 && eventSocket != InvalidConnection) ||
            (e.role != 1 && (!peerKnown || !addr.sameHost(peerSocketAddress)))) {
            network.close(s);
            return;
        }
        Endpoint *child = new Endpoint(network, s, e.role + 1);
        endpoints.push_back(child);
        child->type = e.type;
        if (e.role == 1) {
            controlSocket = s;
            peerSocketAddress = addr;
            peerKnown = true;
            flag("tcp_connected");
            note("AirPlay peer connected");
        }
        if (e.role == 3) {
            require(!pairing.sharedSecret.empty(), "Event before pair verify");
            child->cipher = new ControlCipher(
                crypto,
                crypto.hkdf(pairing.sharedSecret, "Events-Salt", "Events-Read-Encryption-Key"),
                crypto.hkdf(pairing.sharedSecret, "Events-Salt", "Events-Write-Encryption-Key"));
            { SessionLock guard(runtime); inputs.ready(true); }
            eventSocket = s;
            flag("event_connected");
        }
        if (e.role == 5) {
            require(video == 0, "Duplicate video stream");
            VideoPipeline *created = media.video(width, height);
            {
                SessionLock guard(runtime);
                video = created;
            }
            child->screen = new ScreenParser(crypto, e.key);
            flag("video_connected");
        }
        if (e.role == 11) {
            require(e.buffered != 0, "Duplicate buffered audio connection");
            child->key = e.key;
            child->buffered = e.buffered;
            e.buffered = 0;
            network.close(e.socket);
            e.socket = InvalidConnection;
            e.role = 0;
            flag("buffered_audio_connected");
        }
    }
    void timestamp(Bytes &b, size_t at) {
        U64 n = runtime.ntpTime();
        n += timingOffset;
        put32(b, at, U32(n >> 32));
        put32(b, at + 4, U32(n));
    }
    U64 nowNtp() {
        Bytes b(8);
        timestamp(b, 0);
        return (U64(be32(b, 0)) << 32) | be32(b, 4);
    }
    void microphone() {
        for (size_t i = 0; i < endpoints.size(); ++i) {
            Endpoint &e = *endpoints[i];
            if (!e.role || !e.microphone)
                continue;
            std::vector<Bytes> packets = e.microphone->poll();
            for (size_t k = 0; k < packets.size(); ++k) {
                Bytes header(12, 0), nonce = nonce64(e.microphoneCounter);
                header[0] = 0x80;
                header[1] = U8(e.type & 127);
                put16(header, 2, U16(e.microphoneCounter));
                put32(header, 4, e.microphoneTime);
                Bytes packet = join(header, crypto.seal(e.microphoneKey, nonce, pcmSwap(packets[k]),
                                                        slice(header, 4, 8)));
                packet = join(packet, slice(nonce, 4, 8));
                PeerAddress dest = peerSocketAddress;
                dest.port = e.microphonePort;
                if (network.sendDatagram(e.socket, packet, dest))
                    count("microphone_packets");
                ++e.microphoneCounter;
                e.microphoneTime += U32(packets[k].size() / 2);
            }
        }
    }
    void bufferedData(Endpoint &e, const Bytes &data) {
        require(e.pending.size() + data.size() <= 131072, "Buffered audio framing limit");
        e.pending.insert(e.pending.end(), data.begin(), data.end());
        size_t at = 0;
        while (e.pending.size() - at >= 2 && e.buffered->hasCapacity()) {
            unsigned n = be16(e.pending, at);
            require(n >= 38, "Invalid buffered audio frame length");
            if (e.pending.size() - at < n)
                break;
            Bytes packet = slice(e.pending, at + 2, n - 2);
            Bytes payload = decryptAudio(crypto, e.key, packet);
            e.buffered->receive(be32(packet, 4), payload);
            count("audio_packets");
            count("audio_payload_bytes", payload.size());
            at += n;
        }
        if (at)
            e.pending.erase(e.pending.begin(), e.pending.begin() + at);
    }
    void timing() {
        if (!peerKnown || U32(runtime.milliseconds() - timingSent) < 1000)
            return;
        for (size_t i = 0; i < endpoints.size(); ++i) {
            Endpoint &e = *endpoints[i];
            if (e.role != 9 || !e.type)
                continue;
            Bytes request(32, 0);
            request[0] = 0x80;
            request[1] = 0xd2;
            put16(request, 2, 7);
            timestamp(request, 24);
            pendingTiming = slice(request, 24, 8);
            PeerAddress dest = peerSocketAddress;
            dest.port = e.type;
            if (network.sendDatagram(e.socket, request, dest))
                count("timing_requests");
        }
        timingSent = runtime.milliseconds();
    }
    void udp(Endpoint &e) {
        Bytes b;
        PeerAddress source;
        if (!network.receiveDatagram(e.socket, b, source))
            return;
        if (!peerKnown || !source.sameHost(peerSocketAddress))
            return;
        if (e.role == 7) {
            Bytes pcm = decryptAudio(crypto, e.key, b);
            e.audio->receive(pcm);
            count("audio_packets");
            count("audio_payload_bytes", pcm.size());
        }
        if (e.role == 9 && b.size() >= 32 && b[1] == 0xd2) {
            Bytes out(32, 0);
            out[0] = 0x80;
            out[1] = 0xd3;
            put16(out, 2, 7);
            std::copy(b.begin() + 24, b.begin() + 32, out.begin() + 8);
            timestamp(out, 16);
            timestamp(out, 24);
            network.sendDatagram(e.socket, out, source);
        }
        if (e.role == 9 && b.size() >= 32 && b[1] == 0xd3 && slice(b, 8, 8) == pendingTiming) {
            Bytes now(8);
            timestamp(now, 0);
            U64 t1 = (U64(be32(b, 8)) << 32) | be32(b, 12),
                t2 = (U64(be32(b, 16)) << 32) | be32(b, 20),
                t3 = (U64(be32(b, 24)) << 32) | be32(b, 28),
                t4 = (U64(be32(now, 0)) << 32) | be32(now, 4);
            I64 rtt = I64(t4 - t1) - I64(t3 - t2);
            if (rtt >= 0 && rtt < 4294967296LL) {
                timingOffset += (I64(t2 - t1) + I64(t3 - t4)) / 2;
                count("timing_responses");
                flag("timing_synchronized");
            }
            pendingTiming.clear();
        }
        if (e.role == 10)
            network.sendDatagram(e.socket, b, source);
    }
    static void entry(void *p) {
        Impl &self = *static_cast<Impl *>(p);
        try {
            self.run();
        } catch (const std::exception &e) {
            if (!self.runtime.cancelled()) {
                self.note(std::string("Receiver error: ") + e.what());
                {
                    SessionLock guard(self.runtime);
                    self.stats.dict["error"] = Value::str(e.what());
                }
            }
        }
        try {
            if (self.video)
                self.video->finish();
        } catch (const std::exception &e) {
            SessionLock guard(self.runtime);
            self.stats.dict["error"] = Value::str(e.what());
        }
        self.flag("session_ended");
        self.save();
    }
    void save() {
        try {
            runtime.saveReport(snapshot());
        } catch (...) {
        }
    }
    void run() {
        note("AirPlay receiver listening");
        U32 saved = runtime.milliseconds();
        while (!runtime.cancelled()) {
            for (size_t k = 0; k < endpoints.size();) {
                if (!endpoints[k]->role) {
                    delete endpoints[k];
                    endpoints.erase(endpoints.begin() + k);
                } else {
                    if (endpoints[k]->buffered) {
                        endpoints[k]->buffered->tick(nowNtp());
                        if (endpoints[k]->role == 12)
                            bufferedData(*endpoints[k], Bytes());
                    }
                    ++k;
                }
            }
            std::vector<Connection> waiting;
            for (size_t i = 0; i < endpoints.size(); ++i)
                if (endpoints[i]->socket != InvalidConnection &&
                    (endpoints[i]->role != 12 || endpoints[i]->buffered->hasCapacity()))
                    waiting.push_back(endpoints[i]->socket);
            std::vector<Connection> ready = network.poll(waiting, 10);
            size_t count = endpoints.size();
            for (size_t i = 0; i < count; ++i) {
                Endpoint &e = *endpoints[i];
                if (e.socket == InvalidConnection ||
                    std::find(ready.begin(), ready.end(), e.socket) == ready.end())
                    continue;
                if (e.role == 1 || e.role == 3 || e.role == 5 || e.role == 11) {
                    accept(e);
                    continue;
                }
                if (e.role >= 7 && e.role <= 10) {
                    udp(e);
                    continue;
                }
                Bytes b;
                if (!network.receive(e.socket, b)) {
                    if (e.role == 12) {
                        network.close(e.socket);
                        e.socket = InvalidConnection;
                        e.role = 13;
                        continue;
                    }
                    runtime.cancel();
                    note("Peer disconnected");
                    break;
                }
                if (e.role == 2)
                    control(e, b);
                else if (e.role == 4) {
                    std::vector<Request> requests = e.parser.feed(e.cipher->feed(b));
                    for (size_t k = 0; k < requests.size(); ++k)
                        if (requests[k].method.find("RTSP/") != 0 &&
                            requests[k].method.find("HTTP/") != 0)
                            network.sendAll(e.socket,
                                            e.cipher->encrypt(response(requests[k], 200, Bytes())));
                } else if (e.role == 6) {
                    std::vector<VideoPacket> packets = e.screen->feed(b);
                    for (size_t j = 0; j < packets.size(); ++j) {
                        if (packets[j].type == 1)
                            video->submit(avccToAnnexB(packets[j].body), 0);
                        else if (packets[j].type == 0) {
                            video->submit(packets[j].body, 0);
                            if (!firstVideo && !packets[j].body.empty()) {
                                firstVideo=true;firstVideoAt=runtime.milliseconds();
                                note("CarPlay video streaming");
                            }
                        }
                    }
                } else if (e.role == 12)
                    bufferedData(e, b);
            }
            if(video)video->checkStatus();
            U64 played = stats.dict["retired_audio_played_bytes"].integer;
            for (size_t j = 0; j < endpoints.size(); ++j)
                if (endpoints[j]->role) {
                    if (endpoints[j]->audio)
                        played += endpoints[j]->audio->playedBytes();
                    if (endpoints[j]->buffered)
                        played += endpoints[j]->buffered->playedBytes();
                }
            {
                SessionLock guard(runtime);
                stats.dict["audio_played_bytes"] = Value::num(played);
            }
            timing();
            microphone();
            eventCommands();
            if (U32(runtime.milliseconds() - saved) > 1000) {
                save();
                saved = runtime.milliseconds();
            }
        }
    }
};
Receiver::Receiver(ReceiverServices &services, StatusCallback s, void *ctx, unsigned port,
                   bool physical, unsigned w, unsigned h)
    : p_(new Impl(services, s, ctx, port, physical, w, h)) {}
Receiver::~Receiver() {
    delete p_;
}
void Receiver::start() {
    p_->runtime.start(Impl::entry, p_);
}
void Receiver::stop() {
    p_->runtime.cancel();
    p_->runtime.join();
}
unsigned Receiver::port() const {
    return p_->boundPort;
}
std::string Receiver::publicKey() const {
    return p_->publicHex;
}
std::string Receiver::deviceId() const { return p_->capabilities.deviceId; }
std::string Receiver::serialNumber() const { return p_->capabilities.serialNumber; }
Value Receiver::report() {
    return p_->snapshot();
}
void Receiver::touchInput(double x,double y,bool down) { touchContact(0,x,y,down); }
bool Receiver::touchContact(unsigned slot,double x,double y,bool down) {
    SessionLock guard(p_->runtime);bool accepted=p_->inputs.contact(slot,x,y,down);
    if(accepted)p_->userInputSeen=true;return accepted;
}
bool Receiver::inputKey(InputKey key,bool down) {
    SessionLock guard(p_->runtime);bool accepted=p_->inputs.key(key,down);
    if(accepted)p_->userInputSeen=true;return accepted;
}
bool Receiver::navigate(int x,int y,int wheel) {
    SessionLock guard(p_->runtime);bool accepted=p_->inputs.navigate(x,y,wheel);
    if(accepted)p_->userInputSeen=true;return accepted;
}
void Receiver::cancelInput() { SessionLock guard(p_->runtime);p_->inputs.cancel(); }
void Receiver::setNightMode(bool night) { SessionLock guard(p_->runtime);p_->inputs.nightMode(night); }
bool Receiver::requestKeyFrame() { SessionLock guard(p_->runtime);return p_->inputs.keyFrame(); }
void Receiver::setControlCallback(ControlCallback callback,void* context) {
    SessionLock guard(p_->runtime);p_->controlCb=callback;p_->controlContext=context;
}
} // namespace cp
