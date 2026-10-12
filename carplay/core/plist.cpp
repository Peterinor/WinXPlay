#include "protocol.h"
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <set>
namespace cp {
Value Value::str(const std::string &s) {
    Value v;
    v.type = String;
    v.string = s;
    return v;
}
Value Value::num(U64 n) {
    Value v;
    v.type = Integer;
    v.integer = n;
    return v;
}
Value Value::blob(const Bytes &b) {
    Value v;
    v.type = Data;
    v.data = b;
    return v;
}
Value Value::flag(bool b) {
    Value v;
    v.type = Boolean;
    v.boolean = b;
    return v;
}
Value Value::dictionary() {
    Value v;
    v.type = Dict;
    return v;
}
Value Value::list() {
    Value v;
    v.type = Array;
    return v;
}
const Value &Value::at(const std::string &k) const {
    require(type == Dict, "Expected dictionary");
    std::map<std::string, Value>::const_iterator i = dict.find(k);
    require(i != dict.end(), "Missing plist key");
    return i->second;
}
static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64(const Bytes &b) {
    std::string s;
    for (size_t p = 0; p < b.size(); p += 3) {
        U32 n = U32(b[p]) << 16;
        if (p + 1 < b.size())
            n |= U32(b[p + 1]) << 8;
        if (p + 2 < b.size())
            n |= b[p + 2];
        s += alphabet[n >> 18];
        s += alphabet[(n >> 12) & 63];
        s += p + 1 < b.size() ? alphabet[(n >> 6) & 63] : '=';
        s += p + 2 < b.size() ? alphabet[n & 63] : '=';
    }
    return s;
}
static Bytes unbase64(const std::string &str) {
    std::string s;
    for (size_t i = 0; i < str.size(); i++)
        if (str[i] != ' ' && str[i] != '\r' && str[i] != '\n' && str[i] != '\t')
            s += str[i];
    require(s.size() % 4 == 0, "Invalid base64 length");
    Bytes out;
    for (size_t p = 0; p < s.size(); p += 4) {
        unsigned n = 0, pad = 0;
        for (int j = 0; j < 4; j++) {
            if (s[p + j] == '=') {
                require(j >= 2 && p + 4 == s.size(), "Invalid base64 padding");
                ++pad;
                n <<= 6;
            } else {
                require(pad == 0, "Invalid base64 padding order");
                const char *q = strchr(alphabet, s[p + j]);
                require(q != 0 && s[p + j] != 0, "Invalid base64");
                n = (n << 6) | unsigned(q - alphabet);
            }
        }
        require(pad <= 2, "Invalid base64 padding");
        out.push_back(U8(n >> 16));
        if (pad < 2)
            out.push_back(U8(n >> 8));
        if (!pad)
            out.push_back(U8(n));
    }
    return out;
}
static std::string escape(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        switch (s[i]) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += s[i];
        }
    }
    return out;
}
static std::string unescape(const std::string &s) {
    std::string out;
    for (size_t p = 0; p < s.size(); p++) {
        if (s[p] != '&') {
            out += s[p];
            continue;
        }
        size_t end = s.find(';', p);
        require(end != std::string::npos, "Invalid XML entity");
        std::string e = s.substr(p, end - p + 1);
        if (e == "&amp;")
            out += '&';
        else if (e == "&lt;")
            out += '<';
        else if (e == "&gt;")
            out += '>';
        else if (e == "&quot;")
            out += '"';
        else if (e == "&apos;")
            out += '\'';
        else
            require(false, "Unsupported XML entity");
        p = end;
    }
    return out;
}
static void writeXml(std::ostringstream &out, const Value &v, unsigned depth) {
    require(depth <= 32, "Plist depth limit");
    switch (v.type) {
    case Value::Boolean:
        out << (v.boolean ? "<true/>" : "<false/>");
        break;
    case Value::Integer:
        out << "<integer>";
        if (v.negative)
            out << '-' << (U64(0) - v.integer);
        else
            out << v.integer;
        out << "</integer>";
        break;
    case Value::Real:
        out << "<real>" << v.real << "</real>";
        break;
    case Value::String:
        out << "<string>" << escape(v.string) << "</string>";
        break;
    case Value::Data:
        out << "<data>" << base64(v.data) << "</data>";
        break;
    case Value::Array:
        out << "<array>";
        for (size_t i = 0; i < v.array.size(); i++)
            writeXml(out, v.array[i], depth + 1);
        out << "</array>";
        break;
    case Value::Dict:
        out << "<dict>";
        for (std::map<std::string, Value>::const_iterator i = v.dict.begin(); i != v.dict.end();
             ++i) {
            out << "<key>" << escape(i->first) << "</key>";
            writeXml(out, i->second, depth + 1);
        }
        out << "</dict>";
        break;
    default:
        require(false, "XML null unsupported");
    }
}
Bytes xmlPlist(const Value &v) {
    std::ostringstream s;
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\">";
    writeXml(s, v, 0);
    s << "</plist>";
    require(s.str().size() <= 4 * 1024 * 1024, "Plist size limit");
    return bytes(s.str());
}
class XmlReader {
    std::string s;
    size_t p;
    unsigned objects;
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\r' || s[p] == '\n' || s[p] == '\t'))
            ++p;
    }
    bool starts(const char *t) {
        ws();
        return s.compare(p, strlen(t), t) == 0;
    }
    std::string tag() {
        ws();
        require(p < s.size() && s[p] == '<', "Expected XML tag");
        size_t e = s.find('>', p);
        require(e != std::string::npos && e - p <= 256, "Invalid XML tag");
        std::string t = s.substr(p + 1, e - p - 1);
        p = e + 1;
        return t;
    }
    std::string content(const std::string &name) {
        size_t e = s.find('<', p);
        require(e != std::string::npos, "Missing XML closing tag");
        std::string result = unescape(s.substr(p, e - p));
        p = e;
        require(tag() == "/" + name, "Mismatched XML closing tag");
        return result;
    }
    Value value(unsigned depth) {
        require(depth <= 32 && ++objects <= 65536, "Plist complexity limit");
        std::string t = tag();
        if (t == "true/" || t == "true /")
            return Value::flag(true);
        if (t == "false/" || t == "false /")
            return Value::flag(false);
        if (t == "dict/" || t == "dict /")
            return Value::dictionary();
        if (t == "array/" || t == "array /")
            return Value::list();
        if (t == "string/" || t == "string /")
            return Value::str("");
        if (t == "data/" || t == "data /")
            return Value::blob(Bytes());
        if (t == "string")
            return Value::str(content(t));
        if (t == "data")
            return Value::blob(unbase64(content(t)));
        if (t == "integer") {
            std::string n = content(t);
            require(!n.empty() && n.size() <= 20, "Invalid XML integer");
            bool negative = n[0] == '-';
            require(!negative || n.size() > 1, "Invalid XML integer");
            U64 v = 0;
            for (size_t i = negative ? 1 : 0; i < n.size(); i++) {
                require(n[i] >= '0' && n[i] <= '9', "Invalid XML integer");
                U64 next = v * 10 + (n[i] - '0');
                require(next >= v && next / 10 == v, "XML integer overflow");
                v = next;
            }
            require(!negative || v <= 0x8000000000000000ULL, "Signed XML integer overflow");
            Value result = Value::num(negative ? U64(0) - v : v);
            result.negative = negative;
            return result;
        }
        if (t == "dict") {
            Value v = Value::dictionary();
            while (!starts("</dict>")) {
                require(tag() == "key", "Expected plist key");
                std::string key = content("key");
                require(!v.dict.count(key), "Duplicate plist key");
                v.dict[key] = value(depth + 1);
            }
            tag();
            return v;
        }
        if (t == "array") {
            Value v = Value::list();
            while (!starts("</array>"))
                v.array.push_back(value(depth + 1));
            tag();
            return v;
        }
        require(false, "Unsupported XML value");
        return Value();
    }

  public:
    XmlReader(const Bytes &b) : s(text(b)), p(0), objects(0) {}
    Value read() {
        ws();
        if (starts("<?xml")) {
            size_t e = s.find("?>", p);
            require(e != std::string::npos, "Invalid XML declaration");
            p = e + 2;
        }
        if (starts("<!DOCTYPE")) {
            size_t e = s.find('>', p);
            require(e != std::string::npos && s.substr(p, e - p).find('[') == std::string::npos,
                    "XML entities forbidden");
            p = e + 1;
        }
        std::string t = tag();
        require(t == "plist version=\"1.0\"" || t == "plist", "Missing plist root");
        Value v = value(0);
        require(tag() == "/plist", "Missing plist end");
        ws();
        require(p == s.size(), "Trailing plist data");
        return v;
    }
};
static U64 readInt(const Bytes &b, size_t p, size_t n) {
    require(n >= 1 && n <= 8 && p <= b.size() && n <= b.size() - p, "Invalid plist integer span");
    U64 v = 0;
    for (size_t i = 0; i < n; i++)
        v = (v << 8) | b[p + i];
    return v;
}
static void putInt(Bytes &b, U64 n, size_t width) {
    for (size_t i = 0; i < width; i++)
        b.push_back(U8(n >> (8 * (width - i - 1))));
}
static void utf8(std::string &s, unsigned c) {
    require(c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff), "Invalid Unicode code point");
    if (c < 128)
        s += char(c);
    else if (c < 2048) {
        s += char(0xc0 | (c >> 6));
        s += char(0x80 | (c & 63));
    } else if (c < 65536) {
        s += char(0xe0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    } else {
        s += char(0xf0 | (c >> 18));
        s += char(0x80 | ((c >> 12) & 63));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    }
}
class BinaryReader {
    const Bytes &b;
    size_t table, refs;
    std::vector<size_t> offsets;
    std::set<size_t> visiting;
    size_t expanded, materialized;
    BinaryReader(const BinaryReader &);
    BinaryReader &operator=(const BinaryReader &);
    size_t count(size_t &p, U8 marker) {
        size_t n = marker & 15;
        if (n == 15) {
            require(p < table && (b[p] >> 4) == 1 && (b[p] & 15) <= 3, "Invalid extended count");
            size_t width = size_t(1) << (b[p++] & 15);
            U64 large = readInt(b, p, width);
            p += width;
            require(large <= 65536, "Plist count limit");
            n = size_t(large);
        }
        return n;
    }
    void span(size_t p, size_t n) {
        require(p >= 8 && p <= table && n <= table - p, "Plist object outside object table");
    }
    Value object(size_t index, unsigned depth) {
        require(index < offsets.size() && depth <= 32 && ++expanded <= 65536,
                "Plist complexity limit");
        require(!visiting.count(index), "Cyclic plist");
        visiting.insert(index);
        size_t p = offsets[index];
        span(p, 1);
        U8 marker = b[p++], type = marker >> 4;
        Value v;
        if (marker == 0)
            v = Value();
        else if (marker == 8 || marker == 9)
            v = Value::flag(marker == 9);
        else if (type == 1) {
            require((marker & 15) <= 4, "Integer too wide");
            size_t n = size_t(1) << (marker & 15);
            span(p, n);
            if (n == 16) {
                require(readInt(b, p, 8) == 0, "Integer exceeds uint64");
                v = Value::num(readInt(b, p + 8, 8));
            } else {
                v = Value::num(readInt(b, p, n));
                v.negative = n == 8 && (v.integer >> 63) != 0;
            }
        } else if (type == 2) {
            require(marker == 0x22 || marker == 0x23, "Invalid real");
            size_t n = marker == 0x22 ? 4 : 8;
            span(p, n);
            U64 raw = readInt(b, p, n);
            v.type = Value::Real;
            if (n == 4) {
                U32 bits = U32(raw);
                float f;
                memcpy(&f, &bits, 4);
                v.real = f;
            } else
                memcpy(&v.real, &raw, 8);
        } else if (type == 4 || type == 5 || type == 6) {
            size_t n = count(p, marker);
            require(n <= (8 * 1024 * 1024 - materialized) / 3, "Plist expansion budget exceeded");
            materialized += n * 3;
            span(p, n * (type == 6 ? 2 : 1));
            if (type == 4)
                v = Value::blob(slice(b, p, n));
            else if (type == 5)
                v = Value::str(text(slice(b, p, n)));
            else {
                std::string s;
                for (size_t j = 0; j < n; j++) {
                    unsigned c = be16(b, p + 2 * j);
                    if (c >= 0xd800 && c <= 0xdbff) {
                        require(j + 1 < n, "Truncated surrogate");
                        unsigned d = be16(b, p + 2 * (++j));
                        require(d >= 0xdc00 && d <= 0xdfff, "Invalid surrogate");
                        c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
                    }
                    utf8(s, c);
                }
                v = Value::str(s);
            }
        } else if (type == 10 || type == 13) {
            size_t n = count(p, marker);
            span(p, n * refs * (type == 13 ? 2 : 1));
            v = type == 10 ? Value::list() : Value::dictionary();
            for (size_t j = 0; j < n; j++) {
                size_t ref = size_t(readInt(b, p + j * refs, refs));
                Value item = object(ref, depth + 1);
                if (type == 10)
                    v.array.push_back(item);
                else {
                    require(item.type == Value::String && !v.dict.count(item.string),
                            "Invalid binary plist key");
                    v.dict[item.string] =
                        object(size_t(readInt(b, p + (n + j) * refs, refs)), depth + 1);
                }
            }
        } else
            require(false, "Unsupported binary plist object");
        visiting.erase(index);
        return v;
    }

