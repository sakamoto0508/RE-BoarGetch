from pathlib import Path
p=Path(__file__).parent/'build_gate_energy.py'
s=p.read_text(encoding='utf-8')
insertion='''
original_create=create
original_spawn=spawn
def create(path,cls,factory):
    if AS.does_asset_exist(path):
        a=unreal.load_asset(path)
        if isinstance(a,unreal.Material):ML.delete_all_material_expressions(a)
        return a
    return original_create(path,cls,factory)
def spawn(label,*args,**kwargs):
    existing=next((a for a in EA.get_all_level_actors() if a.get_actor_label()=='LobbyArt_'+label),None)
    if existing:return existing
    return original_spawn(label,*args,**kwargs)
'''
s=s.replace('try:run()',insertion+'\ntry:run()')
exec(compile(s,str(p),'exec'))
