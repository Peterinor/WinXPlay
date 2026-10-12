#ifndef CP_TRANSPORT_H
#define CP_TRANSPORT_H
#include "../core/protocol.h"
namespace cp {
typedef U64 Connection;
const Connection InvalidConnection = ~Connection(0);
struct PeerAddress {
    Bytes address;
    U32 scope;
    unsigned port;
    PeerAddress() : address(16, 0), scope(0), port(0) {}
    bool sameHost(const PeerAddress &other) const {
        return address == other.address && scope == other.scope;
    }
};
class Transport {
  public:
    virtual ~Transport() {}
    virtual Connection bind(unsigned port, bool tcp) = 0;
    virtual unsigned localPort(Connection) = 0;
    virtual Connection accept(Connection, PeerAddress &) = 0;
    virtual void close(Connection) = 0;
    virtual void sendAll(Connection, const Bytes &) = 0;
    // TCP false means orderly EOF; errors throw. UDP false means no data.
    virtual bool receive(Connection, Bytes &) = 0;
    virtual bool receiveDatagram(Connection, Bytes &, PeerAddress &) = 0;
    virtual bool sendDatagram(Connection, const Bytes &, const PeerAddress &) = 0;
    virtual std::vector<Connection> poll(const std::vector<Connection> &, unsigned timeoutMs) = 0;
};
} // namespace cp
#endif
