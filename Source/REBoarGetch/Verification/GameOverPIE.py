"""End-to-end PIE verification of the GameOver presentation (AKI-75), GUI editor with real GPU:
  run_ue_gui.sh C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Verification/GameOverPIE.py
A: L_Stage01 -> HP to 1 -> a real Boar contact (lethal) -> presentation timeline sampled every frame + Game View screenshots
   -> duplicate CompleteStageEndPresentation -> Pause / input / extra damage blocked -> WBP_GameOver reveal.
B: Retry -> HP / Boars / Cages / loadout re-initialised, time dilation / camera / widgets clean.
C: cage destroyed -> stage keeps playing (no GameOver).
D: second GameOver -> RETURN TO LOBBY -> Lobby has normal time dilation and no GameOver widgets.
E: Stage again -> last capture and death in the same frame -> Clear wins (GameOver presentation idle) -> Result -> Lobby.
Saved/SaveGames is backed up first and restored at the end (E commits a clear).
Outputs: Saved/Screenshots/GameOver/<stamp>/*.png and Verification/GameOverPIE.json."""
from pathlib import Path
import json, math, shutil, time, traceback
import unreal

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent.parent.parent
SAVES = PROJ / 'Saved' / 'SaveGames'
BACKUP = PROJ / 'Saved' / 'GameOverVerify' / 'SaveGamesBackup'
STAMP = time.strftime('%Y%m%d_%H%M%S')
SHOTS = PROJ / 'Saved' / 'Screenshots' / 'GameOver' / STAMP
STAGE = '/Game/Level/L_Stage01'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
L = unreal.BoarUIAuthoringLibrary
R = {'stamp': STAMP, 'checks': [], 'failed': [], 'shots': []}
S = {'gen': None, 'wait': 0, 'h': None, 'deadline': time.monotonic() + 1500}


def log(msg):
    unreal.log('[GameOverPIE] ' + str(msg))


def check(label, ok, detail=None):
    (R['checks'] if ok else R['failed']).append(label + ('' if detail is None else ' [%s]' % (detail,)))
    log('%s %s %s' % ('PASS' if ok else 'FAIL', label, '' if detail is None else detail))


def world():
    return ues.get_game_world()


def pc():
    w = world()
    return unreal.GameplayStatics.get_player_controller(w, 0) if w else None


def pawn():
    p = pc()
    return p.get_controlled_pawn() if p else None


def mode():
    w = world()
    return unreal.GameplayStatics.get_game_mode(w) if w else None


def comp():
    return pc().get_editor_property('game_over_presentation')


def ename(e):
    return str(getattr(e, 'name', e)).split('.')[-1].upper()


def v(x):
    return [round(x.x, 1), round(x.y, 1), round(x.z, 1)]


def widgets(cls):
    return [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world(), cls, False) if w.is_in_viewport()]


def dilation():
    return round(unreal.GameplayStatics.get_global_time_dilation(world()), 3)


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


def wait_playing():
    yield from wait_until(lambda: pc() and pawn() and mode() and mode().get_stage_state() == unreal.BoarStageState.PLAYING, 2400)
    unreal.SystemLibrary.execute_console_command(world(), 'DisableAllScreenMessages')
    for _ in range(45):
        yield 1


def snapshot():
    """Stage state used to compare before GameOver and after Retry."""
    p = pawn()
    h = p.get_health_component()
    g = p.get_component_by_class(unreal.GadgetComponent)
    boars = unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.BoarBase)
    cages = unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.Cage)
    arm = p.get_component_by_class(unreal.SpringArmComponent)
    return {'hp': h.get_current_health(), 'max_hp': h.get_max_health(),
            'boars': len(boars), 'captured': sum(1 for b in boars if b.get_component_by_class(unreal.CaptureComponent).get_editor_property('is_captured')),
            'cages': len(cages), 'housed': sum(c.get_captured_boar_count() for c in cages), 'cages_destroyed': sum(1 for c in cages if c.get_is_cage_destroyed()),
            'loadout': [str(g.get_gadget_slot_class(i).get_name()) if g.get_gadget_slot_class(i) else None for i in range(4)],
            'dilation': dilation(), 'arm': round(arm.target_arm_length, 1), 'arm_offset': v(arm.target_offset),
            'fov': round(p.get_component_by_class(unreal.CameraComponent).field_of_view, 1),
            'effect_widgets': len(widgets(unreal.BoarGameOverEffectWidget)), 'gameover_widgets': len(widgets(unreal.BoarGameOverWidget)),
            'phase': ename(comp().get_phase()), 'downed': p.is_downed(), 'action': ename(p.get_player_action_state()),
            'move_ignored': pc().is_move_input_ignored(), 'anim_mode': str(p.mesh.get_animation_mode()).split('.')[-1].split(':')[0]}


