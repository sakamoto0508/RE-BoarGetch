"""Point DA_TestStageConfig.Level at the redesign level (only that field). Logs every field before/after."""
import unreal
DA = '/Game/DataAssets/StageConfig/DA_TestStageConfig'
NEW = '/Game/Level/L_Stage01.L_Stage01'
FIELDS = ['stage_id', 'display_name', 'description', 'level', 'thumbnail', 'preview_actor_class', 'target_capture_count',
          'boar_spawn_entries', 'boar_spawn_definitions', 'special_coin_definitions', 'unlock_condition']


def dump(cfg, tag):
    for f in FIELDS:
        v = cfg.get_editor_property(f)
        if f == 'boar_spawn_definitions':
            v = [(str(d.boar_unique_id), str(d.spawn_point_id)) for d in v]
        unreal.log('[S01] %s %s=%s' % (tag, f, v))


cfg = unreal.load_asset(DA)
dump(cfg, 'before')
world = unreal.load_asset(NEW)
assert world, NEW
cfg.set_editor_property('level', world)
assert unreal.EditorAssetLibrary.save_loaded_asset(cfg, False)
dump(cfg, 'after')
unreal.log('[S01] COMPLETE')
