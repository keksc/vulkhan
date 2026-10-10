"""Hairless, rigged direwolf. Fur is authored as point attributes, never hair geometry.
Run: blender -b -noaudio --factory-startup -t 2 -P tools/createWolf.py
"""
import bpy
import math
import os
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/wolf'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.preferences.filepaths.save_version = 0
scene = bpy.context.scene
scene.render.fps = 30
scene.frame_start, scene.frame_end = 1, 22
parts = []

def ellipsoid(name, center, radius, segments=32, rings=20):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = radius
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    parts.append(obj)
    return obj

def capsule(name, a, b, r1, r2):
    direction = Vector(b)-Vector(a)
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=r1, radius2=r2,
                                  depth=direction.length, location=(Vector(a)+Vector(b))/2)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = 'QUATERNION'
    obj.rotation_quaternion = direction.to_track_quat('Z','Y')
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    parts.append(obj)
    ellipsoid(name+' insertion', a, (r1,r1,r1),24,16)
    return obj

def loft(name, sections):
    """Smooth elliptical cross-sections along the longitudinal Y axis."""
    stations=[]
    for i in range(len(sections)-1):
        p=[Vector(sections[max(0,min(len(sections)-1,i+k-1))]) for k in range(4)]
        for j in range(6):
            t=j/6
            stations.append(.5*(2*p[1]+(-p[0]+p[2])*t+(2*p[0]-5*p[1]+4*p[2]-p[3])*t*t+(-p[0]+3*p[1]-3*p[2]+p[3])*t*t*t))
    stations.append(Vector(sections[-1]))
    vertices=[]
    for y,z,width,height in stations:
        for j in range(40):
            angle=j*math.tau/40
            vertices.append((max(width,.001)*math.sin(angle),y,z+max(height,.001)*math.cos(angle)))
    faces=[]
    for i in range(len(stations)-1):
        for j in range(40):
            a=i*40+j; b=i*40+(j+1)%40
            faces.append((a,b,b+40,a+40))
    faces.extend([tuple(reversed(range(40))),tuple((len(stations)-1)*40+j for j in range(40))])
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],faces)
    mesh.update()
    obj=bpy.data.objects.new(name,mesh)
    scene.collection.objects.link(obj)
    parts.append(obj)
    return obj

# Long-legged adult canid: deep thorax, abdominal tuck, distinct withers,
# sloping croup. These are skin volumes; the engine supplies the winter coat.
loft('Thorax and tucked abdomen', [
    (-.80,1.16,.09,.18),(-.64,1.16,.245,.34),(-.40,1.13,.29,.36),
    (-.12,1.14,.285,.32),(.20,1.18,.225,.255),(.48,1.20,.25,.265),
    (.70,1.18,.25,.25),(.84,1.14,.14,.18),(.90,1.12,.04,.07)])
loft('Sloping neck', [(-1.03,1.57,.105,.15),(-.87,1.49,.175,.22),
    (-.72,1.36,.215,.29),(-.54,1.24,.255,.32),(-.38,1.21,.17,.20)])
loft('Wedge skull and muzzle', [(-1.65,1.485,.061,.042),(-1.51,1.493,.085,.054),
    (-1.33,1.525,.10,.075),(-1.20,1.57,.142,.126),(-1.04,1.60,.171,.166),
    (-.90,1.60,.14,.15),(-.79,1.56,.06,.08)])
loft('Mandible', [(-1.61,1.435,.051,.017),(-1.43,1.431,.081,.032),
    (-1.23,1.444,.109,.044),(-1.04,1.455,.132,.079),(-.92,1.48,.055,.047)])
for sign in [-1,1]:
    # Scapula lies against the ribcage, rather than a spherical shoulder.
    shoulder=ellipsoid('Scapula', (sign*.205,-.47,1.21),(.12,.20,.25))
    shoulder.rotation_euler.x=-.22
    brow=ellipsoid('Supraorbital ridge',(sign*.112,-1.18,1.65),(.038,.065,.018))
    brow.rotation_euler.z=sign*-.16

