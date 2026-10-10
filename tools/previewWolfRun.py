"""Render seven review frames from wolf.blend; no preview-only hairs."""
import bpy
from pathlib import Path
from mathutils import Vector

scene = bpy.context.scene
scene.cycles.samples = 12
scene.render.resolution_x, scene.render.resolution_y = 640, 480
scene.render.resolution_percentage = 100
scene.camera.location = (4.8, -1.8, 2.0)
scene.camera.rotation_euler = (Vector((0, -.1, .85))-scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
scene.camera.data.ortho_scale = 3.8
bpy.data.objects['Preview fur (engine uses shells)'].hide_render = True
out = Path('/tmp/vulkhan-wolf-run')
out.mkdir(exist_ok=True)
for index, frame in enumerate(range(1, 22, 3)):
    scene.frame_set(frame)
    scene.render.filepath = str(out / f'frame{index:02d}.png')
    bpy.ops.render.render(write_still=True)
print('WOLF_RUN_PREVIEW_COMPLETE')
