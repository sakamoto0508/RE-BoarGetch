"""PIE smoke: real lobby/BP plus transient catalog. User assets/saves are preserved."""
import hashlib
import json
from pathlib import Path
import time
import traceback
import unreal

@unreal.uclass()
class StageSelectSmokeLockedConfig(unreal.StageConfig):
    pass

@unreal.uclass()
class StageSelectSmokeUnlockedConfig(unreal.StageConfig):
    pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
HERE = Path(__file__).resolve().parent
SAVE_DIR = HERE.parent.parent.parent / 'Saved' / 'SaveGames'
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
passed = []
state = 'start'
deadline = time.monotonic() + 120
next_tick = 0
handle = None
widget = None
entrance = None
catalog = None
pc = None
temporary_stages = []
ISOLATED_SAVED = HERE / 'StageSelectPieUser' / 'Saved'
save_slot = None
save_user_index = 0
performance_defaults = None
original_throttle = None

def isolated_save_path():
    return str(Path(unreal.Paths.project_saved_dir()).resolve()).lower() == str(ISOLATED_SAVED.resolve()).lower()

def hashes():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SAVE_DIR.glob('*.sav')}

before = hashes()

def record(label, condition=True):
    if not condition:
        raise AssertionError(label)
    passed.append(label)
    unreal.log('[StageSelectPIE] PASS: ' + label)

def prop(obj, name):
    return obj.get_editor_property(name)

def find_widget(name):
    bindings = {'Button_Previous': 'PreviousButtonName', 'Button_Next': 'NextButtonName'}
    if name in bindings:
        name = str(prop(widget, bindings[name]))
    tree = next((obj for obj in unreal.ObjectIterator(unreal.WidgetTree) if obj.get_outer() == widget), None)
    value = unreal.find_object(tree, name) if tree is not None else None
    if value is None:
        raise AssertionError('Missing binding widget ' + name)
    return value

def text(name):
    return str(find_widget(name).get_text())

def open_selection():
    global widget
    pawn = pc.get_controlled_pawn()
    old = prop(entrance, 'bRequiresInteraction')
    entrance.set_editor_property('bRequiresInteraction', False)
    try:
        entrance.call_method('HandleTriggerBeginOverlap', args=(None, pawn, None, 0, False, unreal.HitResult()))
    finally:
        entrance.set_editor_property('bRequiresInteraction', old)
    found = unreal.WidgetLibrary.get_all_widgets_of_class(worlds.get_game_world(), unreal.BoarLobbyWidget, True)
    widget = next((w for w in found if w.is_in_viewport()), None)
    record('Existing StageEntrance opens actual Stage Select BP', widget is not None)

def synthetic_catalog(first):
    locked = unreal.get_default_object(StageSelectSmokeLockedConfig)
    locked.set_editor_property('StageId', 'SmokeLocked')
    locked.set_editor_property('DisplayName', 'Locked smoke stage')
    unlock = unreal.StageUnlockCondition()
    unlock.set_editor_property('RequiredClearedStageId', 'SmokeMissingClear')
    locked.set_editor_property('UnlockCondition', unlock)
    locked.set_editor_property('Level', prop(first, 'Level'))
    third = unreal.get_default_object(StageSelectSmokeUnlockedConfig)
    third.set_editor_property('StageId', 'SmokeUnlocked')
    third.set_editor_property('DisplayName', 'Unlocked smoke stage')
    third.set_editor_property('Description', 'Transient verification data')
    third.set_editor_property('TargetCaptureCount', 2)
    third.set_editor_property('Level', prop(first, 'Level'))
    coins, boars = [], []
    for i in range(2):
        coin = unreal.SpecialCoinDefinition()
        coin.set_editor_property('SpecialCoinId', 'SmokeCoin' + str(i))
        coins.append(coin)
        boar = unreal.BoarSpawnDefinition()
        boar.set_editor_property('BoarUniqueId', 'SmokeBoar' + str(i))
        boars.append(boar)
    third.set_editor_property('SpecialCoinDefinitions', coins)
    third.set_editor_property('BoarSpawnDefinitions', boars)
    temporary_stages.extend([locked, third])
    return [first, locked, third, first]

