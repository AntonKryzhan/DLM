#!/usr/bin/env python3
"""DLN foundation differential, hostile-input and dual-stack acceptance.

Uses independent Python integer/stream oracles and operating-system peers.
The inherited v0.134 suite also exercises TLS/HTTP2/WebSocket compatibility.
"""
import ctypes as C
import importlib.util
import json
import os
from pathlib import Path
import random
import socket
import ssl
import subprocess
import tempfile
import threading
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def module_at(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


base = module_at("dln_network_base", HERE / "network.py")
compat = module_at("dln_compat", ROOT / "tools/check_dln_compatibility.py")
U8P = base.U8P


class Options(C.Structure):
    _fields_ = [("no_delay", C.c_uint32), ("keep_alive", C.c_uint32), ("send_buffer", C.c_uint32), ("receive_buffer", C.c_uint32)]


class OptionsResult(C.Structure):
    _fields_ = [("options", Options), ("status", C.c_int32), ("system", C.c_int32)]


class Error(C.Structure):
    _fields_ = [("kind", C.c_int32), ("system", C.c_int32), ("operation", C.c_uint32)]


class Transfer(C.Structure):
    _fields_ = [("count", C.c_size_t), ("error", Error)]


class Parsed(C.Structure):
    _fields_ = [("value", C.c_uint64), ("next", C.c_size_t), ("status", C.c_int32)]


class Decoder(C.Structure):
    _fields_ = [("maximum", C.c_uint32), ("header", C.c_uint32), ("prefix_count", C.c_uint32), ("remaining", C.c_uint32), ("fault", C.c_int32)]


class Step(C.Structure):
    _fields_ = [("consumed", C.c_size_t), ("payload_offset", C.c_size_t), ("payload_count", C.c_size_t), ("remaining", C.c_uint32), ("complete", C.c_bool), ("status", C.c_int32)]


def build_fixture(name, directory):
    compiler = os.environ.get("DLM_NETWORK_DLM", str(ROOT / "target/release" / ("dlm.exe" if os.name == "nt" else "dlm")))
    if not Path(compiler).is_file():
        raise RuntimeError("Build the DLM release compiler before DLN acceptance")
    library = directory / (name + (".dll" if os.name == "nt" else ".so"))
    result = subprocess.run([compiler, "build", str(HERE / (name + ".dlm")), "--lib", str(ROOT / "lib"), "--allow-unsafe", "--target", "sharedlib", "-o", str(library)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return C.CDLL(str(library))


def v4(port):
    address = base.Address(4, port, 0)
    address.bytes[:4] = (127, 0, 0, 1)
    return address


def v6(port, loopback=True, scope=0):
    address = base.Address(6, port, scope)
    address.bytes[15] = 1 if loopback else 0
    return address


class DlnProviderTests(base.ProviderTests):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        signatures = {
            "connect_address": (base.Opened, [base.Address, C.c_uint32]),
            "bind_address": (base.Opened, [base.Address, C.c_uint32, C.c_uint32, C.c_bool]),
            "resolve_family": (base.Resolved, [base.Text, C.c_uint16, C.c_uint32, C.c_uint32]),
            "peer": (base.Resolved, [C.c_void_p]),
            "configure": (base.Io, [C.c_void_p, Options]),
            "get_options": (OptionsResult, [C.c_void_p]),
            "address_text": (base.Io, [base.Address, U8P, C.c_size_t, C.c_bool]),
        }
        for name, (result, args) in signatures.items():
            function = getattr(cls.api, "dlm_net_" + name)
            function.restype, function.argtypes = result, args
            setattr(cls, name, staticmethod(function))

    def require_ipv6(self):
        try:
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as probe:
                probe.bind(("::1", 0))
        except OSError as error:
            self.skipTest("OS IPv6 loopback unavailable: " + str(error))

    def echo_pair(self, address, dual=False, ipv4_client=False):
        with self.owned(self.bind_address(address, 2, 16, dual)) as listener:
            endpoint = self.local(listener)
            target = v4(endpoint.address.port) if ipv4_client else endpoint.address
            with self.owned(self.connect_address(target, 1000)) as client:
                self.assertEqual(self.wait(listener, 1, 1000, None).status, 0)
                with self.owned(self.accept(listener)) as peer:
                    payload = base.octets(b"\0\xff\x80numeric")
                    self.assertEqual(self.transfer(client, payload, len(payload), 1000, None, True).status, 0)
                    received = (C.c_uint8 * len(payload))()
                    self.assertEqual(self.transfer(peer, received, len(received), 1000, None, False).status, 0)
                    self.assertEqual(bytes(received), bytes(payload))
                    remote = self.peer(client)
                    self.assertEqual((remote.status, remote.address.port), (0, endpoint.address.port))
                    settings = self.configure(client, Options(1, 1, 16384, 16384))
                    self.assertEqual(settings.status, 0, settings.system)
                    queried = self.get_options(client)
                    self.assertEqual((queried.status, queried.options.no_delay, queried.options.keep_alive), (0, 1, 1))

    def test_numeric_ipv4_tcp_and_socket_options(self):
        self.echo_pair(v4(0))

    def test_ipv6_tcp_and_dual_stack_ipv4_client(self):
        self.require_ipv6()
        self.echo_pair(v6(0))
        self.echo_pair(v6(0, loopback=False), dual=True, ipv4_client=True)

    def test_ipv6_only_does_not_accept_ipv4(self):
        self.require_ipv6()
        with self.owned(self.bind_address(v6(0, loopback=False), 2, 16, False)) as listener:
            port = self.local(listener).address.port
            # A bound non-listening IPv4 socket reserves this port, ensuring
            # another process cannot accidentally satisfy the test connection.
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reserve:
                reserve.bind(("127.0.0.1", port))
                opened = self.connect_address(v4(port), 100)
                self.assertNotEqual(opened.status, 0)
                self.assertFalse(opened.handle)

    def test_dual_stack_udp_preserves_ipv4_mapped_peer(self):
        self.require_ipv6()
        with self.owned(self.bind_address(v6(0, loopback=False), 3, 0, True)) as receiver:
            port = self.local(receiver).address.port
            with self.owned(self.bind_address(v4(0), 3, 0, False)) as sender:
                data = base.octets(b"\0\xff\x80")
                self.assertEqual(self.udp_send(sender, v4(port), data, len(data)).status, 0)
                self.assertEqual(self.wait(receiver, 1, 1000, None).status, 0)
                out = (C.c_uint8 * 8)()
                packet = self.udp_recv(receiver, out, 8)
                self.assertEqual((packet.io.status, packet.io.count, packet.peer.family), (0, 3, 6))
                self.assertEqual(bytes(packet.peer.bytes[:12]), b"\0" * 10 + b"\xff\xff")
                self.assertEqual(self.udp_send(receiver, packet.peer, out, 3).status, 0)
                self.assertEqual(self.wait(sender, 1, 1000, None).status, 0)
                echoed = self.udp_recv(sender, out, 8)
                self.assertEqual((echoed.io.status, bytes(out[:echoed.io.count])), (0, b"\0\xff\x80"))

    def test_address_families_scopes_and_no_partial_format_write(self):
        for family in (4, 6):
            result = self.resolve_family(base.Text.of("localhost"), 443, family, 0)
            self.assertEqual((result.status, result.address.family), (0, family))
            self.assertEqual(self.resolve_family(base.Text.of("localhost"), 443, family, 0xffffffff).status, 2)
        address = v6(443, scope=3)
        address.bytes[0], address.bytes[1] = 0xfe, 0x80
        out = (C.c_uint8 * 128)()
        result = self.address_text(address, out, len(out), True)
        self.assertEqual((result.status, bytes(out[:result.count])), (0, b"[fe80::1%3]:443"))
        original = bytes([0xa5] * 128)
        out[:] = original
        self.assertEqual(self.address_text(address, out, result.count - 1, True).status, 7)
        self.assertEqual(bytes(out), original)
        scoped = self.resolve_family(base.Text.of("fe80::1%3"), 443, 6, 0)
        self.assertEqual((scoped.status, scoped.address.scope), (0, 3))

    def test_invalid_new_arguments_do_not_mutate_valid_options(self):
        for address, timeout in ((v4(0), 100), (v4(443), 0), (base.Address(5, 443, 0), 100)):
            self.assertEqual(self.connect_address(address, timeout).status, 4)
        self.assertEqual(self.bind_address(v4(0), 2, 16, True).status, 4)
        self.assertEqual(self.bind_address(v4(0), 1, 16, False).status, 4)
        self.assertEqual(self.resolve_family(base.Text.of("localhost"), 1, 7, 0).status, 4)
        with self.pair() as (client, _):
            self.assertEqual(self.configure(client, Options(1, 1, 0, 0)).status, 0)
            self.assertEqual(self.configure(client, Options(0, 0, 0xffffffff, 0)).status, 4)
            after = self.get_options(client)
            self.assertEqual((after.status, after.options.no_delay, after.options.keep_alive), (0, 1, 1))

    def test_typed_dlm_tls_binary_echo_and_owner_cleanup(self):
        self.require_http()
        api = build_fixture("dln_tls", self.path)
        echo = api.typed_tls_echo
        echo.restype, echo.argtypes = Transfer, [U8P, C.c_size_t, U8P, C.c_size_t]
        shutdown = api.typed_tls_shutdown
        shutdown.restype, shutdown.argtypes = base.Io, []
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.load_cert_chain(self.ca, self.key)
        failures = []
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)

            def serve():
                try:
                    raw, _ = listener.accept()
                    with context.wrap_socket(raw, server_side=True) as stream:
                        stream.settimeout(2)
                        data = b""
                        while len(data) < 3:
                            part = stream.recv(3 - len(data))
                            if not part:
                                raise RuntimeError("premature TLS EOF")
                            data += part
                        stream.sendall(data)
                except Exception as error:
                    failures.append(str(error))

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            uri = base.octets(f"https://localhost:{listener.getsockname()[1]}/".encode())
            ca = base.octets(str(self.ca).encode())
            result = echo(uri, len(uri), ca, len(ca))
            thread.join(timeout=4)
            self.assertFalse(thread.is_alive())
            self.assertFalse(failures, failures)
            self.assertEqual((result.error.kind, result.count), (0, 3), result.error.system)
            self.assertEqual(shutdown().status, 0)
        abort = api.typed_resource_failure
        abort.restype, abort.argtypes = None, []
        api.dlm_native_last_error.restype = C.c_char_p
        api.dlm_native_live_allocations.restype = C.c_uint64
        abort()
        self.assertTrue(api.dlm_native_last_error(), "fixture must cross its intentional failure boundary")
        self.assertEqual(api.dlm_native_live_allocations(), 0)
        self.assertEqual(shutdown().status, 0, "partial moves/error unwinding leaked or double-closed a socket")


class CodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="dln-codec-")
        cls.api = build_fixture("dln_codec", Path(cls.directory.name))
        signatures = {
            "read_integer": (Parsed, [U8P, C.c_size_t, C.c_size_t, C.c_size_t, C.c_bool]),
            "write_integer": (C.c_int32, [U8P, C.c_size_t, C.c_size_t, C.c_size_t, C.c_uint64, C.c_bool]),
            "read_varint": (Parsed, [U8P, C.c_size_t, C.c_size_t, C.c_bool]),
            "write_varint": (Parsed, [U8P, C.c_size_t, C.c_size_t, C.c_uint64]),
            "zigzag_encode": (C.c_uint64, [C.c_int64]), "zigzag_decode": (C.c_int64, [C.c_uint64]),
            "frame_new": (Decoder, [C.c_uint32]),
            "frame_feed": (Step, [C.POINTER(Decoder), U8P, C.c_size_t, C.c_size_t]),
            "frame_finish": (C.c_int32, [C.POINTER(Decoder)]), "frame_reset": (None, [C.POINTER(Decoder)]),
        }
        for name, (result, args) in signatures.items():
            function = getattr(cls.api, "codec_" + name)
            function.restype, function.argtypes = result, args
            setattr(cls, name, staticmethod(function))
        cls.api.dlm_native_allocations.restype = C.c_uint64
        cls.api.dlm_native_live_allocations.restype = C.c_uint64

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_big_and_little_endian_against_python_integers(self):
        rng = random.Random(135001)
        for _ in range(2000):
            width, offset = rng.randrange(1, 9), rng.randrange(0, 8)
            value = rng.getrandbits(width * 8)
            for little in (False, True):
                target = base.octets(b"\xa5" * 24)
                self.assertEqual(self.write_integer(target, 24, offset, width, value, little), 0)
                expected = value.to_bytes(width, "little" if little else "big")
                self.assertEqual(bytes(target[offset:offset + width]), expected)
                result = self.read_integer(target, 24, offset, width, little)
                self.assertEqual((result.status, result.value, result.next), (0, value, offset + width))

    def test_integer_invalid_and_short_writes_are_unchanged(self):
        for little in (False, True):
            for offset, width, value, status in ((9, 1, 0, 4), (7, 2, 1, 1), (0, 0, 0, 4), (0, 9, 0, 4), (0, 1, 256, 7), (0xffffffffffffffff, 1, 0, 4)):
                target = base.octets(b"\xa5" * 8)
                self.assertEqual(self.write_integer(target, 8, offset, width, value, little), status)
                self.assertEqual(bytes(target), b"\xa5" * 8)

    def test_uleb128_random_wire_and_all_prefixes(self):
        rng = random.Random(135002)
        values = [0, 127, 128, 16383, 16384, (1 << 63), (1 << 64) - 1] + [rng.getrandbits(64) for _ in range(3000)]
        for value in values:
            number, encoded = value, bytearray()
            while number > 127:
                encoded.append((number & 127) | 128)
                number >>= 7
            encoded.append(number)
            target = base.octets(b"\xa5" * 16)
            result = self.write_varint(target, 16, 2, value)
            self.assertEqual((result.status, result.next, bytes(target[2:result.next])), (0, len(encoded) + 2, bytes(encoded)))
            decoded = self.read_varint(target, result.next, 2, True)
            self.assertEqual((decoded.status, decoded.value), (0, value))
            for prefix in range(len(encoded)):
                self.assertEqual(self.read_varint(target, prefix + 2, 2, True).status, 1)

    def test_varint_overflow_noncanonical_and_no_partial_write(self):
        for raw in (b"\xff" * 10, b"\x80" * 10, b"\x80" * 9 + b"\x02"):
            source = base.octets(raw)
            self.assertEqual(self.read_varint(source, len(source), 0, True).status, 7)
        source = base.octets(b"\x80\x00")
        self.assertEqual(self.read_varint(source, 2, 0, True).status, 4)
        accepted = self.read_varint(source, 2, 0, False)
        self.assertEqual((accepted.status, accepted.value, accepted.next), (0, 0, 2))
        target = base.octets(b"\xa5" * 9)
        self.assertEqual(self.write_varint(target, 9, 0, (1 << 64) - 1).status, 1)
        self.assertEqual(bytes(target), b"\xa5" * 9)

    def test_zigzag_signed_boundaries_and_random_oracle(self):
        rng = random.Random(135003)
        values = [-(1 << 63), (1 << 63) - 1, -1, 0, 1] + [rng.randrange(-(1 << 63), (1 << 63)) for _ in range(4000)]
        for value in values:
            expected = ((value << 1) ^ (value >> 63)) & ((1 << 64) - 1)
            self.assertEqual(self.zigzag_encode(value), expected)
            self.assertEqual(self.zigzag_decode(expected), value)

    def test_streaming_fragmentation_coalescing_and_zero_frames(self):
        rng = random.Random(135004)
        messages = [b"", b"\0\xff\x80", b"", bytes(range(256)) * 4096, b"tail"]
        wire = b"".join(len(value).to_bytes(4, "big") + value for value in messages)
        state = self.frame_new(1048576)
        decoded, current = [], bytearray()
        total = 0
        allocations = self.api.dlm_native_allocations()
        while total < len(wire):
            raw = wire[total:total + rng.randrange(1, 513)]
            source = base.octets(raw)
            offset = 0
            while offset < len(raw):
                step = self.frame_feed(C.byref(state), source, len(raw), offset)
                self.assertIn(step.status, (0, 1))
                self.assertGreater(step.consumed, 0)
                self.assertLessEqual(step.payload_offset + step.payload_count, len(raw))
                current.extend(raw[step.payload_offset:step.payload_offset + step.payload_count])
                offset += step.consumed
                if step.complete:
                    decoded.append(bytes(current))
                    current.clear()
            total += len(raw)
        self.assertEqual(decoded, messages)
        self.assertEqual(self.frame_finish(C.byref(state)), 0)
        self.assertEqual(C.sizeof(Decoder), 20)
        self.assertEqual(self.api.dlm_native_allocations(), allocations, "frame decoder allocated native heap memory")
        self.assertEqual(self.api.dlm_native_live_allocations(), 0)

    def test_streaming_truncation_poison_reset_and_forged_state(self):
        for raw in (b"\0", b"\0\0", b"\0\0\0", b"\0\0\0\x03x"):
            state, source = self.frame_new(32), base.octets(raw)
            self.frame_feed(C.byref(state), source, len(raw), 0)
            self.assertEqual(self.frame_finish(C.byref(state)), 1)
        state, source = self.frame_new(256), base.octets((257).to_bytes(4, "big"))
        rejected = self.frame_feed(C.byref(state), source, 4, 0)
        self.assertEqual((rejected.status, rejected.consumed), (7, 4))
        self.assertEqual(self.frame_feed(C.byref(state), None, 0, 0).status, 7)
        self.frame_reset(C.byref(state))
        self.assertEqual(self.frame_finish(C.byref(state)), 0)
        for invalid in (Decoder(1, 100, 4, 100, 0), Decoder(1, 0, 5, 0, 0), Decoder(1, 256, 1, 0, 0), Decoder(1, 0, 0, 0, 99)):
            self.assertEqual(self.frame_feed(C.byref(invalid), None, 0, 0).status, 4)
            self.assertEqual(self.frame_finish(C.byref(invalid)), 4)
        valid = self.frame_new(32)
        before = bytes(valid)
        self.assertEqual(self.frame_feed(C.byref(valid), None, 0, (1 << 64) - 1).status, 4)
        self.assertEqual(bytes(valid), before)


class CompatibilityTests(unittest.TestCase):
    def test_actual_v134_baseline_is_preserved(self):
        result = compat.check(ROOT, ROOT / "docs/DLN_BASELINE_0.134.0.json")
        self.assertEqual(result["status"], "passed", result["failures"])
        self.assertGreaterEqual(result["protected_files"], 130)
        self.assertGreaterEqual(result["preserved_c_symbols"], 39)

    def test_compatibility_gate_rejects_tampering_and_accepts_additions(self):
        with tempfile.TemporaryDirectory(prefix="dln-compat-") as directory:
            root = Path(directory)
            stable = root / "stable.dlm"
            stable.write_bytes(b"module stable\n")
            source = root / "net.c"
            original = 'int dlm_net_fixture(int x) { const char *s="}"; /* { */ return x+1; }\n'
            source.write_text(original)
            data = {"format": "DLM-DLN-COMPAT-v1", "baseline_version": "0.134.0", "protected_files": {"stable.dlm": compat.digest(stable.read_bytes())}, "network_c_source": "net.c", "network_c_abi": compat.c_functions(original)}
            snapshot = root / "baseline.json"
            snapshot.write_text(json.dumps(data))
            self.assertEqual(compat.check(root, snapshot)["status"], "passed")
            source.write_text(original + "int dlm_net_added(void) { return 0; }\n")
            self.assertEqual(compat.check(root, snapshot)["additional_c_symbols"], 1)
            for changed in (original.replace("int x", "long x"), original.replace("x+1", "x+2"), ""):
                source.write_text(changed)
                self.assertEqual(compat.check(root, snapshot)["status"], "failed")
            source.write_text(original)
            stable.write_bytes(b"module changed\n")
            self.assertEqual(compat.check(root, snapshot)["status"], "failed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
