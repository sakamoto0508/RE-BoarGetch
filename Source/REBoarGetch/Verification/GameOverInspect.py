"""Read-only dump for the GameOver presentation (AKI-75). No saves."""
import unreal
out = []
def log(*a):
    s = ' '.join(str(x) for x in a); out.append(s); unreal.log('[GOI] ' + s)
def tp(o, n):
    try: return o.get_editor_property(n)
    except Exception as e: return '<err %s>' % e
def cdo(path):
    bp = unreal.load_asset(path)
    gen = unreal.load_object(None, path + '_C')
    return bp, unreal.get_default_object(gen) if gen else None

log('=== WBP_GameOver')
log(unreal.BoarUIAuthoringLibrary.describe_widget_blueprint(unreal.load_asset('/Game/BP/Widget/WBP_GameOver')))
bp, d = cdo('/Game/BP/Widget/WBP_GameOver')
for n in ['retry_button_widget_name', 'lobby_button_widget_name']: log(n, tp(d, n))
log('=== WBP HUD')
bp, d = cdo('/Game/BP/Player/BP_BoarPlayerController')
for n in ['player_hud_widget_class', 'game_over_widget_class', 'pause_widget_class']: log(n, tp(d, n))
bp, d = cdo('/Game/BP/GameMode/BP_BoarGameMode')
for n in ['wait_for_stage_end_presentation', 'use_stage_clear_presentation', 'stage_config']: log(n, tp(d, n))
sc = tp(d, 'stage_config')
try: log('stage level', sc.get_editor_property('level'))
except Exception as e: log('lvl err', e)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/REBoarGetch', 'StageConfig')):
    s = a.get_asset(); log('stagecfg', a.package_name, s.stage_id, tp(s, 'level'))
opt = unreal.AssetRegistryDependencyOptions()
for pkg in ['/Game/BP/GameMode/BP_BoarGameMode', '/Game/BP/Widget/WBP_GameOver', '/Game/BP/Player/ABP_Player', '/Game/Animation/Player/ABP_Player']:
    log('referencers', pkg, ar.get_referencers(pkg, opt))
for m in ['/Game/Level/L_Stage01', '/Game/Level/Stage1']:
    log('exists', m, unreal.EditorAssetLibrary.does_asset_exist(m))
for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'World')):
    if 'Stage' in str(a.package_name): log('map', a.package_name)
log('=== ABP_Player graphs')
abp = unreal.load_asset('/Game/BP/Player/ABP_Player')
for n in ['target_skeleton']: log(n, tp(abp, n))
log('=== HUD widget')
for p in ['/Game/BP/Widget/WBP_HUD', '/Game/BP/Widget/WBP_PlayerHUD']:
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        log(unreal.BoarUIAuthoringLibrary.describe_widget_blueprint(unreal.load_asset(p))[:3000])
log('=== Sounds')
for p in ['/Game/InportAssets/Audio/建物が少し崩れる2', '/Game/InportAssets/Audio/電源オン', '/Game/InportAssets/Audio/soundlogo12', '/Game/InportAssets/Audio/soundlogo40', '/Game/InportAssets/Audio/robot01', '/Game/InportAssets/Audio/spaceship']:
    s = unreal.load_asset(p)
    if s: log(p, 'dur', s.get_editor_property('duration'))
log('=== Retro / PP')
for a in ar.get_assets_by_path('/Game/REBoarGetch/Art/VFX', recursive=True):
    log('vfx', a.package_name, a.asset_class_path.asset_name)
for a in ar.get_assets_by_path('/Game/Art', recursive=False):
    log('art', a.package_name)
p = unreal.Paths.project_saved_dir() + 'GameOverInspect.txt'
open(p, 'w', encoding='utf-8').write('\n'.join(out))
unreal.log('[GOI] COMPLETE ' + p)
