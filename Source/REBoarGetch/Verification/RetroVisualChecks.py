"""Additional real-PIE HUD/UI screenshots and stationary TAA/TSR comparison."""
from pathlib import Path
import json
import time
import traceback
import unreal

HERE=Path(__file__).resolve().parent
OUT=HERE/'RetroVisualChecks'
OUT.mkdir(exist_ok=True)
SHOTS=HERE.parent.parent.parent/'Saved/Screenshots/Retro'
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
step='load'; busy=False; handle=None; wait=0; index=0
deadline=time.monotonic()+180
data={'checks':[],'images':{}}
tests=[('TAA_1',2),('TAA_2',2),('TSR_1',4),('TSR_2',4)]

def check(name,condition=True):
    if not condition:raise AssertionError(name)
    data['checks'].append(name)

def command(text):unreal.SystemLibrary.execute_console_command(world,text)

def finish(error=None):
    global handle
    if handle is not None:unreal.unregister_slate_post_tick_callback(handle);handle=None
    if 'original_aa' in globals():command('r.AntiAliasingMethod '+str(original_aa))
    if editor.is_in_play_in_editor():editor.editor_request_end_play()
    data['error']=error
    (OUT/'Results.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    if error:unreal.log_error('[RetroVisual] '+error)
    else:unreal.log('[RetroVisual] COMPLETE')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    global busy,step,wait,world,pc,harness,renderer,index,before,original_aa,handle
    if busy:return
    busy=True
    try:
        if time.monotonic()>deadline:raise TimeoutError(step)
        if step=='load':
            check('Comparison level loads',editor.load_level('/Game/Art/Rendering/Retro/L_RetroCompare'))
            editor.editor_request_begin_play();step='begin'
        elif step=='begin':
            if not editor.is_in_play_in_editor():return
            world=worlds.get_game_world()
            if not world:return
            pc=unreal.GameplayStatics.get_player_controller(world,0)
            if not pc or not pc.get_controlled_pawn():return
            harness=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RetroCompareActor)[0]
            renderer=harness.get_editor_property('renderer')
            harness.set_debug_viewport_size(1280,720)
            original_aa=unreal.SystemLibrary.get_console_variable_int_value('r.AntiAliasingMethod')
            data['original_aa']=original_aa
            hud=unreal.WidgetLibrary.get_all_widgets_of_class(world,unreal.BoarHUDWidget,True)
            check('Existing HUD present',any(w.is_in_viewport() for w in hud))
            renderer.set_preset(unreal.RetroRenderPreset.PS1)
            harness.toggle_debug_ui();wait=30;step='ui_wait'
        elif step=='ui_wait':
            wait-=1
            if wait>0:return
            command('Shot showui filename='+str(OUT/'DebugUI.png')+' -nosuffix')
            wait=30;step='ui_shot'
        elif step=='ui_shot':
            wait-=1
            if wait>0:return
            data['images']['DebugUI']=str(OUT/'DebugUI.png')
            harness.toggle_debug_ui()
            command('Shot showui filename='+str(OUT/'HUD_PS1.png')+' -nosuffix')
            wait=30;step='hud_shot'
        elif step=='hud_shot':
            wait-=1
            if wait>0:return
            data['images']['HUD_PS1']=str(OUT/'HUD_PS1.png')
            unreal.GameplayStatics.set_game_paused(world,True)
            step='aa_start'
        elif step=='aa_start':
            if index>=len(tests):finish();return
            name,aa=tests[index]
            command('r.AntiAliasingMethod '+str(aa))
            wait=90;step='aa_wait'
        elif step=='aa_wait':
            wait-=1
            if wait>0:return
            before=set(SHOTS.glob('*.png'))
            harness.capture_current();check('Capture started '+tests[index][0],harness.is_capturing())
            step='aa_capture'
        elif step=='aa_capture':
            if harness.is_capturing():return
            files=set(SHOTS.glob('*.png'))-before
            check('AA image saved '+tests[index][0],len(files)==1)
            target=OUT/('PS1_'+tests[index][0]+'.png')
            target.write_bytes(next(iter(files)).read_bytes())
            data['images'][tests[index][0]]=str(target)
            index+=1;step='aa_start'
    except Exception:finish(traceback.format_exc())
    finally:busy=False

handle=unreal.register_slate_post_tick_callback(tick)
