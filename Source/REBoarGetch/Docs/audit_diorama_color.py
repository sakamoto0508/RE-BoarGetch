import unreal,json
from pathlib import Path
bp=unreal.load_asset('/Game/UI/StagePreview/BP_Stage01_Diorama')
cdo=unreal.get_default_object(bp.generated_class())
out={'parts':[],'materials':{}}
for p in cdo.get_editor_property('parts'):
 m=p.get_editor_property('material');out['parts'].append(m.get_path_name() if m else None)
for n in ['Grass','Dirt','Water','White','Leaf']:
 m=unreal.load_asset('/Game/UI/StagePreview/M_Preview_'+n)
 out['materials'][n]={'shading':str(m.get_editor_property('shading_model')),'expressions':[]}
 out['materials'][n]['expressions']=str(unreal.MaterialEditingLibrary.get_material_property_input_node(m,unreal.MaterialProperty.MP_EMISSIVE_COLOR))
widget=unreal.load_asset('/Game/BP/Widget/WBP_LobbyStageSelect')
out['ui_material']=str(unreal.get_default_object(widget.generated_class()).get_editor_property('preview_material'))
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
