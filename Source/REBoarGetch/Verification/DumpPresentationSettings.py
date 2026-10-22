"""Read-only: dumps every editable property of the StageClear/GameOver presentation components on BP_BoarPlayerController
(the values the user verified in the editor) to Saved/PresentationSettingsBefore.json, for migration to the presentation BPs."""
import unreal, json
bp = unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController')
cdo = unreal.get_default_object(bp.generated_class())
out = {}
for cls in [unreal.StageClearPresentationComponent, unreal.GameOverPresentationComponent]:
    c = cdo.get_component_by_class(cls)
    vals = {}
    for name in dir(c):
        if name.startswith('_'):
            continue
        try:
            v = c.get_editor_property(name)
        except Exception:
            continue
        if callable(v):
            continue
        if isinstance(v, unreal.Object):
            v = v.get_path_name()
        elif isinstance(v, (unreal.LinearColor, unreal.Vector)):
            v = v.to_tuple()
        elif isinstance(v, unreal.Array):
            v = [x.to_tuple() if hasattr(x, 'to_tuple') else (x.get_path_name() if isinstance(x, unreal.Object) else x) for x in v]
        elif not isinstance(v, (int, float, bool, str)) and v is not None:
            v = str(v)
        vals[name] = v
    out[cls.__name__] = vals
open(unreal.Paths.project_saved_dir() + 'PresentationSettingsBefore.json', 'w', encoding='utf-8').write(json.dumps(out, indent=1, ensure_ascii=False))
unreal.log('[DPS] COMPLETE')
