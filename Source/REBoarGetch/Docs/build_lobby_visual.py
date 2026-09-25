"""Additive, editor-only Lobby art pass. No gameplay edits or PIE."""
import unreal, math, json, traceback
from pathlib import Path
AS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ML=unreal.MaterialEditingLibrary
AT=unreal.AssetToolsHelpers.get_asset_tools()
REPORT={'assets':[],'actors':[],'material_changes':[]}
BASE='/Game/Lobby'

def save(a):
    if not AS.save_loaded_asset(a): raise RuntimeError('Save failed '+a.get_path_name())
    REPORT['assets'].append(a.get_path_name())

def create(path,cls,factory):
    if AS.does_asset_exist(path): raise RuntimeError('Existing asset: '+path)
    return AT.create_asset(path.rsplit('/',1)[1],path.rsplit('/',1)[0],cls,factory)

def node(m,cls,**kw):
    n=ML.create_material_expression(m,cls)
    for k,v in kw.items(): n.set_editor_property(k,v)
    return n

def link(a,b,input='',output=''):
    if not ML.connect_material_expressions(a,output,b,input): raise RuntimeError('Material connection failed '+input)

def output(n,p,o=''):
    if not ML.connect_material_property(n,o,p): raise RuntimeError('Output failed')

def scalar(m,name,value): return node(m,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
def constant(m,value): return node(m,unreal.MaterialExpressionConstant,r=value)

def materials():
    mats={n:unreal.load_asset(BASE+'/Materials/MI_Lobby_'+n) for n in ['Ivory','Ice','Navy','Cyan','Yellow']}
    if not all(mats.values()): raise RuntimeError('Missing Lobby palette')
    tex=[]
    for name,srgb,comp in [('GrassBaseColor',True,unreal.TextureCompressionSettings.TC_DEFAULT),('GrassNormalMap',False,unreal.TextureCompressionSettings.TC_NORMALMAP),('GrassRoughnessMap',False,unreal.TextureCompressionSettings.TC_GRAYSCALE)]:
        t=unreal.load_asset('/Game/Stage01/Ground/'+name)
        if not t: raise RuntimeError('Missing supplied texture '+name)
        t.set_editor_property('srgb',srgb); t.set_editor_property('compression_settings',comp);save(t);tex.append(t)
    path='/Game/Stage01/Ground/Materials/M_Grass_LowPoly'
    if AS.does_asset_exist(path) and AS.get_metadata_tag(unreal.load_asset(path),'LobbyGrassComplete')=='1':
        m=unreal.load_asset(path)
    else:
        m=unreal.load_asset(path) if AS.does_asset_exist(path) else create(path,unreal.Material,unreal.MaterialFactoryNew())
        ML.delete_all_material_expressions(m)
        uv=node(m,unreal.MaterialExpressionTextureCoordinate)
        scale=scalar(m,'Tiling',0.5)
        mul=node(m,unreal.MaterialExpressionMultiply);link(uv,mul,'A');link(scale,mul,'B')
        samples=[]
        for i,t in enumerate(tex):
            s=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name=['GrassColor','GrassNormal','GrassRoughness'][i],texture=t,
                sampler_type=[unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE][i])
            link(mul,s);samples.append(s)
        output(samples[0],unreal.MaterialProperty.MP_BASE_COLOR,'RGB')
        flat=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0,0,1,1))
        lerp=node(m,unreal.MaterialExpressionLinearInterpolate);link(flat,lerp,'A');link(samples[1],lerp,'B','RGB');link(scalar(m,'NormalStrength',0.18),lerp,'Alpha')
        output(lerp,unreal.MaterialProperty.MP_NORMAL)
        rough=node(m,unreal.MaterialExpressionLinearInterpolate);link(scalar(m,'RoughnessMin',0.78),rough,'A');link(scalar(m,'RoughnessMax',0.95),rough,'B');link(samples[2],rough,'Alpha','R')
        output(rough,unreal.MaterialProperty.MP_ROUGHNESS)
        ML.layout_material_expressions(m);ML.recompile_material(m);AS.set_metadata_tag(m,'LobbyGrassComplete','1');save(m)
    path=BASE+'/Materials/MI_Grass_Lobby'
    mi=unreal.load_asset(path) if AS.does_asset_exist(path) else create(path,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    ML.set_material_instance_parent(mi,m)
    for k,v in [('Tiling',0.5),('NormalStrength',0.18),('RoughnessMin',0.78),('RoughnessMax',0.95)]: ML.set_material_instance_scalar_parameter_value(mi,k,v)
    save(mi);mats['Grass']=mi
    path=BASE+'/Materials/M_Lobby_ResearchHologram'
    if AS.does_asset_exist(path): h=unreal.load_asset(path)
    else:
        h=create(path,unreal.Material,unreal.MaterialFactoryNew())
        h.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
        h.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        h.set_editor_property('two_sided',True)
        color=node(h,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0.015,0.65,1.0,1))
        output(color,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        f=node(h,unreal.MaterialExpressionFresnel,exponent=2.0,base_reflect_fraction=0.08)
        op=node(h,unreal.MaterialExpressionMultiply);link(f,op,'A');link(scalar(h,'Opacity',0.6),op,'B')
        output(op,unreal.MaterialProperty.MP_OPACITY);ML.layout_material_expressions(h);ML.recompile_material(h);save(h)
    mats['Holo']=h
    return mats

def spawn(label,mesh,loc,scale=(1,1,1),mat=None,rot=(0,0,0),folder='Structure'):
    label='LobbyArt_'+label
    if label in EXIST: raise RuntimeError('Existing actor '+label)
    a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*loc),unreal.Rotator(*rot))
    a.set_actor_label(label);a.set_folder_path('Lobby_Visual/'+folder);a.set_actor_scale3d(unreal.Vector(*scale))
    c=a.static_mesh_component;c.set_static_mesh(mesh);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property('can_ever_affect_navigation',False)
    if mat:
        for i in range(max(1,c.get_num_materials())): c.set_material(i,mat)
    REPORT['actors'].append(a.get_path_name());EXIST.add(label)
    return a

