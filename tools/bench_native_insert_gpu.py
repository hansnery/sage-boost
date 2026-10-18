"""Join native insertion/sort/owned queues to native group/mesh/FX and real GPU.

Read native code into private RAM only. Temporary manifests contain numeric
relocations and hashes. No game startup, file patch or installable hook.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_mesh_draw_contract import BASELINE, Image, verify as mesh_verify, write_manifest as mesh_manifest
from test_group_contract import verify as group_verify, write_manifest as group_manifest
from native_fx_state_contract import verify as fx_verify, manifest as fx_manifest
from bench_native_sort import verify as sort_verify, write_manifest as sort_manifest
from bench_native_insert import verify as insert_verify, write_manifest as insert_manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--controls-only',action='store_true')
    parser.add_argument('--mode',choices=('combined','sort','render','insert'),default='combined')
    parser.add_argument('--order',choices=('ascending','descending','random','interleaved'),default='random')
    parser.add_argument('image',type=Path);parser.add_argument('executable',type=Path)
    parser.add_argument('fixture_args',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    if args.self_test and args.mode!='combined':parser.error('--self-test requires --mode combined')
    reference,target=Image(BASELINE),Image(args.image)
    meshes,table,_=mesh_verify(reference,target);_,groups=group_verify(reference,target)
    effects=fx_verify(reference,target);sort=sort_verify(reference,target);insert=insert_verify(reference,target)
    with tempfile.TemporaryDirectory(prefix='sage-native-insert-gpu-') as folder:
        mesh,group,fx,sort_path=(Path(folder)/name for name in ('mesh.txt','group.txt','fx.txt','sort.txt'))
        mesh_manifest(mesh,meshes,table);group_manifest(group,groups);fx_manifest(fx,effects);sort_manifest(sort_path,sort)
        insert_path=Path(folder)/'insert.txt';insert_manifest(insert_path,insert)
        mode=('combined','sort','render','insert').index(args.mode)
        order=('ascending','descending','random','interleaved').index(args.order)
        command=[str(args.executable.resolve()),*args.fixture_args,str(args.image.resolve()),
                 str(mesh),str(group),str(fx),str(sort_path),str(mode),str(order),str(insert_path)]
        result=subprocess.call(command+(['3'] if args.controls_only else []))
        if result:return result
        if args.self_test:
            for broken,label in ((1,'corrupt temporary copy'),(2,'duplicate queued mesh'),(4,'corrupt inserted method')):
                outcome=subprocess.run(command+[str(broken)],capture_output=True,text=True)
                expected='!memcmp(q_expected,q_buffer.records,sizeof q_expected)'
                assert outcome.returncode==1 and expected in outcome.stderr,outcome
                print('Queue oracle:',label,'rejected by exact-record comparison',flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
