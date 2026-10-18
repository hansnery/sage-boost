"""Screen native rendering queue cleanup in private RAM; never launch the game."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from bench_native_insert import BASELINE, Image, verify, write_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--samples', type=int, default=21)
    args = parser.parse_args()
    assert 0 <= args.samples <= 101
    records = verify(Image(BASELINE), Image(args.image))
    print('image_sha256', hashlib.sha256(args.image.read_bytes()).hexdigest(), flush=True)
    print('exe_sha256', hashlib.sha256(args.executable.read_bytes()).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix='sage-queue-cleanup-') as folder:
        manifest = Path(folder) / 'manifest.txt'
        write_manifest(manifest, records)
        command = [str(args.executable.resolve()), str(args.image.resolve()), str(manifest)]
        result = subprocess.call(command + [str(args.samples), '0'])
        if result:
            return result
        broken = subprocess.run(command + ['0', '1'], capture_output=True, text=True)
        assert broken.returncode == 1 and 'cleanup oracle mismatch' in broken.stderr, broken
        print('Cleanup oracle rejects omitted mesh release', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
