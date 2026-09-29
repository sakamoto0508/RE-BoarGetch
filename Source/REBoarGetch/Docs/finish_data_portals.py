import unreal,json,traceback
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
out={}
try:
 mi=unreal.load_asset('/Game/Lobby/Materials/MI_DataPortal_Default')
 unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,'GridBrightness',.10)
 unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,'GridDensity',5)
 unreal.EditorAssetLibrary.save_loaded_asset(mi)
 assert LS.load_level('/Game/Level/L_GadgetLab')
 actors={a.get_actor_label():a for a in EA.get_all_level_actors()}
 sample=actors['Meshy_AI_Neon_Portal_Frame_0928055927_texture'];duplicate=actors['DataPortal_Return_Frame']
 sample.set_actor_transform(duplicate.get_actor_transform(),False,False)
 sample.static_mesh_component.set_collision_profile_name('NoCollision')
 sample.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
 sample.static_mesh_component.set_editor_property('can_ever_affect_navigation',False)
 duplicate.static_mesh_component.set_visibility(False);duplicate.set_actor_hidden_in_game(True)
 actors['ReturnSign'].set_actor_hidden_in_game(True);actors['ReturnSign'].get_component_by_class(unreal.TextRenderComponent).set_visibility(False)
 assert LS.save_current_level()
 out['reused_existing_imported_frame_for_lab_return']=sample.get_path_name()
 for level in ['L_Lobby','L_GadgetLab','L_Archive','L_GadgetTest_Net']:
  assert LS.load_level('/Game/Level/'+level)
  rows=[]
  for a in EA.get_all_level_actors():
   if a.get_actor_label().startswith('DataPortal_'):
    c=a.static_mesh_component;r=a.get_actor_rotation()
    rows.append({'name':a.get_actor_label(),'mesh':c.static_mesh.get_path_name(),'visible':c.is_visible(),'hidden_in_game':a.get_editor_property('hidden'),'collision':str(c.get_collision_enabled()),'rotation':[r.pitch,r.yaw,r.roll]})
   if a.get_actor_label()=='HubTestTrigger':out['test_visual_references']=[v.get_actor_label() for v in a.get_editor_property('test_visual_actors') if v]
  out[level]=rows
 out['material_parent']=mi.get_editor_property('parent').get_path_name()
 out['grid_brightness']=unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mi,'GridBrightness')
 assert LS.load_level('/Game/Level/L_Lobby')
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
