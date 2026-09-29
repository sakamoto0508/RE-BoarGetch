import unreal,json,traceback
from pathlib import Path
out={};reg=unreal.AssetRegistryHelpers.get_asset_registry()
try:
 out['assets']={}
 for cls in ['NiagaraSystem','SoundWave','SoundCue']:
  out['assets'][cls]=[str(a.package_name) for a in reg.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Niagara' if cls=='NiagaraSystem' else '/Script/Engine',cls),True) if str(a.package_name).startswith('/Game/')]
 out['blueprints']={}
 for folder in ['/Game/BP/Boar','/Game/BP/Gadget','/Game/BP/Core','/Game/BP/Player','/Game/BP/Cage','/Game/BP/Lobby']:
  for ad in reg.get_assets_by_path(folder,True):
   if str(ad.asset_class_path.asset_name)!='Blueprint':continue
   if not any(s in str(ad.asset_name) for s in ['Boar','Net','Player','Cage','PC_']):continue
   bp=ad.get_asset();cd=unreal.get_default_object(bp.generated_class());row={'class':cd.get_class().get_path_name(),'components':[]}
   for c in (cd.get_components_by_class(unreal.ActorComponent) if isinstance(cd,unreal.Actor) else []):
    v={'name':c.get_name(),'type':c.get_class().get_name()}
    if isinstance(c,unreal.MeshComponent):v['materials']=[m.get_path_name() if m else None for m in c.get_materials()]
    if isinstance(c,unreal.SkeletalMeshComponent):v['skeletal_mesh']=str(c.get_editor_property('skeletal_mesh_asset'))
    if isinstance(c,unreal.CameraComponent):v['fov']=c.field_of_view
    if isinstance(c,unreal.SpringArmComponent):v['arm_length']=c.target_arm_length
    row['components'].append(v)
   out['blueprints'][str(ad.package_name)]=row
 out['dirty']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()+unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))

