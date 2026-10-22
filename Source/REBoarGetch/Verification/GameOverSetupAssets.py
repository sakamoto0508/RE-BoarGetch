"""GameOver presentation (AKI-75) asset setup, idempotent. Run headless after building C++:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Verification/GameOverSetupAssets.py
- Imports SourceArt/GameOver/Player_GameOver_Down.fbx onto the player skeleton -> /Game/Animation/Player/A_Player_GameOver_Down
  and creates AM_Player_GameOver_Down (FullBody slot, no auto blend-out so the last pose is held).
- Imports SourceArt/GameOver/Audio/*.wav -> /Game/REBoarGetch/Art/Audio/GameOver/.
- (Presentation settings — montage, camera, SEs, timeline — now live in BP_GameOverPresentation; see PresentationBPSetup.py.)
- BP_BoarGameMode.bUseGameOverPresentation = True (bWaitForStageEndPresentation is left untouched).
- WBP_GameOver: existing Heading restyled (bold red-orange, navy outline, orange shadow) and its slot enlarged to fit.
Writes Saved/GameOverSetupAssets.json (also re-reads every saved value)."""
import unreal, json, re, traceback
E = unreal.EditorAssetLibrary
L = unreal.BoarUIAuthoringLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'C:/UnrealGames/REBoarGetch/SourceArt/GameOver/'
ANIM = '/Game/Animation/Player'
AUDIO = '/Game/REBoarGetch/Art/Audio/GameOver'
SKEL = '/Game/InportAssets/Player/GreatSwordSlash_UE_Skeleton'
OUT = {}
SFX = ['SFX_GameOver_Impact', 'SFX_GameOver_Noise', 'SFX_GameOver_PowerDown', 'SFX_GameOver_BodyFall', 'SFX_GameOver_Jingle']


def import_sounds():
    tasks = []
    for n in SFX:
        t = unreal.AssetImportTask()
        t.filename = SRC + 'Audio/' + n + '.wav'
        t.destination_path = AUDIO
        t.destination_name = n
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    tools.import_asset_tasks(tasks)
    res = {}
    for n in SFX:
        s = unreal.load_asset(AUDIO + '/' + n)
        res[n] = round(s.get_editor_property('duration'), 2) if s else None
    return res


def import_anim():
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
    t = unreal.AssetImportTask()
    t.filename = SRC + 'Player_GameOver_Down.fbx'
    t.destination_path = ANIM
    t.destination_name = 'A_Player_GameOver_Down'
    t.automated = True
    t.replace_existing = True
    t.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.skeleton = unreal.load_asset(SKEL)
    ad = ui.anim_sequence_import_data
    ad.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    ad.set_editor_property('import_bone_tracks', True)
    ad.set_editor_property('remove_redundant_keys', False)
    ad.set_editor_property('use_default_sample_rate', False)
    ad.set_editor_property('custom_sample_rate', 30)
    t.options = ui
    tools.import_asset_tasks([t])
    OUT['anim_import_objects'] = [str(p) for p in t.imported_object_paths]
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 1')
    anim = unreal.load_asset(ANIM + '/A_Player_GameOver_Down')
    assert anim, 'anim import failed'
    anim.set_editor_property('enable_root_motion', False)
    E.save_loaded_asset(anim)
    return anim


def make_montage(anim):
    path = ANIM + '/AM_Player_GameOver_Down'
    mont = unreal.load_asset(path)
    if not mont:
        f = unreal.AnimMontageFactory()
        f.set_editor_property('source_animation', anim)
        mont = tools.create_asset('AM_Player_GameOver_Down', ANIM, unreal.AnimMontage, f)
    tracks = mont.get_editor_property('slot_anim_tracks')
    track = tracks[0]
    track.set_editor_property('slot_name', 'FullBody')
    tracks[0] = track
    mont.set_editor_property('slot_anim_tracks', tracks)
    bi = mont.get_editor_property('blend_in'); bi.set_editor_property('blend_time', .08); mont.set_editor_property('blend_in', bi)
    bo = mont.get_editor_property('blend_out'); bo.set_editor_property('blend_time', .2); mont.set_editor_property('blend_out', bo)
    mont.set_editor_property('enable_auto_blend_out', False)
    mont.modify()
    E.save_asset(path, only_if_is_dirty=False)
    return mont


def style_heading():
    wbp = unreal.load_asset('/Game/BP/Widget/WBP_GameOver')
    before = L.describe_widget_blueprint(wbp)
    OUT['wbp_before'] = before
    ok = L.configure_text_block_style(wbp, 'Heading', 'GAME OVER', 60, 'Bold', unreal.LinearColor(1.0, .2, .05, 1.0), 2,
                                      unreal.LinearColor(.012, .025, .07, 1.0), unreal.Vector2D(0, 4), unreal.LinearColor(1.0, .52, .04, .85))
    OUT['heading_style'] = ok
    m = re.search(r'Heading \[TextBlock\].*?pos=X=([-\d.]+) Y=([-\d.]+) size=X=([-\d.]+) Y=([-\d.]+) auto=(\d)', before)
    OUT['heading_slot_before'] = [float(g) for g in m.groups()[:4]] if m else None
    # 60pt bold + outline/shadow (~76px tall) above the caption; the caption moves down a little so the cyan frame never touches it.
    OUT['heading_slot'] = L.set_canvas_slot_layout(wbp, 'Heading', unreal.Vector2D(40, 14), unreal.Vector2D(560, 90), False)
    OUT['caption_slot'] = L.set_canvas_slot_layout(wbp, 'Caption', unreal.Vector2D(40, 124), unreal.Vector2D(560, 36), False)
    unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
    OUT['wbp_save'] = E.save_loaded_asset(wbp)
    OUT['wbp_after'] = L.describe_widget_blueprint(unreal.load_asset('/Game/BP/Widget/WBP_GameOver'))


def configure_game_mode():
    gm = unreal.load_asset('/Game/BP/GameMode/BP_BoarGameMode')
    gcd = unreal.get_default_object(gm.generated_class())
    gcd.set_editor_property('use_game_over_presentation', True)
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    OUT['gm_save'] = E.save_loaded_asset(gm)


def reread():
    """Reload what was saved and report it (verifies the settings actually persisted)."""
    gm = unreal.get_default_object(unreal.load_asset('/Game/BP/GameMode/BP_BoarGameMode').generated_class())
    OUT['reread_gm'] = {k: gm.get_editor_property(k) for k in ['use_game_over_presentation', 'use_stage_clear_presentation', 'wait_for_stage_end_presentation']}
    mont = unreal.load_asset(ANIM + '/AM_Player_GameOver_Down')
    anim = unreal.load_asset(ANIM + '/A_Player_GameOver_Down')
    OUT['reread_anim'] = {'length': round(anim.get_play_length(), 3), 'skeleton': anim.get_editor_property('skeleton').get_path_name(),
                          'root_motion': anim.get_editor_property('enable_root_motion'),
                          'montage_len': round(mont.get_play_length(), 3), 'slot': str(mont.get_editor_property('slot_anim_tracks')[0].slot_name),
                          'auto_blend_out': mont.get_editor_property('enable_auto_blend_out')}
    OUT['dirty_after'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]


try:
    OUT['sounds'] = import_sounds()
    anim = import_anim()
    mont = make_montage(anim)
    style_heading()
    configure_game_mode()
    reread()
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'GameOverSetupAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, ensure_ascii=False, default=str))
unreal.log('[GOS] COMPLETE')
