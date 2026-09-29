import unreal,json,math,traceback
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);AS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);AT=unreal.AssetToolsHelpers.get_asset_tools()
root=Path(__file__).parent;report={'assets':[],'maps':[],'hidden':[]}
mats={n:unreal.load_asset('/Game/Lobby/Materials/MI_Lobby_'+n) for n in ['Ivory','Ice','Navy','Cyan','Yellow']}
mats['Glow']=unreal.load_asset('/Game/Lobby/Materials/M_Lobby_CyanEdge');mats['Panel']=unreal.load_asset('/Game/Lobby/Materials/M_StageSelect_HoloPanel')
textmat=unreal.load_asset('/Game/Lobby/Materials/M_StageSelect_HoloText');cube=unreal.load_asset('/Engine/BasicShapes/Cube');cyl=unreal.load_asset('/Engine/BasicShapes/Cylinder')
def actors():return {a.get_actor_label():a for a in EA.get_all_level_actors()}
def mesh(n,p,s,m='Ivory',shape=None,solid=False,yaw=0):
    if n in actors():return actors()[n]
    a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*p),unreal.Rotator(yaw=yaw));a.set_actor_label(n);a.set_folder_path('Facility');a.set_actor_scale3d(unreal.Vector(*[v/100 for v in s]))
    c=a.static_mesh_component;c.set_static_mesh(shape or cube);c.set_material(0,mats[m]);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('BlockAll' if solid else 'NoCollision');c.set_editor_property('can_ever_affect_navigation',False)
    return a
def sign(n,txt,p,yaw=0,size=45):
    a=actors().get(n)
    if not a:a=EA.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(*p),unreal.Rotator(yaw=yaw));a.set_actor_label(n);a.set_folder_path('Facility')
    a.set_actor_location(unreal.Vector(*p),False,False);a.set_actor_rotation(unreal.Rotator(yaw=yaw),False)
    c=a.get_components_by_class(unreal.TextRenderComponent)[0];c.set_font(unreal.load_asset('/Engine/EngineFonts/RobotoDistanceField'));c.set_text(txt);c.set_world_size(size);c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER);c.set_text_material(textmat);c.set_text_render_color(unreal.Color(255,255,255,255))
    return a
def trigger(n,p,action,destination=None,extent=(150,90,150),yaw=0):
    a=actors().get(n)
    if not a:a=EA.spawn_actor_from_class(unreal.HubPortal,unreal.Vector(*p),unreal.Rotator(yaw=yaw));a.set_actor_label(n);a.set_folder_path('Facility/Interaction')
    a.set_editor_property('action',action);a.get_editor_property('trigger').set_box_extent(unreal.Vector(*extent))
    if destination:a.set_editor_property('destination',unreal.load_asset(destination))
    return a
