"""Editor-only additive ground geometry. Never starts PIE or changes gameplay actors."""
import unreal, math, json
from pathlib import Path

ROOT = '/Game/Stage01/GroundStructure'
GROUPS = {}

def tri(group, a, b, c):
    GROUPS.setdefault(group, []).append((a,b,c))

def quad(group, a,b,c,d):
    # All ribbons are top surfaces. Keep upward-facing triangle winding.
    if (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]) < 0:
        a,b,c,d = d,c,b,a
    tri(group,a,b,c); tri(group,a,c,d)

def ell(rx,ry,angle,z,cx=120,cy=-1510):
    t=math.radians(angle)
    return (cx+rx*math.cos(t),cy+ry*math.sin(t),z)

def ring(group,ri,ro,a,b,z,cy=-1510,cx=120,ratio=1):
    quad(group,ell(ri,ri*ratio,a,z,cx,cy),ell(ro,ro*ratio,a,z,cx,cy),
         ell(ro,ro*ratio,b,z,cx,cy),ell(ri,ri*ratio,b,z,cx,cy))

# Cage: low foundation, bevel rim, twelve individually separated floor panels.
ratio=600/650
for i in range(16):
    a=i*22.5; b=(i+1)*22.5
    tri('CageBase',(120,-1510,2.8),ell(650,600,a,2.8),ell(650,600,b,2.8))
    quad('CageBase',ell(650,600,a,2.8),ell(618,570,a,8),ell(618,570,b,8),ell(650,600,b,2.8))
    ring('CageBase',596,618,a,b,8,ratio=ratio)
for i in range(12):
    a=i*30+0.65; b=(i+1)*30-0.65
    ring('CagePanels' if i%2==0 else 'CagePanelsCool',305,586,a,b,3.2,ratio=ratio)
    if i%3 != 0:
        ring('WarningRing',541,566,a+2,b-2,3.6,ratio=ratio)
for i in range(16):
    tri('CagePanelsCool',(120,-1510,3),ell(302,279,i*22.5,3),ell(302,279,(i+1)*22.5,3))
for angle in [0,90,180,270]:
    ring('CyanGuides',575,632,angle-8,angle+8,8.4,ratio=ratio)

