"""AKI-175: Stage 2 gameplay connection (headless commandlet; the editor must be closed).
- creates /Game/DataAssets/StageConfig/DA_Stage02StageConfig (reused if it exists)
- assigns it to BP_GM_Stage02 (StageConfig was None)
- adds 4 BP_BoarSpawnPoint actors to L_Stage02 (replaces existing ones with the same SpawnPointId only)
Does NOT touch Stage 1 config, other maps, AI or tuning values. Writes Logs/setup_gameplay.json."""
import json, math, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import s02_layout as L
ROOT = os.path.dirname(HERE)
LEVEL = '/Game/Level/L_Stage02'
CFG_DIR, CFG_NAME = '/Game/DataAssets/StageConfig', 'DA_Stage02StageConfig'
GM = '/Game/BP/GameMode/BP_GM_Stage02'
CAPSULE_CLEAR = 60.0     # boar capsule half height 56.3 cm: the point is the capsule centre, just above the ground

# (BoarUniqueId, BoarClass, SpawnPointId, (x, y), yaw source)  -- see README "Boar Spawn"
BOARS = [
    ('Stage02_Boar_001', '/Game/BP/Boar/BP_NormalBoar', 'Stage02_Spawn_001', (1300.0, -3600.0)),   # west hills (+390)
    ('Stage02_Boar_002', '/Game/BP/Boar/BP_NormalBoar', 'Stage02_Spawn_002', (-1700.0, -1700.0)),  # south-west middle (+22)
    ('Stage02_Boar_003', '/Game/BP/Boar/BP_RedBoar', 'Stage02_Spawn_003', (900.0, 2700.0)),        # east terrace meadow (+257)
    ('Stage02_Boar_004', '/Game/BP/Boar/BP_BlueBoar', 'Stage02_Spawn_004', (3250.0, 2800.0)),      # north-east ridge (+673)
]
R = {}


def log(m):
    unreal.log_warning('[S02Gameplay] ' + m)


# ---------------------------------------------------------------- StageConfig
cfg_path = CFG_DIR + '/' + CFG_NAME
cfg = unreal.load_asset(cfg_path) if unreal.EditorAssetLibrary.does_asset_exist(cfg_path) else None
if cfg is None:
    fac = unreal.DataAssetFactory()
    fac.set_editor_property('data_asset_class', unreal.StageConfig)
    cfg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(CFG_NAME, CFG_DIR, unreal.StageConfig, fac)
    R['config_created'] = True
assert isinstance(cfg, unreal.StageConfig), cfg
stage1 = unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
idle = stage1.get_editor_property('boar_spawn_definitions')[0].get_editor_property('archive_idle_animation')   # same as Stage 1
cfg.set_editor_property('stage_id', 'Stage02')
cfg.set_editor_property('display_name', 'Stage 2')
cfg.set_editor_property('description', '4体を捕獲しよう')
cfg.set_editor_property('level', unreal.load_asset(LEVEL))
cfg.set_editor_property('target_capture_count', 4)
defs = []
for bid, cls, pid, _ in BOARS:
    d = unreal.BoarSpawnDefinition()
    d.set_editor_property('boar_unique_id', bid)
    d.set_editor_property('boar_class', unreal.EditorAssetLibrary.load_blueprint_class(cls))
    d.set_editor_property('spawn_point_id', pid)
    d.set_editor_property('archive_idle_animation', idle)
    defs.append(d)
cfg.set_editor_property('boar_spawn_definitions', defs)
assert unreal.EditorAssetLibrary.save_loaded_asset(cfg, False), 'save config'
R['config'] = cfg.get_path_name()

# ---------------------------------------------------------------- GameMode
gm_bp = unreal.load_asset(GM)
gm_cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(GM))
R['gm_config_before'] = str(gm_cdo.get_editor_property('stage_config'))
gm_cdo.set_editor_property('stage_config', cfg)
unreal.BlueprintEditorLibrary.compile_blueprint(gm_bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(gm_bp, False), 'save GM'
gm_cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(GM))
R['gm_config_after'] = gm_cdo.get_editor_property('stage_config').get_path_name()

# ---------------------------------------------------------------- Level spawn points
w = unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
assert w.get_path_name().startswith(LEVEL), w.get_path_name()
vs = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)   # also makes the terrain collision ready for traces
st = unreal.ValidateAssetsSettings(); st.set_editor_property('skip_excluded_directories', False)
vs.validate_assets_with_settings([unreal.EditorAssetLibrary.find_asset_data(LEVEL)], st)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sp_cls = unreal.EditorAssetLibrary.load_blueprint_class('/Game/BP/Boar/BP_BoarSpawnPoint')
wanted = {b[2] for b in BOARS}
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.BoarBase):
        raise RuntimeError('placed boar found: ' + a.get_actor_label())     # would double-spawn / break IDs
    if isinstance(a, unreal.BoarSpawnPoint) and str(a.get_editor_property('spawn_point_id')) in wanted:
        log('replacing ' + a.get_actor_label()); eas.destroy_actor(a)
R['points'] = {}
for bid, cls, pid, (x, y) in BOARS:
    hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(x, y, 3000), unreal.Vector(x, y, -1000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    assert hit, 'no ground at %s' % pid
    gz = hit.to_tuple()[4].z
    yaw = math.degrees(math.atan2(L.CAGE[1] - y, L.CAGE[0] - x))      # face the valley / Cage
    a = eas.spawn_actor_from_class(sp_cls, unreal.Vector(x, y, gz + CAPSULE_CLEAR), unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    assert a, pid
    a.set_editor_property('spawn_point_id', pid)
    a.set_actor_label(pid)
    a.set_folder_path('Stage02/Gameplay/BoarSpawn')
    l = a.get_actor_location()
    R['points'][pid] = {'boar': bid, 'class': cls, 'loc': [round(l.x), round(l.y), round(l.z)], 'ground': round(gz, 1), 'yaw': round(yaw, 1)}
assert unreal.EditorLoadingAndSavingUtils.save_packages([w.get_outermost()], False), 'save level'
json.dump(R, open(os.path.join(ROOT, 'Logs', 'setup_gameplay.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
log(json.dumps(R, ensure_ascii=False))
log('COMPLETE')
