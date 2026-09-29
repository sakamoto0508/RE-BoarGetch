import unreal,json
from pathlib import Path
out={};ml=unreal.MaterialEditingLibrary
for name in ['M_Preview_Grass','M_UI_Diorama']:
 m=unreal.load_asset('/Game/UI/StagePreview/'+name);rows=[];seen=set()
 def walk(n):
  if not n or n.get_path_name() in seen:return
  seen.add(n.get_path_name());d={'type':n.get_class().get_name()}
  for k in ['constant','const_a','const_b','default_value','parameter_name']:
   try:d[k]=str(n.get_editor_property(k))
   except Exception:pass
  rows.append(d)
  for v in ml.get_inputs_for_material_expression(m,n):walk(v)
 walk(ml.get_material_property_input_node(m,unreal.MaterialProperty.MP_EMISSIVE_COLOR));out[name]=rows
 ml.recompile_material(m)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
