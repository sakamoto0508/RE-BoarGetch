import unreal,math,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'facility_equipment_structure.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
out={'assets':[],'levels':[],'hidden':[]}
def hide(a):
 a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True);a.set_actor_enable_collision(False);a.set_actor_tick_enabled(False)
 for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
 out['hidden'].append(a.get_actor_label())
def portal(prefix,x,label,action,destination=None):
 made=[]
 for y in [-195,195]:
  for part,p,s,m in [('Base',(x,y,36),(145,145,72),'Navy'),('Pillar',(x,y,237),(110,100,360),'Ivory'),('Joint',(x,y,418),(135,125,42),'Yellow'),('Inset',(x-58,y,245),(8,48,230),'Navy'),('Light',(x-64,y,245),(5,18,215),'Glow')]:
   made.append(mesh(prefix+part+str(y),p,s,m))
 made.append(mesh(prefix+'Top',(x,0,457),(125,490,54),'Ivory'))
 made.append(mesh(prefix+'Field',(x,0,235),(4,278,400),'Panel'))
 made.append(mesh(prefix+'Pad',(x-70,0,2),(260,320,4),'Cyan'))
 caption=sign(prefix+'Title',label,(x-78,0,510),180,36);made.append(caption)
 actor=trigger(prefix+'Trigger',(x,0,140),action,destination,extent=(85,140,145))
 return actor,made,caption
def testlevel():
 p='/Game/Level/L_GadgetTest_Net';assert not AS.does_asset_exist(p),'Existing test map must be audited first'
 assert LS.new_level(p)
 bpp='/Game/BP/Lobby/BP_GM_GadgetTest_Net';assert not AS.does_asset_exist(bpp)
 f=unreal.BlueprintFactory();f.set_editor_property('parent_class',unreal.BoarFacilityGameMode)
 bp=AT.create_asset('BP_GM_GadgetTest_Net','/Game/BP/Lobby',unreal.Blueprint,f);d=unreal.get_default_object(bp.generated_class())
 d.set_editor_property('gadget_test',True);d.set_editor_property('test_gadget_class',unreal.load_asset('/Game/BP/Gadget/BP_NetGadget').generated_class())
 d.set_editor_property('default_pawn_class',unreal.load_asset('/Game/BP/Player/BP_PlayerCharacter').generated_class());d.set_editor_property('player_controller_class',unreal.load_asset('/Game/BP/Lobby/BP_PC_Lobby').generated_class())
 unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert AS.save_loaded_asset(bp);out['assets'].append(bpp)
 w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();w.get_world_settings().set_editor_property('default_game_mode',bp.generated_class())
 mesh('TestFloor',(0,0,-35),(1600,1300,70),'Ivory',solid=True)
 for y in [-650,650]:mesh('TestWallY'+str(y),(0,y,140),(1600,50,280),'Ivory',solid=True);mesh('TestWallCap'+str(y),(0,y,289),(1610,70,24),'Navy')
 for x in [-800,800]:mesh('TestWallX'+str(x),(x,0,140),(50,1300,280),'Ivory',solid=True)
 for y in [-400,400]:mesh('TestGuide'+str(y),(80,y,2),(900,8,4),'Cyan')
 sun=EA.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,700),unreal.Rotator(pitch=-45,yaw=-35));sun.light_component.set_editor_property('intensity',4);sun.light_component.set_editor_property('forward_shading_priority',1)
 for yaw in [140,-80]:
  a=EA.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,650),unreal.Rotator(pitch=-48,yaw=yaw));a.light_component.set_editor_property('intensity',1.6);a.light_component.set_editor_property('cast_shadows',False)
 sky=mesh('Sky',(0,0,0),(30000,30000,30000),'Ivory',shape=unreal.load_asset('/Engine/BasicShapes/Sphere'));sky.static_mesh_component.set_material(0,unreal.load_asset('/Game/Lobby/Materials/M_Facility_Sky'));sky.static_mesh_component.set_cast_shadow(False)
 a=EA.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(-350,0,100),unreal.Rotator(yaw=0));a.set_actor_label('TestPlayerStart')
 # Return gate is behind spawn, far enough to prevent an automatic return loop.
 for y in [-185,185]:
  mesh('ReturnFrame'+str(y),(-660,y,210),(85,85,420),'Ivory');mesh('ReturnBase'+str(y),(-660,y,35),(115,115,70),'Navy');mesh('ReturnJoint'+str(y),(-660,y,427),(110,110,30),'Yellow')
 mesh('ReturnTop',(-660,0,462),(90,460,50),'Ivory');mesh('ReturnField',(-660,0,220),(4,275,400),'Panel')
 sign('ReturnTitle','GADGET LAB',(-600,0,495),0,38)
 trigger('ReturnToGadgetLab',(-660,0,140),unreal.HubPortalAction.TRAVEL,'/Game/Level/L_GadgetLab',extent=(70,140,145))
 for i,y in enumerate([-220,220]):
  a=EA.spawn_actor_from_class(unreal.NetPracticeBoar,unreal.Vector(220,y,70),unreal.Rotator(yaw=180));a.set_actor_label('NetPracticeTarget'+str(i))
  mesh('TargetMat'+str(i),(220,y,2),(270,240,4),'Cyan')
 sign('TestTitle','NET CAPTURE TEST',(750,0,410),180,45)
 sign('TestGuide','CAPTURE / RESET / REPEAT',(750,0,355),180,25)
 assert LS.save_current_level();out['levels'].append(p)
 data=unreal.load_asset('/Game/DataAssets/Gadget/DA_Gadget');data.set_editor_property('initially_unlocked',True);data.set_editor_property('test_level',unreal.load_asset(p));assert AS.save_loaded_asset(data);out['assets'].append(data.get_path_name())
