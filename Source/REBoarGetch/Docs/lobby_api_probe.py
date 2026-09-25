import unreal,json
from pathlib import Path
out={}
for cls, names in [(unreal.MaterialEditingLibrary,['create_material_expression','connect_material_expressions','connect_material_property','set_material_instance_parent','set_material_instance_scalar_parameter_value']), (unreal.EditorActorSubsystem,['spawn_actor_from_class']), (unreal.StaticMeshComponent,['set_collision_enabled'])]:
    for n in names: out[n]=getattr(cls,n).__doc__
for p in ['/Game/Lobby/Materials/MI_Lobby_Ivory','/Game/InportAssets/LPRiverForest/Meshes/Trees/SM_LPTree01','/Game/InportAssets/LPRiverForest/Meshes/Plants/SM_LPBush01','/Game/InportAssets/LPRiverForest/Meshes/Rocks/SM_LPRock01']:
    a=unreal.load_asset(p)
    out[p]=str(a.get_bounds()) if a and isinstance(a,unreal.StaticMesh) else str(a)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2))
