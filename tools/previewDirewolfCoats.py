"""Render the three runtime-inspired palettes without changing the asset."""
import bpy
from pathlib import Path

scene = bpy.context.scene
scene.cycles.samples = 16
scene.render.resolution_x, scene.render.resolution_y = 640, 480
scene.render.resolution_percentage = 100
material = bpy.data.materials['Ash-gray direwolf coat']
nodes, links = material.node_tree.nodes, material.node_tree.links
texture = next(n for n in nodes if n.type == 'TEX_IMAGE')
bsdf = nodes.get('Principled BSDF')
uv = nodes.new('ShaderNodeTexCoord')
sep = nodes.new('ShaderNodeSeparateXYZ')
links.new(uv.outputs['UV'], sep.inputs[0])
mask = nodes.new('ShaderNodeMath')
mask.operation = 'LESS_THAN'
mask.inputs[1].default_value = .78
links.new(sep.outputs['Y'], mask.inputs[0])
weight = nodes.new('ShaderNodeMath')
weight.operation = 'MULTIPLY'
links.new(mask.outputs[0], weight.inputs[0])
white = nodes.new('ShaderNodeMixRGB')
links.new(weight.outputs[0], white.inputs[0])
links.new(texture.outputs['Color'], white.inputs[1])
white.inputs[2].default_value = (.72,.75,.78,1)
tint = nodes.new('ShaderNodeMixRGB')
tint.blend_type = 'MULTIPLY'
links.new(mask.outputs[0], tint.inputs[0])
links.new(white.outputs[0], tint.inputs[1])
links.new(tint.outputs[0], bsdf.inputs['Base Color'])
# Change only the iris swatch in this temporary Blender session for the
# pale palette. Do not write the packed image or the .blend back to disk.
image = texture.image
original = list(image.pixels[:])
root = Path('/tmp/vulkhan-direwolf-coats')
root.mkdir(exist_ok=True)
for name, color, whitening in [('gray',(.82,.87,.95,1),0),
                                ('white',(.94,.96,1,1),.9),
                                ('charcoal',(.38,.40,.44,1),0)]:
    tint.inputs[2].default_value = color
    weight.inputs[1].default_value = whitening
    pixels = original.copy()
    if name == 'white':
        n = image.size[0]
        for y in range(int(n*.8), n):
            for x in range(int(n/6), int(2*n/6)):
                offset = 4*(y*n+x)
                pixels[offset:offset+3] = [.22,.018,.014]
    image.pixels.foreach_set(pixels)
    image.update()
    scene.render.filepath = str(root / f'{name}.png')
    bpy.ops.render.render(write_still=True)
print('DIREWOLF_COATS_PREVIEW_COMPLETE')
