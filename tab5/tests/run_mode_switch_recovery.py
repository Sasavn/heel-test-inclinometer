"""Exact production functions and portable cores; no UART, USB, GUI or user NVS.

Use --source-root for a frozen baseline. --expect-failure requires the actual
recovery assertion to fail, never treats a compiler error as reproduction.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import re
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', lambda m: ' '*len(m[0]), source, flags=re.S)
    depth = 1
    for end in range(match.end(), len(source)):
        depth += (masked[end] == '{') - (masked[end] == '}')
        if not depth:
            return source[match.start():end+1]
    raise AssertionError(name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=REPO)
    parser.add_argument('--expect-failure', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    root = args.source_root.resolve()
    source = (root/'tab5/main/main.c').read_text(encoding='utf-8')
    names = ['sample_callback', 'save_relative_config', 'apply_dual_action_body',
             'apply_dual_action', 'make_row', 'consume_sample']
    (out/'mode_switch_generated.h').write_text('\n\n'.join(function(source, n) for n in names), encoding='utf-8')
    sources = [REPO/'tab5/tests/test_mode_switch_recovery.c',
               root/'tab5/main/tab5_relative.c', root/'tab5/main/tab5_measure.c',
               root/'tab5/main/tab5_workspace.c']
    command = [shutil.which('gcc') or 'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I'+str(root/'tab5/main'), '-I'+str(out)]
    if args.sanitize:
        command += ['-fsanitize=undefined,bounds', '-fsanitize-undefined-trap-on-error']
    command += [*map(str, sources), '-lm', '-o', str(out/'regression.exe')]
    build = subprocess.run(command, capture_output=True, text=True)
    (out/'build.log').write_text(build.stdout+build.stderr, encoding='utf-8')
    build.check_returncode()
    cases = [(1, 1, 0, 0, 1, 1)] if args.expect_failure else itertools.product(range(2), [1, 4], range(2), range(2), range(2), range(1, 6))
    results = []
    for values in cases:
        result = subprocess.run([str(out/'regression.exe'), *map(str, values)], capture_output=True, text=True)
        name = '-'.join(map(str, values))
        (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        failure_checks = ['!memcmp(floor,relative_state.pair_used,sizeof(floor))',
                          'r.ready&&r.quality==0&&r.skew_us==33333']
        passed = (result.returncode == 1 and any(c in result.stderr for c in failure_checks)) if args.expect_failure else result.returncode == 0
        results.append({'case': values, 'exit': result.returncode, 'pass': passed})
        if not passed:
            print(result.stdout+result.stderr)
            break
    manifest = {'passed': all(r['pass'] for r in results), 'expected_failure': args.expect_failure,
                'cases': results, 'compile_command': command, 'extracted_functions': names,
                'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                                  [*sources, root/'tab5/main/main.c', root/'tab5/main/tab5_relative.h']}}
    (out/'result.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f"{'PASS' if manifest['passed'] else 'FAIL'} {len(results)} mode-switch cases; expected_failure={args.expect_failure}")
    raise SystemExit(0 if manifest['passed'] else 1)


if __name__ == '__main__':
    main()
