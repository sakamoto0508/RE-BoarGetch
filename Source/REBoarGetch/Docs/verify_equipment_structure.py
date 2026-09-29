import unreal,json
from pathlib import Path
R=Path(__file__).parent;baseline=json.loads((R/'audit_facility_quality.json').read_text(encoding='utf-8'))
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);out={}
for level in ['L_Archive','L_GadgetLab']:
 assert LS.load_level('/Game/Level/'+level)
 aa={a.get_actor_label():a for a in EA.get_all_level_actors()};issues=[]
 for row in baseline[level]:
  a=aa.get(row['label']);assert a,row['label']
  p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d()
  for now,key in [([p.x,p.y,p.z],'p'),([r.pitch,r.yaw,r.roll],'r'),([s.x,s.y,s.z],'s')]:
   if any(abs(x-y)>.01 for x,y in zip(now,row[key])):issues.append(row['label']+key)
  for c,d in zip(a.get_components_by_class(unreal.PrimitiveComponent),row['components']):
   assert str(c.get_collision_enabled())==d['collision'];assert str(c.get_collision_profile_name())==d['profile']
  if isinstance(a,unreal.HubPortal):
   assert str(a.get_editor_property('action'))==row['action']
   dest=a.get_editor_property('destination');assert (dest.get_path_name() if dest else None)==(row['destination'].split("'")[1] if row['destination']!='None' else None)
 assert not issues,issues
 out[level]={'preserved_actors':len(baseline[level]),'issues':issues}
out['dirty_maps']=[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(R/'verify_equipment_structure.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
