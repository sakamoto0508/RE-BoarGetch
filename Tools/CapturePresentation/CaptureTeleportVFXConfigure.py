# exec'd from CaptureTeleportVFX.py (step configure). AKI-164 青白いテレポート。
# 参考（YouTube「Unity 3d VFX Particle System Портал」冒頭 0:00〜0:13 の完成デモ）で確認した要素を、
# Material と Niagara で独自に組み直す：白い中心光＋シアン／淡い青の外周、螺旋に巻き上がる細い光線、
# 上方へ昇る小さな光粒、最後に一点へ凝縮して消える。オレンジのリングは採用しない（青白のみ）。
E_ = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
MAT = DIR + '/M_CaptureTeleport'
MI_GLOW = DIR + '/MI_CaptureTeleport_Glow'
MI_STREAK = DIR + '/MI_CaptureTeleport_Streak'
MI_RIBBON = DIR + '/MI_CaptureTeleport_Ribbon'
MI_PILLAR = DIR + '/MI_CaptureTeleport_Pillar'
FLOAT, INT, COLOR = '/Script/Niagara.NiagaraFloat', '/Script/Niagara.NiagaraInt32', '/Script/CoreUObject.LinearColor'
IMPACT, TELE = 'NS_CaptureTeleportImpact', 'NS_CaptureTeleport'

HLSL = r'''
float2 d = (UV - 0.5) * 2.0 * float2(ShapeX, ShapeY);
float g = saturate(1.0 - length(d));
float s = pow(g, Falloff);
float core = pow(g, CorePower) * CoreWhite;
return (Col * s + core.xxx) * Alpha * Intensity;
'''


def make_material():
    if E_.does_asset_exist(MAT):
        mat = unreal.load_asset(MAT)
        mel.delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_CaptureTeleport', DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('used_with_niagara_sprites', True)
    mat.set_editor_property('used_with_niagara_ribbons', True)
    mat.set_editor_property('two_sided', True)
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property('code', HLSL)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('description', 'TeleportGlow')
    names = ['UV', 'Col', 'Alpha', 'ShapeX', 'ShapeY', 'Falloff', 'CorePower', 'CoreWhite', 'Intensity']
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    custom.set_editor_property('inputs', ins)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, -200)
    mel.connect_material_expressions(uv, '', custom, 'UV')
    pc = mel.create_material_expression(mat, unreal.MaterialExpressionParticleColor, -700, -100)
    mel.connect_material_expressions(pc, '', custom, 'Col')
    mel.connect_material_expressions(pc, 'A', custom, 'Alpha')
    defaults = {'ShapeX': 1.0, 'ShapeY': 1.0, 'Falloff': 1.6, 'CorePower': 6.0, 'CoreWhite': .6, 'Intensity': 2.0}
    y = 0
    for n, v in defaults.items():
        p = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, y)
        p.set_editor_property('parameter_name', n)
        p.set_editor_property('default_value', v)
        mel.connect_material_expressions(p, '', custom, n)
        y += 90
    mel.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    E_.save_asset(MAT, only_if_is_dirty=False)
    LOG.append('MATERIAL %s' % mat.get_path_name())
    return mat


def make_mi(path, values):
    name = path.split('/')[-1]
    if E_.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        f = unreal.MaterialInstanceConstantFactoryNew()
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR, unreal.MaterialInstanceConstant, f)
    mel.set_material_instance_parent(mi, unreal.load_asset(MAT))
    for k, v in values.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    E_.save_asset(path, only_if_is_dirty=False)
    LOG.append('MI %s %s' % (path, values))


make_material()
# 丸い光（中心が白、外周が粒子色）。白飛びしないよう Intensity は控えめ。
make_mi(MI_GLOW, {'ShapeX': 1.0, 'ShapeY': 1.0, 'Falloff': 1.6, 'CorePower': 6.0, 'CoreWhite': .55, 'Intensity': 1.8})
# 細長い光線（Velocity Aligned の Sprite 用。U 方向を細く、V 方向＝進行方向に長い）。
make_mi(MI_RIBBON, {'ShapeX': 0.0, 'ShapeY': 1.0, 'Falloff': 1.6, 'CorePower': 6.0, 'CoreWhite': .35, 'Intensity': 2.2})
make_mi(MI_PILLAR, {'ShapeX': 3.5, 'ShapeY': 1.0, 'Falloff': 0.7, 'CorePower': 3.0, 'CoreWhite': .7, 'Intensity': 1.6})
make_mi(MI_STREAK, {'ShapeX': 8.0, 'ShapeY': 1.0, 'Falloff': 1.3, 'CorePower': 5.0, 'CoreWhite': .3, 'Intensity': 1.9})


