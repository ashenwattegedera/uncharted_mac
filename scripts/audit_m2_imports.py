"""Reproduce the source/compiled-contract inventory without proprietary inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'docs/evidence/m2-import-inventory.json'


def inventory(binary=None):
    old = json.loads(EVIDENCE.read_text()) if EVIDENCE.exists() else {}
    contracts = json.loads(subprocess.check_output([str(Path(binary).resolve()), 'inventory'], text=True)) if binary else old.get('contracts', [])
    names = [item['name'] for item in contracts]
    if not contracts or len(names) != len(set(names)):
        raise ValueError('compiled registry is empty or contains duplicate names')
    paths = sorted((ROOT / 'src').glob('runtime*.c')) + [ROOT / 'src/import_names.inc', ROOT / 'src/translated_runtime.c', ROOT / 'src/guest_abi.h', ROOT / 'src/guest_layout.h', ROOT / 'src/guest_format.c']
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest() for p in paths}
    imports = re.findall(r'\{"([^"]+)","([^"]+)"\}', (ROOT / 'src/import_names.inc').read_text())
    if len({nid for nid, _ in imports}) != len(imports):
        raise ValueError('duplicate NID in import name inventory')
    # Provider names come from source export tables, not inferred prototypes.
    providers = {}
    for path in paths:
        if path.name.startswith('runtime'):
            tables = re.findall(r'RuntimeExport\s+\w+\[\]\s*=\s*\{(.*?)\n\};', path.read_text(), re.S)
            for table in tables:
                for name in re.findall(r'\{\s*"([^"]+)"\s*,', table):
                    providers.setdefault(name, []).append(str(path.relative_to(ROOT)))
            if path.name == 'runtime_thread.c':
                known = dict(imports)
                for nid in re.findall(r'\{\s*"([^"]+)"\s*,', path.read_text()):
                    if nid in known:
                        providers.setdefault(known[nid], []).append(str(path.relative_to(ROOT)))
    memory_exports = {name for name, sources in providers.items() if 'src/runtime_memory.c' in sources}
    if not memory_exports or memory_exports - set(names):
        raise ValueError(f'unregistered actual memory exports: {sorted(memory_exports-set(names))}')
    result = []
    for nid, name in imports:
        sources = sorted(set(providers.get(name, [])))
        if name in names:
            status = 'implemented-m2'
        elif any('thread' in p or 'fiber' in p for p in sources):
            status = 'deferred-m3-thread-fiber-lifecycle'
        elif any(any(tag in p for tag in ('audio', 'ajm', 'pad')) for p in sources) or name.startswith('sceGnm'):
            status = 'deferred-m4-device-renderer'
        else:
            status = 'unsupported-needs-contract-review-before-use'
        result.append({'nid': nid, 'name': name, 'status': status, 'source_export_tables': sources})
    return {'schema': 1, 'scope': 'Explicit M2 reference contracts; diagnostic names do not establish ABI signatures or game coverage.',
            'source_sha256': hashes, 'contracts': contracts, 'diagnostic_imports': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', help='compiled translated-runtime-test; required to write evidence')
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    if args.write and not args.binary:
        parser.error('--write requires --binary')
    actual = inventory(args.binary)
    if args.write:
        EVIDENCE.write_text(json.dumps(actual, indent=2) + '\n')
    elif actual != json.loads(EVIDENCE.read_text()):
        raise SystemExit('M2 import inventory is stale; review changed contracts/providers and regenerate with --binary ... --write')
    print(f"PASS: {len(actual['contracts'])} explicit contracts, {len(actual['diagnostic_imports'])} diagnostic imports, complete actual memory-export coverage")


if __name__ == '__main__':
    main()
