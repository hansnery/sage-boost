"""Run verified native FX/state + group/mesh with the local effect and GPU."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_mesh_draw_contract import BASELINE,Image,verify as mesh_verify,write_manifest as mesh_manifest
from test_group_contract import verify as group_verify,write_manifest as group_manifest
from native_fx_state_contract import verify as fx_verify,manifest as fx_manifest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test',action='store_true',help='recovery executable: reject broken count/replay controls')
    p.add_argument('image',type=Path);p.add_argument('executable',type=Path)
    p.add_argument('fixture_args',nargs=argparse.REMAINDER);a=p.parse_args()
    reference,target=Image(BASELINE),Image(a.image)
    meshes,table,_=mesh_verify(reference,target);_,groups=group_verify(reference,target)
    fx=fx_verify(reference,target)
    with tempfile.TemporaryDirectory(prefix='sage-native-fx-gpu-') as folder:
        mesh=Path(folder)/'mesh.txt';group=Path(folder)/'group.txt';effects=Path(folder)/'fx.txt'
        mesh_manifest(mesh,meshes,table);group_manifest(group,groups);fx_manifest(effects,fx)
        command=[str(a.executable.resolve()),*a.fixture_args,
            str(a.image.resolve()),str(mesh),str(group),str(effects)]
        result=subprocess.call(command)
        if result:return result
        if a.self_test:
            for mode,name in ((1,'count corruption'),(2,'omitted replay'),
                              (3,'duplicate committed replay'),(4,'missing unbound-stream offset restore')):
                control=subprocess.run(command+[str(mode)],capture_output=True,text=True)
                assert control.returncode==1 and 'benchmark refused' in control.stderr,control
                print('Native FX recovery oracle:',name,'rejected',flush=True)
        return 0

if __name__=='__main__':raise SystemExit(main())
