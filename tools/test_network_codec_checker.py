#!/usr/bin/env python3
"""Adversarial tests of the independent codec plan/source checker; stdlib only."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
loader=importlib.util.spec_from_file_location('checker',ROOT/'tools/check_network_codec.py')
checker=importlib.util.module_from_spec(loader);loader.loader.exec_module(checker)

class CheckerTests(unittest.TestCase):
    def setUp(self):
        self.work=tempfile.TemporaryDirectory(prefix='dlm-codec-check-');self.addCleanup(self.work.cleanup);self.directory=Path(self.work.name);self.source=self.directory/'codec.dlm';self.library=ROOT/'lib'
        original=ROOT/'examples/native/generated/codec_echo.dlm'
        for suffix in ('','.dns','.codec'):shutil.copyfile(Path(str(original)+suffix),Path(str(self.source)+suffix))
    def manifest(self):return Path(str(self.source)+'.codec')
    def replace(self,key,value):
        lines=self.manifest().read_text().splitlines();self.manifest().write_text('\n'.join(key+' '+str(value) if line.startswith(key+' ') else line for line in lines)+'\n')
    def verify(self):return checker.check(self.source,self.library)
    def test_all_three_checked_profiles_retain_pending_obligations(self):
        for name in ('codec_echo','codec_pos','codec_le'):
            report=checker.check(ROOT/f'examples/native/generated/{name}.dlm',self.library);self.assertEqual(report['status'],'passed');self.assertFalse(report['formal_protocol_certificate']);self.assertNotEqual(report['pending_proofs'],0)
    def test_changed_source_is_rejected_even_after_binding_checksum_is_recomputed(self):
        data=self.source.read_bytes().replace(b'return codec_measure(&plan,&message.inner);',b'return CodecIo(0,0,CodecError(0,0,0));',1);self.source.write_bytes(data);self.replace('source_fnv64',checker.fnv(data))
        with self.assertRaisesRegex(ValueError,'wrapper body'):self.verify()
    def test_changed_plan_bounds_are_rejected(self):
        text=self.manifest().read_text();line=next(line for line in text.splitlines() if line.startswith('message 1 '));parts=line.split();parts[3]=str(int(parts[3])+1);self.manifest().write_text(text.replace(line,' '.join(parts)))
        with self.assertRaisesRegex(ValueError,'plan/table'):self.verify()
    def test_proof_mask_and_assurance_cannot_be_forged(self):
        self.replace('pending',0)
        with self.assertRaisesRegex(ValueError,'pending-proof'):self.verify()
        self.replace('pending',447);self.replace('assurance','proved')
        with self.assertRaisesRegex(ValueError,'binding mismatch'):self.verify()
    def test_changed_descriptor_cannot_hide_in_the_plan(self):
        descriptor=Path(str(self.source)+'.dns');data=bytearray(descriptor.read_bytes());data[0]=0;descriptor.write_bytes(data);self.replace('descriptor',data.hex());self.replace('descriptor_fnv64',checker.fnv(data))
        with self.assertRaisesRegex(ValueError,'DNS1'):self.verify()
    def test_runtime_tampering_is_rejected_even_if_manifest_checksums_follow_it(self):
        self.library=self.directory/'lib'
        for name in checker.LIBRARY_SHA256:
            target=self.library/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'lib'/name,target)
        target=self.library/'std/net_codec_runtime.dlm';target.write_bytes(target.read_bytes()+b'\n// changed runtime\n');digest=checker.fnv(target.read_bytes());self.replace('dependency std/net_codec_runtime.dlm',digest);self.replace('runtime_fnv64',digest)
        with self.assertRaisesRegex(ValueError,'changed runtime dependency'):self.verify()
    def test_extra_duplicate_and_missing_dependencies_are_rejected(self):
        text=self.manifest().read_text();line=next(line for line in text.splitlines() if line.startswith('dependency '));self.manifest().write_text(text+line+'\n')
        with self.assertRaisesRegex(ValueError,'duplicate dependency'):self.verify()
        self.manifest().write_text(text.replace(line+'\n',''))
        with self.assertRaisesRegex(ValueError,'dependency closure'):self.verify()
    def test_comments_do_not_hide_changed_executable_bodies(self):
        data=self.source.read_bytes().replace(b'return codec_measure(&plan,&message.inner);',b'/*return codec_measure(&plan,&message.inner);*/ return CodecIo(0,0,CodecError(0,0,0));',1);self.source.write_bytes(data);self.replace('source_fnv64',checker.fnv(data))
        with self.assertRaisesRegex(ValueError,'wrapper body'):self.verify()
    def test_namespace_and_version_must_match_the_descriptor_and_source(self):
        self.replace('minor',2)
        with self.assertRaisesRegex(ValueError,'version/storage'):self.verify()
        self.replace('minor',1);self.replace('prefix','Bad;')
        with self.assertRaisesRegex(ValueError,'namespace'):self.verify()
    def test_input_limits_and_unterminated_comments_are_checked(self):
        self.source.write_bytes(b'x'*(2*1024*1024+1))
        with self.assertRaisesRegex(ValueError,'input limit'):self.verify()
        self.source.write_bytes(b'/*');self.replace('source_fnv64',checker.fnv(b'/*'))
        with self.assertRaisesRegex(ValueError,'unterminated comment'):self.verify()

if __name__=='__main__':unittest.main(verbosity=2)
