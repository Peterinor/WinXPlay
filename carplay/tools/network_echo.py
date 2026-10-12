"""Finite IPv6 test peer for MRN_NETWORK_TEST; no third-party dependencies.

Use an explicit link-local bind address with %interface on a lab peer.
iPhone does not run this service; it is not a CarPlay protocol emulator.
"""
import argparse
import selectors
import socket
import time

MAGIC = b'MRN-NET-TEST-V1'


def serve(bind, port, seconds, on_ready=None):
    endpoint = socket.getaddrinfo(bind, port, socket.AF_INET6, socket.SOCK_STREAM)[0][4]
    udp = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    tcp = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    selector = selectors.DefaultSelector()
    counts = {'tcp': 0, 'udp': 0}
    clients = {}
    try:
        tcp.bind(endpoint)
        actual = tcp.getsockname()
        udp.bind(actual)
        tcp.listen(4)
        for sock, kind in ((tcp, 'listen'), (udp, 'udp')):
            sock.setblocking(False)
            selector.register(sock, selectors.EVENT_READ, kind)
        if on_ready:
            on_ready(actual)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for key, mask in selector.select(min(.1, max(0, deadline - time.monotonic()))):
                sock, kind = key.fileobj, key.data
                if kind == 'listen':
                    client, _ = sock.accept()
                    if len(clients) >= 8:
                        client.close()
                        continue
                    client.setblocking(False)
                    clients[client] = [bytearray(), time.monotonic() + 3, None]
                    selector.register(client, selectors.EVENT_READ, 'client')
                elif kind == 'udp':
                    data, peer = sock.recvfrom(2048)
                    if len(data) == 32 and data.startswith(MAGIC):
                        sock.sendto(data, peer)
                        counts['udp'] += 1
                else:
                    state = clients[sock]
                    try:
                        if mask & selectors.EVENT_READ:
                            chunk = sock.recv(33 - len(state[0]))
                            state[0].extend(chunk)
                            if not chunk or len(state[0]) > 32:
                                state[1] = 0
                            elif len(state[0]) == 32:
                                if state[0].startswith(MAGIC):
                                    state[2] = bytes(state[0])
                                    selector.modify(sock, selectors.EVENT_WRITE, 'client')
                                else:
                                    state[1] = 0
                        elif mask & selectors.EVENT_WRITE:
                            n = sock.send(state[2])
                            state[2] = state[2][n:]
                            if not state[2]:
                                counts['tcp'] += 1
                                state[1] = 0
                    except (ConnectionError, OSError):
                        state[1] = 0
            for sock, state in list(clients.items()):
                if time.monotonic() >= state[1]:
                    selector.unregister(sock)
                    sock.close()
                    del clients[sock]
        return counts
    finally:
        for sock in clients:
            sock.close()
        selector.close()
        tcp.close()
        udp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', required=True, help='IPv6 address, e.g. fe80::1234%%12')
    parser.add_argument('--port', type=int, default=47007)
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535 or not 1 <= args.seconds <= 600:
        parser.error('port must be 1024..65535 and seconds 1..600')
    print(serve(args.bind, args.port, args.seconds,
                lambda endpoint: print('Listening:', endpoint, flush=True)))
