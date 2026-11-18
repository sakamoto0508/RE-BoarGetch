# exec'd from PlayerHitNiagara.py (step configure). AKI-163 Red Boar hit / stun VFX.
# Values that are tuned often are User Parameters or plain stack values editable in the Niagara editor.
RING_MAT = MAT + '.M_PlayerHitRing'
FLOAT = '/Script/Niagara.NiagaraFloat'
COLOR = '/Script/CoreUObject.LinearColor'
VEC3 = '/Script/CoreUObject.Vector3f'
POS = '/Script/Niagara.NiagaraPosition'


def h(expr):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'}, 'value': {'hlslExpression': expr}}


def f(v):
    return {'struct': {'refPath': FLOAT}, 'value': {'value': v}}


def i(v):
    return {'struct': {'refPath': '/Script/Niagara.NiagaraInt32'}, 'value': {'value': v}}


def b(v):
    return {'struct': {'refPath': '/Script/Niagara.NiagaraBool'}, 'value': {'value': -1 if v else 0}}


def col(r, g, bl, a):
    return {'struct': {'refPath': COLOR}, 'value': {'r': r, 'g': g, 'b': bl, 'a': a}}


def link(name, tname):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked'},
            'value': {'linkedVariable': {'name': name, 'type': {'classStructOrEnum': {'refPath': tname}}}}}


def enum(path, value):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum'},
            'value': {'enum': {'refPath': path}, 'enumName': value}}


ONCE = enum('/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions', 'ENiagara_EmitterStateOptions::NewEnumerator1')
DIRECT_COLOR = enum('/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode',
                    'ENiagara_ColorInitializationMode::NewEnumerator1')
SIZE_UNIFORM = enum('/Niagara/Enums/SpriteRenderer/ENiagara_ScaleSpriteSize.ENiagara_ScaleSpriteSize', 'ENiagara_ScaleSpriteSize::NewEnumerator0')


def setin(system, emitter, script, module, names, data):
    if isinstance(names, str):
        names = [names]
    call('SetStackInputData', {'stackInputRef': ref(system, emitter, script, module, names), 'inputData': data})


def module_names(system, emitter):
    r = call('GetEmitterInputValues', {'emitterRef': ref(system, emitter)}, quiet=True)
    try:
        return [m['moduleName'] for m in r['returnValue']]
    except Exception:
        return []


def ensure_module(system, emitter, script, asset, base):
    for n in module_names(system, emitter):
        if n == base or n.startswith(base):
            return n
    r = call('AddModule', {'moduleLocationRef': ref(system, emitter, script), 'moduleAsset': {'refPath': asset}}, quiet=True)
    try:
        return r['returnValue']['moduleName']
    except Exception:
        LOG.append('ADDMODULE FAILED %s %s %s' % (system, emitter, json.dumps(r)[:400]))
        return base


def ensure_set_param(system, emitter, script, attr, tref, default):
    """Adds (once) a SetParameters module writing attr; returns its module name. HLSL is set on it afterwards."""
    data = call('GetEmitterInputValues', {'emitterRef': ref(system, emitter)}, quiet=True)
    for m in (data or {}).get('returnValue', []):
        if any(inp.get('name') == attr for inp in m.get('inputs', [])) and m.get('moduleName', '').startswith('SetVariables'):
            return m['moduleName']
    r = call('AddSetParametersModule', {'moduleLocationRef': ref(system, emitter, script), 'parameters': [
        {'variable': {'name': attr, 'type': {'classStructOrEnum': {'refPath': tref}}}, 'defaultValue': {'struct': {'refPath': tref}, 'value': default}}]})
    try:
        return r['returnValue']['moduleName']
    except Exception:
        LOG.append('SETPARAM FAILED %s %s %s' % (emitter, attr, json.dumps(r)[:400]))
        return None


