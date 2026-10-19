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
    parser.add_argument('--commands', action='store_true', help='evaluate effects on producer and batch owned device commands')
    parser.add_argument('--profile', action='store_true', help='profile coarse producer intervals after ordinary command timing rounds')
    parser.add_argument('--capture-bulk', action='store_true', help='capture qualified follower palettes at the native loop boundary')
    parser.add_argument('--batch-first', action='store_true', help='include the first unit in checked consumer instancing')
    parser.add_argument('--frame-start', action='store_true', help='compare merged synchronous and asynchronous worker frame starts')
    parser.add_argument('--completion', action='store_true', help='compare per-frame and bounded-batch GPU completion with every intermediate image checked')
    args = parser.parse_args()
    if args.completion:
        args.frame_start = True
    if args.frame_start:
        args.batch_first = True
    if args.batch_first:
        args.capture_bulk = True
    if args.capture_bulk:
        args.profile = True
    if args.profile:
        args.commands = True
    guard()
    root = Path(tempfile.mkdtemp(prefix='sage-native-group-consumer-'))
    exe = REPO/'perf/build'/('bench_group_completion.exe' if args.completion else 'bench_group_start.exe' if args.frame_start else 'bench_group_first.exe' if args.batch_first else 'bench_group_capture.exe' if args.capture_bulk else 'bench_group_profile.exe' if args.profile else 'bench_group_commands.exe' if args.commands else 'bench_group_consumer.exe')
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
              'backend': args.backend, 'commands': args.commands, 'profile': args.profile, 'capture_bulk': args.capture_bulk, 'batch_first': args.batch_first, 'frame_start': args.frame_start, 'completion': args.completion, 'directory': str(root), 'inputs_sha256': {str(p.resolve()): sha(p) for p in sources},
              'images': [], 'runs': [], 'game_launched': False, 'runtime_changed': False,
              'sample_columns': ['round', 'native_bulk_complete_ms', 'direct_capture_complete_ms',
                                 'worker_capture_complete_ms', 'saved_vs_native_ms']}
    if args.capture_bulk:
        report['sample_columns'].append('original_worker_complete_ms')
    if args.batch_first:
        report['sample_columns'].append('previous_bulk_worker_complete_ms')
    if args.frame_start:
        report['sample_columns'].extend(['previous_first_worker_complete_ms', 'merged_sync_worker_complete_ms'])
    if args.completion:
        report['completion_columns'] = ['frames', 'round', 'native_per_frame_ms', 'direct_per_frame_ms',
                                        'worker_per_frame_ms', 'native_per_batch_ms', 'direct_per_batch_ms',
                                        'worker_per_batch_ms']
        report['completion_limits'] = [
            'Raw timings cover the complete bounded batch, including final GPU completion; summaries normalize to two frames.',
            'CPU command execution still completes at every frame boundary; only GPU completion is deferred.',
            'Every intermediate correctness image is retained on a separate GPU surface and read after the final drain.',
            'Correctness copies, readbacks and audit wrappers are absent from timing.',
            'No Present, live engine ownership, gameplay FPS or multiplayer claim follows from this fixture.']
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
            for fault in ([0] if args.pilot else range(18 if args.completion else 16 if args.frame_start else 14 if args.batch_first else 13 if args.capture_bulk else 9 if args.commands else 5)):
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
                    label = 'NATIVE GROUP COMMANDS:' if args.commands else 'NATIVE GROUP CONSUMER:'
                    assert label+(' 48' if args.frame_start else ' 36' if args.batch_first else ' 24' if args.capture_bulk else ' 12')+' image/state cases;' in text
                    result['correctness'] = next(s for s in text.splitlines() if s.startswith(label))
                    if args.commands:
                        result['audit'] = next(s for s in text.splitlines() if s.startswith('COMMAND AUDIT:'))
                        result['batches'] = next(s for s in text.splitlines() if s.startswith('COMMAND BATCHES:'))
                        assert 'outstanding_refs=0 ' in result['audit']
                    result['results'] = next(s for s in text.splitlines() if s.startswith('RESULTS:'))
                    if args.capture_bulk:
                        result['bulk'] = next(s for s in text.splitlines() if s.startswith('CAPTURE BULK:'))
                        assert '12 callback refusal pairs;' in result['bulk']
                        result['guards'] = next(s for s in text.splitlines() if s.startswith('CAPTURE GUARDS:'))
                        assert '20 actual-frame predicate refusals;' in result['guards']
                    if args.batch_first:
                        result['first_batch'] = next(s for s in text.splitlines() if s.startswith('FIRST BATCH:'))
                        assert '30 size/image/state cases; 4 first-batch shader failures recover' in result['first_batch']
                    if args.frame_start:
                        result['frame_start'] = next(s for s in text.splitlines() if s.startswith('FRAME START:'))
                        assert '12 image/state cases; 6 held-worker capture probes; 4 shader-failure recovery cases;' in result['frame_start']
                    if args.completion:
                        result['completion_checks'] = next(s for s in text.splitlines() if s.startswith('COMPLETION:'))
                        assert '42 intermediate image/state cases in 9 batches;' in result['completion_checks']
                        comp = [[float(x) for x in s.split(',')[1:]] for s in text.splitlines() if s.startswith('completion,')]
                        assert len(comp) == 36 and all(len(r) == 8 and all(x > 0 for x in r[2:]) for r in comp)
                        assert [(r[0], r[1]) for r in comp] == [(n, r) for n in (2, 4, 8) for r in range(12)]
                        result['completion_samples'] = comp
                        result['completion_medians_ms_per_pair'] = {
                            str(n): {name: statistics.median(r[i]*2/n for r in comp if r[0] == n)
                                     for i, name in enumerate(report['completion_columns']) if i >= 2}
                            for n in (2, 4, 8)}
                        result['completion_paired_savings_ms_per_pair'] = {
                            str(n): {name: statistics.median((r[a]-r[b])*2/n for r in comp if r[0] == n)
                                     for name, a, b in [('worker_vs_native_per_frame', 2, 4),
                                                        ('worker_vs_native_per_batch', 5, 7),
                                                        ('native_batch_vs_per_frame', 2, 5),
                                                        ('worker_batch_vs_per_frame', 4, 7)]}
                            for n in (2, 4, 8)}
                    result['oracles'] = [s for s in text.splitlines() if s.startswith('GROUP ORACLE:')]
                    assert len(result['oracles']) == 3
                    rows = [[float(x) for x in s.split(',')[1:]] for s in text.splitlines() if s.startswith('sample,')]
                    assert len(rows) == 12 and all(len(r) == len(report['sample_columns']) for r in rows)
                    assert [r[0] for r in rows] == list(range(12))
                    result['samples'] = rows
                    result['medians_ms_per_pair'] = {name: statistics.median(r[i] for r in rows)
                                                      for i, name in enumerate(report['sample_columns']) if i}
                    if args.profile:
                        prof = [[float(x) for x in s.split(',')[1:]] for s in text.splitlines() if s.startswith('profile,')]
                        assert len(prof) == 24 and all(len(r) == 9 for r in prof)
                        assert sorted((r[0], r[1]) for r in prof) == [(r, m) for r in range(12) for m in (3, 4)]
                        assert all(abs(r[2]-sum(r[3:8])) < 0.001 and all(x >= 0 for x in r[2:]) for r in prof)
                        result['profile_columns'] = ['round', 'mode', 'complete_ms', 'effect_record_ms', 'native_traversal_ms',
                                                     'publish_replay_or_wait_ms', 'other_group_ms', 'outside_groups_ms',
                                                     'paired_unprofiled_complete_ms']
                        result['profiles'] = prof
                else:
                    expected = ('!gc_completion_pending || mode==gc_completion_mode' if fault == 17 else
                                'gc_pixels_equal(expected[i],gc_completion_pixels[i])' if fault == 16 else
                                'gc_active_frame && gc_active_frame==r->frame_id' if fault == 15 else
                                'consumer.owner==GetCurrentThreadId()' if fault == 14 else
                                'gc_pixels_equal(expected,gc_output)' if fault == 13 else
                                'COMMAND DEVICE AUDIT: forbidden producer call 1' if fault == 12 else
                                'gc_bulk_captured==bulk_before' if fault == 11 else
                                'scope_after.scopes-scope_before.scopes==' if fault == 10 else
                                'gc_captured-captured==' if fault == 9 else
                                'COMMAND DEVICE AUDIT: forbidden producer call 119' if fault == 8 else
                                'COMMAND DEVICE AUDIT: forbidden producer call' if fault == 5 else
                                'words==n && !memcmp(before,after,n*4)' if fault == 6 else
                                'COMMAND CAPACITY: refused' if fault == 7 else
                                'gc_captured-captured==' if fault == 1 else
                                'gc_group.passes==1' if fault == 2 else
                                'selected==gc_effects[' if fault == 4 else 'gc_pixels_equal(expected,gc_output)')
                    assert run.returncode == 1 and expected in text, text[-3000:]
                    result['fault_detected'] = True
                args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
                print(json.dumps(result if fault else {k: v for k, v in result.items() if k not in ('samples', 'completion_samples')}), flush=True)
    for path, digest in report['inputs_sha256'].items():
        assert sha(path) == digest, path
    for image in report['images']:
        assert sha(image['path']) == image['sha256']
        assert sha(image['mesh_manifest']) == image['mesh_sha256']
        assert sha(image['group_manifest']) == image['group_sha256']
    guard()
    print('Native group consumer checks completed.', flush=True)


if __name__ == '__main__':
    main()
