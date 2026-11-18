"""AKI-163 (2nd pass): stylized Red Boar impact shockwave NS_PlayerHitImpact, built with the NiagaraToolsets JSON tools.
The first version (NS_PlayerHitShockwave + M_PlayerHitRing) is kept untouched for comparison (CVar boar.HitVFX.UseLegacy 1).
Usage (editor closed; the step must be inside -script):
  UnrealEditor-Cmd <uproject> -run=pythonscript -script="<this file> <step>" -unattended -nosplash -NullRHI
  steps: build   -> materials + MI + system + all emitter settings (idempotent: re-running re-applies the values)
         inspect -> dump the system
Log: Saved/PlayerHitImpactVFX_<step>.txt

Layout (all world space; the C++ spawns the system at the hit point and sets User.ImpactDirection / User.RingNormal):
  ImpactFlash     0.07 s white -> cream soft flash at the contact point
  ShockwaveRing   the main ring. Sprite faces User.RingNormal (= push direction, tilted a little toward the camera by the C++),
                  so the ring plane is perpendicular to the charge. Hollow centre, sharp outer edge, soft inner trail,
                  angular thickness wobble + slight out-of-round (thickness / wobble: MI_PlayerHitShockRing RingWidth / EdgeNoise).
  ImpactStreaks   User.StreakCount short pointed lines shooting out in the ring plane (slightly pushed along the charge)
  ImpactParticles User.ParticleCount small warm sparks, fast out, ease-out, tiny drop."""
import unreal, json, sys
LOG = []
DIR = '/Game/REBoarGetch/Art/VFX/Player'
TS = 'NiagaraToolsets.NiagaraToolset_System'
STEP = next((a for a in sys.argv[1:] if a in ('build', 'inspect')), 'inspect')
SYS = 'NS_PlayerHitImpact'
EMITTERS = ['ImpactFlash', 'ShockwaveRing', 'ImpactStreaks', 'ImpactParticles']
M_RING, MI_RING = 'M_PlayerHitShockRing', 'MI_PlayerHitShockRing'
M_FLASH, M_STREAK = 'M_PlayerHitFlash', 'M_PlayerHitStreak'

FLOAT = '/Script/Niagara.NiagaraFloat'
INT = '/Script/Niagara.NiagaraInt32'
COLOR = '/Script/CoreUObject.LinearColor'
VEC2 = '/Script/CoreUObject.Vector2f'
VEC3 = '/Script/CoreUObject.Vector3f'
VEC4 = '/Script/CoreUObject.Vector4f'
POS = '/Script/Niagara.NiagaraPosition'


# ------------------------------------------------------------------ tool helpers
def sysref(name):
    return {'refPath': DIR + '/' + name + '.' + name}


def call(tool, args, quiet=False):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    err = r.get_editor_property('error')
    val = r.get_value_as_json_string()
    data = json.loads(val) if val else None
    if isinstance(data, str):
        try:
            data = json.loads(data)
        except Exception:
            pass
    if tool == 'GetEmitterInputValues' and data and not err and not quiet:
        LOG.append('### %s %s' % (tool, args['emitterRef']['emitterName']))
        for m in data['returnValue']:
            LOG.append('  MODULE %s' % m.get('moduleName'))
            for i in m.get('inputs', []):
                LOG.append('     %s = %s' % (i.get('name'), json.dumps(i.get('value'))[:260]))
    elif not quiet or err:
        LOG.append('### %s %s\nERR=%s\n%s' % (tool, json.dumps(args)[:400], err, (val or '')[:1500]))
    return data


def ref(emitter='', script='', module='', inputs=None, renderer=-1):
    return {'system': sysref(SYS), 'emitterName': emitter, 'scriptName': script, 'moduleName': module,
            'rendererIndex': renderer, 'inputNameStack': inputs or []}


def h(expr):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'}, 'value': {'hlslExpression': expr}}


def f(v):
    return {'struct': {'refPath': FLOAT}, 'value': {'value': v}}


def i(v):
    return {'struct': {'refPath': INT}, 'value': {'value': v}}


def col(r, g, bl, a):
    return {'struct': {'refPath': COLOR}, 'value': {'r': r, 'g': g, 'b': bl, 'a': a}}


