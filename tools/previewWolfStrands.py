"""Static Blender review of the runtime guides, not an engine screenshot.
Run: blender -b -noaudio -t 2 -P tools/previewWolfStrands.py
"""
import bpy
import bmesh
import math
import random
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'assets/wolf/wolf.blend'))
scene = bpy.context.scene
scene.frame_set(5)
source = bpy.data.objects['Wolf_LOD0']
evaluated = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
mesh = bpy.data.meshes.new_from_object(evaluated)
card_material = next(i for i, m in enumerate(mesh.materials)
                     if m.name == 'Direwolf hair cards')
parent = list(range(len(mesh.vertices)))
def root(i):
    while parent[i] != i:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i
uv = {}
coat_uv = {}
for face in mesh.polygons:
    if face.material_index != card_material:
        continue
    for loop in face.loop_indices:
        i = mesh.loops[loop].vertex_index
        parent[root(i)] = root(face.vertices[0])
        uv[i] = mesh.uv_layers[0].data[loop].uv.copy()
        coat_uv[i] = mesh.uv_layers[1].data[loop].uv.copy()
bundles = {}
for i in uv:
    bundles.setdefault(root(i), []).append(i)
# Retain only the skinned opaque body for this frozen-frame review.
bm = bmesh.new()
bm.from_mesh(mesh)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index == card_material], context='FACES')
bm.to_mesh(mesh)
bm.free()
mesh.uv_layers.active_index = 0
for i, layer in enumerate(mesh.uv_layers):
    layer.active_render = i == 0
body = bpy.data.objects.new('Strand review body', mesh)
scene.collection.objects.link(body)
body.matrix_world = source.matrix_world.copy()
source.hide_render = True
for o in scene.objects:
    if o.type == 'CURVE':
        o.hide_render = True
# Use evaluated original guide positions, before deleting the card faces.
guide_mesh = evaluated.to_mesh()
curve = bpy.data.curves.new('Runtime guide strand review', 'CURVE')
curve.dimensions = '3D'
curve.resolution_u = 1
curve.bevel_depth = .0003
curve.bevel_resolution = 0
atlas = bpy.data.images['Wolf coat atlas']
pixels = list(atlas.pixels)
W, H = atlas.size
palette = {}
rng = random.Random(4701)
for indices in bundles.values():
    # Blender V is tip-to-root; glTF flips it on export.
    indices.sort(key=lambda i: (-uv[i].y, uv[i].x))
    points = [guide_mesh.vertices[i].co.copy() for i in indices]
    normal = (guide_mesh.vertices[indices[0]].normal + guide_mesh.vertices[indices[1]].normal).normalized()
    cuv = coat_uv[indices[0]]
    offset = (min(H-1, int(cuv.y*H))*W + min(W-1, int(cuv.x*W)))*4
    color = tuple(round(pixels[offset+i]*24)/24 for i in range(3))
    if color not in palette:
        material = bpy.data.materials.new('Undercoat %d' % len(palette))
        material.diffuse_color = (*color, 1)
        material.use_nodes = True
        bsdf = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        bsdf.inputs['Base Color'].default_value = (*color, 1)
        bsdf.inputs['Roughness'].default_value = .75
        palette[color] = len(curve.materials)
        curve.materials.append(material)
    for strand in range(64):
        u, variation, phase, tip = rng.random(), rng.uniform(-1,1), rng.random()*math.tau, rng.uniform(.72,1)
        spline = curve.splines.new('POLY')
        spline.points.add(8)
        spline.material_index = palette[color]
        for j, point in enumerate(spline.points):
            t = tip*j/8
            at = t*3
            layer = min(int(at),2)
            f = at-layer
            p = []
            for k in range(4):
                row = max(0,min(3,layer+k-1))*2
                p.append(points[row].lerp(points[row+1],u))
            v = .5*(2*p[1]+(-p[0]+p[2])*f+(2*p[0]-5*p[1]+4*p[2]-p[3])*f*f+(-p[0]+3*p[1]-3*p[2]+p[3])*f*f*f)
            v += normal*(variation*.009*t+.003*math.sin(t*5+phase)*t*t)
            point.co = (*v, 1)
            point.radius = 1-.65*t
strands = bpy.data.objects.new('Runtime strands (static review)', curve)
scene.collection.objects.link(strands)
strands.matrix_world = source.matrix_world.copy()
evaluated.to_mesh_clear()
scene.render.resolution_x = 1000
scene.render.resolution_y = 750
scene.render.resolution_percentage = 100
scene.cycles.samples = 32
scene.render.filepath = str(ROOT / 'assets/wolf/wolfStrands.png')
bpy.ops.render.render(write_still=True)
print('Rendered %d bundles as %d static preview strands' % (len(bundles),len(curve.splines)),flush=True)
