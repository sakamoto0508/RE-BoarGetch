import unreal,json,traceback
from pathlib import Path
out={};ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
try:
 out['world']=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
 out['actors']=[]
 for a in ea.get_all_level_actors():
  n=a.get_actor_label()
  if any(k in n for k in ['Portal','Return','HubTest','Meshy']):
   row={'name':n,'pos':str(a.get_actor_location()),'hidden':a.get_editor_property('hidden'),'components':[]}
   for c in a.get_components_by_class(unreal.PrimitiveComponent):
    row['components'].append({'name':c.get_name(),'pos':str(c.get_world_location()),'collision':str(c.get_collision_enabled()),'pawn':str(c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)),'overlap':c.get_editor_property('generate_overlap_events'),'extent':str(c.get_scaled_box_extent()) if isinstance(c,unreal.BoxComponent) else '', 'visible':c.is_visible()})
   out['actors'].append(row)
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))

