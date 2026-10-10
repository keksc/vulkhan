"""Validate the exported asset contract without Blender or a GPU."""
import json
import math
from pathlib import Path
import struct

path = Path(__file__).resolve().parents[1] / 'models/wolf.glb'
blob = path.read_bytes()
magic, version, length = struct.unpack_from('<III', blob)
assert magic == 0x46546C67 and version == 2 and length == len(blob)
jlen, jtype = struct.unpack_from('<II', blob, 12)
assert jtype == 0x4E4F534A
asset = json.loads(blob[20:20+jlen])
bin_start = 20+jlen
blen, btype = struct.unpack_from('<II', blob, bin_start)
assert btype == 0x004E4942
binary = blob[bin_start+8:bin_start+8+blen]
components = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
sizes = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def read(index):
    a = asset['accessors'][index]
    view = asset['bufferViews'][a['bufferView']]
    code, size = components[a['componentType']]
    count = sizes[a['type']]
    stride = view.get('byteStride', size*count)
    start = view.get('byteOffset', 0)+a.get('byteOffset', 0)
    assert a['count'] == 0 or start+(a['count']-1)*stride+size*count <= len(binary)
    return [struct.unpack_from('<'+code*count, binary, start+i*stride)
            for i in range(a['count'])]


assert len(asset['skins']) == 1 and len(asset['skins'][0]['joints']) == 24
assert len(read(asset['skins'][0]['inverseBindMatrices'])) == 24
assert len(asset['images']) == 1
image_view = asset['bufferViews'][asset['images'][0]['bufferView']]
image_start = image_view.get('byteOffset', 0)
assert binary[image_start:image_start+8] == b'\x89PNG\r\n\x1a\n'
assert struct.unpack_from('>II', binary, image_start+16) == (1024, 1024)

assert len(asset['animations']) == 1
animation = asset['animations'][0]
assert animation['name'] == 'Run'
assert all(s.get('interpolation', 'LINEAR') in ('LINEAR', 'STEP')
           for s in animation['samplers'])
ends, starts = [], []
for channel in animation['channels']:
    s = animation['samplers'][channel['sampler']]
    times = [v[0] for v in read(s['input'])]
    output = read(s['output'])
    assert len(times) == len(output) and all(math.isfinite(t) for t in times)
    assert all(b > a for a, b in zip(times, times[1:]))
    assert all(math.isfinite(v) for row in output for v in row)
    if channel['target']['path'] == 'rotation':
        assert all(abs(sum(v*v for v in q)-1) < 1e-4 for q in output)
        dot = sum(a*b for a, b in zip(output[0], output[-1]))
        assert abs(abs(dot)-1) < 1e-4, 'Loop rotation seam'
    else:
        assert max(abs(a-b) for a, b in zip(output[0], output[-1])) < 1e-4, 'Loop seam'
    ends.append(times[-1])
    starts.append(times[0])
assert abs(max(ends)-min(starts)-.7) < 1e-5

