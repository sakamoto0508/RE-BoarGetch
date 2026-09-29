import unreal,json,traceback
from pathlib import Path
out={}
try:
 bp=unreal.load_asset('/Game/BP/Lobby/BP_Display_Net');cd=unreal.get_default_object(bp.generated_class());c=cd.get_component_by_class(unreal.SkeletalMeshComponent)
 mesh=unreal.load_asset('/Game/InportAssets/mushitoriami/mushitoriami');mats=mesh.get_editor_property('materials')
 def configure(a):
  c=a.get_component_by_class(unreal.SkeletalMeshComponent)
  c.set_editor_property('skeletal_mesh_asset',mesh)
  for i,m in enumerate(mats):c.set_material(i,m.get_editor_property('material_interface'))
  c.set_collision_profile_name('NoCollision');c.set_editor_property('generate_overlap_events',False);c.set_editor_property('can_ever_affect_navigation',False);c.set_simulate_physics(False);c.set_component_tick_enabled(False);c.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
  a.set_actor_enable_collision(False);a.set_actor_tick_enabled(False)
 configure(cd)
 unreal.EditorAssetLibrary.save_loaded_asset(bp)
 for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
  if a.get_actor_label() in ['DisplayNet_Station','DisplayNet_Collection']:configure(a)
 exec((Path(__file__).parent/'verify_real_display.py').read_text())
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
