"""Check both status and diagnostic for intentionally fatal runtime contracts."""
import subprocess
import sys

CASES = {
    'guard-recursion': (21, 'recursive/concurrent static initialization'),
    'stack-failure': (22, 'stack protector detected corruption'),
    'bad-tls': (21, 'unsupported TLS module/offset'),
}
if __name__ == '__main__':
    status, message = CASES[sys.argv[2]]
    result = subprocess.run([sys.argv[1], sys.argv[2]], capture_output=True, text=True, timeout=10)
    if result.returncode != status or message not in result.stderr:
        sys.exit(f'Expected exit {status} and {message!r}; got {result.returncode}\n{result.stdout}{result.stderr}')
