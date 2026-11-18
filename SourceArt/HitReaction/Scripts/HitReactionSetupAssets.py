"""AKI-163 Red Boar hit reaction: asset setup, idempotent. Run headless after building C++ (editor closed):
  Source/REBoarGetch/Verification/run_ue_py.sh C:/UnrealGames/REBoarGetch/SourceArt/HitReaction/Scripts/HitReactionSetupAssets.py
- Imports SourceArt/HitReaction/Player_HitKnockdown.fbx onto the player skeleton
  -> /Game/Animation/Player/HitReaction/A_Player_HitKnockdown (no root motion; played as a FullBody slot dynamic montage).
- Imports SourceArt/HitReaction/Audio/*.wav -> /Game/REBoarGetch/Art/Audio/Player/.
- Creates /Game/DataAssets/Camera/DA_PlayerHitCameraEffect (ACF Camera Effect Preset: Impulse away from the attacker + short FOV punch).
The Niagara systems are built by PlayerHitNiagara.py.  Writes Saved/HitReactionSetupAssets.json (re-reads every saved value)."""
import unreal, json, traceback
E = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'C:/UnrealGames/REBoarGetch/SourceArt/HitReaction/'
ANIM_DIR = '/Game/Animation/Player/HitReaction'
ANIM = ANIM_DIR + '/A_Player_HitKnockdown'
AUDIO = '/Game/REBoarGetch/Art/Audio/Player'
SKEL = '/Game/InportAssets/Player/GreatSwordSlash_UE_Skeleton'
CAM_DIR = '/Game/DataAssets/Camera'
CAM = CAM_DIR + '/DA_PlayerHitCameraEffect'
SFX = ['SFX_PlayerHit_Impact', 'SFX_PlayerStun_Dizzy']
OUT = {}


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
    t.filename = SRC + 'Player_HitKnockdown.fbx'
    t.destination_path = ANIM_DIR
    t.destination_name = 'A_Player_HitKnockdown'
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
    anim = unreal.load_asset(ANIM)
    assert anim, 'anim import failed'
    anim.set_editor_property('enable_root_motion', False)
    E.save_loaded_asset(anim)
    return {'length': round(anim.get_play_length(), 3), 'skeleton': anim.get_editor_property('skeleton').get_path_name(),
            'root_motion': anim.get_editor_property('enable_root_motion')}


def envelope(attack, sustain, release):
    e = unreal.ACFCameraEffectEnvelope()
    e.set_editor_property('attack', attack)
    e.set_editor_property('sustain', sustain)
    e.set_editor_property('release', release)
    e.set_editor_property('shape', unreal.ACFCameraEnvelopeShape.SMOOTH)
    return e


def make_camera_effect():
    preset = unreal.load_asset(CAM)
    if not preset:
        f = unreal.DataAssetFactory()
        f.set_editor_property('data_asset_class', unreal.ACFCameraEffectPreset)
        preset = tools.create_asset('DA_PlayerHitCameraEffect', CAM_DIR, unreal.ACFCameraEffectPreset, f)
    # 1) Heavy shove: the camera is pushed away from the attacking boar (world direction latched at the hit), one damped bounce.
    imp = unreal.new_object(unreal.ACFCameraEffect_Impulse, outer=preset)
    imp.set_editor_property('envelope', envelope(0.0, 0.05, 0.3))
    imp.set_editor_property('direction_mode', unreal.ACFCameraImpulseDirection.AWAY_FROM_SOURCE)
    imp.set_editor_property('space', unreal.ACFCameraEffectSpace.WORLD)
    imp.set_editor_property('amplitude', 16.0)
    imp.set_editor_property('frequency', 8.0)
    imp.set_editor_property('rotation_amplitude', unreal.Rotator(roll=1.8, pitch=1.4, yaw=0.0))
    # 2) Short FOV punch-in for weight (degrees, additive; returns within ~0.3 s).
    punch = unreal.new_object(unreal.ACFCameraEffect_Punch, outer=preset)
    punch.set_editor_property('envelope', envelope(0.02, 0.04, 0.26))
    punch.set_editor_property('location', unreal.Vector(0.0, 0.0, 0.0))
    punch.set_editor_property('rotation', unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    punch.set_editor_property('fov', -3.0)
    preset.set_editor_property('layers', [imp, punch])
    preset.modify()
    E.save_asset(CAM, only_if_is_dirty=False)


def reread():
    res = {}
    preset = unreal.load_asset(CAM)
    layers = []
    for l in preset.get_editor_property('layers'):
        d = {'class': l.get_class().get_name()}
        env = l.get_editor_property('envelope')
        d['envelope'] = [env.get_editor_property(k) for k in ('attack', 'sustain', 'release')]
        if isinstance(l, unreal.ACFCameraEffect_Impulse):
            d['amp'] = l.get_editor_property('amplitude'); d['freq'] = l.get_editor_property('frequency')
            d['mode'] = str(l.get_editor_property('direction_mode'))
        if isinstance(l, unreal.ACFCameraEffect_Punch):
            d['fov'] = l.get_editor_property('fov')
        layers.append(d)
    res['camera_layers'] = layers
    # reference: the existing capture effect (scale comparison only)
    ref = unreal.load_asset(CAM_DIR + '/DA_CaptureCameraEffect')
    if ref:
        res['capture_ref_layers'] = [l.get_class().get_name() for l in ref.get_editor_property('layers')]
    return res


try:
    OUT['sounds'] = import_sounds()
    OUT['anim'] = import_anim()
    make_camera_effect()
    OUT['reread'] = reread()
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'HitReactionSetupAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[HitReaction] COMPLETE')
