import unreal,json
from pathlib import Path
b=unreal.load_asset('/Game/BP/Widget/WBP_LoadoutEntry');tree=unreal.find_object(b,'WidgetTree')
out=[]
def walk(w):
 r={'name':w.get_name(),'class':w.get_class().get_name()}
 if isinstance(w,unreal.SizeBox):r['height']=w.get_editor_property('height_override')
 out.append(r)
 if isinstance(w,unreal.PanelWidget):
  for c in w.get_all_children():walk(c)
walk(tree.get_editor_property('root_widget'))
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
