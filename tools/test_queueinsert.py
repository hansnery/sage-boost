"""Check the runtime verifier and real installed entry without starting the game."""
import argparse
import subprocess
import tempfile
from pathlib import Path
from bench_native_insert import verify, write_manifest
from test_fx_lifecycle import BASELINE
from verify_sites import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    records = verify(Image(BASELINE), Image(args.image))
    with tempfile.TemporaryDirectory(prefix='sage-runtime-insert-') as folder:
        manifest = Path(folder) / 'manifest.txt'
        write_manifest(manifest, records)
        return subprocess.call([str(args.executable.resolve()), str(args.image.resolve()), str(manifest)])


if __name__ == '__main__':
    raise SystemExit(main())
