"""Exercise the C++ groom converter with the real wolf GLB.

Run after compiling tests/furGroom.cpp:
  python3 tests/furGroomAsset.py /tmp/fur-groom-test
"""
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile

wolf = runpy.run_path(str(Path(__file__).with_name('validateWolf.py')))
card = wolf['asset']['meshes'][0]['primitives'][0]
attributes = card['attributes']
read = wolf['read']
columns = [read(attributes[key]) for key in
           ('POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0')]
indices = [i[0] for i in read(card['indices'])]
with tempfile.TemporaryDirectory(prefix='vulkhan-fur-') as directory:
    fixture = Path(directory) / 'groom.bin'
    with fixture.open('wb') as output:
        output.write(struct.pack('<II', len(columns[0]), len(indices)))
        for row in zip(*columns):
            values = [value for field in row for value in field]
            output.write(struct.pack('<3f3f2f4I4f', *values))
        output.write(struct.pack('<' + 'I' * len(indices), *indices))
        for key in ['_VKH_FUR', '_VKH_FLOW']:
            for row in read(attributes[key]):
                output.write(struct.pack('<3f', *row))
    subprocess.run([sys.argv[1], str(fixture)], check=True)