def gm(path,archive):
    b=unreal.load_asset(path)
    if not b:
        f=unreal.BlueprintFactory();f.set_editor_property('parent_class',unreal.BoarFacilityGameMode)
        b=AT.create_asset(path.rsplit('/',1)[1],path.rsplit('/',1)[0],unreal.Blueprint,f)
    d=unreal.get_default_object(b.generated_class());d.set_editor_property('archive',archive)
    d.set_editor_property('default_pawn_class',unreal.load_asset('/Game/BP/Player/BP_PlayerCharacter').generated_class());d.set_editor_property('player_controller_class',unreal.load_asset('/Game/BP/Lobby/BP_PC_Lobby').generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(b);assert AS.save_loaded_asset(b);report['assets'].append(path);return b.generated_class()
def room(path,mode,archive):
    if AS.does_asset_exist(path):raise RuntimeError('Existing map, inspect before editing '+path)
    assert LS.new_level(path)
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();w.get_world_settings().set_editor_property('default_game_mode',mode)
    hx,hy=(900,700) if archive else (1200,900)
    mesh('FacilityFloor',(0,0,-40),(hx*2,hy*2,80),'Ivory',solid=True)
    for side in [-1,1]:
        mesh('WallX'+str(side),(side*hx,0,110),(50,hy*2,240),'Ivory',solid=True)
        mesh('WallY'+str(side),(0,side*hy,110),(hx*2,50,240),'Ivory',solid=True)
        mesh('WallCapY'+str(side),(0,side*hy,240),(hx*2,65,24),'Navy')
        mesh('WallLightY'+str(side),(0,side*(hy-28),90),(hx*2-150,7,12),'Glow')
    sun=EA.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,800),unreal.Rotator(pitch=-45,yaw=-35));sun.set_actor_label('FacilitySun');sun.light_component.set_editor_property('intensity',4);sun.light_component.set_editor_property('atmosphere_sun_light',True)
    EA.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector(0,0,0))
    sky=EA.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,600));sky.light_component.set_editor_property('real_time_capture',True);sky.light_component.set_editor_property('intensity',1.2)
    start=EA.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(-hx+450,0,100),unreal.Rotator(yaw=0));start.set_actor_label('FacilityPlayerStart')
    # Return gate is behind spawn, with a gap that prevents immediate return loops.
    rx=-hx+130
    for y in [-185,185]:
        mesh('ReturnPillar'+str(y),(rx,y,210),(95,95,420),'Ivory')
        mesh('ReturnBase'+str(y),(rx,y,30),(130,130,60),'Navy')
        mesh('ReturnAccent'+str(y),(rx,y,430),(115,115,30),'Yellow')
        mesh('ReturnGlow'+str(y),(rx+51,y,210),(7,20,280),'Glow')
    mesh('ReturnTop',(rx,0,465),(95,465,70),'Ivory')
    mesh('ReturnField',(rx,0,235),(4,270,420),'Panel')
    sign('ReturnSign','LOBBY',(rx+60,0,500),0,54)
    trigger('ReturnToLobby',(rx,0,140),unreal.HubPortalAction.TRAVEL,'/Game/Level/L_Lobby',extent=(80,140,150))
    # Walk into the glowing pad to open the original UI; Back restores local play.
    ty=-hy+230
    mesh('TerminalBase',(0,ty,35),(300,160,70),'Navy',solid=True)
    mesh('TerminalBody',(0,ty,100),(280,145,70),'Ivory',solid=True)
    mesh('TerminalMonitor',(0,ty-45,235),(340,6,190),'Panel')
    for x in [-175,175]:mesh('MonitorEdge'+str(x),(x,ty-40,235),(6,6,200),'Glow')
    sign('TerminalTitle','ENCYCLOPEDIA' if archive else 'GADGET LOADOUT',(0,ty-34,315),90,42)
    mesh('TerminalPad',(0,ty+160,2),(300,190,4),'Cyan')
    trigger('OpenFacilityUI',(0,ty+160,130),unreal.HubPortalAction.ARCHIVE if archive else unreal.HubPortalAction.LOADOUT,extent=(145,85,140))
    sign('TerminalInstruction','ENTER PAD TO OPEN',(0,ty+45,175),90,24)
    if archive:
        for i,(x,y) in enumerate([(480,-350),(500,300),(-180,410)]):
            mesh('DisplayBase'+str(i),(x,y,45),(230,180,90),'Navy',solid=True)
            mesh('DisplayTop'+str(i),(x,y,100),(250,200,25),'Ivory')
            for s in [-1,1]:mesh('DisplayPage'+str(i)+str(s),(x+s*46,y,130),(85,95,10),'Cyan')
            mesh('DisplaySpine'+str(i),(x,y,135),(9,110,10),'Yellow')
            mesh('ArchivePanel'+str(i),(x+65,y,235),(4,250,165),'Panel')
        sign('ArchiveWallTitle','RESEARCH ARCHIVE',(hx-40,0,400),180,60)
    else:
        for i,x in enumerate([-200,170,520]):
            mesh('TestPlatform'+str(i),(x,480,35+i*25),(240,270,70+i*50),'Ice',solid=True)
            mesh('TestEdge'+str(i),(x,350,72+i*50),(240,7,4),'Yellow')
        ramp=mesh('TestRamp',(-460,480,32),(330,250,22),'Cyan',solid=True);ramp.set_actor_rotation(unreal.Rotator(pitch=11),False)
        for i,y in enumerate([-220,130]):
            a=EA.spawn_actor_from_class(unreal.GadgetTestTarget,unreal.Vector(700,y,140));a.set_actor_label('AttackTestTarget'+str(i));a.set_actor_scale3d(unreal.Vector(.8,.8,1.5))
            mesh('TargetBase'+str(i),(700,y,25),(130,130,50),'Navy',solid=True)
        sign('TestTitle','GADGET TEST',(hx-40,0,470),180,60)
    bush=unreal.load_asset('/Game/InportAssets/LPRiverForest/Meshes/Plants/SM_LPBush01')
    for side in [-1,1]:
        x,y=hx-180,side*(hy-150)
        mesh('Planter'+str(side),(x,y,30),(160,160,60),'Ice',solid=True)
        if bush:
            a=mesh('Plant'+str(side),(x,y,65),(55,55,55),'Ivory',shape=bush)
            for k,m in enumerate(bush.get_editor_property('static_materials')):a.static_mesh_component.set_material(k,m.material_interface)
    assert LS.save_current_level();report['maps'].append(path)
