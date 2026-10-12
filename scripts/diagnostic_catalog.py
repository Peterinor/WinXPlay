"""Read scenario-local build recipes. Companions have no user entry point."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT
SCENARIOS = {}
CATALOG = {}
for manifest in sorted(APP.glob('tools/*/scenario.json')):
    scenario = json.loads(manifest.read_text(encoding='utf-8'))
    name = scenario['scenario']
    if name != manifest.parent.name or name in SCENARIOS:
        raise ValueError(f'Invalid scenario folder: {manifest}')
    SCENARIOS[name] = scenario
    built = set()
    for program in [*scenario['companions'], scenario['main']]:
        item = dict(program, scenario=name, group=manifest.parent.parent.name, source_dir=manifest.parent)
        if item['tool'] in CATALOG:
            raise ValueError(f'Duplicate program identity: {item["tool"]}')
        if not set(program['dependencies']) <= built:
            raise ValueError(f'Companions must precede their consumer in {name}')
        for source in program['sources']:
            path = (manifest.parent / source).resolve()
            if not path.is_relative_to(APP.resolve()) or not path.is_file():
                raise ValueError(f'Invalid scenario source: {path}')
        built.add(program['tool'])
        CATALOG[item['tool']] = item


def binary(tool):
    item = CATALOG[tool]
    return ROOT / 'build' / item['group'] / item['scenario'] / item['executable']


def main_tool(scenario):
    # Old --tool identifiers remain accepted for existing scripted workflows.
    if scenario in SCENARIOS:
        return SCENARIOS[scenario]['main']['tool']
    if scenario in CATALOG and CATALOG[scenario]['category'] != 'helper':
        return scenario
    raise ValueError(f'Choose an operation scenario, not a companion: {scenario}')
