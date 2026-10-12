"""Exercise the actual peer: fragmentation, exact UDP length, bad prefix, timeout."""
import importlib.util
from pathlib import Path
import queue
import socket
import threading
import unittest

spec = importlib.util.spec_from_file_location('peer', Path(__file__).resolve().parents[2] / 'tools/network_echo.py')
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)


class PeerTest(unittest.TestCase):
    def test_ipv6_echo(self):
        ready = queue.Queue()
        result = queue.Queue()
        thread = threading.Thread(target=lambda: result.put(peer.serve('::1', 0, 2, ready.put)))
        thread.start()
        try:
            address = ready.get(timeout=3)
            payload = peer.MAGIC.ljust(32, b'\x00')
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as tcp:
                tcp.settimeout(1)
                tcp.connect(address)
                tcp.sendall(payload[:5])
                tcp.sendall(payload[5:])
                echoed = b''
                while len(echoed) < 32:
                    part = tcp.recv(32 - len(echoed))
                    self.assertTrue(part)
                    echoed += part
                self.assertEqual(echoed, payload)
            with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as udp:
                udp.settimeout(.2)
                udp.connect(address)
                udp.send(payload)
                self.assertEqual(udp.recv(64), payload)
                for invalid in (payload + b'x', b'x' * 32, payload[:10]):
                    udp.send(invalid)
                    with self.assertRaises(socket.timeout):
                        udp.recv(64)
            thread.join(4)
            self.assertFalse(thread.is_alive())
            self.assertEqual(result.get_nowait(), {'udp': 1, 'tcp': 1})
        finally:
            thread.join(4)


if __name__ == '__main__':
    unittest.main()
