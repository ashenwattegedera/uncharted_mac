"""Versioned preparation policy: translated guests retain their x86 FS base."""
import hashlib
import json
from pathlib import Path

MODES = ('native-x86', 'unicorn-reference')
SCHEMA = 1
TRANSLATED = (1 << 63) | (SCHEMA << 56)


def flags(mode):
    if mode not in MODES:
        raise ValueError(f'unknown execution mode: {mode}')
    return TRANSLATED if mode == 'unicorn-reference' else 0


def validate(capabilities, mode):
    if capabilities & ~1 != flags(mode):
        raise ValueError('prepared image execution mode/policy does not match linker')


def artifact(name, mode):
    flags(mode)
    path = Path(name)
    return name if mode == 'native-x86' else f'{path.stem}.{mode}-v{SCHEMA}{path.suffix}'


def metadata(mode, inputs):
    flags(mode)
    tools = {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes().replace(b'\r\n', b'\n')).hexdigest()
             for name in ('execution_profile.py', 'prepare.py', 'link_libc.py', 'link_modules.py')}
    profile = dict(schema=SCHEMA, backend=mode, fs_to_gs=mode == 'native-x86', inputs=inputs, tools_sha256=tools)
    profile['cache_key'] = hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest()
    return profile
