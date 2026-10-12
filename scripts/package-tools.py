"""Package all seven tools and three vehicle tests through validated entry points."""
from pathlib import Path
import argparse
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/packages')
    output = parser.parse_args().output.resolve()
    jobs = [('file_ops', 'tools/file_ops/package.py', []),
            ('system_ops', 'tools/system_ops/package.py', [])]
    from diagnostic_catalog import SCENARIOS
    jobs += [(name, 'tools/package.py', ['--scenario', name]) for name in SCENARIOS]
    for name, script, options in jobs:
        print('Packaging ' + name, flush=True)
        subprocess.run([sys.executable, str(ROOT/script), *options,
                        '--output', str(output/name)], check=True)
    subprocess.run([sys.executable, str(ROOT/'carplay/scripts/package-vehicle-tests.py'),
                    '--output', str(output)], check=True)
    from package_all import write_inventory
    names = [name for name, _, _ in jobs] + ['winxplay_test_'+x for x in ('av', 'connect', 'input')]
    write_inventory(output, names, 'Tools')
    record = ROOT/'build/validation/package-tools.json'
    record.parent.mkdir(parents=True, exist_ok=True)
    record.write_text(json.dumps({'platform': 'wince-arm', 'vehicle_tested': False,
                                 'startup_schemes': ['A', 'B'], 'packages': names}, indent=2), encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
