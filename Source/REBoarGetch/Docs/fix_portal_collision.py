import unreal,json,traceback
from pathlib import Path
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);ls=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
prior=json.loads(Path(__file__).with_name('place_data_portals.json').read_text());out={}
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 assert ls.save_current_level() # preserve current user edits
 for level in ['L_GadgetLab','L_Lobby','L_Archive','L_GadgetTest_Net']:
  assert ls.load_level('/Game/Level/'+level)
  changed=[]
  for a in ea.get_all_level_actors():
   n=a.get_actor_label()
   if not isinstance(a,unreal.StaticMeshActor):continue
   if n.startswith('DataPortal_') or n in prior[level]['hidden_originals'] or (level=='L_GadgetLab' and n=='Meshy_AI_Neon_Portal_Frame_0928055927_texture'):
    c=a.static_mesh_component
    changed.append({'actor':n,'before':str(c.get_collision_enabled()),'profile_before':str(c.get_collision_profile_name())})
    c.set_collision_profile_name('NoCollision');c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property('generate_overlap_events',False)
  assert ls.save_current_level();out[level]=changed
 # Reload separately to verify serialized profiles, not just in-memory state.
 out['verified']={}
 for level in ['L_Lobby','L_GadgetLab','L_Archive','L_GadgetTest_Net']:
  assert ls.load_level('/Game/Level/'+level)
  names={r['actor'] for r in out[level]};checked=[]
  for a in ea.get_all_level_actors():
   if a.get_actor_label() in names:
    c=a.static_mesh_component
    assert c.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION,a.get_actor_label()
    assert str(c.get_collision_profile_name())=='NoCollision',a.get_actor_label()
    checked.append(a.get_actor_label())
  assert len(checked)==len(names);out['verified'][level]=len(checked)
 assert ls.load_level('/Game/Level/L_GadgetLab')
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
