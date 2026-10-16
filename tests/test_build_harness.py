"""Configuration and required-fixture failures must be explicit."""
import os
import platform
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from paths import ROOT, native_executable


class BuildHarnessTests(unittest.TestCase):
    def test_required_binary_cannot_silently_skip(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.dict(os.environ, BB_TEST_BIN_DIR=tmp, BB_REQUIRED_NATIVE_TESTS='bb-probe'):
                with self.assertRaisesRegex(FileNotFoundError, 'Required test executable missing'):
                    native_executable('bb-probe')

    def test_optional_binary_uses_selected_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.dict(os.environ, BB_TEST_BIN_DIR=tmp, BB_REQUIRED_NATIVE_TESTS=''):
                self.assertEqual(native_executable('content-test').parent, Path(tmp))

    def test_unsupported_backend_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run(['cmake', '-S', str(ROOT), '-B', tmp,
                                     '-DBB_EXECUTION_BACKEND=imaginary'],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Unknown BB_EXECUTION_BACKEND', result.stdout + result.stderr)

    @unittest.skipUnless(platform.machine().lower() in ('arm64', 'aarch64'), 'ARM configuration guard')
    def test_native_x86_on_arm_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run(['cmake', '-S', str(ROOT), '-B', tmp,
                                     '-DBB_EXECUTION_BACKEND=native-x86'],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('native-x86 requires an x86-64 host', result.stdout + result.stderr)
