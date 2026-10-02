"""Run in UE Editor using -ExecutePythonScript; no user save writes or asset saves."""
import hashlib
import json
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

HERE = Path(__file__).resolve().parent
SAVE_DIR = HERE.parent.parent.parent / 'Saved' / 'SaveGames'
LEVELS = ['/Game/Level/Stage1', '/Game/Level/L_GadgetLab', '/Game/Level/L_GadgetTest_Net', '/Game/Level/L_Archive']
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
results = []
level_index = 0
step = 'start'
deadline = time.monotonic() + 90
next_tick = 0
handle = None

def disk_saves():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SAVE_DIR.glob('*.sav')}

initial_saves = disk_saves()

def record(label, condition=True):
    if not condition:
        raise AssertionError(label)
    results.append(label)
    unreal.log('[RefactorPIE] PASS: ' + label)

def prop(obj, name):
    return obj.get_editor_property(name)

def save_snapshot(instance):
    save = instance.get_progress()
    fields = ['CapturedBoarHistory', 'ArchiveDisplayBoarIds', 'bHasSavedArchiveDisplays',
              'CapturedBoarUniqueIds', 'SpecialCoinIds', 'ClearedStageIds',
              'UnlockedGadgetIds', 'GadgetLoadout', 'LastAttemptedStageId', 'bHasSavedLoadout']
    return {field: str(prop(save, field)) for field in fields}

def open_terminal(world, pawn, action):
    terminals = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.HubPortal)
    terminal = next((t for t in terminals if prop(t, 'Action') == action), None)
    if terminal is None:
        raise AssertionError('Existing terminal not found: ' + str(action))
    # Exercise the existing overlap handler on the PIE-only actor without travel or asset edits.
    old = prop(terminal, 'bRequiresInteraction')
    terminal.set_editor_property('bRequiresInteraction', False)
    try:
        terminal.call_method('Enter', args=(None, pawn, None, 0, False, unreal.HitResult()))
    finally:
        terminal.set_editor_property('bRequiresInteraction', old)

def widget(world, cls):
    found = unreal.WidgetLibrary.get_all_widgets_of_class(world, cls, True)
    return next((w for w in found if w.is_in_viewport()), None)