def v3(x, y, z):
    return {'struct': {'refPath': VEC3}, 'value': {'x': x, 'y': y, 'z': z}}


def link(name, tname):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked'},
            'value': {'linkedVariable': {'name': name, 'type': {'classStructOrEnum': {'refPath': tname}}}}}


def enum(path, value):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum'},
            'value': {'enum': {'refPath': path}, 'enumName': value}}


ONCE = enum('/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions', 'ENiagara_EmitterStateOptions::NewEnumerator1')
DIRECT_COLOR = enum('/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode',
                    'ENiagara_ColorInitializationMode::NewEnumerator1')


def setin(emitter, script, module, name, data):
    call('SetStackInputData', {'stackInputRef': ref(emitter, script, module, [name]), 'inputData': data})


def set_param(emitter, script, attr, tref, default, expr):
    """One Set Parameters module per attribute, appended in call order (later ones may read earlier ones). Idempotent."""
    data = call('GetEmitterInputValues', {'emitterRef': ref(emitter)}, quiet=True)
    mod = None
    for m in (data or {}).get('returnValue', []):
        if m.get('moduleName', '').startswith('SetVariables') and any(inp.get('name') == attr for inp in m.get('inputs', [])):
            # The same attribute can be written in both spawn and update; match the script by trying to set it there.
            r = unreal.ToolsetRegistry.execute_tool(TS, 'SetStackInputData', json.dumps(
                {'stackInputRef': ref(emitter, script, m['moduleName'], [attr]), 'inputData': h(expr)}))
            if not r.get_editor_property('error'):
                return
    r = call('AddSetParametersModule', {'moduleLocationRef': ref(emitter, script), 'parameters': [
        {'variable': {'name': attr, 'type': {'classStructOrEnum': {'refPath': tref}}}, 'defaultValue': {'struct': {'refPath': tref}, 'value': default}}]},
        quiet=True)
    try:
        mod = r['returnValue']['moduleName']
    except Exception:
        LOG.append('SETPARAM FAILED %s %s %s' % (emitter, attr, json.dumps(r)[:400]))
        return
    setin(emitter, script, mod, attr, h(expr))


