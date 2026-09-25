import unreal,math,json,traceback
from pathlib import Path
root=Path(__file__).parent
exec((root/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
REPORT={'assets':[],'actors':[],'modified':[],'hidden_visuals':[]}
def run():
    global EXIST,GROUPS
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
    if '/Game/Level/L_Lobby.' not in unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name():raise RuntimeError('Wrong map')
    actors=EA.get_all_level_actors();EXIST={a.get_actor_label() for a in actors};GROUPS={}
    mats={n:unreal.load_asset('/Game/Lobby/Materials/MI_Lobby_'+n) for n in ['Ivory','Navy','Cyan','Yellow']};mats['Glow']=unreal.load_asset('/Game/Lobby/Materials/M_Lobby_CyanEdge')
    # Dedicated translucent sign assets, with real font glyphs rather than a baked sign image.
    textmat=create('/Game/Lobby/Materials/M_StageSelect_HoloText',unreal.Material,unreal.MaterialFactoryNew())
    textmat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);textmat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);textmat.set_editor_property('two_sided',True)
    font=unreal.load_asset('/Engine/EngineFonts/RobotoDistanceField')
    glyph=node(textmat,unreal.MaterialExpressionFontSampleParameter,parameter_name='Font',font=font,font_texture_page=0)
    output(glyph,unreal.MaterialProperty.MP_OPACITY,'A')
    color=node(textmat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.2,2.2,3.3,1));output(color,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    ML.layout_material_expressions(textmat);ML.recompile_material(textmat);save(textmat)
    panel=create('/Game/Lobby/Materials/M_StageSelect_HoloPanel',unreal.Material,unreal.MaterialFactoryNew())
    panel.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);panel.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);panel.set_editor_property('two_sided',True)
    output(node(panel,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.01,.15,.45,1)),unreal.MaterialProperty.MP_EMISSIVE_COLOR);output(constant(panel,.42),unreal.MaterialProperty.MP_OPACITY)
    ML.recompile_material(panel);save(panel);mats['Panel']=panel
    cube=unreal.load_asset('/Engine/BasicShapes/Cube');cyl=unreal.load_asset('/Engine/BasicShapes/Cylinder')
    def box(n,p,s,m='Ivory'):return spawn('Energy_'+n,cube,p,tuple(v/100 for v in s),mats[m],folder='GateEnergy')
    def cylinder(n,p,s,m='Ivory'):return spawn('Energy_'+n,cyl,p,tuple(v/100 for v in s),mats[m],folder='GateEnergy')
    # Retain old actors and their collision; suppress only superseded decorative rendering.
    for a in actors:
        n=a.get_actor_label()
        hide=n in ['StageGate_Lintel','StageGate_Beacon'] or n.startswith(('Port_GateArch','LobbyArt_GateCrown','LobbyArt_GateCrest','LobbyArt_Ref_Gate'))
        if hide and isinstance(a,unreal.StaticMeshActor):
            a.static_mesh_component.set_visibility(False);a.static_mesh_component.set_hidden_in_game(True);REPORT['hidden_visuals'].append(n)
    # A deep faceted half arch joins the two large support towers.
    def arcpoint(x,ry,rz,t):return(x,ry*math.cos(t),380+rz*math.sin(t))
    for i in range(12):
        a=math.pi*i/12;b=math.pi*(i+1)/12
        for x in [1100,1310]:quad('Ivory',arcpoint(x,240,360,a),arcpoint(x,425,490,a),arcpoint(x,425,490,b),arcpoint(x,240,360,b))
        quad('Ivory',arcpoint(1100,425,490,a),arcpoint(1310,425,490,a),arcpoint(1310,425,490,b),arcpoint(1100,425,490,b))
        quad('Navy',arcpoint(1094,244,366,a),arcpoint(1094,302,424,a),arcpoint(1094,302,424,b),arcpoint(1094,244,366,b))
        quad('Navy',arcpoint(1100,240,360,a),arcpoint(1100,240,360,b),arcpoint(1310,240,360,b),arcpoint(1310,240,360,a))
        quad('Glow',arcpoint(1087,252,374,a),arcpoint(1087,272,394,a),arcpoint(1087,272,394,b),arcpoint(1087,252,374,b))
    for side in [-1,1]:
        y=side*340
        box('TowerBody'+str(side),(1210,y,235),(230,200,440))
        box('TowerBase'+str(side),(1210,y,35),(265,230,70),'Navy')
        box('TowerFoot'+str(side),(1200,y,77),(240,222,25),'Yellow')
        box('TowerInset'+str(side),(1090,y,255),(12,98,275),'Navy')
        box('TowerLight'+str(side),(1080,y,260),(8,23,222),'Glow')
        box('TowerCap'+str(side),(1210,y,467),(250,230,40),'Navy')
        box('TowerAccent'+str(side),(1090,y,439),(26,211,30),'Yellow')
        box('GateWallLink'+str(side),(1475,side*750,150),(560,85,240))
        box('GateWallCap'+str(side),(1475,side*750,281),(580,102,23),'Navy')
        box('GateWallPanel'+str(side),(1475,side*701,158),(460,10,140),'Cyan')
        box('GateWallLight'+str(side),(1475,side*694,70),(420,6,7),'Glow')
    # Wide floating sign with a readable editable text component.
    box('SignPanel',(1050,0,902),(4,650,110),'Panel')
    for z in [846,958]:box('SignFrameZ'+str(z),(1043,0,z),(8,660,7),'Glow')
    for y in [-330,330]:box('SignFrameY'+str(y),(1043,y,902),(8,7,116),'Glow')
    sign=next(a for a in actors if a.get_actor_label()=='Stage1Sign')
    sign.set_actor_location(unreal.Vector(1034,0,902),False,False)
    t=sign.get_components_by_class(unreal.TextRenderComponent)[0];t.set_text('STAGE SELECT');t.set_world_size(65);t.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER);t.set_text_material(textmat);t.set_text_render_color(unreal.Color(255,255,255,255));REPORT['modified'].append('Stage1Sign')
    # Broad white body sleeves sit inside each existing base footprint.
    for a in actors:
        n=a.get_actor_label()
        if n.startswith('Port_ColumnCap_'):
            a.set_actor_scale3d(unreal.Vector(1.5,1.5,.16));a.static_mesh_component.set_material(0,mats['Ivory']);REPORT['modified'].append(n)
        if n.startswith('Port_Column_'):
            p=a.get_actor_location();key=n.removeprefix('Port_Column_')
            cylinder('Body_'+key,(p.x,p.y,245),(123,123,370))
            cylinder('Base_'+key,(p.x,p.y,35),(200,200,70),'Navy')
            cylinder('FootCollar_'+key,(p.x,p.y,85),(150,150,28))
            cylinder('EnergyCollar_'+key,(p.x,p.y,431),(142,142,18),'Cyan')
            cylinder('YellowTop_'+key,(p.x,p.y,475),(123,123,5),'Yellow')
            for signum in [-1,1]:
                box('BodyRecess_'+key+str(signum),(p.x+signum*62,p.y,252),(7,31,250),'Navy')
                box('BodyLine_'+key+str(signum),(p.x+signum*67,p.y,252),(5,11,228),'Glow')
    # Ground guide preserves the existing approach elevation and open entrance.
    for x in [820,960,1100]:
        for side in [-1,1]:
            a=box('FloorChevron'+str(x)+str(side),(x,side*28,8),(12,75,2),'Glow');a.set_actor_rotation(unreal.Rotator(0,side*40,0),False)
    for name,faces in GROUPS.items():
        verts=[];inds=[]
        for face in faces:
            i=len(verts);verts.extend(unreal.Vector(*v) for v in face);inds.extend([unreal.IntVector(i,i+2,i+1),unreal.IntVector(i,i+1,i+2)])
        dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=inds,uv0=[unreal.Vector2D(v.y/100,v.z/100) for v in verts]));dm.set_per_face_normals()
        path='/Game/Lobby/Meshes/SM_GateEnergy_'+name
        if AS.does_asset_exist(path):raise RuntimeError('Existing '+path)
        options=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();options.set_editor_property('enable_collision',False);options.set_editor_property('enable_nanite',False)
        mesh,result=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,options);mesh.set_material(0,mats[name]);save(mesh);spawn('Energy_Arch_'+name,mesh,(0,0,0),folder='GateEnergy')
    for a in EA.get_all_level_actors():
        if not a.get_actor_label().startswith('LobbyArt_Energy_'):continue
        c=a.static_mesh_component;c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('can_ever_affect_navigation',False)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():raise RuntimeError('Save failed')
    REPORT['saved']=True
try:run()
except Exception:REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
finally:(root/'build_gate_energy.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
