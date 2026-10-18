"""Verify local group/mesh contracts and run the joined native GPU fixture.

No game startup or EA code/assets persisted: temporary manifests contain
only numeric relocation roles, offsets, sizes and hashes.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_mesh_draw_contract import BASELINE,Image,verify as mesh_verify,write_manifest as mesh_manifest
from test_group_contract import verify as group_verify,write_manifest as group_manifest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path)
    p.add_argument('executable',type=Path)
    p.add_argument('fixture_args',nargs=argparse.REMAINDER)
    args=p.parse_args()
    reference,target=Image(BASELINE),Image(args.image)
    records,table,_=mesh_verify(reference,target)
    _,groups=group_verify(reference,target)
    with tempfile.TemporaryDirectory(prefix='sage-native-group-gpu-') as folder:
        mesh=Path(folder)/'mesh.txt'; group=Path(folder)/'group.txt'
        mesh_manifest(mesh,records,table); group_manifest(group,groups)
        return subprocess.call([str(args.executable.resolve()),*args.fixture_args,
                                str(args.image.resolve()),str(mesh),str(group)])


if __name__=='__main__':
    raise SystemExit(main())
