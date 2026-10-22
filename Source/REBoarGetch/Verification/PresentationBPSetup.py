"""Builds the presentation Blueprints (idempotent) and wires them to BP_BoarPlayerController. Run headless after building C++:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Verification/PresentationBPSetup.py
- /Game/BP/Presentation/BP_StageClearPresentation (parent AStageClearPresentationDirector)
- /Game/BP/Presentation/BP_GameOverPresentation   (parent AGameOverPresentationDirector)
Class defaults are migrated from Saved/PresentationSettingsBefore.json (the values that were on the old C++ components,
verified by the user in the editor). The Event Graph reproduces the previous C++ timeline with Delay nodes; asset choices
(SE / jingle) are pin values in the graph. Writes Saved/PresentationBPSetup.json (incl. the re-read graphs and defaults)."""
import unreal, json, traceback
TS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
E = unreal.EditorAssetLibrary
FOLDER = '/Game/BP/Presentation'
OUT = {}
OLD = json.load(open(unreal.Paths.project_saved_dir() + 'PresentationSettingsBefore.json', encoding='utf-8'))
SC_OLD, GO_OLD = OLD['StageClearPresentationComponent'], OLD['GameOverPresentationComponent']


def call(tool, args):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    err = r.get_editor_property('error')
    val = r.get_value_as_json_string() or ''
    if err:
        raise RuntimeError('%s: %s' % (tool, err[:3000]))
    data = json.loads(val) if val else None
    return json.loads(data) if isinstance(data, str) else data


def ensure_bp(name, parent):
    path = FOLDER + '/' + name
    if not E.does_asset_exist(path):
        f = unreal.BlueprintFactory()
        f.set_editor_property('parent_class', parent)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Blueprint, f)
    return unreal.load_asset(path), path + '.' + name


def obj(p):
    return unreal.load_asset(p.split('.')[0]) if p else None


def lc(v):
    return unreal.LinearColor(*v)


def vec(v):
    return unreal.Vector(*v)


SND = {
    'jingle': SC_OLD['victory_jingle'], 'jump': SC_OLD['jump_sound'], 'star': SC_OLD['star_sound'],
    'transfer_start': SC_OLD['transfer_start_sound'], 'transfer_complete': SC_OLD['transfer_complete_sound'],
    'impact': GO_OLD['impact_sound'], 'noise': GO_OLD['noise_sound'], 'power_down': GO_OLD['system_down_sound'],
    'body_fall': GO_OLD['body_fall_sound'], 'defeat': GO_OLD['defeat_jingle'],
}


def f(v):
    return '%.3f' % v


def timeline(event, steps):
    """steps: list of ('call', dsl) / ('comment', text) / ('delay', seconds). Delay is a sequential latent node."""
    lines = []
    for kind, val in steps:
        if kind == 'delay':
            lines.append('  (Utilities|FlowControl|Delay :Duration %s)' % f(val))
        else:
            lines.append('  ' + ('; ' + val if kind == 'comment' else val))
    return '(event %s\n%s)\n' % (event, '\n'.join(lines))


# The previous C++ timeline, now owned by the Blueprint (absolute times are reproduced exactly; see GameOverPresentation.md).
SC = SC_OLD
STAGE_CLEAR_DSL = timeline('Presentation|EventOnPresentationStarted', [
    ('comment', '勝利カメラへBlend・HUD非表示・ステージBGMフェード'),
    ('call', '(StageClearPresentation|BeginVictoryShot)'),
    ('call', '(Presentation|Audio|StopStageBGM :FadeOutTime %s)' % f(SC['stage_bgm_fade_out_time'])),
    ('delay', SC['montage_start_delay']),
    ('comment', 'Victory Montage と勝利ジングル（BGMチャンネル）'),
    ('call', '(StageClearPresentation|PlayVictoryMontage)'),
    ('call', '(Presentation|Audio|PlayPresentationBGM :Music "%s" :FadeInTime 0.0)' % SND['jingle']),
    ('delay', SC['jump_sound_time']),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['jump']),
    ('delay', SC['star_burst_time'] - SC['jump_sound_time']),
    ('comment', '網を最も高く掲げた瞬間：星VFX'),
    ('call', '(StageClearPresentation|SpawnStarBurst)'),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['star']),
    ('delay', SC['glow_start_time'] - SC['star_burst_time']),
    ('call', '(StageClearPresentation|BeginGlow :RampDuration %s)' % f(SC['dissolve_start_time'] - SC['glow_start_time'])),
    ('delay', SC['dissolve_start_time'] - SC['glow_start_time']),
    ('comment', 'データ化して消失'),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['transfer_start']),
    ('call', '(StageClearPresentation|BeginDissolve :Duration %s)' % f(SC['dissolve_duration'])),
    ('delay', SC['dissolve_duration']),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['transfer_complete']),
    ('delay', SC['retreat_delay']),
    ('comment', '注視点を見たまま後退'),
    ('call', '(StageClearPresentation|BeginCameraRetreat :Duration %s :ParticleFadeDuration %s)' % (f(SC['retreat_duration']), f(SC['particle_fade_duration']))),
    ('delay', SC['retreat_duration']),
    ('call', '(StageClearPresentation|BeginScreenFadeOut :Duration %s)' % f(SC['fade_out_duration'])),
    ('delay', SC['fade_out_duration']),
    ('comment', '完了報告 → C++がResultへ進める'),
    ('call', '(Presentation|FinishPresentation)'),
])

