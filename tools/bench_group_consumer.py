"""Offline native group / independent consumer comparison; never starts a game."""
import argparse
import contextlib
import io
import json
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from bench_native_source_worker import guard, sha, REPO
from test_mesh_draw_contract import BASELINE, Image, verify as mesh_verify, write_manifest as mesh_manifest
from test_group_contract import verify as group_verify, write_manifest as group_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('install', type=Path)
    parser.add_argument('--image', type=Path, action='append', required=True)
    parser.add_argument('--backend', choices=('native', 'dxvk'), default='dxvk')
    parser.add_argument('--workload', choices=('lit', 'shadow'), action='append')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args()
    guard()
    root = Path(tempfile.mkdtemp(prefix='sage-native-group-consumer-'))
    exe = REPO/'perf/build/bench_group_consumer.exe'
    shutil.copy2(exe, root/exe.name)
    if args.backend == 'dxvk':
        shutil.copy2(args.install/'d3d9.dll', root/'d3d9.dll')
    sources = [Path(__file__), REPO/'perf/Makefile', exe,
               *sorted((REPO/'perf/tests').glob('*.c')), *sorted((REPO/'perf/tests').glob('*.h')),
               *sorted((REPO/'perf/src').glob('*.c')), *sorted((REPO/'perf/src').glob('*.h')),
               *sorted((REPO/'perf/build').glob('*.o')),
               *(args.install/name for name in ('Shaders.big', 'msvcr71.dll', 'd3dx9_27.dll', 'd3d9.dll')),
               REPO/'tools/test_mesh_draw_contract.py', REPO/'tools/test_group_contract.py', BASELINE]
    report = {'base_commit': subprocess.check_output(['rtk', 'git', 'rev-parse', 'HEAD'], text=True).strip(),
              'backend': args.backend, 'directory': str(root), 'inputs_sha256': {str(p.resolve()): sha(p) for p in sources},
              'images': [], 'runs': [], 'game_launched': False, 'runtime_changed': False,
              'sample_columns': ['round', 'native_bulk_complete_ms', 'direct_capture_complete_ms',
                                 'worker_capture_complete_ms', 'saved_vs_native_ms']}
    reference = Image(BASELINE)
    for index, image in enumerate(args.image):
        with contextlib.redirect_stdout(io.StringIO()):
            target = Image(image)
            meshes, table, _ = mesh_verify(reference, target)
            _, groups = group_verify(reference, target)
        mesh = root/f'mesh-{index}.txt'; group = root/f'group-{index}.txt'
        mesh_manifest(mesh, meshes, table); group_manifest(group, groups)
        report['images'].append({'path': str(image.resolve()), 'sha256': sha(image),
                                 'mesh_manifest': str(mesh), 'mesh_sha256': sha(mesh),
                                 'group_manifest': str(group), 'group_sha256': sha(group)})
        for workload in args.workload or ['lit', 'shadow']:
            for fault in ([0] if args.pilot else [0, 1, 2, 3, 4]):
                guard()
                command = [str(root/exe.name), str(args.install/'Shaders.big'), str(args.install/'msvcr71.dll'),
                           workload, 'narrow', '100', 'leader-bulk-first', str(image.resolve()), str(mesh), str(group), str(fault)]
                log = root/f'{index}-{workload}-{fault}.log'
                startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
                start = time.perf_counter()
                with log.open('w', encoding='utf-8') as out:
                    run = subprocess.run(command, cwd=root, stdout=out, stderr=subprocess.STDOUT, timeout=180, startupinfo=startup)
                guard()
                text = log.read_text(encoding='utf-8', errors='replace')
                result = {'command': command, 'returncode': run.returncode, 'fault': fault,
                          'seconds': time.perf_counter()-start, 'log': str(log), 'log_sha256': sha(log)}
                report['runs'].append(result)
                args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
                if not fault:
                    assert run.returncode == 0, text[-3000:]
                    assert 'NATIVE GROUP CONSUMER: 12 image/state cases;' in text
                    result['correctness'] = next(s for s in text.splitlines() if s.startswith('NATIVE GROUP CONSUMER:'))
                    result['results'] = next(s for s in text.splitlines() if s.startswith('RESULTS:'))
                    result['oracles'] = [s for s in text.splitlines() if s.startswith('GROUP ORACLE:')]
                    assert len(result['oracles']) == 3
                    rows = [[float(x) for x in s.split(',')[1:]] for s in text.splitlines() if s.startswith('sample,')]
                    assert len(rows) == 12 and all(len(r) == 5 for r in rows)
                    result['samples'] = rows
                    result['medians_ms_per_pair'] = {name: statistics.median(r[i] for r in rows)
                                                      for i, name in enumerate(report['sample_columns']) if i}
                else:
                    expected = ('gc_captured-captured==' if fault == 1 else
                                'gc_group.passes==1' if fault == 2 else
                                'selected==gc_effects[r->identity]' if fault == 4 else 'gc_pixels_equal(expected,gc_output)')
                    assert run.returncode == 1 and expected in text, text[-3000:]
                    result['fault_detected'] = True
                args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
                print(json.dumps(result if fault else {k: v for k, v in result.items() if k != 'samples'}), flush=True)
    for path, digest in report['inputs_sha256'].items():
        assert sha(path) == digest, path
    print('Native group consumer checks completed.', flush=True)


if __name__ == '__main__':
    main()