def sample(t0):
    p = pawn()
    c = comp()
    cm = pc().player_camera_manager
    loc, rot = cm.get_camera_location(), cm.get_camera_rotation()
    pel, hd = p.mesh.get_socket_location('pelvis'), p.mesh.get_socket_location('head')
    focus = unreal.Vector((pel.x + hd.x) / 2, (pel.y + hd.y) / 2, (pel.z + hd.z) / 2)
    d = unreal.Vector(focus.x - loc.x, focus.y - loc.y, focus.z - loc.z)
    dist = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
    fwd = rot.get_forward_vector()
    err = math.degrees(math.acos(max(-1, min(1, (fwd.x * d.x + fwd.y * d.y + fwd.z * d.z) / max(dist, 1e-3)))))
    anim = p.mesh.get_anim_instance()
    mont = c.get_director().get_editor_property('down_montage') if c.get_director() else None
    hud = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world(), unreal.BoarHUDWidget, False)]
    base = p.get_actor_location().z
    return {'t': round(time.monotonic() - t0, 3), 'age': round(c.get_presentation_age(), 3), 'state': ename(mode().get_stage_state()),
            'phase': ename(c.get_phase()), 'dilation': dilation(), 'action': ename(p.get_player_action_state()), 'downed': p.is_downed(),
            'montage': bool(anim and mont and anim.montage_is_playing(mont)), 'montage_pos': round(anim.montage_get_position(mont), 3) if anim and mont else None,
            'anim_mode': str(p.mesh.get_animation_mode()).split('.')[-1].split(':')[0],
            'pelvis_z': round(pel.z - base, 1), 'head_z': round(hd.z - base, 1),
            'cam': v(loc), 'cam_dist': round(dist, 1), 'aim_err': round(err, 1), 'pitch': round(rot.pitch, 1), 'yaw': round(rot.yaw, 1),
            'arm': round(p.get_component_by_class(unreal.SpringArmComponent).target_arm_length, 1),
            'hud_opacity': round(hud[0].get_render_opacity(), 2) if hud else None, 'hud_vis': ename(hud[0].get_visibility()) if hud else None,
            'effect_widgets': len(widgets(unreal.BoarGameOverEffectWidget)), 'gameover_widgets': len(widgets(unreal.BoarGameOverWidget)),
            'hp': p.get_health_component().get_current_health(),
            'nearest_boar': round(min([math.hypot(b.get_actor_location().x - p.get_actor_location().x, b.get_actor_location().y - p.get_actor_location().y)
                                       for b in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.BoarBase)] or [0]), 1)}