def renderer(emitter, material, alignment='Automatic', facing='Automatic', sort_order=0):
    r = unreal.ToolsetRegistry.execute_tool(TS, 'GetRendererData', json.dumps({'rendererRef': ref(emitter, renderer=0)}))
    v = json.loads(r.get_value_as_json_string())
    if isinstance(v, str):
        v = json.loads(v)
    props = json.loads(v['returnValue']['propertyValues'])
    props['Material'] = {'refPath': material}
    props['Alignment'] = alignment
    props['FacingMode'] = facing
    props['bCastShadows'] = False
    props['SortOrderHint'] = sort_order
    call('SetRendererData', {'renderer': ref(emitter, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})


# ------------------------------------------------------------------ materials
ML = unreal.MaterialEditingLibrary


def new_material(name):
    path = DIR + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        # delete_all_material_expressions crashes the commandlet (!IsRooted assert). To rebuild a material,
        # delete its .uasset on disk before the run; existing ones are left as they are.
        LOG.append('MATERIAL %s exists, kept' % name)
        return None
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    # Without the Niagara sprite usage flag the renderer silently falls back to the grey default material.
    mat.set_editor_property('used_with_niagara_sprites', True)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    return mat


def X(mat, cls, x, y):
    return ML.create_material_expression(mat, cls, x, y)


def scalar(mat, name, value, x, y):
    e = X(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', value)
    return e


def vector(mat, name, value, x, y):
    e = X(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', unreal.LinearColor(*value))
    return e


def custom(mat, code, names, x, y):
    e = X(mat, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property('code', code)
    e.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    e.set_editor_property('inputs', ins)
    return e


def finish(mat, name, rgba):
    """rgba: Custom node returning float4(emissive, opacity)."""
    rgb = X(mat, unreal.MaterialExpressionComponentMask, 300, -60)
    for k, v in (('r', True), ('g', True), ('b', True), ('a', False)):
        rgb.set_editor_property(k, v)
    a = X(mat, unreal.MaterialExpressionComponentMask, 300, 80)
    for k, v in (('r', False), ('g', False), ('b', False), ('a', True)):
        a.set_editor_property(k, v)
    ML.connect_material_expressions(rgba, '', rgb, '')
    ML.connect_material_expressions(rgba, '', a, '')
    ML.connect_material_property(rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    ML.connect_material_property(a, '', unreal.MaterialProperty.MP_OPACITY)
    ML.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(DIR + '/' + name, only_if_is_dirty=False)
    LOG.append('MATERIAL %s niagara_sprites=%s' % (name, mat.get_editor_property('used_with_niagara_sprites')))


def wire_common(mat, node):
    uv = X(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    pc = X(mat, unreal.MaterialExpressionParticleColor, -900, -60)
    ML.connect_material_expressions(uv, '', node, 'UV')
    ML.connect_material_expressions(pc, '', node, 'C')
    ML.connect_material_expressions(pc, 'A', node, 'A')


RING_CODE = r'''
// AKI-163 stylized impact ring. Age = ParticleRelativeTime (Particles.NormalizedAge), Seed = ParticleRandom (Particles.MaterialRandom),
// Width / Noise = material parameters RingWidth / EdgeNoise (tune them on MI_PlayerHitShockRing).
// (Not a DynamicParameter: its four outputs have empty names, so Python can only wire Param1.)
float2 d = UV - 0.5;
float r = length(d) * 2.0;
float ang = atan2(d.y, d.x);
float age = saturate(Age);
float seed = Seed * 6.2831853;
float n = Noise;
// Slightly out-of-round and uneven thickness so it reads as a hand-drawn shockwave, not a perfect UI circle.
float R = 0.78 + 0.035 * n * sin(ang * 3.0 + seed * 1.7);
float wob = 1.0 + n * (0.6 * sin(ang * 5.0 + seed) + 0.4 * sin(ang * 9.0 - seed * 2.3));
float w = max(Width * wob * lerp(1.7, 0.55, age), 0.004);
float dd = r - R;
// Sharp leading (outer) edge, soft trailing (inner) gradient: gives the feeling of speed. Centre stays clear.
float outer = saturate(1.0 - dd / (w * 0.3));
float inner = saturate(1.0 + dd / w);
float prof = dd > 0.0 ? outer : inner * inner * inner;
// Thin warm outline just outside the bright band: keeps the ring readable over bright skies / sand.
float rim = saturate(1.0 - abs(dd - w * 0.22) / (w * 0.22)) * (1.0 - age * 0.5);
float core = saturate((prof - 0.55) * 2.2);
float3 c = lerp(C.rgb, float3(1.0, 1.0, 1.0), core);
c = lerp(c, Rim.rgb, rim * 0.65);
float alpha = saturate(prof * 0.95 + rim * 0.55) * A;
return float4(c * Boost, alpha);
'''

FLASH_CODE = r'''
// AKI-163 soft impact flash / spark dot: white-hot core, cream halo, faint 4-point glint.
float2 d = UV - 0.5;
float r = saturate(length(d) * 2.0);
float core = pow(1.0 - r, 5.0);
float halo = pow(1.0 - r, 1.8);
float glint = saturate(1.0 - abs(d.x * d.y) * 90.0) * saturate(1.0 - r * 1.1);
float3 c = lerp(C.rgb, float3(1.0, 1.0, 1.0), saturate(core * 1.5));
float alpha = saturate(halo * 0.85 + glint * 0.35 + core) * A;
return float4(c * Boost, alpha);
'''

STREAK_CODE = r'''
// AKI-163 radial impact streak: a pointed spindle (both ends sharp), white core, along the sprite V axis.
float across = abs(UV.x * 2.0 - 1.0);
float along = abs(UV.y * 2.0 - 1.0);
float half_w = 1.0 - along;
float m = saturate((half_w - across) * 3.0);
float core = saturate((half_w * 0.5 - across) * 4.0);
float3 c = lerp(C.rgb, float3(1.0, 1.0, 1.0), core);
return float4(c * Boost, m * A);
'''


def build_materials():
    mat = new_material(M_RING)
    if mat:
        node = custom(mat, RING_CODE, ['UV', 'C', 'A', 'Age', 'Seed', 'Width', 'Noise', 'Rim', 'Boost'], -300, 0)
        wire_common(mat, node)
        ML.connect_material_expressions(X(mat, unreal.MaterialExpressionParticleRelativeTime, -900, 60), '', node, 'Age')
        ML.connect_material_expressions(X(mat, unreal.MaterialExpressionParticleRandom, -900, 120), '', node, 'Seed')
        ML.connect_material_expressions(scalar(mat, 'RingWidth', 0.11, -900, 180), '', node, 'Width')
        ML.connect_material_expressions(scalar(mat, 'EdgeNoise', 0.35, -900, 240), '', node, 'Noise')
        ML.connect_material_expressions(vector(mat, 'RimColor', (1.0, 0.55, 0.18, 1.0), -900, 300), '', node, 'Rim')
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 1.6, -900, 420), '', node, 'Boost')
        finish(mat, M_RING, node)

    mat = new_material(M_FLASH)
    if mat:
        node = custom(mat, FLASH_CODE, ['UV', 'C', 'A', 'Boost'], -300, 0)
        wire_common(mat, node)
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 1.5, -900, 300), '', node, 'Boost')
        finish(mat, M_FLASH, node)

    mat = new_material(M_STREAK)
    if mat:
        node = custom(mat, STREAK_CODE, ['UV', 'C', 'A', 'Boost'], -300, 0)
        wire_common(mat, node)
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 1.5, -900, 300), '', node, 'Boost')
        finish(mat, M_STREAK, node)

    path = DIR + '/' + MI_RING
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MI_RING, DIR, unreal.MaterialInstanceConstant,
                                                                      unreal.MaterialInstanceConstantFactoryNew())
    else:
        mi = unreal.load_asset(path)
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, unreal.load_asset(DIR + '/' + M_RING))
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    LOG.append('MI %s parent=%s' % (MI_RING, mi.get_editor_property('parent').get_name()))