def f(v):
    return {'struct': {'refPath': FLOAT}, 'value': {'value': v}}


def vec3(x, y, z):
    return {'struct': {'refPath': '/Script/CoreUObject.Vector3f'}, 'value': {'x': x, 'y': y, 'z': z}}


def b(v):
    return {'struct': {'refPath': '/Script/Niagara.NiagaraBool'}, 'value': {'value': -1 if v else 0}}


def h(expr):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression'}, 'value': {'hlslExpression': expr}}


def link(name, tname):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked'},
            'value': {'linkedVariable': {'name': name, 'type': {'classStructOrEnum': {'refPath': tname}}}}}


def enum(path, value):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum'},
            'value': {'enum': {'refPath': path}, 'enumName': value}}


def setin(system, emitter, script, module, names, data):
    if isinstance(names, str):
        names = [names]
    call('SetStackInputData', {'stackInputRef': ref(system, emitter, script, module, names), 'inputData': data}, quiet=True)


def add_module(system, emitter, script, asset):
    r = call('AddModule', {'moduleLocationRef': ref(system, emitter, script), 'moduleAsset': {'refPath': asset}}, quiet=True)
    try:
        name = r['returnValue']['moduleName']
    except Exception:
        name = None
    LOG.append('ADDMODULE %s/%s %s -> %s' % (system, emitter, asset, name))
    return name


def module_names(system, emitter):
    r = call('GetEmitterInputValues', {'emitterRef': ref(system, emitter)}, quiet=True)
    try:
        return [m['moduleName'] for m in r['returnValue']]
    except Exception:
        return []


def ensure_module(system, emitter, script, asset, base):
    names = module_names(system, emitter)
    for n in names:
        if n == base or n.startswith(base):
            return n
    return add_module(system, emitter, script, asset)


