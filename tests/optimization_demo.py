"""Reproducible before/after demo. Run from WSL/Linux (generated C uses Linux runtime)."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def command(args, cwd):
    subprocess.run([str(x) for x in args], cwd=cwd, check=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', default='build-wsl/frontend/frontend')
    parser.add_argument('--output', default='build-wsl/optimization-demo')
    parser.add_argument('--cc', default='gcc', help='gcc or path to chibicc')
    parser.add_argument('--dot', default=shutil.which('dot'), help='optional Graphviz executable')
    args = parser.parse_args()
    frontend = Path(args.frontend).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for name, flags in [('before', []), ('after', ['--optimize'])]:
        directory = output / name
        directory.mkdir(exist_ok=True)
        command([frontend, *flags, '--ast', 'ast.dot', '--hir', 'hir.txt',
                 '--cfg', 'cfg.dot', '--lir', 'lir.txt', '--emit-c', 'program.c',
                 ROOT / 'examples/local_optimizations.lang'], directory)
        # chibicc accepts separate translation units via assembly/object files.
        for stem, source in [('program', 'program.c'), ('runtime', ROOT / 'runtime/vm.c')]:
            command([args.cc, '-I' + str(ROOT / 'runtime'), '-S', source, '-o', stem + '.s'], directory)
            command([args.cc, '-c', stem + '.s', '-o', stem + '.o'], directory)
        command([args.cc, 'program.o', 'runtime.o', '-o', 'program'], directory)
        result = subprocess.run([str(directory / 'program')], capture_output=True, timeout=10)
        results.append((result.returncode, result.stdout, result.stderr))
        if args.dot:
            command([args.dot, '-Tsvg', 'cfg.dot', '-o', 'cfg.svg'], directory)
    if results[0] != results[1] or results[0] != (21, b'', b''):
        raise RuntimeError('Unexpected program results: ' + repr(results))
    print('OK: original and optimized generated programs both return 21.')
    print('HIR, LIR, C and graphs:', output)
    if not args.dot:
        print('Graphviz not found: DOT files are ready; use --dot to also create SVG.')


if __name__ == '__main__':
    main()