# ------------------------------------------------------------------ system
def build_system():
    path = DIR + '/' + SYS
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        call('CreateNiagaraSystem', {'assetName': SYS, 'assetPath': DIR,
                                     'templateSystem': {'refPath': '/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight'}})
    summ = call('GetSystemSummary', {'system': sysref(SYS)}, quiet=True)
    have = [e.get('emitterName') or e.get('name') for e in (summ or {}).get('returnValue', {}).get('emitters', [])] if summ else []
    for em in EMITTERS:
        if em not in have:
            call('AddEmitter', {'system': sysref(SYS), 'emitterName': em,
                                'templateEmitter': {'refPath': '/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst'}})
    for n in have:
        if n not in EMITTERS:
            call('RemoveEmitter', {'emitterToRemove': ref(n)})

    def uv(name, desc, t, default):
        return {'name': 'User.' + name, 'description': 'AKI-163 ' + desc, 'type': {'classStructOrEnum': {'refPath': t}}, 'defaultValue': default}

    call('AddUserVariables', {'system': sysref(SYS), 'variablesToAdd': [
        uv('Color', 'ring / streak tint (UPlayerHitReactionComponent::ImpactVFXColor)', COLOR, col(1.0, .93, .62, 1.0)),
        uv('FlashColor', 'centre flash tint', COLOR, col(1.0, .97, .84, 1.0)),
        uv('FlashIntensity', 'centre flash brightness multiplier', FLOAT, f(1.4)),
        uv('SparkColor', 'small spark tint', COLOR, col(1.0, .66, .26, 1.0)),
        uv('Scale', 'overall scale (UPlayerHitReactionComponent::ImpactVFXScale)', FLOAT, f(1.0)),
        uv('MaxRadius', 'ring radius at the end of its life (cm)', FLOAT, f(125.0)),
        uv('Lifetime', 'ring lifetime (s); streaks live ~60 %% of it', FLOAT, f(0.22)),
        uv('ExpandPower', 'expansion ease-out exponent; higher = faster initial burst', FLOAT, f(3.0)),
        uv('StreakCount', 'number of radial streaks', INT, i(8)),
        uv('StreakLength', 'streak length (cm)', FLOAT, f(52.0)),
        uv('ParticleCount', 'number of small sparks', INT, i(6)),
        uv('ImpactDirection', 'push direction (world, set by C++)', VEC3, v3(1, 0, 0)),
        uv('RingNormal', 'ring plane normal (world, set by C++: push direction tilted toward the camera)', VEC3, v3(1, 0, 0)),
    ]})
    unreal.EditorAssetLibrary.save_asset(path)


