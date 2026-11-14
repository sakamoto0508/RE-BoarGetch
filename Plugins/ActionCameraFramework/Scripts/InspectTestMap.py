"""Read-only: dumps light / sky / post-process actors of L_Lobby (known good) and L_ACFCameraTest for comparison
to Saved/ACFVerification/InspectTestMap.json."""
import unreal, json

PROPS = ['mobility', 'intensity', 'visible', 'atmosphere_sun_light', 'real_time_capture', 'source_type', 'cast_shadows',
         'light_color', 'affects_world']
CLASSES = (unreal.LightComponentBase, unreal.SkyAtmosphereComponent, unreal.ExponentialHeightFogComponent, unreal.PostProcessComponent)
OUT = {}
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for path in ['/Game/Level/L_Lobby', '/ActionCameraFramework/Samples/L_ACFCameraTest']:
    level.load_level(path)
    rows = []
    for a in sub.get_all_level_actors():
        cls = a.get_class().get_name()
        if not any(k in cls for k in ('Light', 'Sky', 'Fog', 'PostProcess', 'Atmosphere', 'Cloud', 'Exposure')):
            continue
        row = {'label': a.get_actor_label(), 'class': cls, 'rot': str(a.get_actor_rotation())}
        for comp in a.get_components_by_class(unreal.ActorComponent):
            if isinstance(comp, CLASSES):
                for p in PROPS:
                    try:
                        row[comp.get_class().get_name() + '.' + p] = str(comp.get_editor_property(p))
                    except Exception:
                        pass
        if isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property('settings')
            row['unbound'] = a.get_editor_property('unbound')
            for p in ['override_auto_exposure_method', 'auto_exposure_method', 'override_auto_exposure_bias', 'auto_exposure_bias',
                      'override_auto_exposure_min_brightness', 'auto_exposure_min_brightness', 'override_auto_exposure_max_brightness', 'auto_exposure_max_brightness']:
                try:
                    row['pp.' + p] = str(s.get_editor_property(p))
                except Exception:
                    pass
        rows.append(row)
    ws = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    OUT[path] = {'actors': rows, 'game_mode': str(ws.get_editor_property('default_game_mode'))}
open(unreal.Paths.project_saved_dir() + 'ACFVerification/InspectTestMap.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ACF] COMPLETE')
