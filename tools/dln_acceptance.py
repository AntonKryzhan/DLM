#!/usr/bin/env python3
"""Run the native DLN release gate and retain revision-bound machine-readable results."""
import argparse
import datetime
import hashlib
import importlib.util
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOSTS = ROOT / 'crates/dlm_core/tests/native_hosts'
SUITES = ('network', 'dln_foundation', 'dln_reactor', 'dln_async', 'dln_crypto',
          'dln_quic', 'dln_transport', 'dln_spec', 'dln_codegen', 'dln_state',
          'dln_nodes', 'dln_adaptive', 'dln_gateway', 'dln_continuity', 'dln_http')
NETWORK_TESTS = ('native_network', 'dln_foundation', 'dln_reactor', 'dln_async',
                 'dln_crypto', 'dln_quic', 'dln_transport', 'dln_spec', 'dln_codec',
                 'dln_state', 'dln_nodes', 'dln_adaptive', 'dln_release', 'dln_gateway', 'dln_continuity', 'dln_proxy')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def run_suite(name, output):
    spec = importlib.util.spec_from_file_location('release_' + name, HOSTS / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(module))
    report = {'tests': result.testsRun, 'failures': [case.id() for case, _ in result.failures],
              'errors': [case.id() for case, _ in result.errors],
              'skipped': [{'test': case.id(), 'reason': reason} for case, reason in result.skipped],
              'unexpected_successes': [case.id() for case in result.unexpectedSuccesses],
              'expected_failures': [case.id() for case, _ in result.expectedFailures]}
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if result.wasSuccessful() and result.testsRun else 1


def payload(message):
    size = 0 if message % 31 == 0 else 1 if message % 29 == 0 else 4095 if message % 23 == 0 else 4096
    data = bytearray((message * 17 + i * 29 + (i >> 8)) & 255 for i in range(size))
    if size >= 4:
        data[:4] = message.to_bytes(4, 'big')
    return data


def verify_transfer(result):
    digest = hashlib.sha256()
    count = 0
    for message in range(result['messages']):
        data = payload(message)
        digest.update(data)
        count += len(data)
    if result['sha256'] != digest.hexdigest() or result['bytes'] != count or result['migrations'] < 3:
        raise RuntimeError('independent transfer length, SHA-256 or migration check failed')