def finish(error=None):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    if editor.is_in_play_in_editor():
        editor.editor_request_end_play()
    if performance_defaults is not None and original_throttle is not None:
        performance_defaults.set_editor_property('bThrottleCPUWhenNotForeground', original_throttle)
    if save_slot is not None and isolated_save_path():
        unreal.GameplayStatics.delete_game_in_slot(save_slot, save_user_index)
    data = {'passed': passed, 'error': error, 'user_save_files_unchanged': before == hashes(),
            'physical_keyboard_gamepad_tested': False}
    (HERE / 'StageSelectPieSmokeResults.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    if error:
        unreal.log_error('[StageSelectPIE] ' + error)
    else:
        unreal.log('[StageSelectPIE] COMPLETE ' + str(len(passed)))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    global state, next_tick, deadline, pc, entrance, catalog, widget, save_slot, save_user_index, performance_defaults, original_throttle
    if time.monotonic() < next_tick:
        return
    next_tick = time.monotonic() + .5
    try:
        if time.monotonic() > deadline:
            raise TimeoutError(state)
        if state == 'start':
            record('Runtime Save directory is isolated by -UserDir', isolated_save_path())
            performance_defaults = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
            original_throttle = prop(performance_defaults, 'bThrottleCPUWhenNotForeground')
            performance_defaults.set_editor_property('bThrottleCPUWhenNotForeground', False)
            record('Lobby level loads', editor.load_level('/Game/Level/L_Lobby'))
            editor.editor_request_begin_play()
            state = 'real'
        elif state == 'real':
            if not editor.is_in_play_in_editor(): return
            world = worlds.get_game_world()
            if world is None: return
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            if pc is None or pc.get_controlled_pawn() is None: return
            record('Actual lobby controller/player created', isinstance(pc, unreal.BoarPlayerController))
            entrance = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StageEntrance)[0]
            catalog = list(prop(entrance, 'StageCatalog'))
            open_selection()
            instance = unreal.GameplayStatics.get_game_instance(world)
            save_slot = str(prop(instance, 'SaveSlotName'))
            save_user_index = int(prop(instance, 'SaveUserIndex'))
            save = instance.get_progress()
            expected = 'CLEAR' if prop(catalog[0], 'StageId') in prop(save, 'ClearedStageIds') else '未クリア'
            record('Actual stage name displayed', text('StageName') == str(prop(catalog[0], 'DisplayName')))
            record('Actual clear status matches isolated progress', text('Text_StageStatus') == expected)
            record('Actual stage progress displayed', '図鑑：' in text('Text_StageProgress'))
            record('Actual start enabled', find_widget('Button_Start').get_is_enabled())
            record('Single-stage carousel previous/next disabled', not find_widget('Button_Previous').get_is_enabled()
                   and not find_widget('Button_Next').get_is_enabled())
            state = 'cancel'
        elif state == 'cancel':
            record('Initial focus reaches Start', find_widget('Button_Start').has_user_focus(pc))
            widget.call_method('HandleCancelClicked')
            record('Cancel removes selection and releases input locks', not widget.is_in_viewport()
                   and not pc.is_move_input_ignored() and not pc.is_look_input_ignored())
            entrance.set_editor_property('StageCatalog', synthetic_catalog(catalog[0]))
            open_selection()
            widget.call_method('NextStage')
            record('Start disabled during carousel animation', not find_widget('Button_Start').get_is_enabled())
            state = 'locked'
        elif state == 'locked':
            if text('Text_StageNumber') != 'SmokeLocked': return
            record('Carousel moves to locked card', text('Text_StageNumber') == 'SmokeLocked')
            record('Locked card label/status redacted', text('StageName') == '???' and text('Text_StageStatus') == 'LOCKED')
            record('Locked progress hidden and start disabled', text('Text_StageProgress') == '' and not find_widget('Button_Start').get_is_enabled())
            widget.call_method('HandleStartStageClicked')
            record('Locked start leaves lobby selection open', widget.is_in_viewport())
            widget.call_method('SelectStage', args=(0,))
            record('Entry selection returns to unlocked stage', text('Text_StageNumber') == str(prop(catalog[0], 'StageId')))
            widget.call_method('SelectStage', args=(1,))
            record('Locked entry intent rejected', text('Text_StageNumber') == str(prop(catalog[0], 'StageId')))
            widget.call_method('NextStage')
            widget.call_method('NextStage')
            state = 'third'
        elif state == 'third':
            if text('Text_StageNumber') != 'SmokeUnlocked': return
            record('Rapid Next requests finish at third card', text('Text_StageNumber') == 'SmokeUnlocked')
            record('Duplicate catalog ID excluded at carousel boundary', not find_widget('Button_Next').get_is_enabled())
            record('Transient coin/boar progress rendered', text('Text_StageProgress') == '特別コイン：0 / 2\n図鑑：0 / 2')
            record('Carousel transform settles after animation', prop(find_widget('CarouselCurrent'), 'RenderTransform').translation == unreal.Vector2D(0, 0))
            record('Focus returns to enabled Start', find_widget('Button_Start').has_user_focus(pc))
            widget.call_method('PreviousStage')
            state = 'previous'
        elif state == 'previous':
            if text('Text_StageNumber') != 'SmokeLocked': return
            record('Previous returns to locked card', text('Text_StageNumber') == 'SmokeLocked')
            widget.call_method('PreviousStage')
            state = 'first'
        elif state == 'first':
            if text('Text_StageNumber') != str(prop(catalog[0], 'StageId')): return
            record('Previous returns to first card', text('Text_StageNumber') == str(prop(catalog[0], 'StageId')))
            record('Preview actor retained for visible first card', len(unreal.GameplayStatics.get_all_actors_of_class(worlds.get_game_world(), unreal.BoarStagePreviewActor)) > 0)
            # Actual Start path writes inside the isolated process UserDir, then travels normally.
            instance = unreal.GameplayStatics.get_game_instance(worlds.get_game_world())
            record('Start guard verifies isolated save directory', isolated_save_path())
            widget.call_method('HandleStartStageClicked')
            state = 'travel'
            deadline = time.monotonic() + 120
        elif state == 'travel':
            world = worlds.get_game_world()
            mode = unreal.GameplayStatics.get_game_mode(world) if world else None
            if not isinstance(mode, unreal.BoarGameMode): return
            if mode.get_stage_state() != unreal.BoarStageState.PLAYING: return
            record('Start travels through existing StageEntrance to Stage1 Playing')
            record('Isolated slot records LastAttemptedStage', str(unreal.GameplayStatics.get_game_instance(world).get_progress().get_editor_property('LastAttemptedStageId')) == str(prop(catalog[0], 'StageId')))
            editor.editor_request_end_play()
            state = 'stop'
        else:
            if editor.is_in_play_in_editor(): return
            unreal.GameplayStatics.delete_game_in_slot(save_slot, save_user_index)
            record('User save files remain byte-identical', before == hashes())
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = unreal.register_slate_post_tick_callback(tick)
