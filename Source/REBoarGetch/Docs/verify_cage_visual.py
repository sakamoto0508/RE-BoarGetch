import unreal,json
from pathlib import Path
es=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():raise RuntimeError('PIE active')
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():raise RuntimeError('Unsaved map')
es.load_level('/Game/Level/Test')
out={'actors':[],'assets':[]}
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if not isinstance(a,unreal.Cage):continue
    d={'name':a.get_actor_label(),'path':a.get_path_name(),'class':a.get_class().get_path_name(),'transform':str(a.get_actor_transform()),'components':[]}
    for c in a.get_components_by_class(unreal.SceneComponent):
        row={'name':c.get_name(),'class':c.get_class().get_name(),'location':str(c.get_editor_property('relative_location')),'scale':str(c.get_editor_property('relative_scale3d'))}
        if isinstance(c,unreal.PrimitiveComponent):row['collision']=str(c.get_collision_enabled())
        if isinstance(c,unreal.StaticMeshComponent):
            mesh=c.get_editor_property('static_mesh');row['mesh']=mesh.get_path_name() if mesh else None;row['bounds']=str(mesh.get_bounds()) if mesh else None;row['materials']=[m.get_path_name() if m else None for m in c.get_materials()]
        d['components'].append(row)
    for k in ['max_hp','current_hp','respawn_delay','captured_boar_spacing']:d[k]=a.get_editor_property(k)
    out['actors'].append(d)
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
for folder in ['/Game/BP/Cage','/Game/BP/Widget','/Game/Stage01']:
    out['assets'] += [p for p in assets.list_assets(folder,recursive=True,include_folder=False) if 'Cage' in p or 'Health' in p]
Path(__file__).with_name('audit_cage_visual_after.json').write_text(json.dumps(out,indent=2),encoding='utf-8')

