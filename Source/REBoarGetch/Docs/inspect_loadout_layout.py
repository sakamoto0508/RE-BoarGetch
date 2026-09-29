import unreal,json
from pathlib import Path
b=unreal.load_asset('/Game/BP/Widget/WBP_GadgetLoadout');tree=unreal.find_object(b,'WidgetTree');out=[]
for n in ['SlotsList','CandidatesList','Text_Status','Button_Back']:
 w=unreal.find_object(tree,n);p=w.get_parent();row={'name':n,'class':w.get_class().get_name(),'parent':p.get_name() if p else None,'slot':str(w.slot)}
 if isinstance(w.slot,unreal.CanvasPanelSlot):row.update({'position':str(w.slot.get_position()),'size':str(w.slot.get_size())})
 out.append(row)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
