"""Verify the runtime bulk finder against private, rebased local images."""
import argparse
import subprocess
import tempfile
from pathlib import Path
from bulkcontract_table import graph
from test_fx_lifecycle import BASELINE
from verify_sites import Image
import bench_native_insert as insert
import bench_native_sort as sort


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    reference, target = Image(BASELINE), Image(args.image)
    data = graph(reference, target)
    with tempfile.TemporaryDirectory(prefix='sage-runtime-bulk-') as folder:
        manifest = Path(folder) / 'bodies.txt'
        lines = [str(len(data['entries']))]
        for rva, size, raw_hash, _, _, patches in data['entries']:
            lines.append(f'{rva} {size} {raw_hash} {len(patches)}')
            lines.extend(f'{offset} {kind} {symbol}' for offset, kind, symbol, _ in patches)
        manifest.write_text('\n'.join(lines) + '\n', encoding='ascii')
        expected = Path(folder) / 'locations.txt'
        expected.write_text('\n'.join(map(str, data['locations'])) + '\n', encoding='ascii')
        queue_manifests = []
        for name, module in (('insert', insert), ('sort', sort)):
            path = Path(folder) / (name + '.txt')
            module.write_manifest(path, module.verify(reference, target))
            queue_manifests.append(str(path))
        return subprocess.call([str(args.executable.resolve()), str(args.image.resolve()), str(manifest), str(expected),
                                str(args.image.resolve().parent / 'msvcr71.dll'), *queue_manifests])


if __name__ == '__main__':
    raise SystemExit(main())
