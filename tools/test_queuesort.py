"""Check runtime sorting and insertion hook coexistence in private images."""
import argparse
import subprocess
import tempfile
from pathlib import Path
import bench_native_sort as sort
import bench_native_insert as insert
from test_fx_lifecycle import BASELINE
from verify_sites import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    reference, target = Image(BASELINE), Image(args.image)
    with tempfile.TemporaryDirectory(prefix='sage-runtime-sort-') as folder:
        manifests = []
        for name, module in [('sort', sort), ('insert', insert)]:
            path = Path(folder) / f'{name}.txt'
            module.write_manifest(path, module.verify(reference, target))
            manifests.append(str(path))
        return subprocess.call([str(args.executable.resolve()), str(args.image.resolve()), *manifests])


if __name__ == '__main__':
    raise SystemExit(main())
