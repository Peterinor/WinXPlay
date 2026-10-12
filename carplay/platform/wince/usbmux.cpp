#include "../common/native_services.h"
#include "../common/native_crypto.h"
#include "../common/phone_pair.h"
#include "../common/diagnostics.h"
#include "usb_driver/contract.h"
#include <algorithm>
#include <sstream>
#ifdef UNDER_CE
#include <ntddndis.h>
#endif
namespace cp {
namespace {
#include "usb_diagnostics.h"
#include "network_diagnostics.h"
void muxEvent(const char *event,unsigned a=0,unsigned b=0,unsigned c=0){
    std::ostringstream s;s<<"a="<<a<<" b="<<b<<" c="<<c;diagnosticEvent(event,s.str());
}
void socketFailure(const char *operation,int result,size_t transferred,size_t expected){
    int error=result<0?WSAGetLastError():0;
    std::ostringstream s;s<<operation<<" result="<<result<<" wsa="<<error
        <<" transferred="<<transferred<<" expected="<<expected;
    diagnosticEvent("USB_SERVICE_IO_FAILED",s.str());throw std::runtime_error(s.str());
}
struct Connection {
    SOCKET socket;
    unsigned source, destination;
    U32 sent, received, acknowledged;
    bool connected, closed, remoteFin, ackPendingLogged;
    DWORD begun;
    Bytes incoming;
    Connection()
        : socket(INVALID_SOCKET), source(0), destination(0), sent(0), received(0), acknowledged(0),
          connected(false), closed(false), remoteFin(false), ackPendingLogged(false), begun(GetTickCount()) {}
    ~Connection() {
        if (socket != INVALID_SOCKET)
            closesocket(socket);
    }
};
class Host {
    HANDLE usb_, thread_, stop_;
    CRITICAL_SECTION mutex_;
    std::vector<Connection *> connections_;
    Bytes pending_;
    U16 sequence_, ack_;
    unsigned nextPort_;
    unsigned loggedHeaders_;
    unsigned loggedWrites_;
    std::string error_;
    void write(const Bytes &data) {
        if(loggedWrites_++<12){
            // Header-only evidence of exactly what we send, never Lockdown/TLS payloads.
            unsigned type=be32(data,0);
            size_t count=type==0?20:(type==2?17:36);
            if(count>data.size())count=data.size();
            const char *hex="0123456789abcdef";std::string header;
            for(size_t i=0;i<count;++i){header+=hex[data[i]>>4];header+=hex[data[i]&15];}
            diagnosticEvent("MUX_TX_HEADER_HEX",header);
        }
        size_t pos = 0;
        while (pos < data.size()) {
            DWORD n = 0;
            BOOL ok=WriteFile(usb_, &data[pos], std::min(unsigned(data.size() - pos), 65536u), &n, 0);
            if(!ok || !n){
                muxEvent("MUX_BULK_WRITE_FAILED",ok?0:GetLastError(),pos,n);
                require(false,"USBMUX bulk write failed");
            }
            pos += n;
        }
    }
    bool readFrame(Bytes &out) {
        if (pending_.size() < 8 || pending_.size() < be32(pending_, 4)) {
            Bytes chunk(65536);
            DWORD n = 0;
            if (!ReadFile(usb_, &chunk[0], chunk.size(), &n, 0)) {
                if (GetLastError() == ERROR_TIMEOUT)
                    return false;
                muxEvent("MUX_BULK_READ_FAILED",GetLastError());
                require(false, "iPhone USB disconnected");
            }
            require(pending_.size() + n <= 1024 * 1024, "USBMUX input overflow");
            pending_.insert(pending_.end(), chunk.begin(), chunk.begin() + n);
        }
        if (pending_.size() < 8)
            return false;
        unsigned length = be32(pending_, 4);
        require(length >= 16 && length <= 1024 * 1024, "USBMUX invalid frame length");
        if (pending_.size() < length)
            return false;
        if(loggedHeaders_<8){
            ++loggedHeaders_;muxEvent("MUX_RX_HEADER_PROTOCOL_LENGTH_WORD8",be32(pending_,0),length,be32(pending_,8));
        }
        out = slice(pending_, 0, length);
        pending_.erase(pending_.begin(), pending_.begin() + length);
        if (be32(out, 0) != 0) {
            // Phone RX headers can contain 0xfaceface; 0xfeedface is our TX marker.
            // Length/protocol/TCP bounds define the frame, not an echoed TX marker.
            ack_ = be16(out, 12);
        }
        return true;
    }
    void frame(unsigned type, const Bytes &body) {
        Bytes out(16);
        put32(out, 0, type);
        put32(out, 4, 16 + body.size());
        put32(out, 8, 0xfeedface);
        put16(out, 12, sequence_++);
        put16(out, 14, ack_);
        out.insert(out.end(), body.begin(), body.end());
        write(out);
    }
    void tcp(Connection &c, U8 flags, const Bytes &payload = Bytes()) {
        Bytes out(20);
        put16(out, 0, U16(c.source));
        put16(out, 2, U16(c.destination));
        put32(out, 4, c.sent);
        put32(out, 8, c.received);
        out[12] = 0x50;
        out[13] = flags;
        put16(out, 14, 512);
        out.insert(out.end(), payload.begin(), payload.end());
        frame(6, out);
        c.sent += payload.size() + ((flags & 3) ? 1 : 0);
    }
    void dispatch(const Bytes &packet) {
        if(be32(packet,0)==1){muxEvent("MUX_CONTROL",packet.size()>16?packet[16]:0,packet.size());return;}
        if (be32(packet, 0) != 6)
            return;
        require(packet.size() >= 36, "Short USBMUX TCP header");
        unsigned header = (packet[28] >> 4) * 4;
        require(header >= 20 && header <= packet.size() - 16, "Invalid USBMUX TCP offset");
        unsigned destination = be16(packet, 18);
        for (size_t i = 0; i < connections_.size(); ++i) {
            Connection &c = *connections_[i];
            if (c.source != destination || c.closed)
                continue;
            require(be16(packet, 16) == c.destination, "USBMUX TCP source mismatch");
            U8 flags = packet[29];
            U32 seq = be32(packet, 20), ack = be32(packet, 24);
            if (flags & 4) {
                muxEvent("MUX_PEER_RESET",c.source,c.destination);
                c.closed = true;
                shutdown(c.socket, SD_BOTH);
                return;
            }
            if (flags & 16) {
                require(U32(c.sent - ack) <= 65536, "USBMUX acknowledgement outside send window");
                if (c.acknowledged != ack) {
                    if (c.ackPendingLogged)
                        muxEvent("MUX_ACK_PROGRESS",c.source,c.destination,c.sent-ack);
                    c.ackPendingLogged = false;
                    c.begun = GetTickCount();
                }
                c.acknowledged = ack;
            }
            if (!c.connected) {
                require((flags & 0x12) == 0x12 && ack == 1, "USBMUX SYN acknowledgement rejected");
                c.received = seq + 1;
                c.connected = true;
                muxEvent("MUX_TCP_CONNECTED",c.source,c.destination);
                tcp(c, 16);
                return;
            }
            unsigned size = packet.size() - 16 - header;
            if (seq != c.received) {
                tcp(c, 16);
                return;
            } // acknowledge duplicate; never deliver data twice
            require(c.incoming.size() + size <= 1024 * 1024, "USBMUX peer exceeded receive buffer");
            c.incoming.insert(c.incoming.end(), packet.begin() + 16 + header, packet.end());
            c.received += size;
            if (flags & 1) {
                muxEvent("MUX_PEER_FIN",c.source,c.destination);
                ++c.received;
                tcp(c, 16);
                c.remoteFin = true;
                return;
            }
            if (size)
                tcp(c, 16);
            return;
        }
    }
    void process() {
        for (size_t i = 0; i < connections_.size(); ++i) {
            Connection &c = *connections_[i];
            if (c.closed)
                continue;
            if (!c.sent) {
                tcp(c, 2);
                muxEvent("MUX_SYN_SENT",c.source,c.destination);
                continue;
            }
            if (!c.connected) {
                if (GetTickCount() - c.begun > 5000) {
                    muxEvent("MUX_SYN_TIMEOUT",c.source,c.destination);
                    c.closed = true;
                    shutdown(c.socket, SD_BOTH);
                }
                continue;
            }
            if (!c.incoming.empty()) {
                int n = send(c.socket, (char *)&c.incoming[0], c.incoming.size(), 0);
                if (n > 0)
                    c.incoming.erase(c.incoming.begin(), c.incoming.begin() + n);
                else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                    c.closed = true;
                    tcp(c, 4);
                    continue;
                }
            }
            if (c.remoteFin && c.incoming.empty()) {
                shutdown(c.socket, SD_SEND);
                tcp(c, 17);
                c.closed = true;
                continue;
            }
            if (c.sent != c.acknowledged && !c.ackPendingLogged && GetTickCount() - c.begun > 10000) {
                // USBMUX runs over reliable USB bulk transfers. A quiet CarKit
                // channel may leave a small tail unacknowledged while AirPlay
                // continues over NCM. ACK age alone is not a connection failure.
                // Keep bounded backpressure below; USB errors, peer FIN/RST and
                // application cancellation still close the channel normally.
                muxEvent("MUX_ACK_PENDING",c.source,c.destination,c.sent-c.acknowledged);
                c.ackPendingLogged = true;
            }
            if (U32(c.sent - c.acknowledged) >= 16384)
                continue;
            Bytes data(16384 - U32(c.sent - c.acknowledged));
            int n = recv(c.socket, (char *)&data[0], data.size(), 0);
            if (n > 0) {
                if (c.sent == c.acknowledged)
                    c.begun = GetTickCount();
                data.resize(n);
                tcp(c, 16, data);
            } else if (!n) {
                tcp(c, 17);
                c.closed = true;
            } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                tcp(c, 4);
                c.closed = true;
            }
        }
    }
    static DWORD WINAPI run(void *ctx) {
        Host &h = *(Host *)ctx;
        try {
            while (WaitForSingleObject(h.stop_, 0) != WAIT_OBJECT_0) {
                Bytes packet;
                bool got = h.readFrame(packet);
                EnterCriticalSection(&h.mutex_);
                try {
                    if (got)
                        h.dispatch(packet);
                    h.process();
                } catch (...) {
                    LeaveCriticalSection(&h.mutex_);
                    throw;
                }
                LeaveCriticalSection(&h.mutex_);
            }
        } catch (const std::exception &e) {
            diagnosticEvent("MUX_WORKER_FAILED",e.what());
            EnterCriticalSection(&h.mutex_);
            h.error_ = e.what();
            for (size_t i = 0; i < h.connections_.size(); ++i)
                shutdown(h.connections_[i]->socket, SD_BOTH);
            LeaveCriticalSection(&h.mutex_);
        }
        return 0;
    }