def sprite_renderer(system, emitter, material, alignment='Automatic', facing='Automatic', sort_order=0):
    r = unreal.ToolsetRegistry.execute_tool(TS, 'GetRendererData', json.dumps({'rendererRef': ref(system, emitter, renderer=0)}))
    v = json.loads(r.get_value_as_json_string())
    if isinstance(v, str):
        v = json.loads(v)
    props = json.loads(v['returnValue']['propertyValues'])
    props['Material'] = {'refPath': material}
    props['Alignment'] = alignment
    props['FacingMode'] = facing
    props['bCastShadows'] = False
    props['SortOrderHint'] = sort_order
    call('SetRendererData', {'renderer': ref(system, emitter, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})


def local_space(system, emitter):
    call('SetEmitterData', {'emitter': ref(system, emitter), 'emitterData': {'propertyValues': json.dumps({'bLocalSpace': True})}})


# ======================= NS_PlayerHitShockwave =======================
S = SHOCK[0]
call('AddUserVariables', {'system': sysref(S), 'variablesToAdd': [
    {'name': 'User.Color', 'description': 'AKI-163 shockwave tint (UPlayerHitReactionComponent::ImpactVFXColor)',
     'type': {'classStructOrEnum': {'refPath': COLOR}}, 'defaultValue': col(1.0, .42, .12, 1.0)},
    {'name': 'User.MaxSize', 'description': 'AKI-163 ring diameter at the end (cm)',
     'type': {'classStructOrEnum': {'refPath': FLOAT}}, 'defaultValue': f(240.0)},
    {'name': 'User.Lifetime', 'description': 'AKI-163 ring lifetime (s)',
     'type': {'classStructOrEnum': {'refPath': FLOAT}}, 'defaultValue': f(0.3)},
]})
for em in SHOCK[1]:
    setin(S, em, 'EmitterUpdateScript', 'EmitterState', 'Loop Behavior', ONCE)
    setin(S, em, 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', f(0.5))
    setin(S, em, 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', i(1))
    setin(S, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color Mode', DIRECT_COLOR)
# Ring: camera-facing ring that bursts out fast (ease-out) and fades; reads from any camera angle and through the retro filter.
setin(S, 'Ring', 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', link('User.Lifetime', FLOAT))
setin(S, 'Ring', 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.Color', COLOR))
setin(S, 'Ring', 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.MaxSize'))
m = ensure_module(S, 'Ring', 'ParticleUpdateScript', '/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize', 'ScaleSpriteSize')
setin(S, 'Ring', 'ParticleUpdateScript', m, 'Scale Sprite Size Mode', SIZE_UNIFORM)
setin(S, 'Ring', 'ParticleUpdateScript', m, 'Uniform Scale Factor', h('lerp(0.12, 1.0, 1.0 - pow(1.0 - Particles.NormalizedAge, 3.0))'))
setin(S, 'Ring', 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('pow(1.0 - Particles.NormalizedAge, 1.6)'))
sprite_renderer(S, 'Ring', RING_MAT, sort_order=1)
# Flash: a white comic "impact star" at the contact point for 0.09 s (same star sprite as the Stage Clear stars).
setin(S, 'Flash', 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(0.09))
setin(S, 'Flash', 'ParticleSpawnScript', 'InitializeParticle', 'Color', col(1.0, .96, .85, 1.0))
setin(S, 'Flash', 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', f(95.0))
m = ensure_module(S, 'Flash', 'ParticleUpdateScript', '/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize', 'ScaleSpriteSize')
setin(S, 'Flash', 'ParticleUpdateScript', m, 'Scale Sprite Size Mode', SIZE_UNIFORM)
setin(S, 'Flash', 'ParticleUpdateScript', m, 'Uniform Scale Factor', h('lerp(0.8, 1.5, Particles.NormalizedAge)'))
setin(S, 'Flash', 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - Particles.NormalizedAge'))
sprite_renderer(S, 'Flash', STAR_MAT, sort_order=2)

# ======================= NS_PlayerStunOrbit =======================
T = STUN[0]
call('AddUserVariables', {'system': sysref(T), 'variablesToAdd': [
    {'name': 'User.Color', 'description': 'AKI-163 star tint (UPlayerHitReactionComponent::StunVFXColor)',
     'type': {'classStructOrEnum': {'refPath': COLOR}}, 'defaultValue': col(1.0, .86, .25, 1.0)},
    {'name': 'User.OrbitRadius', 'description': 'AKI-163 orbit radius around the head (cm)',
     'type': {'classStructOrEnum': {'refPath': FLOAT}}, 'defaultValue': f(24.0)},
    {'name': 'User.OrbitSpeed', 'description': 'AKI-163 orbit speed (rad/s)',
     'type': {'classStructOrEnum': {'refPath': FLOAT}}, 'defaultValue': f(5.0)},
    {'name': 'User.StarSize', 'description': 'AKI-163 star sprite size (cm)',
     'type': {'classStructOrEnum': {'refPath': FLOAT}}, 'defaultValue': f(17.0)},
    {'name': 'User.HaloColor', 'description': 'AKI-163 flat stun-field ring tint',
     'type': {'classStructOrEnum': {'refPath': COLOR}}, 'defaultValue': col(.25, .9, 1.0, .65)},
]})
for idx, em in enumerate(STUN[1]):
    local_space(T, em)
    # One particle per emitter that lives until the C++ kills the component when the stun ends.
    setin(T, em, 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', f(9999.0))
    setin(T, em, 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', i(1))
    setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(9999.0))
    setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color Mode', DIRECT_COLOR)
    if em.startswith('Star'):
        ph = '%.4f' % (idx * 2.0943951)
        setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.Color', COLOR))
        setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.StarSize'))
        mp = ensure_set_param(T, em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0})
        if mp:
            a = '(Emitter.Age * User.OrbitSpeed + %s)' % ph
            setin(T, em, 'ParticleUpdateScript', mp, 'Particles.Position',
                  h('float3(cos(%s) * User.OrbitRadius, sin(%s) * User.OrbitRadius, sin(2.0 * %s) * 3.0)' % (a, a, a)))
        mr = ensure_set_param(T, em, 'ParticleUpdateScript', 'Particles.SpriteRotation', FLOAT, {'value': 0})
        if mr:
            setin(T, em, 'ParticleUpdateScript', mr, 'Particles.SpriteRotation', h('Emitter.Age * 220.0 + %s * 57.3' % ph))
        m = ensure_module(T, em, 'ParticleUpdateScript', '/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize', 'ScaleSpriteSize')
        setin(T, em, 'ParticleUpdateScript', m, 'Scale Sprite Size Mode', SIZE_UNIFORM)
        setin(T, em, 'ParticleUpdateScript', m, 'Uniform Scale Factor', h('saturate(Emitter.Age / 0.12) * (1.0 + 0.18 * sin(Emitter.Age * 14.0 + %s))' % ph))
        setin(T, em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', f(1.0))
        sprite_renderer(T, em, STAR_MAT, sort_order=2)
    else:
        # Halo: flat cyan ring around the orbit, pulsing (sci-fi "stun field"); seen from above and from the side.
        setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.HaloColor', COLOR))
        setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.OrbitRadius * 2.7'))
        setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'UsePositionOffset', b(True))
        setin(T, em, 'ParticleSpawnScript', 'InitializeParticle', 'Position Offset', h('float3(0.0, 0.0, -2.0)'))
        ensure_set_param(T, em, 'ParticleSpawnScript', 'Particles.SpriteFacing', VEC3, {'x': 0, 'y': 0, 'z': 1})
        m = ensure_module(T, em, 'ParticleUpdateScript', '/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize', 'ScaleSpriteSize')
        setin(T, em, 'ParticleUpdateScript', m, 'Scale Sprite Size Mode', SIZE_UNIFORM)
        setin(T, em, 'ParticleUpdateScript', m, 'Uniform Scale Factor', h('saturate(Emitter.Age / 0.15) * (1.0 + 0.07 * sin(Emitter.Age * 9.0))'))
        setin(T, em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('0.7 + 0.3 * sin(Emitter.Age * 9.0)'))
        sprite_renderer(T, em, RING_MAT, alignment='Unaligned', facing='CustomFacingVector', sort_order=0)
