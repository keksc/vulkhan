"""Reference proportion pass on the existing hairless wolf, through Blender MCP.

Preserves vertex order, weights, UVs, LODs and engine fur attributes.
Run after refineWolf.py on a fresh source, or once on the saved Blender asset.
"""
import bpy
import math
import bmesh
from mathutils import Vector


def ramp(a, b, x):
    t = max(0.0, min(1.0, (x-a)/(b-a)))
    return t*t*(3-2*t)


def reshape(point):
    x, y, z = point
    xx, yy, zz = x, y, z
    head = (1-ramp(-.98, -.69, y))*ramp(1.21, 1.46, z)
    # Broader braincase, deeper muzzle, lifted head carried above the withers.
    xx *= 1 + .24*head
    yy += .075*(1-ramp(-1.40, -1.12, y))*head
    zz += head*(.075 + (z-1.56)*.15)
    # Compact ear tips, avoiding the tall fox-like proportion.
    zz -= .035*ramp(1.73, 1.90, z)
    # A stronger neck under the engine's long winter ruff.
    neck = math.exp(-((y+.77)/.25)**2)*ramp(.98, 1.4, z)
    xx *= 1+.11*neck
    zz += .035*neck
    # Lower the croup and lift the excessively low brisket slightly.
    zz -= .062*math.exp(-((y-.57)/.32)**2)*ramp(.80, 1.42, z)
    zz += .045*math.exp(-((y+.42)/.36)**2)*(1-ramp(.85,1.15,z))*ramp(.61,.78,z)
    # Straighter forelegs; thicker metacarpals and substantial canine feet.
    leg = (1-ramp(.69,.96,z))*ramp(.09,.19,abs(x))
    if y < -.16:
        center_y = -.63 + (.20*(max(.205,min(.72,z))-.205)/.515)
        yy += leg*(-.565-center_y)
    paw = 1-ramp(.12,.24,z)
    cx = .225 if x >= 0 else -.225
    xx += (x-cx)*(.20*leg+.14*paw)
    if z < .2:
        py = -.68 if y < 0 else .63
        yy += (y-py)*.10*paw
        zz += (z-.025)*.12*paw
    # Smoothly thicken the tail core, leaving its plume to the engine.
    tail = ramp(.86,1.1,y)
    xx *= 1+.30*tail
    return Vector((xx,yy,zz))


def match_references():
    for level in range(3):
        obj=bpy.data.objects[f'Wolf_LOD{level}']
        if obj.get('reference_proportions',0):
            continue
        for vertex in obj.data.vertices:
            vertex.co=reshape(vertex.co)
        obj.data.update()
        obj['reference_proportions']=1
    rig=bpy.data.objects['Wolf_Rig']
    if not rig.get('reference_proportions',0):
        bpy.ops.object.select_all(action='DESELECT')
        rig.hide_set(False)
        rig.select_set(True)
        bpy.context.view_layer.objects.active=rig
        bpy.ops.object.mode_set(mode='EDIT')
        for bone in rig.data.edit_bones:
            bone.head=reshape(bone.head)
            bone.tail=reshape(bone.tail)
        bpy.ops.object.mode_set(mode='OBJECT')
        rig['reference_proportions']=1
    rig.data.pose_position='REST'
    bpy.ops.object.select_all(action='DESELECT')
    body=bpy.data.objects['Wolf_LOD0']
    body.select_set(True)
    bpy.context.view_layer.objects.active=body
    print('Reference proportions applied across all LODs and bone rest positions')


