import unreal,json,traceback
from pathlib import Path
ML=unreal.MaterialEditingLibrary;AS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
out={'saved':[]};base='/Game/UI/StagePreview/'
palette={'Grass':(.23,.68,.045),'Earth':(.28,.115,.035),'Dirt':(.72,.40,.12),'Water':(.015,.48,.92),'White':(.85,.92,1),'Navy':(.025,.065,.14),'Cyan':(.015,.75,1),'Yellow':(1,.72,.025),'Leaf':(.10,.43,.025)}
def graph(m):
 seen=set();nodes=[]
 def walk(n):
  if not n or n.get_path_name() in seen:return
  seen.add(n.get_path_name());nodes.append(n)
  for v in ML.get_inputs_for_material_expression(m,n):walk(v)
 walk(ML.get_material_property_input_node(m,unreal.MaterialProperty.MP_EMISSIVE_COLOR));return nodes
try:
 for name,col in palette.items():
  m=unreal.load_asset(base+'M_Preview_'+name);assert m
  nodes=graph(m)
  # Preserve the existing mesh-facing shade, using a pixel-stage normal for emissive.
  dot=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionDotProduct))
  shade=ML.get_material_property_input_node(m,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  color=next(n for n in ML.get_inputs_for_material_expression(m,shade) if isinstance(n,unreal.MaterialExpressionConstant3Vector))
  color.set_editor_property('constant',unreal.LinearColor(*col,1))
  normal=ML.create_material_expression(m,unreal.MaterialExpressionPixelNormalWS)
  assert ML.connect_material_expressions(normal,'',dot,'A')
  m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
  ML.recompile_material(m);assert AS.save_loaded_asset(m);out['saved'].append(m.get_path_name())
 m=unreal.load_asset(base+'M_UI_Diorama');nodes=graph(m)
 sample=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionTextureSampleParameter2D))
 blend=ML.get_material_property_input_node(m,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 saturation=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionScalarParameter))
 saturation.set_editor_property('default_value',1.0)
 assert ML.connect_material_expressions(sample,'RGB',blend,'B')
 ML.recompile_material(m);assert AS.save_loaded_asset(m);out['saved'].append(m.get_path_name())
 for p in [base+'BP_Stage01_Diorama','/Game/BP/Widget/WBP_LobbyStageSelect']:
  b=unreal.load_asset(p);unreal.BlueprintEditorLibrary.compile_blueprint(b);assert AS.save_loaded_asset(b);out['saved'].append(p)
 out['parts']=len(unreal.get_default_object(unreal.load_asset(base+'BP_Stage01_Diorama').generated_class()).get_editor_property('parts'))
 out['runtime_test']='Not run'
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
