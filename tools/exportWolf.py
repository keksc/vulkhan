"""Export the edited hairless wolf and its numerical fur attributes.
Run: blender -b -noaudio -t 2 -P tools/exportWolf.py
Save Blender edits to assets/wolf/wolf.blend first.
"""
import bpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'assets/wolf/wolf.blend'))
rig = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
rig.data.pose_position = 'POSE'
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
for level in range(3):
    obj = bpy.data.objects[f'Wolf_LOD{level}']
    obj.hide_set(False)
    obj.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.export_scene.gltf(
    filepath=str(ROOT / 'models/wolf.glb'), export_format='GLB',
    use_selection=True, export_animations=True, export_force_sampling=True,
    export_animation_mode='ACTIONS', export_skins=True, export_texcoords=True,
    export_normals=True, export_yup=True, export_apply=False,
    export_attributes=True, export_extras=True)
print('Exported hairless wolf, surface fur attributes and run animation')
