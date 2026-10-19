"""Offline native matrix capture into owned render packets; never starts a game.

Build perf/build/bench_native_source_worker.exe first. Local code/shader bytes
remain private to the fixture; reports retain hashes, metadata and timings.
"""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import io
import json
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

from bench_native_matrix_sources import BODIES, BASELINE, Image, refusals, verify

REPO = Path(__file__).resolve().parents[1]
COLUMNS = ['round', 'direct_prepare_ms', 'serial_prepare_ms', 'pipeline_prepare_ms',
           'direct_wait_ms', 'serial_wait_ms', 'pipeline_wait_ms', 'direct_complete_ms',
           'serial_complete_ms', 'pipeline_complete_ms', 'direct_worker_ms',
           'serial_worker_ms', 'pipeline_worker_ms', 'direct_queue_ms',
           'serial_queue_ms', 'pipeline_queue_ms', 'saved_vs_direct_ms', 'saved_vs_serial_ms']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def guard():
    command = ("Get-Process | Where-Object { $_.ProcessName -match "
               "'game\\.dat|lotrbfme|worldbuilder|realms|aotr|bfme' } | "
               "Select-Object ProcessName,Id | ConvertTo-Json -Compress")
    result = subprocess.run(['rtk', 'powershell', '-NoProfile', '-Command', command],
                            capture_output=True, text=True, check=True)
    assert not result.stdout.strip(), 'game or launcher active: ' + result.stdout


def parse(text):
    assert 'refused' not in text and 'FAILED:' not in text
    lines = text.splitlines()
    ownership = [line for line in lines if line.startswith('OWNERSHIP:')]
    assert len(ownership) == 2 and all('deliberate borrowed-buffer control rejected' in line for line in ownership)
    assert 'WORKER: consumed=590 ' in text and 'CAPTURE: 509760 native matrix callbacks' in text
    assert 'NATIVE SOURCES: 54 independent' in text
    assert 'SOURCE ORACLE: changed matrices alter ' in text
    rows = [[float(v) for v in line.split(',')[1:]] for line in lines if line.startswith('sample,')]
    assert len(rows) == 21 and all(len(row) == len(COLUMNS) for row in rows)
    assert [int(row[0]) for row in rows] == list(range(21))
    return {'oracle': next(line for line in lines if line.startswith('ORACLE:')),
            'packet': next(line for line in lines if line.startswith('PACKET:')),
            'ownership_controls': ownership, 'samples': rows,
            'medians_ms_per_pair': {name: statistics.median(row[i] for row in rows)
                                    for i, name in enumerate(COLUMNS) if i}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('install', type=Path, help='local directory containing Shaders.big and D3DX27')
    parser.add_argument('--image', type=Path, action='append', required=True)
    parser.add_argument('--workload', choices=('lit', 'shadow'), action='append')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--checked', action='store_true')
    args = parser.parse_args()
    assert 1 <= args.repeat <= 3
    guard()
    root = Path(tempfile.mkdtemp(prefix='sage-native-source-worker-'))
    install = args.install.resolve()
    exe = REPO/'perf/build/bench_native_source_worker.exe'
    shutil.copy2(exe, root/exe.name)
    shutil.copy2(install/'d3d9.dll', root/'d3d9.dll')
    sources = [Path(__file__), REPO/'tools/bench_native_matrix_sources.py',
               REPO/'perf/Makefile', *sorted((REPO/'perf/tests').glob('*.c')),
               *sorted((REPO/'perf/tests').glob('*.h')), REPO/'perf/tests/shadow_fx/shadow_fx.c',
               *sorted((REPO/'perf/src').glob('*.c')), *sorted((REPO/'perf/src').glob('*.h')),
               *sorted((REPO/'perf/build').glob('*.o')), exe, BASELINE,
               *(install/name for name in ('Shaders.big', 'msvcr71.dll', 'd3dx9_27.dll', 'd3d9.dll'))]
    report = {'base_commit': subprocess.check_output(['rtk', 'git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
              'directory': str(root), 'inputs_sha256': {str(p.resolve()): sha(p) for p in sources},
              'sample_columns': COLUMNS, 'images': [], 'runs': [],
              'runtime_changed': False, 'game_launched': False, 'deployed': False}
    reference = Image(BASELINE)
    for index, image in enumerate(args.image):
        image = image.resolve()
        target = Image(image)
        with contextlib.redirect_stdout(io.StringIO()):
            records, symbols = verify(reference, target)
            rejected = refusals(reference, target, symbols)
        manifest = root/f'sources-{index}.txt'
        lines = []
        for rva, size, digest, patches in records:
            lines.append(f'{rva} {size} {digest} {len(patches)}')
            lines.extend(' '.join(map(str, patch)) for patch in patches)
        manifest.write_text('\n'.join(lines)+'\n', encoding='ascii')
        report['images'].append({'path': str(image), 'sha256': sha(image), 'refusal_controls': rejected,
                                'manifest': str(manifest), 'manifest_sha256': sha(manifest),
                                'bodies': [{'name': name, 'rva': rva, 'size': size,
                                            'sha256': hashlib.sha256(bytes(target.mem[rva:rva+size])).hexdigest()}
                                           for (name, _, _), (rva, size, _, _) in zip(BODIES, records)]})
        for repeat in range(args.repeat):
            workloads = args.workload or ['lit', 'shadow']
            if repeat & 1:
                workloads = list(reversed(workloads))
            for workload in workloads:
                # The deliberate setter corruption must reach the image/state oracle.
                for fault in ['', 'wrong-setter', 'stale-source'] if repeat == 0 else ['']:
                    guard()
                    label = f'{index}-{repeat}-{workload}' + ('-'+fault if fault else '')
                    log = root/(label+'.log')
                    command = [str(root/exe.name), str(install/'Shaders.big'), str(install/'msvcr71.dll'),
                               workload, 'checked' if args.checked else 'owned', str(image), str(manifest)]
                    if fault:
                        command.append(fault)
                    startup = subprocess.STARTUPINFO()
                    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    startup.wShowWindow = 0
                    start = time.monotonic()
                    with log.open('w', encoding='utf-8') as out:
                        run = subprocess.run(command, cwd=root, stdout=out, stderr=subprocess.STDOUT,
                                             timeout=180, startupinfo=startup)
                    guard()
                    text = log.read_text(encoding='utf-8')
                    result = {'command': command, 'returncode': run.returncode, 'fault': fault,
                              'seconds': time.monotonic()-start, 'log': str(log), 'log_sha256': sha(log)}
                    report['runs'].append(result)
                    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
                    if fault:
                        assert run.returncode == 1 and ('!memcmp(pixels,reference[p->oracle],sizeof pixels)' in text
                                                       or '!memcmp(state,reference_state[p->oracle],n*4)' in text), text[-3000:]
                        result['fault_detected_by_output_oracle'] = True
                    else:
                        assert run.returncode == 0, text[-4000:]
                        result.update(parse(text))
                    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
                    print(json.dumps({'case': label, 'returncode': run.returncode,
                                      'medians': result.get('medians_ms_per_pair'), 'log': str(log)}), flush=True)
    assert all(sha(path) == digest for path, digest in report['inputs_sha256'].items())
    for image in report['images']:
        assert sha(image['path']) == image['sha256'] and sha(image['manifest']) == image['manifest_sha256']
    guard()
    print('All native-source, pixel/state, ownership and negative controls passed.', flush=True)


if __name__ == '__main__':
    main()