# Dirt road edges: connected earth cut + broad raised turf shoulder, with tapered ends.
# Existing 380 cm road remains entirely untouched. Crossings stay open.
for side in [-1,1]:
    for i in range(64):
        a=i*math.tau/64; b=(i+1)*math.tau/64
        mid=(a+b)/2
        cardinal=min(abs((mid-k*math.pi/2+math.pi)%(2*math.pi)-math.pi) for k in range(5))
        if cardinal < math.radians(15): continue
        def point(t,offset,z):
            x=1400*math.cos(t); y=1030*math.sin(t)
            nx=math.cos(t)/1400; ny=math.sin(t)/1030
            length=math.hypot(nx,ny); nx/=length; ny/=length
            return (120+x+side*nx*offset,-1510+y+side*ny*offset,z)
        ha=10+2*math.sin(a*3); hb=10+2*math.sin(b*3)
        widtha=360+35*math.sin(a*5); widthb=360+35*math.sin(b*5)
        quad('PathCut',point(a,193,2.7),point(a,211,ha),point(b,211,hb),point(b,193,2.7))
        quad('TurfTerrace' if i//4%2==0 else 'TurfTerraceLight',point(a,211,ha),point(a,292,ha+2),point(b,292,hb+2),point(b,211,hb))
        quad('TurfTerrace' if i//4%2==0 else 'TurfTerraceLight',point(a,292,ha+2),point(a,widtha,.5),point(b,widthb,.5),point(b,292,hb+2))

# Narrow shoulders on straight connectors. Short tapered ends leave junctions open.
segments=[((-520,-1510),(-1010,-1510),165),((760,-1510),(1190,-1510),165),
          ((320,-180),(1050,-180),180),((1180,160),(2040,160),210),
          ((1200,-3100),(2040,-3100),210),((-2090,-3000),(-1420,-3000),200)]
for start,end,half_width in segments:
    dx=end[0]-start[0]; dy=end[1]-start[1]; length=math.hypot(dx,dy)
    nx=-dy/length; ny=dx/length
    for side in [-1,1]:
        for j in range(4):
            def sp(t,offset,z):
                return (start[0]+dx*t+nx*offset*side,start[1]+dy*t+ny*offset*side,z)
            a=j/4; b=(j+1)/4
            za=2.8+8*min(1,a*4,(1-a)*4); zb=2.8+8*min(1,b*4,(1-b)*4)
            quad('PathCut',sp(a,half_width+3,2.7),sp(a,half_width+18,za),sp(b,half_width+18,zb),sp(b,half_width+3,2.7))
            quad('TurfTerrace',sp(a,half_width+18,za),sp(a,half_width+115,.5),sp(b,half_width+115,.5),sp(b,half_width+18,zb))

# Shallow polygonal shores, split at bridge entrances and stream connection.
for i in range(32):
    a=i*360/32; b=(i+1)*360/32; mid=math.radians((a+b)/2)
    if abs(math.sin(mid)*875)<310: continue
    if math.sin(mid)<-.88: continue
    def shore(rx,ry,t,z): return ell(rx,ry,t,z,-2580,-1410)
    quad('ShoreSoil',shore(525,850,a,8.3),shore(608,943,a,16),shore(608,943,b,16),shore(525,850,b,8.3))
    quad('TurfTerrace',shore(608,943,a,16),shore(700,1035,a,.5),shore(700,1035,b,.5),shore(608,943,b,16))

# Plateau's lower apron. Keep both ramp mouths and west Cage route unobstructed.
for x,direction in [(1760,-1),(3060,1)]:
    for y0,y1 in [(-2175,-1760),(-1250,-725)]:
        for j in range(3):
            a=y0+(y1-y0)*j/3; b=y0+(y1-y0)*(j+1)/3
            quad('PlateauApron',(x,a,44),(x+direction*70,a,14),(x+direction*70,b,18),(x,b,44))
            quad('TurfTerrace',(x+direction*70,a,14),(x+direction*165,a,.5),(x+direction*165,b,.5),(x+direction*70,b,18))
for y,direction in [(-2175,-1),(-725,1)]:
    for x0,x1 in [(1760,2080),(2740,3060)]:
        quad('PlateauApron',(x0,y,38),(x0,y+direction*95,.5),(x1,y+direction*95,.5),(x1,y,38))

material_paths={
 'CageBase':'/Game/Stage01/Quality/Materials/MI_S01_Quality_Blue',
 'CagePanels':'/Game/Stage01/Quality/Materials/MI_S01_Quality_Ivory',
 'CagePanelsCool':'/Game/Stage01/GroundStructure/Materials/MI_S01_FacilityFloor',
 'WarningRing':'/Game/Stage01/Quality/Materials/MI_S01_Quality_Yellow',
 'CyanGuides':'/Game/Stage01/Quality/Materials/MI_S01_Quality_Cyan',
 'PathCut':'/Game/Stage01/Ground/Materials/MI_S01_Ground_DampSoil',
 'TurfTerrace':'/Game/Stage01/Ground/Materials/MI_S01_Ground_Turf',
 'TurfTerraceLight':'/Game/Stage01/Ground/Materials/MI_S01_Ground_TurfLight',
 'ShoreSoil':'/Game/Stage01/Ground/Materials/MI_S01_Ground_DampSoil',
 'PlateauApron':'/Game/Stage01/Quality/Materials/MI_S01_Nature_Rock',
}

def run():
    assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError('PIE is running; editor-only operation refused')
    # Only this pass's exact ten mesh paths may be created or rebuilt.
    materials={k:unreal.load_asset(p) for k,p in material_paths.items()}
    if not all(materials.values()): raise RuntimeError('Missing material')
    result=[]
    for name,faces in GROUPS.items():
        mesh=unreal.DynamicMesh()
        verts=[]; indices=[]
        for face in faces:
            index=len(verts)
            verts.extend(unreal.Vector(*v) for v in face)
            indices.append(unreal.IntVector(index,index+2,index+1))
        buffers=unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=indices,
            uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts])
        mesh.append_buffers_to_mesh(buffers)
        mesh.set_per_face_normals()
        options=unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property('enable_collision',False)
        options.set_editor_property('enable_nanite',False)
        path=ROOT+'/Meshes/SM_S01_'+name
        if assets.does_asset_exist(path):
            asset=unreal.load_asset(path)
            _,outcome=unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(
                mesh,asset,unreal.GeometryScriptCopyMeshToAssetOptions(),unreal.GeometryScriptMeshWriteLOD())
        else:
            asset,outcome=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh,path,options)
        if not asset: raise RuntimeError('Failed '+path+': '+str(outcome))
        asset.set_material(0,materials[name])
        if not assets.save_loaded_asset(asset): raise RuntimeError('Save failed '+path)
        result.append({'name':name,'path':path,'triangles':len(faces)})
    Path(__file__).with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    unreal.log('GROUND_STRUCTURE_CREATED '+str(len(result)))

run()
