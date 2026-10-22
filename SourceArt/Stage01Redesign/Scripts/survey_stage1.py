"""Read-only survey of Stage1 / Test / StageConfig. Writes Logs/survey_stage1.json."""
import json, unreal
from pathlib import Path
OUT = Path(__file__).resolve().parent.parent / 'Logs'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
res = {}

def v(x): return [round(x.x, 1), round(x.y, 1), round(x.z, 1)]

cfg = unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
c = {}
for p in ['stage_id', 'display_name', 'description', 'level', 'target_capture_count']:
    try: c[p] = str(cfg.get_editor_property(p))
    except Exception as e: c[p] = 'ERR %s' % e
c['defs'] = [{'id': str(d.boar_unique_id), 'cls': str(d.boar_class.get_name() if d.boar_class else None), 'sp': str(d.spawn_point_id)} for d in cfg.get_editor_property('boar_spawn_definitions')]
c['entries'] = [{'cls': str(d.boar_class.get_name() if d.boar_class else None), 'n': d.count} for d in cfg.get_editor_property('boar_spawn_entries')]
c['coins'] = [{'id': str(d.special_coin_id), 'sp': str(d.spawn_point_id)} for d in cfg.get_editor_property('special_coin_definitions')]
res['config'] = c
ar = unreal.AssetRegistryHelpers.get_asset_registry()
res['stage_configs'] = [str(a.package_name) for a in ar.get_assets_by_path('/Game/DataAssets', recursive=True)]

for lvl in ['/Game/Level/Stage1', '/Game/Level/Test']:
    les.load_level(lvl)
    acts = []
    for a in eas.get_all_level_actors():
        d = {'label': a.get_actor_label(), 'cls': a.get_class().get_name(), 'loc': v(a.get_actor_location()),
             'rot': [round(a.get_actor_rotation().yaw, 1)], 'scale': v(a.get_actor_scale3d())}
        o, e = a.get_actor_bounds(False)
        d['ext'] = v(e)
        if isinstance(a, unreal.StaticMeshActor):
            m = a.static_mesh_component.static_mesh
            d['mesh'] = m.get_path_name() if m else None
            d['mats'] = [mm.get_path_name() if mm else None for mm in a.static_mesh_component.get_materials()]
        if isinstance(a, unreal.BoarSpawnPoint):
            d['spid'] = str(a.get_editor_property('spawn_point_id'))
        try: d['folder'] = str(a.get_folder_path())
        except Exception: pass
        acts.append(d)
    ws = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    try: res[lvl + '_gm'] = str(ws.get_editor_property('default_game_mode'))
    except Exception as e: res[lvl + '_gm'] = str(e)
    res[lvl] = acts
(OUT / 'survey_stage1.json').write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding='utf-8')
unreal.log('[Survey] COMPLETE')
