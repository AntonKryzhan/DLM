#!/usr/bin/env python3
"""Independent loopback peers for the native C provider; no public services.

Run: python3 crates/dlm_core/tests/native_hosts/network.py
Needs a GNU-compatible C compiler and OpenSSL CLI. HTTP/2 additionally uses
Node's standard http2 module. Set DLM_NETWORK_REQUIRE_CURL=1 in a configured CI
job to make an unavailable/old libcurl a failure instead of a reported skip.
"""
import base64
import contextlib
import ctypes as C
import hashlib
import http.server
import os
from pathlib import Path
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[4]
U8P = C.POINTER(C.c_uint8)


class Text(C.Structure):
    _fields_ = [("data", C.c_void_p), ("count", C.c_size_t)]

    @classmethod
    def of(cls, value):
        if isinstance(value, str):
            value = value.encode()
        pointer = C.c_char_p(value)
        return cls(C.cast(pointer, C.c_void_p), len(value))


class Io(C.Structure):
    _fields_ = [("count", C.c_size_t), ("status", C.c_int32), ("system", C.c_int32)]


class Opened(C.Structure):
    _fields_ = [("handle", C.c_void_p), ("status", C.c_int32), ("system", C.c_int32)]


class Address(C.Structure):
    _fields_ = [("family", C.c_uint32), ("port", C.c_uint32), ("scope", C.c_uint32), ("bytes", C.c_uint8 * 16)]


class Resolved(C.Structure):
    _fields_ = [("address", Address), ("status", C.c_int32), ("system", C.c_int32)]


class Datagram(C.Structure):
    _fields_ = [("io", Io), ("peer", Address)]


class WsIo(C.Structure):
    _fields_ = [("io", Io), ("offset", C.c_uint64), ("remaining", C.c_uint64), ("flags", C.c_uint32)]


class Poll(C.Structure):
    _fields_ = [("socket", C.c_void_p), ("events", C.c_uint32), ("ready", C.c_uint32), ("status", C.c_int32), ("system", C.c_int32)]


def octets(value):
    return (C.c_uint8 * len(value)).from_buffer_copy(value)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass  # Expected when testing certificate failures/client aborts.

    def do_GET(self):
        if self.path == "/ws":
            return self.websocket()
        if self.path == "/slow":
            time.sleep(0.25)
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/")
            body = b"redirect"
        else:
            self.send_response(200)
            body = b"\x00\xff\x80hello" if self.path != "/large" else b"x" * 8192
        if self.path == "/headers":
            self.send_header("X-Large", "x" * 70000)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", "999")
        self.end_headers()

    def websocket(self):
        self.close_connection = True
        if self.headers.get("Authorization") != "Bearer local-test":
            self.send_response(403)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.connection.settimeout(2)
        try:
            first, second = self.rfile.read(2)
            size = second & 127
            if size == 126:
                size = struct.unpack("!H", self.rfile.read(2))[0]
            elif size == 127:
                size = struct.unpack("!Q", self.rfile.read(8))[0]
            if not (second & 128) or size > 1024 * 1024:
                return
            mask = self.rfile.read(4)
            raw = self.rfile.read(size)
            body = bytes(b ^ mask[i % 4] for i, b in enumerate(raw))
            if first & 15 != 2:
                return
            # An independent peer splits a binary message into two frames,
            # injects a ping, and includes non-UTF-8 bytes in the payload.
            cut = min(2, len(body))
            self.wfile.write(bytes([0x02, cut]) + body[:cut])
            self.wfile.write(b"\x89\x01?")
            rest = body[cut:]
            header = bytes([0x80, len(rest)]) if len(rest) < 126 else b"\x80\x7e" + struct.pack("!H", len(rest))
            self.wfile.write(header + rest)
            self.wfile.flush()
            # Wait for the client to finish reading before the TCP close.
            try:
                self.rfile.read(2)
            except (OSError, TimeoutError):
                pass
        except (OSError, ValueError, struct.error):
            pass


class ProviderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="dlm-network-")
        cls.path = Path(cls.directory.name)
        source = ROOT / "crates/dlm_core/src/native/network.c"
        cls.library = cls.path / ("network.dll" if os.name == "nt" else "network.so")
        command = [os.environ.get("CC", "gcc" if os.name == "nt" else "cc"), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-shared", str(source), "-o", str(cls.library)]
        command += ["-lws2_32"] if os.name == "nt" else ["-fPIC", "-ldl"]
        subprocess.run(command, check=True, capture_output=True)
        cls.api = C.CDLL(str(cls.library))
        signatures = {
            "capabilities": (C.c_uint32, []), "curl_version": (C.c_uint32, []),
            "provider_shutdown": (Io, []),
            "tcp": (Opened, [Text, C.c_uint16, C.c_uint32]),
            "listen": (Opened, [Text, C.c_uint16, C.c_uint32]),
            "udp": (Opened, [Text, C.c_uint16]), "accept": (Opened, [C.c_void_p]),
            "close": (None, [C.c_void_p]), "local": (Resolved, [C.c_void_p]),
            "resolve": (Resolved, [Text, C.c_uint16, C.c_uint32]),
            "send": (Io, [C.c_void_p, U8P, C.c_size_t]), "recv": (Io, [C.c_void_p, U8P, C.c_size_t]),
            "transfer": (Io, [C.c_void_p, U8P, C.c_size_t, C.c_uint32, C.c_void_p, C.c_bool]),
            "shutdown": (Io, [C.c_void_p]),
            "udp_send": (Io, [C.c_void_p, Address, U8P, C.c_size_t]),
            "udp_recv": (Datagram, [C.c_void_p, U8P, C.c_size_t]),
            "wait": (Io, [C.c_void_p, C.c_uint32, C.c_uint32, C.c_void_p]),
            "poll": (Io, [C.POINTER(Poll), C.c_size_t, C.c_uint32, C.c_void_p]),
            "cancel_new": (C.c_void_p, []), "cancel_close": (None, [C.c_void_p]),
            "cancel_signal": (None, [C.c_void_p]), "cancel_reset": (None, [C.c_void_p]),
            "tls": (Opened, [Text, C.c_uint32, Text, Text]),
            "http": (Opened, [Text, C.c_uint32, C.c_size_t, Text, Text]),
            "http_version": (Io, [C.c_void_p, C.c_uint32]), "http_header": (Io, [C.c_void_p, Text]),
            "http_body": (Io, [C.c_void_p, U8P, C.c_size_t]), "http_method": (Io, [C.c_void_p, Text]),
            "http_ech": (Io, [C.c_void_p, Text]), "http_perform": (Io, [C.c_void_p, C.c_void_p]),
            "http_info": (Io, [C.c_void_p, C.c_uint32]),
            "http_read": (Io, [C.c_void_p, C.c_bool, C.c_size_t, U8P, C.c_size_t]),
            "websocket": (Opened, [Text, C.c_uint32, Text, Text]), "ws_start": (Io, [C.c_void_p]),
            "ws_send": (Io, [C.c_void_p, U8P, C.c_size_t, C.c_uint32, C.c_uint32, C.c_void_p]),
            "ws_recv": (WsIo, [C.c_void_p, U8P, C.c_size_t]), "random": (Io, [U8P, C.c_size_t]),
        }
        for name, (result, args) in signatures.items():
            function = getattr(cls.api, "dlm_net_" + name)
            function.restype, function.argtypes = result, args
            setattr(cls, name, staticmethod(function))
        cls.ca = cls.path / "localhost.crt"
        cls.key = cls.path / "localhost.key"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost", "-keyout", str(cls.key), "-out", str(cls.ca)], check=True, capture_output=True)
        cls.servers = []
        cls.threads = []
        cls.http_port = cls.start_server(None)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.load_cert_chain(cls.ca, cls.key)
        cls.https_port = cls.start_server(context)
        old = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        old.minimum_version = old.maximum_version = ssl.TLSVersion.TLSv1_2
        old.load_cert_chain(cls.ca, cls.key)
        cls.old_tls_port = cls.start_server(old)
        caps = cls.capabilities()
        print(f"provider capabilities={caps}, libcurl=0x{cls.curl_version():06x}", flush=True)
        if os.environ.get("DLM_NETWORK_REQUIRE_CURL") and not (caps & 16):
            raise RuntimeError("configured job requires libcurl >=8.10")

    @classmethod
    def start_server(cls, context):
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.daemon_threads = True
        if context:
            server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        thread.start()
        cls.servers.append(server)
        cls.threads.append(thread)
        return server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        for server in cls.servers:
            server.shutdown()
            server.server_close()
        for thread in cls.threads:
            thread.join(timeout=2)
        if cls.provider_shutdown().status != 0:
            raise AssertionError("test leaked a provider resource")
        cls.directory.cleanup()

    @contextlib.contextmanager
    def owned(self, opened):
        self.assertEqual(opened.status, 0, (opened.status, opened.system))
        self.assertTrue(opened.handle)
        try:
            yield opened.handle
        finally:
            self.close(opened.handle)

    @contextlib.contextmanager
    def pair(self):
        with self.owned(self.listen(Text.of("127.0.0.1"), 0, 16)) as listener:
            address = self.local(listener)
            with self.owned(self.tcp(Text.of("127.0.0.1"), address.address.port, 1000)) as client:
                self.assertEqual(self.wait(listener, 1, 1000, None).status, 0)
                with self.owned(self.accept(listener)) as peer:
                    yield client, peer

    def require_http(self):
        if not self.capabilities() & 16:
            self.skipTest("libcurl >=8.10 unavailable")

    def request(self, path="/", secure=False, maximum=65536, timeout=2000, hostname="localhost", trust=True):
        self.require_http()
        port = self.https_port if secure else self.http_port
        url = f"{'https' if secure else 'http'}://{hostname}:{port}{path}"
        return self.http(Text.of(url), timeout, maximum, Text.of(str(self.ca) if secure and trust else ""), Text.of(""))

    def read_body(self, handle):
        count = self.http_info(handle, 2).count
        result = (C.c_uint8 * count)()
        io = self.http_read(handle, False, 0, result, count)
        self.assertEqual((io.status, io.count), (0, count))
        return bytes(result)

    def test_tcp_binary_partial_eof_and_empty_buffer(self):
        with self.pair() as (client, peer):
            value = octets(b"\x00\xff\x80")
            self.assertEqual(self.transfer(client, value, 3, 1000, None, True).status, 0)
            self.assertEqual(self.shutdown(client).status, 0)
            target = (C.c_uint8 * 8)()
            result = self.transfer(peer, target, 8, 1000, None, False)
            self.assertEqual((result.status, result.count, bytes(target[:3])), (2, 3, bytes(value)))
            self.assertEqual(self.recv(peer, None, 0).status, 4)
            self.assertEqual(self.transfer(peer, None, 0, 0, None, False).status, 0)
            self.assertEqual(self.transfer(None, None, 0, 0, None, False).status, 4)

    def test_tcp_would_block_timeout_and_cancellation(self):
        with self.pair() as (_, peer):
            target = (C.c_uint8 * 4)()
            self.assertEqual(self.recv(peer, target, 4).status, 1)
            start = time.monotonic()
            self.assertEqual(self.transfer(peer, target, 4, 30, None, False).status, 3)
            self.assertLess(time.monotonic() - start, 1)
            token = self.cancel_new()
            self.assertTrue(token)
            timer = threading.Timer(0.04, self.cancel_signal, (token,))
            timer.start()
            try:
                start = time.monotonic()
                self.assertEqual(self.wait(peer, 1, 2000, token).status, 9)
                self.assertLess(time.monotonic() - start, 0.5)
                self.cancel_reset(token)
                self.assertEqual(self.wait(peer, 1, 0, token).status, 3)
            finally:
                timer.join()
                self.cancel_close(token)

    def test_multi_socket_poll_and_limits(self):
        with self.pair() as (client, peer):
            items = (Poll * 2)(Poll(client, 2, 0, 0, 0), Poll(peer, 1, 0, 0, 0))
            result = self.poll(items, 2, 1000, None)
            self.assertEqual(result.status, 0)
            self.assertTrue(items[0].ready & 2)
            self.assertEqual(items[1].ready, 0)
            self.assertEqual(self.poll(items, 0, 0, None).status, 4)
            self.assertEqual(self.poll(items, 4097, 0, None).status, 4)
            items[0].events = 0x80
            self.assertEqual(self.poll(items, 2, 0, None).status, 4)

    def test_udp_zero_length_binary_truncation_and_ipv6(self):
        for host in ("127.0.0.1", "::1"):
            opened = self.udp(Text.of(host), 0)
            if host == "::1" and opened.status:
                continue  # IPv6-disabled hosts still exercise IPv4 above.
            with self.owned(opened) as udp:
                address = self.local(udp)
                self.assertEqual(address.status, 0)
                target = (C.c_uint8 * 2)()
                self.assertEqual(self.udp_recv(udp, target, 2).io.status, 1)
                self.assertEqual(self.udp_send(udp, address.address, None, 0).status, 0)
                self.assertEqual(self.wait(udp, 1, 1000, None).status, 0)
                empty = self.udp_recv(udp, target, 2)
                self.assertEqual((empty.io.status, empty.io.count), (0, 0))
                payload = octets(b"\x00\xff\x80\xfe")
                self.assertEqual(self.udp_send(udp, address.address, payload, 4).status, 0)
                self.assertEqual(self.wait(udp, 1, 1000, None).status, 0)
                truncated = self.udp_recv(udp, target, 2)
                self.assertEqual((truncated.io.status, truncated.io.count), (10, 2))
                self.assertEqual(bytes(target), b"\x00\xff")
                self.assertEqual(self.udp_recv(udp, target, 2).io.status, 1)

    def test_input_validation_and_random(self):
        self.close(None)
        self.assertEqual(self.tcp(Text.of(b"local\x00host"), 443, 50).status, 4)
        self.assertEqual(self.tcp(Text.of("localhost"), 0, 50).status, 4)
        self.assertEqual(self.tcp(Text.of("localhost"), 443, 0).status, 4)
        self.assertEqual(self.resolve(Text.of(b"local\x00host"), 1, 0).status, 4)
        self.assertEqual(self.accept(None).status, 4)
        self.assertEqual(self.random(None, 0).status, 0)
        a, b = (C.c_uint8 * 32)(), (C.c_uint8 * 32)()
        self.assertEqual(self.random(a, 32).status, 0)
        self.assertEqual(self.random(b, 32).status, 0)
        self.assertNotEqual(bytes(a), bytes(b))
        self.assertEqual(self.random(a, 67108865).status, 4)

    def test_provider_shutdown_refuses_live_owners_and_missing_backend(self):
        with self.owned(self.udp(Text.of("127.0.0.1"), 0)):
            self.assertEqual(self.provider_shutdown().status, 4)
        self.assertEqual(self.provider_shutdown().status, 0)
        previous = os.environ.get("DLM_LIBCURL")
        try:
            os.environ["DLM_LIBCURL"] = str(self.path / "missing-library")
            self.assertEqual(self.capabilities(), 7)
            request = self.http(Text.of("https://localhost/"), 1000, 1024, Text.of(""), Text.of(""))
            self.assertEqual((request.status, bool(request.handle)), (6, False))
            self.assertEqual(self.provider_shutdown().status, 0)
        finally:
            if previous is None:
                os.environ.pop("DLM_LIBCURL", None)
            else:
                os.environ["DLM_LIBCURL"] = previous

    def test_http_binary_request_response_and_header_validation(self):
        with self.owned(self.request()) as handle:
            self.assertEqual(self.http_version(handle, 1).status, 0)
            self.assertEqual(self.http_header(handle, Text.of("X-Test: ok\r\nInjected: bad")).status, 4)
            self.assertEqual(self.http_header(handle, Text.of("X-Test: valid")).status, 0)
            self.assertEqual(self.http_method(handle, Text.of("M-SEARCH")).status, 0)
            self.assertEqual(self.http_method(handle, Text.of("GET /\r\nInjected")).status, 4)
            payload = octets(b"\x00\xff\x80binary")
            self.assertEqual(self.http_body(handle, payload, len(payload)).status, 0)
            self.assertEqual(self.http_method(handle, Text.of("POST")).status, 0)
            payload[1] = 0  # Provider owns a copy before request execution.
            self.assertEqual(self.http_perform(handle, None).status, 0)
            self.assertEqual(self.http_info(handle, 0).count, 200)
            self.assertEqual(self.http_info(handle, 1).count, 1)
            self.assertEqual(self.read_body(handle), b"\x00\xff\x80binary")

    def test_http_response_limits_redirect_and_head(self):
        for path in ("/large", "/headers"):
            with self.owned(self.request(path, maximum=16)) as handle:
                self.assertEqual(self.http_perform(handle, None).status, 7)
        with self.owned(self.request("/redirect")) as handle:
            self.assertEqual(self.http_perform(handle, None).status, 0)
            self.assertEqual(self.http_info(handle, 0).count, 302)
        with self.owned(self.request()) as handle:
            self.assertEqual(self.http_method(handle, Text.of("HEAD")).status, 0)
            self.assertEqual(self.http_perform(handle, None).status, 0)
            self.assertEqual(self.read_body(handle), b"")

    def test_tls13_trust_hostname_and_downgrade_rejection(self):
        with self.owned(self.request(secure=True)) as handle:
            result = self.http_perform(handle, None)
            self.assertEqual(result.status, 0, (result.status, result.system))
            self.assertEqual(self.read_body(handle), b"\x00\xff\x80hello")
        for options in ({"trust": False}, {"hostname": "127.0.0.1"}):
            with self.owned(self.request(secure=True, **options)) as handle:
                self.assertEqual(self.http_perform(handle, None).status, 8)
        opened = self.tls(Text.of(f"https://localhost:{self.old_tls_port}/"), 1000, Text.of(str(self.ca)), Text.of(""))
        self.assertEqual(opened.status, 8)
        self.assertFalse(opened.handle)

    def test_tls_stream_and_wrong_pin(self):
        self.require_http()
        url = Text.of(f"https://localhost:{self.https_port}/")
        with self.owned(self.tls(url, 2000, Text.of(str(self.ca)), Text.of(""))) as handle:
            data = octets(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            self.assertEqual(self.transfer(handle, data, len(data), 2000, None, True).status, 0)
            received = bytearray()
            target = (C.c_uint8 * 128)()
            end = time.monotonic() + 2
            while time.monotonic() < end:
                result = self.recv(handle, target, 128)
                if result.status == 1:
                    self.wait(handle, 1, 200, None)
                    continue
                if result.status == 2:
                    break
                self.assertEqual(result.status, 0, result.system)
                received.extend(target[:result.count])
                if b"\x00\xff\x80hello" in received:
                    break  # HTTP Content-Length, not an unclean TCP EOF, ends this response.
            self.assertIn(b"\x00\xff\x80hello", received)
        wrong = Text.of("sha256//" + base64.b64encode(b"\0" * 32).decode())
        failed = self.tls(url, 2000, Text.of(str(self.ca)), wrong)
        self.assertEqual((failed.status, bool(failed.handle)), (8, False))

    def test_http_timeout_cancellation_and_invalid_scheme(self):
        with self.owned(self.request("/slow", timeout=40)) as handle:
            self.assertEqual(self.http_perform(handle, None).status, 3)
        token = self.cancel_new()
        try:
            self.cancel_signal(token)
            with self.owned(self.request()) as handle:
                self.assertEqual(self.http_perform(handle, token).status, 9)
        finally:
            self.cancel_close(token)
        invalid = self.http(Text.of("file:///etc/passwd"), 1000, 1024, Text.of(""), Text.of(""))
        self.assertEqual((invalid.status, bool(invalid.handle)), (4, False))

    def test_websocket_auth_fragmentation_binary_and_ping(self):
        if not self.capabilities() & 128:
            self.skipTest("libcurl WebSocket unavailable")
        url = Text.of(f"wss://localhost:{self.https_port}/ws")
        with self.owned(self.websocket(url, 2000, Text.of(str(self.ca)), Text.of(""))) as handle:
            self.assertNotEqual(self.ws_start(handle).status, 0)
        with self.owned(self.websocket(url, 2000, Text.of(str(self.ca)), Text.of(""))) as handle:
            self.assertEqual(self.http_header(handle, Text.of("Authorization: Bearer local-test")).status, 0)
            self.assertEqual(self.ws_start(handle).status, 0)
            payload = octets(b"\x00\xff\x80hello")
            result = self.ws_send(handle, payload, len(payload), 2, 1000, None)
            self.assertEqual((result.status, result.count), (0, len(payload)))
            target = (C.c_uint8 * 2)()
            received = bytearray()
            end = time.monotonic() + 2
            while len(received) < len(payload) and time.monotonic() < end:
                result = self.ws_recv(handle, target, 2)
                if result.io.status == 1:
                    self.wait(handle, 1, 200, None)
                    continue
                self.assertEqual(result.io.status, 0, result.io.system)
                if not (result.flags & (8 | 16 | 64)):
                    received.extend(target[:result.io.count])
            self.assertEqual(received, bytes(payload))

    def test_http2_negotiation_with_independent_node_server(self):
        self.require_http()
        node = shutil.which("node")
        if not node or not self.capabilities() & 32:
            self.skipTest("Node http2 or libcurl HTTP/2 unavailable")
        script = self.path / "h2.cjs"
        script.write_text("""const fs=require('node:fs'),h2=require('node:http2');
const server=h2.createSecureServer({key:fs.readFileSync(process.argv[2]),cert:fs.readFileSync(process.argv[3])});
server.on('sessionError',()=>{});server.on('stream',s=>{s.respond({':status':200});s.end(Buffer.from([0,255,128,72,50]));});
server.listen(0,'127.0.0.1',()=>console.log(server.address().port));
""")
        process = subprocess.Popen([node, str(script), str(self.key), str(self.ca)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            port = int(process.stdout.readline().strip())
            opened = self.http(Text.of(f"https://localhost:{port}/"), 2000, 4096, Text.of(str(self.ca)), Text.of(""))
            with self.owned(opened) as handle:
                self.assertEqual(self.http_version(handle, 2).status, 0)
                result = self.http_perform(handle, None)
                self.assertEqual(result.status, 0, result.system)
                self.assertEqual(self.http_info(handle, 1).count, 2)
                self.assertEqual(self.read_body(handle), b"\0\xff\x80H2")
        finally:
            process.terminate()
            process.communicate(timeout=5)

    def test_http3_only_does_not_silently_fallback(self):
        with self.owned(self.request(secure=True, timeout=100)) as handle:
            option = self.http_version(handle, 3)
            if self.capabilities() & 64:
                self.assertEqual(option.status, 0)
                self.assertNotEqual(self.http_perform(handle, None).status, 0)
                self.assertNotEqual(self.http_info(handle, 1).count, 1)
            else:
                self.assertEqual(option.status, 6)

    def test_compiled_dlm_dynamic_url_https_and_resource_cleanup(self):
        self.require_http()
        compiler = os.environ.get("DLM_NETWORK_DLM", str(ROOT / "target/release" / ("dlm.exe" if os.name == "nt" else "dlm")))
        if not Path(compiler).is_file():
            self.skipTest("build the DLM release compiler to check the language boundary")
        fixture = ROOT / "crates/dlm_core/tests/native_hosts/network_http.dlm"
        library = self.path / ("dlm_http.dll" if os.name == "nt" else "dlm_http.so")
        subprocess.run([compiler, "build", str(fixture), "--lib", str(ROOT / "lib"), "--allow-unsafe", "--target", "sharedlib", "-o", str(library)], check=True, capture_output=True)
        api = C.CDLL(str(library))
        fetch = api.network_fixture_fetch
        fetch.restype, fetch.argtypes = Io, [U8P, C.c_size_t, U8P, C.c_size_t, U8P, C.c_size_t, C.c_uint32]
        shutdown = api.network_fixture_shutdown
        shutdown.restype, shutdown.argtypes = Io, []
        url = octets(f"https://localhost:{self.https_port}/".encode())
        ca = octets(str(self.ca).encode())
        target = (C.c_uint8 * 64)()
        for _ in range(3):
            result = fetch(url, len(url), ca, len(ca), target, 64, 1)
            self.assertEqual(result.status, 0, result.system)
            self.assertEqual(bytes(target[:result.count]), b"\x00\xff\x80hello")
            self.assertEqual(shutdown().status, 0, "DLM resource/finalizer leaked a provider instance")


if __name__ == "__main__":
    unittest.main(verbosity=2)