def renderer(system, emitter, material, aligned):
    r = unreal.ToolsetRegistry.execute_tool(TS, 'GetRendererData', json.dumps({'rendererRef': ref(system, emitter, renderer=0)}))
    v = json.loads(r.get_value_as_json_string())
    if isinstance(v, str):
        v = json.loads(v)
    props = json.loads(v['returnValue']['propertyValues'])
    props['Material'] = {'refPath': material + '.' + material.split('/')[-1]}
    props['Alignment'] = 'VelocityAligned' if aligned else 'Unaligned'
    props['bCastShadows'] = False
    props['SortMode'] = 'None'
    call('SetRendererData', {'renderer': ref(system, emitter, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})


def uvar(name, tname, value, desc):
    return {'name': name, 'description': desc, 'type': {'classStructOrEnum': {'refPath': tname}},
            'defaultValue': {'struct': {'refPath': tname}, 'value': value}}


def add_user(system, variables, refresh=False):
    have = call('GetUserVariables', {'system': sysref(system)}, quiet=True)
    s = json.dumps(have)
    if refresh:
        # 既定値を更新するため、Link で参照している Int 以外を作り直す（HLSL は名前で参照するので影響しない）。
        old = [{'name': v['name'], 'type': v['type']} for v in variables
               if ('"%s"' % v['name']) in s and v['type']['classStructOrEnum']['refPath'] != INT]
        if old:
            call('RemoveUserVariables', {'system': sysref(system), 'variablesToRemove': old})
        s = json.dumps(call('GetUserVariables', {'system': sysref(system)}, quiet=True))
    todo = [v for v in variables if ('"%s"' % v['name']) not in s]
    if todo:
        call('AddUserVariables', {'system': sysref(system), 'variablesToAdd': todo})


ONCE = enum('/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions', 'ENiagara_EmitterStateOptions::NewEnumerator1')
ROT_UNSET = enum('/Niagara/Enums/ENiagara_SpriteRotationMode.ENiagara_SpriteRotationMode', 'ENiagara_SpriteRotationMode::NewEnumerator0')
D = 'AKI-164 teleport'
SIZE_UNIFORM = enum('/Niagara/Enums/SpriteRenderer/ENiagara_ScaleSpriteSize.ENiagara_ScaleSpriteSize', 'ENiagara_ScaleSpriteSize::NewEnumerator0')

# ------------------------------------------------------------------ 命中（NS_CaptureTeleportImpact）
add_user(IMPACT, [
    uvar('User.Color', COLOR, {'r': .35, 'g': .8, 'b': 1., 'a': 1.}, D + ': outer color (C++ sets ImpactColor)'),
    uvar('User.FlashSize', FLOAT, {'value': 85.}, D + ': center flash size (cm)'),
    uvar('User.StreakCount', INT, {'value': 10}, D + ': radial streak count'),
    uvar('User.StreakSpeed', FLOAT, {'value': 560.}, D + ': radial streak speed (cm/s)'),
    uvar('User.StreakSize', FLOAT, {'value': 30.}, D + ': radial streak length (cm)'),
    uvar('User.SparkCount', INT, {'value': 8}, D + ': small light particle count'),
    uvar('User.SparkSize', FLOAT, {'value': 7.}, D + ': small light particle size (cm)'),
    uvar('User.LifetimeScale', FLOAT, {'value': 1.}, D + ': lifetime multiplier'),
])
# Flash: 1 粒・0.14 秒。中心は白、外周は User.Color。少し広がりながら消える。
setin(IMPACT, 'Flash', 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('0.14 * User.LifetimeScale'))
setin(IMPACT, 'Flash', 'ParticleSpawnScript', 'InitializeParticle', 'Color', h('lerp(User.Color, float4(1.0, 1.0, 1.0, 1.0), 0.45)'))
setin(IMPACT, 'Flash', 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h('User.FlashSize'))
setin(IMPACT, 'Flash', 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('(1.0 - Particles.NormalizedAge) * 0.85'))
m = ensure_module(IMPACT, 'Flash', 'ParticleUpdateScript', '/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize', 'ScaleSpriteSize')
setin(IMPACT, 'Flash', 'ParticleUpdateScript', m, 'Scale Sprite Size Mode', SIZE_UNIFORM)
setin(IMPACT, 'Flash', 'ParticleUpdateScript', m, 'Uniform Scale Factor', h('lerp(0.55, 1.35, Particles.NormalizedAge)'))
renderer(IMPACT, 'Flash', MI_GLOW, False)
# Streaks: 放射状の短い光線。
for em, cnt, size, spd, life, grav, drag, mat, al, col in [
        ('Streaks', 'User.StreakCount', 'User.StreakSize', 'User.StreakSpeed', (0.10, 0.18), 0., 5., MI_STREAK, True, 'User.Color'),
        ('Sparks', 'User.SparkCount', 'User.SparkSize', '(User.StreakSpeed * 0.4)', (0.20, 0.32), -300., 3., MI_GLOW, False,
         'lerp(User.Color, float4(1.0, 1.0, 1.0, 1.0), 0.5)')]:
    setin(IMPACT, em, 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', link(cnt, INT))
    setin(IMPACT, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Min', h('%.3f * User.LifetimeScale' % life[0]))
    setin(IMPACT, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Max', h('%.3f * User.LifetimeScale' % life[1]))
    setin(IMPACT, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', h(col))
    setin(IMPACT, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Min', h('%s * 0.6' % size))
    setin(IMPACT, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Max', h(size))
    setin(IMPACT, em, 'ParticleSpawnScript', 'InitializeParticle', 'Sprite Rotation Mode', ROT_UNSET)
    setin(IMPACT, em, 'ParticleSpawnScript', 'ShapeLocation', 'Sphere Radius', f(12.))
    setin(IMPACT, em, 'ParticleSpawnScript', 'AddVelocity', ['Velocity Speed', 'Minimum'], h('%s * 0.55' % spd))
    setin(IMPACT, em, 'ParticleSpawnScript', 'AddVelocity', ['Velocity Speed', 'Maximum'], h(spd))
    setin(IMPACT, em, 'ParticleUpdateScript', 'GravityForce', 'Gravity', vec3(0, 0, grav))
    setin(IMPACT, em, 'ParticleUpdateScript', 'Drag', 'Drag', f(drag))
    setin(IMPACT, em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - Particles.NormalizedAge * Particles.NormalizedAge'))
    renderer(IMPACT, em, mat, al)

# ------------------------------------------------------------------ 転送（NS_CaptureTeleport）v2（AKI-164 / 165 改修）
# 年齢は C++ が実時間で指定する。0 = テレポート開始（命中の約0.3秒後）、User.CommitTime = 捕獲確定（Boar 消失・Cage 収容、
# Gameplay の時刻のまま）、その後 User.TailDuration で光柱が弱まり、光粒子が上空へ抜けて消える。
# 構成（どの角度・上からも立体的に見えるよう、カメラを向く Ribbon・縦の光柱・床の光円・球状の核を重ねる）：
#   RibbonA/B/C : Boar の周りを 120° ずつずれて螺旋状に昇るシアン～青の光の帯（Screen Facing Ribbon）
#   Swirl       : 螺旋に巻き上がる細い光線（Velocity Aligned Sprite）
#   Pillar      : 下から上へ伸びる縦の光柱（Z 軸に沿ってカメラを向く Sprite）。確定まで強まり、Tail で弱まる
#   BaseDisc    : 足元の水平な光円（上から見たときの形）
#   Core        : 白い高輝度の中心核（Boar のシルエットを残す強さ）
#   Motes       : 上昇する細かな光粒子（Tail まで残る）
#   BurstUp     : 確定の瞬間に上空へ抜ける光線
add_user(TELE, [
    uvar('User.Color', COLOR, {'r': .16, 'g': .42, 'b': 1., 'a': 1.}, D + ': outer color (C++ sets TeleportColor)'),
    uvar('User.CoreColor', COLOR, {'r': .7, 'g': .84, 'b': 1., 'a': .8}, D + ': core color (C++ sets TeleportCoreColor)'),
    uvar('User.Radius', FLOAT, {'value': 60.}, D + ': boar radius (C++)'),
    uvar('User.Height', FLOAT, {'value': 100.}, D + ': boar height (C++)'),
    uvar('User.TransferDuration', FLOAT, {'value': .78}, D + ': legacy = CommitTime (C++)'),
    uvar('User.CommitTime', FLOAT, {'value': .78}, D + ': seconds from teleport start to capture commit / boar gone (C++)'),
    uvar('User.TailDuration', FLOAT, {'value': .85}, D + ': seconds the pillar fades and particles rise after commit (C++)'),
    uvar('User.SwirlRate', FLOAT, {'value': 110.}, D + ': spiral light streaks per second'),
    uvar('User.SwirlSpin', FLOAT, {'value': 1.3}, D + ': spiral turns per second (streaks)'),
    uvar('User.RiseSpeed', FLOAT, {'value': 230.}, D + ': upward speed of streaks (cm/s)'),
    uvar('User.StreakSize', FLOAT, {'value': 34.}, D + ': streak length (cm)'),
    uvar('User.RibbonRate', FLOAT, {'value': 80.}, D + ': ribbon points per second (per ribbon)'),
    uvar('User.RibbonTurns', FLOAT, {'value': 2.4}, D + ': turns each ribbon makes while climbing'),
    uvar('User.RibbonWidth', FLOAT, {'value': 9.}, D + ': ribbon width (cm)'),
    uvar('User.RibbonLifetime', FLOAT, {'value': .9}, D + ': ribbon trail length in seconds'),
    uvar('User.PillarSize', FLOAT, {'value': 3.4}, D + ': pillar height in boar heights'),
    uvar('User.PillarOpacity', FLOAT, {'value': .15}, D + ': pillar peak opacity'),
    uvar('User.DiscSize', FLOAT, {'value': 2.8}, D + ': base disc diameter in boar radii'),
    uvar('User.MoteRate', FLOAT, {'value': 70.}, D + ': small light particles per second'),
    uvar('User.MoteSize', FLOAT, {'value': 4.}, D + ': small light particle size (cm)'),
    uvar('User.CoreSize', FLOAT, {'value': 120.}, D + ': white core glow size (cm)'),
    uvar('User.BurstCount', INT, {'value': 16}, D + ': light rays shooting up at commit'),
    uvar('User.BurstSpeed', FLOAT, {'value': 1100.}, D + ': speed of the rays shooting up (cm/s)'),
    uvar('User.LifetimeScale', FLOAT, {'value': 1.}, D + ': lifetime multiplier'),
], refresh=True)
C_ = 'User.CommitTime'
END = '(User.CommitTime + User.TailDuration)'
# 確定までに 0→1 へ強まり、確定後 Tail の 60% で 0 へ戻る包絡（Emitter.Age 基準）。
RAMP = 'saturate(Emitter.Age / max(0.05, User.CommitTime * 0.7))'
FADE = '(1.0 - saturate((Emitter.Age - User.CommitTime) / max(0.05, User.TailDuration * 0.6)))'

# --- Swirl / Motes（既存 Emitter を再調整）
for em, rate, size, life, mat, al, col, speed, stop in [
        ('Swirl', 'User.SwirlRate', 'User.StreakSize', (0.70, 1.10), MI_STREAK, True, 'User.Color', 'User.RiseSpeed', C_),
        ('Motes', 'User.MoteRate', 'User.MoteSize', (0.8, 1.3), MI_GLOW, False, 'User.CoreColor', '(User.RiseSpeed * 0.5)',
         '(User.CommitTime + User.TailDuration * 0.5)')]:
    setin(TELE, em, 'EmitterUpdateScript', 'SpawnRate', 'SpawnRate', h('(Emitter.Age < %s) ? %s : 0.0' % (stop, rate)))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Min', h('%.3f * User.LifetimeScale' % life[0]))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Max', h('%.3f * User.LifetimeScale' % life[1]))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', h(col))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Min', h('%s * 0.6' % size))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Max', h(size))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Sprite Rotation Mode', ROT_UNSET)
    setin(TELE, em, 'ParticleSpawnScript', 'ShapeLocation', 'Sphere Radius', h('User.Radius * %s' % ('0.75' if em == 'Swirl' else '1.1')))
    setin(TELE, em, 'ParticleSpawnScript', 'ShapeLocation', 'Shape Origin', h('Particles.Position + float3(0.0, 0.0, User.Height * 0.45)'))
    setin(TELE, em, 'ParticleSpawnScript', 'AddVelocity', ['Velocity Speed', 'Minimum'], h('%s * 0.6' % speed))
    setin(TELE, em, 'ParticleSpawnScript', 'AddVelocity', ['Velocity Speed', 'Maximum'], h(speed))
    setin(TELE, em, 'ParticleSpawnScript', 'AddVelocity', 'Cone Angle', f(10. if em == 'Swirl' else 18.))
    setin(TELE, em, 'ParticleUpdateScript', 'GravityForce', 'Gravity', vec3(0, 0, 0))
    setin(TELE, em, 'ParticleUpdateScript', 'Drag', 'Drag', f(1.0))
    setin(TELE, em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha',
          h('saturate(Particles.NormalizedAge / 0.15) * (1.0 - saturate((Particles.NormalizedAge - 0.6) / 0.40))'))
    if em == 'Swirl':
        vm = ensure_module(TELE, em, 'ParticleUpdateScript', '/Niagara/Modules/Update/Velocity/VortexVelocity.VortexVelocity', 'VortexVelocity')
        setin(TELE, em, 'ParticleUpdateScript', vm, 'Velocity Amount', h('User.SwirlSpin * 6.2832 * User.Radius'))
        setin(TELE, em, 'ParticleUpdateScript', vm, 'Vortex Axis', vec3(0, 0, 1))
    pm = ensure_module(TELE, em, 'ParticleUpdateScript', '/Niagara/Modules/Update/Forces/PointAttractionForce.PointAttractionForce', 'PointAttractionForce')
    # 上空（光柱の上端）へ吸い込む。確定の少し前から強める。Motes は Tail でも上へ抜ける。
    setin(TELE, em, 'ParticleUpdateScript', pm, 'AttractorPosition', h('Engine.Owner.Position + float3(0.0, 0.0, User.Height * User.PillarSize * 0.9)'))
    setin(TELE, em, 'ParticleUpdateScript', pm, 'AttractionStrength', h('(Emitter.Age > User.CommitTime * 0.6 ? 1800.0 : 0.0)'))
    setin(TELE, em, 'ParticleUpdateScript', pm, 'Attraction Radius', h('User.Height * User.PillarSize * 2.0'))
    setin(TELE, em, 'ParticleUpdateScript', pm, 'Module.Kill Within Radius', b(True))
    setin(TELE, em, 'ParticleUpdateScript', pm, 'Kill Radius', h('User.Height * 0.15'))
    renderer(TELE, em, mat, al)

# --- Swirl を手続き的な螺旋にする（参考動画の「巻きながら昇る光」）。
# 粒子ごとに Particles.MaterialRandom から開始角・半径・開始高さ・回転方向（7 割が同じ向き）を決め、
#   角度 = 開始角 + 向き × User.SwirlSpin × 2π × 年齢、高さ = 開始高さ + User.RiseSpeed × 年齢、
#   位置 = Owner + (cos 角度 × 半径, sin 角度 × 半径, 高さ)、速度 = 螺旋の接線（Velocity Aligned の向き）。
# 半径は上へ行くほど少し広がる（参考の柱の形）。物理モジュールは止める。
for mn in ['VortexVelocity', 'PointAttractionForce', 'GravityForce', 'Drag']:
    call('SetModuleEnabled', {'moduleRef': ref(TELE, 'Swirl', 'ParticleUpdateScript', mn), 'bEnabled': False}, quiet=True)
RND = 'Particles.MaterialRandom'
DIRS = '(frac(%s * 7.31) < 0.3 ? -1.0 : 1.0)' % RND
SPIN = '(User.SwirlSpin * 6.2832)'
ZS = '(frac(%s * 3.17) * 0.35 * User.Height + User.RiseSpeed * Particles.Age)' % RND
HN = 'saturate(%s / (User.Height * 3.0))' % ZS
ANG = '(%s * 6.2832 + %s * %s * Particles.Age)' % (RND, DIRS, SPIN)
RAD = '(User.Radius * (0.55 + 0.35 * frac(%s * 5.71)) * (0.85 + 0.45 * %s))' % (RND, HN)
POS = 'Engine.Owner.Position + float3(cos(%s) * %s, sin(%s) * %s, %s)' % (ANG, RAD, ANG, RAD, ZS)
VEL = 'float3(-sin(%s) * %s * %s * %s, cos(%s) * %s * %s * %s, User.RiseSpeed)' % (ANG, RAD, DIRS, SPIN, ANG, RAD, DIRS, SPIN)
existing = [n for n in module_names(TELE, 'Swirl') if n.startswith('SetVariables')]
if existing:
    helix_module = existing[0]
else:
    r = call('AddSetParametersModule', {'moduleLocationRef': ref(TELE, 'Swirl', 'ParticleUpdateScript'), 'parameters': [
        {'variable': {'name': 'Particles.Position', 'type': {'classStructOrEnum': {'refPath': '/Script/Niagara.NiagaraPosition'}}},
         'defaultValue': {'struct': {'refPath': '/Script/Niagara.NiagaraPosition'}, 'value': {'x': 0, 'y': 0, 'z': 0}}},
        {'variable': {'name': 'Particles.Velocity', 'type': {'classStructOrEnum': {'refPath': '/Script/CoreUObject.Vector3f'}}},
         'defaultValue': {'struct': {'refPath': '/Script/CoreUObject.Vector3f'}, 'value': {'x': 0, 'y': 0, 'z': 0}}}]})
    helix_module = r['returnValue']['moduleName']
LOG.append('HELIX MODULE %s' % helix_module)
for var, expr in [('Particles.Position', POS), ('Particles.Velocity', VEL)]:
    call('SetStackInputData', {'stackInputRef': ref(TELE, 'Swirl', 'ParticleUpdateScript', helix_module, [var]), 'inputData': h(expr)})

# --- 螺旋 Ribbon ×3：発生点が足元から頭上へ螺旋を描いて昇り、短い寿命の点を Ribbon がつなぐ（= 昇る光の帯）。
RIB = [('RibbonA', 1.00, 1.0), ('RibbonB', 0.70, -0.8), ('RibbonC', 1.15, 1.2), ('RibbonD', 0.55, -1.1), ('RibbonE', 0.90, 0.9), ('RibbonF', 0.80, -1.0)]
for i, (em, rad, turn) in enumerate(RIB):
    phase = '%.4f' % (i * 1.0472)
    rt = '(User.RibbonTurns * %.2f)' % turn
    rr = '(User.Radius * %.2f)' % rad
    sr = ensure_module(TELE, em, 'EmitterUpdateScript', '/Niagara/Modules/Emitter/SpawnRate.SpawnRate', 'SpawnRate')
    setin(TELE, em, 'EmitterUpdateScript', sr, 'SpawnRate', h('(Emitter.Age < User.CommitTime + 0.05) ? User.RibbonRate : 0.0'))
    setin(TELE, em, 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', h('User.CommitTime + User.TailDuration + 0.2'))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h('User.RibbonLifetime * User.LifetimeScale'))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color',
          h('lerp(User.Color, User.CoreColor, 0.35 * %s) * float4(1.0, 1.0, 1.0, 0.6 + 0.4 * %s)' % (RAMP, RAMP)))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'UsePositionOffset', b(True))
    # 角度 = 経過 / 確定時刻 × 回数 × 2π + 位相、半径は上へ行くほど少し絞る、高さは足元→身長の 2.85 倍（高い光柱）。回転方向は Ribbon ごとに交互。
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Position Offset', h(
        'float3(cos(saturate(Emitter.Age / max(0.05, User.CommitTime)) * %s * 6.2832 + %s) * %s * (1.0 - 0.4 * saturate(Emitter.Age / max(0.05, User.CommitTime))), '
        'sin(saturate(Emitter.Age / max(0.05, User.CommitTime)) * %s * 6.2832 + %s) * %s * (1.0 - 0.4 * saturate(Emitter.Age / max(0.05, User.CommitTime))), '
        'User.Height * (0.05 + 2.8 * saturate(Emitter.Age / max(0.05, User.CommitTime))))' % (rt, phase, rr, rt, phase, rr)))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Ribbon Width Mode',
          enum('/Niagara/Enums/Ribbons/ENiagara_UnsetDirectSet.ENiagara_UnsetDirectSet', 'ENiagara_UnsetDirectSet::NewEnumerator1'))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Ribbon Width', h('User.RibbonWidth'))
    # 点はゆっくり昇り、帯全体が上へ流れる。
    av = ensure_module(TELE, em, 'ParticleSpawnScript', '/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity', 'AddVelocity')
    setin(TELE, em, 'ParticleSpawnScript', av, 'Velocity', h('float3(0.0, 0.0, User.Height * 0.25)'))
    sc = ensure_module(TELE, em, 'ParticleUpdateScript', '/Niagara/Modules/Update/Color/ScaleColor.ScaleColor', 'ScaleColor')
    setin(TELE, em, 'ParticleUpdateScript', sc, 'Scale Alpha', h('1.0 - Particles.NormalizedAge'))
    r = unreal.ToolsetRegistry.execute_tool(TS, 'GetRendererData', json.dumps({'rendererRef': ref(TELE, em, renderer=0)}))
    v = json.loads(r.get_value_as_json_string())
    if isinstance(v, str):
        v = json.loads(v)
    props = json.loads(v['returnValue']['propertyValues'])
    props['Material'] = {'refPath': MI_RIBBON + '.' + MI_RIBBON.split('/')[-1]}
    props['FacingMode'] = 'Screen'
    props['bCastShadows'] = False
    call('SetRendererData', {'renderer': ref(TELE, em, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})

# --- 単発 Sprite の共通設定（Pillar / BaseDisc / Core）
def single(em, life, size, col, offset, alpha, mat, scale=None):
    setin(TELE, em, 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', h('User.CommitTime + User.TailDuration + 0.2'))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime', h(life))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', h(col))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size', h(size))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'UsePositionOffset', b(True))
    setin(TELE, em, 'ParticleSpawnScript', 'InitializeParticle', 'Position Offset', h(offset))
    setin(TELE, em, 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h(alpha))
    if scale:
        m = ensure_module(TELE, em, 'ParticleUpdateScript', '/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize', 'ScaleSpriteSize')
        setin(TELE, em, 'ParticleUpdateScript', m, 'Scale Sprite Size Mode', SIZE_UNIFORM)
        setin(TELE, em, 'ParticleUpdateScript', m, 'Uniform Scale Factor', h(scale))


