import unreal,json,traceback,re
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
out={}
mesh=unreal.load_asset('/Game/InportAssets/ImportMeshy/Meshy_AI_Neon_Portal_Frame_0928055927_texture/StaticMeshes/Meshy_AI_Neon_Portal_Frame_0928055927_texture')
surface=unreal.load_asset('/Game/Lobby/Meshes/SM_DataPortal_Surface')
plans={'L_Lobby':[('Stage',1210,0,-90,28),('Gadget',0,-970,180,23.2),('Archive',0,970,0,23.2)],'L_GadgetLab':[('Return',-1070,0,90,16),('Test',1010,0,-90,16)],'L_Archive':[('Return',-770,0,90,16)],'L_GadgetTest_Net':[('Return',-660,0,90,16)]}
def state(a):
 p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d()
 return {'transform':[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z],'collision':[(c.get_name(),str(c.get_collision_enabled()),str(c.get_collision_profile_name())) for c in a.get_components_by_class(unreal.PrimitiveComponent)]}
def obsolete(level,n):
 if level=='L_Lobby':
  return bool(re.match(r'^(StageGate_(Left|Right|Lintel|Beacon)|Port_GateArch|LobbyArt_Energy_(Arch_|Tower)|LobbyArt_GateFoot|LobbyArt_GateCrown|LobbyArt_GateCrest|LobbyArt_Ref_Gate|Portal(Gadget|Archive)(Arch|Field|LobbyArt_Energy_Tower))',n))
 return bool(re.match(r'^Return(Pillar|Frame|Base|Accent|Glow|Joint|Top|Field)',n) or (level=='L_GadgetLab' and re.match(r'^HubTest(Base|Pillar|Joint|Inset|Light|Top|Field)',n)))
try:
 assert mesh and surface
 for level,entries in plans.items():
  assert LS.load_level('/Game/Level/'+level)
  actors={a.get_actor_label():a for a in EA.get_all_level_actors()}
  assert not any(n.startswith('DataPortal_') for n in actors),'Already placed '+level
  before={a.get_path_name():state(a) for a in actors.values()}
  hidden=[]
  for n,a in actors.items():
   if isinstance(a,unreal.StaticMeshActor) and obsolete(level,n):
    a.static_mesh_component.set_visibility(False);a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True);hidden.append(n)
  added=[]
  for name,x,y,yaw,scale in entries:
   pair=[]
   for kind,asset in [('Frame',mesh),('Surface',surface)]:
    a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(x,y,0),unreal.Rotator(pitch=0,yaw=yaw,roll=0))
    a.set_actor_label('DataPortal_'+name+'_'+kind);a.set_folder_path('Art/DataPortals')
    a.set_actor_scale3d(unreal.Vector(scale,scale,scale))
    c=a.static_mesh_component;c.set_static_mesh(asset);c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property('can_ever_affect_navigation',False)
    if kind=='Surface':c.set_cast_shadow(False)
    if name=='Test':a.set_actor_hidden_in_game(True)
    pair.append(a);added.append(a.get_actor_label())
   if name=='Test':
    trigger=actors['HubTestTrigger']
    old=list(trigger.get_editor_property('test_visual_actors'))
    retained=[a for a in old if a and a.get_actor_label() not in hidden]
    trigger.set_editor_property('test_visual_actors',retained+pair)
  assert all(state(a)==before[a.get_path_name()] for a in actors.values()),'Existing transform/collision changed'
  assert LS.save_current_level()
  out[level]={'added':added,'hidden_originals':hidden,'existing_transforms_collision_unchanged':True,'saved':True}
 assert LS.load_level('/Game/Level/L_Lobby')
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
