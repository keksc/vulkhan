"""Add skinned, groomed hair-card LODs to the editable Blender direwolf.
Run: blender -b -noaudio -t 2 -P tools/createWolfHairCards.py
"""
import bpy
import math
import random
import bisect
import os
import sys
import numpy as np
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/wolf'
bpy.ops.wm.open_mainfile(filepath=str(OUT / 'wolf.blend'))
bpy.context.preferences.filepaths.save_version = 0
scene = bpy.context.scene
rig = next(o for o in scene.objects if o.type == 'ARMATURE')
rig.data.pose_position = 'REST'
scene.frame_set(1)
# A layered gray/tan undercoat with a dark dorsal saddle, lighter cheeks,
# irregular clumps and longitudinal fibers. Preserve the bare-skin swatches.
image = bpy.data.images.get('Wolf coat atlas')
if image.packed_file:
    image.unpack(method='REMOVE')
N = 1024
image.scale(N, N)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
u, v = xx/(N-1), yy/(N-1)
height = (v-.04)/.68*1.9
saddle = np.clip((height-.78)/.50, 0, 1)
face = np.exp(-((u-.13)/.14)**4) * np.clip((1.48-height)/.24, 0, 1)
saddle *= 1-face*.90
cream = np.array([.30,.31,.29],dtype=np.float32)
charcoal = np.array([.11,.125,.13],dtype=np.float32)
colour = cream*(1-saddle[:,:,None]) + charcoal*saddle[:,:,None]
rng = np.random.default_rng(4701)
def noise(cell):
    grid = rng.random((int(N/cell)+2,int(N/cell)+2)).astype(np.float32)
    x,z = xx/cell,yy/cell
    ix,iz = x.astype(int),z.astype(int)
    fx,fz = x-ix,z-iz
    fx,fz = fx*fx*(3-2*fx),fz*fz*(3-2*fz)
    return ((1-fx)*(1-fz)*grid[iz,ix]+fx*(1-fz)*grid[iz,ix+1]
            +(1-fx)*fz*grid[iz+1,ix]+fx*fz*grid[iz+1,ix+1])-.5
clumps = noise(83)*.14 + noise(23)*.06 + noise(7)*.025
fibres = np.sin(yy*2.5 + xx*.07 + np.sin(xx*.027)*2)*.009
colour += clumps[:,:,None] + fibres[:,:,None]
colour += face[:,:,None] * np.array([.07,.055,.035],dtype=np.float32)
pixels = np.ones((N,N,4),dtype=np.float32)
pixels[:,:,:3] = np.clip(colour,.025,.65)
swatches=[(.026,.031,.034),(.48,.245,.055),(.007,.01,.013),
          (.07,.037,.035),(.36,.30,.29),(.13,.12,.10)]
for i,c in enumerate(swatches):
    pixels[int(N*.80):,int(i*N/6):int((i+1)*N/6),:3] = c
image.pixels.foreach_set(pixels.ravel())
image.filepath_raw = str(OUT/'wolfCoat.png')
image.file_format = 'PNG'
image.save()
image.pack()
material = bpy.data.materials.get('Direwolf hair cards')
if material is None:
    material = bpy.data.materials.new('Direwolf hair cards')
material.use_nodes = True
material.diffuse_color = (.22, .26, .29, .8)
bsdf = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
bsdf.inputs['Base Color'].default_value = (.22, .26, .29, 1)
bsdf.inputs['Roughness'].default_value = .9
bsdf.inputs['Alpha'].default_value = .8
if hasattr(material, 'surface_render_method'):
    material.surface_render_method = 'DITHERED'
# Procedural strand mask for Blender review. Vulkan uses the equivalent UV
# coverage shader, avoiding a separate texture/descriptor for every card.
nodes, links = material.node_tree.nodes, material.node_tree.links
for n in list(nodes):
    if n.type not in {'BSDF_PRINCIPLED', 'OUTPUT_MATERIAL'}:
        nodes.remove(n)
tex = nodes.new('ShaderNodeTexCoord')
sep = nodes.new('ShaderNodeSeparateXYZ')
links.new(tex.outputs['UV'], sep.inputs[0])
def op(kind, a, b=None):
    n = nodes.new('ShaderNodeMath'); n.operation = kind
    for index, value in enumerate([a] if b is None else [a, b]):
        if isinstance(value, (int, float)):
            n.inputs[index].default_value = value
        else:
            links.new(value, n.inputs[index])
    return n.outputs[0]