leg_specs={}
paw_positions={}
for side,x in [('L',.225),('R',-.225)]:
    for kind,hip,knee,ankle,paw in [
        ('front',(x,-.49,1.17),(x,-.43,.72),(x,-.63,.205),(x,-.68,.088)),
        ('hind',(x,.63,1.20),(x,.34,.80),(x,.78,.36),(x,.63,.09))]:
        name=kind+'.'+side
        leg_specs[name]=(Vector(hip),Vector(knee),Vector(ankle))
        paw_positions[name]=Vector(paw)
        if kind=='hind':
            thigh=ellipsoid('Thigh '+side,(x,.54,1.005),(.132,.215,.27))
            thigh.rotation_euler.x=-.36
        capsule(name+' upper',hip,knee,.095 if kind=='front' else .122,.058 if kind=='front' else .073)
        capsule(name+' lower',knee,ankle,.058 if kind=='front' else .065,.034)
        capsule(name+' metapodial',ankle,paw,.036,.050)
        ellipsoid(name+' paw pad',paw,(.078,.105,.066),28,16)
        for toe,dx in enumerate([-.054,-.018,.018,.054]):
            y=paw[1]-.072-(.012 if toe in (1,2) else 0)
            ellipsoid(name+' toe', (x+dx,y,.067),(.024,.062,.049),24,16)

tail_points=[(0,.78,1.19),(0,1.01,1.06),(0,1.24,.79),(0,1.40,.48),(0,1.43,.26)]
for i in range(4):
    capsule('Tail vertebra '+str(i),tail_points[i],tail_points[i+1],
            [.076,.074,.064,.048][i],[.074,.064,.048,.013][i])
ellipsoid('Tail tip',tail_points[-1],(.014,.024,.025),20,12)

# Fuse the anatomy at sufficient resolution to retain ankles, toes and muzzle.
bpy.ops.object.select_all(action='DESELECT')
for obj in parts: obj.select_set(True)
bpy.context.view_layer.objects.active=parts[0]
bpy.ops.object.join()
body=bpy.context.object
body.name='Wolf_LOD0'
bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
body.data.remesh_voxel_size=.012
bpy.ops.object.voxel_remesh()
smooth=body.modifiers.new('Sculpt relaxation','SMOOTH')
smooth.factor=.48
smooth.iterations=4
bpy.ops.object.modifier_apply(modifier=smooth.name)
dec=body.modifiers.new('Surface topology','DECIMATE')
dec.ratio=min(1,42000/max(1,len(body.data.polygons)*2))
bpy.ops.object.modifier_apply(modifier=dec.name)
parts=[body]

# Rounded, cupped ear pinnae. The silhouette is a thick organic shell with
# a convex back, curled rim and recessed interior, not a triangular plate.
for side,sign in [('L',1),('R',-1)]:
    outline=[(-.092,1.695),(-.074,1.815),(-.026,1.945),(.015,1.974),
             (.043,1.925),(.086,1.785),(.083,1.695),(.005,1.665)]
    vertices=[]
    for ring in range(4):
        factor=[1,.78,.45,.02][ring]
        for dx,z in outline:
            vertices.append((sign*(.123+dx*factor),-.945+[-.005,.012,.052,.065][ring],
                             1.70+(1.80+(z-1.80)*factor-1.70)*.70))
    faces=[]
    for ring in range(3):
        for j in range(8): faces.append((ring*8+j,ring*8+(j+1)%8,(ring+1)*8+(j+1)%8,(ring+1)*8+j))
    faces.append(tuple(range(24,32)))
    mesh=bpy.data.meshes.new('Cupped pinna '+side)
    mesh.from_pydata(vertices,[],faces)
    obj=bpy.data.objects.new('Ear '+side,mesh)
    scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active=obj
    obj.select_set(True)
    solid=obj.modifiers.new('Ear cartilage','SOLIDIFY'); solid.thickness=.023
    bpy.ops.object.modifier_apply(modifier=solid.name)
    sub=obj.modifiers.new('Rounded ear rim','SUBSURF'); sub.levels=2
    bpy.ops.object.modifier_apply(modifier=sub.name)
    obj['bone']='ear.'+side
    parts.append(obj)

