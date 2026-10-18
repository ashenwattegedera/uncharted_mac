"""Execution-mode TLS preparation and cache compatibility without game files."""
from paths import ROOT
import unittest
from execution_profile import TRANSLATED, artifact, metadata, validate
from link_modules import patch_fs_loads


class ProfileTests(unittest.TestCase):
    def test_translated_keeps_fs_native_rewrites_executable_only(self):
        load=bytes.fromhex('64488b042500000000')
        segments=[dict(type=1,flags=5,vaddr=0,filesz=9),dict(type=1,flags=6,vaddr=9,filesz=9)]
        translated=bytearray(load*2)
        self.assertEqual(patch_fs_loads(translated,segments,0,'unicorn-reference'),0)
        self.assertEqual(translated,load*2)
        native=bytearray(load*2)
        self.assertEqual(patch_fs_loads(native,segments,0,'native-x86'),1)
        self.assertEqual(native,bytes.fromhex('65')+load[1:]+load)

    def test_incompatible_profiles_cannot_share_prepared_input_or_filename(self):
        for mode,wrong in [('native-x86',TRANSLATED|1),('unicorn-reference',1)]:
            with self.assertRaisesRegex(ValueError,'does not match'):
                validate(wrong,mode)
        with self.assertRaises(ValueError):
            validate(1,'unknown')
        self.assertNotEqual(artifact('boot.bin','native-x86'),artifact('boot.bin','unicorn-reference'))
        self.assertNotEqual(metadata('native-x86',{'input':'A'})['cache_key'],metadata('unicorn-reference',{'input':'A'})['cache_key'])
        self.assertNotEqual(metadata('unicorn-reference',{'input':'A'})['cache_key'],metadata('unicorn-reference',{'input':'B'})['cache_key'])

    def test_actual_linker_preserves_profile_and_boots_its_translated_output(self):
        import json
        import os
        from pathlib import Path
        import struct
        import subprocess
        import tempfile
        from unittest.mock import patch
        import link_modules
        from execution_profile import flags
        from test_link_libc import LinkTests

        load=bytes.fromhex('64488b042500000000')
        base,page=0x400000,16384
        payload=bytearray(base+page)
        payload[base:base+12]=load+b'\x31\xc0\xc3'
        for mode in ('native-x86','unicorn-reference'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                out=Path(directory)
                main,module=LinkTests().fixture()
                main['ph']=[dict(type=0x61000001,vaddr=base+128),
                            dict(type=1,flags=5,vaddr=base,filesz=12,memsz=page)]
                prepared=struct.pack('<8s6Q',b'BBPROBE2',len(payload),base,1,0,1,flags(mode)|1)
                prepared+=struct.pack('<3Q',base,page,5)+b'fixture#q#q'.ljust(128,b'\0')+payload
                (out/artifact('boot.bin',mode)).write_bytes(prepared)
                with patch.object(link_modules,'module',side_effect=[main,module]):
                    link_modules.link(Path('synthetic-input'),out,('libc.prx',),execution_mode=mode)
                image=out/artifact('boot-linked.bin',mode)
                data=image.read_bytes()
                self.assertEqual(data[:8],b'BBPROBE5')
                self.assertEqual(struct.unpack_from('<Q',data,48)[0],flags(mode)|1)
                report=json.loads((out/artifact('link.json',mode)).read_text())
                self.assertEqual(report['fs_loads_patched'],int(mode=='native-x86'))
                offset=len(data)-struct.unpack_from('<Q',data,8)[0]+base
                self.assertEqual(data[offset],0x65 if mode=='native-x86' else 0x64)
                runner=os.environ.get('BB_REFERENCE_BOOT')
                if mode=='unicorn-reference' and runner:
                    completed=subprocess.run([runner,str(image)],capture_output=True,text=True,timeout=30)
                    self.assertEqual(completed.returncode,0,completed.stdout+completed.stderr)
                    self.assertIn('Reference entry completed: result=0x0',completed.stdout)
