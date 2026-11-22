#!/usr/bin/env python3
"""Scoped I/O, bounded producer commands, OS peers and structured cleanup."""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location("scope_reactor_base", HERE / "dln_reactor.py")
reactor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reactor)
base = reactor.base


class Statistics(C.Structure):
    _fields_ = [("reactor", reactor.Statistics), ("queued", C.c_uint32),
                ("queue_limit", C.c_uint32), ("kernel_pending", C.c_uint32)]


class ScopeTests(unittest.TestCase):
    force_poll = False
    setUp = reactor.ReactorTests.setUp
    pair = reactor.ReactorTests.pair
    submit = reactor.ReactorTests.submit
    done = reactor.ReactorTests.done
    ack = reactor.ReactorTests.ack

    @classmethod
    def setUpClass(cls):
        reactor.ReactorTests.setUpClass.__func__(cls)
        signatures = {
            "async_new": (base.Opened, [C.c_uint32, C.c_uint32, C.c_size_t, C.c_uint32]),
            "async_close": (None, [C.c_void_p]),
            "async_adopt": (reactor.Request, [C.c_void_p, base.Opened, C.c_uint32]),
            "async_connect": (reactor.Request, [C.c_void_p, base.Address, C.c_uint32]),
            "async_waker": (base.Opened, [C.c_void_p, C.c_uint64, C.c_uint32]),
            "async_waker_close": (None, [C.c_void_p]),
            "async_wake": (base.Io, [C.c_void_p]),
            "async_join": (base.Io, [C.c_void_p, C.c_uint32, C.c_void_p]),
            "async_stats": (Statistics, [C.c_void_p]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(cls.api, "dlm_net_" + name)
            function.restype, function.argtypes = result, arguments
            setattr(cls, name, staticmethod(function))
        cls.reactor_close = staticmethod(cls.async_close)
        cls.reactor_adopt = staticmethod(cls.async_adopt)
        cls.reactor_connect = staticmethod(cls.async_connect)
        cls.reactor_stats = staticmethod(lambda group: cls.async_stats(group).reactor)

    @classmethod
    def tearDownClass(cls):
        reactor.ReactorTests.tearDownClass.__func__(cls)

    def tearDown(self):
        for group in self.groups:
            self.async_close(group)
        for waker in getattr(self, "wakers", []):
            self.async_waker_close(waker)
        for peer in self.peers:
            peer.close()
        self.assertEqual(self.provider_shutdown().status, 0, "scope/socket/waker owner leaked")

    def group(self, channels=16, operations=32, byte_limit=8 * 1024 * 1024, wake_limit=32):
        result = self.async_new(channels, operations, byte_limit, wake_limit)
        self.assertEqual(result.status, 0, result.system)
        self.groups.append(result.handle)
        return result.handle

    def waker(self, group, operation, action=1):
        result = self.async_waker(group, operation, action)
        self.assertEqual(result.status, 0, result.system)
        if not hasattr(self, "wakers"):
            self.wakers = []
        self.wakers.append(result.handle)
        return result.handle

    # The same independent peer contracts apply to both ownership surfaces.
    test_payload_backpressure = reactor.ReactorTests.test_backpressure_counts_pending_and_unacknowledged_results
    test_fragmented_read_and_copy = reactor.ReactorTests.test_fragmented_exact_read_and_result_copy_boundaries
    test_partial_timeout_eof = reactor.ReactorTests.test_partial_read_timeout_and_eof_preserve_progress
    test_legacy_cancel_token = reactor.ReactorTests.test_cancel_before_during_and_after_completion
    test_stale_and_foreign_keys = reactor.ReactorTests.test_stale_and_cross_reactor_keys_cannot_alias_new_resources
    test_connect_accept_exchange = reactor.ReactorTests.test_async_connect_accept_and_binary_exchange
    test_refused_connect = reactor.ReactorTests.test_connect_refusal_is_terminal_and_closes_failed_channel
    test_udp_boundaries_and_peer = reactor.ReactorTests.test_udp_zero_length_truncation_peer_and_send
    test_channel_close_retains_results = reactor.ReactorTests.test_channel_close_cancels_and_retains_result_until_ack
    test_completion_fifo_and_reuse = reactor.ReactorTests.test_completion_fifo_out_of_order_ack_and_reused_slot
    test_random_lifecycle = reactor.ReactorTests.test_random_operation_lifecycle_and_bounded_counters
    test_adoption_and_invalid_arguments = reactor.ReactorTests.test_failed_adoption_consumes_input_and_null_arguments_fail
    test_zero_timeout = reactor.ReactorTests.test_zero_timeout_progress_and_signal_after_deadline

    def test_backend_and_queue_configuration(self):
        self.assertEqual(self.async_new(1, 1, 0, 0).status, 4)
        self.assertEqual(self.async_new(1, 1, 0, 4097).status, 4)
        group = self.group(wake_limit=7)
        stats = self.async_stats(group)
        expected = 5 if os.name == "nt" else 4 if self.force_poll else 1 if os.uname().sysname == "Linux" else 2
        self.assertEqual((stats.reactor.backend, stats.queue_limit, stats.queued, stats.kernel_pending), (expected, 7, 0, 0))

    def test_zero_capacity_udp_distinguishes_empty_and_truncated(self):
        group = self.group(1, 2, 0)
        opened = self.bind_address(reactor.address(0), 3, 0, False)
        self.assertEqual(opened.status, 0)
        port = self.local(opened.handle).address.port
        channel = self.reactor_adopt(group, opened, 3).channel
        import socket
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            for payload, status in [(b"", 0), (b"nonempty", 10)]:
                request = self.submit(group, channel, 5, 0)
                peer.sendto(payload, ("127.0.0.1", port))
                result = self.done(group, request)
                self.assertEqual((result.status, result.count), (status, 0))
                self.ack(group, request)

    def test_copied_write_against_slow_peer(self):
        group = self.group()
        channel, peer = self.pair(group, send_buffer=8192)
        expected = bytes(range(256)) * 8192
        raw = base.octets(expected)
        request = self.reactor_submit(group, channel, 2, raw, len(raw), True, 5000, base.Address())
        self.assertEqual(request.status, 0)
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

        thread = threading.Thread(target=drain)
        thread.start()
        result = self.done(group, request)
        thread.join(6)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertEqual((result.status, result.count, bytes(received)), (0, len(expected), expected))

    def test_bounded_queue_coalesces_without_losing_commands(self):
        group = self.group(wake_limit=1)
        first, second = [self.reactor_timer(group, 2000, True) for _ in range(2)]
        a, b = self.waker(group, first.id), self.waker(group, second.id)
        self.assertEqual((self.async_wake(a).count, self.async_wake(a).count), (1, 0))
        self.assertEqual(self.async_wake(b).status, 7)
        self.assertEqual(self.async_stats(group).queued, 1)
        self.assertEqual(self.async_join(group, 0, None).status, 3)
        self.assertEqual(self.reactor_result(group, first.id).status, 0)
        self.assertEqual(self.async_wake(b).status, 0)
        self.assertEqual(self.async_join(group, 1000, None).count, 2)

    def test_foreign_thread_wakes_blocking_poll(self):
        group = self.group()
        event = self.reactor_timer(group, 2000, True)
        token = self.waker(group, event.id)
        outcomes = []
        thread = threading.Timer(0.02, lambda: outcomes.append(self.async_wake(token).status))
        thread.start()
        begin = time.monotonic()
        try:
            self.assertEqual(self.reactor_poll(group, 1500, None).status, 0)
        finally:
            thread.join(2)
        self.assertLess(time.monotonic() - begin, 1)
        self.assertEqual(outcomes, [0])
        self.assertEqual(self.reactor_result(group, event.id).status, 0)

    def test_operation_cancel_wakes_join_with_retained_completion(self):
        group = self.group()
        channel, _ = self.pair(group)
        read = self.submit(group, channel, 1, 64)
        self.reactor_timer(group, 0, False)
        token = self.waker(group, read.id, 2)
        thread = threading.Timer(0.02, self.async_wake, args=(token,))
        thread.start()
        try:
            result = self.async_join(group, 1500, None)
        finally:
            thread.join(2)
        self.assertEqual((result.status, result.count), (0, 2))
        self.assertEqual(self.reactor_result(group, read.id).status, 9)
        self.assertEqual(self.async_stats(group).kernel_pending, 0)

    def test_closed_scope_rejects_retained_token(self):
        group = self.group()
        token = self.waker(group, 0, 3)
        self.async_close(group)
        self.groups.remove(group)
        self.assertEqual(self.async_wake(token).status, 9)
        self.assertEqual(self.provider_shutdown().status, 4)

    def test_stale_token_cannot_signal_reused_operation(self):
        group = self.group(1, 1)
        old = self.reactor_timer(group, 2000, True)
        token = self.waker(group, old.id)
        self.reactor_cancel(group, old.id)
        self.ack(group, old)
        current = self.reactor_timer(group, 2000, True)
        self.assertNotEqual(old.id, current.id)
        self.assertEqual(self.async_wake(token).status, 0)
        self.assertEqual(self.async_join(group, 0, None).status, 3)
        self.assertEqual(self.reactor_result(group, current.id).status, 1)

    def test_timely_queued_signal_survives_late_owner_and_duplicate(self):
        group = self.group()
        event = self.reactor_timer(group, 20, True)
        token = self.waker(group, event.id)
        self.assertEqual(self.async_wake(token).count, 1)
        time.sleep(0.04)
        self.assertEqual(self.async_wake(token).count, 0)
        self.assertEqual(self.async_join(group, 0, None).status, 0)
        self.assertEqual(self.reactor_result(group, event.id).status, 0)

    def test_late_signal_keeps_timeout(self):
        group = self.group()
        event = self.reactor_timer(group, 1, True)
        token = self.waker(group, event.id)
        time.sleep(0.01)
        self.assertEqual(self.async_wake(token).status, 0)
        self.assertEqual(self.async_join(group, 0, None).status, 0)
        self.assertEqual(self.reactor_result(group, event.id).status, 3)

    def test_many_producers_and_cancel_all(self):
        group = self.group(1, 32, 0, 32)
        operations = [self.reactor_timer(group, 3000, True) for _ in range(32)]
        tokens = [self.waker(group, operation.id) for operation in operations]
        outcomes = []
        threads = [threading.Thread(target=lambda token=token: outcomes.append(self.async_wake(token).status)) for token in tokens]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(2)
            self.assertFalse(thread.is_alive())
        self.assertEqual(outcomes, [0] * 32)
        self.assertEqual(self.async_join(group, 1000, None).count, 32)
        for operation in operations:
            self.assertEqual(self.reactor_result(group, operation.id).status, 0)
            self.ack(group, operation)
        pending = [self.reactor_timer(group, 3000, True) for _ in range(4)]
        stop = self.waker(group, 0, 3)
        self.assertEqual(self.async_wake(stop).status, 0)
        self.assertEqual(self.async_join(group, 1000, None).count, 4)
        for operation in pending:
            self.assertEqual(self.reactor_result(group, operation.id).status, 9)

    def test_token_kind_validation_and_join_timeout_preserves_work(self):
        group = self.group()
        timer = self.reactor_timer(group, 2000, False)
        self.assertEqual(self.async_waker(group, timer.id, 1).status, 4)
        self.assertEqual(self.async_waker(group, timer.id, 3).status, 4)
        self.assertEqual(self.async_waker(group, 0, 2).status, 4)
        self.assertEqual(self.async_waker(group, 0, 4).status, 4)
        self.assertEqual(self.async_join(group, 0, None).status, 3)
        self.assertEqual(self.reactor_result(group, timer.id).status, 1)
        token = self.waker(group, timer.id, 2)
        self.assertEqual(self.async_wake(token).status, 0)
        self.assertEqual(self.async_join(group, 0, None).status, 0)
        self.assertEqual(self.reactor_result(group, timer.id).status, 9)


class PortablePollScopeTests(ScopeTests):
    force_poll = True


class DlmScopeTests(unittest.TestCase):
    def test_runtime_failure_releases_partially_moved_scope_and_token(self):
        compiler = os.environ.get("DLM_NETWORK_DLM", str(ROOT / "target/release" / ("dlm.exe" if os.name == "nt" else "dlm")))
        with tempfile.TemporaryDirectory(prefix="dln-scoped-owned-") as directory:
            output = Path(directory) / ("owned.dll" if os.name == "nt" else "owned.so")
            command = [compiler, "build", str(HERE / "dln_async.dlm"), "--lib", str(ROOT / "lib"), "--allow-unsafe", "--target", "sharedlib", "-o", str(output)]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            api = C.CDLL(str(output))
            api.scope_abort_with_pending_buffers_and_waker.restype = None
            api.scope_shutdown.restype = base.Io
            api.dlm_native_last_error.restype = C.c_char_p
            api.dlm_native_live_allocations.restype = C.c_uint64
            for _ in range(16):
                api.scope_abort_with_pending_buffers_and_waker()
                self.assertTrue(api.dlm_native_last_error())
                self.assertEqual(api.dlm_native_live_allocations(), 0)
                self.assertEqual(api.scope_shutdown().status, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
