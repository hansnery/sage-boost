"""Offline loader checks; no game or third-party DLL entry point is executed."""
from pathlib import Path
import shutil
import subprocess
import tempfile

build = Path('build').resolve()
for mode in ('off', 'host', 'duplicate', 'legacy', 'tacitus'):
    proxy = Path('C:/Jogos/AgeoftheRing/rotwk/dinput8.dll')
    if mode == 'tacitus' and not proxy.exists():
        print('Tacitus resource-only coexistence check skipped (not installed)')
        continue
    with tempfile.TemporaryDirectory(prefix='unlocker-asi-') as directory:
        root = Path(directory)
        for name in ('test_asi.exe', 'bfme2unlocker.asi'):
            shutil.copy2(build / name, root / name)
        if mode == 'off':
            (root / 'bfme2unlocker.ini').write_text('[FactionUnlocker]\nEnabled=0\n')
        if mode == 'duplicate':
            shutil.copy2(build / 'bfme2unlocker.asi', root / 'duplicate.asi')
        argument = 'none'
        if mode in ('legacy', 'tacitus'):
            shutil.copy2(build / 'dinput8.dll' if mode == 'legacy' else proxy,
                         root / 'dinput8.dll')
            argument = str(root / 'dinput8.dll')
        subprocess.run([str(root / 'test_asi.exe'), argument, mode], cwd=root, check=True)
        log = (root / 'bfme2unlocker.log').read_text()
        expected = ('master switch off' if mode == 'off' else
                    'sage-boost dinput8 proxy already loaded' if mode == 'legacy' else
                    'not BFME2 1.06 or ROTWK 2.01 -> refused')
        assert expected in log, (mode, log)
        if mode == 'duplicate':
            assert 'another unlocker ASI owns this process' in (root / 'duplicate.log').read_text()
        print(f'{mode}: passed')
