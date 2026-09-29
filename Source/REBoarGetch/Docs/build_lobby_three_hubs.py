import unreal,math,json,traceback
from pathlib import Path
root=Path(__file__).parent
exec((root/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
REPORT={'assets':[],'actors':[]}
def run():
    global EXIST,GROUPS
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if world.get_game_world() or '/Game/Level/L_Lobby.' not in world.get_editor_world().get_path_name():raise RuntimeError('Wrong world')
    EXIST={a.get_actor_label() for a in EA.get_all_level_actors()};GROUPS={}
    mats={n:unreal.load_asset('/Game/Lobby/Materials/MI_Lobby_'+n) for n in ['Ivory','Ice','Navy','Cyan','Yellow']}
    mats['Glow']=unreal.load_asset('/Game/Lobby/Materials/M_Lobby_CyanEdge')
    mats['Panel']=unreal.load_asset('/Game/Lobby/Materials/M_StageSelect_HoloPanel')
    textmat=unreal.load_asset('/Game/Lobby/Materials/M_StageSelect_HoloText')
    assert all(mats.values()) and textmat
    cube=unreal.load_asset('/Engine/BasicShapes/Cube');cyl=unreal.load_asset('/Engine/BasicShapes/Cylinder')
    def box(n,p,s,m='Ivory',rot=(0,0,0)):
        a=next((a for a in EA.get_all_level_actors() if a.get_actor_label()=='LobbyArt_Hub3_'+n),None)
        if a is None:a=spawn('Hub3_'+n,cube,p,tuple(v/100 for v in s),mats[m],rot,folder='ThreeHubs')
        c=a.static_mesh_component;c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        return a
    def text(n,words,p,size,yaw):
        label='LobbyArt_Hub3_'+n
        a=next((a for a in EA.get_all_level_actors() if a.get_actor_label()==label),None)
        if a is None:a=EA.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(*p),unreal.Rotator(pitch=0,yaw=yaw,roll=0))
        a.set_actor_label(label);a.set_folder_path('Lobby_Visual/ThreeHubs')
        t=a.get_components_by_class(unreal.TextRenderComponent)[0];t.set_font(unreal.load_asset('/Engine/EngineFonts/RobotoDistanceField'));t.set_text(words);t.set_world_size(size);t.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER);t.set_text_material(textmat);t.set_text_render_color(unreal.Color(255,255,255,255));REPORT['actors'].append(a.get_path_name())
    for side,title in [(-1,'GADGET LOADOUT'),(1,'ENCYCLOPEDIA')]:
        prefix='Gadget' if side<0 else 'Archive';y=side*720
        # Smaller complementary portals leave the main Stage gate dominant.
        def pt(t,r,depth=0):return(r*math.cos(t),y+depth,260+r*math.sin(t))
        for i in range(10):
            a=math.pi*i/10;b=math.pi*(i+1)/10
            for d in [-45,45]:quad('Ivory',pt(a,210,d),pt(a,280,d),pt(b,280,d),pt(b,210,d))
            quad('Ivory',pt(a,280,-45),pt(a,280,45),pt(b,280,45),pt(b,280,-45))
            quad('Navy',pt(a,210,-45),pt(a,210,45),pt(b,210,45),pt(b,210,-45))
            quad('Glow',pt(a,215,-side*50),pt(a,232,-side*50),pt(b,232,-side*50),pt(b,215,-side*50))
        for x in [-248,248]:
            box(prefix+'Tower'+str(x),(x,y,135),(100,130,260))
            box(prefix+'Base'+str(x),(x,y,27),(130,150,50),'Navy')
            box(prefix+'Yellow'+str(x),(x,y,260),(112,142,24),'Yellow')
            box(prefix+'Inset'+str(x),(x,y-side*68,146),(48,8,162),'Navy')
            box(prefix+'Lamp'+str(x),(x,y-side*75,146),(16,7,144),'Glow')
        box(prefix+'Sign',(0,y,589),(640,4,86),'Panel')
        for z in [544,634]:box(prefix+'Frame'+str(z),(0,y-side*6,z),(654,6,6),'Glow')
        for x in [-325,325]:box(prefix+'FrameX'+str(x),(x,y-side*6,589),(6,6,96),'Glow')
        text(prefix+'Title',title,(0,y-side*12,569),51,90 if side<0 else -90)
        # Thin graphics overlay the existing walkable floor; they do not create collision.
        for x in [-170,170]:box(prefix+'Path'+str(x),(x,side*624,7),(8,180,2),'Cyan')
        for x in [-160,0,160]:box(prefix+'Threshold'+str(x),(x,y,8),(100,18,2),'Yellow')
    # Left workshop: retain existing bench, targets and display pods.
    box('WorkbenchBack',(-200,-1350,155),(600,34,240),'Navy')
    box('BlueprintScreen',(-200,-1328,181),(390,8,160),'Panel')
    for x in [-360,-40]:box('ScreenEdge'+str(x),(x,-1320,181),(7,6,140),'Glow')
    for x,z,w in [(-200,230,270),(-240,187,190),(-200,140,270)]:box('BlueprintLine'+str(z),(x,-1317,z),(w,4,5),'Cyan')
    # Large schematic silhouette, not a copied icon or a gameplay gadget.
    box('ToolDiagram',(-110,-1312,182),(70,6,38),'Yellow',rot=(0,0,25))
    text('WorkshopSubtitle','WORKSHOP / TEST',(-200,-1310,330),32,90)
    for x in [250,410]:
        for dx in [-62,62]:box('TestLane'+str(x)+str(dx),(x+dx,-1090,7),(6,200,2),'Yellow')
    # Research stations line the outer edge, keeping the original jumping lane open.
    for i,x in enumerate([-690,430]):
        box('ArchiveBase'+str(i),(x,1260,35),(270,160,70),'Navy')
        box('ArchiveDesk'+str(i),(x,1260,104),(260,150,70))
        box('ArchiveTop'+str(i),(x,1260,148),(288,180,18),'Ice')
        box('ArchiveScreen'+str(i),(x,1315,235),(260,5,160),'Panel')
        for xx in [x-134,x+134]:box('ArchiveEdge'+str(i)+str(xx),(xx,1308,235),(6,6,170),'Glow')
        for z in [150,320]:box('ArchiveFrame'+str(i)+str(z),(x,1308,z),(275,6,6),'Glow')
        text('ArchiveCaption'+str(i),'RESEARCH' if i==0 else 'FIELD ARCHIVE',(x,1300,345),31,-90)
        # An open-book silhouette, composed of two large luminous pages.
        for s in [-1,1]:box('ArchivePage'+str(i)+str(s),(x+s*47,1245,173),(84,80,7),'Cyan',rot=(0,0,s*10))
        box('ArchiveSpine'+str(i),(x,1245,174),(7,90,7),'Yellow')
    # Quiet directional guides from the portal toward the archive stations.
    for x in [-760,-540,320,540]:box('ArchiveGuide'+str(x),(x,1160,7),(140,6,2),'Cyan')
    # Strengthen the central departure axis without moving its existing entrance.
    for side in [-1,1]:
        box('MainGuide'+str(side),(810,side*150,7),(330,10,2),'Cyan')
        for x in [700,820,940]:box('MainGuideAccent'+str(side)+str(x),(x,side*170,7),(70,8,2),'Yellow')
    # Merge faceted arches into a few material-group meshes.
    for name,faces in GROUPS.items():
        path='/Game/Lobby/Meshes/SM_Hub3_Portal_'+name
        if AS.does_asset_exist(path):raise RuntimeError('Existing '+path)
        vs=[];ids=[]
        for face in faces:
            i=len(vs);vs.extend(unreal.Vector(*v) for v in face);vs.extend(unreal.Vector(*v) for v in reversed(face));ids.extend([unreal.IntVector(i,i+1,i+2),unreal.IntVector(i+3,i+4,i+5)])
        dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=vs,triangles=ids,uv0=[unreal.Vector2D(v.x/100,v.z/100) for v in vs]));dm.set_per_face_normals()
        opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opts.set_editor_property('enable_collision',False);opts.set_editor_property('enable_nanite',False)
        mesh,result=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opts);assert mesh;mesh.set_material(0,mats[name]);save(mesh)
        a=spawn('Hub3_Arch_'+name,mesh,(0,0,0),folder='ThreeHubs');a.static_mesh_component.set_editor_property('use_default_collision',False);a.static_mesh_component.set_collision_profile_name('NoCollision')
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level();REPORT['saved']=True
try:run()
except Exception:REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
finally:(root/'build_lobby_three_hubs.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')



