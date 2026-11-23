#!/usr/bin/env python3
"""The reviewed Windows fix must not admit any other protected-source change."""
import json
from pathlib import Path
import tempfile
import unittest
import check_dln_compatibility as compatibility

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = 'crates/dlm_core/src/backend_c_runtime.c'


class ReviewedSourceTests(unittest.TestCase):
    def test_all_historical_snapshots_report_the_exact_reviewed_windows_fix(self):
        for snapshot in sorted((ROOT / 'docs').glob('DLN_BASELINE_*.json')):
            result = compatibility.check(ROOT, snapshot)
            self.assertEqual(result['status'], 'passed', result['failures'])
            self.assertEqual(len(result['reviewed_source_updates']), 1)
            self.assertEqual(result['reviewed_source_updates'][0]['path'], RUNTIME)

    def test_runtime_exception_rejects_mutations_other_paths_and_other_baselines(self):
        original = (ROOT / RUNTIME).read_bytes()
        old, new, _ = compatibility.REVIEWED_SOURCE_UPDATES[RUNTIME]
        self.assertEqual(compatibility.digest(original), new)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            network = root / 'network.c'
            network.write_text('int dlm_net_stable(void) { return 1; }\n')
            snapshot = root / 'baseline.json'
            for relative, expected, content, status in [
                (RUNTIME, old, original, 'passed'),
                (RUNTIME, old, original + b'\n/* unreviewed */\n', 'failed'),
                (RUNTIME, old, original.replace(b'cannot open file ', b'cannot open fail ', 1), 'failed'),
                (RUNTIME, '0' * 64, original, 'failed'),
                ('other.c', old, original, 'failed'),
            ]:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                snapshot.write_text(json.dumps({'format': 'DLM-DLN-COMPAT-v1', 'baseline_version': '0.134.0',
                                               'protected_files': {relative: expected}, 'network_c_source': 'network.c',
                                               'network_c_abi': compatibility.c_functions(network.read_text())}))
                result = compatibility.check(root, snapshot)
                self.assertEqual(result['status'], status, (relative, expected, result))


if __name__ == '__main__':
    unittest.main(verbosity=2)