# Base coat color and six bare-part swatches. No painted fake hair.
N=1024
img=bpy.data.images.new('Wolf coat atlas',width=N,height=N,alpha=True)
yy,xx=np.mgrid[0:N,0:N].astype(np.float32)
u,v=xx/(N-1),yy/(N-1)
height=(v-.04)/.68*2.1
saddle=np.clip((height-.95)/.55,0,1)*.34
cream=np.array([.54,.56,.555],dtype=np.float32)
ash=np.array([.32,.36,.385],dtype=np.float32)
pixels=np.ones((N,N,4),dtype=np.float32)
pixels[:,:,:3]=cream*(1-saddle[:,:,None])+ash*saddle[:,:,None]
swatches=[(.012,.016,.019),(.30,.125,.025),(.002,.003,.004),(.022,.015,.014),(.22,.17,.16),(.055,.05,.043)]
for i,c in enumerate(swatches): pixels[int(N*.8):,int(i*N/6):int((i+1)*N/6),:3]=c
img.pixels.foreach_set(pixels.ravel())
img.filepath_raw=str(OUT/'wolfCoat.png'); img.file_format='PNG'; img.save(); img.pack()
mat=bpy.data.materials.new('Direwolf skin and bare features')
mat.use_nodes=True
bsdf=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
bsdf.inputs['Roughness'].default_value=.82
tex=mat.node_tree.nodes.new('ShaderNodeTexImage'); tex.image=img
mat.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
def uv_coat(obj,swatch=None):
    for layer in list(obj.data.uv_layers): obj.data.uv_layers.remove(layer)
    uv=obj.data.uv_layers.new(name='Coat')
    for poly in obj.data.polygons:
        poly.use_smooth=True
        for li in poly.loop_indices:
            p=obj.matrix_world@obj.data.vertices[obj.data.loops[li].vertex_index].co
            uv.data[li].uv=((swatch+.5)/6,.90) if swatch is not None else (max(.01,min(.99,(p.y+1.8)/3.6)),max(.04,min(.76,.04+p.z/2.1*.68)))
    obj.data.materials.clear(); obj.data.materials.append(mat)
for obj in parts: uv_coat(obj)
def detail(name,center,radius,swatch,bone='head',rotation=None):
    obj=ellipsoid(name,center,radius,32,20)
    if rotation: obj.rotation_mode='QUATERNION'; obj.rotation_quaternion=rotation
    uv_coat(obj,swatch); obj['bone']=bone
    return obj
nose=detail('Nose leather',(0,-1.651,1.484),(.077,.039,.049),0)
for vtx in nose.data.vertices:
    vtx.co.x*=.84+.16*(vtx.co.z/.049+1)*.5
    vtx.co.z=min(vtx.co.z,.041)
for sign in [-1,1]:
    detail('Nostril',(sign*.047,-1.682,1.487),(.017,.006,.010),2)
# Small inset eyes, angled forward: no exposed white sclera or protruding bulbs.
for side,sign in [('L',1),('R',-1)]:
    n=Vector((sign*.80,-.59,.075)).normalized()
    h=Vector((-n.y,n.x,0)).normalized()
    z=h.cross(n).normalized()
    if z.z<0: h=-h; z=-z
    rotation=Matrix((h,n,z)).transposed().to_quaternion()
    center=Vector((sign*.137,-1.20,1.618))
    detail('Dark eyelid '+side,center,(.029,.009,.015),0,rotation=rotation)
    detail('Amber iris '+side,center+n*.010,(.0105,.004,.0105),1,rotation=rotation)
    detail('Round pupil '+side,center+n*.0135,(.0045,.0015,.0065),2,rotation=rotation)

def lip(side,sign):
    curve=bpy.data.curves.new('Mouth line '+side,'CURVE'); curve.dimensions='3D'
    curve.bevel_depth=.0027; curve.bevel_resolution=2
    spline=curve.splines.new('BEZIER'); spline.bezier_points.add(3)
    for p,co in zip(spline.bezier_points,[(sign*.054,-1.615,1.447),(sign*.085,-1.455,1.449),(sign*.109,-1.315,1.460),(sign*.135,-1.182,1.478)]):
        p.co=co; p.handle_left_type=p.handle_right_type='AUTO'
    obj=bpy.data.objects.new('Lip '+side,curve); scene.collection.objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.object.convert(target='MESH'); obj=bpy.context.object
    obj['bone']='jaw'; parts.append(obj); uv_coat(obj,3)
for side,sign in [('L',1),('R',-1)]: lip(side,sign)
for name,paw in paw_positions.items():
    for dx in [-.054,-.018,.018,.054]:
        detail('Claw '+name,(paw.x+dx,paw.y-.127,.047),(.008,.024,.009),5,name+'.paw')

# The same 24-bone rig and baked clip remain usable by the wildlife system.
bpy.ops.object.select_all(action='DESELECT')
bpy.ops.object.armature_add(); rig=bpy.context.object; rig.name='Wolf_Rig'
bpy.ops.object.mode_set(mode='EDIT'); rig.data.edit_bones.remove(rig.data.edit_bones[0])
def bone(name,a,b,parent=None):
    e=rig.data.edit_bones.new(name); e.head,e.tail=a,b
    if parent: e.parent=rig.data.edit_bones[parent]
    return e
