import unreal,json,traceback
from pathlib import Path
out={};EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
try:
 for level in ['L_GadgetLab','L_Lobby']:
  assert LS.load_level('/Game/Level/'+level);rows=[]
  for a in EA.get_all_level_actors():
   if not a.get_actor_label().startswith('Architecture_'):continue
   r={'actor':a.get_actor_label()}
   if isinstance(a,unreal.StaticMeshActor):
    c=a.static_mesh_component;r.update(mesh=c.static_mesh.get_path_name(),material=c.get_material(0).get_path_name(),collision=str(c.get_collision_enabled()))
    assert c.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
   elif isinstance(a,unreal.RectLight):
    c=a.get_component_by_class(unreal.RectLightComponent);r.update(lumens=c.get_editor_property('intensity'),temperature=c.get_editor_property('temperature'))
   rows.append(r)
  out[level]=rows
 out['dirty_maps']=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 out['pie_running']=bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
