"""Generate a developer-only gate experiment from our own bulk.c source."""
from pathlib import Path
import argparse
import hashlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    source = args.source.read_text()
    guards = ('*(const unsigned *)(first + 4) != 1',
              '(*(const unsigned *)(first + 4) - 1u) > 5u')
    found = [guard for guard in guards if source.count(guard) == 1]
    assert len(found) == 1, 'bulk method-count guard changed'
    guard = found[0]
    assert source.startswith('#include "bulk.h"\n')
    result = source.replace(guard, '!bench_chain_gate(*(const unsigned *)(first + 4))')
    result = result.replace('#include "bulk.h"\n',
                            '#include "bulk.h"\nextern int bench_chain_gate(unsigned);\n', 1)
    args.output.write_text(result, encoding='utf-8', newline='\n')
    print('CHAIN_SCREEN_SOURCE', hashlib.sha256(args.source.read_bytes()).hexdigest(),
          hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
