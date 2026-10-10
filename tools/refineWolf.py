"""Final anatomical refinement, usable in the connected Blender or generator.
Does not reopen the file or create hair geometry. Applied once per fresh mesh.
"""
import bpy
import bmesh
import math


def refine_wolf(scene):
    for level in range(3):
        obj = bpy.data.objects[f'Wolf_LOD{level}']
        if obj.get('vkh_anatomy_refinement', 0) == 1:
            continue
        mesh = obj.data
        for vertex in mesh.vertices:
            p = vertex.co
            # Less fox-like facial length; maintain all bare-feature alignment.
            if p.y < -1.12:
                p.y = -1.12 + (p.y + 1.12) * .80
                p.x *= 1.08
            # Raise the withers slightly above the croup.
            if p.z > 1.05:
                p.z += .04 * math.exp(-((p.y + .46)/.29)**2) * min(1,(p.z-1.05)/.25)
        bm = bmesh.new()
        bm.from_mesh(mesh)
        selected = [v for v in bm.verts if abs(v.co.x) > .17 and
                    ((-.67 < v.co.y < -.20 and v.co.z > .78) or
                     (.35 < v.co.y < .78 and v.co.z > .72))]
        for _ in range(5):
            bmesh.ops.smooth_vert(bm, verts=selected, factor=.35,
                                 use_axis_x=True, use_axis_y=True, use_axis_z=True)
        bm.to_mesh(mesh)
        bm.free()
        mesh.update()
        # Warm, subdued cartilage in the recessed portion of each ear.
        ear_groups = {g.index for g in obj.vertex_groups if g.name.startswith('ear.')}
        bare = set()
        for vertex in mesh.vertices:
            p = vertex.co
            if (any(g.group in ear_groups and g.weight > .8 for g in vertex.groups)
                    and -.925 < p.y < -.87 and 1.725 < p.z < 1.85
                    and abs(abs(p.x)-.123) < .042 and vertex.normal.y < -.3):
                bare.add(vertex.index)
        for loop in mesh.loops:
            if loop.vertex_index in bare:
                mesh.uv_layers[0].data[loop.index].uv = (4.5/6,.90)
        params = mesh.attributes['_VKH_FUR']
        for i in bare:
            params.data[i].vector = (0,0,.5)
        obj['vkh_anatomy_refinement'] = 1
    print('Refined all three skin LODs: muzzle, withers, shoulder/thigh transitions and inner ears')

if __name__ == '__main__':
    refine_wolf(bpy.context.scene)
