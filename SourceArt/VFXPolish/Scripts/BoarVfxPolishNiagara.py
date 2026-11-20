"""VFX polish pass: footstep / Blue Boar run dust (NS_FootstepDust) and cage break (NS_CageBreak), built with the NiagaraToolsets JSON tools.
The cage-attack hit reuses NS_PlayerHitImpact as is (no new asset); this script only dumps its User parameters for reference.

Usage (editor closed; the step must be inside -script):
  UnrealEditor-Cmd <uproject> -run=pythonscript -script="<this file> <step>" -unattended -nosplash -NullRHI
  (or Tools/Verification/run_ue_py.sh "<this file> build")
  steps: build   -> material instances + both systems + all emitter settings (idempotent: re-running re-applies the values)
         inspect -> dump the systems and their compile state
Log: Saved/BoarVfxPolish_<step>.txt

Shared assets (footstep dust only):
  SM_BoarDustPuff + M_BoarBrakeDust (low-poly lit dust lump, AKI-172 brake dust) -> run dust puffs
  M_PlayerHitStreak (AKI-163 hit impact)                                          -> Blue Boar speed streaks
The cage explosion has its own materials (M_CageExplosion*, built here) and uses /Engine/BasicShapes/Cube for the debris.

NS_FootstepDust (world space; C++ UFootstepDustComponent spawns it at the ground under the foot and sets every User parameter per step):
  DustPuff     User.SpawnCount low-poly lumps kicked backward (-User.MoveDirection) and a little sideways, low, drag stops them, shrink out.
  SpeedStreak  User.StreakCount thin pale lines flowing backward along the ground (0 for the player; Blue Boar only at high speed).

NS_CageBreak (world space; spawned at the cage root by UCageVisualComponent): a dedicated stylized explosion.
  Flash       0.18 s comic starburst flash.            Light      one warm point light fading over 0.5 s.
  Fireball    toon fire puffs (posterized white-hot -> yellow -> orange -> red -> soot), burst out and drift up (~0.4-0.7 s).
  Smoke       light grey-brown cel-shaded puffs rising after the fire (never black), gone by ~1.4 s.
  Sparks      fast hot streaks on ballistic arcs.      Shockwave / AirShock  ground pressure ring + quick centre ring.
  Debris      metal plates / bars blown out and tumbling; a third start as glowing embers that cool down.
  GroundDust  low dusty ring pushed out along the ground."""
import unreal, json, sys
LOG = []
TS = 'NiagaraToolsets.NiagaraToolset_System'
STEP = next((a for a in sys.argv[1:] if a in ('build', 'inspect')), 'inspect')

DIR_COMMON = '/Game/REBoarGetch/Art/VFX/Common'
DIR_CAGE = '/Game/REBoarGetch/Art/VFX/Cage'
DIR_PLAYER = '/Game/REBoarGetch/Art/VFX/Player'
DIR_BOAR = '/Game/REBoarGetch/Art/VFX/Boar'

FOOT = (DIR_COMMON, 'NS_FootstepDust')
CAGE = (DIR_CAGE, 'NS_CageBreak')
HIT = (DIR_PLAYER, 'NS_PlayerHitImpact')
FOOT_EMITTERS = ['DustPuff', 'SpeedStreak']
CAGE_EMITTERS = ['Flash', 'Light', 'Fireball', 'Smoke', 'Sparks', 'Shockwave', 'AirShock', 'Debris', 'GroundDust']
MESH_EMITTERS = {'DustPuff', 'Debris'}

DUST_MESH = DIR_BOAR + '/SM_BoarDustPuff.SM_BoarDustPuff'
DUST_MAT = DIR_BOAR + '/M_BoarBrakeDust.M_BoarBrakeDust'
CUBE_MESH = '/Engine/BasicShapes/Cube.Cube'
M_STREAK = DIR_PLAYER + '/M_PlayerHitStreak.M_PlayerHitStreak'

FLOAT = '/Script/Niagara.NiagaraFloat'
INT = '/Script/Niagara.NiagaraInt32'
COLOR = '/Script/CoreUObject.LinearColor'
VEC2 = '/Script/CoreUObject.Vector2f'
VEC3 = '/Script/CoreUObject.Vector3f'
POS = '/Script/Niagara.NiagaraPosition'

CUR = {'sys': FOOT}


# ------------------------------------------------------------------ tool helpers
def sysref(target=None):
    d, n = target or CUR['sys']
    return {'refPath': d + '/' + n + '.' + n}


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
            for inp in m.get('inputs', []):
                v = json.dumps(inp.get('value'))
                if 'Unsupported' not in v:
                    LOG.append('     %s = %s' % (inp.get('name'), v[:220]))
    elif not quiet or err:
        LOG.append('### %s %s\nERR=%s\n%s' % (tool, json.dumps(args)[:400], err, (val or '')[:2500]))
    return data


def ref(emitter='', script='', module='', inputs=None, renderer=-1):
    return {'system': sysref(), 'emitterName': emitter, 'scriptName': script, 'moduleName': module,
            'rendererIndex': renderer, 'inputNameStack': inputs or []}


def h(expr):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'}, 'value': {'hlslExpression': expr}}


def f(v):
    return {'struct': {'refPath': FLOAT}, 'value': {'value': v}}


def i(v):
    return {'struct': {'refPath': INT}, 'value': {'value': v}}


def col(r, g, bl, a=1.0):
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
    for m in (data or {}).get('returnValue', []):
        if m.get('moduleName', '').startswith('SetVariables') and any(inp.get('name') == attr for inp in m.get('inputs', [])):
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


