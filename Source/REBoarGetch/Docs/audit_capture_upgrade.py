import unreal, json, traceback
from pathlib import Path

paths = [
 '/Game/BP/Player/BP_BoarPlayerController', '/Game/BP/Lobby/BP_PC_Lobby',
 '/Game/BP/Boar/BP_NormalBoar', '/Game/BP/Gadget/BP_NetGadget',
 '/Game/BP/Cage/BP_Cage', '/Game/REBoarGetch/Art/VFX/Capture/M_CaptureGlow',
 '/Game/REBoarGetch/Art/VFX/Capture/NS_CaptureSuccess',
 '/Game/InportAssets/Meshy_AI_ローポリ風のサ_quadruped/Meshes/Material_1',
]
out = {'assets': {}, 'existing_new_assets': {}, 'dirty': {}}
try:
 for p in paths:
  a = unreal.load_asset(p)
  out['assets'][p] = str(a.get_class().get_name()) if a else None
  if a and isinstance(a, unreal.Blueprint):
   cd = unreal.get_default_object(a.generated_class())
   comps = []
   for c in cd.get_components_by_class(unreal.ActorComponent):
    if any(n in c.get_name().lower() for n in ['capture', 'cage', 'camera', 'mesh']):
     comps.append({'name': c.get_name(), 'class': c.get_class().get_name()})
   out['assets'][p] = {'class': a.get_class().get_name(), 'components': comps}
   if 'Controller' in p:
    c = cd.get_component_by_class(unreal.CapturePresentationComponent)
    out['assets'][p]['presentation'] = {n: str(c.get_editor_property(n)) for n in ['hit_stop_duration','slow_duration','camera_zoom_amount','camera_duration','glow_duration','glow_material','capture_vfx','impact_sound','capture_success_sound']} if c else None
 for n in ['M_CaptureDataTransfer','NS_CaptureRing','NS_CaptureTransfer','NS_CageReceive']:
  p='/Game/REBoarGetch/Art/VFX/Capture/'+n
  out['existing_new_assets'][p]=unreal.EditorAssetLibrary.does_asset_exist(p)
 out['dirty']['maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 out['dirty']['content']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
 out['material_custom_properties']=[x for x in dir(unreal.MaterialExpressionCustom) if 'input' in x.lower() or 'output' in x.lower()]
 out['custom_input_type']=[x for x in dir(unreal) if 'CustomInput' in x]
except Exception: out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2,ensure_ascii=False))
