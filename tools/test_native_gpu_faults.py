"""Audit one-shot API rejection in the joined native group/GPU fixture.

Uses only private process memory and numeric manifests. Successful audit
means expected counterexamples were detected, not that recovery is fixed.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_mesh_draw_contract import BASELINE,Image,verify as mesh_verify,write_manifest as mesh_manifest
from test_group_contract import verify as group_verify,write_manifest as group_manifest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--recovery',action='store_true',help='also reject omitted/duplicated fallback controls')
    p.add_argument('image',type=Path);p.add_argument('executable',type=Path)
    p.add_argument('fixture_args',nargs=argparse.REMAINDER)
    args=p.parse_args()
    reference,target=Image(BASELINE),Image(args.image)
    records,table,_=mesh_verify(reference,target);_,groups=group_verify(reference,target)
    with tempfile.TemporaryDirectory(prefix='sage-gpu-faults-') as folder:
        mesh=Path(folder)/'mesh.txt';group=Path(folder)/'group.txt'
        mesh_manifest(mesh,records,table);group_manifest(group,groups)
        command=[str(args.executable.resolve()),*args.fixture_args,
                 str(args.image.resolve()),str(mesh),str(group)]
        result=subprocess.call(command)
        if result:return result
        if args.self_test:
            control=subprocess.run(command+['1'],capture_output=True,text=True)
            assert control.returncode==1 and 'benchmark refused' in control.stderr,control
            print('Fault oracle: deliberately corrupted submitted-instance count rejected',flush=True)
            if args.recovery:
                for mode,name in ((2,'omitted replay'),(3,'duplicate committed replay')):
                    control=subprocess.run(command+[str(mode)],capture_output=True,text=True)
                    assert control.returncode==1 and 'benchmark refused' in control.stderr,control
                    print('Recovery oracle:',name,'rejected',flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
