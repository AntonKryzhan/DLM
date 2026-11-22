#!/usr/bin/env python3
"""Owned reactor acceptance against independent OS peers, native and poll backends."""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import random
import socket
import subprocess
import tempfile
import threading
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location("reactor_network_base", HERE / "network.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class Request(C.Structure):
    _fields_ = [("id", C.c_uint64), ("channel", C.c_uint64), ("status", C.c_int32), ("system", C.c_int32)]


class Completion(C.Structure):
    _fields_ = [("id", C.c_uint64), ("channel", C.c_uint64), ("accepted", C.c_uint64), ("count", C.c_size_t),
                ("status", C.c_int32), ("system", C.c_int32), ("kind", C.c_uint32), ("peer", base.Address)]


class Statistics(C.Structure):
    _fields_ = [("buffered_bytes", C.c_size_t), ("byte_limit", C.c_size_t), ("channels", C.c_uint32),
                ("pending", C.c_uint32), ("completed", C.c_uint32), ("channel_limit", C.c_uint32),
                ("operation_limit", C.c_uint32), ("backend", C.c_uint32), ("status", C.c_int32), ("system", C.c_int32)]


class Options(C.Structure):
    _fields_ = [("no_delay", C.c_uint32), ("keep_alive", C.c_uint32), ("send_buffer", C.c_uint32), ("receive_buffer", C.c_uint32)]


def address(port):
    result = base.Address(4, port, 0)
    result.bytes[:4] = (127, 0, 0, 1)
    return result


class ReactorTests(unittest.TestCase):
    force_poll = False

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="dln-reactor-")
        cls.path = Path(cls.directory.name)
        library = cls.path / ("reactor.dll" if os.name == "nt" else "reactor.so")
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-Wno-misleading-indentation", "-shared", str(ROOT / "crates/dlm_core/src/native/network.c"), "-o", str(library)]
        command += ["-lws2_32"] if os.name == "nt" else ["-fPIC", "-ldl"]
        if cls.force_poll:
            command.append("-DDLM_REACTOR_FORCE_POLL")
        completed = subprocess.run(command, capture_output=True, text=True)
        if completed.returncode:
            raise RuntimeError(completed.stdout + completed.stderr)
        cls.api = C.CDLL(str(library))
        signatures = {
            "reactor_new": (base.Opened, [C.c_uint32, C.c_uint32, C.c_size_t]),
            "reactor_close": (None, [C.c_void_p]),
            "reactor_adopt": (Request, [C.c_void_p, base.Opened, C.c_uint32]),
            "reactor_submit": (Request, [C.c_void_p, C.c_uint64, C.c_uint32, base.U8P, C.c_size_t, C.c_bool, C.c_uint32, base.Address]),
            "reactor_connect": (Request, [C.c_void_p, base.Address, C.c_uint32]),
            "reactor_timer": (Request, [C.c_void_p, C.c_uint32, C.c_bool]),
            "reactor_signal": (base.Io, [C.c_void_p, C.c_uint64]),
            "reactor_cancel": (base.Io, [C.c_void_p, C.c_uint64]),
            "reactor_poll": (base.Io, [C.c_void_p, C.c_uint32, C.c_void_p]),
            "reactor_result": (Completion, [C.c_void_p, C.c_uint64]),
            "reactor_copy": (base.Io, [C.c_void_p, C.c_uint64, C.c_size_t, base.U8P, C.c_size_t]),
            "reactor_ack": (base.Io, [C.c_void_p, C.c_uint64]),
            "reactor_channel_close": (base.Io, [C.c_void_p, C.c_uint64]),
            "reactor_release": (base.Opened, [C.c_void_p, C.c_uint64, C.c_uint32]),
            "reactor_stats": (Statistics, [C.c_void_p]),
            "connect_address": (base.Opened, [base.Address, C.c_uint32]),
            "bind_address": (base.Opened, [base.Address, C.c_uint32, C.c_uint32, C.c_bool]),
            "local": (base.Resolved, [C.c_void_p]), "close": (None, [C.c_void_p]),
            "configure": (base.Io, [C.c_void_p, Options]),
            "recv": (base.Io, [C.c_void_p, base.U8P, C.c_size_t]),
            "provider_shutdown": (base.Io, []),
            "cancel_new": (C.c_void_p, []), "cancel_signal": (None, [C.c_void_p]), "cancel_close": (None, [C.c_void_p]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(cls.api, "dlm_net_" + name)
            function.restype, function.argtypes = result, arguments
            setattr(cls, name, staticmethod(function))

    @classmethod
    def tearDownClass(cls):
        if cls.provider_shutdown().status != 0:
            raise AssertionError("reactor acceptance leaked an owner")
        cls.directory.cleanup()

    def setUp(self):
        self.groups, self.peers = [], []

    def tearDown(self):
        for group in self.groups:
            self.reactor_close(group)
        for peer in self.peers:
            peer.close()
        self.assertEqual(self.provider_shutdown().status, 0, "reactor/socket owner leaked")

    def group(self, channels=16, operations=32, byte_limit=8 * 1024 * 1024):
        result = self.reactor_new(channels, operations, byte_limit)
        self.assertEqual(result.status, 0, result.system)
        self.groups.append(result.handle)
        return result.handle

    def pair(self, group, send_buffer=0):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(2)
            opened = self.connect_address(address(listener.getsockname()[1]), 1000)
            self.assertEqual(opened.status, 0, opened.system)
            if send_buffer:
                self.assertEqual(self.configure(opened.handle, Options(1, 0, send_buffer, 0)).status, 0)
            peer, _ = listener.accept()
        peer.settimeout(3)
        self.peers.append(peer)
        adopted = self.reactor_adopt(group, opened, 1)
        self.assertEqual(adopted.status, 0)
        return adopted.channel, peer

    def submit(self, group, channel, kind, count=0, payload=None, exact=True, timeout=2000, peer=None):
        raw = base.octets(payload) if payload is not None else None
        if payload is not None:
            count = len(payload)
        return self.reactor_submit(group, channel, kind, raw, count, exact, timeout, peer or base.Address())

    def done(self, group, operation):
        self.assertEqual(operation.status, 0, operation.system)
        for _ in range(100):
            result = self.reactor_result(group, operation.id)
            if result.status != 1:
                return result
            result = self.reactor_poll(group, 2000, None)
            self.assertIn(result.status, (0, 3), result.system)
        self.fail("operation did not reach terminal state")

    def ack(self, group, operation):
        self.assertEqual(self.reactor_ack(group, operation.id).status, 0)

    def test_configuration_backend_and_empty_poll(self):
        for limits in ((0, 1, 0), (1, 0, 0), (4097, 1, 0), (1, 4097, 0), (1, 1, 67108865)):
            self.assertEqual(self.reactor_new(*limits).status, 4)
        group = self.group(1, 2, 0)
        stats = self.reactor_stats(group)
        expected = 3 if os.name == "nt" else 4 if self.force_poll else 1 if os.uname().sysname == "Linux" else 2
        self.assertEqual((stats.backend, stats.pending, stats.completed, stats.buffered_bytes), (expected, 0, 0, 0))
        self.assertEqual(self.reactor_poll(group, 1000, None).status, 1)
        self.assertEqual(self.provider_shutdown().status, 4)

    def test_backpressure_counts_pending_and_unacknowledged_results(self):
        group = self.group(1, 2, 8)
        channel, peer = self.pair(group)
        read = self.submit(group, channel, 1, 8)
        timer = self.reactor_timer(group, 0, False)
        self.assertEqual((read.status, timer.status), (0, 0))
        self.assertEqual(self.submit(group, channel, 1, 1).status, 1)
        self.assertEqual(self.reactor_timer(group, 0, False).status, 7)
        self.assertEqual(self.reactor_ack(group, read.id).status, 4)
        self.assertEqual(self.reactor_cancel(group, read.id).count, 1)
        self.ack(group, timer)
        self.assertEqual(self.submit(group, channel, 2, payload=b"x").status, 7)
        self.assertEqual(self.reactor_stats(group).buffered_bytes, 8)
        self.ack(group, read)
        write = self.submit(group, channel, 2, payload=b"abcdefgh")
        self.assertEqual(self.done(group, write).count, 8)
        self.assertEqual(peer.recv(8), b"abcdefgh")
        self.assertEqual(self.reactor_stats(group).buffered_bytes, 8)
        self.ack(group, write)
        self.assertEqual(self.reactor_stats(group).buffered_bytes, 0)

    def test_fragmented_exact_read_and_result_copy_boundaries(self):
        group = self.group()
        channel, peer = self.pair(group)
        read = self.submit(group, channel, 1, 8)
        peer.sendall(b"\x00a")
        self.reactor_poll(group, 0, None)
        partial = self.reactor_result(group, read.id)
        self.assertEqual((partial.status, partial.count), (1, 2))
        target = base.octets(b"\xa5" * 16)
        self.assertEqual(self.reactor_copy(group, read.id, 0, target, 16).status, 4)
        self.assertEqual(bytes(target), b"\xa5" * 16)
        peer.sendall(b"bc\xffdef")
        result = self.done(group, read)
        self.assertEqual((result.status, result.count), (0, 8))
        copied = self.reactor_copy(group, read.id, 2, target, 4)
        self.assertEqual((copied.status, copied.count, bytes(target[:4])), (0, 4, b"bc\xffd"))
        self.assertEqual(bytes(target[4:]), b"\xa5" * 12)
        self.assertEqual(self.reactor_copy(group, read.id, 9, target, 4).status, 4)
        self.ack(group, read)
        self.assertEqual(self.reactor_copy(group, read.id, 0, target, 4).status, 4)

    def test_partial_read_timeout_and_eof_preserve_progress(self):
        group = self.group()
        channel, peer = self.pair(group)
        peer.sendall(b"abc")
        read = self.submit(group, channel, 1, 8, timeout=30)
        result = self.done(group, read)
        self.assertEqual((result.status, result.count), (3, 3))
        self.ack(group, read)
        peer.sendall(b"xy")
        peer.shutdown(socket.SHUT_WR)
        read = self.submit(group, channel, 1, 8)
        result = self.done(group, read)
        self.assertEqual((result.status, result.count), (2, 2))
        self.ack(group, read)

    def test_source_buffer_is_copied_before_partial_async_write(self):
        group = self.group()
        channel, peer = self.pair(group, send_buffer=8192)
        expected = bytes(range(256)) * 8192
        raw = base.octets(expected)
        request = self.reactor_submit(group, channel, 2, raw, len(raw), True, 5000, base.Address())
        self.assertEqual(request.status, 0)
        first = self.reactor_result(group, request.id)
        self.assertEqual(first.status, 1)
        self.assertGreater(first.count, 0)
        self.assertLess(first.count, len(expected))
        C.memset(raw, 0xa5, len(raw))
        received, errors = bytearray(), []

        def drain():
            try:
                while len(received) < len(expected):
                    chunk = peer.recv(65536)
                    if not chunk:
                        break
                    received.extend(chunk)
            except OSError as error:
                errors.append(error)

        thread = threading.Thread(target=drain, daemon=True)
        thread.start()
        result = self.done(group, request)
        thread.join(timeout=6)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertEqual((result.status, result.count), (0, len(expected)))
        self.assertEqual(bytes(received), expected)
        self.ack(group, request)

    def test_cancel_before_during_and_after_completion(self):
        group = self.group()
        channel, peer = self.pair(group)
        peer.sendall(b"ab")
        read = self.submit(group, channel, 1, 8)
        token = self.cancel_new()
        self.assertTrue(token)
        timer = threading.Timer(0.02, self.cancel_signal, args=(token,))
        try:
            timer.start()
            started = time.monotonic()
            wait = self.reactor_poll(group, 1000, token)
            elapsed = time.monotonic() - started
            timer.join()
            self.assertEqual(wait.status, 9)
            self.assertLess(elapsed, 0.75)
            result = self.reactor_result(group, read.id)
            self.assertEqual((result.status, result.count), (9, 2))
            self.ack(group, read)
            pending = self.reactor_timer(group, 1000, False)
            self.assertEqual(self.reactor_poll(group, 1000, token).status, 9)
            self.assertEqual(self.reactor_result(group, pending.id).status, 9)
            self.ack(group, pending)
        finally:
            timer.join()
            self.cancel_close(token)
        completed = self.reactor_timer(group, 0, False)
        self.assertEqual(self.reactor_cancel(group, completed.id).count, 0)
        self.assertEqual(self.reactor_result(group, completed.id).status, 0)
        self.ack(group, completed)

    def test_stale_and_cross_reactor_keys_cannot_alias_new_resources(self):
        first, second = self.group(1, 2), self.group(1, 2)
        channel, _ = self.pair(first)
        other, _ = self.pair(second)
        self.assertNotEqual(channel, other)
        self.assertEqual(self.submit(second, channel, 1, 1).status, 4)
        old = self.reactor_timer(first, 0, False)
        self.assertEqual(self.reactor_cancel(second, old.id).status, 4)
        self.ack(first, old)
        new = self.reactor_timer(first, 0, False)
        self.assertNotEqual(old.id, new.id)
        self.assertEqual(self.reactor_ack(first, old.id).status, 4)
        self.ack(first, new)
        self.assertEqual(self.reactor_channel_close(first, channel).status, 0)
        replacement, _ = self.pair(first)
        self.assertNotEqual(channel, replacement)
        self.assertEqual(self.submit(first, channel, 1, 1).status, 4)
        self.assertEqual(self.reactor_channel_close(first, channel).status, 4)

    def test_release_waits_for_ack_then_restores_independent_owner(self):
        group = self.group()
        channel, peer = self.pair(group)
        read = self.submit(group, channel, 1, 8)
        self.assertEqual(self.reactor_release(group, channel, 1).status, 1)
        self.reactor_cancel(group, read.id)
        self.assertEqual(self.reactor_release(group, channel, 1).status, 1)
        self.ack(group, read)
        released = self.reactor_release(group, channel, 1)
        self.assertEqual(released.status, 0)
        try:
            self.assertEqual(self.submit(group, channel, 1, 1).status, 4)
            self.reactor_close(group)
            self.groups.remove(group)
            peer.sendall(b"live")
            output = base.octets(b"\xa5" * 8)
            result = self.recv(released.handle, output, 8)
            self.assertEqual((result.status, result.count, bytes(output[:4])), (0, 4, b"live"))
        finally:
            self.close(released.handle)

    def test_async_connect_accept_and_binary_exchange(self):
        group = self.group(4, 8)
        opened = self.bind_address(address(0), 2, 16, False)
        self.assertEqual(opened.status, 0)
        local = self.local(opened.handle)
        listening = self.reactor_adopt(group, opened, 2)
        accepting = self.submit(group, listening.channel, 3)
        connecting = self.reactor_connect(group, local.address, 2000)
        completed = {}
        while len(completed) < 2:
            self.assertEqual(self.reactor_poll(group, 2000, None).status, 0)
            result = self.reactor_result(group, 0)
            self.assertEqual(result.status, 0, result.system)
            completed[result.kind] = result
            self.assertEqual(self.reactor_ack(group, result.id).status, 0)
        self.assertEqual(set(completed), {3, 8})
        self.assertEqual(completed[3].id, accepting.id)
        self.assertEqual(completed[8].id, connecting.id)
        server = completed[3].accepted
        read = self.submit(group, server, 1, 4)
        write = self.submit(group, connecting.channel, 2, payload=b"\x00\xff\x80x")
        self.assertEqual(self.done(group, write).status, 0)
        self.ack(group, write)
        self.assertEqual(self.done(group, read).count, 4)
        target = base.octets(b"????")
        self.assertEqual(self.reactor_copy(group, read.id, 0, target, 4).count, 4)
        self.assertEqual(bytes(target), b"\x00\xff\x80x")
        self.ack(group, read)
        self.assertEqual(self.reactor_stats(group).channels, 3)

    def test_connect_refusal_is_terminal_and_closes_failed_channel(self):
        group = self.group()
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]
        request = self.reactor_connect(group, address(port), 1000)
        result = self.done(group, request)
        self.assertEqual((result.kind, result.status), (8, 5), result.system)
        self.assertNotEqual(result.system, 0)
        self.assertEqual(self.reactor_stats(group).channels, 0)
        self.assertEqual(self.submit(group, request.channel, 1, 1).status, 4)
        self.ack(group, request)

    def test_udp_zero_length_truncation_peer_and_send(self):
        group = self.group()
        opened = self.bind_address(address(0), 3, 0, False)
        self.assertEqual(opened.status, 0)
        port = self.local(opened.handle).address.port
        channel = self.reactor_adopt(group, opened, 3).channel
        peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        peer.bind(("127.0.0.1", 0))
        peer.settimeout(2)
        self.peers.append(peer)
        self.assertEqual(self.submit(group, channel, 1, 2).status, 4)
        read = self.submit(group, channel, 5, 2)
        peer.sendto(b"abcdef", ("127.0.0.1", port))
        result = self.done(group, read)
        self.assertEqual((result.status, result.count, result.peer.family, result.peer.port), (10, 2, 4, peer.getsockname()[1]))
        target = base.octets(b"????")
        self.assertEqual(self.reactor_copy(group, read.id, 0, target, 4).count, 2)
        self.assertEqual(bytes(target), b"ab??")
        self.ack(group, read)
        zero = self.submit(group, channel, 5, 0)
        peer.sendto(b"", ("127.0.0.1", port))
        result = self.done(group, zero)
        self.assertEqual((result.status, result.count), (0, 0))
        self.ack(group, zero)
        send = self.submit(group, channel, 6, payload=b"", peer=address(peer.getsockname()[1]))
        self.assertEqual(self.done(group, send).status, 0)
        self.assertEqual(peer.recvfrom(8)[0], b"")
        self.ack(group, send)

    def test_channel_close_cancels_and_retains_result_until_ack(self):
        group = self.group(1, 4, 8)
        channel, peer = self.pair(group)
        peer.sendall(b"old")
        read = self.submit(group, channel, 1, 8)
        self.assertEqual(self.reactor_channel_close(group, channel).status, 0)
        replacement, _ = self.pair(group)
        self.assertNotEqual(channel, replacement)
        result = self.reactor_result(group, read.id)
        self.assertEqual((result.status, result.count), (9, 3))
        target = base.octets(b"????")
        self.assertEqual(self.reactor_copy(group, read.id, 0, target, 4).count, 3)
        self.assertEqual(bytes(target), b"old?")
        self.assertEqual(self.reactor_stats(group).buffered_bytes, 8)
        self.ack(group, read)
        self.assertEqual(self.reactor_stats(group).buffered_bytes, 0)

    def test_completion_fifo_out_of_order_ack_and_reused_slot(self):
        group = self.group(1, 4, 0)
        first, middle, last = [self.reactor_timer(group, 2000, True) for _ in range(3)]
        for operation in (last, middle, first):
            self.assertEqual(self.reactor_signal(group, operation.id).status, 0)
        self.assertEqual(self.reactor_result(group, 0).id, last.id)
        self.ack(group, middle)
        reused = self.reactor_timer(group, 0, False)
        self.assertEqual(self.reactor_result(group, 0).id, last.id)
        self.ack(group, last)
        self.assertEqual(self.reactor_result(group, 0).id, first.id)
        self.ack(group, first)
        self.assertEqual(self.reactor_result(group, 0).id, reused.id)
        self.ack(group, reused)
        self.assertEqual(self.reactor_result(group, 0).status, 1)
        event = self.reactor_timer(group, 10, True)
        result = self.done(group, event)
        self.assertEqual(result.status, 3)
        self.ack(group, event)

    def test_random_operation_lifecycle_and_bounded_counters(self):
        group = self.group(1, 7, 0)
        rng, pending, completed, stale = random.Random(136004), {}, [], []
        for _ in range(1200):
            choice = rng.randrange(4)
            if choice == 0:
                request = self.reactor_timer(group, 100000, True)
                if len(pending) + len(completed) == 7:
                    self.assertEqual(request.status, 7)
                else:
                    self.assertEqual(request.status, 0)
                    pending[request.id] = request
            elif choice == 1 and pending:
                key = rng.choice(list(pending))
                if rng.randrange(2):
                    self.assertEqual(self.reactor_cancel(group, key).count, 1)
                else:
                    self.assertEqual(self.reactor_signal(group, key).status, 0)
                completed.append(key)
                del pending[key]
            elif choice == 2 and completed:
                key = rng.choice(completed)
                self.assertEqual(self.reactor_ack(group, key).status, 0)
                completed.remove(key)
                stale.append(key)
            elif stale:
                self.assertEqual(self.reactor_ack(group, rng.choice(stale)).status, 4)
            stats = self.reactor_stats(group)
            self.assertEqual((stats.pending, stats.completed, stats.buffered_bytes), (len(pending), len(completed), 0))
            self.assertEqual(self.reactor_result(group, 0).id, completed[0] if completed else 0)
        self.assertEqual(self.reactor_cancel(group, 0).count, len(pending))
        while self.reactor_stats(group).completed:
            result = self.reactor_result(group, 0)
            self.assertEqual(self.reactor_ack(group, result.id).status, 0)

    def test_failed_adoption_consumes_input_and_null_arguments_fail(self):
        group = self.group(1, 2)
        self.pair(group)
        opened = self.bind_address(address(0), 3, 0, False)
        self.assertEqual(self.reactor_adopt(group, opened, 3).status, 7)
        opened = self.bind_address(address(0), 3, 0, False)
        self.assertEqual(self.reactor_adopt(group, opened, 1).status, 4)
        self.assertEqual(self.reactor_poll(None, 0, None).status, 4)
        self.assertEqual(self.reactor_result(None, 0).status, 4)
        self.assertEqual(self.reactor_ack(group, (1 << 64) - 1).status, 4)
        self.assertEqual(self.reactor_stats(group).channels, 1)

    def test_zero_timeout_progress_and_signal_after_deadline(self):
        group = self.group()
        channel, peer = self.pair(group)
        peer.sendall(b"x")
        request = self.submit(group, channel, 1, 8, timeout=0)
        result = self.reactor_result(group, request.id)
        self.assertEqual((result.status, result.count), (3, 1))
        self.ack(group, request)
        empty = self.submit(group, channel, 2, payload=b"", timeout=0)
        self.assertEqual(self.done(group, empty).status, 0)
        self.ack(group, empty)
        event = self.reactor_timer(group, 2, True)
        time.sleep(0.01)
        self.assertEqual(self.reactor_signal(group, event.id).status, 3)
        self.assertEqual(self.reactor_result(group, event.id).status, 3)
        self.ack(group, event)


class PortablePollTests(ReactorTests):
    force_poll = True


class DlmReactorTests(unittest.TestCase):
    def test_dlm_udp_wrapper_and_error_boundary_release_pending_owners(self):
        compiler = os.environ.get("DLM_NETWORK_DLM", str(ROOT / "target/release" / ("dlm.exe" if os.name == "nt" else "dlm")))
        with tempfile.TemporaryDirectory(prefix="dln-reactor-owned-") as directory:
            output = Path(directory) / ("owned.dll" if os.name == "nt" else "owned.so")
            command = [compiler, "build", str(HERE / "dln_reactor.dlm"), "--lib", str(ROOT / "lib"), "--allow-unsafe", "--target", "sharedlib", "-o", str(output)]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            api = C.CDLL(str(output))
            api.reactor_abort_with_pending_buffers.restype = None
            api.reactor_udp_roundtrip.restype = None
            api.reactor_shutdown.restype = base.Io
            api.dlm_native_last_error.restype = C.c_char_p
            api.dlm_native_live_allocations.restype = C.c_uint64
            for _ in range(8):
                api.reactor_abort_with_pending_buffers()
                self.assertTrue(api.dlm_native_last_error())
                self.assertEqual(api.dlm_native_live_allocations(), 0)
                self.assertEqual(api.reactor_shutdown().status, 0)
            api.reactor_udp_roundtrip()
            self.assertFalse(api.dlm_native_last_error())
            self.assertEqual(api.dlm_native_live_allocations(), 0)
            self.assertEqual(api.reactor_shutdown().status, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