def renderer_props(emitter):
    r = unreal.ToolsetRegistry.execute_tool(TS, 'GetRendererData', json.dumps({'rendererRef': ref(emitter, renderer=0)}))
    v = json.loads(r.get_value_as_json_string())
    if isinstance(v, str):
        v = json.loads(v)
    return json.loads(v['returnValue']['propertyValues'])


def sprite_renderer(emitter, material, alignment='Unaligned', facing='FaceCamera', sort_order=0):
    props = renderer_props(emitter)
    props['Material'] = {'refPath': material}
    props['Alignment'] = alignment
    props['FacingMode'] = facing
    props['bCastShadows'] = False
    props['SortOrderHint'] = sort_order
    call('SetRendererData', {'renderer': ref(emitter, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})


def mesh_renderer(emitter, mesh, material):
    props = renderer_props(emitter)
    m0 = props['Meshes'][0]
    m0['mesh'] = {'refPath': mesh}
    props['Meshes'] = [m0]
    props['bOverrideMaterials'] = True
    om = props['OverrideMaterials'][0]
    om['explicitMat'] = {'refPath': material}
    props['OverrideMaterials'] = [om]
    props['bCastShadows'] = False
    call('SetRendererData', {'renderer': ref(emitter, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})


def uv(name, desc, t, default):
    return {'name': 'User.' + name, 'description': desc, 'type': {'classStructOrEnum': {'refPath': t}}, 'defaultValue': default}


def ensure_system(target, emitters, mesh_emitters):
    CUR['sys'] = target
    d, n = target
    if not unreal.EditorAssetLibrary.does_asset_exist(d + '/' + n):
        call('CreateNiagaraSystem', {'assetName': n, 'assetPath': d,
                                     'templateSystem': {'refPath': '/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight'}})
    summ = call('GetSystemSummary', {'system': sysref()}, quiet=True)
    have = [e.get('emitterName') or e.get('name') for e in (summ or {}).get('returnValue', {}).get('emitters', [])] if summ else []
    for em in emitters:
        if em not in have:
            tmpl = 'UpwardMeshBurst.UpwardMeshBurst' if em in mesh_emitters else 'SimpleSpriteBurst.SimpleSpriteBurst'
            call('AddEmitter', {'system': sysref(), 'emitterName': em,
                                'templateEmitter': {'refPath': '/Niagara/DefaultAssets/Templates/Emitters/' + tmpl}})
    for x in have:
        if x not in emitters:
            call('RemoveEmitter', {'emitterToRemove': ref(x)})


HASH = 'frac(sin((float)Particles.UniqueID * 12.9898 + %s * 78.233 + Engine.Time * 3.71) * 43758.5453)'
T = 'Particles.NormalizedAge'


def rnd(em):
    set_param(em, 'ParticleSpawnScript', 'Particles.Rnd', VEC3, {'x': 0, 'y': 0, 'z': 0},
              'float3(%s, %s, %s)' % (HASH % '1.0', HASH % '2.0', HASH % '3.0'))


def once(em, count, loop=1.5):
    setin(em, 'EmitterUpdateScript', 'EmitterState', 'Loop Behavior', ONCE)
    setin(em, 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', f(loop))
    setin(em, 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', count)
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color Mode', DIRECT_COLOR)


# ------------------------------------------------------------------ explosion materials (dedicated, NS_CageBreak only)
# Stylized cartoon / PS1 explosion: posterized temperature bands, lumpy billow outlines that erode with age, comic starburst flash.
# Inputs come from ParticleColor (tint + alpha), ParticleRelativeTime (Age) and ParticleRandom (Seed, from Particles.MaterialRandom).
# A material is only created when missing; to rebuild one, delete its .uasset on disk first (delete_all_material_expressions crashes).
ML = unreal.MaterialEditingLibrary
M_EXP_FLASH, M_EXP_FIRE, M_EXP_SMOKE = 'M_CageExplosionFlash', 'M_CageExplosionFire', 'M_CageExplosionSmoke'
M_EXP_SPARK, M_EXP_SHOCK, M_EXP_DEBRIS = 'M_CageExplosionSpark', 'M_CageExplosionShock', 'M_CageExplosionDebris'


def mpath(name):
    return DIR_CAGE + '/' + name + '.' + name


def new_material(name, sprite=True):
    path = DIR_CAGE + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        LOG.append('MATERIAL %s exists, kept' % name)
        return None
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR_CAGE, unreal.Material, unreal.MaterialFactoryNew())
    if sprite:
        # Without the Niagara sprite usage flag the renderer silently falls back to the grey default material.
        mat.set_editor_property('used_with_niagara_sprites', True)
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property('two_sided', True)
    else:
        mat.set_editor_property('used_with_niagara_mesh_particles', True)
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
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


def custom(mat, code, names, x, y, out=None):
    e = X(mat, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property('code', code)
    e.set_editor_property('output_type', out or unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    e.set_editor_property('inputs', ins)
    return e


def wire_common(mat, node, age=False, seed=False):
    uvn = X(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    pc = X(mat, unreal.MaterialExpressionParticleColor, -900, -60)
    ML.connect_material_expressions(uvn, '', node, 'UV')
    ML.connect_material_expressions(pc, '', node, 'C')
    ML.connect_material_expressions(pc, 'A', node, 'A')
    if age:
        ML.connect_material_expressions(X(mat, unreal.MaterialExpressionParticleRelativeTime, -900, 60), '', node, 'Age')
    if seed:
        ML.connect_material_expressions(X(mat, unreal.MaterialExpressionParticleRandom, -900, 120), '', node, 'Seed')


def finish(mat, name, rgba):
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
    unreal.EditorAssetLibrary.save_asset(DIR_CAGE + '/' + name, only_if_is_dirty=False)
    LOG.append('MATERIAL %s created' % name)


FLASH_CODE = r'''
// Cage explosion flash: white-hot core, warm halo and a comic 8-ray starburst of uneven length.
float2 d = UV - 0.5;
float r = length(d) * 2.0;
float ang = atan2(d.y, d.x);
float s = Seed * 6.2831853;
float core = pow(saturate(1.0 - r), 4.0);
float halo = pow(saturate(1.0 - r), 1.6);
float ray = pow(saturate(abs(cos(ang * 4.0 + s))), 24.0);
float len = 0.72 + 0.28 * sin(ang * 3.0 + s * 2.0);
float rays = ray * saturate(1.0 - r / len);
float3 c = lerp(C, float3(1.0, 1.0, 1.0), saturate(core * 2.0 + rays * 0.5));
float alpha = saturate(core * 1.5 + halo * 0.55 + rays * 0.9) * A;
return float4(c * Boost, alpha);
'''

FIRE_CODE = r'''
// Cage explosion fireball puff: lumpy cartoon billow, posterized temperature bands (white-hot -> yellow -> orange -> red -> soot)
// that cool from the outside in while noise eats holes into the puff. Age = NormalizedAge, Seed = MaterialRandom.
float2 d = UV - 0.5;
float r = length(d) * 2.0;
float ang = atan2(d.y, d.x);
float s = Seed * 6.2831853;
float age = saturate(Age);
float lobe = 0.6 * sin(ang * 5.0 + s) + 0.4 * sin(ang * 3.0 - s * 1.7);
float radius = 0.74 + 0.12 * lobe;
float n = 0.5 + 0.25 * sin(d.x * 13.0 + s * 3.1) * sin(d.y * 11.0 - s * 2.3) + 0.25 * sin((d.x + d.y) * 7.0 + s);
float dens = saturate(1.0 - r / radius);
float field = dens * 1.7 + n * 0.55 - age * 1.25;
float alpha = saturate(field * 7.0);
float heat = saturate(dens * 1.4 + n * 0.35 - age * 1.35);
float3 c = Soot.rgb;
c = heat > 0.12 ? Red.rgb : c;
c = heat > 0.38 ? Orange.rgb : c;
c = heat > 0.62 ? Yellow.rgb : c;
c = heat > 0.85 ? float3(1.0, 1.0, 0.92) : c;
float glow = heat > 0.12 ? Boost : 1.0;
return float4(c * C * glow, alpha * A);
'''

SMOKE_CODE = r'''
// Cage explosion smoke puff: light grey-brown (never black), cel-shaded ball lit from the top-left in 3 bands,
// still glowing warm right after the fire, eroding away with age.
float2 d = UV - 0.5;
float r = length(d) * 2.0;
float ang = atan2(d.y, d.x);
float s = Seed * 6.2831853;
float age = saturate(Age);
float lobe = 0.6 * sin(ang * 4.0 + s) + 0.4 * sin(ang * 7.0 - s * 1.3);
float radius = 0.78 + 0.1 * lobe;
float n = 0.5 + 0.25 * sin(d.x * 9.0 + s * 2.1) * sin(d.y * 10.0 - s * 1.4) + 0.25 * sin((d.x - d.y) * 6.0 + s * 0.7);
float dens = saturate(1.0 - r / radius);
float field = dens * 1.5 + n * 0.5 - age * 1.05;
float alpha = saturate(field * 6.0);
float z = sqrt(saturate(1.0 - r * r));
float3 N = normalize(float3(d.x * 2.0, -d.y * 2.0, z + 0.001));
float l = saturate(dot(N, normalize(float3(-0.45, 0.65, 0.6))) * 0.5 + 0.5);
l = saturate(floor(l * 3.0) / 2.0);
float3 c = lerp(Shadow.rgb, Light.rgb, l);
c = lerp(c, Warm.rgb, saturate(1.0 - age * 4.0) * 0.6);
return float4(c * C, alpha * A * Opacity);
'''

SPARK_CODE = r'''
// Cage explosion spark: hot spindle streak along the sprite V axis (velocity aligned), white core, tinted rim.
float across = abs(UV.x * 2.0 - 1.0);
float w = saturate(1.0 - abs(UV.y * 2.0 - 1.0));
float m = saturate((w * 0.9 - across) * 4.0);
float core = saturate((w * 0.4 - across) * 6.0);
float3 c = lerp(C, float3(1.0, 0.97, 0.85), core);
return float4(c * Boost, m * A);
'''

SHOCK_CODE = r'''
// Cage explosion shockwave: thin bright pressure ring that sharpens as it expands, faint haze inside.
float2 d = UV - 0.5;
float r = length(d) * 2.0;
float age = saturate(Age);
float R = 0.8;
float w = lerp(0.14, 0.04, age);
float ring = saturate(1.0 - abs(r - R) / w);
ring = ring * ring;
float haze = saturate(1.0 - r / R) * 0.22 * (1.0 - age);
float3 c = lerp(C, float3(1.0, 1.0, 1.0), ring * 0.6);
return float4(c * Boost, saturate(ring + haze) * A);
'''

DEBRIS_CODE = r'''
// Cage explosion debris: hot fragments (particle alpha < 1) glow as embers and cool down as the alpha rises to 1.
return C * saturate(1.0 - A) * Ember;
'''


def build_materials():
    mat = new_material(M_EXP_FLASH)
    if mat:
        node = custom(mat, FLASH_CODE, ['UV', 'C', 'A', 'Seed', 'Boost'], -300, 0)
        wire_common(mat, node, seed=True)
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 3.0, -900, 300), '', node, 'Boost')
        finish(mat, M_EXP_FLASH, node)

    mat = new_material(M_EXP_FIRE)
    if mat:
        node = custom(mat, FIRE_CODE, ['UV', 'C', 'A', 'Age', 'Seed', 'Yellow', 'Orange', 'Red', 'Soot', 'Boost'], -300, 0)
        wire_common(mat, node, age=True, seed=True)
        ML.connect_material_expressions(vector(mat, 'YellowColor', (1.0, 0.85, 0.30, 1.0), -900, 180), '', node, 'Yellow')
        ML.connect_material_expressions(vector(mat, 'OrangeColor', (1.0, 0.50, 0.08, 1.0), -900, 240), '', node, 'Orange')
        ML.connect_material_expressions(vector(mat, 'RedColor', (0.90, 0.22, 0.05, 1.0), -900, 300), '', node, 'Red')
        ML.connect_material_expressions(vector(mat, 'SootColor', (0.34, 0.29, 0.26, 1.0), -900, 360), '', node, 'Soot')
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 2.2, -900, 420), '', node, 'Boost')
        finish(mat, M_EXP_FIRE, node)

    mat = new_material(M_EXP_SMOKE)
    if mat:
        node = custom(mat, SMOKE_CODE, ['UV', 'C', 'A', 'Age', 'Seed', 'Light', 'Shadow', 'Warm', 'Opacity'], -300, 0)
        wire_common(mat, node, age=True, seed=True)
        ML.connect_material_expressions(vector(mat, 'LightColor', (0.70, 0.66, 0.60, 1.0), -900, 180), '', node, 'Light')
        ML.connect_material_expressions(vector(mat, 'ShadowColor', (0.40, 0.37, 0.35, 1.0), -900, 240), '', node, 'Shadow')
        ML.connect_material_expressions(vector(mat, 'WarmColor', (1.0, 0.55, 0.22, 1.0), -900, 300), '', node, 'Warm')
        ML.connect_material_expressions(scalar(mat, 'SmokeOpacity', 0.9, -900, 360), '', node, 'Opacity')
        finish(mat, M_EXP_SMOKE, node)

    mat = new_material(M_EXP_SPARK)
    if mat:
        node = custom(mat, SPARK_CODE, ['UV', 'C', 'A', 'Boost'], -300, 0)
        wire_common(mat, node)
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 3.0, -900, 300), '', node, 'Boost')
        finish(mat, M_EXP_SPARK, node)

    mat = new_material(M_EXP_SHOCK)
    if mat:
        node = custom(mat, SHOCK_CODE, ['UV', 'C', 'A', 'Age', 'Boost'], -300, 0)
        wire_common(mat, node, age=True)
        ML.connect_material_expressions(scalar(mat, 'EmissiveBoost', 1.6, -900, 300), '', node, 'Boost')
        finish(mat, M_EXP_SHOCK, node)

    mat = new_material(M_EXP_DEBRIS, sprite=False)
    if mat:
        pc = X(mat, unreal.MaterialExpressionParticleColor, -700, 0)
        ML.connect_material_property(pc, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
        rough = X(mat, unreal.MaterialExpressionConstant, -700, 200)
        rough.set_editor_property('r', 0.6)
        ML.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
        node = custom(mat, DEBRIS_CODE, ['C', 'A', 'Ember'], -300, 100, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        ML.connect_material_expressions(pc, 'RGB', node, 'C')
        ML.connect_material_expressions(pc, 'A', node, 'A')
        ML.connect_material_expressions(scalar(mat, 'EmberBoost', 6.0, -700, 300), '', node, 'Ember')
        ML.connect_material_property(node, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        ML.recompile_material(mat)
        unreal.EditorAssetLibrary.save_asset(DIR_CAGE + '/' + M_EXP_DEBRIS, only_if_is_dirty=False)
        LOG.append('MATERIAL %s created' % M_EXP_DEBRIS)


# ------------------------------------------------------------------ NS_FootstepDust
BACK = '(-normalize(float3(User.MoveDirection.x, User.MoveDirection.y, 0.0) + float3(0.0001, 0.0, 0.0)))'
SIDE = 'normalize(cross(float3(0.0, 0.0, 1.0), %s))' % BACK


def build_footstep():
    ensure_system(FOOT, FOOT_EMITTERS, MESH_EMITTERS)
    call('AddUserVariables', {'system': sysref(), 'variablesToAdd': [
        uv('SpawnCount', 'dust lumps per step (UFootstepDustComponent)', INT, i(5)),
        uv('SizeScale', 'largest lump scale (1 = 12 cm radius)', FLOAT, f(0.55)),
        uv('Lifetime', 'longest lump lifetime (s); shortest is 55 %', FLOAT, f(0.38)),
        uv('DustColor', 'dust tint', COLOR, col(0.72, 0.58, 0.40)),
        uv('KickSpeed', 'backward kick speed (cm/s)', FLOAT, f(110.0)),
        uv('KickUp', 'upward kick speed (cm/s); keep low so the dust hugs the ground', FLOAT, f(45.0)),
        uv('Spread', 'sideways spread as a fraction of KickSpeed', FLOAT, f(0.6)),
        uv('MoveDirection', 'horizontal movement direction (world, set by C++); dust flows the other way', VEC3, v3(1, 0, 0)),
        uv('StreakCount', 'thin speed streaks per step (0 = none; Blue Boar at speed)', INT, i(0)),
        uv('StreakLength', 'speed streak length (cm)', FLOAT, f(70.0)),
        uv('StreakColor', 'speed streak tint (alpha = opacity)', COLOR, col(0.95, 0.88, 0.70, 0.5)),
    ]})

    em = 'DustPuff'
    once(em, link('User.SpawnCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Min', h('User.Lifetime * 0.55'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Max', link('User.Lifetime', FLOAT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.DustColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Mesh Uniform Scale Min', h('User.SizeScale * 0.45'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Mesh Uniform Scale Max', link('User.SizeScale', FLOAT))
    rnd(em)
    # Start right at the foot, a few cm around it.
    set_param(em, 'ParticleSpawnScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Engine.Owner.Position + %s * (Particles.Rnd.y - 0.5) * 14.0 + float3(0.0, 0.0, 3.0)' % SIDE)
    # Kicked backward and a little sideways, low (overrides the template cone).
    set_param(em, 'ParticleSpawnScript', 'Particles.Velocity', VEC3, {'x': 0, 'y': 0, 'z': 0},
              '%s * User.KickSpeed * lerp(0.35, 1.0, Particles.Rnd.x) + %s * User.KickSpeed * User.Spread * (Particles.Rnd.y * 2.0 - 1.0)'
              ' + float3(0.0, 0.0, User.KickUp * lerp(0.4, 1.0, Particles.Rnd.z))' % (BACK, SIDE))
    setin(em, 'ParticleUpdateScript', 'GravityForce', 'Gravity', v3(0, 0, -220))
    setin(em, 'ParticleUpdateScript', 'Drag', 'Drag', f(6.0))
    mesh_renderer(em, DUST_MESH, DUST_MAT)

    em = 'SpeedStreak'
    once(em, link('User.StreakCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('lerp(0.14, 0.22, %s)' % (HASH % '7.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.StreakColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', f(6.0))
    rnd(em)
    set_param(em, 'ParticleSpawnScript', 'Particles.StartOffset', VEC3, {'x': 0, 'y': 0, 'z': 0},
              '%s * (Particles.Rnd.y - 0.5) * 34.0 + float3(0.0, 0.0, lerp(6.0, 22.0, Particles.Rnd.z))' % SIDE)
    set_param(em, 'ParticleSpawnScript', 'Particles.SpriteAlignment', VEC3, {'x': 1, 'y': 0, 'z': 0}, BACK)
    # Flows backward from the foot and eases out: reads as wind behind a fast runner, not as a projectile.
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Engine.Owner.Position + Particles.StartOffset + %s * User.StreakLength * lerp(0.25, 1.15, 1.0 - pow(1.0 - %s, 2.0))' % (BACK, T))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 4, 'y': 40},
              'float2(lerp(3.0, 5.0, Particles.Rnd.x), User.StreakLength * lerp(0.45, 0.7, Particles.Rnd.y) * lerp(1.0, 0.4, %s))' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 2.0)' % T))
    sprite_renderer(em, M_STREAK, alignment='CustomAlignment', facing='FaceCamera', sort_order=1)

    unreal.EditorAssetLibrary.save_asset(FOOT[0] + '/' + FOOT[1], only_if_is_dirty=False)


# ------------------------------------------------------------------ NS_CageBreak (explosion)
CENTER = 'Engine.Owner.Position + float3(0.0, 0.0, User.CenterHeight * User.Scale)'
AGE = 'Particles.Age'


def sprite_rot(em, spin):
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteRotation', FLOAT, {'value': 0},
              'Particles.Rnd.z * 360.0 + %s * lerp(-%s, %s, Particles.Rnd.x)' % (AGE, spin, spin))


def build_cage():
    ensure_system(CAGE, CAGE_EMITTERS, MESH_EMITTERS)
    call('AddUserVariables', {'system': sysref(), 'variablesToAdd': [
        uv('Scale', 'overall scale (UCageVisualComponent::BreakVFXScale)', FLOAT, f(1.0)),
        uv('CenterHeight', 'explosion centre height above the cage root (cm, before Scale)', FLOAT, f(150.0)),
        uv('FootprintRadius', 'cage footprint radius (cm, before Scale)', FLOAT, f(190.0)),
        uv('FlashColor', 'flash tint', COLOR, col(1.0, 0.92, 0.70)),
        uv('FlashSize', 'flash sprite size (cm)', FLOAT, f(620.0)),
        uv('LightColor', 'explosion light colour', COLOR, col(1.0, 0.62, 0.28)),
        uv('LightIntensity', 'explosion light brightness multiplier', FLOAT, f(40.0)),
        uv('LightRadius', 'explosion light radius (cm)', FLOAT, f(1400.0)),
        uv('FireCount', 'number of fireball puffs', INT, i(12)),
        uv('FireSize', 'fireball puff size (cm)', FLOAT, f(240.0)),
        uv('FireSpeed', 'fireball outward speed (cm/s)', FLOAT, f(420.0)),
        uv('FireLifetime', 'longest fireball lifetime (s)', FLOAT, f(0.7)),
        uv('SmokeCount', 'number of smoke puffs (light grey-brown, short)', INT, i(10)),
        uv('SmokeSize', 'smoke puff size (cm)', FLOAT, f(300.0)),
        uv('SmokeLifetime', 'longest smoke lifetime (s)', FLOAT, f(1.4)),
        uv('SmokeTint', 'smoke tint (multiplies the grey material)', COLOR, col(1.0, 0.97, 0.92)),
        uv('SparkCount', 'number of sparks', INT, i(28)),
        uv('SparkColor', 'spark tint', COLOR, col(1.0, 0.70, 0.25)),
        uv('SparkSpeed', 'spark speed (cm/s)', FLOAT, f(1300.0)),
        uv('DebrisCount', 'number of cage fragments', INT, i(16)),
        uv('DebrisColorA', 'fragment colour A (frame metal)', COLOR, col(0.42, 0.47, 0.53)),
        uv('DebrisColorB', 'fragment colour B (light panel)', COLOR, col(0.82, 0.84, 0.86)),
        uv('DebrisSpeed', 'fragment outward speed (cm/s)', FLOAT, f(620.0)),
        uv('DebrisLifetime', 'longest fragment lifetime (s)', FLOAT, f(1.3)),
        uv('ShockColor', 'shockwave tint', COLOR, col(1.0, 0.90, 0.72)),
        uv('ShockRadius', 'ground shockwave end radius (cm)', FLOAT, f(620.0)),
        uv('DustCount', 'number of ground dust puffs', INT, i(14)),
        uv('DustColor', 'ground dust tint (multiplies the grey smoke material)', COLOR, col(1.25, 1.02, 0.74)),
        uv('DustSpeed', 'ground dust outward speed (cm/s)', FLOAT, f(520.0)),
    ]})

    # ---- Flash: comic starburst, 0.18 s
    em = 'Flash'
    once(em, i(1))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(0.18))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.FlashColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.FlashSize * User.Scale'))
    set_param(em, 'ParticleSpawnScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0}, CENTER)
    set_param(em, 'ParticleSpawnScript', 'Particles.MaterialRandom', FLOAT, {'value': 0}, HASH % '5.0')
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 500, 'y': 500},
              'float2(1.0, 1.0) * User.FlashSize * User.Scale * lerp(0.55, 1.3, sqrt(%s))' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('pow(1.0 - %s, 1.5)' % T))
    sprite_renderer(em, mpath(M_EXP_FLASH), sort_order=7)

    # ---- Light: one bright warm point light that lights the ground and nearby props, fading in 0.5 s
    em = 'Light'
    once(em, i(1))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(0.5))
    set_param(em, 'ParticleSpawnScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0}, CENTER)
    set_param(em, 'ParticleUpdateScript', 'Particles.Color', COLOR, {'r': 1, 'g': 1, 'b': 1, 'a': 1},
              'float4(User.LightColor.rgb * User.LightIntensity * pow(1.0 - %s, 2.0), 1.0)' % T)
    set_param(em, 'ParticleUpdateScript', 'Particles.LightRadius', FLOAT, {'value': 1000},
              'User.LightRadius * User.Scale * lerp(1.0, 0.65, %s)' % T)
    call('AddRenderer', {'newRendererLocation': ref(em), 'rendererClass': {'refPath': '/Script/Niagara.NiagaraLightRendererProperties'}})
    call('RemoveRenderer', {'rendererToRemove': ref(em, renderer=0)})
    try:
        props = renderer_props(em)
        LOG.append('LIGHT PROPS %s' % json.dumps(props)[:1500])
        for k, v in (('bUseInverseSquaredFalloff', False), ('bAffectsTranslucency', True), ('RadiusScale', 1.0), ('DefaultExponent', 1.5)):
            if k in props:
                props[k] = v
        call('SetRendererData', {'renderer': ref(em, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})
    except Exception as e:
        LOG.append('LIGHT PROPS FAILED %s' % e)

    # ---- Fireball: lumpy toon fire puffs bursting outward (decelerating) and drifting up
    em = 'Fireball'
    once(em, link('User.FireCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('User.FireLifetime * lerp(0.6, 1.0, %s)' % (HASH % '7.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', col(1.0, 1.0, 1.0, 1.0))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.FireSize * User.Scale * 0.4'))
    rnd(em)
    set_param(em, 'ParticleSpawnScript', 'Particles.Dir', VEC3, {'x': 0, 'y': 0, 'z': 1},
              'normalize(Particles.Rnd * 2.0 - 1.0 + float3(0.0, 0.0, 0.45) + float3(0.0001, 0.0, 0.0))')
    set_param(em, 'ParticleSpawnScript', 'Particles.StartPos', VEC3, {'x': 0, 'y': 0, 'z': 0},
              '%s + Particles.Dir * User.FootprintRadius * User.Scale * 0.3 * Particles.Rnd.y' % CENTER)
    set_param(em, 'ParticleSpawnScript', 'Particles.MaterialRandom', FLOAT, {'value': 0}, HASH % '8.0')
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Particles.StartPos + Particles.Dir * User.FireSpeed * User.Scale * lerp(0.5, 1.1, Particles.Rnd.x) * (1.0 - exp(-%s * 4.5)) / 4.5'
              ' + float3(0.0, 0.0, 90.0 * %s * %s) * User.Scale' % (AGE, AGE, AGE))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 200, 'y': 200},
              'float2(1.0, 1.0) * User.FireSize * User.Scale * lerp(0.75, 1.2, Particles.Rnd.z) * lerp(0.4, 1.0, 1.0 - pow(1.0 - %s, 3.0))' % T)
    sprite_rot(em, '60.0')
    sprite_renderer(em, mpath(M_EXP_FIRE), sort_order=5)

    # ---- Smoke: light grey-brown puffs rising after the fire, gone in ~1.4 s (never black, never lingering)
    em = 'Smoke'
    once(em, link('User.SmokeCount', INT))
    setin(em, 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Time', f(0.07))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('User.SmokeLifetime * lerp(0.65, 1.0, %s)' % (HASH % '9.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.SmokeTint', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.SmokeSize * User.Scale * 0.5'))
    rnd(em)
    set_param(em, 'ParticleSpawnScript', 'Particles.Dir', VEC3, {'x': 0, 'y': 0, 'z': 1},
              'normalize(float3(Particles.Rnd.x * 2.0 - 1.0, Particles.Rnd.y * 2.0 - 1.0, 0.6) + float3(0.0001, 0.0, 0.0))')
    set_param(em, 'ParticleSpawnScript', 'Particles.StartPos', VEC3, {'x': 0, 'y': 0, 'z': 0},
              '%s + Particles.Dir * User.FootprintRadius * User.Scale * 0.35' % CENTER)
    set_param(em, 'ParticleSpawnScript', 'Particles.MaterialRandom', FLOAT, {'value': 0}, HASH % '10.0')
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Particles.StartPos + Particles.Dir * 260.0 * User.Scale * (1.0 - exp(-%s * 2.5)) / 2.5'
              ' + float3(0.0, 0.0, lerp(80.0, 170.0, Particles.Rnd.z) * %s) * User.Scale' % (AGE, AGE))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 250, 'y': 250},
              'float2(1.0, 1.0) * User.SmokeSize * User.Scale * lerp(0.7, 1.15, Particles.Rnd.z) * lerp(0.5, 1.15, 1.0 - pow(1.0 - %s, 2.0))' % T)
    sprite_rot(em, '25.0')
    sprite_renderer(em, mpath(M_EXP_SMOKE), sort_order=3)

    # ---- Sparks: fast hot streaks on ballistic arcs
    em = 'Sparks'
    once(em, link('User.SparkCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('lerp(0.35, 0.75, %s)' % (HASH % '11.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.SparkColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', f(6.0))
    rnd(em)
    set_param(em, 'ParticleSpawnScript', 'Particles.V0', VEC3, {'x': 0, 'y': 0, 'z': 0},
              'normalize(Particles.Rnd * 2.0 - 1.0 + float3(0.0, 0.0, 0.7) + float3(0.0001, 0.0, 0.0)) * User.SparkSpeed * User.Scale * lerp(0.45, 1.0, %s)' % (HASH % '12.0'))
    set_param(em, 'ParticleSpawnScript', 'Particles.StartPos', VEC3, {'x': 0, 'y': 0, 'z': 0}, CENTER)
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Particles.StartPos + Particles.V0 * %s * (1.0 - 0.35 * %s) + float3(0.0, 0.0, -0.5 * 1400.0 * User.Scale * %s * %s)' % (AGE, T, AGE, AGE))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteAlignment', VEC3, {'x': 0, 'y': 0, 'z': 1},
              'normalize(Particles.V0 - float3(0.0, 0.0, 1400.0 * User.Scale * %s) + float3(0.0001, 0.0, 0.0))' % AGE)
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 6, 'y': 50},
              'float2(lerp(5.0, 8.0, Particles.Rnd.y), lerp(40.0, 85.0, Particles.Rnd.z) * (1.0 - 0.7 * %s)) * User.Scale' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 3.0)' % T))
    sprite_renderer(em, mpath(M_EXP_SPARK), alignment='CustomAlignment', facing='FaceCamera', sort_order=6)

    # ---- Shockwave: flat pressure ring racing over the ground
    em = 'Shockwave'
    once(em, i(1))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(0.38))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.ShockColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('80.0 * User.Scale'))
    set_param(em, 'ParticleSpawnScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0}, 'Engine.Owner.Position + float3(0.0, 0.0, 8.0)')
    set_param(em, 'ParticleSpawnScript', 'Particles.SpriteFacing', VEC3, {'x': 0, 'y': 0, 'z': 1}, 'float3(0.0, 0.0, 1.0)')
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 80, 'y': 80},
              'float2(1.0, 1.0) * (2.0 * User.ShockRadius * User.Scale / 0.8) * lerp(0.2, 1.0, 1.0 - pow(1.0 - %s, 3.0))' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 1.8)' % T))
    sprite_renderer(em, mpath(M_EXP_SHOCK), facing='CustomFacingVector', sort_order=1)

    # ---- AirShock: quick camera-facing pressure ring at the centre
    em = 'AirShock'
    once(em, i(1))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', f(0.24))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', h('lerp(User.ShockColor, float4(1.0, 1.0, 1.0, 1.0), 0.5)'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('80.0 * User.Scale'))
    set_param(em, 'ParticleSpawnScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0}, CENTER)
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 80, 'y': 80},
              'float2(1.0, 1.0) * (2.0 * 0.6 * User.ShockRadius * User.Scale / 0.8) * lerp(0.15, 1.0, 1.0 - pow(1.0 - %s, 3.0))' % T)
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - pow(%s, 2.0)' % T))
    sprite_renderer(em, mpath(M_EXP_SHOCK), sort_order=6)

    radial = 'float3(cos(Particles.Rnd.x * 6.2831853), sin(Particles.Rnd.x * 6.2831853), 0.0)'

    # ---- Debris: metal plates / bars blown out, tumbling; about a third start as glowing embers and cool down
    em = 'Debris'
    once(em, link('User.DebrisCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Min', h('User.DebrisLifetime * 0.7'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Max', link('User.DebrisLifetime', FLOAT))
    rnd(em)
    set_param(em, 'ParticleSpawnScript', 'Particles.Rnd2', VEC3, {'x': 0, 'y': 0, 'z': 0},
              'float3(%s, %s, %s)' % (HASH % '21.0', HASH % '22.0', HASH % '23.0'))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color',
          h('float4(lerp(User.DebrisColorA.rgb, User.DebrisColorB.rgb, step(0.62, %s)), %s < 0.35 ? 0.15 : 1.0)' % (HASH % '24.0', HASH % '25.0')))
    set_param(em, 'ParticleSpawnScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Engine.Owner.Position + (%s * User.FootprintRadius * lerp(0.2, 0.8, Particles.Rnd.y)'
              ' + float3(0.0, 0.0, User.CenterHeight * lerp(0.3, 1.3, Particles.Rnd.z))) * User.Scale' % radial)
    set_param(em, 'ParticleSpawnScript', 'Particles.Velocity', VEC3, {'x': 0, 'y': 0, 'z': 0},
              '(%s * User.DebrisSpeed * lerp(0.6, 1.3, Particles.Rnd2.x) + float3(0.0, 0.0, lerp(380.0, 820.0, Particles.Rnd2.y))) * User.Scale' % radial)
    set_param(em, 'ParticleSpawnScript', 'Particles.BaseScale', VEC3, {'x': 0.1, 'y': 0.05, 'z': 0.02},
              '(Particles.Rnd2.z < 0.5 ? float3(lerp(0.10, 0.22, Particles.Rnd.z), lerp(0.07, 0.14, Particles.Rnd2.x), 0.022)'
              ' : float3(lerp(0.20, 0.36, Particles.Rnd.y), 0.032, 0.032)) * User.Scale')
    setin(em, 'ParticleUpdateScript', 'GravityForce', 'Gravity', v3(0, 0, -1600))
    setin(em, 'ParticleUpdateScript', 'Drag', 'Drag', f(0.7))
    setin(em, 'ParticleUpdateScript', 'UpdateMeshOrientation', 'Rotation Rate', f(3.2))
    # Embers cool: alpha (glow amount in the material) rises from 0.15 to 1 over the life.
    setin(em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('lerp(1.0, 1.0 / max(Particles.Initial.Color.a, 0.05), saturate(%s * 1.6))' % T))
    set_param(em, 'ParticleUpdateScript', 'Particles.Scale', VEC3, {'x': 0.1, 'y': 0.05, 'z': 0.02},
              'Particles.BaseScale * (1.0 - pow(%s, 5.0))' % T)
    mesh_renderer(em, CUBE_MESH, mpath(M_EXP_DEBRIS))

    # ---- GroundDust: low ring of dusty smoke pushed out along the ground by the blast
    em = 'GroundDust'
    once(em, link('User.DustCount', INT))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('lerp(0.7, 1.1, %s)' % (HASH % '13.0')))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', link('User.DustColor', COLOR))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('120.0 * User.Scale'))
    rnd(em)
    set_param(em, 'ParticleSpawnScript', 'Particles.Dir', VEC3, {'x': 1, 'y': 0, 'z': 0}, radial)
    set_param(em, 'ParticleSpawnScript', 'Particles.StartPos', VEC3, {'x': 0, 'y': 0, 'z': 0},
              'Engine.Owner.Position + (Particles.Dir * User.FootprintRadius * lerp(0.7, 1.0, Particles.Rnd.y) + float3(0.0, 0.0, 35.0)) * User.Scale')
    set_param(em, 'ParticleSpawnScript', 'Particles.MaterialRandom', FLOAT, {'value': 0}, HASH % '14.0')
    set_param(em, 'ParticleUpdateScript', 'Particles.Position', POS, {'x': 0, 'y': 0, 'z': 0},
              'Particles.StartPos + (Particles.Dir * User.DustSpeed * lerp(0.6, 1.1, Particles.Rnd.z) * (1.0 - exp(-%s * 3.2)) / 3.2'
              ' + float3(0.0, 0.0, 30.0 * %s)) * User.Scale' % (AGE, AGE))
    set_param(em, 'ParticleUpdateScript', 'Particles.SpriteSize', VEC2, {'x': 150, 'y': 150},
              'float2(1.0, 1.0) * User.Scale * lerp(110.0, 190.0, Particles.Rnd.z) * lerp(0.5, 1.4, 1.0 - pow(1.0 - %s, 2.0))' % T)
    sprite_rot(em, '30.0')
    sprite_renderer(em, mpath(M_EXP_SMOKE), sort_order=2)

    unreal.EditorAssetLibrary.save_asset(CAGE[0] + '/' + CAGE[1], only_if_is_dirty=False)


def dump(target, emitters):
    CUR['sys'] = target
    call('GetSystemSummary', {'system': sysref()}, quiet=True)
    for em in emitters:
        call('GetEmitterInputValues', {'emitterRef': ref(em)})
    call('GetUserVariables', {'system': sysref()})
    call('GetSystemCompileState', {'system': sysref()})


if STEP == 'build':
    build_materials()
    build_footstep()
    build_cage()
    # Save again once the edits have compiled: the first save happens before the async compile finishes, which left
    # NS_CageBreak with stale compile data and made the editor compile it on first use (~1 s late explosion in PIE).
    for target in (FOOT, CAGE):
        CUR['sys'] = target
        call('GetSystemCompileState', {'system': sysref()}, quiet=True)
        unreal.load_asset(target[0] + '/' + target[1]).modify()
        unreal.EditorAssetLibrary.save_asset(target[0] + '/' + target[1], only_if_is_dirty=False)
dump(FOOT, FOOT_EMITTERS)
dump(CAGE, CAGE_EMITTERS)
CUR['sys'] = HIT
call('GetUserVariables', {'system': sysref()})
open(unreal.Paths.project_saved_dir() + 'BoarVfxPolish_%s.txt' % STEP, 'w', encoding='utf-8').write('\n'.join(LOG))
unreal.log_warning('[BVP] COMPLETE')
