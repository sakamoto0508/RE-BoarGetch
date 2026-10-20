"""Editor (not PIE) check of the Retro Render panel: draws each tab to PNG and verifies that the panel's preset
buttons drive the editor-world component + MPC (viewport preview). Saves no assets."""
from pathlib import Path
import json
import unreal
HERE = Path(__file__).resolve().parent
OUT = HERE / 'RetroCompare2'
LIB = unreal.RetroRenderEditorLibrary
results = {}
state = {'n': 0, 'h': None}

def mpc_value(comp, name):
    # Value the component writes to MPC_Retro (KismetMaterialLibrary is not exposed to Python here).
    return comp.get_effective_parameters().get_editor_property(name) if comp.is_retro_active() else 0.0

def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert les.load_level('/Game/Art/Rendering/Retro/L_RetroCompare')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    comp = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RetroCompareActor)[0].get_editor_property('renderer')
    results['editor_component_ready'] = comp.is_ready()
    tex = [unreal.load_asset(p) for p in ['/Game/Stage01/Ground/GrassBaseColor', '/Game/Stage01/Ground/GrassNormalMap', '/Game/Stage01/Ground/GroundBaseColor']]
    results['look_png'] = LIB.render_retro_panel_to_png(str(OUT / 'Panel_Look_PS1.png'), 0, 1, [], 900, 1250)
    results['after_ps1'] = {'preset': str(comp.get_preset()), 'VertexSnapStrength': mpc_value(comp, 'vertex_snap_strength'),
                            'Pixelation(MID)': comp.get_dynamic_material().get_scalar_parameter_value('Pixelation')}
    results['textures_png'] = LIB.render_retro_panel_to_png(str(OUT / 'Panel_Textures.png'), 1, -1, tex, 900, 900)
    results['meshes_png'] = LIB.render_retro_panel_to_png(str(OUT / 'Panel_Meshes.png'), 2, -1, [], 900, 900)
    LIB.render_retro_panel_to_png(str(OUT / 'Panel_Look_OFF.png'), 0, 0, [], 900, 400)
    results['after_off'] = {'preset': str(comp.get_preset()), 'VertexSnapStrength': mpc_value(comp, 'vertex_snap_strength'),
                            'post_enabled': comp.is_retro_active()}
    (HERE / 'RetroPanelCheck.json').write_text(json.dumps(results, indent=1), encoding='utf-8')
    unreal.log('[RetroPanel] ' + json.dumps(results))

def tick(dt):
    state['n'] += 1
    if state['n'] < 60:
        return
    unreal.unregister_slate_post_tick_callback(state['h'])
    try:
        main()
    except Exception:
        import traceback
        unreal.log_error('[RetroPanel] ' + traceback.format_exc())
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state['h'] = unreal.register_slate_post_tick_callback(tick)