bone('root',(0,.4,1.20),(0,.4,1.35))
bone('spine',(0,.60,1.20),(0,-.08,1.22),'root')
bone('chest',(0,-.08,1.22),(0,-.57,1.30),'spine')
bone('neck',(0,-.57,1.30),(0,-.93,1.57),'chest')
bone('head',(0,-.93,1.57),(0,-1.48,1.51),'neck')
bone('jaw',(0,-1.03,1.46),(0,-1.51,1.435),'head')
for side,sign in [('L',1),('R',-1)]: bone('ear.'+side,(sign*.123,-.92,1.71),(sign*.13,-.91,1.95),'head')
for name,(hip,knee,ankle) in leg_specs.items():
    bone(name+'.upper',hip,knee,'chest' if name.startswith('front') else 'spine')
    bone(name+'.lower',knee,ankle,name+'.upper')
    bone(name+'.paw',ankle,paw_positions[name],name+'.lower')
for i in range(4): bone('tail'+str(i),tail_points[i],tail_points[i+1],'root' if i==0 else 'tail'+str(i-1))
bpy.ops.object.mode_set(mode='OBJECT'); rig.show_in_front=True

def segment_dist(p,b):
    a,d=b.head_local,b.tail_local-b.head_local
    t=max(0,min(1,(p-a).dot(d)/d.length_squared))
    return (p-a-d*t).length
for obj in parts:
    groups={b.name:obj.vertex_groups.new(name=b.name) for b in rig.data.bones}
    for vertex in obj.data.vertices:
        p=obj.matrix_world@vertex.co
        if 'bone' in obj: weights=[(obj['bone'],1)]
        else:
            if p.y>.9: candidates=['tail'+str(i) for i in range(4)]
            elif p.z<.78 and abs(p.x)>.11:
                name=('front' if p.y<0 else 'hind')+('.L' if p.x>0 else '.R')
                candidates=[name+'.'+s for s in ['upper','lower','paw']]
            elif p.y<-.94: candidates=['head','neck','jaw']
            else:
                candidates=['spine','chest','neck','head','tail0']
                if abs(p.x)>.15:
                    side='L' if p.x>0 else 'R'
                    candidates+=['front.'+side+'.upper','hind.'+side+'.upper']
            ranked=sorted((segment_dist(p,rig.data.bones[n]),n) for n in candidates)[:3]
            raw=[math.exp(-((d-ranked[0][0])/.10)**2)*math.exp(-d*3) for d,n in ranked]
            weights=[(n,w/sum(raw)) for (_,n),w in zip(ranked,raw)]
        for n,w in weights:
            if w>1e-5: groups[n].add([vertex.index],w,'REPLACE')
    mod=obj.modifiers.new('Wolf skeleton','ARMATURE'); mod.object=rig; obj.parent=rig
bpy.ops.object.select_all(action='DESELECT')
for obj in parts: obj.select_set(True)
bpy.context.view_layer.objects.active=body; bpy.ops.object.join(); body.name='Wolf_LOD0'
for mod in list(body.modifiers):
    if mod.type!='ARMATURE': body.modifiers.remove(mod)

# 2-bone analytic foot placement, sampled into ordinary joint TRS channels.
# All targets are baked; the engine does not need Blender IK constraints.
for pb in rig.pose.bones: pb.rotation_mode='QUATERNION'
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
ordered=list(rig.data.bones)

def aim(name,head,tail):
    r=rest[name].to_quaternion()
    delta=(rig.data.bones[name].tail_local-rig.data.bones[name].head_local).rotation_difference(tail-head)
    return Matrix.Translation(head) @ (delta@r).to_matrix().to_4x4()

