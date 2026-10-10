"""Compile the CPU scene regression with the configured game build's flags.

Run in the development shell after building the game:
  python3 tests/runSceneFurGroom.py [build-directory]

Uses the game's linked objects so it exercises the real Scene implementation
without starting GLFW, Vulkan, networking, or audio.
"""
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1] if len(sys.argv) > 1 else root / 'build').resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
entry = next(c for c in commands if c['file'].endswith('/wolfSys.cpp'))
compile_args = shlex.split(entry['command'])
link_args = shlex.split((build / 'CMakeFiles/vulkhan.dir/link.txt').read_text())
with tempfile.TemporaryDirectory(prefix='vulkhan-scene-fur-') as directory:
    obj = Path(directory) / 'test.o'
    exe = Path(directory) / 'test'
    compile_args[compile_args.index('-o') + 1] = str(obj)
    compile_args[compile_args.index('-c') + 1] = str(root / 'tests/sceneFurGroom.cpp')
    subprocess.run(compile_args, cwd=entry['directory'], check=True)
    link_args = [a for a in link_args if not a.endswith('/src/main.cpp.o')]
    link_args.insert(1, str(obj))
    link_args[link_args.index('-o') + 1] = str(exe)
    subprocess.run(link_args, cwd=build, check=True)
    subprocess.run([str(exe)], cwd=root, check=True)
