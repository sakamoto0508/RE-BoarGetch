import unreal, json, traceback
from pathlib import Path
E=unreal.EditorAssetLibrary; M=unreal.MaterialEditingLibrary
p='/Game/REBoarGetch/Art/VFX/Capture/M_CaptureDataTransfer'
o={'path':p,'already_exists':E.does_asset_exist(p)}
try:
 if o['already_exists']:
  mat=unreal.load_asset(p)
  M.delete_all_material_expressions(mat)
 if not o['already_exists']:
  mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_CaptureDataTransfer','/Game/REBoarGetch/Art/VFX/Capture',unreal.Material,unreal.MaterialFactoryNew())
 if True:
  assert mat
  mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
  mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
  mat.set_editor_property('two_sided',True)
  M.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
  def node(cls,x,y): return M.create_material_expression(mat,cls,x,y)
  wp=node(unreal.MaterialExpressionWorldPosition,-800,-450)
  progress=node(unreal.MaterialExpressionScalarParameter,-800,-300);progress.set_editor_property('parameter_name','DissolveProgress');progress.set_editor_property('default_value',0.0)
  minz=node(unreal.MaterialExpressionScalarParameter,-800,-150);minz.set_editor_property('parameter_name','CaptureMinZ');minz.set_editor_property('default_value',0.0)
  height=node(unreal.MaterialExpressionScalarParameter,-800,0);height.set_editor_property('parameter_name','CaptureHeight');height.set_editor_property('default_value',150.0)
  amount=node(unreal.MaterialExpressionScalarParameter,-800,150);amount.set_editor_property('parameter_name','GlowAmount');amount.set_editor_property('default_value',1.0)
  color=node(unreal.MaterialExpressionVectorParameter,-800,300);color.set_editor_property('parameter_name','GlowColor');color.set_editor_property('default_value',unreal.LinearColor(0.01,0.72,1,1))
  custom=node(unreal.MaterialExpressionCustom,-300,-220)
  custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT4)
  inputs=[]
  for n in ['WP','Progress','MinZ','Height','Glow','Color']:
   ci=unreal.CustomInput();ci.set_editor_property('input_name',n);inputs.append(ci)
  custom.set_editor_property('inputs',inputs)
  custom.set_editor_property('code',r'''
float h = max(Height, 1.0);
float z = saturate((WP.z-MinZ)/h);
float3 cell = floor(WP.xyz*0.045);
float jitter = frac(sin(dot(cell,float3(12.9898,78.233,37.719)))*43758.5453);
float threshold = Progress + (jitter-0.5)*0.105;
float alive = (z > threshold) ? 1.0 : 0.0;
float scan = 1.0-smoothstep(0.0,0.075,abs(z-Progress));
float3 g = abs(frac(WP.xyz*0.044)-0.5);
float grid = 1.0-smoothstep(0.455,0.49,max(g.x,max(g.y,g.z)));
float tri = 1.0-smoothstep(0.025,0.07,abs(frac((WP.x+WP.y)*0.032)-0.5));
float spark = step(0.945,jitter)*step(0.68,frac(WP.z*0.08));
float3 cyan = max(Color.rgb,0.0);
float3 rgb = cyan*(1.8+2.3*grid+0.7*tri+6.0*scan+3.0*spark)*max(Glow,0.0);
rgb += float3(0.12,0.17,0.18)*scan*max(Glow,0.0);
return float4(rgb,alive);
''')
  for n,src in [('WP',wp),('Progress',progress),('MinZ',minz),('Height',height),('Glow',amount),('Color',color)]:
   assert M.connect_material_expressions(src,'',custom,n),n
  rgb=node(unreal.MaterialExpressionComponentMask,80,-250);rgb.set_editor_property('r',True);rgb.set_editor_property('g',True);rgb.set_editor_property('b',True)
  alpha=node(unreal.MaterialExpressionComponentMask,80,50);alpha.set_editor_property('a',True)
  assert M.connect_material_expressions(custom,'',rgb,'None')
  assert M.connect_material_expressions(custom,'',alpha,'None')
  assert M.connect_material_property(rgb,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  assert M.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY_MASK)
  M.recompile_material(mat)
  assert E.save_loaded_asset(mat)
 o['expressions']=M.get_num_material_expressions(mat)
 o['saved']=True
except Exception:o['error']=traceback.format_exc()
Path(__file__).with_suffix('.json').write_text(json.dumps(o,indent=2))