GROUPS={}
def tri(g,a,b,c): GROUPS.setdefault(g,[]).append((a,b,c))
def quad(g,a,b,c,d): tri(g,a,b,c);tri(g,a,c,d)
def ellipse(x,y,rx,ry,a,z): return (x+rx*math.cos(a),y+ry*math.sin(a),z)
def ring(g,x,y,ri,ro,z,ratio=1,n=32,gaps=0):
    for i in range(n):
        a=math.tau*i/n+gaps;b=math.tau*(i+1)/n-gaps
        quad(g,ellipse(x,y,ri,ri*ratio,a,z),ellipse(x,y,ro,ro*ratio,a,z),ellipse(x,y,ro,ro*ratio,b,z),ellipse(x,y,ri,ri*ratio,b,z))
def island(x,y,rx,ry):
    for i in range(10):
        a=math.tau*i/10;b=math.tau*(i+1)/10
        ia=ellipse(x,y,rx*.83,ry*.83,a,16);ib=ellipse(x,y,rx*.83,ry*.83,b,16)
        oa=ellipse(x,y,rx,ry,a,1);ob=ellipse(x,y,rx,ry,b,1)
        tri('Grass',(x,y,16),ia,ib);quad('Grass',ia,oa,ob,ib)
        quad('Navy',oa,ellipse(x,y,rx+9,ry+9,a,1),ellipse(x,y,rx+9,ry+9,b,1),ob)

