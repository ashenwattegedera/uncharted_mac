"""Repository paths for the Python tests (run: python3 -m unittest discover -s tests)."""
import sys
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))


def native_executable(name):
    """CTest selects an exact build tree and requires its declared native fixtures."""
    directory = Path(os.environ.get('BB_TEST_BIN_DIR', ROOT / 'out'))
    suffix = '.exe' if sys.platform == 'win32' else ''
    path = directory / (name + suffix)
    required = os.environ.get('BB_REQUIRED_NATIVE_TESTS', '').split(',')
    if name in required and not path.is_file():
        raise FileNotFoundError(f'Required test executable missing: {path}. Build this CMake tree before running CTest.')
    return path
