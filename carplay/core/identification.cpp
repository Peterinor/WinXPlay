#include "protocol.h"
namespace cp {
namespace {
void add(Bytes &out, U16 id, const Bytes &b) {
    Bytes p = parameter(id, b);
    out.insert(out.end(), p.begin(), p.end());
}
Bytes string0(const char *s) {
    Bytes b = bytes(s);
    b.push_back(0);
    return b;
}
Bytes u16(U16 value) {
    Bytes b(2);
    put16(b, 0, value);
    return b;
}
} // namespace
std::vector<Frame> wiredSubscriptions() {
    std::vector<Frame> messages;
    Bytes body;
    add(body, 0, u16(500));
    add(body, 1, Bytes(1, 1));
    messages.push_back(Frame(0xae03, body));
    messages.push_back(Frame(
        0x5000,
        unhex("001800000004000100040004000400060004000c0004001a00100001000400000004000100040007")));
    body.clear();
    add(body, 0, u16(42));
    add(body, 1, Bytes());
    add(body, 2, Bytes());
    messages.push_back(Frame(0x5200, body));
    messages.push_back(Frame(0xae00, unhex("000400040004000500040006")));
    messages.push_back(Frame(0x4157, unhex("000400000004000400040005")));
    messages.push_back(Frame(0x4154, unhex("00040000000400010004000200040003000400040004000b")));
    return messages;
}
Frame accessoryIdentification(unsigned usbInterface, const char *displayName, const char *model, const char *serial) {
    require(usbInterface <= 255, "Invalid CarPlay USB interface");
    Bytes out;
    require(displayName && model && serial && *serial,"Missing accessory identification");
    const char *names[] = {displayName, model, "WinXPlay", serial, "0.2",
                           "1.0"};
    for (unsigned i = 0; i < 6; ++i)
        add(out, U16(i), string0(names[i]));
    add(out, 6, unhex("aa01aa035000500252005203ae00ae024157415941544156ae034301"));
    add(out, 7, unhex("aa00aa02aa04aa05ea00ea01500152015202ae01415841554300"));
    add(out, 8, Bytes(1, 2));
    add(out, 9, u16(20));
    Bytes ea;
    add(ea, 0, Bytes(1, 1));
    add(ea, 1, string0("org.mrn.carplay"));
    add(ea, 2, Bytes(1, 0));
    add(out, 10, ea);
    add(out, 12, string0("en"));
    add(out, 13, string0("en"));
    Bytes transport;
    add(transport, 0, u16(0));
    add(transport, 1, string0("USBHostTransport"));
    add(transport, 2, Bytes());
    add(transport, 3, Bytes(1, U8(usbInterface)));
    add(transport, 4, Bytes());
    add(out, 16, transport);
    Bytes route;
    add(route, 0, u16(42));
    add(route, 1, string0("RouteGuidance"));
    add(route, 2, u16(64));
    add(route, 4, u16(64));
    add(route, 6, u16(8));
    add(out, 30, route);
    return Frame(0x1d01, out);
}
} // namespace cp
