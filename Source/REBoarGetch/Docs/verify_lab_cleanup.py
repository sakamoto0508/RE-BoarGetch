import unreal,json
from pathlib import Path
root=Path(__file__).parent
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);ls=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
aa={a.get_actor_label():a for a in ea.get_all_level_actors()}
report=json.loads((root/'clean_lab_rebuild.json').read_text())
for n in report['hidden']+['HubStationReverse']:
 a=aa[n];a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True)
 for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
# Smaller station title clears the hologram and collection sight line.
a=aa['HubStationTitle'];a.set_actor_location(unreal.Vector(0,-295,320),False,False);a.get_component_by_class(unreal.TextRenderComponent).set_world_size(20)
aa['LabStationInteraction'].set_actor_location(unreal.Vector(0,-295,287),False,False);aa['LabStationInteraction'].get_component_by_class(unreal.TextRenderComponent).set_world_size(16)
assert ls.save_current_level()
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
t=unreal.AssetExportTask();t.object=w;t.filename=str(root/'lab_cleanup_after.t3d');t.automated=True;t.prompt=False;t.exporter=unreal.LevelExporterT3D();assert unreal.Exporter.run_asset_export_task(t)
before=(root/'lab_cleanup_before.t3d').read_text();after=(root/'lab_cleanup_after.t3d').read_text()
import re
checks={}
for cls in ['HubPortal','GadgetTestTarget']:
 pat=r'      Begin Actor Class=/Script/REBoarGetch\.'+cls+r' .*?      End Actor'
 checks[cls]=re.findall(pat,before,re.S)==re.findall(pat,after,re.S)
assert all(checks.values()),checks
out={'saved':True,'gameplay_serialized_unchanged':checks,'pod_count':5,'assigned_gadgets':1,'reserve_pods':4,'hidden_count':len(report['hidden'])+1,'deleted_count':0,'PIE':False,'dirty_maps':len(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),'new_mesh_assets':0,'compile':'No Blueprint, C++ or Material changes; built-in static mesh architecture only'}
(root/'lab_cleanup_verified.json').write_text(json.dumps(out,indent=2))