GO = GO_OLD
nb = GO['noise_burst_times']
cam_start = GO['camera_start_time']
body = GO['down_start_time'] + GO['body_impact_anim_time']
finish = max(GO['down_start_time'] + 1.4, cam_start + GO['camera_duration']) + GO['ui_delay_after_settle']
GAME_OVER_DSL = timeline('Presentation|EventOnPresentationStarted', [
    ('comment', '0秒：赤フラッシュ・重い低音SE・BGMフェード・短いヒットストップ・Boarの跳ね返り'),
    ('call', '(GameOverPresentation|BeginRedFlash :PeakAlpha %s :Duration %s)' % (f(GO['flash_peak_alpha']), f(GO['flash_duration']))),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s" :Pitch %s)' % (SND['impact'], f(GO['impact_pitch']))),
    ('call', '(Presentation|Audio|StopStageBGM :FadeOutTime %s)' % f(GO['bgm_fade_out_time'])),
    ('call', '(GameOverPresentation|BeginHitStop :Duration %s :TimeScale %s)' % (f(GO['hit_stop_duration']), f(GO['hit_stop_time_scale']))),
    ('call', '(GameOverPresentation|SettlePlayeronGround)'),
    ('call', '(GameOverPresentation|KnockBackNearbyBoars :Radius %s :Distance %s :HopHeight %s :Duration %s)' % (
        f(GO['knockback_radius']), f(GO['knockback_distance']), f(GO['knockback_hop_height']), f(GO['knockback_duration']))),
    ('delay', nb[0]),
    ('call', '(GameOverPresentation|PlayNoiseBurst :Duration %s :Intensity %s)' % (f(GO['noise_burst_duration']), f(GO['noise_intensity']))),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['noise']),
    ('delay', GO['down_start_time'] - nb[0]),
    ('comment', 'ダウン（上体が反る→よろける→尻もち→仰向け）'),
    ('call', '(GameOverPresentation|PlayDownAnimation)'),
    ('delay', GO['gadget_glow_start_time'] - GO['down_start_time']),
    ('call', '(GameOverPresentation|BeginGadgetGlowDecay :DecayTime %s)' % f(GO['gadget_glow_decay_time'])),
    ('call', '(GameOverPresentation|FadeOutHUD :Duration %s)' % f(GO['hud_fade_out_time'])),
    ('delay', GO['warning_start_time'] - GO['gadget_glow_start_time']),
    ('call', '(GameOverPresentation|BeginWarning :FadeInTime %s)' % f(GO['warning_fade_in_time'])),
    ('delay', GO['system_down_sound_time'] - GO['warning_start_time']),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['power_down']),
    ('delay', cam_start - GO['system_down_sound_time']),
    ('comment', '既存SpringArmで少し上昇→倒れた身体を中央に捉えて後退'),
    ('call', '(GameOverPresentation|BeginCameraPullBack :Duration %s :RiseDuration %s)' % (f(GO['camera_duration']), f(GO['camera_rise_duration']))),
    ('delay', body - cam_start),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['body_fall']),
    ('call', '(GameOverPresentation|PlayNoiseBurst :Duration %s :Intensity %s)' % (f(GO['noise_burst_duration']), f(GO['noise_intensity'] * .7))),
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['noise']),
    ('delay', finish - body),
    ('comment', 'ダウン完了・カメラ停止＋余韻 → C++がGameOver UIへ進める'),
    ('call', '(Presentation|FinishPresentation)'),
]) + timeline('GameOverPresentation|EventOnGameOverUIShown', [
    ('call', '(Presentation|Audio|PlayPresentationSound :Sound "%s")' % SND['defeat']),
    ('call', '(GameOverPresentation|SetWarningAlphaforUI :Alpha %s)' % f(GO['warning_alpha_during_ui'])),
])


def write_graph(bp_ref, code):
    graph = {'refPath': bp_ref + ':EventGraph'}
    call('write_graph_dsl', {'graph': graph, 'code': code})
    return call('read_graph_dsl', {'graph': graph})


