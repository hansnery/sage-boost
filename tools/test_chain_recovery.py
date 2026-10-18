"""Run material-chain recovery, optionally selecting a deliberate corruption."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_mesh_draw_contract import BASELINE, Image, verify as mesh_verify, write_manifest as mesh_manifest
from test_group_contract import verify as group_verify, write_manifest as group_manifest
from native_fx_state_contract import verify as fx_verify, manifest as fx_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fault', type=int, choices=range(1, 8))
    parser.add_argument('image', type=Path)
    parser.add_argument('executable', type=Path)
    parser.add_argument('fixture_args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    reference, target = Image(BASELINE), Image(args.image)
    meshes, table, _ = mesh_verify(reference, target)
    _, groups = group_verify(reference, target)
    effects = fx_verify(reference, target)
    with tempfile.TemporaryDirectory(prefix='sage-chain-recovery-') as folder:
        mesh, group, fx = (Path(folder) / name for name in ('mesh.txt', 'group.txt', 'fx.txt'))
        mesh_manifest(mesh, meshes, table)
        group_manifest(group, groups)
        fx_manifest(fx, effects)
        command = [str(args.executable.resolve()), *args.fixture_args,
                   str(args.image.resolve()), str(mesh), str(group), str(fx)]
        if args.fault:
            command.append(str(args.fault))
        return subprocess.call(command)


if __name__ == '__main__':
    raise SystemExit(main())
