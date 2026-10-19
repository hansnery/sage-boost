"""Screen readback waits against an explicitly supplied official Accelerator DLL.

Build perf/build/bench_external_{barriers,readbacks}.exe first. Runs only
standalone fixtures in a fresh TEMP directory; never launches or patches a
game. The fixed-effect replies are an optimistic screen, not a live mirror.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
EXTERNAL_SHA256 = '2c837e566775386a401d709cf01f3971085ef32d9a59793443eac0682f984663'
COLUMNS = ['round', 'off_caller_ms', 'threaded_caller_ms', 'combined_caller_ms', 'known_only_caller_ms',
           'off_complete_ms', 'threaded_complete_ms', 'combined_complete_ms', 'known_only_complete_ms',
           'combined_vs_threaded_ms', 'combined_vs_off_ms', 'combined_vs_known_only_ms']
NATIVE_COLUMNS = ['round', 'native_caller_ms', 'known_caller_ms',
                  'native_complete_ms', 'known_complete_ms', 'saved_complete_ms']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def guard():
    command = ("Get-Process | Where-Object { $_.ProcessName -match "
               "'game\\.dat|lotrbfme|worldbuilder|realms|aotr|bfme' } | "
               "Select-Object ProcessName,Id | ConvertTo-Json -Compress")
    result = subprocess.run(['rtk', 'powershell', '-NoProfile', '-Command', command],
                            capture_output=True, text=True, check=True)
    assert not result.stdout.strip(), 'game or launcher active: ' + result.stdout


def parse_screen(text, native=False):
    lines = text.splitlines()
    checks = [line for line in lines if line.startswith('CHECK:')]
    assert len(checks) == (12 if native else 24) and all('different_pixels=0 state_equal=1' in line for line in checks)
    assert 'NEGATIVE: poisoned_constant_answer_detected=1 count=576' in text
    drains = [list(map(int, re.findall(r'=([0-9]+)', line)))
              for line in lines if line.startswith('DRAINS:')]
    expected = [] if native else [[0, 2887, 0, 0, 0], [1, 2887, 0, 0, 0],
                                 [2, 5, 578, 1152, 2312], [3, 5, 578, 1152, 2312]]
    assert drains == expected, drains
    if native:
        assert 'NATIVE: external DLL not loaded;' in text
    columns = NATIVE_COLUMNS if native else COLUMNS
    samples = [[float(value) for value in line.split(',')[1:]]
               for line in lines if line.startswith('native_sample,' if native else 'sample,')]
    assert len(samples) == 24 and all(len(row) == len(columns) for row in samples)
    assert [int(row[0]) for row in samples] == list(range(24))
    return {'oracle': next(line for line in lines if line.startswith('ORACLE:')),
            'image_state_checks': checks, 'poisoned_answer_detected': True,
            'drain_counts': drains, 'samples': samples,
            'medians_ms_per_pair': {name: statistics.median(row[i] for row in samples)
                                    for i, name in enumerate(columns) if i}}


def parse_barriers(text):
    rows = [[int(parts[1]), parts[2], int(parts[3]), int(parts[4])]
            for line in text.splitlines() if line.startswith('probe,')
            for parts in [line.split(',')]]
    for round_number in range(4):
        observed = {row[1]: row[2:] for row in rows if row[0] == round_number}
        for name in ['GetVertexShader', 'GetVertexShaderConstantF_after_Commit',
                     'GetVertexDeclaration_after_Commit', 'GetStreamSourceFreq_after_Commit',
                     'GetStreamSource_after_Commit', 'GetIndices_after_Commit']:
            assert observed[name] == [1, 0], (name, observed[name])
        assert observed['shader_AddRef_after_Commit'] == [0, 0]
        assert observed['shader_Release_after_Commit'] == [0, 2]
    return {'probes': rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('install', type=Path)
    parser.add_argument('external_dll', type=Path, help='official 2.0 build 73 test-export DLL')
    parser.add_argument('--repeat', type=int, default=2)
    parser.add_argument('--native-only', action='store_true', help='do not load the external DLL; measure unhooked versus fixture replies')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert 1 <= args.repeat <= 3
    assert sha(args.external_dll) == EXTERNAL_SHA256, 'unexpected external DLL version'
    guard()
    root = Path(tempfile.mkdtemp(prefix='sage-external-readbacks-'))
    install = args.install.resolve()
    dll = root/'bfme2_accel.new.dll'
    shutil.copy2(args.external_dll, dll)
    shutil.copy2(install/'d3d9.dll', root/'d3d9.dll')
    executables = [REPO/'perf/build'/name for name in
                   ['bench_external_barriers.exe', 'bench_external_readbacks.exe']]
    for exe in executables:
        shutil.copy2(exe, root/exe.name)
    sources = [Path(__file__), REPO/'perf/Makefile',
               *sorted((REPO/'perf/tests').glob('*.c')), *sorted((REPO/'perf/tests').glob('*.h')),
               REPO/'perf/tests/shadow_fx/shadow_fx.c',
               *sorted((REPO/'perf/src').glob('*.c')), *sorted((REPO/'perf/src').glob('*.h')),
               *sorted((REPO/'perf/build').glob('*.o')), *executables,
               *(install/name for name in ['Shaders.big', 'msvcr71.dll', 'd3dx9_27.dll', 'd3d9.dll']),
               dll, root/'d3d9.dll', *(root/exe.name for exe in executables)]
    report = {'base_commit': subprocess.check_output(['rtk', 'git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
              'directory': str(root), 'inputs_sha256': {str(p.resolve()): sha(p) for p in sources},
              'sample_columns': NATIVE_COLUMNS if args.native_only else COLUMNS,
              'external_dll_loaded': not args.native_only,
              'drain_columns': ['mode', 'drains', 'skipped_shader', 'skipped_constants', 'skipped_bindings'],
              'barrier_columns': ['round', 'operation', 'drains', 'submissions'],
              'barriers': [], 'runs': [], 'game_launched': False, 'deployed': False,
              'limits': ['Fixed effect shader and raw bone/register mapping known in advance.',
                         'Resources live throughout the fixture; no general failure/lifetime/Reset proof.',
                         'Full-frame boundary reads remain synchronous; native game sources are absent.',
                         'Separate 256x256 lit/shadow workloads, bulk after second member; no complete scene or Present.',
                         'Saved milliseconds across these workloads are not additive or an FPS estimate.']}
    jobs = [] if args.native_only else [(0, workload, True) for workload in ['lit', 'shadow']]
    for repeat in range(args.repeat):
        jobs.extend((repeat, workload, False) for workload in
                    (['lit', 'shadow'] if not repeat & 1 else ['shadow', 'lit']))
    for repeat, workload, barrier in jobs:
        guard()
        exe = executables[0 if barrier else 1]
        label = ('barriers' if barrier else str(repeat))+'-'+workload
        log = root/(label+'.log')
        command = [str(root/exe.name), str(install/'Shaders.big'), str(install/'msvcr71.dll'),
                   workload, 'native' if args.native_only else str(dll)]
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        start = time.monotonic()
        with log.open('w', encoding='utf-8') as out:
            run = subprocess.run(command, cwd=root, stdout=out, stderr=subprocess.STDOUT,
                                 timeout=180, startupinfo=startup)
        guard()
        text = log.read_text(encoding='utf-8')
        result = {'command': command, 'returncode': run.returncode, 'seconds': time.monotonic()-start,
                  'log': str(log), 'log_sha256': sha(log), 'workload': workload, 'repeat': repeat}
        report['barriers' if barrier else 'runs'].append(result)
        args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        assert run.returncode == 0 and 'FAILED:' not in text, text[-4000:]
        result.update(parse_barriers(text) if barrier else parse_screen(text, args.native_only))
        args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({'case': label, 'medians': result.get('medians_ms_per_pair'), 'log': str(log)}), flush=True)
    assert all(sha(path) == digest for path, digest in report['inputs_sha256'].items())
    guard()
    print('All independent image/state, changed-input, poisoned-answer and barrier controls passed.', flush=True)


if __name__ == '__main__':
    main()
