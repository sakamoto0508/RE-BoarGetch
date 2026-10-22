"""Idempotent wiring of the stage clear presentation into existing Blueprints (run headless after building C++).
- WBP_Result: STAGE CLEAR! / capture line / coin line text + UMG reveal animations + reveal SEs.
- BP_BoarPlayerController: StageClearPresentation component assets, timings, SEs.
- BP_BoarGameMode: bUseStageClearPresentation (explicit).
Writes Saved/StageClearConfigureBP.json"""
import unreal, json, traceback
E = unreal.EditorAssetLibrary
L = unreal.BoarUIAuthoringLibrary
OUT = {}
SND = '/Game/InportAssets/Audio/'


def snd(n):
    s = unreal.load_asset(SND + n)
    assert s, n
    return s


def hidden(wbp_path, name):
    w = unreal.load_object(None, wbp_path + '.WBP_Result:WidgetTree.' + name)
    if w:
        w.set_editor_property('visibility', unreal.SlateVisibility.COLLAPSED)
    return bool(w)


try:
    # ---------------- WBP_Result ----------------
    WBP = '/Game/BP/Widget/WBP_Result'
    wbp = unreal.load_asset(WBP)
    ok = {}
    ok['Text_StageClear'] = L.ensure_text_block(wbp, 'Text_StageClear', 'VerticalBox_Result', -1, 'Text_StageClear',
                                                'STAGE CLEAR!', 58, unreal.LinearColor(1.0, .78, .08, 1))
    ok['Text_CaptureLine'] = L.ensure_text_block(wbp, 'Text_CaptureLine', 'VerticalBox_Result', 1, 'Text_CapturedCount',
                                                 'ボア捕獲数 0 / 0', 34, unreal.LinearColor(.91, .97, 1, 1))
    ok['Text_CoinLine'] = L.ensure_text_block(wbp, 'Text_CoinLine', 'VerticalBox_Result', 2, 'SpecialCoinCountValue',
                                              '特別コイン +0', 30, unreal.LinearColor(1, .84, .22, 1))
    # The two new lines replace the old split capture row and the coin row (their TextBlocks stay for the C++ bindings).
    for n in ['CaptureSummaryLabel', 'HorizontalBox_CaptureCount', 'SpecialCoinCountRow']:
        ok['collapse_' + n] = hidden(WBP, n)
    ok['Anim_StageClear'] = L.ensure_reveal_animation(wbp, 'Anim_StageClear', 'Text_StageClear', .55, 1.7, 0.)
    ok['Anim_CaptureCount'] = L.ensure_reveal_animation(wbp, 'Anim_CaptureCount', 'Text_CaptureLine', .35, 1.25, 26.)
    ok['Anim_SpecialCoin'] = L.ensure_reveal_animation(wbp, 'Anim_SpecialCoin', 'Text_CoinLine', .35, 1.25, 26.)
    unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
    cdo = unreal.get_default_object(wbp.generated_class())
    cdo.set_editor_property('stage_clear_sound', snd('電源オン'))
    cdo.set_editor_property('capture_count_sound', snd('選択5'))
    cdo.set_editor_property('special_coin_sound', snd('button31'))
    cdo.set_editor_property('reveal_step_interval', .35)
    OUT['wbp_ok'] = ok
    OUT['wbp_save'] = E.save_loaded_asset(wbp)
    OUT['wbp_tree'] = L.describe_widget_blueprint(wbp)

    # ---------------- BP_BoarPlayerController ----------------
    bp = unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController')
    cd = unreal.get_default_object(bp.generated_class())
    c = cd.get_component_by_class(unreal.StageClearPresentationComponent)
    assert c, 'StageClearPresentation component missing (rebuild C++)'
    cap = '/Game/REBoarGetch/Art/VFX/Capture/'
    sc = '/Game/REBoarGetch/Art/VFX/StageClear/'
    c.set_editor_property('victory_montage', unreal.load_asset('/Game/Animation/Player/AM_Player_Victory_NetRaise'))
    c.set_editor_property('star_vfx', unreal.load_asset(sc + 'NS_StageClearStars'))
    c.set_editor_property('data_material', unreal.load_asset(cap + 'M_CaptureDataTransfer'))
    c.set_editor_property('glow_material', unreal.load_asset(cap + 'M_CaptureGlow'))
    c.set_editor_property('transfer_vfx', unreal.load_asset(cap + 'NS_CaptureSuccess'))
    c.set_editor_property('victory_jingle', snd('soundlogo12'))
    c.set_editor_property('jump_sound', snd('回避__2_'))
    c.set_editor_property('star_sound', snd('パワーアップ'))
    c.set_editor_property('transfer_start_sound', snd('Cyber21-4__mp3cut_net_'))
    c.set_editor_property('transfer_complete_sound', snd('Cyber15-4__mp3cut_net_'))
    # Timeline matched to A_Player_Victory_NetRaise (takeoff 0.27s, apex 0.54s, landing 0.80s, settled 1.18s, 1.9s long).
    for k, v in {'montage_start_delay': .25, 'jump_sound_time': .2, 'star_burst_time': .56, 'glow_start_time': 1.2,
                 'dissolve_start_time': 1.45, 'dissolve_duration': .75, 'retreat_delay': .05, 'retreat_duration': 2.1,
                 'retreat_distance': 400., 'retreat_height': 100., 'victory_fov': 55., 'retreat_end_fov': 58.,
                 'particle_fade_duration': .7, 'fade_out_duration': .6, 'camera_blend_time': .5}.items():
        c.set_editor_property(k, v)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    OUT['pc_save'] = E.save_loaded_asset(bp)
    cd = unreal.get_default_object(bp.generated_class())
    c = cd.get_component_by_class(unreal.StageClearPresentationComponent)
    OUT['pc_component'] = {k: str(c.get_editor_property(k)) for k in ['victory_montage', 'star_vfx', 'data_material', 'glow_material',
                                                                       'victory_jingle', 'jump_sound', 'star_sound', 'transfer_start_sound',
                                                                       'transfer_complete_sound', 'retreat_duration', 'retreat_distance',
                                                                       'retreat_height', 'victory_fov']}
    # ---------------- BP_BoarGameMode ----------------
    gm = unreal.load_asset('/Game/BP/GameMode/BP_BoarGameMode')
    gcd = unreal.get_default_object(gm.generated_class())
    gcd.set_editor_property('use_stage_clear_presentation', True)
    OUT['gm_wait_bp_presentation'] = gcd.get_editor_property('wait_for_stage_end_presentation')
    OUT['gm_save'] = E.save_loaded_asset(gm)
    OUT['dirty'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'StageClearConfigureBP.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, ensure_ascii=False))
unreal.log('[SCB] COMPLETE')
