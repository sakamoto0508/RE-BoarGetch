"""AKI-165: 捕獲カメラ演出を1つの DataAsset（DA_CaptureCameraPresentation）にまとめ、CapturePresentation に接続する。
回転（Spiral Orbit）・揺れ（Camera Effect Layer）・壁際（Collision）・時間と Blend を1か所で調整できる。
回転・時間・揺れの初期値はこのスクリプトの定数、Collision は通常 TPS（DA_BoarCamera_ThirdPerson）から写す。
Usage: UnrealEditor-Cmd <uproject> -run=pythonscript -script="<this file>"   (CCP_STEP=apply|verify)
結果: Saved/CaptureCameraPresentationAsset_<step>.json"""
import unreal, json, os, traceback
E = unreal.EditorAssetLibrary
DIR = '/Game/DataAssets/Camera'
PATH = DIR + '/DA_CaptureCameraPresentation'
STEP = os.environ.get('CCP_STEP', 'apply')
OUT = {'step': STEP}
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/ActionCameraFramework'], True)

ORBIT_KEYS = ['focus_offset', 'focus_damping_time', 'start_mode', 'start_angle_offset', 'rotation_degrees', 'direction', 'duration',
              'radius_start', 'radius_end', 'height_start', 'height_end', 'angle_curve', 'angle_ease_exponent', 'height_curve',
              'radius_curve', 'look_at_offset', 'override_field_of_view', 'field_of_view']
COLLISION_KEYS = ['enabled', 'deoccluder', 'fallback_origin_offset', 'framing_recovery', 'ignore_controlled_pawn', 'ignore_look_at_target_actor',
                  'ignore_pivot_actor', 'min_distance', 'probe_channel', 'probe_radius', 'pull_in_damping_time', 'return_damping_time']
EFFECT_KEYS = {
    unreal.ACFCameraEffect_Punch: ['enabled', 'weight', 'priority', 'blend_mode', 'envelope', 'distance_attenuation', 'space', 'location', 'rotation', 'fov'],
    unreal.ACFCameraEffect_Impulse: ['enabled', 'weight', 'priority', 'blend_mode', 'envelope', 'distance_attenuation', 'space', 'amplitude',
                                     'rotation_amplitude', 'frequency', 'direction_mode', 'fixed_direction'],
}


def text(v):
    if isinstance(v, unreal.Object):
        return v.get_path_name()
    try:
        return v.export_text()
    except Exception:
        return str(v)


def copy(src, dst, keys):
    for k in keys:
        try:
            dst.set_editor_property(k, src.get_editor_property(k))
        except Exception as ex:
            OUT.setdefault('copy_skipped', []).append('%s.%s: %s' % (src.get_class().get_name(), k, ex))


ORBIT = dict(focus_offset=unreal.Vector(0, 0, 0), focus_damping_time=.12, start_mode=unreal.ACFCameraSpiralStart.CONTROL_ROTATION,
             start_angle_offset=0., rotation_degrees=360., direction=1, duration=1.55, radius_start=300., radius_end=340.,
             height_start=0., height_end=280., angle_ease_exponent=1.6, look_at_offset=unreal.Vector(0, 0, 10),
             override_field_of_view=True, field_of_view=70.)


def blend(curve, duration):
    b = unreal.ACFCameraBlendSettings()
    b.set_editor_property('curve', curve)
    b.set_editor_property('duration', duration)
    b.set_editor_property('exponent', 2.0)
    b.set_editor_property('source', unreal.ACFCameraBlendSource.LIVE)
    return b


def envelope(attack, sustain, release):
    e = unreal.ACFCameraEffectEnvelope()
    e.set_editor_property('attack', attack)
    e.set_editor_property('sustain', sustain)
    e.set_editor_property('release', release)
    return e


def node(preset_path, cls):
    return [n for n in unreal.load_asset(preset_path).get_editor_property('nodes') if isinstance(n, cls)][0]


