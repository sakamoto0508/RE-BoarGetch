"""Read-only probe for the air net drop task: ground net montage notifies/sections, capture sphere, net socket.
Run: run_ue_py.sh AirNetDropProbe.py -> Saved/AirNetDropProbe.json"""
import unreal, json, traceback
OUT = {}
A = unreal.AnimationLibrary
def notifies(asset):
    res = []
    for ev in A.get_animation_notify_events(asset):
        n = ev.notify
        res.append({'t': ev.get_editor_property('trigger_time_offset') if False else A.get_animation_notify_event_trigger_time(ev) if hasattr(A,'get_animation_notify_event_trigger_time') else None,
                    'name': str(n.get_class().get_name()) if n else None,
                    'enable': n.get_editor_property('bEnableCaptureWindow') if n and n.get_class().get_name()=='AnimNotify_NetCaptureWindow' else None})
    return res
try:
    m = unreal.load_asset('/Game/InportAssets/Animation/Montage/AM_Player_NetSwing')
    OUT['montage_len'] = m.get_play_length()
    OUT['montage_rate'] = m.get_editor_property('rate_scale')
    OUT['montage_slots'] = [str(t.slot_name) for t in m.get_editor_property('slot_anim_tracks')]
    try: OUT['montage_sections'] = [m.get_section_name(i) for i in range(m.get_num_sections())]
    except Exception as e: OUT['montage_sections'] = str(e)
    try:
        evs = A.get_animation_notify_events(m)
        OUT['montage_notifies'] = [{'time': e.get_editor_property('trigger_time_offset'), 'cls': e.notify.get_class().get_name() if e.notify else None,
            'enable': e.notify.get_editor_property('bEnableCaptureWindow') if e.notify and 'NetCapture' in e.notify.get_class().get_name() else None} for e in evs]
        OUT['montage_notify_times'] = [A.get_animation_notify_event_names(m)]
    except Exception: OUT['montage_notifies_err'] = traceback.format_exc()
    try:
        tr = A.get_animation_notify_events(m)
        OUT['montage_notify_trigger'] = [ (e.get_editor_property('link_value') if True else 0) for e in tr]
    except Exception: OUT['n2err'] = traceback.format_exc()
    s = unreal.load_asset('/Game/InportAssets/Animation/A_Player_NetSwing')
    OUT['seq_len'] = s.get_play_length()
    try:
        evs = A.get_animation_notify_events(s)
        OUT['seq_notifies'] = [{'time': e.get_editor_property('link_value'), 'cls': e.notify.get_class().get_name() if e.notify else None} for e in evs]
    except Exception: OUT['seq_notifies_err'] = traceback.format_exc()
    for n in ['A_Player_JumpStart','A_Player_FallLoop','A_Player_Land','A_Player_DoubleJump','A_Player_Idle']:
        a = unreal.load_asset('/Game/InportAssets/Animation/' + n)
        OUT['len_' + n] = a.get_play_length() if a else None
    bp = unreal.load_asset('/Game/BP/Gadget/BP_NetGadget')
    cdo = unreal.get_default_object(bp.generated_class())
    sph = cdo.get_editor_property('capture_collision')
    OUT['sphere_cdo'] = str(sph)
    # SCS components may override; spawn-free inspection through SubobjectDataSubsystem
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    comps = []
    for h in handles:
        d = sub.k2_find_subobject_data_from_handle(h)
        o = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        if isinstance(o, unreal.SceneComponent):
            e = {'name': o.get_name(), 'cls': o.get_class().get_name(), 'loc': str(o.get_editor_property('relative_location')), 'rot': str(o.get_editor_property('relative_rotation')), 'scale': str(o.get_editor_property('relative_scale3d'))}
            if isinstance(o, unreal.SphereComponent): e['radius'] = o.get_editor_property('sphere_radius')
            if isinstance(o, unreal.StaticMeshComponent): e['mesh'] = str(o.get_editor_property('static_mesh'))
            comps.append(e)
    OUT['net_components'] = comps
    OUT['net_cooldown'] = cdo.get_editor_property('cooldown_seconds')
    OUT['net_socket_name'] = str(cdo.get_editor_property('equip_socket_name'))
    pbp = unreal.load_asset('/Game/BP/Player/BP_PlayerCharacter')
    pc = unreal.get_default_object(pbp.generated_class())
    mesh = pc.get_editor_property('mesh')
    OUT['player_mesh'] = str(mesh.get_editor_property('skeletal_mesh_asset') if hasattr(mesh,'skeletal_mesh_asset') else mesh.skeletal_mesh)
    OUT['player_anim_class'] = str(mesh.get_editor_property('anim_class'))
    OUT['player_mesh_rel'] = [str(mesh.get_editor_property('relative_location')), str(mesh.get_editor_property('relative_rotation'))]
    cm = pc.get_editor_property('character_movement')
    for p in ['gravity_scale','air_control','jump_z_velocity','max_walk_speed','braking_deceleration_falling','falling_lateral_friction']:
        OUT['cm_' + p] = cm.get_editor_property(p)
    cap = pc.get_editor_property('capsule_component')
    OUT['capsule'] = [cap.get_editor_property('capsule_radius'), cap.get_editor_property('capsule_half_height')]
    OUT['player_tick'] = [pc.get_editor_property('primary_actor_tick').get_editor_property('can_ever_tick'), pc.get_editor_property('primary_actor_tick').get_editor_property('start_with_tick_enabled')]
    sk = unreal.load_asset('/Game/InportAssets/Player/GreatSwordSlash_UE')
    socks = []
    for i in range(sk.num_sockets()):
        so = sk.get_socket_by_index(i)
        socks.append({'name': str(so.socket_name), 'bone': str(so.bone_name), 'loc': str(so.relative_location), 'rot': str(so.relative_rotation), 'scale': str(so.relative_scale)})
    OUT['sockets'] = socks
    nb = unreal.load_asset('/Game/BP/Boar/BP_NormalBoar') or None
    OUT['boar_bp'] = str(nb)
    if nb:
        bc = unreal.get_default_object(nb.generated_class())
        c = bc.get_editor_property('capsule_component')
        OUT['boar_capsule'] = [c.get_editor_property('capsule_radius'), c.get_editor_property('capsule_half_height')]
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'AirNetDropProbe.json', 'w').write(json.dumps(OUT, indent=1, default=str))
unreal.log('[ANDP] COMPLETE')
