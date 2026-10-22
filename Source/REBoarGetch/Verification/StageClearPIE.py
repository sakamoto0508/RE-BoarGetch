"""End-to-end PIE verification of the stage clear presentation (GUI editor, real GPU):
  run_ue_gui.sh StageClearPIE.py
Flow: Stage1 -> capture 2 boars directly + the last one with a real net swing (CaptureWithFeedback / capture presentation)
      -> victory presentation (timed Game View screenshots + metrics) -> WBP_Result reveal -> Lobby
      -> Stage1 again (player/net restored) -> GameOver (no victory presentation) -> Retry -> second clear (0 coins) -> Lobby -> Stage1.
Saved/SaveGames is backed up first and restored at the end (a Clear commits progress).
Outputs: Saved/Screenshots/StageClear/<stamp>/*.png and Verification/StageClearPIE.json."""
from pathlib import Path
import json, math, shutil, time, traceback
import unreal

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent.parent.parent
SAVES = PROJ / 'Saved' / 'SaveGames'
BACKUP = PROJ / 'Saved' / 'StageClearVerify' / 'SaveGamesBackup'
STAMP = time.strftime('%Y%m%d_%H%M%S')
SHOTS = PROJ / 'Saved' / 'Screenshots' / 'StageClear' / STAMP
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
L = unreal.BoarUIAuthoringLibrary
R = {'stamp': STAMP, 'checks': [], 'failed': [], 'cycles': {}, 'shots': []}
S = {'gen': None, 'wait': 0, 'h': None, 'deadline': time.monotonic() + 1500}


def log(msg):
    unreal.log('[StageClearPIE] ' + str(msg))


