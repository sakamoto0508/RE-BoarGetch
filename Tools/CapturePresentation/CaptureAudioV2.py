"""AKI-166 capture audio import and clean-process verification (no PIE)."""
import json, os
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
dest = '/Game/REBoarGetch/Art/Audio/Capture/AKI166'
waves = {
    'capture_impact_v2': ('SFX_CaptureV2_Impact', .155),
    'capture_spiral_v2': ('SFX_CaptureV2_Spiral', 1.),
    'capture_confirm_v2': ('SFX_CaptureV2_Confirm', .32),
    'capture_multi_v2': ('SFX_CaptureV2_Multi', .41),
}
blueprints = ('/Game/BP/Player/BP_BoarPlayerController', '/Game/BP/Lobby/BP_PC_Lobby')
legacy = ('impact_sound', 'transfer_start_sound', 'capture_success_sound', 'multi_capture_sound',
          'camera_presentation', 'teleport_vfx', 'hit_stop_duration', 'slow_duration',
          'capture_dissolve_duration', 'transfer_finish_duration', 'teleport_lead_time')
source = Path(unreal.Paths.project_dir()).resolve() / 'SourceArt/Capture/Audio/AKI166'
step = os.environ.get('AKI166_STEP', 'verify')
report = {'step': step, 'waves': {}, 'controllers': {}}

def component(path):
    blueprint = unreal.load_asset(path)
    return blueprint, unreal.get_default_object(blueprint.generated_class()).get_component_by_class(unreal.CapturePresentationComponent)

def references(comp):
    out = {}
    for key in legacy:
        v = comp.get_editor_property(key)
        out[key] = v.get_path_name() if isinstance(v, unreal.Object) else str(v)
    return out

if step in ('apply', 'wire'):
    for path in blueprints:
        _, c = component(path)
        for key in waves:
            c.get_editor_property(key) # Check the new reflected fields before writing.
if step in ('apply', 'import'):
    for name, _ in waves.values():
        assert not assets.does_asset_exist(dest + '/' + name), name
    for key, (name, _) in waves.items():
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(source / (name + '.wav')))
        task.set_editor_property('destination_path', dest)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', False)
        task.set_editor_property('save', False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        wave = unreal.load_asset(dest + '/' + name)
        assert isinstance(wave, unreal.SoundWave), name
        wave.set_editor_property('looping', key == 'capture_spiral_v2')
        wave.set_editor_property('override_concurrency', True)
        settings = wave.get_editor_property('concurrency_overrides')
        settings.set_editor_property('max_count', 1)
        settings.set_editor_property('resolution_rule', unreal.MaxConcurrentResolutionRule.STOP_OLDEST)
        wave.set_editor_property('concurrency_overrides', settings)
        assert assets.save_loaded_asset(wave, only_if_is_dirty=False), name
if step in ('apply', 'wire'):
    for path in blueprints:
        bp, c = component(path)
        before = references(c)
        for key, (name, _) in waves.items():
            c.set_editor_property(key, unreal.load_asset(dest + '/' + name))
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        _, c = component(path)
        assert references(c) == before, path
        assert assets.save_loaded_asset(bp, only_if_is_dirty=False), path

for key, (name, duration) in waves.items():
    wave = unreal.load_asset(dest + '/' + name)
    assert isinstance(wave, unreal.SoundWave), name
    assert abs(wave.get_editor_property('duration') - duration) < .005, name
    assert wave.get_editor_property('looping') == (key == 'capture_spiral_v2'), name
    assert wave.get_editor_property('override_concurrency'), name
    settings = wave.get_editor_property('concurrency_overrides')
    assert settings.get_editor_property('max_count') == 1, name
    assert settings.get_editor_property('resolution_rule') == unreal.MaxConcurrentResolutionRule.STOP_OLDEST, name
    report['waves'][name] = dict(path=wave.get_path_name(), duration=wave.get_editor_property('duration'),
                                 looping=wave.get_editor_property('looping'), max_count=1)

if step in ('apply', 'wire', 'verify'):
    for path in blueprints:
        bp, c = component(path)
        status = str(bp.get_editor_property('status'))
        assert 'BS_UP_TO_DATE' in status, (path, status)
        report['controllers'][path] = {'status': status, 'existing': references(c)}
        for key, (name, _) in waves.items():
            wave = c.get_editor_property(key)
            assert isinstance(wave, unreal.SoundWave) and wave.get_path_name() == report['waves'][name]['path'], (path, key)
            report['controllers'][path][key] = wave.get_path_name()

with open(Path(os.environ['TEMP']) / ('aki166_' + step + '.json'), 'w', encoding='utf-8') as f:
    json.dump(report, f, indent=2, ensure_ascii=False)
unreal.log('AKI-166 ' + step + ' OK')