HASH = 'frac(sin((float)Particles.UniqueID * 12.9898 + %s * 78.233 + Engine.Time * 3.71) * 43758.5453)'
NRM = 'normalize(User.RingNormal + float3(0.0001, 0.0, 0.0))'
BASIS_U = 'normalize(abs(%s.z) < 0.95 ? cross(%s, float3(0.0, 0.0, 1.0)) : cross(%s, float3(1.0, 0.0, 0.0)))' % (NRM, NRM, NRM)
DIR_N = 'normalize(User.ImpactDirection + float3(0.0001, 0.0, 0.0))'
T = 'Particles.NormalizedAge'


def common(em, count):
    setin(em, 'EmitterUpdateScript', 'EmitterState', 'Loop Behavior', ONCE)
    setin(em, 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', f(0.5))
    setin(em, 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', count)
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color Mode', DIRECT_COLOR)


def radial(em, forward, up):
    """Per-particle random + an evenly spread direction in the ring plane, nudged along the push direction. Stored in SpriteAlignment."""
    set_param(em, 'ParticleSpawnScript', 'Particles.Rnd', VEC3, {'x': 0, 'y': 0, 'z': 0},
              'float3(%s, %s, %s)' % (HASH % '1.0', HASH % '2.0', HASH % '3.0'))
    set_param(em, 'ParticleSpawnScript', 'Particles.BasisU', VEC3, {'x': 0, 'y': 1, 'z': 0}, BASIS_U)
    set_param(em, 'ParticleSpawnScript', 'Particles.BasisV', VEC3, {'x': 0, 'y': 0, 'z': 1}, 'cross(%s, Particles.BasisU)' % NRM)
    # Consecutive UniqueIDs of one burst give evenly spaced angles; a little jitter keeps it from looking mechanical.
    count = 'User.StreakCount' if em == 'ImpactStreaks' else 'User.ParticleCount'
    a = '(6.2831853 * ((float)Particles.UniqueID + Particles.Rnd.x * 0.45) / max((float)%s, 1.0))' % count
    set_param(em, 'ParticleSpawnScript', 'Particles.SpriteAlignment', VEC3, {'x': 0, 'y': 0, 'z': 1},
              'normalize(cos(%s) * Particles.BasisU + sin(%s) * Particles.BasisV + %s * %s + float3(0.0, 0.0, %s))' % (a, a, DIR_N, forward, up))


def cleanup_dynamic_parameter():
    data = call('GetEmitterInputValues', {'emitterRef': ref('ShockwaveRing')}, quiet=True)
    for m in (data or {}).get('returnValue', []):
        if any(inp.get('name') == 'Particles.DynamicMaterialParameter' for inp in m.get('inputs', [])):
            for script in ('ParticleSpawnScript', 'ParticleUpdateScript'):
                r = call('RemoveModule', {'moduleToRemove': ref('ShockwaveRing', script, m['moduleName'])}, quiet=True)
                LOG.append('REMOVE %s %s -> %s' % (script, m['moduleName'], json.dumps(r)[:300]))
    r = call('RemoveUserVariables', {'system': sysref(SYS), 'variablesToRemove': [
        {'name': 'User.' + n, 'type': {'classStructOrEnum': {'refPath': FLOAT}}} for n in ('RingWidth', 'EdgeNoise')]}, quiet=True)
    LOG.append('REMOVE USER -> %s' % json.dumps(r)[:300])


def build_emitters():
    # ---- ImpactFlash: 0.07 s white/cream flash at the contact point.
    em = 'ImpactFlash'
    common(em, i(1))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(0.07))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color',
          h('User.FlashColor * float4(User.FlashIntensity, User.FlashIntensity, User.FlashIntensity, 1.0)'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('70.0 * User.Scale'))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 70, 'y': 70},
              'float2(1.0, 1.0) * 70.0 * User.Scale * lerp(0.75, 1.3, sqrt(%s))' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('pow(1.0 - %s, 2.0)' % T))
    renderer(em, '%s/%s.%s' % (DIR, M_FLASH, M_FLASH), sort_order=3)

    # ---- ShockwaveRing: faces User.RingNormal, i.e. the ring plane is perpendicular to the charge.
    em = 'ShockwaveRing'
    common(em, i(1))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', link('User.Lifetime', FLOAT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.Color', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('20.0 * User.Scale'))
    set_param(em, 'ParticleSpawnScript', 'Particles.SpriteFacing', VEC3, {'x': 1, 'y': 0, 'z': 0}, NRM)
    # Per-hit seed for the ring wobble (material ParticleRandom). Thickness / wobble live on MI_PlayerHitShockRing.
    set_param(em, 'ParticleSpawnScript', 'Particles.MaterialRandom', FLOAT, {'value': 0}, HASH % '5.0')
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 20, 'y': 20},
              # ring radius sits at 0.78 of the sprite half-size in the material
              'float2(1.0, 1.0) * (2.0 * User.MaxRadius * User.Scale / 0.78) * lerp(0.12, 1.0, 1.0 - pow(1.0 - %s, User.ExpandPower))' % T)
    # Holds full strength for the first half, then drops fast.
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 2.5)' % T))
    renderer(em, '%s/%s.%s' % (DIR, MI_RING, MI_RING), alignment='Unaligned', facing='CustomFacingVector', sort_order=1)

    # ---- ImpactStreaks: short sharp lines radiating in the ring plane; shorter life than the ring.
    em = 'ImpactStreaks'
    common(em, link('User.StreakCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('User.Lifetime * lerp(0.5, 0.72, %s)' % (HASH % '7.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', h('lerp(User.Color, float4(1.0, 1.0, 1.0, 1.0), 0.5)'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('10.0 * User.Scale'))
    radial(em, '0.22', '0.0')
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Engine.Owner.Position + Particles.SpriteAlignment * User.MaxRadius * User.Scale * '
              'lerp(0.18, 0.95 + 0.35 * Particles.Rnd.y, 1.0 - pow(1.0 - %s, 3.0))' % T)
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 6, 'y': 50},
              'float2(lerp(5.0, 9.0, Particles.Rnd.z), User.StreakLength * lerp(0.7, 1.3, Particles.Rnd.y) * lerp(1.0, 0.35, %s)) * User.Scale' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 2.0)' % T))
    renderer(em, '%s/%s.%s' % (DIR, M_STREAK, M_STREAK), alignment='CustomAlignment', facing='FaceCamera', sort_order=2)

    # ---- ImpactParticles: a few warm sparks, fast out and decelerating, mostly forward along the push.
    em = 'ImpactParticles'
    common(em, link('User.ParticleCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('lerp(0.22, 0.34, %s)' % (HASH % '9.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.SparkColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('8.0 * User.Scale'))
    radial(em, '0.6', '0.25')
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Engine.Owner.Position + (Particles.SpriteAlignment * User.MaxRadius * lerp(0.8, 1.25, Particles.Rnd.y) * '
              'lerp(0.12, 1.0, 1.0 - pow(1.0 - %s, 3.0)) - float3(0.0, 0.0, 40.0 * %s * %s)) * User.Scale' % (T, T, T))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 6, 'y': 12},
              'float2(1.0, 2.2) * lerp(5.0, 9.0, Particles.Rnd.z) * User.Scale * (1.0 - 0.6 * %s)' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 2.0)' % T))
    renderer(em, '%s/%s.%s' % (DIR, M_FLASH, M_FLASH), alignment='CustomAlignment', facing='FaceCamera', sort_order=0)

    unreal.EditorAssetLibrary.save_asset(DIR + '/' + SYS, only_if_is_dirty=False)


def dump():
    call('GetSystemSummary', {'system': sysref(SYS)})
    for em in EMITTERS:
        call('GetEmitterInputValues', {'emitterRef': ref(em)})
    call('GetUserVariables', {'system': sysref(SYS)})


if STEP == 'build':
    build_materials()
    build_system()
    cleanup_dynamic_parameter()
    build_emitters()
dump()
open(unreal.Paths.project_saved_dir() + 'PlayerHitImpactVFX_%s.txt' % STEP, 'w', encoding='utf-8').write('\n'.join(LOG))
unreal.log('[PHI] COMPLETE')
