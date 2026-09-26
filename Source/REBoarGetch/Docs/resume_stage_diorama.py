from pathlib import Path
p=Path(__file__).parent/'create_stage_diorama.py'
s=p.read_text(encoding='utf-8')
s=s.replace('try:run()', '''
original_create=create
def create(path,cls,factory):
    if AS.does_asset_exist(path):
        a=unreal.load_asset(path)
        if isinstance(a,unreal.Material):ML.delete_all_material_expressions(a)
        return a
    return original_create(path,cls,factory)
try:run()''')
exec(compile(s,str(p),'exec'))