def apply():
    if E.does_asset_exist(PATH):
        da = unreal.load_asset(PATH)
    else:
        f = unreal.DataAssetFactory()
        f.set_editor_property('data_asset_class', unreal.CaptureCameraPresentationData)
        da = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_CaptureCameraPresentation', DIR, unreal.CaptureCameraPresentationData, f)
    orbit = da.get_editor_property('orbit')
    for k, v in ORBIT.items():
        orbit.set_editor_property(k, v)
    copy(node(DIR + '/DA_BoarCamera_ThirdPerson', unreal.ACFCameraNode_Collision), da.get_editor_property('collision'), COLLISION_KEYS)
    da.set_editor_property('hold_after_orbit', .1)
    da.set_editor_property('blend_in', blend(unreal.ACFCameraBlendCurve.EASE_OUT, .15))
    da.set_editor_property('return_blend', blend(unreal.ACFCameraBlendCurve.EASE_IN_OUT, .45))
    da.set_editor_property('priority', 900)
    # 揺れ：Punch（一瞬の寄りと頷き）+ Impulse（Boar から押し返す減衰振動）。
    punch = unreal.new_object(unreal.ACFCameraEffect_Punch, outer=da)
    punch.set_editor_property('fov', -6.)
    punch.set_editor_property('rotation', unreal.Rotator(roll=0., pitch=-1.2, yaw=0.))
    punch.set_editor_property('envelope', envelope(.02, .05, .35))
    impulse = unreal.new_object(unreal.ACFCameraEffect_Impulse, outer=da)
    impulse.set_editor_property('amplitude', 14.)
    impulse.set_editor_property('rotation_amplitude', unreal.Rotator(roll=1.5, pitch=1.8, yaw=.8))
    impulse.set_editor_property('frequency', 13.)
    impulse.set_editor_property('envelope', envelope(0., .05, .55))
    att = impulse.get_editor_property('distance_attenuation')
    att.set_editor_property('enabled', False)
    impulse.set_editor_property('distance_attenuation', att)
    da.set_editor_property('shake_layers', [punch, impulse])
    da.set_editor_property('shake_scale', 1.)
    E.save_loaded_asset(da, only_if_is_dirty=False)
    for bp_path in ['/Game/BP/Player/BP_BoarPlayerController', '/Game/BP/Lobby/BP_PC_Lobby']:
        bp = unreal.load_asset(bp_path)
        c = unreal.get_default_object(bp.generated_class()).get_component_by_class(unreal.CapturePresentationComponent)
        c.set_editor_property('camera_presentation', da)
        # 調整先を1つにするため、個別の Sequence は外す（DataAsset が無いときだけ使う Fallback）。
        c.set_editor_property('focus_sequence', None)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert E.save_loaded_asset(bp, only_if_is_dirty=False), bp_path


def report():
    da = unreal.load_asset(PATH)
    o = da.get_editor_property('orbit')
    OUT['orbit'] = {k: text(o.get_editor_property(k)) for k in ORBIT_KEYS}
    OUT['collision'] = {k: text(da.get_editor_property('collision').get_editor_property(k)) for k in ['enabled', 'probe_radius', 'min_distance', 'probe_channel']}
    OUT['timing'] = {k: text(da.get_editor_property(k)) for k in ['hold_after_orbit', 'blend_in', 'return_blend', 'priority', 'shake_scale']}
    OUT['shake_layers'] = [dict(cls=l.get_class().get_name(), **{k: text(l.get_editor_property(k)) for k in EFFECT_KEYS.get(type(l), [])})
                           for l in da.get_editor_property('shake_layers')]
    OUT['blueprints'] = []
    for bp_path in ['/Game/BP/Player/BP_BoarPlayerController', '/Game/BP/Lobby/BP_PC_Lobby']:
        bp = unreal.load_asset(bp_path)
        c = unreal.get_default_object(bp.generated_class()).get_component_by_class(unreal.CapturePresentationComponent)
        OUT['blueprints'].append({'path': bp_path, 'status': str(bp.get_editor_property('status')),
                                  'camera_presentation': text(c.get_editor_property('camera_presentation')),
                                  'focus_sequence': text(c.get_editor_property('focus_sequence'))})
    OUT['dirty'] = [x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]


try:
    if STEP == 'apply':
        apply()
    report()
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'CaptureCameraPresentationAsset_%s.json' % STEP, 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, ensure_ascii=False))
unreal.log('[CCP] COMPLETE')