def refine_features():
    for level in range(3):
        obj=bpy.data.objects[f'Wolf_LOD{level}']
        if obj.get('reference_features',0):
            continue
        mesh=obj.data
        bare=set()
        iris=set()
        eyelid=set()
        for loop in mesh.loops:
            uv=mesh.uv_layers[0].data[loop.index].uv
            if uv.y>.79:
                bare.add(loop.vertex_index)
                if .1667<uv.x<.3334:
                    iris.add(loop.vertex_index)
                if uv.x<.1667:
                    eyelid.add(loop.vertex_index)
        for v in mesh.vertices:
            x,y,z=v.co
            # Fill the bridge and masseter, with a restrained stop above the nose.
            if v.index not in bare and y<-.95:
                bridge=math.exp(-((y+1.31)/.16)**2)*math.exp(-((z-1.62)/.08)**2)
                v.co.z+=.018*bridge
                cheek=math.exp(-((y+1.035)/.14)**2)*math.exp(-((z-1.57)/.12)**2)
                v.co.x*=1+.12*cheek
            # Flatten the lip corner and the surrounding lower cheek together.
            v.co.z-=.018*math.exp(-((y+1.17)/.095)**2)*math.exp(-((z-1.55)/.045)**2)
        bm=bmesh.new();bm.from_mesh(mesh)
        selected=[v for v in bm.verts if v.index not in bare and abs(v.co.x)>.17 and
                  ((-.69<v.co.y<-.19 and .82<v.co.z<1.53) or
                   (.30<v.co.y<.88 and .71<v.co.z<1.48))]
        for _ in range(14):
            bmesh.ops.smooth_vert(bm,verts=selected,factor=.40,
                                 use_axis_x=True,use_axis_y=True,use_axis_z=True)
        bm.to_mesh(mesh);bm.free();mesh.update()
        obj['reference_features']=1
    # Preserve the engine's one-material primitive and embedded atlas contract.
    import numpy as np
    atlas=bpy.data.images['Wolf coat atlas']
    pixels=np.array(atlas.pixels[:],dtype=np.float32).reshape(1024,1024,4)
    pixels[819:,170:341,:3]=(.24,.018,.012)
    atlas.pixels.foreach_set(pixels.ravel());atlas.update();atlas.pack()
    print('Smoothed limb transitions; refined muzzle, lips, nose and garnet eyes')


def finish_face():
    import numpy as np
    atlas=bpy.data.images['Wolf coat atlas']
    pixels=np.array(atlas.pixels[:],dtype=np.float32).reshape(1024,1024,4)
    yy,xx=np.mgrid[0:205,0:171]
    u,v=xx/170,yy/204
    edge=np.minimum(v-.18, np.minimum((u-.5)*1.95+(.87-v),(.5-u)*1.95+(.87-v)))
    t=np.clip(edge/.10,0,1);t=t*t*(3-2*t)
    pixels[819:1024,682:853,:3]=np.array([.51,.54,.55])[None,None,:]*(1-t[:,:,None])+np.array([.19,.135,.13])[None,None,:]*t[:,:,None]
    atlas.pixels.foreach_set(pixels.ravel());atlas.update();atlas.pack()
    for level in range(3):
        obj=bpy.data.objects[f'Wolf_LOD{level}'];m=obj.data
        if obj.get('reference_face_finish',0):continue
        adjacency=[[] for _ in m.vertices]
        for e in m.edges:
            a,b=e.vertices;adjacency[a].append(b);adjacency[b].append(a)
        seen=set();ear_uv={}
        for vert in m.vertices:
            if vert.index in seen:continue
            stack=[vert.index];seen.add(vert.index);ids=[]
            while stack:
                i=stack.pop();ids.append(i)
                for j in adjacency[i]:
                    if j not in seen:seen.add(j);stack.append(j)
            c=sum((m.vertices[i].co for i in ids),Vector())/len(ids)
            if c.z>1.80 and len(ids)<2000:
                lo=Vector(tuple(min(m.vertices[i].co[k] for i in ids) for k in range(3)))
                hi=Vector(tuple(max(m.vertices[i].co[k] for i in ids) for k in range(3)))
                for i in ids:
                    p=m.vertices[i].co
                    # Smooth atlas coordinates instead of per-face color boundaries.
                    if m.vertices[i].normal.y<-.08:
                        u=(p.x-lo.x)/(hi.x-lo.x);v=(p.z-lo.z)/(hi.z-lo.z)
                        ear_uv[i]=((682+u*170)/1024,(819+v*204)/1024)
                        m.attributes['_VKH_FUR'].data[i].vector=(0,0,.5)
            if -1.52<c.y<-1.48 and abs(c.x)>.03 and c.z<1.60 and len(ids)<1000:
                for i in ids:
                    p=m.vertices[i].co
                    p.y=c.y+(p.y-c.y)*.15-.003
                    p.x=c.x+(p.x-c.x)*1.20
            if -1.20<c.y<-1.15 and 1.69<c.z<1.72 and .19<abs(c.x)<.198:
                for i in ids:m.vertices[i].co=c+(m.vertices[i].co-c)*1.30
            if -1.20<c.y<-1.15 and 1.69<c.z<1.72 and .198<abs(c.x)<.205:
                normal=Vector((.80 if c.x>0 else -.80,-.59,.075)).normalized()
                for i in ids:m.vertices[i].co+=normal*.004
        for loop in m.loops:
            if loop.vertex_index in ear_uv:m.uv_layers[0].data[loop.index].uv=ear_uv[loop.vertex_index]
        m.update();obj['reference_face_finish']=1
    print('Smooth ear inset atlas, flattened dark nares, larger irises')


if __name__=='__main__':
    match_references()
    refine_features()
    finish_face()
