import unreal
def cdo(path):
    c = unreal.load_class(None, path)
    return unreal.get_default_object(c)
for p in ['/Game/BP/Player/BP_PlayerCharacter.BP_PlayerCharacter_C', '/Game/BP/Boar/BP_NormalBoar.BP_NormalBoar_C']:
    o = cdo(p)
    m = o.get_component_by_class(unreal.CharacterMovementComponent)
    cap = o.get_component_by_class(unreal.CapsuleComponent)
    s = '%s walk=%s jumpZ=%s grav=%s step=%s slope=%s cap=%s/%s' % (p.split('.')[-1], m.max_walk_speed, m.jump_z_velocity, m.gravity_scale,
        m.max_step_height, m.get_walkable_floor_angle(), cap.get_unscaled_capsule_radius(), cap.get_unscaled_capsule_half_height())
    arm = o.get_component_by_class(unreal.SpringArmComponent)
    if arm:
        s += ' arm=%s off=%s rot=%s' % (arm.target_arm_length, arm.socket_offset, arm.get_relative_transform().rotation.rotator())
    try:
        s += ' maxjumps=%s' % o.get_editor_property('jump_max_count')
    except Exception: pass
    unreal.log('[S01] ' + s)
unreal.log('[S01] COMPLETE')