def lobby():
    assert LS.load_level('/Game/Level/L_Lobby')
    aa=actors()
    # Archive old same-level functionality, preserving actor identity and transforms.
    exact=['GadgetArea','GadgetBench','GadgetTarget_A','GadgetTarget_B','GadgetSign','PracticeSign','PracticeStart','PracticeLanding','PracticeRamp']
    prefixes=('Port_Gadget','LobbyArt_Gadget','LobbyArt_Practice','LobbyArt_Hub3_Work','LobbyArt_Hub3_Blueprint','LobbyArt_Hub3_Tool','LobbyArt_Hub3_Screen','LobbyArt_Hub3_Test','LobbyArt_Hub3_ArchiveBase','LobbyArt_Hub3_ArchiveDesk','LobbyArt_Hub3_ArchiveTop','LobbyArt_Hub3_ArchiveScreen','LobbyArt_Hub3_ArchiveEdge','LobbyArt_Hub3_ArchiveFrame','LobbyArt_Hub3_ArchiveCaption','LobbyArt_Hub3_ArchivePage','LobbyArt_Hub3_ArchiveSpine','LobbyArt_Hub3_ArchiveGuide')
    for n,a in aa.items():
        # Existing practice block labels are confirmed by their geometry/location, not arbitrary project matches.
        p=a.get_actor_location();practiceblock=isinstance(a,unreal.StaticMeshActor) and abs(p.y-930)<1 and p.x in [-350,50,480]
        oldportal=n.startswith('LobbyArt_Hub3_Arch_') or (n.startswith(('LobbyArt_Hub3_Gadget','LobbyArt_Hub3_Archive')) and any(k in n for k in ['Tower','Base','Yellow','Inset','Lamp','Sign','Frame']))
        if n in exact or n.startswith(prefixes) or practiceblock or oldportal:
            a.set_actor_hidden_in_game(True);a.set_actor_enable_collision(False)
            for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
            report['hidden'].append(n)
    # Reuse the exact large gate meshes/tower vocabulary at 80% scale.
    for side,title in [(-1,'GADGET LOADOUT'),(1,'ENCYCLOPEDIA')]:
        prefix='PortalGadget' if side<0 else 'PortalArchive';yaw=side*90;target_y=side*970;scale=.8
        for material in ['Ivory','Navy','Glow']:
            source=unreal.load_asset('/Game/Lobby/Meshes/SM_GateEnergy_'+material);assert source
            mesh(prefix+'Arch'+material,(0,target_y-side*1210*scale,0),(80,80,80),material,shape=source,yaw=yaw)
        for n,a in aa.items():
            if not n.startswith('LobbyArt_Energy_Tower'):continue
            p=a.get_actor_location();s=a.get_actor_scale3d();c=a.static_mesh_component
            xx=-side*p.y*scale;yy=target_y+side*(p.x-1210)*scale
            copy=mesh(prefix+n,(xx,yy,p.z*scale),(s.x*80,s.y*80,s.z*80),'Ivory',shape=c.get_editor_property('static_mesh'),yaw=yaw)
            copy.static_mesh_component.set_material(0,c.get_material(0))
        mesh(prefix+'Field',(0,target_y,300),(370,4,560),'Panel')
        mesh(prefix+'Sign',(0,target_y-side*100,730),(700,4,100),'Panel')
        for z in [677,783]:mesh(prefix+'FrameZ'+str(z),(0,target_y-side*106,z),(714,7,7),'Glow')
        for x in [-355,355]:mesh(prefix+'FrameX'+str(x),(x,target_y-side*106,730),(7,7,110),'Glow')
        # Retain and reposition the existing editable title actor.
        sign('LobbyArt_Hub3_GadgetTitle' if side<0 else 'LobbyArt_Hub3_ArchiveTitle',title,(0,target_y-side*115,710),90 if side<0 else -90,56)
        trigger(prefix+'Travel',(0,target_y,140),unreal.HubPortalAction.TRAVEL,'/Game/Level/L_GadgetLab' if side<0 else '/Game/Level/L_Archive',extent=(160,85,150))
        for x in [-180,180]:mesh(prefix+'FloorGuide'+str(x),(x,side*770,6),(9,360,3),'Cyan')
    assert LS.save_current_level();report['maps'].append('/Game/Level/L_Lobby')
def run():
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    mode1=gm('/Game/BP/Lobby/BP_GM_GadgetLab',False);mode2=gm('/Game/BP/Lobby/BP_GM_Archive',True)
    room('/Game/Level/L_GadgetLab',mode1,False);room('/Game/Level/L_Archive',mode2,True);lobby()
try:run()
except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
finally:(root/'build_facility_levels.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