def source_identity():
    paths = set()
    for directory in ('lib/std', 'crates/dlm_core/src/native', 'crates/dlm_network_proof',
                      'crates/dlm_transport_proof', 'crates/dlm_gateway', 'crates/dlm_cli/src', 'native/quic',
                      'crates/dlm_core/tests', 'tools'):
        paths.update(path for path in (ROOT / directory).rglob('*') if path.is_file()
                     and path.suffix in ('.rs', '.c', '.h', '.dlm', '.py', '.json', '.toml') and '__pycache__' not in path.parts)
    paths.update(ROOT / name for name in ('Cargo.toml', 'Cargo.lock', 'vendor/DLN_QUIC_SOURCES.json'))
    values = {path.relative_to(ROOT).as_posix(): sha256(path) for path in sorted(paths)}
    identity = hashlib.sha256(json.dumps(values, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return identity, values


class Gate:
    def __init__(self, output, environment):
        self.output = output
        self.environment = environment
        self.steps = []

    def run(self, name, command, timeout=300, environment=None):
        log = self.output / (name + '.log')
        start = time.monotonic()
        print(name, flush=True)
        error = None
        with log.open('w', encoding='utf-8') as stream:
            try:
                process = subprocess.run([str(part) for part in command], cwd=ROOT,
                                         env=environment or self.environment, stdout=stream,
                                         stderr=subprocess.STDOUT, timeout=timeout)
                code = process.returncode
            except (OSError, subprocess.TimeoutExpired) as exception:
                error = str(exception)
                stream.write(error + '\n')
                code = -1
        step = {'name': name, 'command': [str(part) for part in command], 'cwd': str(ROOT),
                'status': 'PASS' if code == 0 else 'FAIL', 'exit_code': code,
                'elapsed_seconds': round(time.monotonic() - start, 3), 'log': log.name}
        if error:
            step['error'] = error
        self.steps.append(step)
        return step

    def host(self, executable, name, arguments, environment=None, timeout=90):
        step = self.run(name, [executable, *arguments], timeout=timeout, environment=environment)
        if step['status'] != 'PASS':
            return step
        try:
            results = [json.loads(line) for line in (self.output / step['log']).read_text().splitlines() if line.startswith('{')]
            if not results or any(value.get('status') != 'PASS' for value in results):
                raise RuntimeError('missing or failed native case results')
            for value in results:
                if value['case'] == 'impaired_transfer':
                    verify_transfer(value)
            step['results'] = results
        except (ValueError, KeyError, RuntimeError) as exception:
            step['status'] = 'FAIL'
            step['error'] = str(exception)
        return step


def acceptance(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, NO_PROXY='*', no_proxy='*', RUST_MIN_STACK='33554432', PYTHONUNBUFFERED='1')
    environment.setdefault('CARGO_BUILD_JOBS', '2')
    environment.setdefault('CARGO_INCREMENTAL', '0')
    environment.setdefault('CARGO_PROFILE_DEV_DEBUG', '0')
    environment.setdefault('CARGO_PROFILE_TEST_DEBUG', '0')
    gate = Gate(output, environment)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (output / 'report.json').write_text(json.dumps({'format': 'DLM-DLN-ACCEPTANCE-v1', 'status': 'RUNNING', 'started_utc': started}) + '\n', encoding='utf-8')
    compiler = (args.compiler or ROOT / 'target' / args.profile / ('dlm.exe' if os.name == 'nt' else 'dlm')).resolve()
    initial_identity, hashes = source_identity()
    if not args.compiler:
        command = ['cargo', 'build', '--workspace', '--locked']
        if args.profile == 'release':
            command.append('--release')
        gate.run('build', command, 600)
    environment['DLM_NETWORK_DLM'] = str(compiler)
    version = gate.run('version', [compiler, '--version'])
    version['output'] = (output / version['log']).read_text().strip()
    quic = args.quic.resolve() if args.quic else output / ('dlm-quic-1.dll' if os.name == 'nt' else 'libdlm-quic-1.so')
    if not args.quic:
        gate.run('quic_build', [sys.executable, ROOT / 'native/quic/build.py', quic], 300)
    environment['DLM_QUIC_PREBUILT'] = str(quic)
    environment['DLM_LIBQUIC'] = str(quic)
    if not args.skip_rust:
        commands = [('workspace_tests', ['cargo', 'test', '--workspace', '--locked'])] if args.workspace_tests else [
            ('rust_network', ['cargo', 'test', '--locked', '-p', 'dlm_core',
                              *[part for name in NETWORK_TESTS for part in ('--test', name)]]),
            ('rust_proofs', ['cargo', 'test', '--locked', '-p', 'dlm_network_proof', '-p', 'dlm_transport_proof']),
            ('rust_network_cli', ['cargo', 'test', '--locked', '-p', 'dlm_cli', '--test', 'cli_network_codegen', '--test', 'cli_network_protocol'])]
        for name, command in commands:
            if args.profile == 'release':
                command.append('--release')
            gate.run(name, command, 1200)
    gateway_command = ['cargo', 'test', '--locked', '-p', 'dlm_gateway']
    if args.profile == 'release':
        gateway_command.append('--release')
    gate.run('rust_gateway', [*gateway_command, '--', '--include-ignored'], 300)
    gate.run('continuity_model', [compiler.with_name('dlm-net.exe' if os.name == 'nt' else 'dlm-net'), 'continuity-model', '--output', output / 'continuity-model.json'])
    host = output / ('dln-release-host.exe' if os.name == 'nt' else 'dln-release-host')
    flags = [os.environ.get('CC', 'gcc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-Wno-misleading-indentation', '-UNDEBUG', str(HOSTS / 'dln_release_host.c')]
    libraries = ['-lws2_32'] if os.name == 'nt' else ['-pthread', '-lm', '-ldl']
    built = gate.run('release_host_build', [*flags, *libraries, '-o', host])
    host_available = built['status'] == 'PASS'
    if host_available:
        gate.host(host, 'delayed_probes', ['probes'])
        gate.host(host, 'silent_failover', ['blackhole'])
        gate.host(host, 'lost_authentication', ['auth-loss'])
        gate.host(host, 'hostile_inputs', ['fuzz'])
        for loss in (0, 1, 5, 10):
            for mode in (1, 2):
                gate.host(host, 'transfer_%s_%s' % (loss, mode),
                          ['transfer', str(14600 + loss * 10 + mode), str(loss), str(mode), '320'])
    for suite in SUITES:
        result_path = output / (suite + '.json')
        if result_path.exists():
            result_path.unlink()
        step = gate.run(suite, [sys.executable, __file__, '--run-suite', suite, '--suite-result', result_path])
        if result_path.is_file():
            step['unittest'] = json.loads(result_path.read_text())
            if step['unittest']['skipped'] or step['unittest']['expected_failures']:
                if step['status'] == 'PASS':
                    step['status'] = 'INCOMPLETE'
        elif step['status'] == 'PASS':
            step['status'] = 'FAIL'
            step['error'] = 'missing unittest result'
    gate.run('codec_checker', [sys.executable, ROOT / 'tools/test_network_codec_checker.py'])
    gate.run('compatibility_checker', [sys.executable, ROOT / 'tools/test_dln_compatibility.py'])
    for snapshot in sorted((ROOT / 'docs').glob('DLN_BASELINE_*.json')):
        gate.run('compatibility_' + snapshot.stem, [sys.executable, ROOT / 'tools/check_dln_compatibility.py', '--snapshot', snapshot])
    soak_host, soak_environment = host, environment
    if args.sanitizers:
        sanitized = host.with_name(host.stem + '-sanitized' + host.suffix)
        built = gate.run('sanitizer_build', [*flags, '-fsanitize=address,undefined', '-fno-omit-frame-pointer', *libraries, '-o', sanitized])
        if built['status'] == 'PASS':
            sanitized_environment = dict(environment, ASAN_OPTIONS='detect_leaks=%s:abort_on_error=1' % int(args.leak_sanitizer), UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            soak_host, soak_environment = sanitized, sanitized_environment
            for name, arguments in [('probes', ['probes']), ('blackhole', ['blackhole']), ('auth_loss', ['auth-loss']),
                                    ('transfer', ['transfer', '14699', '10', '2', '320']), ('fuzz', ['fuzz'])]:
                gate.host(sanitized, 'sanitized_' + name, arguments, sanitized_environment)
    soak = {'status': 'NOT_RUN', 'requested_seconds': args.soak_seconds, 'cycles': 0}
    if args.soak_seconds and host_available:
        step = gate.host(soak_host, 'soak', ['soak', str(args.soak_seconds)], environment=soak_environment, timeout=args.soak_seconds + 120)
        soak = step['results'][0] if step['status'] == 'PASS' else {'status': 'FAIL', 'requested_seconds': args.soak_seconds}
    status = 'FAIL' if any(step['status'] == 'FAIL' for step in gate.steps) else 'INCOMPLETE' if any(step['status'] == 'INCOMPLETE' for step in gate.steps) else 'PASS'
    final_identity, _ = source_identity()
    if initial_identity != final_identity:
        status = 'FAIL'
        gate.steps.append({'name': 'source_identity', 'status': 'FAIL', 'error': 'source changed during acceptance'})
    packages = {}
    for package in ('aioquic', 'cryptography', 'websockets'):
        try:
            packages[package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            packages[package] = 'NOT_INSTALLED'
    test_counts = {'python_passed': 0, 'python_skipped': 0, 'rust_passed': 0, 'rust_ignored': 0}
    for step in gate.steps:
        if 'unittest' in step:
            unit = step['unittest']
            test_counts['python_passed'] += unit['tests'] - sum(len(unit[key]) for key in ('skipped', 'failures', 'errors', 'expected_failures', 'unexpected_successes'))
            test_counts['python_skipped'] += len(unit['skipped'])
        if step['name'] == 'workspace_tests' or step['name'].startswith('rust_'):
            for passed, ignored in re.findall(r'test result: ok\. (\d+) passed; 0 failed; (\d+) ignored;', (output / step['log']).read_text()):
                test_counts['rust_passed'] += int(passed)
                test_counts['rust_ignored'] += int(ignored)
    report = {'format': 'DLM-DLN-ACCEPTANCE-v1', 'scope': 'local native engineering release gate',
              'status': status, 'started_utc': started, 'finished_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'platform': platform.platform(), 'python': sys.version, 'python_packages': packages, 'test_counts': test_counts, 'source_sha256': initial_identity,
              'source_files': hashes, 'compiler_sha256': sha256(compiler) if compiler.is_file() else None,
              'gateway_sha256': sha256(compiler.with_name('dlm-net.exe' if os.name == 'nt' else 'dlm-net')) if compiler.with_name('dlm-net.exe' if os.name == 'nt' else 'dlm-net').is_file() else None,
              'gateway_library_sha256': sha256(compiler.with_name('dlm_gateway.dll' if os.name == 'nt' else 'libdlm_gateway.so')) if compiler.with_name('dlm_gateway.dll' if os.name == 'nt' else 'libdlm_gateway.so').is_file() else None,
              'quic_sha256': sha256(quic) if quic.is_file() else None, 'steps': gate.steps, 'soak': soak,
              'external_gates': {'other_native_operating_systems': 'NOT_RUN', 'privileged_tun_driver': 'NOT_RUN',
                                 'public_internet_two_hosts': 'NOT_RUN', '24_hour_soak': 'PASS' if args.soak_seconds >= 86400 and soak['status'] == 'PASS' else 'NOT_RUN'},
              'leak_sanitizer': 'REQUESTED' if args.leak_sanitizer else 'NOT_RUN',
              'formal_scope': 'finite model certificates only; native and cryptographic proofs pending'}
    path = output / 'report.json'
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(status + ': ' + str(path), flush=True)
    return 0 if status == 'PASS' else 3 if status == 'INCOMPLETE' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'target/dln-acceptance')
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--profile', choices=('debug', 'release'), default='release')
    parser.add_argument('--quic', type=Path)
    parser.add_argument('--skip-rust', action='store_true')
    parser.add_argument('--workspace-tests', action='store_true')
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--leak-sanitizer', action='store_true', help='also enable LeakSanitizer; requires an environment permitting thread inspection')
    parser.add_argument('--soak-seconds', type=int, default=0)
    parser.add_argument('--run-suite', choices=SUITES, help=argparse.SUPPRESS)
    parser.add_argument('--suite-result', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 0 <= args.soak_seconds <= 259200:
        parser.error('soak duration must be between 0 and 259200 seconds')
    if args.leak_sanitizer and not args.sanitizers:
        parser.error('leak-sanitizer requires sanitizers')
    if args.run_suite:
        if not args.suite_result:
            parser.error('suite-result is required')
        return run_suite(args.run_suite, args.suite_result)
    return acceptance(args)


if __name__ == '__main__':
    sys.exit(main())
