import unreal,json,traceback
from pathlib import Path
out={};E=unreal.EditorAssetLibrary;M=unreal.MaterialEditingLibrary
try:
 folder='/Game/REBoarGetch/Art/VFX/Capture';mp=folder+'/M_CaptureGlow';np=folder+'/NS_CaptureSuccess'
 out['existing']={p:E.does_asset_exist(p) for p in [mp,np]}
 mat=unreal.load_asset(mp)
 if not mat:
  mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_CaptureGlow',folder,unreal.Material,unreal.MaterialFactoryNew())
  mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);mat.set_editor_property('two_sided',True)
  M.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
  def node(cls,x,y):return M.create_material_expression(mat,cls,x,y)
  color=node(unreal.MaterialExpressionVectorParameter,-500,0);color.set_editor_property('parameter_name','GlowColor');color.set_editor_property('default_value',unreal.LinearColor(.02,.65,1,1))
  amount=node(unreal.MaterialExpressionScalarParameter,-500,150);amount.set_editor_property('parameter_name','GlowAmount');amount.set_editor_property('default_value',.5)
  strength=node(unreal.MaterialExpressionScalarParameter,-500,300);strength.set_editor_property('parameter_name','GlowStrength');strength.set_editor_property('default_value',6)
  mul=node(unreal.MaterialExpressionMultiply,-230,0);M.connect_material_expressions(color,'',mul,'A');M.connect_material_expressions(strength,'',mul,'B');M.connect_material_property(mul,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  opacity=node(unreal.MaterialExpressionMultiply,-230,160);opacity.set_editor_property('const_b',.65);M.connect_material_expressions(amount,'',opacity,'A');M.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
  M.recompile_material(mat)
 assert E.save_loaded_asset(mat)
 ns=unreal.load_asset(np)
 if not ns:ns=E.duplicate_asset('/Game/InportAssets/LevelPrototyping/Interactable/JumpPad/Assets/NS_JumpPad',np)
 assert ns and E.save_loaded_asset(ns)
 out['material']=mat.get_path_name();out['vfx']=ns.get_path_name()
 out['dirty']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()+unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
except Exception:out['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
