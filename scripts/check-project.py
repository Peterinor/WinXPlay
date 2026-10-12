"""Dependency-free checks for a standalone WinXPlay source checkout."""
import ast
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
EXCLUDE = {'build', '.local', '__pycache__', '.git', 'out', 'dist'}


def source_files():
    for folder, directories, files in os.walk(ROOT):
        directories[:] = [d for d in directories if d not in EXCLUDE]
        for name in files:
            yield Path(folder) / name


def check():
    errors = []
    counts = {'python': 0, 'documents': 0, 'local_links': 0, 'spdx_headers': 0, 'script_references': 0}
    for path in source_files():
        relative = path.relative_to(ROOT).as_posix()
        if path.suffix.lower() in {'.exe','.dll','.obj','.lib','.pdb','.pk8','.p7b','.der'} or path.name.startswith('phone-pair') and path.suffix == '.plist':
            errors.append(f'{relative}: generated or private runtime material in source tree')
        if path.name == 'scenario.json':
            scenario = json.loads(path.read_text(encoding='utf-8-sig'))
            main = scenario['main']
            if main.get('category') == 'experiment':
                instructions = main.get('instructions_file')
                if not instructions or not (path.parent/instructions).is_file():
                    errors.append(f'{path.relative_to(ROOT)}: missing local packaging instructions')
        if path.suffix not in {'.py', '.ps1', '.psm1', '.cmd', '.cpp', '.h', '.inc', '.md'}:
            continue
        text = path.read_text(encoding='utf-8-sig')
        relative = path.relative_to(ROOT).as_posix()
        if path.suffix == '.py':
            counts['python'] += 1
            try:
                ast.parse(text, filename=relative)
            except SyntaxError as error:
                errors.append(f'{relative}: {error}')
        if path.suffix in {'.py', '.ps1', '.psm1', '.cmd'}:
            if re.search(r'''(?:/|\\|['"])(?:evidence|research)(?:/|\\|['"])''', text):
                errors.append(f'{relative}: implicit research-workspace dependency')
            if re.search(r'[A-Z]:[/\\](?:coding|Users)[/\\]', text, re.I):
                errors.append(f'{relative}: machine-specific workspace/user path')
            if not relative.startswith('examples/') and re.search(r'(?<![A-Za-z0-9_])[A-Za-z]:[/\\]', text):
                errors.append(f'{relative}: hard-coded host drive path; configure the dependency explicitly')
        if path.suffix in {'.ps1', '.psm1'}:
            for reference in re.findall(r'\$PSScriptRoot([/\\][^"\r\n$]*\.(?:ps1|psm1))', text):
                dependency=(path.parent/reference.replace('\\','/').lstrip('/')).resolve()
                counts['script_references'] += 1
                if not dependency.is_relative_to(ROOT) or not dependency.is_file():
                    errors.append(f'{relative}: invalid project script reference: {reference}')
        for line in text.splitlines()[:8]:
            if 'SPDX-License-Identifier:' in line:
                counts['spdx_headers'] += 1
                if '\ufffc' in line or '\ufffd' in line or '\u200b' in line:
                    errors.append(f'{relative}: unexpected character in SPDX header')
        if path.suffix != '.md':
            continue
        counts['documents'] += 1
        # Code fences are examples, not navigable Markdown links.
        prose = re.sub(r'```.*?```', '', text, flags=re.S)
        for match in re.finditer(r'!?\[[^\]]*\]\(([^)\n]+)\)', prose):
            target = match.group(1).strip().split(' "', 1)[0].strip('<>')
            if target.startswith(('#', 'http:', 'https:', 'mailto:', 'app:')):
                continue
            target = target.split('#', 1)[0]
            if not target:
                continue
            counts['local_links'] += 1
            resolved = (path.parent / target).resolve()
            if not resolved.is_relative_to(ROOT):
                errors.append(f'{relative}: link escapes standalone project: {target}')
            elif not resolved.exists():
                errors.append(f'{relative}: missing link: {target}')
    result = dict(passed=not errors, root=str(ROOT), counts=counts, errors=errors)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == '__main__':
    sys.exit(check())