def check(label, ok, detail=None):
    (R['checks'] if ok else R['failed']).append(label + ('' if detail is None else ' [%s]' % (detail,)))
    log('%s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def world():
    return ues.get_game_world()


def pc():
    w = world()
    return unreal.GameplayStatics.get_player_controller(w, 0) if w else None


def mode():
    w = world()
    return unreal.GameplayStatics.get_game_mode(w) if w else None


def ename(e):
    return str(getattr(e, 'name', e)).split('.')[-1].upper()


def v(x):
    return [round(x.x, 1), round(x.y, 1), round(x.z, 1)]


def shot(label, ui=True):
    path = str(SHOTS / (label + '.png'))
    while not L.request_verification_screenshot(path, ui):
        yield 1
    yield 1
    while L.is_verification_screenshot_pending():
        yield 1
    R['shots'].append(label + '.png')


def wait_until(fn, frames=1200):
    for _ in range(frames):
        if fn():
            return True
        yield 1
    return False


def visuals():
    """Visibility of the player body and every mesh on the equipped net (and anything attached)."""
    p = pc()
    pawn = p.get_controlled_pawn() if p else None
    if not pawn:
        return {'pawn': None}
    out = {'body_visible': pawn.mesh.is_visible(), 'body_overlay': str(pawn.mesh.get_overlay_material()),
           'mesh_rel': v(pawn.mesh.get_editor_property('relative_location'))}
    g = pawn.get_component_by_class(unreal.GadgetComponent).get_current_gadget()
    out['gadget'] = g.get_class().get_name() if g else None
    if g:
        meshes = g.get_components_by_class(unreal.MeshComponent)
        out['net_meshes_visible'] = [m.is_visible() and not m.get_editor_property('hidden_in_game') for m in meshes]
        out['net_attach_socket'] = str(g.get_attach_parent_socket_name())
        out['net_parent'] = g.get_attach_parent_actor().get_name() if g.get_attach_parent_actor() else None
    out['extra_skeletal_on_pawn'] = len(pawn.get_components_by_class(unreal.SkeletalMeshComponent)) - 1
    base = pawn.get_actor_location().z
    out['pelvis_z'] = round(pawn.mesh.get_socket_location('pelvis').z - base, 1)
    out['hand_r_z'] = round(pawn.mesh.get_socket_location('hand_r').z - base, 1)
    out['anim_mode'] = str(pawn.mesh.get_animation_mode()).split('.')[-1].split(':')[0]
    if g:
        tops = [(lambda b: b[0].z + b[1].z)(unreal.SystemLibrary.get_component_bounds(m)) for m in g.get_components_by_class(unreal.MeshComponent)]
        out['net_top_z'] = round(max(tops) - base, 1) if tops else None
    return out


def camera_metrics(comp):
    cm = pc().player_camera_manager
    loc, rot = cm.get_camera_location(), cm.get_camera_rotation()
    focus = comp.get_victory_focus_point()
    d = unreal.Vector(focus.x - loc.x, focus.y - loc.y, focus.z - loc.z)
    dist = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
    fwd = rot.get_forward_vector()
    err = math.degrees(math.acos(max(-1, min(1, (fwd.x * d.x + fwd.y * d.y + fwd.z * d.z) / max(dist, 1e-3)))))
    return {'cam': v(loc), 'focus': v(focus), 'dist': round(dist, 1), 'aim_err_deg': round(err, 2), 'fov': round(cm.get_fov_angle(), 1),
            'view_target': pc().get_view_target().get_class().get_name()}


def wait_playing():
    yield from wait_until(lambda: pc() and pc().get_controlled_pawn() and mode() and mode().get_stage_state() == unreal.BoarStageState.PLAYING, 2400)
    # hide engine debug text (e.g. GPU memory warnings) so screenshots show only the game
    unreal.SystemLibrary.execute_console_command(world(), 'DisableAllScreenMessages')
    for _ in range(45):
        yield 1


def capture_direct(boar):
    if boar.get_component_by_class(unreal.CaptureComponent).capture(None):
        mode().handle_boar_captured(boar)
        return True
    return False


def capture_with_net(boar):
    """Real gameplay path: net swing input -> AnimNotify capture window -> CaptureWithFeedback -> capture presentation."""
    p = pc()
    pawn = p.get_controlled_pawn()
    gadget = pawn.get_component_by_class(unreal.GadgetComponent).get_current_gadget()
    coll = gadget.get_component_by_class(unreal.SphereComponent)
    fwd = pawn.get_actor_forward_vector()
    loc = pawn.get_actor_location()
    boar.set_actor_location(unreal.Vector(loc.x + fwd.x * 120, loc.y + fwd.y * 120, loc.z), False, True)
    method = 'input'
    try:
        lib = getattr(unreal, 'SubsystemBlueprintLibrary', None) or getattr(unreal, 'SubsystemLibrary', None)
        R['subsystem_libs'] = [n for n in dir(unreal) if 'Subsystem' in n and ('Library' in n or 'Statics' in n)]
        sub = lib.get_local_player_subsystem(p, unreal.EnhancedInputLocalPlayerSubsystem) if lib else \
            unreal.EnhancedInputLibrary.get_local_player_subsystem(p) if hasattr(unreal.EnhancedInputLibrary, 'get_local_player_subsystem') else None
        if sub is None:
            raise RuntimeError('no local player subsystem accessor: %s' % R['subsystem_libs'])
        value = unreal.EnhancedInputLibrary.make_input_action_value_of_type(1.0, 0.0, 0.0, unreal.InputActionValueType.BOOLEAN)
        sub.inject_input_for_action(p.get_editor_property('gadget_action'), value, [], [])
    except Exception as e:
        method = 'capture_window (input injection unavailable: %s)' % e
    cap = boar.get_component_by_class(unreal.CaptureComponent)
    for i in range(150):
        if cap.get_editor_property('is_captured'):
            return method, i
        # keep the boar inside the swinging net head so the animation's capture window is what catches it
        c = coll.get_world_location()
        boar.set_actor_location(unreal.Vector(c.x, c.y, max(c.z - 40, loc.z - 40)), False, True)
        if i == 100 and method == 'input':
            method = 'capture_window (swing did not overlap in 100 frames)'
        if i >= 100:
            gadget.begin_capture_window()
            gadget.end_capture_window()
        yield 1
    return method, -1


def clear_cycle(name, coins):
    cyc = R['cycles'].setdefault(name, {'timeline': [], 'result': {}})
    m = mode()
    target = m.get_stage_config().target_capture_count
    cyc['start_visuals'] = visuals()
    yield from shot(name + '_00_start')
    if coins:
        pawn = pc().get_controlled_pawn()

        coin = L.spawn_verification_coin(world(), 'StageClearVerify_Coin_%s' % name, pawn.get_actor_location())
        cyc['coin_collected'] = coin.try_collect(pawn)
    boars = [b for b in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.BoarBase)
             if not b.get_component_by_class(unreal.CaptureComponent).get_editor_property('is_captured')]
    cyc['boars_available'] = len(boars)
    for b in boars[:max(0, target - 1 - m.get_captured_boar_count())]:
        capture_direct(b)
        for _ in range(20):
            yield 1
    yield from wait_until(lambda: mode().get_captured_boar_count() >= target - 1, 600)
    cyc['before_last'] = mode().get_captured_boar_count()
    last = [b for b in boars if not b.get_component_by_class(unreal.CaptureComponent).get_editor_property('is_captured')][0]
    gen = capture_with_net(last)
    try:
        while True:
            yield next(gen)
    except StopIteration as stop:
        cyc['last_capture'] = stop.value
    comp = pc().get_editor_property('stage_clear_presentation')
    # timeline: from the last capture until the Result widget appears
    t0 = time.monotonic()
    next_shot = 0.0
    seen_phases = []
    result = None
    while time.monotonic() - t0 < 20:
        ph = ename(comp.get_phase())
        if not seen_phases or seen_phases[-1] != ph:
            seen_phases.append(ph)
        st = ename(mode().get_stage_state())
        row = {'t': round(time.monotonic() - t0, 2), 'state': st, 'phase': ph, 'vis': visuals()}
        if ph not in ('IDLE',):
            row.update(camera_metrics(comp))
        cyc['timeline'].append(row)
        ws = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world(), unreal.BoarResultWidget, False) if w.is_in_viewport()]
        if ws:
            result = ws[0]
            break
        if time.monotonic() - t0 >= next_shot:
            yield from shot('%s_%02d_t%04.1f_%s' % (name, 10 + int(next_shot * 5), time.monotonic() - t0, ph.lower()))
            next_shot += .2
        else:
            yield 1
    cyc['phases'] = seen_phases
    check(name + ': Result appears after the presentation', result is not None, seen_phases)
    if not result:
        return
    # Result reveal: sequential UMG animations, input gated until done
    t1 = time.monotonic()
    reveal = []
    for at in [0.05, .4, .8, 1.15, 1.5, 1.9, 2.4]:
        while time.monotonic() - t1 < at:
            yield 1
        cap_line = result.get_editor_property('text_capture_line')
        coin_line = result.get_editor_property('text_coin_line')
        title = result.get_editor_property('text_stage_clear')
        reveal.append({'t': at, 'accepted': result.is_dismiss_input_accepted(),
                       'opacity': [round(w.get_render_opacity(), 2) for w in (title, cap_line, coin_line)]})
        yield from shot('%s_60_result_t%.2f' % (name, at))
    run = mode().get_stage_run_data()
    cyc['result'] = {'capture_line': str(result.get_editor_property('text_capture_line').get_text()),
                     'coin_line': str(result.get_editor_property('text_coin_line').get_text()),
                     'run_housed': run.current_housed_count, 'run_coins': len(run.special_coin_ids), 'target': target,
                     'reveal': reveal}
    exp_cap = 'ボア捕獲数 %d / %d' % (run.current_housed_count, target)
    exp_coin = '特別コイン +%d' % len(run.special_coin_ids)
    check(name + ': Result capture line matches run data', cyc['result']['capture_line'] == exp_cap, cyc['result']['capture_line'])
    check(name + ': Result coin line matches run data', cyc['result']['coin_line'] == exp_coin, cyc['result']['coin_line'])
    check(name + ': coin count is the live run value', len(run.special_coin_ids) == coins, len(run.special_coin_ids))
    check(name + ': input gated until reveal finished', not reveal[0]['accepted'] and not reveal[2]['accepted'] and reveal[-1]['accepted'],
          [r['accepted'] for r in reveal])
    order = [next((r['t'] for r in reveal if r['opacity'][i] > .5), None) for i in range(3)]
    check(name + ': STAGE CLEAR -> capture -> coin shown in order', None not in order and order[0] <= order[1] <= order[2], order)
    tl = [r for r in cyc['timeline'] if r['phase'] in ('VICTORY', 'RETREAT')]
    check(name + ': victory camera is the view target', all(r.get('view_target') == 'CameraActor' for r in tl[3:]), len(tl))
    retreat = [r for r in cyc['timeline'] if r['phase'] == 'RETREAT']
    if retreat:
        check(name + ': camera keeps looking at VictoryFocusPoint while retreating', max(r['aim_err_deg'] for r in retreat) < 3.0,
              max(r['aim_err_deg'] for r in retreat))
        check(name + ': camera moves away during retreat', retreat[-1]['dist'] > retreat[0]['dist'] + 150,
              (retreat[0]['dist'], retreat[-1]['dist']))
        check(name + ': focus point fixed', len({tuple(r['focus']) for r in retreat}) == 1, retreat[0]['focus'])
        gone = [r['vis'] for r in retreat]
        check(name + ': body and net hidden after vanish (no net left behind)',
              all(not g['body_visible'] and not any(g.get('net_meshes_visible', [])) and g['extra_skeletal_on_pawn'] == 0 for g in gone),
              gone[0])
    vic = [r for r in cyc['timeline'] if r['phase'] == 'VICTORY']
    shown = [r['vis'] for r in vic if r['vis'].get('body_visible')]
    if shown:
        p0, h0, n0 = shown[0]['pelvis_z'], shown[0]['hand_r_z'], shown[0].get('net_top_z') or 0
        jump = max(s['pelvis_z'] for s in shown) - p0
        raise_hand = max(s['hand_r_z'] for s in shown) - h0
        raise_net = max((s.get('net_top_z') or 0) for s in shown) - n0
        cyc['pose'] = {'pelvis_rise': round(jump, 1), 'hand_rise': round(raise_hand, 1), 'net_top_rise': round(raise_net, 1),
                       'anim_modes': sorted({s['anim_mode'] for s in shown})}
        check(name + ': victory hop moves the pelvis up (bone motion, movement disabled)', jump > 15, cyc['pose'])
        check(name + ': net is raised high', raise_hand > 25 and raise_net > 60, cyc['pose'])
    if vic:
        check(name + ': body and net visible together at victory start', vic[0]['vis']['body_visible'] and all(vic[0]['vis'].get('net_meshes_visible', [False])),
              vic[0]['vis'])
    result.on_dismiss_requested.broadcast()