  public:
    BinaryReader(const Bytes &input) : b(input), table(0), refs(0), expanded(0), materialized(0) {}
    Value read() {
        require(b.size() >= 40, "Short binary plist");
        size_t t = b.size() - 32, width = b[t + 6];
        refs = b[t + 7];
        require(width >= 1 && width <= 8 && refs >= 1 && refs <= 8, "Invalid plist widths");
        U64 n = readInt(b, t + 8, 8), root = readInt(b, t + 16, 8), at = readInt(b, t + 24, 8);
        require(n > 0 && n <= 65536 && root < n && at >= 8 && at <= t &&
                    n <= (t - size_t(at)) / width,
                "Invalid plist trailer");
        table = size_t(at);
        for (size_t i = 0; i < size_t(n); i++) {
            U64 off = readInt(b, table + i * width, width);
            require(off >= 8 && off < table, "Invalid object offset");
            offsets.push_back(size_t(off));
        }
        return object(size_t(root), 0);
    }
};
static void marker(Bytes &b, U8 type, size_t n) {
    if (n < 15)
        b.push_back(U8(type | n));
    else {
        b.push_back(type | 15);
        b.push_back(0x12);
        putInt(b, n, 4);
    }
}
class BinaryWriter {
    std::vector<Bytes> objects;
    size_t add(const Value &v, unsigned depth) {
        require(depth <= 32 && objects.size() < 65536, "Plist complexity limit");
        size_t id = objects.size();
        objects.push_back(Bytes());
        Bytes b;
        switch (v.type) {
        case Value::Null:
            b.push_back(0);
            break;
        case Value::Boolean:
            b.push_back(v.boolean ? 9 : 8);
            break;
        case Value::Integer:
            if (!v.negative && v.integer >> (63)) {
                b.push_back(0x14);
                putInt(b, 0, 8);
            } else
                b.push_back(0x13);
            putInt(b, v.integer, 8);
            break;
        case Value::Real: {
            b.push_back(0x23);
            U64 n;
            memcpy(&n, &v.real, 8);
            putInt(b, n, 8);
            break;
        }
        case Value::Data:
            marker(b, 0x40, v.data.size());
            b.insert(b.end(), v.data.begin(), v.data.end());
            break;
        case Value::String: {
            bool ascii = true;
            for (size_t i = 0; i < v.string.size(); i++)
                if ((unsigned char)v.string[i] > 127)
                    ascii = false;
            if (ascii) {
                marker(b, 0x50, v.string.size());
                b.insert(b.end(), v.string.begin(), v.string.end());
            } else {
                Bytes encoded;
                for (size_t i = 0; i < v.string.size();) {
                    unsigned c = (unsigned char)v.string[i++], extra = 0, min = 0;
                    if (c < 128) {
                    } else if ((c & 0xe0) == 0xc0) {
                        c &= 31;
                        extra = 1;
                        min = 128;
                    } else if ((c & 0xf0) == 0xe0) {
                        c &= 15;
                        extra = 2;
                        min = 2048;
                    } else if ((c & 0xf8) == 0xf0) {
                        c &= 7;
                        extra = 3;
                        min = 65536;
                    } else
                        require(false, "Invalid UTF8");
                    require(extra <= v.string.size() - i, "Truncated UTF8");
                    for (unsigned j = 0; j < extra; j++) {
                        unsigned d = (unsigned char)v.string[i++];
                        require((d & 0xc0) == 0x80, "Invalid UTF8 continuation");
                        c = (c << 6) | (d & 63);
                    }
                    require(c >= min && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff),
                            "Invalid UTF8 scalar");
                    if (c > 65535) {
                        c -= 65536;
                        putInt(encoded, 0xd800 + (c >> 10), 2);
                        putInt(encoded, 0xdc00 + (c & 1023), 2);
                    } else
                        putInt(encoded, c, 2);
                }
                marker(b, 0x60, encoded.size() / 2);
                b.insert(b.end(), encoded.begin(), encoded.end());
            }
            break;
        }
        case Value::Array:
            marker(b, 0xa0, v.array.size());
            for (size_t i = 0; i < v.array.size(); i++)
                putInt(b, add(v.array[i], depth + 1), 4);
            break;
        case Value::Dict: {
            marker(b, 0xd0, v.dict.size());
            std::vector<size_t> values;
            for (std::map<std::string, Value>::const_iterator i = v.dict.begin(); i != v.dict.end();
                 ++i) {
                putInt(b, add(Value::str(i->first), depth + 1), 4);
                values.push_back(add(i->second, depth + 1));
            }
            for (size_t j = 0; j < values.size(); j++)
                putInt(b, values[j], 4);
            break;
        }
        }
        objects[id] = b;
        return id;
    }

  public:
    Bytes write(const Value &v) {
        size_t root = add(v, 0);
        Bytes b = bytes("bplist00");
        std::vector<size_t> offsets;
        for (size_t i = 0; i < objects.size(); i++) {
            offsets.push_back(b.size());
            b.insert(b.end(), objects[i].begin(), objects[i].end());
            require(b.size() <= 4 * 1024 * 1024, "Binary plist size limit");
        }
        size_t table = b.size();
        for (size_t i = 0; i < offsets.size(); i++)
            putInt(b, offsets[i], 4);
        b.insert(b.end(), 6, 0);
        b.push_back(4);
        b.push_back(4);
        putInt(b, objects.size(), 8);
        putInt(b, root, 8);
        putInt(b, table, 8);
        return b;
    }
};
Value parsePlist(const Bytes &b) {
    require(b.size() <= 4 * 1024 * 1024, "Plist size limit");
    if (b.size() >= 8 && text(slice(b, 0, 8)) == "bplist00")
        return BinaryReader(b).read();
    return XmlReader(b).read();
}
Bytes binaryPlist(const Value &v) {
    return BinaryWriter().write(v);
}
} // namespace cp