u = op('FRACT', sep.outputs['X'])
strands = op('ABSOLUTE', op('SINE', op('MULTIPLY', u, math.pi * 24)))
mask = op('GREATER_THAN', strands, .16)
edge = op('MULTIPLY', op('MULTIPLY', u, op('SUBTRACT', 1, u)), 8)
links.new(op('MULTIPLY', op('MULTIPLY', mask, edge), op('POWER', sep.outputs['Y'], .3)), bsdf.inputs['Alpha'])
# Match the coat colour at each card's actual position, rather than dark
# constant strips sitting on a lighter body.
geometry = nodes.new('ShaderNodeNewGeometry')
position = nodes.new('ShaderNodeSeparateXYZ')
links.new(geometry.outputs['Position'], position.inputs[0])
combine = nodes.new('ShaderNodeCombineXYZ')
links.new(op('DIVIDE', op('ADD', position.outputs['Y'], 1.65), 3.3), combine.inputs['X'])
links.new(op('ADD', .04, op('MULTIPLY', position.outputs['Z'], .68/1.9)), combine.inputs['Y'])
coat = nodes.new('ShaderNodeTexImage')
coat.image = bpy.data.images.get('Wolf coat atlas')
links.new(combine.outputs[0], coat.inputs['Vector'])
tips = nodes.new('ShaderNodeMixRGB')
tips.blend_type = 'MIX'
links.new(op('MULTIPLY', op('SUBTRACT',1,sep.outputs['Y']), .18),tips.inputs[0])
links.new(coat.outputs['Color'],tips.inputs[1])
tips.inputs[2].default_value = (.35,.365,.34,1)
links.new(tips.outputs[0], bsdf.inputs['Base Color'])