def hub():
 assert LS.load_level('/Game/Level/L_GadgetLab');aa=actors()
 # Retain all actor/asset identities. Old merged visual batches are archived intact.
 for n,a in aa.items():
  if (n.startswith(('Equipment_','TestPlatform','TestEdge','TestRamp','AttackTestTarget','TargetBase')) or n=='TestTitle' or (n.startswith('Quality_') and not n.startswith('Quality_SoftFill'))):hide(a)
 G.clear();architecture(1200,900);wall_support(1200,900)
 ring('Navy',0,-80,190,320,1.8);ring('Cyan',0,-80,255,300,2);ring('Yellow',0,-80,309,318,2.2,n=16,gap=.06)
 drum('Navy',0,-80,3,185,16);drum('Ivory',0,-80,19,169,18)
 for x in [-173,173]:
  box('Ivory',(x,-95,170),(42,100,285));box('Navy',(x,-95,42),(70,125,84));box('Yellow',(x,-95,316),(60,120,28));box('Glow',(x,-38,205),(12,6,145))
 box('Ivory',(0,-140,347),(405,80,35));box('Navy',(0,-142,397),(580,25,80));box('Cyan',(0,-124,397),(550,5,54))
 ring('Glow',0,-80,85,92,153);gem('Holo',(0,-80,225),(110,105,100))
 for x in [-260,260]:
  drum('Navy',x,-170,0,65,65);drum('Ivory',x,-170,65,56,30);box('Yellow',(x,-170,100),(70,75,14))
 floorzone('Collection',0,640,1050,250)
 panel('Collection',(0,768,340),(1000,35,235))
 # One real catalog entry. Do not invent other gadgets or unlock counts.
 box('Navy',(0,650,50),(350,200,100));box('Ivory',(0,650,108),(370,220,22))
 box('Yellow',(-174,650,119),(18,220,16));box('Yellow',(174,650,119),(18,220,16))
 beam('Navy',(0,650,130),(0,650,235),22)
 arch('Ivory',0,650,283,62,12,15);arch('Ivory',0,650,283,62,12,15,plane='xz')
 # Complete the lower half of the net head with a short faceted loop.
 for i in range(12):
  a=math.pi+math.pi*i/12;b=math.pi+math.pi*(i+1)/12
  beam('Ivory',(62*math.cos(a),650,283+62*math.sin(a)),(62*math.cos(b),650,283+62*math.sin(b)),12,15)
 for x in [-30,0,30]:beam('Cyan',(x,647,233),(x,647,333),3)
 for z in [253,283,313]:beam('Cyan',(-48,647,z),(48,647,z),3)
 # Floor routes point to return, collection and the single test destination.
 for x in [-550,550]:box('Ice',(x,0,1),(500,220,1));box('Cyan',(x,118,2),(500,9,1));box('Yellow',(x,-118,2),(500,9,1))
 box('Ice',(0,380,1),(245,270,1));box('Cyan',(-130,380,2),(9,270,1));box('Cyan',(130,380,2),(9,270,1))
 save_groups('GadgetHub')
 sign('HubStationTitle','GADGET LOADOUT & UNLOCK',(0,-121,380),90,31)
 sign('HubStationReverse','GADGET LOADOUT & UNLOCK',(0,-160,380),-90,31)
 sign('HubCollectionTitle','GADGET COLLECTION',(0,740,489),-90,46)
 label=sign('HubCollectionNet','READY / NET',(0,526,175),-90,35)
 # Face data panel into the room.
 mesh('HubCollectionFace',(0,744,345),(965,4,205),'Panel')
 actor,visuals,title=portal('HubTest',1010,'GADGET TEST',unreal.HubPortalAction.GADGET_TEST)
 actor.set_editor_property('test_visual_actors',visuals);actor.set_editor_property('test_label',title);actor.set_editor_property('collection_labels',[label])
 actor.set_actor_hidden_in_game(True)
 for a in visuals:a.set_actor_hidden_in_game(True)
 # Trigger disables itself at BeginPlay until selection; static editor position remains reviewable.
 assert LS.save_current_level();out['levels'].append('/Game/Level/L_GadgetLab')
 for p in ['/Game/BP/Widget/WBP_GadgetLoadout','/Game/BP/Lobby/BP_GM_GadgetLab','/Game/BP/Core/BP_BoarGameInstance']:
  b=unreal.load_asset(p);unreal.BlueprintEditorLibrary.compile_blueprint(b);assert AS.save_loaded_asset(b);out['assets'].append(p)
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 testlevel();hub()
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
finally:(ROOT/'build_gadget_lab_hub.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