def build():
    global EXIST
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if world.get_game_world(): raise RuntimeError('PIE active')
    if '/Game/Level/L_Lobby.' not in world.get_editor_world().get_path_name(): raise RuntimeError('Wrong level')
    current=EA.get_all_level_actors();EXIST={a.get_actor_label() for a in current}
    mats=materials()
    cube=unreal.load_asset('/Engine/BasicShapes/Cube');cyl=unreal.load_asset('/Engine/BasicShapes/Cylinder');sphere=unreal.load_asset('/Engine/BasicShapes/Sphere')
    def box(n,p,s,m='Ivory',rot=(0,0,0),folder='Structure'): return spawn(n,cube,p,tuple(v/100 for v in s),mats[m],rot,folder)
    def cylinder(n,p,s,m='Ivory',rot=(0,0,0)): return spawn(n,cyl,p,tuple(v/100 for v in s),mats[m],rot)
    # Add floor divisions over the existing colliding plaza, preserving step heights.
    for i in range(12):
        a=math.tau*i/12+.012;b=math.tau*(i+1)/12-.012
        quad('Ice' if i%3==0 else 'Ivory',ellipse(0,0,190,190,a,31.6),ellipse(0,0,566,566,a,31.6),ellipse(0,0,566,566,b,31.6),ellipse(0,0,190,190,b,31.6))
    ring('Navy',0,0,567,579,31.7)
    ring('Yellow',0,0,604,623,22.6,n=16,gaps=.065)
    ring('Cyan',0,0,161,169,39,n=16,gaps=.035)
    # Transparent research globe; no opaque obstruction along the stage sightline.
    spawn('ResearchGlobe',sphere,(0,0,260),(2.2,2.2,2.2),mats['Holo'],folder='Research')
    ring('Cyan',0,0,135,140,165,n=32)
    ring('Holo',0,0,126,137,285,n=32)
    ring('Cyan',0,0,77,81,372,n=24)
    for i in range(3):
        a=math.tau*i/3
        cylinder('Projector'+str(i),(125*math.cos(a),125*math.sin(a),50),(22,22,23),'Navy')
        cylinder('ProjectorLens'+str(i),(125*math.cos(a),125*math.sin(a),63),(18,18,3),'Cyan')
    # Preserve original gate actors; reassign only their visual material overrides.
    for a in current:
        if a.get_actor_label() in ['StageGate_Left','StageGate_Right','StageGate_Lintel']:
            c=a.static_mesh_component;c.set_material(0,mats['Ivory']);REPORT['material_changes'].append(a.get_actor_label())
    for side in [-1,1]:
        y=side*430
        box('GateFoot'+str(side),(1310,y,30),(210,200,60),'Navy')
        box('GateWing'+str(side),(1400,side*625,220),(180,260,440),'Ivory')
        box('GateWindow'+str(side),(1307,side*625,265),(8,180,180),'Navy')
        box('GateWindowLight'+str(side),(1301,side*625,265),(5,150,120),'Cyan')
        box('GateRoof'+str(side),(1400,side*625,465),(245,300,50),'Navy')
        box('GateThresholdMark'+str(side),(1220,side*190,7),(150,12,4),'Yellow')
        box('GateCrown'+str(side),(1300,side*205,860),(140,360,45),'Ivory',rot=(0,0,side*12))
    box('GateCrest',(1218,0,835),(16,140,65),'Yellow')
    # Gadget area: thick display backing and simple large equipment silhouettes.
    box('GadgetBorder',(-200,-1290,12),(910,20,12),'Navy')
    for x in [-610,210]:
        box('GadgetSupport'+str(x),(x,-1260,195),(35,45,370),'Ivory')
    box('GadgetCanopy',(-200,-1220,390),(900,180,40),'Navy')
    box('GadgetCanopyLight',(-200,-1128,387),(800,5,12),'Cyan')
    for i,x in enumerate([-440,-210,20]):
        box('DisplayStem'+str(i),(x,-900,113),(18,18,55),'Navy',folder='Gadget')
        box('DisplayBody'+str(i),(x,-900,157),(76,55,45),'Ivory',rot=(0,i*20,0),folder='Gadget')
        cylinder('DisplayLens'+str(i),(x,-900,190),(45,45,14),'Cyan')
        box('DisplayStripe'+str(i),(x-40,-900,155),(7,56,32),'Yellow',folder='Gadget')
    # Practice platform tops follow existing surfaces; no collision changes.
    for label,x,z,w in [('01',-350,80,200),('02',50,140,200),('03',480,200,260),('Landing',940,160,400)]:
        for side in [-1,1]: box('PracticeEdge'+label+str(side),(x,930+side*155,z+.8),(w-20,12,1.5),'Yellow',folder='Practice')
        box('PracticeStripe'+label,(x,930,z+1),(w-25,20,2),'Cyan',folder='Practice')
    # Main approach stays open, with broad flat panels and edge guides.
    for x in [-1400,-1180,-960,820,1040]:
        box('WalkPanel'+str(x),(x,0,3.6),(190,310,1),'Ice',folder='Floor')
        for side in [-1,1]:box('WalkEdge'+str(x)+str(side),(x,side*170,4),(160,7,2),'Cyan',folder='Floor')
    for side in [-1,1]:
        box('WallCapY'+str(side),(0,side*1480,137),(3530,65,14),'Ivory')
        box('WallCapX'+str(side),(side*1780,0,137),(65,3000,14),'Ivory')
    # Bounded garden islands, never over the main paths or practice ramp.
    gardens=[(-1350,-870,300,370),(-1340,900,290,340),(880,-1080,410,320),(1580,930,140,280)]
    for g in gardens:island(*g)
    # Water basin: faceted rim and shallow visible water over the existing floor.
    ring('Ivory',820,-1100,155,182,22,ratio=.8,n=12)
    for i in range(12):tri('Cyan',(820,-1100,17),ellipse(820,-1100,153,122,i*math.tau/12,17),ellipse(820,-1100,153,122,(i+1)*math.tau/12,17))
    ring('Holo',820,-1100,65,71,18,ratio=.8,n=24)
    # Group meshes keep the asset/actor count small and include UVs for UE builds.
    for name,faces in GROUPS.items():
        path=BASE+'/Meshes/SM_LobbyArt_'+name
        if AS.does_asset_exist(path):raise RuntimeError('Existing mesh '+path)
        verts=[];indices=[]
        for face in faces:
            i=len(verts);verts.extend(unreal.Vector(*v) for v in face);indices.append(unreal.IntVector(i,i+2,i+1))
        dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=indices,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts]));dm.set_per_face_normals()
        opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opts.set_editor_property('enable_collision',False);opts.set_editor_property('enable_nanite',False)
        mesh,result=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opts)
        if not mesh:raise RuntimeError(str(result))
        mesh.set_material(0,mats[name]);save(mesh);spawn('Surface_'+name,mesh,(0,0,0),folder='Floor')
    # Small, grouped low-poly planting, reusing the imported environment meshes.
    nature={k:unreal.load_asset('/Game/InportAssets/LPRiverForest/Meshes/'+p) for k,p in {'Tree':'Trees/SM_LPTree01','Bush':'Plants/SM_LPBush01','Rock':'Rocks/SM_LPRock01','Grass':'Plants/SM_LPGrass01'}.items()}
    for k,v in nature.items():
        if not v:raise RuntimeError('Missing nature mesh '+k)
    for i,(x,y,rx,ry) in enumerate(gardens):
        spawn('Tree'+str(i),nature['Tree'],(x-65,y+90,14),(.65,.65,.65),rot=(0,i*63,0),folder='Garden')
        for j,(dx,dy) in enumerate([(-100,-100),(90,130)]):
            spawn('Bush'+str(i)+'_'+str(j),nature['Bush'],(x+dx,y+dy,14),(.65,.65,.65),rot=(0,j*77+i*20,0),folder='Garden')
            spawn('Rock'+str(i)+'_'+str(j),nature['Rock'],(x+dx+60,y+dy-25,13),(.4,.4,.4),rot=(0,j*45+i*60,0),folder='Garden')
        for j in range(3):spawn('Grass'+str(i)+'_'+str(j),nature['Grass'],(x-rx*.55+j*38,y-ry*.5,12),(.6,.6,.6),rot=(0,j*93,0),folder='Garden')
    if not unreal.EditorLevelLibrary.save_current_level():raise RuntimeError('Level save failed')
    REPORT['saved_level']=world.get_editor_world().get_path_name()

try:build()
except Exception:
    REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
finally:
    Path(__file__).with_suffix('.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
