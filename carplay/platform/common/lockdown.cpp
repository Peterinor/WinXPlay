#include "native_services.h"
#include "receiver_context.h"
#include "local_identity.h"
#include "diagnostics.h"
#include "network_wait.h"
#include "../../devices/profile.h"
#include "../../core/receiver.h"
#include <limits>
#include <memory>
#include <sstream>
#ifdef UNDER_CE
#include "phone_pair.h"
#endif
#ifdef WINXPLAY_WOLFSSL
#include <wolfssl/ssl.h>
#include <wolfssl/wolfio.h>
#endif
namespace cp {
namespace {
#ifdef WINXPLAY_WOLFSSL
#include "tls_wolfssl.h"
#else
#define API(result, name, args)                                                                    \
    typedef result(__cdecl *name##_t) args;                                                        \
    name##_t name
// These declarations match the public OpenSSL 3 C ABI. No private structs are inspected.
struct TlsApi {
    HMODULE sslModule, cryptoModule;
    API(const void *, TLS_client_method, ());
    API(void *, SSL_CTX_new, (const void *));
    API(void, SSL_CTX_free, (void *));
    API(void *, SSL_new, (void *));
    API(void, SSL_free, (void *));
    API(int, SSL_set_fd, (void *, int));
    API(int, SSL_connect, (void *));
    API(int, SSL_read, (void *, void *, int));
    API(int, SSL_write, (void *, const void *, int));
    API(int, SSL_get_error, (const void *, int));
    API(int, SSL_CTX_use_certificate, (void *, void *));
    API(int, SSL_CTX_use_PrivateKey, (void *, void *));
    API(int, SSL_CTX_check_private_key, (const void *));
    API(int, SSL_CTX_set_cipher_list, (void *, const char *));
    API(void, SSL_CTX_set_verify, (void *, int, void *));
    API(void, SSL_CTX_set_cert_verify_callback, (void *, int (*)(void *, void *), void *));
    API(void *, BIO_new_mem_buf, (const void *, int));
    API(int, BIO_free, (void *));
    API(void *, PEM_read_bio_X509, (void *, void **, void *, void *));
    API(void *, PEM_read_bio_PrivateKey, (void *, void **, void *, void *));
    API(void, X509_free, (void *));
    API(void, EVP_PKEY_free, (void *));
    API(void *, X509_STORE_CTX_get0_cert, (void *));
    API(void, X509_STORE_CTX_set_error, (void *, int));
    API(void *, X509_get_pubkey, (void *));
    API(int, EVP_PKEY_eq, (const void *, const void *));
    FARPROC get(HMODULE module, const char *name) {
        FARPROC proc = GetProcAddress(module, name);
        require(proc != 0, "OpenSSL TLS API missing");
        return proc;
    }
    TlsApi() : sslModule(0), cryptoModule(0) {
        std::wstring cryptoPath =
            environment(L"CARPLAY_CRYPTO", L"");
        std::wstring sslPath =
            environment(L"CARPLAY_SSL", L"");
        require(cryptoPath.size() > 3 && cryptoPath[1] == ':' && sslPath.size() > 3 &&
                    sslPath[1] == ':',
                "TLS paths must be absolute");
        cryptoModule = LoadLibraryExW(cryptoPath.c_str(), 0, LOAD_WITH_ALTERED_SEARCH_PATH);
        sslModule = LoadLibraryExW(sslPath.c_str(), 0, LOAD_WITH_ALTERED_SEARCH_PATH);
        try {
            require(cryptoModule != 0 && sslModule != 0,
                    "Cannot load configured OpenSSL TLS libraries");
#define S(name) name = (name##_t)get(sslModule, #name)
#define C(name) name = (name##_t)get(cryptoModule, #name)
            S(TLS_client_method);
            S(SSL_CTX_new);
            S(SSL_CTX_free);
            S(SSL_new);
            S(SSL_free);
            S(SSL_set_fd);
            S(SSL_connect);
            S(SSL_read);
            S(SSL_write);
            S(SSL_get_error);
            S(SSL_CTX_use_certificate);
            S(SSL_CTX_use_PrivateKey);
            S(SSL_CTX_check_private_key);
            S(SSL_CTX_set_cipher_list);
            S(SSL_CTX_set_verify);
            S(SSL_CTX_set_cert_verify_callback);
            C(BIO_new_mem_buf);
            C(BIO_free);
            C(PEM_read_bio_X509);
            C(PEM_read_bio_PrivateKey);
            C(X509_free);
            C(EVP_PKEY_free);
            C(X509_STORE_CTX_get0_cert);
            C(X509_STORE_CTX_set_error);
            C(X509_get_pubkey);
            C(EVP_PKEY_eq);
#undef S
#undef C
        } catch (...) {
            if (sslModule)
                FreeLibrary(sslModule);
            if (cryptoModule)
                FreeLibrary(cryptoModule);
            throw;
        }
    }
    ~TlsApi() {
        FreeLibrary(sslModule);
        FreeLibrary(cryptoModule);
    }
};
#undef API
class Channel {
    SOCKET socket_;
    TlsApi *api_;
    void *context_, *ssl_, *expected_;
    Channel(const Channel &);
    Channel &operator=(const Channel &);
    static int verify(void *store, void *opaque) {
        Channel *self = (Channel *)opaque;
        TlsApi &a = *self->api_;
        void *cert = a.X509_STORE_CTX_get0_cert(store);
        if (!cert)
            return 0;
        void *actual = a.X509_get_pubkey(cert), *expected = a.X509_get_pubkey(self->expected_);
        int result = actual && expected && a.EVP_PKEY_eq(actual, expected) == 1;
        if (actual)
            a.EVP_PKEY_free(actual);
        if (expected)
            a.EVP_PKEY_free(expected);
        a.X509_STORE_CTX_set_error(store, result ? 0 : 50);
        return result;
    }
    void write(const Bytes &b) {
        size_t offset = 0;
        while (offset < b.size()) {
            int n = ssl_ ? api_->SSL_write(ssl_, &b[offset], int(b.size() - offset))
                         : send(socket_, (const char *)&b[offset], int(b.size() - offset), 0);
            require(n > 0, "Lockdown write failed");
            offset += n;
        }
    }
    Bytes read(size_t n) {
        require(n <= 4 * 1024 * 1024, "Lockdown frame exceeds limit");
        Bytes b(n);
        size_t offset = 0;
        while (offset < n) {
            int count = ssl_ ? api_->SSL_read(ssl_, &b[offset], int(n - offset))
                             : recv(socket_, (char *)&b[offset], int(n - offset), 0);
            require(count > 0, "Lockdown read failed or timed out");
            offset += count;
        }
        return b;
    }
    void *certificate(const Bytes &pem) {
        require(!pem.empty() && pem.size() < 65536, "Invalid pairing certificate size");
        void *bio = api_->BIO_new_mem_buf(&pem[0], int(pem.size()));
        require(bio != 0, "Cannot read certificate");
        void *cert = api_->PEM_read_bio_X509(bio, 0, 0, 0);
        api_->BIO_free(bio);
        require(cert != 0, "Invalid pairing certificate");
        return cert;
    }

  public:
    explicit Channel(SOCKET socket)
        : socket_(socket), api_(0), context_(0), ssl_(0), expected_(0) {}
    void sendBytes(const Bytes &data) {
        write(data);
    }
    Bytes receiveSome() {
        DWORD timeout = 500;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        Bytes b(4096);
        int count = ssl_ ? api_->SSL_read(ssl_, &b[0], int(b.size()))
                         : recv(socket_, (char *)&b[0], int(b.size()), 0);
        if (count < 0) {
            int error = ssl_ ? api_->SSL_get_error(ssl_, count) : 0;
            if (WSAGetLastError() == WSAETIMEDOUT || error == 2 || error == 3)
                return Bytes();
        }
        require(count > 0, "CarKit byte stream closed or read failed");
        b.resize(count);
        return b;
    }
    ~Channel() {
        if (api_) {
            if (ssl_)
                api_->SSL_free(ssl_);
            if (context_)
                api_->SSL_CTX_free(context_);
            if (expected_)
                api_->X509_free(expected_);
            delete api_;
        }
        closesocket(socket_);
    }
    void secure(const Value &record) {
        require(!ssl_, "TLS already started");
        api_ = new TlsApi();
        context_ = api_->SSL_CTX_new(api_->TLS_client_method());
        require(context_ != 0, "TLS context failed");
        void *root = certificate(record.at("RootCertificate").data);
        int loaded = api_->SSL_CTX_use_certificate(context_, root);
        api_->X509_free(root);
        require(loaded == 1, "Cannot load pairing certificate");
        Bytes key = record.at("RootPrivateKey").data;
        require(!key.empty() && key.size() < 65536, "Invalid pairing key size");
        void *bio = api_->BIO_new_mem_buf(&key[0], int(key.size()));
        require(bio != 0, "Cannot read pairing key");
        void *privateKey = api_->PEM_read_bio_PrivateKey(bio, 0, 0, 0);
        api_->BIO_free(bio);
        SecureZeroMemory(&key[0], key.size());
        require(privateKey != 0, "Invalid pairing private key");
        loaded = api_->SSL_CTX_use_PrivateKey(context_, privateKey);
        api_->EVP_PKEY_free(privateKey);
        require(loaded == 1 && api_->SSL_CTX_check_private_key(context_) == 1,
                "Pairing certificate/key mismatch");
        expected_ = certificate(record.at("DeviceCertificate").data);
        // Pairing certificates may use legacy signatures. Relax OpenSSL's PKI policy ONLY here;
        // the TLS peer still must match the exact public key in this device's Windows pair record.
        require(api_->SSL_CTX_set_cipher_list(context_, "DEFAULT:@SECLEVEL=0") == 1,
                "TLS cipher setup failed");
        api_->SSL_CTX_set_verify(context_, 1, 0);
        api_->SSL_CTX_set_cert_verify_callback(context_, verify, this);
        ssl_ = api_->SSL_new(context_);
        require(ssl_ != 0, "TLS allocation failed");
        require(api_->SSL_set_fd(ssl_, int(socket_)) == 1 && api_->SSL_connect(ssl_) == 1,
                "Device TLS handshake failed (pairing key pin required)");
    }
    Value request(const Value &value) {
        Bytes body = xmlPlist(value), head(4);
        put32(head, 0, U32(body.size()));
        head.insert(head.end(), body.begin(), body.end());
        write(head);
        Bytes size = read(4);
        U32 n = be32(size, 0);
        require(n > 0 && n <= 4 * 1024 * 1024, "Invalid Lockdown response size");
        Value result = parsePlist(read(n));
        if (result.dict.count("Error")) {
            std::string err = result.at("Error").type == Value::String ? result.at("Error").string
                                                                       : "non-text error";
            throw std::runtime_error("Lockdown: " + err);
        }
        return result;
    }
};
#endif
} // namespace
Value restorePhoneSession(UsbMux &mux, const AppleDevice &device) {
    // Refresh host trust after USB reattachment without opening CarKit or
    // starting accessory identification while its network is unavailable.
    diagnosticEvent("TRUST_SESSION_BEGIN");
    Value report=Value::dictionary();
    Value record=mux.pairRecord(device);
    Channel channel(mux.connectDevice(device,62078));
    Value start=Value::dictionary();
    start.dict["Label"]=Value::str("WinXPlay");
    start.dict["Request"]=Value::str("StartSession");
    start.dict["HostID"]=record.at("HostID");
    start.dict["SystemBUID"]=record.at("SystemBUID");
    Value response;
    try { response=channel.request(start); }
    catch(const std::exception &e){
#ifdef UNDER_CE
        const std::string error=e.what();
        if(error=="Lockdown: InvalidHostID" || error=="Lockdown: InvalidPairRecord")
            invalidatePhonePair(executableDirectory(),device.serial);
#endif
        diagnosticEvent("TRUST_SESSION_FAILED",e.what());throw;
    }
    require(response.at("EnableSessionSSL").type==Value::Boolean &&
            response.at("EnableSessionSSL").boolean,"Device did not enable Lockdown TLS");
    channel.secure(record);
    report.dict["lockdown_tls"]=Value::flag(true);
    report.dict["carkit_service_opened"]=Value::flag(false);
    diagnosticEvent("TRUST_SESSION_RESTORED","CarKit not opened");
    return report;
}
Value probeCarKit(UsbMux &mux, const AppleDevice &device, Receiver *receiver, HANDLE cancel,
                  void (*status)(void *, const std::string &), void *statusContext) {
    diagnosticEvent("CARKIT_BEGIN");
    Value report = Value::dictionary();
    // This state is read on the first network wait and also on early failures.
    report.dict["waiting_for_network"] = Value::flag(false);
    std::string stage = "read_pair_record";
    try {
        Value record = mux.pairRecord(device);
        report.dict["phone_pair_record"] = Value::flag(true);
        stage = "start_lockdown_session";
        Channel lockdown(mux.connectDevice(device, 62078));
        Value start = Value::dictionary();
        start.dict["Label"] = Value::str("WinXPlay");
        start.dict["Request"] = Value::str("StartSession");
        start.dict["HostID"] = record.at("HostID");
        start.dict["SystemBUID"] = record.at("SystemBUID");
        diagnosticEvent("LOCKDOWN_START_SESSION_BEGIN");
        Value response = lockdown.request(start);
        diagnosticEvent("LOCKDOWN_START_SESSION_ACCEPTED");
        require(response.at("EnableSessionSSL").type == Value::Boolean &&
                    response.at("EnableSessionSSL").boolean,
                "Device did not enable Lockdown TLS");
        stage = "lockdown_tls";
        lockdown.secure(record);
        report.dict["lockdown_tls"] = Value::flag(true);
        stage = "start_carkit_service";
        Value service = Value::dictionary();
        service.dict["Request"] = Value::str("StartService");
        service.dict["Service"] = Value::str("com.apple.carkit.service");
        response = lockdown.request(service);
        require(response.at("Port").type == Value::Integer && response.at("Port").integer > 0 &&
                    response.at("Port").integer <= 65535,
                "Invalid CarKit port");
        stage = "connect_carkit_service";
        Channel carkit(mux.connectDevice(device, unsigned(response.at("Port").integer)));
        if (response.dict.count("EnableServiceSSL") && response.at("EnableServiceSSL").boolean)
            carkit.secure(record);
        report.dict["carkit_service_opened"] = Value::flag(true);
        stage = "iap2_link_negotiation";
        diagnosticEvent("CARKIT_OPENED");
        LinkNegotiator link;
        link.start(0);
        DWORD began = GetTickCount();
        CarPlayNetworkWait network(began);
        bool identification = false, certificateRequested = false;
        bool missingIdentity = false, authenticated = false;
        bool extended = receiver || environment(L"CARPLAY_IDENTIFY", L"") == L"1";
        bool identificationAccepted = false, availability = false;
        bool recordedStream = false;
        std::vector<Frame> outbound;
        std::auto_ptr<LocalAuth> identity;
        std::auto_ptr<MfiExchange> authentication;
        Value incoming = Value::list();
        while ((receiver || DWORD(GetTickCount() - began) < 25000) && !identification &&
               !missingIdentity && (receiver || (extended ? !availability : !authenticated)) &&
               (!cancel || WaitForSingleObject(cancel, 0) != WAIT_OBJECT_0)) {
            if (receiver) {
                Value state = receiver->report();
                require(!state.dict.count("error"),
                        "AirPlay media receiver failed; see streaming-diagnostics.plist");
                if (state.dict.count("session_ended"))
                    break;
                require(state.dict.count("record_received") || !network.timedOut(GetTickCount()),
                        "CarPlay setup timed out; reconnecting automatically");
                stage = network.waiting() ? "receiver_network_address" :
                    (state.dict.count("record_received") ? "streaming" : "airplay_setup");
                if(!recordedStream && state.dict.count("record_received")){
                    recordedStream=true;diagnosticEvent("AIRPLAY_RECORD_RECEIVED");
                }
            }
            link.tick(DWORD(GetTickCount() - began));
            if (link.canSend() && !outbound.empty()) {
                link.send(outbound.front());
                outbound.erase(outbound.begin());
            }
            Bytes output = link.takeOutput();
            if (!output.empty())
                carkit.sendBytes(output);
            link.feed(carkit.receiveSome());
            if (link.ready())
                report.dict["iap2_link_ready"] = Value::flag(true);
            std::vector<Frame> frames = link.takeMessages();
            for (size_t i = 0; i < frames.size(); i++) {
                std::ostringstream message;message<<"id=0x"<<std::hex<<unsigned(frames[i].id);
                diagnosticEvent("IAP2_CONTROL",message.str());
                if (incoming.array.size() < 256)
                    incoming.array.push_back(Value::num(frames[i].id));
                report.dict["received_control_messages"] = incoming;
                if (receiver)
                    writeDiagnosticFile(executableDirectory() + L"/session-usb.plist", xmlPlist(report));
                if (frames[i].id == 0x1d00) {
                    if (extended) {
                        std::string serial=receiver?receiver->serialNumber():applicationAccessorySerialNumber();
                        outbound.push_back(accessoryIdentification(mux.usbInterface(), deviceProfile().displayName,
                            deviceProfile().model,serial.c_str()));
                    }
                    else
                        identification = true;
                }
                if (frames[i].id == 0x1d02) {
                    identificationAccepted = true;
                    report.dict["iap2_identification_accepted"] = Value::flag(true);
                    if (receiver) {
                        std::vector<Frame> subscriptions = wiredSubscriptions();
                        outbound.insert(outbound.end(), subscriptions.begin(), subscriptions.end());
                    }
                }
                if (frames[i].id == 0x1d03) {
                    report.dict["identification_rejection"] = Value::str(hex(frames[i].body));
                    require(false, "iPhone rejected accessory identification");
                }
                if (frames[i].id == 0x4300) {
                    availability = true;
                    report.dict["carplay_availability_received"] = Value::flag(true);
                    network.available();
                }
                if (frames[i].id == 0xaa00) {
                    certificateRequested = true;
                    report.dict["mfi_certificate_requested"] = Value::flag(true);
                    std::wstring path = environment(L"CARPLAY_IDENTITY", L"");
                    if (path.empty()) {
                        missingIdentity = true;
                        break;
                    }
                    if (!identity.get()) {
                        stage = "load_accessory_identity";
                        identity.reset(new LocalAuth(
                            path, environment(L"CARPLAY_CRYPTO",
                                              L"")));
                        authentication.reset(new MfiExchange(*identity));
                        report.dict["mfi_identity_loaded"] = Value::flag(true);
                    }
                }
                if (frames[i].id >= 0xaa00 && frames[i].id <= 0xaa05 && authentication.get()) {
                    stage = "mfi_authentication";
                    std::vector<Frame> replies = authentication->receive(frames[i]);
                    for (size_t j = 0; j < replies.size(); ++j) {
                        link.send(replies[j]);
                        if (replies[j].id == 0xaa01)
                            report.dict["mfi_certificate_sent"] = Value::flag(true);
                        if (replies[j].id == 0xaa03)
                            report.dict["mfi_challenge_answered"] = Value::flag(true);
                    }
                    authenticated = authentication->state() == MfiExchange::Accepted;
                    if (authenticated)
                        report.dict["mfi_authentication_accepted"] = Value::flag(true);
                }
            }
            if (receiver && network.waiting()) {
                stage = "receiver_network_address";
                std::string addresses = network.poll(GetTickCount(), receiverAddresses);
                if(addresses.empty()) {
                    if(!report.at("waiting_for_network").boolean){
                        diagnosticEvent("CARKIT_WAIT_NETWORK", "session retained; no authentication retry");
                        if(status)status(statusContext,"Waiting for receiver USB network IPv6 link-local address");
                        report.dict["waiting_for_network"]=Value::flag(true);
                        writeDiagnosticFile(executableDirectory()+L"/session-usb.plist",xmlPlist(report));
                    }
                } else {
                    if (receiver) {
                        Bytes body, group;
                        size_t at = 0;
                        while (at < addresses.size()) {
                            size_t end = addresses.find(';', at);
                            if (end == std::string::npos)
                                end = addresses.size();
                            Bytes value = bytes(addresses.substr(at, end - at));
                            value.push_back(0);
                            Bytes p = parameter(0, value);
                            group.insert(group.end(), p.begin(), p.end());
                            at = end + 1;
                        }
                        body = parameter(0, group);
                        Bytes port(4);
                        put32(port, 0, receiver->port());
                        Bytes p = parameter(2, port);
                        body.insert(body.end(), p.begin(), p.end());
                        const std::string values[] = {receiver->deviceId(), receiver->publicKey(),
                                                      "320.17"};
                        for (unsigned j = 0; j < 3; ++j) {
                            Bytes value = bytes(values[j]);
                            value.push_back(0);
                            p = parameter(U16(3 + j), value);
                            body.insert(body.end(), p.begin(), p.end());
                        }
                        outbound.push_back(Frame(0x4301, body));
                        report.dict["carplay_start_queued"] = Value::flag(true);
                        report.dict["waiting_for_network"] = Value::flag(false);
                        diagnosticEvent("CARKIT_NETWORK_READY", "start queued in existing session");
                        if(status)status(statusContext,"Receiver USB network ready; waiting for CarPlay video");
                        writeDiagnosticFile(executableDirectory()+L"/session-usb.plist",xmlPlist(report));
                    }
                }
            }
        }
        Bytes finalAck = link.takeOutput();
        if (!finalAck.empty())
            carkit.sendBytes(finalAck);
        require(identification || certificateRequested,
                "iPhone did not request identification or authentication");
        if (identification)
            report.dict["iap2_identification_requested"] = Value::flag(true);
        if (missingIdentity) {
            report.dict["blocked_on"] = Value::str("No accessory MFi identity provisioned");
            stage = "load_accessory_identity";
            require(false, "Accessory identity is missing; place certificate.p7b and identity.pk8 in the identity folder beside the executable, or set CARPLAY_IDENTITY");
        }
        require(!authentication.get() || authenticated,
                "iPhone did not accept accessory authentication within probe deadline");
        if (extended)
            require(identificationAccepted && availability,
                    "Identification or CarPlay availability timeout");
        report.dict["next_stage"] =
            Value::str(receiver ? "Session ended; see streaming diagnostics"
                                : "Start receiver for video and audio validation");
    } catch (const std::exception &e) {
        diagnosticEvent("CARKIT_FAILED",stage+" "+e.what());
#ifdef UNDER_CE
        const std::string failure=e.what();
        if(stage=="start_lockdown_session" &&
           (failure=="Lockdown: InvalidHostID" || failure=="Lockdown: InvalidPairRecord")){
            try{invalidatePhonePair(executableDirectory(),device.serial);
                report.dict["pairing_reset_for_retry"]=Value::flag(true);
            }catch(const std::exception &reset){report.dict["pairing_reset_error"]=Value::str(reset.what());}
        }
#endif
        report.dict["failed_stage"] = Value::str(stage);
        report.dict["error"] = Value::str(e.what());
    }
    report.dict["carplay_connected"] =
        Value::flag(receiver && receiver->report().dict.count("record_received") != 0);
    diagnosticEvent("CARKIT_END",report.at("carplay_connected").boolean?"record_received=true":"record_received=false");
    return report;
}
} // namespace cp
