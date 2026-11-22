#!/usr/bin/env python3
"""Independent RFC and OpenSSL-backed peers for the GnuTLS/Nettle adapter."""
import ctypes as C
import hashlib
import hmac
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import time
import unittest

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location("crypto_network_base", HERE / "network.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class Statistics(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ("sent", "received", "send_bytes", "receive_bytes", "record_limit", "byte_limit", "deadline")]
    _fields_ += [("failed_auth", C.c_uint32), ("status", C.c_int32), ("system", C.c_int32)]


def hkdf(ikm, salt, info, length):
    return HKDF(algorithm=hashes.SHA256(), length=length, salt=salt, info=info).derive(ikm)


def record(ikm, salt, context, sequence, plain, aad=b"", direction=b"c2s"):
    material = hkdf(ikm, salt, b"DLM-CRYPTO-v1/" + direction + b"|" + context, 44)
    nonce = bytes(a ^ b for a, b in zip(material[32:], b"\0" * 4 + sequence.to_bytes(8, "big")))
    header = b"DLC1" + struct.pack(">QI", sequence, len(plain))
    return header + AESGCM(material[:32]).encrypt(nonce, plain, header + aad)


class CryptoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="dln-crypto-")
        path = Path(cls.directory.name) / ("crypto.dll" if os.name == "nt" else "crypto.so")
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-shared", str(ROOT / "crates/dlm_core/src/native/network.c"), "-o", str(path)]
        command += ["-lws2_32"] if os.name == "nt" else ["-fPIC", "-ldl"]
        subprocess.run(command, check=True, capture_output=True, text=True)
        cls.api = C.CDLL(str(path))
        signatures = {
            "capabilities": (C.c_uint32, []), "shutdown": (base.Io, []), "close": (None, [C.c_void_p]),
            "secret": (base.Opened, [base.U8P, C.c_size_t, C.c_uint32, C.c_bool]),
            "hash": (base.Io, [C.c_void_p, base.U8P, C.c_size_t, base.U8P, C.c_size_t]),
            "hmac": (base.Io, [C.c_void_p, base.U8P, C.c_size_t, base.U8P, C.c_size_t]),
            "public": (base.Io, [C.c_void_p, base.U8P, C.c_size_t]),
            "agree": (base.Opened, [base.Opened, base.U8P, C.c_size_t]),
            "hkdf": (base.Opened, [base.Opened, base.U8P, C.c_size_t, base.U8P, C.c_size_t, C.c_size_t]),
            "channel": (base.Opened, [base.Opened, base.U8P, C.c_size_t, base.U8P, C.c_size_t, C.c_uint32, C.c_uint64, C.c_uint64, C.c_uint32]),
            "seal": (base.Io, [C.c_void_p, base.U8P, C.c_size_t, base.U8P, C.c_size_t, base.U8P, C.c_size_t]),
            "open": (base.Io, [C.c_void_p, base.U8P, C.c_size_t, base.U8P, C.c_size_t, base.U8P, C.c_size_t]),
            "stats": (Statistics, [C.c_void_p]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(cls.api, "dlm_net_crypto_" + name)
            function.restype, function.argtypes = result, arguments
            setattr(cls, name, staticmethod(function))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def setUp(self):
        self.owners = []
        self.assertEqual(self.capabilities(), 15, "GnuTLS/Hogweed backend unavailable")

    def tearDown(self):
        for handle in self.owners:
            self.close(handle)
        self.assertEqual(self.shutdown().status, 0, "crypto owner leaked")

    def owned(self, opened):
        if opened.handle:
            self.owners.append(opened.handle)
        return opened

    def material(self, value, kind=1):
        result = self.owned(self.secret(base.octets(value), len(value), kind, False))
        self.assertEqual(result.status, 0, result.system)
        return result

    def channel_pair(self, ikm=bytes(range(32)), salt=b"salt", context=b"protocol transcript", records=1000, byte_limit=1 << 20, ttl=10000):
        result = []
        for role in (1, 2):
            secret = self.material(ikm)
            self.owners.remove(secret.handle)
            channel = self.owned(self.channel(secret, base.octets(salt), len(salt), base.octets(context), len(context), role, records, byte_limit, ttl))
            self.assertEqual(channel.status, 0, channel.system)
            result.append(channel.handle)
        return result

    def encrypt(self, channel, plain, aad=b"", capacity=None):
        capacity = len(plain) + 32 if capacity is None else capacity
        output = base.octets(b"\xa5" * capacity)
        result = self.seal(channel, base.octets(plain), len(plain), base.octets(aad), len(aad), output, capacity)
        return result, bytes(output)

    def decrypt(self, channel, data, aad=b"", capacity=None):
        capacity = max(0, len(data) - 32) + 8 if capacity is None else capacity
        output = base.octets(b"\xa5" * capacity)
        result = self.open(channel, base.octets(data), len(data), base.octets(aad), len(aad), output, capacity)
        return result, bytes(output)

    def test_sha256_and_hmac_rfc4231(self):
        for message in (b"", b"abc", bytes(range(256)) * 31):
            output = base.octets(b"\xa5" * 40)
            result = self.hash(None, base.octets(message), len(message), output, 40)
            self.assertEqual((result.status, result.count, bytes(output)), (0, 32, hashlib.sha256(message).digest() + b"\xa5" * 8))
        key = self.material(b"\x0b" * 20)
        output = base.octets(b"?" * 32)
        self.assertEqual(self.hmac(key.handle, base.octets(b"Hi There"), 8, output, 32).status, 0)
        self.assertEqual(bytes(output).hex(), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")

    def test_hkdf_rfc5869_cases_and_output_bounds(self):
        for salt, info, expected in [(bytes(range(13)), bytes(range(0xf0, 0xfa)), "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"), (b"", b"", "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8")]:
            secret = self.material(b"\x0b" * 22);self.owners.remove(secret.handle)
            derived = self.owned(self.hkdf(secret, base.octets(salt), len(salt), base.octets(info), len(info), 42))
            self.assertEqual(derived.status, 0, derived.system)
            output = base.octets(b"?" * 32)
            self.assertEqual(self.hmac(derived.handle, base.octets(b"verify"), 6, output, 32).status, 0)
            self.assertEqual(bytes(output), hmac.new(bytes.fromhex(expected), b"verify", hashlib.sha256).digest())
        for count in (0, 31, 8161):
            secret = self.material(b"x" * 32);self.owners.remove(secret.handle)
            result = self.hkdf(secret, None, 0, None, 0, count)
            self.assertEqual((result.status, result.handle), (4, None))

    def test_x25519_matches_independent_openssl_and_ignores_high_bit(self):
        rng = random.Random(13805)
        for _ in range(24):
            a, b = rng.randbytes(32), rng.randbytes(32)
            reference = X25519PrivateKey.from_private_bytes(a)
            peer = X25519PrivateKey.from_private_bytes(b).public_key().public_bytes_raw()
            private = self.material(a, 2);output = base.octets(b"?" * 32)
            self.assertEqual(self.public(private.handle, output, 32).status, 0)
            self.assertEqual(bytes(output), reference.public_key().public_bytes_raw())
            peer = peer[:-1] + bytes([peer[-1] | 128])
            self.owners.remove(private.handle)
            shared = self.owned(self.agree(private, base.octets(peer), 32));self.assertEqual(shared.status, 0)
            self.assertEqual(self.hmac(shared.handle, base.octets(b"shared"), 6, output, 32).status, 0)
            expected = reference.exchange(X25519PublicKey.from_public_bytes(peer))
            self.assertEqual(bytes(output), hmac.new(expected, b"shared", hashlib.sha256).digest())

    def test_low_order_and_wrong_size_peer_consume_private(self):
        for peer in (bytes(32), b"\x01" + bytes(31), bytes(31), bytes(33)):
            private = self.material(bytes(range(32)), 2);self.owners.remove(private.handle)
            result = self.agree(private, base.octets(peer), len(peer))
            self.assertEqual(result.status, 12 if len(peer) == 32 else 4)
            self.assertIsNone(result.handle)

    def test_random_generation_and_shutdown_with_live_owner(self):
        first = self.owned(self.secret(None, 32, 2, True));second = self.owned(self.secret(None, 32, 2, True))
        self.assertEqual((first.status, second.status), (0, 0))
        a, b = base.octets(bytes(32)), base.octets(bytes(32))
        self.assertEqual(self.public(first.handle, a, 32).status, 0);self.assertEqual(self.public(second.handle, b, 32).status, 0)
        self.assertNotEqual(bytes(a), bytes(b));self.assertEqual(self.shutdown().status, 4)
        for count in (0, 1, 31, 65537):
            self.assertEqual(self.secret(None, count, 1, True).status, 4)

    def test_record_matches_openssl_ciphertext_and_binary_boundaries(self):
        ikm, salt, context = bytes(range(32)), b"salt", b"protocol transcript"
        client, server = self.channel_pair();rng = random.Random(13806)
        for sequence, length in enumerate((0, 1, 15, 16, 17, 255, 4096)):
            plain, aad = rng.randbytes(length), rng.randbytes(sequence * 3)
            result, encoded = self.encrypt(client, plain, aad)
            self.assertEqual((result.status, result.count), (0, length + 32))
            self.assertEqual(encoded, record(ikm, salt, context, sequence, plain, aad))
            opened, output = self.decrypt(server, encoded, aad)
            self.assertEqual((opened.status, opened.count, output), (0, length, plain + b"\xa5" * 8))
        reverse, data = self.encrypt(server, b"reverse")
        self.assertEqual(reverse.status, 0);self.assertEqual(data, record(ikm, salt, context, 0, b"reverse", direction=b"s2c"))
        self.assertEqual(self.decrypt(client, data)[0].status, 0)

    def test_openssl_generated_records_reorder_and_replay_window_edges(self):
        _, server = self.channel_pair(records=256)
        def incoming(sequence): return record(bytes(range(32)), b"salt", b"protocol transcript", sequence, b"payload")
        for sequence in (0, 2, 1, 100, 37):
            self.assertEqual(self.decrypt(server, incoming(sequence))[0].status, 0)
        for sequence in (0, 1, 2, 36, 37, 100):
            result, output = self.decrypt(server, incoming(sequence))
            self.assertEqual((result.status, result.count, output), (13, 0, b"\xa5" * len(output)))

    def test_auth_failure_never_exposes_plaintext_or_advances_replay(self):
        _, server = self.channel_pair(records=256)
        valid = record(bytes(range(32)), b"salt", b"protocol transcript", 100, b"secret", b"aad")
        bad = valid[:-1] + bytes([valid[-1] ^ 1])
        result, output = self.decrypt(server, bad, b"aad")
        self.assertEqual((result.status, result.count, output), (12, 0, b"\xa5" * len(output)))
        low = record(bytes(range(32)), b"salt", b"protocol transcript", 0, b"earlier", b"aad")
        self.assertEqual(self.decrypt(server, low, b"aad")[0].status, 0)
        self.assertEqual(self.decrypt(server, valid, b"aad")[0].status, 0)
        self.assertEqual((self.stats(server).received, self.stats(server).failed_auth), (2, 1))

    def test_wrong_aad_context_role_and_reflection_fail_closed(self):
        client, server = self.channel_pair();_, other = self.channel_pair(context=b"other")
        result, data = self.encrypt(client, b"secret", b"correct");self.assertEqual(result.status, 0)
        for target, aad in ((server, b"wrong"), (other, b"correct"), (client, b"correct")):
            result, output = self.decrypt(target, data, aad)
            self.assertEqual((result.status, output), (12, b"\xa5" * len(output)))
        self.assertEqual(self.decrypt(server, data, b"correct")[0].status, 0)

    def test_capacity_rejection_does_not_burn_nonce_or_receive_state(self):
        client, server = self.channel_pair()
        result, output = self.encrypt(client, b"abcd", capacity=35)
        self.assertEqual((result.status, output, self.stats(client).sent), (7, b"\xa5" * 35, 0))
        result, encoded = self.encrypt(client, b"abcd");self.assertEqual(result.status, 0)
        result, output = self.decrypt(server, encoded, capacity=3)
        self.assertEqual((result.status, output, self.stats(server).received), (7, b"\xa5" * 3, 0))
        self.assertEqual(self.decrypt(server, encoded)[0].status, 0)

    def test_key_record_and_byte_budgets(self):
        client, server = self.channel_pair(records=2, byte_limit=6)
        for _ in range(2):
            result, encoded = self.encrypt(client, b"abc");self.assertEqual(result.status, 0)
            self.assertEqual(self.decrypt(server, encoded)[0].status, 0)
        self.assertEqual(self.encrypt(client, b"")[0].status, 15)
        self.assertEqual(self.decrypt(server, encoded)[0].status, 15)
        self.assertEqual((self.stats(client).sent, self.stats(client).send_bytes, self.stats(server).receive_bytes), (2, 6, 6))
        other, _ = self.channel_pair(records=3, byte_limit=2)
        self.assertEqual(self.encrypt(other, b"abc")[0].status, 15);self.assertEqual(self.stats(other).sent, 0)

    def test_authentication_attempt_budget_is_not_reset(self):
        client, server = self.channel_pair();_, data = self.encrypt(client, b"payload")
        bad = data[:-1] + bytes([data[-1] ^ 1])
        for _ in range(64): self.assertEqual(self.decrypt(server, bad)[0].status, 12)
        self.assertEqual(self.stats(server).failed_auth, 64)
        self.assertEqual(self.decrypt(server, data)[0].status, 15)

    def test_key_expiry_is_monotonic_and_fail_closed(self):
        client, server = self.channel_pair(ttl=50)
        data = record(bytes(range(32)), b"salt", b"protocol transcript", 0, b"message")
        time.sleep(0.07)
        self.assertEqual(self.encrypt(client, b"x")[0].status, 14)
        result, output = self.decrypt(server, data)
        self.assertEqual((result.status, output), (14, b"\xa5" * len(output)))
        self.assertEqual(self.stats(client).status, 14)

    def test_header_corruption_is_bounded_and_does_not_commit(self):
        client, server = self.channel_pair();_, data = self.encrypt(client, b"payload")
        variants = [b"", bytes(31), b"BAD!" + data[4:], data[:12] + struct.pack(">I", 0xffffffff) + data[16:], data + b"extra"]
        for bad in variants:
            result, output = self.decrypt(server, bad)
            self.assertIn(result.status, (4, 11));self.assertEqual(output, b"\xa5" * len(output))
        self.assertEqual(self.stats(server).received, 0);self.assertEqual(self.decrypt(server, data)[0].status, 0)

    def test_invalid_parameters_and_nulls(self):
        self.assertEqual(self.secret(None, 1, 1, False).status, 4)
        self.assertEqual(self.secret(None, 32, 3, True).status, 4)
        output = base.octets(b"?" * 32)
        self.assertEqual(self.public(None, output, 32).status, 4)
        self.assertEqual(self.seal(None, None, 0, None, 0, output, 32).status, 4)
        self.assertEqual(self.open(None, None, 0, None, 0, output, 32).status, 4)
        for role, records, limit, ttl in ((0, 1, 1, 1), (3, 1, 1, 1), (1, 0, 1, 1), (1, 1048577, 1, 1), (1, 1, 0, 1), (1, 1, 1073741825, 1), (1, 1, 1, 0)):
            secret = self.material(b"x" * 32);self.owners.remove(secret.handle)
            result = self.channel(secret, None, 0, None, 0, role, records, limit, ttl)
            self.assertEqual((result.status, result.handle), (4, None))

    def test_failed_secret_is_not_downgraded_to_unkeyed_hash(self):
        failed = self.secret(None, 0, 1, True)
        self.assertEqual((failed.status, failed.handle), (4, None))
        output = base.octets(b"\xa5" * 32)
        result = self.hmac(failed.handle, base.octets(b"message"), 7, output, 32)
        self.assertEqual((result.status, result.count, bytes(output)), (4, 0, b"\xa5" * 32))

    def test_explicit_bad_library_override_has_no_fallback(self):
        self.assertEqual(self.shutdown().status, 0)
        previous = os.environ.get("DLM_LIBGNUTLS")
        try:
            for value in ("relative-library-name", str(Path(self.directory.name) / "missing-gnutls.dll")):
                os.environ["DLM_LIBGNUTLS"] = value
                self.assertEqual(self.capabilities(), 0)
                result = self.secret(base.octets(b"x" * 32), 32, 1, False)
                self.assertEqual((result.status, result.handle), (6, None))
                self.assertEqual(self.shutdown().status, 0)
        finally:
            if previous is None:
                os.environ.pop("DLM_LIBGNUTLS", None)
            else:
                os.environ["DLM_LIBGNUTLS"] = previous


class DlmCryptoTests(unittest.TestCase):
    def test_runtime_failure_wipes_owned_keys_and_partial_move(self):
        compiler = os.environ.get("DLM_NETWORK_DLM", str(ROOT / "target/release" / ("dlm.exe" if os.name == "nt" else "dlm")))
        with tempfile.TemporaryDirectory(prefix="dln-crypto-owned-") as directory:
            output = Path(directory) / ("owned.dll" if os.name == "nt" else "owned.so")
            command = [compiler, "build", str(HERE / "dln_crypto.dlm"), "--lib", str(ROOT / "lib"), "--allow-unsafe", "--target", "sharedlib", "-o", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            api = C.CDLL(str(output))
            api.crypto_abort_with_partial_move.restype = None
            api.crypto_shutdown.restype = base.Io
            api.crypto_network_shutdown.restype = base.Io
            api.dlm_native_last_error.restype = C.c_char_p
            api.dlm_native_live_allocations.restype = C.c_uint64
            for _ in range(16):
                api.crypto_abort_with_partial_move()
                self.assertTrue(api.dlm_native_last_error())
                self.assertEqual(api.dlm_native_live_allocations(), 0)
                self.assertEqual(api.crypto_shutdown().status, 0)
                self.assertEqual(api.crypto_network_shutdown().status, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
