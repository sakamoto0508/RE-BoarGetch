"""UE5.8 -ExecutePythonScript=.../RetroPieSmoke.py; renders real PIE, exits when done.
Writes only verification results/screenshots; never saves gameplay or existing assets.
"""
from pathlib import Path
import hashlib
import json
import statistics
import time
import traceback
import unreal

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent.parent
ROOT = '/Game/Art/Rendering/Retro'
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
results = {'checks': [], 'gpu': {}, 'audit': {}}
step = 'load'
deadline = time.monotonic()+300
handle = None
busy = False
wait_frames = 0
mode_index = 0
samples = []
resize_index = 0
resize_sizes = [(1024,768),(1600,900)]
MODES = [unreal.RetroRenderPreset.OFF, unreal.RetroRenderPreset.PS1,
         unreal.RetroRenderPreset.N64, unreal.RetroRenderPreset.PS2, unreal.RetroRenderPreset.CRT]
NAMES = ['OFF','PS1','N64','PS2','CRT']
old_screenshots = set((PROJECT/'Saved/Screenshots/Retro').glob('*.png'))

def saves():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (PROJECT/'Saved/SaveGames').glob('*.sav')}
initial_saves = saves()

def record(text, condition=True):
    if not condition:
        raise AssertionError(text)
    results['checks'].append(text)
    unreal.log('[RetroPIE] PASS '+text)

def widgets(world, cls):
    return unreal.WidgetLibrary.get_all_widgets_of_class(world, cls, False)