def set_vector_attr(em, attr, value):
    # Particles.SpriteAlignment / SpriteFacing を Spawn で固定（SetParameters モジュール）。
    if attr.split('.')[-1] in json.dumps(call('GetEmitterInputValues', {'emitterRef': ref(TELE, em)}, quiet=True)):
        return
    r = call('AddSetParametersModule', {'moduleLocationRef': ref(TELE, em, 'ParticleSpawnScript'), 'parameters': [
        {'variable': {'name': attr, 'type': {'classStructOrEnum': {'refPath': '/Script/CoreUObject.Vector3f'}}},
         'defaultValue': {'struct': {'refPath': '/Script/CoreUObject.Vector3f'}, 'value': {'x': value[0], 'y': value[1], 'z': value[2]}}}]})
    LOG.append('SETPARAM %s %s -> %s' % (em, attr, json.dumps(r)[:200]))


def sprite_renderer(em, material, alignment, facing):
    r = unreal.ToolsetRegistry.execute_tool(TS, 'GetRendererData', json.dumps({'rendererRef': ref(TELE, em, renderer=0)}))
    v = json.loads(r.get_value_as_json_string())
    if isinstance(v, str):
        v = json.loads(v)
    props = json.loads(v['returnValue']['propertyValues'])
    props['Material'] = {'refPath': material + '.' + material.split('/')[-1]}
    props['Alignment'] = alignment
    props['FacingMode'] = facing
    props['bCastShadows'] = False
    call('SetRendererData', {'renderer': ref(TELE, em, renderer=0), 'rendererData': {'propertyValues': json.dumps(props)}})


