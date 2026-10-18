"""Expose two replaceable definitions from our instancer for an offline experiment.

Only our own C source is transformed, into the ignored build directory. No
production source or executable image is modified.
"""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'perf/src/inst.c').read_bytes()
text = source.decode().replace('\r\n', '\n')
for signature, renamed in (
    ('static int draw_batch(int counted)', 'static int unchecked_draw_batch(int counted)'),
    ('static int stage(const unsigned char *mesh)', 'static int unchecked_stage(const unsigned char *mesh)'),
):
    assert text.count(signature + '\n{') == 1, signature
    text = text.replace(signature + '\n{', signature + ';\n' + renamed + '\n{')
output = ROOT / 'perf/build/inst_recovery_base.c'
output.write_bytes(text.encode())
print('Recovery source:', hashlib.sha256(source).hexdigest(), '->',
      hashlib.sha256(output.read_bytes()).hexdigest())
