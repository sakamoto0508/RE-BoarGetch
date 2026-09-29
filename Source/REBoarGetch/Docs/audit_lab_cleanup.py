import unreal,json,traceback
from pathlib import Path
root=Path(__file__).parent
out={}
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_name()=='L_GadgetLab'
rows=[]
for a in ea.get_all_level_actors():
 r={'label':a.get_actor_label(),'name':a.get_name(),'class':a.get_class().get_name(),'path':a.get_path_name(),'hidden':a.get_editor_property('hidden'),'pos':str(a.get_actor_location())}
 if isinstance(a,unreal.StaticMeshActor):
  c=a.static_mesh_component;r['mesh']=c.static_mesh.get_path_name() if c.static_mesh else None;r['collision']=str(c.get_collision_enabled())
 rows.append(r)
out['actors']=rows
try:
 t=unreal.AssetExportTask();t.object=w;t.filename=str(root/'lab_cleanup_before.t3d');t.automated=True;t.prompt=False;t.exporter=unreal.LevelExporterT3D();out['export']=unreal.Exporter.run_asset_export_task(t)
except Exception:out['error']=traceback.format_exc()
(root/'lab_cleanup_audit.json').write_text(json.dumps(out,indent=2))
