import unreal,json,traceback
from pathlib import Path
out={}
try:
 EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 assert LS.load_level('/Game/Level/L_GadgetLab')
 out['actors']=[]
 for a in EA.get_all_level_actors():
  row={'name':a.get_actor_label(),'class':a.get_class().get_name(),'position':str(a.get_actor_location()),'hidden':a.get_editor_property('hidden')}
  if isinstance(a,unreal.StaticMeshActor):row['mesh']=str(a.static_mesh_component.static_mesh)
  if isinstance(a,unreal.HubPortal):row.update(action=str(a.get_editor_property('action')),destination=str(a.get_editor_property('destination')))
  out['actors'].append(row)
 bp=unreal.load_asset('/Game/BP/Gadget/BP_NetGadget');cd=unreal.get_default_object(bp.generated_class())
 ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);out['components']=[]
 for h in ss.k2_gather_subobject_data_for_blueprint(bp):
  data=unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h);o=unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data,bp)
  if not o:continue
  r={'name':o.get_name(),'class':o.get_class().get_name(),'path':o.get_path_name()}
  if isinstance(o,unreal.MeshComponent):
   r['materials']=[str(m) for m in o.get_materials()];r['transform']=str(o.get_relative_transform())
   if isinstance(o,unreal.StaticMeshComponent):r['mesh']=str(o.static_mesh);r['bounds']=str(o.static_mesh.get_bounding_box()) if o.static_mesh else None
   if isinstance(o,unreal.SkeletalMeshComponent):r['mesh']=str(o.get_skinned_asset())
  out['components'].append(r)
 gi=unreal.get_default_object(unreal.load_asset('/Game/BP/Core/BP_BoarGameInstance').generated_class());out['catalog']=[str(c) for c in gi.get_editor_property('gadget_catalog')]
 d=cd.get_editor_property('gadget_definition');out['definition']={k:str(d.get_editor_property(k)) for k in ['gadget_id','initially_unlocked','required_special_coin_count','test_level']}
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