# Pillar：Z 軸に沿ってカメラの方を向く縦長の光（カメラが回り込んでも柱に見える）。下端を足元に合わせる。
single('Pillar', END, 'User.Height * User.PillarSize', 'User.CoreColor',
       'float3(0.0, 0.0, User.Height * User.PillarSize * 0.42)',
       'User.PillarOpacity * saturate((Emitter.Age - User.CommitTime * 0.25) / max(0.05, User.CommitTime * 0.5)) * %s' % FADE,
       MI_PILLAR, 'lerp(0.55, 1.0, saturate(Emitter.Age / max(0.05, User.CommitTime)))')
set_vector_attr('Pillar', 'Particles.SpriteAlignment', (0., 0., 1.))
sprite_renderer('Pillar', MI_PILLAR, 'CustomAlignment', 'FaceCamera')
# BaseDisc：足元の水平な光円（上から見たときの形）。
single('BaseDisc', END, 'User.Radius * User.DiscSize', 'User.Color', 'float3(0.0, 0.0, 3.0)',
       '0.45 * %s * %s' % (RAMP, FADE), MI_GLOW, 'lerp(0.6, 1.1, %s)' % RAMP)
set_vector_attr('BaseDisc', 'Particles.SpriteFacing', (0., 0., 1.))
sprite_renderer('BaseDisc', MI_GLOW, 'Unaligned', 'CustomFacingVector')
# Core：白い中心核。確定まで強まり（シルエットを潰さない 0.7 まで）、確定で縮みながら上へ。
single('Core', '(User.CommitTime + 0.15)', 'User.CoreSize', 'User.CoreColor', 'float3(0.0, 0.0, User.Height * 0.5)',
       '0.45 * %s' % RAMP, MI_GLOW,
       '(Emitter.Age < User.CommitTime ? lerp(0.5, 1.0, %s) : lerp(1.0, 0.2, saturate((Emitter.Age - User.CommitTime) / 0.15)))' % RAMP)
