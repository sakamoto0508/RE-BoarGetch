"""Air Net Drop clips (idempotent): imports the Blender FBX set onto the player skeleton and places the NetCaptureWindow notifies.
Creates only new assets under /Game/Animation/Player/AirNetDrop (safe while the editor has other assets open).
Run: run_ue_py.sh AirNetDropSetupAssets.py   -> Saved/AirNetDropSetupAssets.json"""
import unreal, json, traceback
OUT = {}
E = unreal.EditorAssetLibrary
A = unreal.AnimationLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'C:/UnrealGames/REBoarGetch/SourceArt/AirNetDrop/FBX/'
DST = '/Game/Animation/Player/AirNetDrop'
SKEL = '/Game/InportAssets/Player/GreatSwordSlash_UE_Skeleton'
CLIPS = ['Anticipation', 'Swing', 'DropLoop', 'Recovery', 'Land']
# NetCaptureWindow notifies (seconds, enable).  Swing opens when the net passes the front on its way down; the dive hold keeps it
# open; Recovery closes it.  Land opens/closes once at the slam (the C++ starts Land after that point when the window already closed).
NOTIFIES = {
    'Swing': [(0.15, True)],
    'Recovery': [(0.02, False)],
    'Land': [(0.02, True), (0.13, False)],
}


def import_clip(clip):
    name = 'A_Player_AirNetDrop_' + clip
    t = unreal.AssetImportTask()
    t.filename = SRC + name + '.fbx'
    t.destination_path = DST
    t.destination_name = name
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
    anim = unreal.load_asset(DST + '/' + name)
    if not anim:
        return None
    anim.set_editor_property('enable_root_motion', False)
    # notifies: rebuild the NetCapture track from scratch so re-runs stay idempotent
    if A.is_valid_anim_notify_track_name(anim, 'NetCapture'):
        A.remove_animation_notify_events_by_track(anim, 'NetCapture')
    else:
        A.add_animation_notify_track(anim, 'NetCapture')
    for time, enable in NOTIFIES.get(clip, []):
        n = A.add_animation_notify_event(anim, 'NetCapture', time, unreal.AnimNotify_NetCaptureWindow)
        n.set_editor_property('enable_capture_window', enable)
    E.save_loaded_asset(anim, only_if_is_dirty=False)
    return anim


def pose(anim, bone, t):
    tr = A.get_bone_pose_for_time(anim, bone, t, False)
    return [round(tr.translation.x, 2), round(tr.translation.y, 2), round(tr.translation.z, 2)]


try:
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
    for clip in CLIPS:
        anim = import_clip(clip)
        if not anim:
            OUT[clip] = 'IMPORT FAILED'
            continue
        info = {'path': anim.get_path_name(), 'skeleton': anim.get_editor_property('skeleton').get_path_name(),
                'length': round(anim.get_play_length(), 4), 'root_motion': anim.get_editor_property('enable_root_motion'),
                'pelvis_t0': pose(anim, 'pelvis', 0.0), 'hand_r_t0': pose(anim, 'hand_r', 0.0)}
        evs = A.get_animation_notify_events(anim)
        info['notifies'] = [(e.notify.get_class().get_name(), e.notify.get_editor_property('enable_capture_window')) for e in evs if e.notify]
        OUT[clip] = info
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 1')
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'AirNetDropSetupAssets.json', 'w').write(json.dumps(OUT, indent=1))
unreal.log('[ANDS] COMPLETE')