try:
    OUT['timeline_check'] = {'go_body_fall_at': round(body, 3), 'go_finish_at': round(finish, 3), 'go_noise_times': nb}
    # ---------------- BP_StageClearPresentation ----------------
    sc_bp, sc_ref = ensure_bp('BP_StageClearPresentation', unreal.StageClearPresentationDirector)
    d = unreal.get_default_object(sc_bp.generated_class())
    for k in ['camera_distance', 'camera_height', 'camera_yaw_offset', 'camera_blend_time', 'victory_fov', 'retreat_end_fov',
              'retreat_distance', 'retreat_height', 'retreat_ease_exponent', 'montage_play_rate', 'allow_single_node_fallback',
              'fallback_jump_height', 'star_scale', 'fragment_count', 'fragment_rise_speed']:
        d.set_editor_property(k, SC[k])
    d.set_editor_property('victory_focus_offset', vec(SC['victory_focus_offset']))
    d.set_editor_property('star_offset', vec(SC['star_offset']))
    d.set_editor_property('data_color', lc(SC['data_color']))
    d.set_editor_property('star_colors', [lc(c) for c in SC['star_colors']])
    d.set_editor_property('fallback_jump_delay', SC['jump_sound_time'])
    for k in ['victory_montage', 'victory_animation', 'star_vfx', 'data_material', 'glow_material', 'transfer_vfx', 'retreat_curve']:
        d.set_editor_property(k, obj(SC[k]))
    OUT['stage_clear_graph'] = write_graph(sc_ref, STAGE_CLEAR_DSL)
    unreal.BlueprintEditorLibrary.compile_blueprint(sc_bp)
    OUT['stage_clear_save'] = E.save_loaded_asset(sc_bp)

    # ---------------- BP_GameOverPresentation ----------------
    go_bp, go_ref = ensure_bp('BP_GameOverPresentation', unreal.GameOverPresentationDirector)
    d = unreal.get_default_object(go_bp.generated_class())
    for k in ['down_play_rate', 'allow_single_node_fallback', 'camera_end_pitch', 'camera_yaw_drift', 'pull_back_distance',
              'focus_interp_speed', 'camera_end_fov', 'camera_ease_exponent', 'vignette_alpha']:
        d.set_editor_property(k, GO[k])
    d.set_editor_property('focus_offset', vec(GO['focus_offset']))
    d.set_editor_property('gadget_glow_color', lc(GO['gadget_glow_color']))
    d.set_editor_property('camera_probe_channel', unreal.CollisionChannel.ECC_VISIBILITY)
    for k in ['down_montage', 'down_animation', 'gadget_glow_material', 'camera_curve']:
        d.set_editor_property(k, obj(GO[k]))
    OUT['game_over_graph'] = write_graph(go_ref, GAME_OVER_DSL)
    unreal.BlueprintEditorLibrary.compile_blueprint(go_bp)
    OUT['game_over_save'] = E.save_loaded_asset(go_bp)

    # ---------------- BP_BoarPlayerController: control layer → presentation BPs ----------------
    pc = unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController')
    cdo = unreal.get_default_object(pc.generated_class())
    c = cdo.get_component_by_class(unreal.StageClearPresentationComponent)
    c.set_editor_property('director_class', sc_bp.generated_class())
    c.set_editor_property('capture_wait_timeout', SC['capture_wait_timeout'])
    c.set_editor_property('safety_timeout', SC['safety_timeout'])
    c.set_editor_property('enabled', SC['enabled'])
    g = cdo.get_component_by_class(unreal.GameOverPresentationComponent)
    g.set_editor_property('director_class', go_bp.generated_class())
    g.set_editor_property('safety_timeout', GO['safety_timeout'])
    g.set_editor_property('enabled', GO['enabled'])
    unreal.BlueprintEditorLibrary.compile_blueprint(pc)
    OUT['pc_save'] = E.save_loaded_asset(pc)

    # ---------------- re-read from disk ----------------
    for name in ['BP_StageClearPresentation', 'BP_GameOverPresentation']:
        bp = unreal.load_asset(FOLDER + '/' + name)
        dd = unreal.get_default_object(bp.generated_class())
        OUT['reread_' + name] = {'parent': str(bp.get_editor_property('parent_class')) if hasattr(bp, 'parent_class') else '',
                                 'graph': call('read_graph_dsl', {'graph': {'refPath': FOLDER + '/' + name + '.' + name + ':EventGraph'}})}
        keys = ['victory_montage', 'star_vfx', 'data_material', 'glow_material', 'transfer_vfx', 'camera_distance', 'retreat_distance', 'victory_fov'] \
            if 'Stage' in name else ['down_montage', 'down_animation', 'gadget_glow_material', 'pull_back_distance', 'camera_end_pitch', 'camera_probe_channel']
        OUT['reread_' + name]['defaults'] = {k: str(dd.get_editor_property(k)) for k in keys}
    cdo = unreal.get_default_object(unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController').generated_class())
    OUT['reread_pc'] = {'stage_clear_director': str(cdo.get_component_by_class(unreal.StageClearPresentationComponent).get_editor_property('director_class')),
                        'game_over_director': str(cdo.get_component_by_class(unreal.GameOverPresentationComponent).get_editor_property('director_class'))}
    OUT['dirty_after'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'PresentationBPSetup.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, ensure_ascii=False, default=str))
unreal.log('[PBS] COMPLETE')
