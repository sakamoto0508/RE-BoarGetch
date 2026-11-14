"""Fixes the lighting of the existing L_ACFCameraTest in place (geometry untouched), using the same
setup_lighting() as CreateManualTestAssets.py. Writes Saved/ACFVerification/FixTestMapLighting.json."""
import unreal, json, os, sys, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
OUT = {}
try:
    import importlib.util
    spec = importlib.util.spec_from_file_location('acf_manual', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'CreateManualTestAssets.py'))
    src = open(spec.origin, encoding='utf-8').read().split('\ntry:\n    presets()')[0]  # definitions only
    ns = {'__name__': 'acf_manual'}
    exec(compile(src, spec.origin, 'exec'), ns)
    ns['OUT'] = OUT
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    OUT['loaded'] = level.load_level('/ActionCameraFramework/Samples/L_ACFCameraTest')
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ns['setup_lighting'](sub)
    OUT['saved'] = level.save_current_level()
    OUT['lights'] = [(a.get_actor_label(), str(a.get_actor_rotation())) for a in sub.get_all_level_actors() if 'Light' in a.get_class().get_name()]
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'ACFVerification/FixTestMapLighting.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