setin(TELE, 'Core', 'ParticleSpawnScript', 'AddVelocity', 'Velocity', h('float3(0.0, 0.0, User.Height * 0.35 / max(0.05, User.CommitTime))'))
sprite_renderer('Core', MI_GLOW, 'Unaligned', 'FaceCamera')

# --- BurstUp：確定の瞬間、光線が上空へ抜ける。
setin(TELE, 'BurstUp', 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', link('User.BurstCount', INT))
setin(TELE, 'BurstUp', 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Time', h('User.CommitTime'))
setin(TELE, 'BurstUp', 'EmitterUpdateScript', 'EmitterState', 'Loop Duration', h('User.CommitTime + User.TailDuration + 0.2'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Min', h('0.30 * User.LifetimeScale'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Max', h('0.55 * User.LifetimeScale'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'InitializeParticle', 'Color', h('lerp(User.Color, User.CoreColor, 0.5)'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Min', h('User.StreakSize * 1.2'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Max', h('User.StreakSize * 2.2'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'InitializeParticle', 'Sprite Rotation Mode', ROT_UNSET)
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'ShapeLocation', 'Sphere Radius', h('User.Radius * 0.6'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'ShapeLocation', 'Shape Origin', h('Particles.Position + float3(0.0, 0.0, User.Height * 0.6)'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'AddVelocity', 'Velocity Mode',
      enum('/Niagara/Enums/Utility/ENiagara_VelocityMode.ENiagara_VelocityMode', 'ENiagara_VelocityMode::NewEnumerator2'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'AddVelocity', ['Velocity Speed', 'Minimum'], h('User.BurstSpeed * 0.6'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'AddVelocity', ['Velocity Speed', 'Maximum'], h('User.BurstSpeed'))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'AddVelocity', 'Cone Axis', vec3(0, 0, 1))
setin(TELE, 'BurstUp', 'ParticleSpawnScript', 'AddVelocity', 'Cone Angle', f(14.))
setin(TELE, 'BurstUp', 'ParticleUpdateScript', 'GravityForce', 'Gravity', vec3(0, 0, 0))
setin(TELE, 'BurstUp', 'ParticleUpdateScript', 'Drag', 'Drag', f(1.5))
setin(TELE, 'BurstUp', 'ParticleUpdateScript', 'ScaleColor', 'Scale Alpha', h('1.0 - Particles.NormalizedAge'))
renderer(TELE, 'BurstUp', MI_STREAK, True)