for frame in range(1,23):
    phase=(frame-1)/21
    theta=phase*math.tau
    poses={}
    for b in ordered:
        local=Matrix.Identity(4)
        if b.name=='root':
            local=Matrix.Translation((0,0,.028*(1-math.cos(theta*2))))
            poses[b.name]=local @ rest[b.name]
            continue
        if b.name=='chest':
            axis=rest[b.name].to_quaternion().inverted() @ Vector((1,0,0))
            local=Quaternion(axis,.045*math.sin(theta)).to_matrix().to_4x4()
        elif b.name=='neck':
            axis=rest[b.name].to_quaternion().inverted() @ Vector((1,0,0))
            local=Quaternion(axis,-.04*math.sin(theta)).to_matrix().to_4x4()
        elif b.name.startswith('tail'):
            axis=rest[b.name].to_quaternion().inverted() @ Vector((0,0,1))
            local=Quaternion(axis,.06*math.sin(theta+int(b.name[-1])*.6)).to_matrix().to_4x4()
        parent=poses[b.parent.name]
        poses[b.name]=parent @ rest[b.parent.name].inverted() @ rest[b.name] @ local
    for name,(hip,knee,ankle) in leg_specs.items():
        offset={'front.L':0.,'front.R':.12,'hind.L':.52,'hind.R':.64}[name]
        p=(phase+offset)%1
        stance=.40
        if p<stance:
            y=hip.y+.46-.92*(p/stance)
            z=ankle.z
        else:
            t=(p-stance)/(1-stance)
            y=hip.y-.46+.92*(t*t*(3-2*t))
            z=ankle.z+.24*math.sin(math.pi*t)
        start=poses[name+'.upper'].translation
        target=Vector((hip.x,y,z))
        d=target-start
        l1=(knee-hip).length
        l2=(ankle-knee).length
        length=min(d.length,l1+l2-.003)
        direction=d.normalized()
        target=start+direction*length
        cos_a=max(-1,min(1,(l1*l1+length*length-l2*l2)/(2*l1*length)))
        perpendicular=Vector((0,-direction.z,direction.y))
        if name.startswith('hind'): perpendicular=-perpendicular
        joint=start+direction*(l1*cos_a)+perpendicular*(l1*math.sqrt(1-cos_a*cos_a))
        poses[name+'.upper']=aim(name+'.upper',start,joint)
        poses[name+'.lower']=aim(name+'.lower',joint,target)
        poses[name+'.paw']=Matrix.Translation(target) @ rest[name+'.paw'].to_quaternion().to_matrix().to_4x4()
    for b in ordered:
        pb=rig.pose.bones[b.name]
        kwargs={}
        if b.parent:
            kwargs={'parent_matrix':poses[b.parent.name], 'parent_matrix_local':rest[b.parent.name]}
        pb.matrix_basis=b.convert_local_to_pose(poses[b.name],rest[b.name],invert=True,**kwargs)
        pb.keyframe_insert('location',frame=frame)
        pb.keyframe_insert('rotation_quaternion',frame=frame)
        pb.keyframe_insert('scale',frame=frame)
rig.animation_data.action.name='Run'
scene.frame_set(1)
# Lower geometry tiers keep the same skin and atlas.
for level,ratio in [(1,.38),(2,.12)]:
    obj=body.copy()
    obj.data=body.data.copy()
    scene.collection.objects.link(obj)
    obj.name='Wolf_LOD'+str(level)
    # Apply decimation in REST pose, before the skin modifier.
    modifier=obj.modifiers.new('LOD simplify','DECIMATE')
    modifier.ratio=ratio
    bpy.context.view_layer.objects.active=obj
    bpy.ops.object.modifier_move_up(modifier=modifier.name)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.hide_render=True


# Point-domain grooming instructions. Blender contains zero strands or cards.
# Direction is stored explicitly in glTF's Y-up coordinates because generic
# custom attributes are not coordinate-converted by the Blender exporter.
def smoothstep(a,b,x):
    t=max(0,min(1,(x-a)/(b-a)))
    return t*t*(3-2*t)
