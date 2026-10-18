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