triangles = []
for level in range(3):
    node = next(n for n in asset['nodes'] if n['name'] == f'Wolf_LOD{level}')
    assert node['skin'] == 0
    assert node['extras']['vkh_fur_version'] == 1
    mesh = asset['meshes'][node['mesh']]
    assert len(mesh['primitives']) == 1, 'Export hairless skin only'
    p = mesh['primitives'][0]
    attrs = p['attributes']
    for semantic in ['POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0', '_VKH_FUR', '_VKH_FLOW']:
        assert semantic in attrs
    positions = read(attrs['POSITION'])
    uvs = read(attrs['TEXCOORD_0'])
    parameters = read(attrs['_VKH_FUR'])
    flows = read(attrs['_VKH_FLOW'])
    indices = [r[0] for r in read(p['indices'])]
    assert len(indices) % 3 == 0 and max(indices) < len(positions)
    assert len(positions) == len(uvs) == len(parameters) == len(flows)
    assert all(math.isfinite(v) for row in positions+parameters+flows for v in row)
    for length, density, lift in parameters:
        assert 0 <= length <= .3 and 0 <= density <= 1 and 0 <= lift <= 1
    bare = [i for i, uv in enumerate(uvs) if uv[1] < .2]
    assert bare and all(parameters[i][0] == 0 and parameters[i][1] == 0 for i in bare)
    assert sum(p[1] > .5 for p in parameters) > 1000
    assert max(p[0] for p in parameters) > .12, 'Tail and ruff need a longer coat'
    assert all(abs(sum(v*v for v in flow)-1) < .001 for flow in flows)
    torso = [i for i,p in enumerate(positions) if -.4 < p[2] < .4 and p[1] > 1]
    assert torso and sum(flows[i][2] < -.5 for i in torso)/len(torso) > .9
    for joints, weights in zip(read(attrs['JOINTS_0']), read(attrs['WEIGHTS_0'])):
        assert all(0 <= j < 24 for j in joints)
        assert all(0 <= w <= 1 for w in weights)
        assert abs(sum(weights)-1) < 1e-4
    triangles.append(len(indices)//3)
assert triangles[0] > triangles[1] > triangles[2] > 1000
assert not any('card' in m.get('name','').lower() for m in asset['materials'])
print(f'Hairless surface LOD triangles: {triangles}; fur masks, lengths and flow valid')
# Bare nose atlas swatch pins the exported asset's actual forward axis.
p = asset['meshes'][0]['primitives'][0]
positions = read(p['attributes']['POSITION'])
uvs = read(p['attributes']['TEXCOORD_0'])
nose = [v for v, uv in zip(positions, uvs)
        if abs(uv[0]-1/12) < .001 and uv[1] < .2]
assert nose and sum(v[2] for v in nose)/len(nose) > 1.0, 'Wolf muzzle must face +Z'

print(f'Wolf GLB valid: 24 joints, seamless 0.7s Run, LOD triangles {triangles}')

# Regression for the engine's positive-height viewport + Y-flipped
# projection. Use exported outward normals to select surfaces facing the
# camera, then classify the actual indexed triangles in framebuffer space.
# Vulkan CCW means positive signed area (negative 2D cross product).
def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def normalize(v):
    length = math.sqrt(dot(v, v))
    return tuple(x/length for x in v)


camera = (4.8, 1.5, -4.5)
forward = normalize(sub((0, 1, 0), camera))
right = normalize(cross(forward, (0, 1, 0)))
up = cross(right, forward)
for mesh in asset['meshes']:
    p = mesh['primitives'][0]
    vertices = read(p['attributes']['POSITION'])
    normals = read(p['attributes']['NORMAL'])
    indices = [r[0] for r in read(p['indices'])]
    ccw, cw = 0, 0
    for i in range(0, len(indices), 3):
        ids = indices[i:i+3]
        points = [vertices[j] for j in ids]
        center = tuple(sum(q[k] for q in points)/3 for k in range(3))
        normal = tuple(sum(normals[j][k] for j in ids) for k in range(3))
        if dot(normal, sub(camera, center)) <= .05:
            continue
        projected = []
        for point in points:
            v = sub(point, camera)
            w = dot(forward, v)
            assert w > 0
            projected.append((dot(right, v)/w, -dot(up, v)/w))
        a, b, c = projected
        area = -((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
        if abs(area) < 1e-12:
            continue
        if area > 0:
            ccw += 1
        else:
            cw += 1
    assert ccw > 100 and ccw/(ccw+cw) > .9, 'Wolf outward winding changed'
    print(f'{mesh["name"]}: CCW retains {ccw} camera-facing triangles; CW retains {cw}')

# Verify the reason for reverse sampling: in the authored clip the front
# paw's planted stance advances toward the +Z muzzle. Reverse playback must
# move that paw toward the tail. Evaluate exact exported keyframes, including
# the complete parent hierarchy, rather than assuming a Blender axis.
def multiply(a, b):
    return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def globals_at(key):
    trs = [{'translation': n.get('translation', [0, 0, 0]),
            'rotation': n.get('rotation', [0, 0, 0, 1]),
            'scale': n.get('scale', [1, 1, 1])} for n in asset['nodes']]
    for channel in animation['channels']:
        sampler = animation['samplers'][channel['sampler']]
        values = read(sampler['output'])
        path = channel['target']['path']
        if path in trs[0]:
            trs[channel['target']['node']][path] = values[min(key, len(values)-1)]
    children = {child for node in asset['nodes'] for child in node.get('children', [])}
    identity = [[int(r == c) for c in range(4)] for r in range(4)]
    result = {}

    def visit(index, parent):
        node = trs[index]
        x, y, z, w = node['rotation']
        local = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), node['translation'][0]],
                 [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), node['translation'][1]],
                 [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), node['translation'][2]],
                 [0, 0, 0, 1]]
        for r in range(3):
            for c in range(3):
                local[r][c] *= node['scale'][c]
        result[index] = multiply(parent, local)
        for child in asset['nodes'][index].get('children', []):
            visit(child, result[index])
    for index in range(len(asset['nodes'])):
        if index not in children:
            visit(index, identity)
    return result


paw = next(i for i, node in enumerate(asset['nodes']) if node['name'] == 'front.L.paw')
early = globals_at(2)[paw][2][3]
late = globals_at(5)[paw][2][3]
assert late-early > .1, 'Authored stance direction changed; update run-cache playback'
print(f'Authored stance advances {late-early:.3f}m toward +Z; reverse playback moves it toward the tail')
