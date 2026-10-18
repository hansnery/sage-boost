"""Run the offline GPU fixture after verifying local native mesh bodies.

No game launch or image bytes written. The temporary contract is numeric
metadata; the C fixture loads and relocates those bodies into private RAM.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_mesh_draw_contract import BASELINE, Image, verify, write_manifest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path)
    p.add_argument('executable',type=Path)
    p.add_argument('fixture_args',nargs=argparse.REMAINDER)
    args=p.parse_args()
    records,table,_=verify(Image(BASELINE),Image(args.image))
    with tempfile.TemporaryDirectory(prefix='sage-native-gpu-') as folder:
        manifest=Path(folder)/'contract.txt'
        write_manifest(manifest,records,table)
        return subprocess.call([str(args.executable.resolve()),*args.fixture_args,
                                str(args.image.resolve()),str(manifest)])


if __name__=='__main__':
    raise SystemExit(main())
