import unreal,json
from pathlib import Path
out=[]
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 if a.get_actor_label().startswith('DisplayNet_'):
  c=a.get_component_by_class(unreal.SkeletalMeshComponent)
  out.append({'name':a.get_actor_label(),'hidden':a.get_editor_property('hidden'),'editor_hidden':a.is_temporarily_hidden_in_editor(),'visible':c.get_editor_property('visible'),'transform':str(c.get_world_transform()),'bounds':str(a.get_actor_bounds(False))})
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