def run():
    if SAVES.exists():
        if BACKUP.exists():
            shutil.rmtree(BACKUP)
        shutil.copytree(SAVES, BACKUP)
        R['save_backup'] = str(BACKUP)
    assert les.load_level('/Game/Level/Stage1')
    yield 30
    # Game View only: PIE in its own 1280x720 window (the user's play settings are restored in finish()).


    L.request_pie_in_new_window(1280, 720)
    yield from wait_until(lambda: les.is_in_play_in_editor(), 600)
    yield from wait_playing()
    R['viewport'] = str(unreal.WidgetLayoutLibrary.get_viewport_size(world()))
    check('A: player starts with body + net visible', visuals()['body_visible'] and all(visuals().get('net_meshes_visible', [False])), visuals())
    yield from clear_cycle('A', coins=1)
    yield from wait_until(lambda: world() and 'Lobby' in world().get_name(), 1200)
    for _ in range(60):
        yield 1
    yield from shot('A_90_lobby')
    check('A: returned to Lobby', world() and 'Lobby' in world().get_name(), world().get_name() if world() else None)
    # Next stage entry from the Lobby (StageConfig.Level == Stage1)
    unreal.GameplayStatics.open_level(world(), 'Stage1')
    yield from wait_until(lambda: world() and world().get_name() == 'Stage1' and mode() and pc() and pc().get_controlled_pawn(), 1200)
    yield from wait_playing()
    vs = visuals()
    R['B_start_visuals'] = vs
    yield from shot('B_00_next_stage_start')
    check('B: next stage shows body + net normally', vs['body_visible'] and all(vs.get('net_meshes_visible', [False])) and vs['extra_skeletal_on_pawn'] == 0
          and vs['body_overlay'] == 'None', vs)
    # GameOver must not use the victory presentation
    mode().request_game_over()
    yield from wait_until(lambda: mode().get_stage_state() == unreal.BoarStageState.GAME_OVER, 300)
    for _ in range(40):
        yield 1
    comp = pc().get_editor_property('stage_clear_presentation')
    check('B: GameOver does not start the victory presentation', comp.get_phase() == unreal.StageClearPresentationPhase.IDLE and pc().get_view_target() == pc().get_controlled_pawn(),
          (str(comp.get_phase()), pc().get_view_target().get_name()))
    yield from shot('B_10_gameover')
    pc().retry_stage()
    yield from wait_until(lambda: world() and mode() and mode().get_stage_state() in (unreal.BoarStageState.PREPARING, unreal.BoarStageState.PLAYING), 600)
    yield from wait_playing()
    vs = visuals()
    yield from shot('C_00_after_retry')
    check('C: after Retry body + net normal and presentation idle',
          vs['body_visible'] and all(vs.get('net_meshes_visible', [False])) and pc().get_editor_property('stage_clear_presentation').get_phase() == unreal.StageClearPresentationPhase.IDLE, vs)
    yield from clear_cycle('C', coins=0)
    yield from wait_until(lambda: world() and 'Lobby' in world().get_name(), 1200)
    for _ in range(30):
        yield 1
    check('C: returned to Lobby', world() and 'Lobby' in world().get_name())
    unreal.GameplayStatics.open_level(world(), 'Stage1')
    yield from wait_until(lambda: world() and world().get_name() == 'Stage1' and mode() and pc() and pc().get_controlled_pawn(), 1200)
    yield from wait_playing()
    vs = visuals()
    yield from shot('D_00_third_entry')
    check('D: third entry body + net normal', vs['body_visible'] and all(vs.get('net_meshes_visible', [False])), vs)
    les.editor_request_end_play()
    yield from wait_until(lambda: not les.is_in_play_in_editor(), 600)


def finish(error=None):
    if S['h'] is not None:
        unreal.unregister_slate_post_tick_callback(S['h'])
        S['h'] = None
    R['error'] = error
    if S.get('play_settings'):
        L.restore_pie_window()
        R['play_settings_restored'] = True
    try:
        if BACKUP.exists():
            if SAVES.exists():
                shutil.rmtree(SAVES)
            shutil.copytree(BACKUP, SAVES)
            R['save_restored'] = True
    except Exception:
        R['save_restore_error'] = traceback.format_exc()
    (HERE / 'StageClearPIE.json').write_text(json.dumps(R, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
    if les.is_in_play_in_editor():
        les.editor_request_end_play()
    log('%s passed=%d failed=%d' % ('ERROR ' + error if error else 'COMPLETE', len(R['checks']), len(R['failed'])))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


def tick(dt):
    if S.get('busy'):
        return
    S['busy'] = True
    try:
        if time.monotonic() > S['deadline']:
            raise TimeoutError('timeout')
        if S['wait'] > 0:
            S['wait'] -= 1
            return
        S['wait'] = next(S['gen']) or 0
    except StopIteration:
        finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        S['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['gen'] = run()
S['h'] = unreal.register_slate_post_tick_callback(tick)
