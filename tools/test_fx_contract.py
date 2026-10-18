"""Synthetic refusal tests for the offline native FX verifier (no EA data)."""
import struct
from types import SimpleNamespace

from test_fx_lifecycle import Function, contract


def check(code, *, direct=(), virtual=(), name='fixture', symbols=None):
    image = SimpleNamespace(base=0x400000, mem=bytearray(code + b'\0' * 256))
    spec = Function(name, 0, len(code), direct, virtual)
    return contract(image, 0, spec, symbols or [128 + i * 4 for i in range(12)])


def refuses(code, **kwargs):
    try:
        check(code, **kwargs)
    except AssertionError:
        return
    raise AssertionError('unsafe contract was accepted')


def run():
    # Relocation is allowed only to the exact known dependency and field.
    direct = b'\xe8' + struct.pack('<i', 128 - 5) + b'\xc3'
    normal, fields, _ = check(direct, direct=(0,))
    assert fields == [(1, 0)] and normal == b'\xe8\0\0\0\0\xc3'
    moved = b'\xe8' + struct.pack('<i', 160 - 5) + b'\xc3'
    assert check(moved, direct=(0,), symbols=[160])[0] == normal
    refuses(moved, direct=(0,))  # Known symbol, wrong dependency index.
    refuses(b'\xe8' + struct.pack('<i', 800 - 5) + b'\xc3', direct=(0,))
    refuses(b'\xff\xd0\xc3')  # Register-indirect call.
    refuses(b'\xff\x90\x08\x01\0\0\xc3', virtual=(0x10C,))
    check(b'\xff\x90\x0c\x01\0\0\xc3', virtual=(0x10C,))
    refuses(b'\xa1\x00\x01\x40\x00\xc3')  # Absolute data pointer.
    refuses(b'\xb8\x10\x00\x40\x00\xc3')  # Immediate image pointer.
    refuses(b'\x64\x8b\x00\xc3')  # Segmented data access.
    refuses(b'\xeb\x7f\xc3')  # External branch.
    refuses(b'\xeb\xff\xc3')  # Branch into its own displacement.
    refuses(b'\xe8\0\0')  # Truncated instruction.
    refuses(b'\x90')  # No final return.
    check(b'\xeb\0\xc3')
    tail = b'\xe9' + struct.pack('<i', 156 - 5) + b'\xc2\x08\0'
    check(tail, name='bind', direct=(7,))
    refuses(tail, direct=(7,))
    wrong_tail = b'\xe9' + struct.pack('<i', 128 - 5) + b'\xc2\x08\0'
    refuses(wrong_tail, name='bind', direct=(7,))
    # Ordinary immediates stay in the full-body comparison: flags cannot
    # change just because all call targets happen to remain valid.
    assert check(b'\x6a\x06\xc3')[0] != check(b'\x6a\x07\xc3')[0]
    print('FX verifier: relocation controls and 13 altered-code refusals passed')


if __name__ == '__main__':
    run()