def lethal_contact():
    """HP -> 1, then push a live Boar into the player capsule (the real damage path: overlap -> TakeDamage -> OnDeath)."""
    p = pawn()
    h = p.get_health_component()
    h.take_damage(h.get_current_health() - 1)
    for _ in range(70):   # the direct damage above does not start the contact invincibility; let any old one expire
        yield 1
    boars = [b for b in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.BoarBase)
             if not b.get_component_by_class(unreal.CaptureComponent).get_editor_property('is_captured')]
    pc_caps = p.get_component_by_class(unreal.CapsuleComponent)
    b0 = boars[0]
    bc = b0.get_component_by_class(unreal.CapsuleComponent)
    R["contact_diag"] = {"player_overlap_events": pc_caps.get_editor_property("generate_overlap_events"),
                         "player_vs_pawn": str(pc_caps.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)),
                         "boar_overlap_events": bc.get_editor_property("generate_overlap_events"),
                         "boar_vs_pawn": str(bc.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN))}
    for i in range(40):
        if mode().get_stage_state() != unreal.BoarStageState.PLAYING or h.is_dead():
            return "boar_contact", i
        b = boars[(i // 10) % len(boars)]
        bl = b.get_actor_location()
        pz = bl.z - b.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height() + pc_caps.get_scaled_capsule_half_height() + 2
        # place the player 90 cm beside the boar, then sweep it into the boar: a real blocking contact (Hit) like walking into it
        if i % 2 == 0:
            p.set_actor_location(unreal.Vector(bl.x + 90, bl.y, pz), False, True)
        else:
            res = p.set_actor_location(unreal.Vector(bl.x, bl.y, pz), True, False)
            hit = res[1] if isinstance(res, tuple) else res
            R.setdefault("contact_hits", []).append([str(res if not isinstance(res, tuple) else res[0]),
                [str(x)[:80] for x in hit.to_tuple()] if hit else None, h.get_current_health()])
        yield 1
    h.take_damage(1)
    return 'direct_damage_fallback', -1


def game_over_cycle(name):
    cyc = R.setdefault(name, {})
    cyc['before'] = snapshot()
    yield from shot(name + '_00_before')
    gen = lethal_contact()
    try:
        while True:
            yield next(gen)
    except StopIteration as stop:
        cyc['lethal_path'] = stop.value
    t0 = time.monotonic()
    tl = []
    shots = [0, .05, .15, .3, .5, .8, 1.1, 1.4, 1.8, 2.2]
    ui_at = None
    while time.monotonic() - t0 < 9:
        row = sample(t0)
        tl.append(row)
        if row["gameover_widgets"] and ui_at is None:
            ui_at = row
            head = widgets(unreal.BoarGameOverWidget)[0].get_editor_property("heading_widget")
            frames = []
            t2 = time.monotonic()
            for _ in range(60):
                if head:
                    frames.append((round(time.monotonic() - t2, 3), round(head.get_editor_property("render_transform").scale.x, 3), round(head.get_render_opacity(), 2)))
                yield 1
            cyc["heading_frames"] = frames
            break
        if shots and row['age'] >= shots[0] and row['phase'] == 'PLAYING':
            yield from shot('%s_%02d_age%.2f' % (name, 10 + len(R['shots']) % 90, row['age']))
            shots.pop(0)
        else:
            yield 1
    cyc['timeline'] = tl
    return tl, ui_at


def run():
    if SAVES.exists():
        if BACKUP.exists():
            shutil.rmtree(BACKUP)
        shutil.copytree(SAVES, BACKUP)
        R['save_backup'] = str(BACKUP)
    assert les.load_level(STAGE)
    yield 30
    L.request_pie_in_new_window(1280, 720)
    S['play_settings'] = True
    yield from wait_until(lambda: les.is_in_play_in_editor(), 600)
    yield from wait_playing()
    R['world'] = world().get_name()

    # ---------------- A: real lethal contact ----------------
    tl, ui = yield from game_over_cycle('A')
    play = [r for r in tl if r['phase'] == 'PLAYING']
    check('A: GameOver confirmed from HP0 (real contact path)', R['A']['lethal_path'][0] == 'boar_contact' and tl[0]['state'] == 'GAME_OVER', R['A']['lethal_path'])
    check('A: presentation started once and is active', len(play) > 10, len(play))
    check('A: hit stop slowed time briefly', any(r['dilation'] < .5 for r in tl[:12]), [r['dilation'] for r in tl[:8]])
    check('A: time dilation restored after the hit stop', all(r['dilation'] == 1.0 for r in tl if r['age'] > .2), None)
    check('A: player is DOWN (Stun does not override)', all(r['action'] == 'DOWN' and r['downed'] for r in tl), sorted({r['action'] for r in tl}))
    check('A: down montage plays', any(r['montage'] for r in play) or any(r['anim_mode'] == 'ANIMATION_SINGLE_NODE' for r in play),
          sorted({(r['montage'], r['anim_mode']) for r in play}))
    low = [r for r in play if r['age'] > 1.4]
    check('A: final pose is lying on the floor (pelvis low, head at floor level)', bool(low) and low[-1]['pelvis_z'] < play[0]['pelvis_z'] - 40 and low[-1]['head_z'] < play[0]['pelvis_z'],
          (play[0]['pelvis_z'], low[-1]['pelvis_z'] if low else None, low[-1]['head_z'] if low else None))
    check('A: no GameOver UI before the down finished', ui is not None and ui['age'] >= 1.45 and all(r['gameover_widgets'] == 0 for r in play), ui and ui['age'])
    check('A: UI appears within the safety timeout', ui is not None and ui['age'] < 6.0, ui and ui['age'])
    cam = [r for r in play if r['age'] >= .8]
    if cam:
        check('A: camera pulls back (arm grows) and tilts down', cam[-1]['arm'] > cam[0]['arm'] + 150 and cam[-1]['pitch'] < cam[0]['pitch'] - 10,
              (cam[0]['arm'], cam[-1]['arm'], cam[0]['pitch'], cam[-1]['pitch']))
        check('A: camera keeps the fallen body near screen centre', max(r['aim_err'] for r in cam if r['age'] > 1.2) < 12,
              max(r['aim_err'] for r in cam if r['age'] > 1.2))
        steps = [abs(b['yaw'] - a['yaw']) for a, b in zip(cam, cam[1:])]
        check('A: no sudden camera rotation', max(steps) < 3, max(steps))
    check('A: the boar that hit the player is knocked back (does not hide the down)', play[0]['nearest_boar'] < 200 and play[-1]['nearest_boar'] > play[0]['nearest_boar'] + 100,
          (play[0]['nearest_boar'], play[-1]['nearest_boar']))
    check('A: HUD faded out', any(r['hud_opacity'] is not None and r['hud_opacity'] <= .01 for r in play), [r['hud_opacity'] for r in play[::10]])
    check('A: exactly one effect widget during the presentation', all(r['effect_widgets'] == 1 for r in play), sorted({r['effect_widgets'] for r in play}))
    for _ in range(10):
        yield 1
    # duplicate completion notifications must not create a second UI
    mode().complete_stage_end_presentation()
    mode().complete_stage_end_presentation()
    for _ in range(10):
        yield 1
    check('A: duplicate completion does not duplicate WBP_GameOver', len(widgets(unreal.BoarGameOverWidget)) == 1, len(widgets(unreal.BoarGameOverWidget)))
    pc().open_pause_menu()
    for _ in range(5):
        yield 1
    check('A: Pause cannot open during GameOver', not pc().is_pause_menu_open())
    p = pawn()
    hp = p.get_health_component().get_current_health()
    check('A: input / damage blocked', pc().is_move_input_ignored() and not p.get_editor_property('can_be_damaged'), (pc().is_move_input_ignored(), p.get_editor_property('can_be_damaged'), hp))
    t1 = time.monotonic()
    w = widgets(unreal.BoarGameOverWidget)[0]
    head = w.get_editor_property("heading_widget")
    R['A']['heading_path'] = head.get_path_name() if head else None
    reveal = []
    for at in [.05, .2, .4, .8]:
        while time.monotonic() - t1 < at:
            yield 1
        reveal.append((at, round(head.get_editor_property("render_transform").scale.x, 3), round(head.get_render_opacity(), 2)))
        yield from shot('A_80_ui_t%.2f' % at)
    R['A']['heading_reveal'] = reveal
    hf = R['A'].get('heading_frames', [])
    check('A: GAME OVER heading scales in (>1.1), dips slightly and settles at 1.0', bool(hf) and hf[0][1] > 1.1 and min(f[1] for f in hf) < .995
          and abs(hf[-1][1] - 1.0) < .01 and hf[-1][2] > .99, hf[:3] + hf[-2:])
    held = sample(t1)
    check('A: lying pose held while the UI is shown', held['pelvis_z'] < tl[0]['pelvis_z'] - 40, held['pelvis_z'])
    R['A']['after_ui'] = snapshot()

    # ---------------- B: Retry ----------------
    w.request_retry()
    yield from wait_until(lambda: world() and mode() and pawn() and mode().get_stage_state() == unreal.BoarStageState.PLAYING
                          and pawn().get_health_component().get_current_health() > 0, 1200)
    yield from wait_playing()
    after = snapshot()
    R['B_after_retry'] = after
    yield from shot('B_00_after_retry')
    b = R['A']['before']
    check('B: Retry restores HP, Boars, Cages and loadout', after['hp'] == after['max_hp'] and after['boars'] == b['boars'] and after['captured'] == 0
          and after['housed'] == 0 and after['cages_destroyed'] == 0 and after['loadout'] == b['loadout'], (after, b))
    check('B: no presentation state carried into the retried stage', after['dilation'] == 1.0 and after['arm'] == b['arm'] and after['fov'] == b['fov']
          and after['effect_widgets'] == 0 and after['gameover_widgets'] == 0 and after['phase'] == 'IDLE' and not after['downed']
          and not after['move_ignored'] and after['anim_mode'] == b['anim_mode'], after)

    # ---------------- C: cage destroyed -> no GameOver ----------------
    cages = unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.Cage)
    for c in cages:
        c.destroy_cage()
    for _ in range(60):
        yield 1
    check('C: destroyed cage does not cause GameOver', mode().get_stage_state() == unreal.BoarStageState.PLAYING and comp().get_phase() == unreal.GameOverPresentationPhase.IDLE,
          (ename(mode().get_stage_state()), [c.get_is_cage_destroyed() for c in cages]))

    # ---------------- D: GameOver -> Lobby ----------------
    tl, ui = yield from game_over_cycle('D')
    check('D: second GameOver shows the UI once', ui is not None and len(widgets(unreal.BoarGameOverWidget)) == 1)
    for _ in range(30):
        yield 1
    widgets(unreal.BoarGameOverWidget)[0].request_lobby()
    yield from wait_until(lambda: world() and 'Lobby' in world().get_name() and pc() and pawn(), 1200)
    for _ in range(60):
        yield 1
    yield from shot('D_90_lobby')
    check('D: RETURN TO LOBBY opens the Lobby with clean state', 'Lobby' in world().get_name() and dilation() == 1.0
          and not widgets(unreal.BoarGameOverWidget) and not widgets(unreal.BoarGameOverEffectWidget), (world().get_name(), dilation()))

    # ---------------- E: same-frame Clear + GameOver -> Clear wins ----------------
    unreal.GameplayStatics.open_level(world(), 'L_Stage01')
    yield from wait_until(lambda: world() and world().get_name() == 'L_Stage01' and mode() and pawn(), 1200)
    yield from wait_playing()
    m = mode()
    target = m.get_stage_config().target_capture_count
    boars = [b for b in unreal.GameplayStatics.get_all_actors_of_class(world(), unreal.BoarBase)
             if not b.get_component_by_class(unreal.CaptureComponent).get_editor_property('is_captured')]
    for b in boars[:target - 1]:
        if b.get_component_by_class(unreal.CaptureComponent).capture(None):
            m.handle_boar_captured(b)
        for _ in range(20):
            yield 1
    yield from wait_until(lambda: mode().get_captured_boar_count() >= target - 1, 600)
    last = boars[target - 1]
    # same Python call = same game frame: last capture + lethal damage, resolved together at the frame end
    if last.get_component_by_class(unreal.CaptureComponent).capture(None):
        m.handle_boar_captured(last)
    pawn().get_health_component().take_damage(999)
    R['E_same_frame'] = {'clear_requested_state': ename(m.get_stage_state()), 'hp': pawn().get_health_component().get_current_health()}
    yield from wait_until(lambda: mode().get_stage_state() != unreal.BoarStageState.PLAYING, 300)
    for _ in range(20):
        yield 1
    st = ename(mode().get_stage_state())
    check('E: same-frame Clear and GameOver -> Clear wins', st == 'CLEARED' and comp().get_phase() == unreal.GameOverPresentationPhase.IDLE
          and not widgets(unreal.BoarGameOverEffectWidget), (st, ename(comp().get_phase())))
    ok = yield from wait_until(lambda: bool(widgets(unreal.BoarResultWidget)), 1200)
    check('E: Clear presentation still reaches the Result', ok and not widgets(unreal.BoarGameOverWidget))
    yield from shot('E_50_result')
    if ok:
        for _ in range(240):
            yield 1
        widgets(unreal.BoarResultWidget)[0].on_dismiss_requested.broadcast()
        yield from wait_until(lambda: world() and 'Lobby' in world().get_name(), 1200)
    les.editor_request_end_play()
    yield from wait_until(lambda: not les.is_in_play_in_editor(), 600)


def finish(error=None):
    if S['h'] is not None:
        unreal.unregister_slate_post_tick_callback(S['h'])
        S['h'] = None
    R['error'] = error
    if S.get('play_settings'):
        L.restore_pie_window()
    try:
        if BACKUP.exists():
            if SAVES.exists():
                shutil.rmtree(SAVES)
            shutil.copytree(BACKUP, SAVES)
            R['save_restored'] = True
    except Exception:
        R['save_restore_error'] = traceback.format_exc()
    (HERE / 'GameOverPIE.json').write_text(json.dumps(R, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
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
