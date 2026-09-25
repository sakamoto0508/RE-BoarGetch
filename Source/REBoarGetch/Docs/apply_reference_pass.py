import unreal,math,json,traceback
from pathlib import Path
root=Path(__file__).parent
# Reuse the established geometry/material helpers without executing the previous pass.
exec((root/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
REPORT={'assets':[],'actors':[],'material_changes':[]}
ES=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
def newmat(path):return create(path,unreal.Material,unreal.MaterialFactoryNew())
def finish(m):ML.layout_material_expressions(m);ML.recompile_material(m);save(m);return m
def run():
    global EXIST,GROUPS
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():raise RuntimeError('Dirty map')
    ES.load_level('/Game/Level/Test')
    m=newmat('/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World')
    pos=node(m,unreal.MaterialExpressionWorldPosition)
    xy=node(m,unreal.MaterialExpressionComponentMask,r=True,g=True,b=False,a=False);link(pos,xy)
    uv=node(m,unreal.MaterialExpressionDivide);link(xy,uv,'A');link(scalar(m,'TileSizeCm',200),uv,'B')
    samples=[]
    for i,n in enumerate(['GrassBaseColor','GrassNormalMap','GrassRoughnessMap']):
        t=unreal.load_asset('/Game/Stage01/Ground/'+n)
        s=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name=n,texture=t,sampler_type=[unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE][i]);link(uv,s);samples.append(s)
    output(samples[0],unreal.MaterialProperty.MP_BASE_COLOR,'RGB')
    flat=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0,0,1,1))
    normal=node(m,unreal.MaterialExpressionLinearInterpolate);link(flat,normal,'A');link(samples[1],normal,'B','RGB');link(scalar(m,'NormalStrength',.15),normal,'Alpha');output(normal,unreal.MaterialProperty.MP_NORMAL)
    rough=node(m,unreal.MaterialExpressionLinearInterpolate);link(constant(m,.78),rough,'A');link(constant(m,.95),rough,'B');link(samples[2],rough,'Alpha','R');output(rough,unreal.MaterialProperty.MP_ROUGHNESS);finish(m)
    mi=create('/Game/Stage01/Ground/Materials/MI_Grass_Stage01',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());ML.set_material_instance_parent(mi,m);save(mi)
    changed=[]
    for a in EA.get_all_level_actors():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            mesh=c.get_editor_property('static_mesh')
            if not mesh or '/Plants/' in mesh.get_path_name():continue
            for i,old in enumerate(c.get_materials()):
                if old and any(t in old.get_name() for t in ['MI_Stage01_Grass','MI_S01_Ground_Turf']):c.set_material(i,mi);changed.append(a.get_actor_label())
    REPORT['stage_grass_actors']=changed
    if not ES.save_current_level():raise RuntimeError('Stage save failed')
    ES.load_level('/Game/Level/L_Lobby')
    EXIST={a.get_actor_label() for a in EA.get_all_level_actors()};GROUPS={}
    mats={n:unreal.load_asset('/Game/Lobby/Materials/MI_Lobby_'+n) for n in ['Ivory','Navy','Cyan','Yellow']}
    glow=newmat('/Game/Lobby/Materials/M_Lobby_CyanEdge')
    color=node(glow,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.015,2.2,3.8,1));output(color,unreal.MaterialProperty.MP_EMISSIVE_COLOR);output(constant(glow,.5),unreal.MaterialProperty.MP_ROUGHNESS);finish(glow);mats['Glow']=glow
    cube=unreal.load_asset('/Engine/BasicShapes/Cube')
    def box(n,p,s,mat='Ivory',yaw=0):return spawn('Ref_'+n,cube,p,tuple(v/100 for v in s),mats[mat],(0,yaw,0),'ReferencePolish')
    # Large glasslike inset panels within the current perimeter, with substantial frames.
    for side in [-1,1]:
        for j,x in enumerate([-1320,-660,0,660,1320]):
            y=side*1448
            box('WallPanel'+str(side)+'_'+str(j),(x,y,77),(555,12,72),'Cyan')
            box('WallGlow'+str(side)+'_'+str(j),(x,y-side*9,30),(510,7,5),'Glow')
            box('WallUpper'+str(side)+'_'+str(j),(x,y,126),(585,26,20))
        for j,y in enumerate([-1080,-540,0,540,1080]):
            if side==1 and abs(y)<800:continue
            x=side*1750
            box('EndPanel'+str(side)+'_'+str(j),(x,y,77),(12,440,72),'Cyan')
            box('EndGlow'+str(side)+'_'+str(j),(x-side*9,y,30),(7,400,5),'Glow')
    for x in [-1650,-990,-330,330,990,1650]:
        for side in [-1,1]:
            y=side*1450
            box('WallPillar'+str(x)+str(side),(x,y,106),(62,62,212),'Ivory')
            box('WallPillarInset'+str(x)+str(side),(x,y-side*34,104),(34,8,160),'Navy')
            box('WallPillarLine'+str(x)+str(side),(x,y-side*39,108),(7,4,135),'Glow')
            box('WallPillarCap'+str(x)+str(side),(x,y,218),(54,54,10),'Glow')
    # Existing four plaza columns acquire luminous strips on both visible faces.
    for x in [-650,650]:
        for y in [-610,610]:
            for sign in [-1,1]:box('ColumnLine'+str(x)+str(y)+str(sign),(x+sign*43,y,252),(5,10,240),'Glow')
    # Thick polygonal planter curbs, with inset cyan perimeter strips.
    for i,(x,y,rx,ry) in enumerate([(-1350,-870,300,370),(-1340,900,290,340),(880,-1080,410,320),(1580,930,140,280)]):
        for j in range(10):
            a=math.tau*j/10;b=math.tau*(j+1)/10
            pa=ellipse(x,y,rx+5,ry+5,a,22);pb=ellipse(x,y,rx+5,ry+5,b,22)
            qa=ellipse(x,y,rx+24,ry+24,a,22);qb=ellipse(x,y,rx+24,ry+24,b,22)
            quad('Ivory',pa,qa,qb,pb)
            quad('Ivory',qa,(qa[0],qa[1],1),(qb[0],qb[1],1),qb)
            quad('Glow',ellipse(x,y,rx+14,ry+14,a+.015,22.5),ellipse(x,y,rx+18,ry+18,a+.015,22.5),ellipse(x,y,rx+18,ry+18,b-.015,22.5),ellipse(x,y,rx+14,ry+14,b-.015,22.5))
    # Vertical oval portal in front of the existing gate; opening remains clear.
    for j in range(48):
        a=math.tau*j/48;b=math.tau*(j+1)/48
        def p(ry,rz,t,x=1170):return (x,ry*math.cos(t),400+rz*math.sin(t))
        quad('Glow',p(226,366,a),p(234,374,a),p(234,374,b),p(226,366,b))
        if j%4!=0:quad('Cyan',p(249,389,a+.01),p(261,401,a+.01),p(261,401,b-.01),p(249,389,b-.01))
    ring('Glow',1110,0,158,165,9,ratio=1.25,n=32)
    for side in [-1,1]:
        box('GateJamb'+str(side),(1190,side*293,263),(95,100,470),'Ivory')
        box('GateJambInset'+str(side),(1139,side*293,260),(8,65,250),'Navy')
        box('GateJambLight'+str(side),(1133,side*293,280),(5,12,170),'Glow')
        box('GateYellowBase'+str(side),(1190,side*293,43),(115,124,30),'Yellow')
        box('GateYellowCap'+str(side),(1190,side*293,506),(115,124,40),'Yellow')
    # Tilted terminal screens on existing display devices.
    for i,x in enumerate([-440,-210,20]):box('TerminalScreen'+str(i),(x-40,-900,183),(8,44,34),'Cyan')
    for name,faces in GROUPS.items():
        verts=[];indices=[]
        for f in faces:
            k=len(verts);verts.extend(unreal.Vector(*v) for v in f);indices.append(unreal.IntVector(k,k+2,k+1))
        dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=indices,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts]));dm.set_per_face_normals()
        opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opts.set_editor_property('enable_collision',False);opts.set_editor_property('enable_nanite',False)
        path='/Game/Lobby/Meshes/SM_LobbyRef_'+name
        if AS.does_asset_exist(path):raise RuntimeError('Already exists '+path)
        mesh,result=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opts);mesh.set_material(0,mats[name]);save(mesh);spawn('Ref_Surface_'+name,mesh,(0,0,0),folder='ReferencePolish')
    for a in EA.get_all_level_actors():
        if not a.get_actor_label().startswith('LobbyArt_Ref_'):continue
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('can_ever_affect_navigation',False)
    if not ES.save_current_level():raise RuntimeError('Lobby save failed')
    REPORT['saved']=True
try:run()
except Exception:REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
finally:(root/'apply_reference_pass.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
