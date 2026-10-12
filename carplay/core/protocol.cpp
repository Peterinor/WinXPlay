// Wire layouts reference DiPlay 4ce354d5 (see the project root THIRD_PARTY.md).
#include "protocol.h"
#include <algorithm>
#include <sstream>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <cctype>
namespace cp {
Bytes join(const Bytes &a, const Bytes &b) {
    Bytes r = a;
    r.insert(r.end(), b.begin(), b.end());
    return r;
}
void eraseSecret(Bytes &b) {
    if (b.empty())
        return;
    volatile U8 *p = &b[0];
    for (size_t i = 0; i < b.size(); ++i)
        p[i] = 0;
}

void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
static void range(const Bytes &b, size_t p, size_t n) {
    require(p <= b.size() && n <= b.size() - p, "Truncated packet");
}
U16 be16(const Bytes &b, size_t p) {
    range(b, p, 2);
    return U16((b[p] << 8) | b[p + 1]);
}
U32 be32(const Bytes &b, size_t p) {
    range(b, p, 4);
    return (U32(b[p]) << 24) | (U32(b[p + 1]) << 16) | (U32(b[p + 2]) << 8) | b[p + 3];
}
U16 le16(const Bytes &b, size_t p) {
    range(b, p, 2);
    return U16(b[p] | (b[p + 1] << 8));
}
U32 le32(const Bytes &b, size_t p) {
    range(b, p, 4);
    return b[p] | (U32(b[p + 1]) << 8) | (U32(b[p + 2]) << 16) | (U32(b[p + 3]) << 24);
}
void put16(Bytes &b, size_t p, U16 n, bool l) {
    range(b, p, 2);
    b[p] = U8(l ? n : n >> 8);
    b[p + 1] = U8(l ? n >> 8 : n);
}
void put32(Bytes &b, size_t p, U32 n, bool l) {
    range(b, p, 4);
    for (int i = 0; i < 4; i++)
        b[p + i] = U8(n >> (8 * (l ? i : 3 - i)));
}
Bytes slice(const Bytes &b, size_t p, size_t n) {
    range(b, p, n);
    return Bytes(b.begin() + p, b.begin() + p + n);
}
Bytes bytes(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
std::string text(const Bytes &b) {
    return std::string(b.begin(), b.end());
}
std::string hex(const Bytes &b) {
    std::string s;
    const char *h = "0123456789abcdef";
    for (size_t i = 0; i < b.size(); i++) {
        s += h[b[i] >> 4];
        s += h[b[i] & 15];
    }
    return s;
}
Bytes unhex(const std::string &s) {
    require(s.size() % 2 == 0, "Odd hex length");
    Bytes b;
    for (size_t i = 0; i < s.size(); i += 2) {
        unsigned n = 0;
        for (int j = 0; j < 2; j++) {
            unsigned char c = s[i + j];
            require(isxdigit(c) != 0, "Invalid hex");
            n = n * 16 + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
        }
        b.push_back(U8(n));
    }
    return b;
}
static void append(Bytes &a, const Bytes &b) {
    a.insert(a.end(), b.begin(), b.end());
}
Bytes csm(U16 id, const Bytes &body) {
    require(body.size() <= 65529, "CSM too large");
    Bytes b(6);
    put16(b, 0, 0x4040);
    put16(b, 2, U16(6 + body.size()));
    put16(b, 4, id);
    append(b, body);
    return b;
}
Bytes parameter(U16 id, const Bytes &body) {
    require(body.size() <= 65531, "Parameter too large");
    Bytes b(4);
    put16(b, 0, U16(4 + body.size()));
    put16(b, 2, id);
    append(b, body);
    return b;
}
std::vector<Frame> parameters(const Bytes &b) {
    std::vector<Frame> out;
    for (size_t p = 0; p < b.size();) {
        U16 n = be16(b, p);
        require(n >= 4, "Invalid parameter length");
        out.push_back(Frame(be16(b, p + 2), slice(b, p + 4, n - 4)));
        p += n;
    }
    return out;
}
std::vector<Frame> CsmParser::feed(const Bytes &chunk) {
    std::vector<Frame> out;
    require(chunk.size() <= 4 * 1024 * 1024, "Chunk too large");
    for (size_t i = 0; i < chunk.size(); i++) {
        pending_.push_back(chunk[i]);
        while (pending_.size() >= 2 && (pending_[0] != 0x40 || pending_[1] != 0x40))
            pending_.erase(pending_.begin());
        if (pending_.size() < 6)
            continue;
        size_t n = be16(pending_, 2);
        if (n < 6) {
            pending_.erase(pending_.begin());
            continue;
        }
        if (pending_.size() == n) {
            out.push_back(Frame(be16(pending_, 4), slice(pending_, 6, n - 6)));
            pending_.clear();
        }
    }
    return out;
}
static U8 checksum(const Bytes &b, size_t start, size_t end) {
    unsigned n = 0;
    for (size_t i = start; i < end; i++)
        n += b[i];
    return U8(0 - n);
}
Bytes linkFrame(const LinkFrame &f) {
    require(f.body.size() <= 65525, "Link frame too large");
    Bytes b(9);
    b[0] = 0xff;
    b[1] = 0x5a;
    put16(b, 2, U16(9 + f.body.size() + (f.body.empty() ? 0 : 1)));
    b[4] = f.control;
    b[5] = f.sequence;
    b[6] = f.ack;
    b[7] = f.session;
    b[8] = checksum(b, 0, 8);
    if (!f.body.empty()) {
        append(b, f.body);
        b.push_back(checksum(f.body, 0, f.body.size()));
    }
    return b;
}
LinkFrame parseLink(const Bytes &b) {
    require(b.size() >= 9 && b.size() <= 65535, "Invalid link length");
    require(b[0] == 0xff && b[1] == 0x5a && be16(b, 2) == b.size(), "Invalid link header");
    require(checksum(b, 0, 9) == 0, "Invalid link header checksum");
    LinkFrame f;
    f.control = b[4];
    f.sequence = b[5];
    f.ack = b[6];
    f.session = b[7];
    if (b.size() > 9) {
        require(b.size() >= 11 && checksum(b, 9, b.size()) == 0, "Invalid link payload checksum");
        f.body = slice(b, 9, b.size() - 10);
    }
    return f;
}
Bytes ntb16(const Bytes &frame, U16 seq) {
    require(!frame.empty() && frame.size() <= 65507, "Invalid Ethernet frame size");
    size_t n = frame.size() + 28;
    Bytes b(n + (n % 512 == 0 ? 1 : 0), 0);
    put32(b, 0, 0x484d434e, true);
    put16(b, 4, 12, true);
    put16(b, 6, seq, true);
    put16(b, 8, U16(n), true);
    put16(b, 10, 12, true);
    put32(b, 12, 0x304d434e, true);
    put16(b, 16, 16, true);
    put16(b, 20, 28, true);
    put16(b, 22, U16(frame.size()), true);
    std::copy(frame.begin(), frame.end(), b.begin() + 28);
    return b;
}
std::vector<Bytes> parseNtb16(const Bytes &b) {
    require(b.size() >= 28 && le32(b, 0) == 0x484d434e && le16(b, 4) == 12, "Invalid NTB16 header");
    size_t n = le16(b, 8);
    require(n >= 28 && n <= b.size() &&
                (b.size() == n || (n % 512 == 0 && b.size() == n + 1 && b[n] == 0)),
            "Invalid NTB16 block length");
    std::vector<Bytes> out;
    std::vector<size_t> visited;
    size_t expandedBytes = 0;
    size_t p = le16(b, 10);
    while (p) {
        require(p >= 12 && p <= n - 12 &&
                    std::find(visited.begin(), visited.end(), p) == visited.end(),
                "Invalid or cyclic NDP pointer");
        visited.push_back(p);
        require(le32(b, p) == 0x304d434e, "Unsupported NDP signature/CRC");
        size_t len = le16(b, p + 4);
        require(len >= 12 && len % 4 == 0 && len <= n - p, "Invalid NDP length");
        bool end = false;
        for (size_t e = p + 8; e < p + len; e += 4) {
            size_t at = le16(b, e), count = le16(b, e + 2);
            if (!at && !count) {
                end = true;
                break;
            }
            require(at >= 12 && count > 0 && at <= n && count <= n - at, "Invalid NTB datagram");
            require(at + count <= p || at >= p + len, "Datagram overlaps NDP");
            require(count <= n - expandedBytes, "NCM datagram expansion budget exceeded");
            expandedBytes += count;
            out.push_back(slice(b, at, count));
        }
        require(end, "Missing NDP terminator");
        p = le16(b, p + 6);
    }
    require(!out.empty(), "Empty NTB");
    return out;
}
Bytes tlv8(U8 type, const Bytes &value) {
    require(value.size() <= 65535, "TLV too large");
    Bytes b;
    size_t p = 0;
    do {
        size_t n = std::min(size_t(255), value.size() - p);
        b.push_back(type);
        b.push_back(U8(n));
        append(b, slice(value, p, n));
        p += n;
    } while (p < value.size());
    return b;
}
std::map<U8, Bytes> parseTlv8(const Bytes &b) {
    require(b.size() <= 131072, "TLV input too large");
    std::map<U8, Bytes> out;
    for (size_t p = 0; p < b.size();) {
        range(b, p, 2);
        U8 type = b[p], n = b[p + 1];
        append(out[type], slice(b, p + 2, n));
        p += 2 + n;
    }
    return out;
}
Bytes touch(double x, double y, bool down, unsigned w, unsigned h) {
    require(w > 0 && h > 0 && w <= 65535 && h <= 65535 && x == x && y == y,
            "Invalid touch geometry");
    x = std::max(0.0, std::min(1.0, x));
    y = std::max(0.0, std::min(1.0, y));
    Bytes b(12, 0);
    b[1] = down ? 1 : 0;
    b[6] = 1;
    put16(b, 2, U16(floor(x * w + 0.5)), true);
    put16(b, 4, U16(floor(y * h + 0.5)), true);
    return b;
}
Bytes knob(bool select, bool home, bool back, int x, int y, int wheel) {
    Bytes b(4);
    b[0] = U8((select ? 1 : 0) | (home ? 2 : 0) | (back ? 4 : 0));
    b[1] = U8(std::max(-127, std::min(127, x)));
    b[2] = U8(std::max(-127, std::min(127, y)));
    b[3] = U8(std::max(-127, std::min(127, wheel)));
    return b;
}
Bytes avccToAnnexB(const Bytes &b) {
    require(b.size() >= 7 && b[0] == 1 && (b[4] & 3) == 3, "Invalid avcC");
    Bytes out;
    size_t p = 6;
    unsigned count = b[5] & 31;
    require(count > 0, "Missing SPS");
    for (int group = 0; group < 2; group++) {
        for (unsigned i = 0; i < count; i++) {
            U16 n = be16(b, p);
            p += 2;
            require(n > 0, "Empty parameter set");
            append(out, unhex("00000001"));
            append(out, slice(b, p, n));
            p += n;
        }
        if (group == 0) {
            range(b, p, 1);
            count = b[p++];
            require(count > 0, "Missing PPS");
        }
    }
    return out;
}
Bytes nalToAnnexB(const Bytes &b) {
    require(b.size() <= 4 * 1024 * 1024, "NAL payload too large");
    Bytes out;
    for (size_t p = 0; p < b.size();) {
        U32 n = be32(b, p);
        p += 4;
        require(n > 0, "Empty NAL");
        append(out, unhex("00000001"));
        append(out, slice(b, p, n));
        p += n;
    }
    return out;
}
Bytes pcmSwap(const Bytes &b) {
    require(b.size() % 2 == 0, "Unaligned PCM16");
    Bytes out = b;
    for (size_t i = 0; i < b.size(); i += 2)
        std::swap(out[i], out[i + 1]);
    return out;
}
static std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); i++)
        s[i] = char(tolower((unsigned char)s[i]));
    return s;
}
static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::vector<Request> RtspParser::feed(const Bytes &b) {
    const size_t max = 4 * 1024 * 1024;
    require(b.size() <= max && pending_.size() <= max - b.size(), "RTSP buffer limit");
    append(pending_, b);
    std::vector<Request> out;
    while (!pending_.empty()) {
        std::string all = text(pending_);
        size_t end = all.find("\r\n\r\n");
        if (end == std::string::npos) {
            require(all.size() <= 32768, "RTSP header limit");
            break;
        }
        require(end <= 32768, "RTSP header limit");
        std::istringstream lines(all.substr(0, end));
        std::string line;
        std::getline(lines, line);
        std::istringstream first(trim(line));
        Request r;
        std::string extra;
        require(bool(first >> r.method >> r.path), "Invalid RTSP start line");
        if (r.method == "RTSP/1.0" || r.method == "HTTP/1.1") {
            require(r.path.size() == 3 &&
                        r.path.find_first_not_of("0123456789") == std::string::npos,
                    "Invalid RTSP response status");
            std::getline(first, r.protocol);
            r.protocol = trim(r.protocol);
        } else {
            require(bool(first >> r.protocol) && !(first >> extra), "Invalid RTSP request line");
            require(r.protocol == "RTSP/1.0" || r.protocol == "HTTP/1.1",
                    "Unsupported RTSP protocol");
        }
        size_t length = 0;
        while (std::getline(lines, line)) {
            size_t colon = line.find(':');
            require(colon != std::string::npos, "Invalid header");
            std::string key = lower(trim(line.substr(0, colon))),
                        val = trim(line.substr(colon + 1));
            require(!key.empty() && !r.headers.count(key), "Duplicate header");
            r.headers[key] = val;
        }
        if (r.headers.count("content-length")) {
            std::string s = r.headers["content-length"];
            require(!s.empty() && s.size() <= 8, "Invalid Content-Length");
            for (size_t i = 0; i < s.size(); i++) {
                require(s[i] >= '0' && s[i] <= '9', "Invalid Content-Length");
                length = length * 10 + (s[i] - '0');
            }
            require(length <= max - 32772, "RTSP body limit");
        }
        size_t total = end + 4 + length;
        if (total > pending_.size())
            break;
        r.body = slice(pending_, end + 4, length);
        out.push_back(r);
        pending_.erase(pending_.begin(), pending_.begin() + total);
    }
    return out;
}
Bytes response(const Request &r, unsigned status, const Bytes &body, const std::string &type) {
    std::ostringstream s;
    s << r.protocol << " " << status << " "
      << (status == 200   ? "OK"
          : status == 403 ? "Forbidden"
                          : "Bad Request")
      << "\r\n";
    std::map<std::string, std::string>::const_iterator i = r.headers.find("cseq");
    if (i != r.headers.end())
        s << "CSeq: " << i->second << "\r\n";
    s << "Content-Type: " << type << "\r\nContent-Length: " << body.size() << "\r\n\r\n";
    Bytes b = bytes(s.str());
    append(b, body);
    return b;
}
Bytes nonce64(U64 n) {
    Bytes b(12, 0);
    for (int i = 0; i < 8; i++)
        b[4 + i] = U8(n >> (8 * i));
    return b;
}
ControlCipher::ControlCipher(Crypto &c, const Bytes &r, const Bytes &w)
    : crypto_(c), readKey_(r), writeKey_(w), readCounter_(0), writeCounter_(0), failed_(false) {
    require(r.size() == 32 && w.size() == 32, "Invalid control keys");
}
Bytes ControlCipher::encrypt(const Bytes &plain) {
    require(!failed_ && plain.size() <= 4 * 1024 * 1024, "Invalid cipher state/size");
    Bytes out;
    size_t p = 0;
    do {
        size_t n = std::min(size_t(16384), plain.size() - p);
        require(writeCounter_ != (std::numeric_limits<U64>::max)(), "Nonce exhausted");
        Bytes head(2);
        put16(head, 0, U16(n), true);
        Bytes sealed = crypto_.seal(writeKey_, nonce64(writeCounter_), slice(plain, p, n), head);
        ++writeCounter_;
        append(out, head);
        append(out, sealed);
        p += n;
    } while (p < plain.size());
    return out;
}
Bytes ControlCipher::feed(const Bytes &b) {
    require(!failed_ && b.size() <= 4 * 1024 * 1024 &&
                pending_.size() <= 4 * 1024 * 1024 - b.size(),
            "Invalid cipher buffer");
    append(pending_, b);
    Bytes out;
    try {
        while (pending_.size() >= 2) {
            size_t n = le16(pending_, 0);
            require(n <= 16384, "Oversized encrypted control frame");
            if (pending_.size() < n + 18)
                break;
            require(readCounter_ != (std::numeric_limits<U64>::max)(), "Nonce exhausted");
            Bytes p = crypto_.open(readKey_, nonce64(readCounter_), slice(pending_, 2, n + 16),
                                   slice(pending_, 0, 2));
            ++readCounter_;
            append(out, p);
            pending_.erase(pending_.begin(), pending_.begin() + n + 18);
        }
    } catch (...) {
        failed_ = true;
        pending_.clear();
        throw;
    }
    return out;
}
std::vector<VideoPacket> ScreenParser::feed(const Bytes &chunk) {
    require(!failed_ && chunk.size() <= 4 * 1024 * 1024 + 128 &&
                pending_.size() <= 8 * 1024 * 1024 - chunk.size(),
            "Screen buffer limit or failed stream");
    append(pending_, chunk);
    std::vector<VideoPacket> out;
    while (pending_.size() >= 128) {
        size_t n = le32(pending_, 0);
        require(n <= 4 * 1024 * 1024, "Screen frame limit");
        if (pending_.size() < 128 + n)
            break;
        VideoPacket p;
        p.type = pending_[4];
        Bytes body = slice(pending_, 128, n);
        if (p.type == 0) {
            require(n >= 16, "Short video ciphertext");
            try {
                p.body = nalToAnnexB(
                    crypto_.open(key_, nonce64(counter_), body, slice(pending_, 0, 128)));
                ++counter_;
            } catch (...) {
                failed_ = true;
                pending_.clear();
                throw;
            }
        } else if (p.type == 1)
            p.body = body;
        // iPhone sends auxiliary screen messages between frames. They are bounded by
        // the same header length, but do not consume a video encryption nonce.
        if (p.type == 0 || p.type == 1)
            out.push_back(p);
        pending_.erase(pending_.begin(), pending_.begin() + 128 + n);
    }
    return out;
}
Bytes decryptAudio(Crypto &c, const Bytes &key, const Bytes &p) {
    require(p.size() >= 36 && p.size() <= 65535 && (p[0] & 0xff) == 0x80, "Invalid audio packet");
    Bytes nonce(12, 0);
    std::copy(p.end() - 8, p.end(), nonce.begin() + 4);
    return c.open(key, nonce, slice(p, 12, p.size() - 20), slice(p, 4, 8));
}
Bytes microphonePacket(Crypto &c, const Bytes &key, const Bytes &pcm, U16 sequence, U32 timestamp,
                       U64 counter, U8 type) {
    Bytes h(12, 0);
    h[0] = 0x80;
    h[1] = type & 127;
    put16(h, 2, sequence);
    put32(h, 4, timestamp);
    Bytes nonce = nonce64(counter);
    Bytes body = c.seal(key, nonce, pcmSwap(pcm), slice(h, 4, 8));
    append(h, body);
    append(h, slice(nonce, 4, 8));
    return h;
}
unsigned Session::attach() {
    ++generation_;
    state_ = UsbAvailable;
    error_.clear();
    return generation_;
}
void Session::trusted(unsigned g) {
    if (g != generation_)
        return;
    require(state_ == UsbAvailable, "Invalid trust transition");
    state_ = Trusted;
}
void Session::beginAuth(unsigned g) {
    if (g != generation_)
        return;
    require(state_ == Trusted, "Invalid auth transition");
    state_ = Authenticating;
}
void Session::authenticated(unsigned g) {
    if (g != generation_)
        return;
    require(state_ == Authenticating, "Authentication not started");
    state_ = Authenticated;
}
void Session::video(unsigned g) {
    if (g != generation_)
        return;
    require(state_ == Authenticated || state_ == Streaming, "Unauthenticated video");
    state_ = Streaming;
}
void Session::disconnect() {
    ++generation_;
    state_ = Disconnected;
    error_.clear();
}
void Session::fail(unsigned g, const std::string &reason) {
    if (g != generation_)
        return;
    state_ = Faulted;
    error_ = reason;
}
} // namespace cp