def check_level(world, pc):
    instance = unreal.GameplayStatics.get_game_instance(world)
    pawn = pc.get_controlled_pawn()
    component = pawn.get_component_by_class(unreal.GadgetComponent)
    flow = pc.get_component_by_class(unreal.BoarUIFlowComponent)
    record('UI Flow component created', flow is not None)
    record('Gadget component available', component is not None)
    before = save_snapshot(instance)
    if level_index == 0:
        mode = unreal.GameplayStatics.get_game_mode(world)
        record('Stage1 reaches Playing', mode.get_stage_state() == unreal.BoarStageState.PLAYING)
        if any(component.get_gadget_slot_class(i) for i in range(4)):
            record('Initial gadget equipped after stage preparation', component.get_current_gadget() is not None)
        pc.open_pause_menu()
        record('Pause opens and stops game time', pc.is_pause_menu_open() and unreal.GameplayStatics.is_game_paused(world))
        record('Pause owns movement/look locks', pc.is_move_input_ignored() and pc.is_look_input_ignored())
        pause = widget(world, unreal.BoarPauseWidget)
        pc.open_loadout_menu()
        loadout = widget(world, unreal.BoarLoadoutWidget)
        record('Pause loadout constructed', loadout is not None)
        loadout.call_method('ChooseSlot', args=(2,))
        pc.call_method('HandleMenuBack')
        record('Back from loadout retains Pause', widget(world, unreal.BoarLoadoutWidget) is None and pc.is_pause_menu_open())
        pc.open_settings_menu(pause, None)
        record('Settings child opens', widget(world, unreal.BoarSettingsWidget) is not None)
        pc.call_method('HandleMenuBack')
        record('Settings child closes to Pause', widget(world, unreal.BoarSettingsWidget) is None and pc.is_pause_menu_open())
        pc.leave_stage_from_pause()
        record('Leave-stage confirmation opens', widget(world, unreal.BoarConfirmationWidget) is not None)
        pc.call_method('HandleMenuBack')
        record('Cancel confirmation keeps stage', widget(world, unreal.BoarConfirmationWidget) is None and pc.is_pause_menu_open())
        pc.resume_from_pause()
        record('Resume restores time and owned locks', not pc.is_pause_menu_open() and not unreal.GameplayStatics.is_game_paused(world)
               and not pc.is_move_input_ignored() and not pc.is_look_input_ignored())
    elif level_index == 1:
        open_terminal(world, pawn, unreal.HubPortalAction.LOADOUT)
        loadout = widget(world, unreal.BoarLoadoutWidget)
        record('Lab terminal opens loadout', loadout is not None)
        loadout.call_method('ChooseLabMode', args=(2,))
        loadout.call_method('ChooseCandidate', args=(1,))
        record('Lab selects trial independently of slots', str(instance.get_selected_test_gadget()) == 'Net')
        loadout.call_method('ChooseCandidate', args=(0,))
        record('Lab clears trial selection', str(instance.get_selected_test_gadget()) == 'None')
        pc.call_method('HandleMenuBack')
        record('Lab menu closes and releases input', widget(world, unreal.BoarLoadoutWidget) is None and not pc.is_move_input_ignored())
    elif level_index == 2:
        trial = component.get_gadget_slot_class(0)
        record('Test level provides trial in first slot', trial is not None and all(component.get_gadget_slot_class(i) is None for i in range(1, 4)))
        record('Temporary trial can move slots', component.set_gadget_slot(2, trial))
        record('Temporary move clears original slot', component.get_gadget_slot_class(0) is None and component.get_gadget_slot_class(2) == trial)
        record('Temporary trial can clear slot', component.set_gadget_slot(2, None))
    else:
        open_terminal(world, pawn, unreal.HubPortalAction.ARCHIVE)
        record('Archive terminal opens encyclopedia', widget(world, unreal.BoarEncyclopediaWidget) is not None)
        pc.call_method('HandleMenuBack')
        record('Archive menu closes and releases input', widget(world, unreal.BoarEncyclopediaWidget) is None and not pc.is_move_input_ignored())
    record(LEVELS[level_index] + ': persistent progress unchanged', before == save_snapshot(instance))

def finish(error=None):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    if editor.is_in_play_in_editor():
        editor.editor_request_end_play()
    data = {'passed': results, 'error': error, 'save_files_unchanged': initial_saves == disk_saves()}
    (HERE / 'PieSmokeResults.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    if error:
        unreal.log_error('[RefactorPIE] ' + error)
    else:
        unreal.log('[RefactorPIE] COMPLETE: ' + str(len(results)) + ' checks')
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    global level_index, step, deadline, next_tick
    if time.monotonic() < next_tick:
        return
    next_tick = time.monotonic() + .5
    try:
        if time.monotonic() > deadline:
            raise TimeoutError('PIE state timeout: ' + step)
        if step == 'start':
            if editor.is_in_play_in_editor():
                return
            if not editor.load_level(LEVELS[level_index]):
                raise RuntimeError('Failed to load ' + LEVELS[level_index])
            editor.editor_request_begin_play()
            step = 'check'
            deadline = time.monotonic() + 90
        elif step == 'check':
            if not editor.is_in_play_in_editor():
                return
            world = worlds.get_game_world()
            if world is None:
                return
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            if pc is None or pc.get_controlled_pawn() is None:
                return
            check_level(world, pc)
            editor.editor_request_end_play()
            step = 'stop'
        else:
            if editor.is_in_play_in_editor():
                return
            level_index += 1
            if level_index < len(LEVELS):
                step = 'start'
                deadline = time.monotonic() + 90
            else:
                record('Save files unchanged on disk', initial_saves == disk_saves())
                editor.load_level('/Game/Level/Stage1')
                finish()
    except Exception:
        finish(traceback.format_exc())

handle = unreal.register_slate_post_tick_callback(tick)
unreal.log('[RefactorPIE] Scheduled smoke checks; verification editor exits afterward.')
