"""Read-only export of the in-air locomotion clips for Air Net Drop authoring (Blender)."""
import unreal, os, json, traceback
root = 'C:/UnrealGames/REBoarGetch/SourceArt/AirNetDrop/UEExport/'
os.makedirs(root, exist_ok=True)
OUT = {}
def export(asset_path, filename):
    task = unreal.AssetExportTask()
    task.object = unreal.load_asset(asset_path)
    task.filename = root + filename
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    opt = unreal.FbxExportOption()
    opt.export_morph_targets = False
    opt.level_of_detail = False
    opt.collision = False
    task.options = opt
    OUT[filename] = unreal.Exporter.run_asset_export_task(task)
for n in ['A_Player_FallLoop', 'A_Player_DoubleJump', 'A_Player_JumpStart', 'A_Player_Land']:
    export('/Game/InportAssets/Animation/' + n, n + '.fbx')
try:
    m = unreal.load_asset('/Game/InportAssets/Animation/Montage/AM_Player_NetSwing')
    evs = unreal.AnimationLibrary.get_animation_notify_events(m)
    res = []
    for e in evs:
        d = {'cls': e.notify.get_class().get_name() if e.notify else None}
        for attr in ['trigger_time_offset', 'end_trigger_time_offset', 'link_value', 'notify_name', 'duration']:
            try: d[attr] = str(getattr(e, attr))
            except Exception as ex: d[attr] = 'n/a'
        if e.notify and 'NetCapture' in d['cls']:
            d['enable'] = e.notify.get_editor_property('bEnableCaptureWindow')
        res.append(d)
    OUT['ground_notifies'] = res
    OUT['ground_notify_names'] = [str(x) for x in unreal.AnimationLibrary.get_animation_notify_event_names(m)]
    OUT['dir_events'] = [a for a in dir(evs[0]) if not a.startswith('_')] if evs else []
except Exception:
    OUT['err'] = traceback.format_exc()
open(root + 'export_info.json', 'w').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ANDE] COMPLETE')
