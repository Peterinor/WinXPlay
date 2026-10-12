// Shared desktop USB service adapter, included inside namespace cp.
static void sendAll(SOCKET s, const Bytes &b) {
    size_t p = 0;
    while (p < b.size()) {
        int n = send(s, (const char *)&b[p], int(b.size() - p), 0);
        require(n > 0, "USB service write failed");
        p += n;
    }
}
static Bytes receive(SOCKET s, size_t n) {
    require(n <= 4 * 1024 * 1024, "USB packet limit");
    Bytes b(n);
    size_t p = 0;
    while (p < n) {
        int count = recv(s, (char *)&b[p], int(n - p), 0);
        require(count > 0, "USB service disconnected or timed out");
        p += count;
    }
    return b;
}
UsbMux::UsbMux(unsigned port) : port_(port), tag_(0) {
    WSADATA w;
    require(port > 0 && port <= 65535 && WSAStartup(MAKEWORD(2, 2), &w) == 0,
            "Winsock initialization failed");
}
UsbMux::~UsbMux() {
    WSACleanup();
}
SOCKET UsbMux::connectService() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    require(s != INVALID_SOCKET, "Socket creation failed");
    sockaddr_in a;
    ZeroMemory(&a, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(U16(port_));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);
    int result = connect(s, (sockaddr *)&a, sizeof(a));
    if (result != 0) {
        fd_set writes, errors;
        FD_ZERO(&writes);
        FD_ZERO(&errors);
        FD_SET(s, &writes);
        FD_SET(s, &errors);
        timeval t = {3, 0};
        result = select(0, 0, &writes, &errors, &t);
        int err = 0, len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
        if (result <= 0 || err || FD_ISSET(s, &errors)) {
            closesocket(s);
            require(false, "Apple device service unavailable (localhost:27015)");
        }
    }
    nonblock = 0;
    ioctlsocket(s, FIONBIO, &nonblock);
    DWORD timeout = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    return s;
}
Value UsbMux::request(SOCKET s, const Value &message) {
    Value v = message;
    v.dict["ClientVersionString"] = Value::str("MRN native 0.1");
    v.dict["ProgName"] = Value::str("MRNCarPlay");
    v.dict["kLibUSBMuxVersion"] = Value::num(3);
    Bytes data = xmlPlist(v), head(16);
    put32(head, 0, U32(16 + data.size()), true);
    put32(head, 4, 1, true);
    put32(head, 8, 8, true);
    put32(head, 12, ++tag_, true);
    head.insert(head.end(), data.begin(), data.end());
    sendAll(s, head);
    head = receive(s, 16);
    U32 length = le32(head, 0);
    require(length >= 16 && length <= 4 * 1024 * 1024 && le32(head, 4) == 1 && le32(head, 8) == 8 &&
                le32(head, 12) == tag_,
            "Invalid usbmux header");
    return parsePlist(receive(s, length - 16));
}
std::vector<AppleDevice> UsbMux::devices() {
    SOCKET s = connectService();
    try {
        Value v = Value::dictionary();
        v.dict["MessageType"] = Value::str("ListDevices");
        Value r = request(s, v);
        const Value &list = r.at("DeviceList");
        require(list.type == Value::Array, "Invalid device list");
        std::vector<AppleDevice> out;
        for (size_t i = 0; i < list.array.size(); i++) {
            const Value &d = list.array[i];
            const Value &p = d.at("Properties");
            AppleDevice dev;
            require(d.at("DeviceID").type == Value::Integer &&
                        d.at("DeviceID").integer <= 0xffffffff,
                    "Invalid device identifier");
            dev.id = unsigned(d.at("DeviceID").integer);
            require(p.at("SerialNumber").type == Value::String &&
                        p.at("ConnectionType").type == Value::String,
                    "Invalid device properties");
            dev.serial = p.at("SerialNumber").string;
            dev.connection = p.at("ConnectionType").string;
            out.push_back(dev);
        }
        closesocket(s);
        return out;
    } catch (...) {
        closesocket(s);
        throw;
    }
}
SOCKET UsbMux::connectDevice(const AppleDevice &dev, unsigned port) {
    require(port > 0 && port <= 65535, "Invalid destination port");
    SOCKET s = connectService();
    try {
        Value v = Value::dictionary();
        v.dict["MessageType"] = Value::str("Connect");
        v.dict["DeviceID"] = Value::num(dev.id);
        v.dict["PortNumber"] = Value::num(htons(U16(port)));
        Value result = request(s, v);
        require(result.at("Number").type == Value::Integer && result.at("Number").integer == 0,
                "Device refused USB connection");
        return s;
    } catch (...) {
        closesocket(s);
        throw;
    }
}
Value UsbMux::deviceInfo(const AppleDevice &device) {
    SOCKET s = connectDevice(device, 62078);
    try {
        Value request = Value::dictionary();
        request.dict["Label"] = Value::str("MRNCarPlay");
        request.dict["Request"] = Value::str("GetValue");
        request.dict["Key"] = Value::str("ProductVersion");
        Bytes body = xmlPlist(request), head(4);
        put32(head, 0, U32(body.size()));
        head.insert(head.end(), body.begin(), body.end());
        sendAll(s, head);
        head = receive(s, 4);
        U32 n = be32(head, 0);
        require(n > 0 && n <= 1024 * 1024, "Invalid Lockdown plist length");
        Value result = parsePlist(receive(s, n));
        closesocket(s);
        return result;
    } catch (...) {
        closesocket(s);
        throw;
    }
}
Value UsbMux::pairRecord(const AppleDevice &device) {
    SOCKET socket = connectService();
    try {
        Value message = Value::dictionary();
        message.dict["MessageType"] = Value::str("ReadPairRecord");
        message.dict["PairRecordID"] = Value::str(device.serial);
        Value result = request(socket, message);
        require(result.dict.count("PairRecordData") != 0,
                "Windows pairing record unavailable; unlock iPhone and trust this computer");
        require(result.at("PairRecordData").type == Value::Data, "Invalid pairing record type");
        Value record = parsePlist(result.at("PairRecordData").data);
        closesocket(socket);
        return record;
    } catch (...) {
        closesocket(socket);
        throw;
    }
}