# Keep pure skin meshes in the .blend so repeated runs never stack cards.
for level in range(3):
    name = f'Wolf_LOD{level}'
    source_name = f'WolfSkinSource_LOD{level}'
    source = bpy.data.objects.get(source_name)
    old = bpy.data.objects.get(name)
    if source is None:
        source = old.copy(); source.data = old.data.copy()
        scene.collection.objects.link(source)
        source.name = source_name
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    source.hide_render = True
    source.hide_set(True)
    body = source.copy(); body.data = source.data.copy()
    scene.collection.objects.link(body)
    body.name = name
    body.hide_set(False)
    body.hide_render = level != 0
    mesh = body.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active.data
    triangles, cumulative = [], []
    total = 0.
    for triangle in mesh.loop_triangles:
        verts = [mesh.vertices[i] for i in triangle.vertices]
        centre = sum((v.co for v in verts), Vector()) / 3
        normal = sum((v.normal for v in verts), Vector()).normalized()
        # Bare nose/eyes/claws and the lower legs never grow guard-hair cards.
        if min(uv[i].uv.y for i in triangle.loops) > .78 or centre.z < .32:
            continue
        if centre.y < -1.22 or (centre.y < -.92 and centre.z > 1.47):
            continue
        if normal.z < -.7:
            continue
        ruff = -.9 < centre.y < -.38 and centre.z > .85
        tail = centre.y > .83
        back = centre.z > 1.03 and centre.y > -.35
        weight = triangle.area * (4.0 if ruff else 2.5 if tail else 1.5 if back else 1.0)
        if weight <= 0:
            continue
        total += weight
        triangles.append(triangle)
        cumulative.append(total)
    rng = random.Random(4701)
    vertices, normals, faces, coords, weights, coat_coords = [], [], [], [], [], []
    counts = [1600, 650, 200]
    for card in range(counts[level]):
        tri = triangles[bisect.bisect_left(cumulative, rng.random() * total)]
        a, b = rng.random(), rng.random()
        if a+b > 1: a, b = 1-a, 1-b
        blend = [1-a-b, a, b]
        roots = [mesh.vertices[i] for i in tri.vertices]
        root = sum((v.co * w for v,w in zip(roots,blend)), Vector())
        normal = sum((v.normal * w for v,w in zip(roots,blend)), Vector()).normalized()
        emitter_uv = sum((uv[i].uv * w for i,w in zip(tri.loops,blend)), Vector((0,0)))
        group_weights = {}
        for v,w in zip(roots,blend):
            for g in v.groups:
                group_weights[g.group] = group_weights.get(g.group,0.) + g.weight*w
        skin = sorted(group_weights.items(),key=lambda x:-x[1])[:4]
        norm_weight = sum(w for g,w in skin)
        skin = [(g,w/norm_weight) for g,w in skin]
        ruff = -.9 < root.y < -.38 and root.z > .85
        tail = root.y > .83
        cheek = root.y < -.92
        ear = root.z > 1.48 and root.y < -.65
        length = (.012 if ear else .16 if tail else .14 if ruff else .035 if cheek else .055)
        length *= rng.uniform(.7,1.2)
        width = length * rng.uniform(.8, 1.2) if ruff or tail else length * rng.uniform(1.0, 1.6)
        # Neck and cheeks sweep outward/down into the ruff; body guard hairs
        # lie backward, with small variations rather than parallel spikes.
        flow = Vector((normal.x*(.40 if ruff or cheek else .12),
                       .75 if ruff or cheek else 1,
                       -.60 if ruff or cheek else -.30 if tail else -.12))
        flow += Vector((rng.uniform(-.10,.10),0,rng.uniform(-.08,.08)))
        tangent = flow-normal*flow.dot(normal)
        if tangent.length < .01: tangent = Vector((1,0,0))
        tangent.normalize()
        side = normal.cross(tangent).normalized()
        first = len(vertices)
        for row in range(4):
            t = row/3
            loft = .55 if ruff or tail else .28
            centre = root + normal * (.002 + length*loft*(t-.35*t*t)) + tangent*length*t
            half = width*.5*(1-.40*t)
            for column in range(2):
                vertices.append(tuple(centre+side*half*(column*2-1)))
                normals.append(tuple(normal))
                coords.append((card+column,1-t))
                weights.append(skin)
                coat_coords.append(tuple(emitter_uv))
        for row in range(3):
            base = first + row*2
            faces.extend([(base,base+3,base+1),(base,base+2,base+3)])
    cards_mesh = bpy.data.meshes.new(f'Direwolf groom cards {level}')
    cards_mesh.from_pydata(vertices, [], faces)
    cards_mesh.materials.append(material)
    cards = bpy.data.objects.new(f'Groom cards {level}', cards_mesh)
    scene.collection.objects.link(cards)
    cards.parent = rig
    armature = cards.modifiers.new('Shared wolf skeleton', 'ARMATURE')
    armature.object = rig
    for group in body.vertex_groups:
        cards.vertex_groups.new(name=group.name)
    for i, groups in enumerate(weights):
        for group, weight in groups:
            cards.vertex_groups[group].add([i], weight, 'REPLACE')
    layer = cards_mesh.uv_layers.new(name=mesh.uv_layers.active.name)
    for poly in cards_mesh.polygons:
        poly.use_smooth = True
        for loop in poly.loop_indices:
            layer.data[loop].uv = coords[cards_mesh.loops[loop].vertex_index]
    # TEXCOORD_0 defines bundle side/layer; TEXCOORD_1 samples the
    # actual emitting triangle's coat, including UV seams and facial patches.
    coat_layer = cards_mesh.uv_layers.new(name='Fur emitter coat')
    for loop in cards_mesh.loops:
        coat_layer.data[loop.index].uv = coat_coords[loop.vertex_index]
    cards_mesh.uv_layers.active_index = 0
    cards_mesh.normals_split_custom_set_from_vertices(normals)
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True); cards.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    body.name = name
    body.data.uv_layers.active_index = 0
    for i, layer in enumerate(body.data.uv_layers):
        layer.active_render = i == 0
    print(f'{name}: {counts[level]} groom cards, {len(faces)} card triangles', flush=True)

rig.data.pose_position = 'POSE'
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
for o in scene.objects:
    if o.name in [f'Wolf_LOD{i}' for i in range(3)]:
        o.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.export_scene.gltf(filepath=str(ROOT/'models/wolf.glb'), export_format='GLB',
    use_selection=True, export_animations=True, export_force_sampling=True,
    export_animation_mode='ACTIONS', export_skins=True, export_texcoords=True,
    export_normals=True, export_yup=True, export_apply=False)
# Review only the exported card groom, not the older preview-only strands.
for o in scene.objects:
    if o.type == 'CURVE': o.hide_render = True
scene.frame_set(5)
scene.render.resolution_x = 640
scene.render.resolution_y = 480
scene.render.resolution_percentage = 100
scene.cycles.samples = 16
scene.render.filepath = str(OUT/'wolfHairCards.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'wolf.blend'))
if os.environ.get('VULKHAN_SKIP_WOLF_RENDER') != '1':
    bpy.ops.render.render(write_still=True)
print('Hair-card asset and Blender review saved', flush=True)
sys.stdout.flush(); sys.stderr.flush()
os._exit(0)