def finish(error=None):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    results['error'] = error
    results['save_files_unchanged'] = initial_saves == saves()
    results['screenshots'] = [str(p) for p in sorted(set((PROJECT/'Saved/Screenshots/Retro').glob('*.png'))-old_screenshots)]
    (HERE/'RetroPieResults.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    if editor.is_in_play_in_editor(): editor.editor_request_end_play()
    if error: unreal.log_error('[RetroPIE] '+error)
    else: unreal.log('[RetroPIE] COMPLETE')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    global step, deadline, wait_frames, harness, renderer, world, pc, mid, mode_index, samples, resize_index, busy
    if busy: return
    busy = True
    try:
        if time.monotonic()>deadline: raise TimeoutError(step)
        if step=='load':
            record('UE5.8', unreal.SystemLibrary.get_engine_version().startswith('5.8'))
            record('Comparison level loads', editor.load_level(ROOT+'/L_RetroCompare'))
            actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
            pp = [a for a in actors if isinstance(a, unreal.PostProcessVolume)]
            results['audit']['post_process_volumes'] = [
                {'name': a.get_name(), 'priority': a.get_editor_property('priority'),
                 'unbound': a.get_editor_property('unbound')} for a in pp]
            results['audit']['cameras'] = [a.get_name() for a in actors if a.get_component_by_class(unreal.CameraComponent)]
            registry = unreal.AssetRegistryHelpers.get_asset_registry()
            contexts = registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/EnhancedInput','InputMappingContext'), True)
            results['audit']['input'] = {}
            for data in contexts:
                ctx = data.get_asset()
                mappings=list(ctx.get_editor_property('default_key_mappings').get_editor_property('mappings'))
                for override in ctx.get_editor_property('mapping_profile_overrides').values():
                    mappings.extend(override.get_editor_property('mappings'))
                results['audit']['input'][ctx.get_path_name()] = [m.get_editor_property('key').export_text() for m in mappings]
            record('F7 has no project mapping',not any('F7' in key for keys in results['audit']['input'].values() for key in keys))
            material = unreal.load_asset(ROOT+'/M_RetroCompare')
            record('After Tonemapping location', material.get_editor_property('blendable_location') == unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
            errors = unreal.MaterialEditingLibrary.recompile_material(material)
            record('Material compilation', not errors)
            results['material_statistics'] = str(unreal.MaterialEditingLibrary.get_statistics(material))
            editor.editor_request_begin_play()
            step='begin'; deadline=time.monotonic()+300
        elif step=='begin':
            if not editor.is_in_play_in_editor(): return
            world = worlds.get_game_world()
            if not world: return
            pc = unreal.GameplayStatics.get_player_controller(world,0)
            if not pc or not pc.get_controlled_pawn(): return
            found = unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RetroCompareActor)
            record('Single opt-in harness', len(found)==1)
            harness=found[0]; renderer=harness.get_editor_property('renderer')
            harness.set_debug_viewport_size(1280,720)
            record('MID initialized', renderer.is_ready())
            mid=renderer.get_dynamic_material()
            for mode in MODES:
                renderer.set_preset(mode)
                record('Switch '+str(mode),renderer.get_preset()==mode)
            for _ in range(50):
                for mode in MODES: renderer.set_preset(mode)
            record('250 switches retain same MID',renderer.get_dynamic_material()==mid)
            renderer.set_preset(unreal.RetroRenderPreset.PS1)
            renderer.set_parameter('Pixelation',.42)
            record('Runtime parameter reflected in MID',abs(mid.get_scalar_parameter_value('Pixelation')-.42)<.001)
            renderer.reset_current_preset()
            record('Reset restores PS1',abs(renderer.get_parameters().get_editor_property('pixelation')-1)<.001)
            renderer.set_preset(unreal.RetroRenderPreset.OFF)
            pp=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RetroCompareActor)[0].get_components_by_class(unreal.PostProcessComponent)
            record('OFF disables custom post process', len(pp)==1 and not pp[0].get_editor_property('enabled'))
            renderer.set_preset(unreal.RetroRenderPreset.PS1)
            harness.toggle_debug_ui(); step='ui'; wait_frames=10
        elif step=='ui':
            wait_frames-=1
            if wait_frames>0: return
            ui=next((w for w in widgets(world,unreal.RetroDebugWidget) if w.is_in_viewport()),None)
            record('UMG opens',ui is not None)
            record('UMG has 13 parameter rows',len(widgets(world,unreal.RetroParameterRow))==13)
            for name in ['Off','PS1','N64','PS2','CRT']: ui.call_method(name)
            record('UMG preset route changes renderer',renderer.get_preset()==unreal.RetroRenderPreset.CRT)
            rows=widgets(world,unreal.RetroParameterRow)
            ui.call_method('PS1')
            fields=[('Pixelation','pixelation'),('TargetVerticalResolution','target_vertical_resolution'),
                ('ColorQuantization','color_quantization'),('DitheringStrength','dithering_strength'),
                ('Saturation','saturation'),('Contrast','contrast'),('Brightness','brightness'),
                ('ScanlineStrength','scanline_strength'),('VignetteStrength','vignette_strength'),
                ('ChromaticAberration','chromatic_aberration'),('CRTCurvature','crt_curvature'),
                ('SoftSampling','soft_sampling'),('PhosphorMask','phosphor_mask')]
            before=renderer.get_parameters()
            rows[0].call_method('Changed',args=(.7,))
            after=renderer.get_parameters()
            changed=[(name,attr) for name,attr in fields if abs(before.get_editor_property(attr)-after.get_editor_property(attr))>.001]
            record('Slider callback changes one parameter',len(changed)==1)
            name,attr=changed[0]
            record('Slider callback updates MID immediately',abs(mid.get_scalar_parameter_value(name)-after.get_editor_property(attr))<.001)
            record('Slider callback keeps same MID',renderer.get_dynamic_material()==mid)
            ui.call_method('Reset')
            harness.toggle_debug_ui()
            record('Close releases move/look input',not pc.is_move_input_ignored() and not pc.is_look_input_ignored())
            record('Close restores hidden cursor',not pc.get_editor_property('show_mouse_cursor'))
            step='capture_delay'; wait_frames=45
        elif step=='capture_delay':
            wait_frames-=1
            if wait_frames>0: return
            harness.capture_all(); record('Five-image comparison begins',harness.is_capturing())
            step='capture'; deadline=time.monotonic()+180
        elif step=='capture':
            if harness.is_capturing(): return
            files=set((PROJECT/'Saved/Screenshots/Retro').glob('*.png'))-old_screenshots
            record('Five comparison images saved',len(files)==5)
            for name in NAMES: record('Image '+name,any(p.stem.endswith('_'+name) for p in files))
            unreal.GameplayStatics.set_game_paused(world,True)
            unreal.SystemLibrary.execute_console_command(world,'r.VSync 0')
            unreal.SystemLibrary.execute_console_command(world,'t.MaxFPS 0')
            unreal.SystemLibrary.execute_console_command(world,'r.ProfileGPU.ShowUI 0')
            renderer.set_preset(MODES[0]); step='gpu_warmup'; wait_frames=90
            deadline=time.monotonic()+300
        elif step=='gpu_warmup':
            wait_frames-=1
            if wait_frames>0:return
            samples=[]; step='gpu_sample'; wait_frames=120
        elif step=='gpu_sample':
            value=harness.get_gpu_frame_milliseconds()
            if value>0: samples.append(value)
            wait_frames-=1
            if wait_frames>0:return
            results['gpu'][NAMES[mode_index]] = {'samples':len(samples),
                'mean_ms':statistics.mean(samples) if samples else None,
                'median_ms':statistics.median(samples) if samples else None}
            unreal.log('[RetroPIE] GPU '+NAMES[mode_index]+' '+str(results['gpu'][NAMES[mode_index]]))
            unreal.log('[RetroProfile] '+NAMES[mode_index])
            unreal.SystemLibrary.execute_console_command(world,'profilegpu')
            step='gpu_profile_wait';wait_frames=15
        elif step=='gpu_profile_wait':
            wait_frames-=1
            if wait_frames>0:return
            mode_index+=1
            if mode_index==len(MODES):
                unreal.GameplayStatics.set_game_paused(world,False)
                renderer.set_preset(unreal.RetroRenderPreset.OFF)
                record('MID stable after all checks',renderer.get_dynamic_material()==mid)
                step='resize_start'
            else:
                renderer.set_preset(MODES[mode_index]); step='gpu_warmup'; wait_frames=90
        elif step=='resize_start':
            if resize_index>=len(resize_sizes): finish(); return
            width,height=resize_sizes[resize_index]
            harness.set_debug_viewport_size(width,height)
            wait_frames=60;step='resize_warmup'
        elif step=='resize_warmup':
            wait_frames-=1
            if wait_frames>0:return
            size=harness.get_debug_viewport_size()
            record('PIE viewport resized '+str(resize_sizes[resize_index]),(size.x,size.y)==resize_sizes[resize_index])
            harness.capture_all();step='resize_capture'
        elif step=='resize_capture':
            if harness.is_capturing():return
            record('Comparison captured at requested '+str(resize_sizes[resize_index]))
            resize_index+=1;step='resize_start'
    except Exception:
        finish(traceback.format_exc())
    finally:
        busy = False

handle = unreal.register_slate_post_tick_callback(tick)
unreal.log('[RetroPIE] Scheduled real-rendering PIE checks')