  public:
    WinXPlayUsbInfo info;
    Host() : usb_(INVALID_HANDLE_VALUE), thread_(0), stop_(0), sequence_(0), ack_(0xffff), nextPort_(1), loggedHeaders_(0), loggedWrites_(0) {
        InitializeCriticalSection(&mutex_);
        try {
            usb_ = CreateFileW(L"CMX1:", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
            require(usb_ != INVALID_HANDLE_VALUE, "iPhone USBMUX interface unavailable");
            DWORD n = 0;
            require(DeviceIoControl(usb_, WINXPLAY_USB_QUERY, 0, 0, &info, sizeof(info), &n, 0) &&
                        n == sizeof(info) && info.version == 1,
                    "USB client ABI mismatch");
            muxEvent("MUX_OPEN_GENERATION_CONFIG_NCM",info.generation,info.configuration,info.ncmPresent);
            Bytes version(20);
            put32(version, 4, 20);
            put32(version, 8, 2);
            write(version);
            DWORD start = GetTickCount();
            Bytes reply;
            bool ready = false;
            while (GetTickCount() - start < 5000) {
                if (readFrame(reply) && be32(reply, 0) == 0 && reply.size() == 20 &&
                    be32(reply, 8) == 2) {
                    ready = true;
                    break;
                }
            }
            require(ready, "USBMUX version handshake timeout");
            diagnosticEvent("MUX_VERSION_OK","version=2");
            frame(2, Bytes(1, 7));
            diagnosticEvent("MUX_SETUP_SENT","sequence=0 acknowledgement=65535");
            stop_ = CreateEventW(0, TRUE, FALSE, 0);
            require(stop_ != 0, "USBMUX stop event");
            thread_ = CreateThread(0, 0, run, this, 0, 0);
            require(thread_ != 0, "USBMUX thread");
        } catch (...) {
            if (usb_ != INVALID_HANDLE_VALUE)
                CloseHandle(usb_);
            if (stop_)
                CloseHandle(stop_);
            DeleteCriticalSection(&mutex_);
            throw;
        }
    }
    ~Host() {
        SetEvent(stop_);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        CloseHandle(stop_);
        CloseHandle(usb_);
        for (size_t i = 0; i < connections_.size(); ++i)
            delete connections_[i];
        DeleteCriticalSection(&mutex_);
    }
    bool usable() {
        EnterCriticalSection(&mutex_);bool good=error_.empty();LeaveCriticalSection(&mutex_);
        WinXPlayUsbInfo current;DWORD n=0;
        return good && DeviceIoControl(usb_,WINXPLAY_USB_QUERY,0,0,&current,sizeof(current),&n,0) &&
            n==sizeof(current) && current.generation==info.generation;
    }
    SOCKET connect(unsigned destination) {
        require(destination > 0 && destination <= 65535, "Invalid USB destination");
        SOCKET listener = socket(AF_INET, SOCK_STREAM, 0), client = INVALID_SOCKET,
               server = INVALID_SOCKET;
        try {
            require(listener != INVALID_SOCKET, "Local channel listener");
            sockaddr_in address;
            ZeroMemory(&address, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            require(bind(listener, (sockaddr *)&address, sizeof(address)) == 0 &&
                        ::listen(listener, 1) == 0,
                    "Local channel bind");
            int length = sizeof(address);
            require(getsockname(listener, (sockaddr *)&address, &length) == 0,
                    "Local channel address");
            client = socket(AF_INET, SOCK_STREAM, 0);
            require(client != INVALID_SOCKET &&
                        ::connect(client, (sockaddr *)&address, sizeof(address)) == 0,
                    "Local channel connect");
            server = accept(listener, 0, 0);
            require(server != INVALID_SOCKET, "Local channel accept");
            closesocket(listener);
            listener = INVALID_SOCKET;
            u_long nonblock = 1;
            ioctlsocket(server, FIONBIO, &nonblock);
            DWORD timeout = 5000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
            Connection *c = new Connection;
            c->socket = server;
            c->destination = destination;
            EnterCriticalSection(&mutex_);
            for(size_t i=0;i<connections_.size();){
                if(connections_[i]->closed){delete connections_[i];connections_.erase(connections_.begin()+i);}
                else ++i;
            }
            bool valid = error_.empty() && connections_.size() < 8;
            if (valid) {
                c->source = nextPort_++;
                connections_.push_back(c);
            }
            LeaveCriticalSection(&mutex_);
            if (!valid) {
                delete c;
                server = INVALID_SOCKET;
                require(false, "USBMUX session unavailable or connection limit");
            }
            server = INVALID_SOCKET;
            muxEvent("MUX_CONNECT_REQUEST",c->source,destination,info.generation);
            return client;
        } catch (...) {
            if (listener != INVALID_SOCKET)
                closesocket(listener);
            if (client != INVALID_SOCKET)
                closesocket(client);
            if (server != INVALID_SOCKET)
                closesocket(server);
            throw;
        }
    }
};
// The receiver has one connection worker. Keep the USB transport for the
// attachment, while Lockdown/CarKit sockets come and go on automatic retries.
struct HostCache {
    Host *host;unsigned users;bool winsock;
    HostCache():host(0),users(0),winsock(false){}
    void clear(){require(!users,"USBMUX transport still in use");delete host;host=0;
        if(winsock){WSACleanup();winsock=false;}}
    ~HostCache(){delete host;if(winsock)WSACleanup();}
} hostCache;
static void sendAll(SOCKET s, const Bytes &b) {
    size_t p = 0;
    while (p < b.size()) {
        int n = send(s, (char *)&b[p], b.size() - p, 0);
        if(n<=0)socketFailure("USB service send",n,p,b.size());
        p += n;
    }
}
static Bytes receive(SOCKET s, size_t n) {
    require(n <= 1024 * 1024, "USB service receive limit");
    Bytes b(n);
    size_t p = 0;
    while (p < n) {
        int got = recv(s, (char *)&b[p], n - p, 0);
        if(got<=0)socketFailure("USB service read",got,p,n);
        p += got;
    }
    return b;
}
} // namespace
UsbMux::UsbMux(unsigned port) : native_(0), port_(port), tag_(0) {
    if(hostCache.host && !hostCache.host->usable())hostCache.clear();
    try {
        if(!hostCache.host){
            WSADATA w;require(WSAStartup(MAKEWORD(2,2),&w)==0,"Winsock initialization");
            hostCache.winsock=true;hostCache.host=new Host;
        }else diagnosticEvent("MUX_TRANSPORT_REUSED");
        native_=hostCache.host;++hostCache.users;
    } catch (...) {
        if(!hostCache.host)hostCache.clear();
        throw;
    }
}
UsbMux::~UsbMux() {
    if(native_)--hostCache.users;
}
SOCKET UsbMux::connectDevice(const AppleDevice &dev, unsigned port) {
    require(dev.id == ((Host *)native_)->info.generation, "Stale USB device generation");
    return ((Host *)native_)->connect(port);
}
Value UsbMux::deviceInfo(const AppleDevice &dev) {
    diagnosticEvent("DEVICE_ID_QUERY_BEGIN");
    SOCKET s = connectDevice(dev, 62078);
    try {
        Value request = Value::dictionary();
        request.dict["Request"] = Value::str("GetValue");
        request.dict["Key"] = Value::str("UniqueDeviceID");
        Bytes body = xmlPlist(request), head(4);
        put32(head, 0, body.size());
        head.insert(head.end(), body.begin(), body.end());
        sendAll(s, head);
        Value out = parsePlist(receive(s, be32(receive(s, 4), 0)));
        diagnosticEvent("DEVICE_ID_QUERY_RESPONSE",out.dict.count("Error")?out.at("Error").string:"no_error");
        closesocket(s);
        return out;
    } catch (...) {
        closesocket(s);
        throw;
    }
}
std::vector<AppleDevice> UsbMux::devices() {
    AppleDevice d;
    d.id = ((Host *)native_)->info.generation;
    d.connection = "USB";
    Value v = deviceInfo(d);
    require(v.at("Value").type == Value::String, "iPhone identity missing");
    d.serial = v.at("Value").string;
    recordNetworkState(d.id);
    return std::vector<AppleDevice>(1, d);
}
unsigned UsbMux::usbInterface() const {
    return ((Host *)native_)->info.interfaceNumber;
}
Value UsbMux::pairRecord(const AppleDevice &device) {
    class Channel : public PhonePairChannel {
        UsbMux &mux_;const AppleDevice &device_;
        SOCKET pairSocket_;
      public:
        Channel(UsbMux &m,const AppleDevice &d):mux_(m),device_(d),pairSocket_(INVALID_SOCKET){}
        ~Channel(){if(pairSocket_!=INVALID_SOCKET)closesocket(pairSocket_);}
        Value exchange(const Value &v){
            const std::string verb=v.at("Request").string;
            DWORD begin=GetTickCount();diagnosticEvent("PAIR_EXCHANGE_BEGIN",verb);
            // Keep SetValue(UntrustedHostBUID) and Pair on one Lockdown channel.
            // Earlier public-key reads still close before potentially slow RSA generation.
            SOCKET s=pairSocket_;pairSocket_=INVALID_SOCKET;
            if(s==INVALID_SOCKET)s=mux_.connectDevice(device_,62078);
            else diagnosticEvent("PAIR_CHANNEL_REUSED",verb);
            try{
                Bytes body=xmlPlist(v),head(4);put32(head,0,body.size());
                head.insert(head.end(),body.begin(),body.end());sendAll(s,head);
                if(verb=="Pair"){
                    const Value &record=v.at("PairRecord");std::ostringstream sent;
                    sent<<"bytes="<<body.size()<<" root_pem="<<record.at("RootCertificate").data.size()
                        <<" host_pem="<<record.at("HostCertificate").data.size()
                        <<" device_pem="<<record.at("DeviceCertificate").data.size();
                    diagnosticEvent("PAIR_REQUEST_SENT",sent.str());
                }
                Value result=parsePlist(receive(s,be32(receive(s,4),0)));
                std::ostringstream detail;detail<<verb<<" elapsed_ms="<<DWORD(GetTickCount()-begin);
                diagnosticEvent("PAIR_EXCHANGE_RECEIVED",detail.str());
                if(verb=="SetValue" && v.at("Key").string=="UntrustedHostBUID")pairSocket_=s;
                else closesocket(s);
                return result;
            }catch(const std::exception &e){
                std::ostringstream detail;detail<<verb<<" elapsed_ms="<<DWORD(GetTickCount()-begin)<<" "<<e.what();
                diagnosticEvent("PAIR_EXCHANGE_FAILED",detail.str());closesocket(s);throw;
            }
        }
    } channel(*this,device);
    static unsigned deniedGeneration=0;
    static std::string deniedDevice;
    require(deniedGeneration!=device.id || deniedDevice!=device.serial,
            "Pairing denied on iPhone; reconnect its USB cable to try again");
    try{return ensurePhonePair(channel,executableDirectory(),device.serial);}
    catch(const std::exception &e){
        if(std::string(e.what())=="Lockdown pairing: UserDeniedPairing"){
            deniedGeneration=device.id;deniedDevice=device.serial;
        }
        throw;
    }
}
Value prepareUsbCarPlay() {
    if(hostCache.host){
        if(hostCache.host->usable()){
            Value result=Value::dictionary();result.dict["mode_switch_requested"]=Value::flag(false);return result;
        }
        diagnosticEvent("MUX_TRANSPORT_RESET","unusable or detached");hostCache.clear();
    }
    HANDLE h = CreateFileW(L"CMX1:", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    recordUsbOpen(error);
    if (h == INVALID_HANDLE_VALUE) {
        std::ostringstream message;
        message << "Waiting for iPhone USB driver CMX1: open error=" << error;
        throw std::runtime_error(message.str());
    }
    WinXPlayUsbInfo info;
    DWORD n = 0;
    BOOL ok = DeviceIoControl(h, WINXPLAY_USB_QUERY, 0, 0, &info, sizeof(info), &n, 0);
    if (!ok || n != sizeof(info)) {
        muxEvent("USB_QUERY_FAILED",ok?0:GetLastError(),n);
        CloseHandle(h);
        require(false, "USB client query failed");
    }
    muxEvent("USB_GENERATION_CONFIG_NCM",info.generation,info.configuration,info.ncmPresent);
    if (!info.ncmPresent)
        ok = DeviceIoControl(h, WINXPLAY_USB_CARPLAY_MODE, 0, 0, 0, 0, &n, 0);
    CloseHandle(h);
    require(ok != FALSE, "CarPlay USB mode request failed");
    Value result = Value::dictionary();
    result.dict["mode_switch_requested"] = Value::flag(!info.ncmPresent);
    return result;
}
std::string receiverAddresses() {
    // Waiting now retains one CarKit session, so devices() is not called again.
    // Keep the existing rate-limited network evidence alive during that wait.
    recordNetworkState(0);
    std::wstring configured = environment(L"CARPLAY_ADDRESSES", L"");
    if (!configured.empty()) {
        diagnosticEvent("NETWORK_ADDRESSES","configured override");
        return utf8(configured);
    }
    SOCKET s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if(s==INVALID_SOCKET)muxEvent("NETWORK_IPV6_SOCKET_FAILED",WSAGetLastError());
    require(s != INVALID_SOCKET, "IPv6 unavailable");
    Bytes buffer(16384);
    DWORD n = 0;
    int rc = WSAIoctl(s, SIO_ADDRESS_LIST_QUERY, 0, 0, &buffer[0], buffer.size(), &n, 0, 0);
    if(rc!=0)muxEvent("NETWORK_ENUMERATION_FAILED",WSAGetLastError());
    closesocket(s);
    require(rc == 0, "IPv6 interface enumeration failed");
    SOCKET_ADDRESS_LIST *list = (SOCKET_ADDRESS_LIST *)&buffer[0];
    muxEvent("NETWORK_ADDRESS_COUNT",list->iAddressCount,n);
    std::string result;
    for (int i = 0; i < list->iAddressCount; ++i) {
        SOCKET_ADDRESS &a = list->Address[i];
        if (a.iSockaddrLength < int(sizeof(sockaddr_in6)) || a.lpSockaddr->sa_family != AF_INET6)
            continue;
        sockaddr_in6 *v = (sockaddr_in6 *)a.lpSockaddr;
        wchar_t raw[128];DWORD rawSize=128;
        if(WSAAddressToStringW(a.lpSockaddr,a.iSockaddrLength,0,raw,&rawSize)==0)
            diagnosticEvent("NETWORK_IPV6_ADDRESS",utf8(raw));
        if (v->sin6_addr.s6_addr[0] != 0xfe || (v->sin6_addr.s6_addr[1] & 0xc0) != 0x80)
            continue;
        wchar_t text[128];
        DWORD length = 128;
        if (WSAAddressToStringW(a.lpSockaddr, a.iSockaddrLength, 0, text, &length) == 0) {
            if (!result.empty())
                result += ';';
            result += utf8(text);
        }
    }
    require(!result.empty(), "Waiting for receiver USB network IPv6 link-local address");
    diagnosticEvent("NETWORK_LINK_LOCAL",result);
    return result;
}
} // namespace cp
