import unreal,json,re
from pathlib import Path
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
bp=unreal.load_asset('/Game/BP/Cage/BP_Cage')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
assert assets.save_loaded_asset(bp)
actors=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.Cage)]
out=[]
for a in actors:
    rows=[]
    for c in a.get_components_by_class(unreal.SceneComponent):
        p=c.get_attach_parent()
        rows.append({'name':c.get_name(),'parent':p.get_path_name() if p else None,'world':str(c.get_world_location()),'collision':str(c.get_collision_enabled()) if isinstance(c,unreal.PrimitiveComponent) else None})
    out.append({'actor':a.get_path_name(),'components':rows})
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
unreal.log('CAGE_FINAL_SAVED '+str(len(out)))