for level in range(3):
    obj=bpy.data.objects[f'Wolf_LOD{level}']
    mesh=obj.data
    params=mesh.attributes.new('_VKH_FUR','FLOAT_VECTOR','POINT')
    flows=mesh.attributes.new('_VKH_FLOW','FLOAT_VECTOR','POINT')
    bare=set()
    for loop in mesh.loops:
        if mesh.uv_layers[0].data[loop.index].uv.y>.79: bare.add(loop.vertex_index)
    for vertex in mesh.vertices:
        p=vertex.co; n=vertex.normal
        ruff=math.exp(-((p.y+.65)/.28)**2)*smoothstep(.8,1.1,p.z)
        tail=smoothstep(.82,1.02,p.y)
        face=1-smoothstep(-1.38,-.99,p.y)
        leg=1-smoothstep(.48,.88,p.z)
        ear=smoothstep(1.69,1.82,p.z)
        length=.054+.065*ruff+.095*tail
        length=length*(1-face)+(.010+.025*smoothstep(-1.48,-1.12,p.y))*face
        length=length*(1-leg)+.012*leg
        length=length*(1-ear)+.010*ear
        flow=Vector((n.x*(.35*ruff+.6*face),1,-.24-.55*ruff))
        flow=flow.lerp(Vector((n.x*.12,.20,-1)),max(leg,tail))
        if ear>.5: flow=Vector((0,.12,1))
        flow.normalize()
        density=0 if vertex.index in bare else 1
        params.data[vertex.index].vector=(length if density else 0,density,.48+.22*ruff+.20*tail)
        flows.data[vertex.index].vector=(flow.x,flow.z,-flow.y)
    obj['vkh_fur_version']=1
    obj['vkh_fur_units']='metres; _VKH_FUR=(length,density,lift); _VKH_FLOW=glTF-space direction'
    obj.hide_set(level!=0)

# Apply the same refinements used interactively through the Blender connector.
import importlib.util
spec = importlib.util.spec_from_file_location('refineWolf', ROOT/'tools/refineWolf.py')
refinement = importlib.util.module_from_spec(spec)
spec.loader.exec_module(refinement)
refinement.refine_wolf(scene)
spec = importlib.util.spec_from_file_location('matchWolfReferences', ROOT/'tools/matchWolfReferences.py')
reference_match = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference_match)
reference_match.match_references()
reference_match.refine_features()
reference_match.finish_face()
img.save()
rig.data.pose_position='POSE'

# Export skin/animation plus numerical groom attributes only.
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
for level in range(3):
    obj=bpy.data.objects[f'Wolf_LOD{level}']; obj.hide_set(False); obj.select_set(True)
bpy.context.view_layer.objects.active=rig
bpy.ops.export_scene.gltf(filepath=str(ROOT/'models/wolf.glb'),export_format='GLB',
    use_selection=True,export_animations=True,export_force_sampling=True,
    export_animation_mode='ACTIONS',export_skins=True,export_texcoords=True,
    export_normals=True,export_yup=True,export_apply=False,
    export_attributes=True,export_extras=True)
rig.data.pose_position='REST'
for level in range(1,3): bpy.data.objects[f'Wolf_LOD{level}'].hide_set(True)

# Hairless anatomical review: neutral studio and fixed standing pose.
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.003))
plane=bpy.context.object; plane.name='Studio floor (not exported)'
stage=bpy.data.materials.new('Studio slate'); stage.use_nodes=True
bsdf=next(n for n in stage.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
bsdf.inputs['Base Color'].default_value=(.095,.12,.145,1)
bsdf.inputs['Roughness'].default_value=.9
plane.data.materials.append(stage)
world=bpy.data.worlds.new('Neutral studio'); scene.world=world; world.use_nodes=True
background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
background.inputs[0].default_value=(.32,.36,.42,1); background.inputs[1].default_value=.4
for name,pos,power,size in [('Key',(-3,-4,5),750,4),('Rim',(2,3,4),800,3),('Fill',(4,-2,2.5),400,3)]:
    bpy.ops.object.light_add(type='AREA',location=pos)
    obj=bpy.context.object; obj.name=name; obj.data.energy=power; obj.data.shape='DISK'; obj.data.size=size
    obj.rotation_euler=(Vector((0,-.1,1))-obj.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(4,-5,2.5))
cam=bpy.context.object; cam.name='Anatomy camera'
cam.rotation_euler=(Vector((0,-.05,1.0))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO'; cam.data.ortho_scale=3.85; scene.camera=cam
scene.render.engine='CYCLES'; scene.cycles.device='CPU'; scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.render.resolution_x,scene.render.resolution_y=1200,900
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.filepath=str(OUT/'wolfAnatomy.png')
bpy.ops.object.select_all(action='DESELECT'); body.select_set(True); bpy.context.view_layer.objects.active=body
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'wolf.blend'))
if os.environ.get('VULKHAN_SKIP_WOLF_RENDER')!='1': bpy.ops.render.render(write_still=True)
print('HAIRLESS_WOLF_COMPLETE',len(body.data.vertices),len(body.data.polygons),len(rig.data.bones),flush=True)
# Avoid audio teardown hanging in background Blender on PipeWire-less workers.
import sys
sys.stdout.flush(); sys.stderr.flush(); os._exit(0)
