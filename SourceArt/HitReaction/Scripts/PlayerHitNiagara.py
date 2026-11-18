"""AKI-163: builds the Red Boar hit VFX through the engine NiagaraToolsets JSON tools (see memory/ue-niagara-headless-authoring).
Usage (editor closed; the step must be inside -script, run_ue_py.sh passes extra args to UE, not to the script):
  UnrealEditor-Cmd <uproject> -run=pythonscript -script="<this file> <step>" -unattended -nosplash -NullRHI
  steps: create    -> M_PlayerHitRing material + NS_PlayerHitShockwave / NS_PlayerStunOrbit with template emitters, dump stacks
         configure -> sizes / colours / motion (HLSL Set Parameters), renderers, user parameter Color
         inspect   -> dump current values
Log: Saved/PlayerHitNiagara_<step>.txt
- NS_PlayerHitShockwave: one-shot at the hit point, camera-facing expanding ring + a 0.08 s white core flash. Its X axis is the
  push direction (the C++ spawns it rotated to the hit direction). User.Color tints the ring.
- NS_PlayerStunOrbit: looping; 3 yellow stars circling the head (local space; the C++ moves the component to the head every
  frame) + a flat pulsing cyan ring ("stun field").  User.Color tints the stars.  Killed instantly by the C++ when the stun ends."""
import unreal, json, sys
LOG = []
DIR = '/Game/REBoarGetch/Art/VFX/Player'
TS = 'NiagaraToolsets.NiagaraToolset_System'
STEP = next((a for a in sys.argv[1:] if a in ('create', 'configure', 'inspect', 'fixmat')), 'inspect')
MAT = DIR + '/M_PlayerHitRing'
STAR_MAT = '/Game/REBoarGetch/Art/VFX/StageClear/M_StageClearStar.M_StageClearStar'
SHOCK = ('NS_PlayerHitShockwave', ['Ring', 'Flash'])
STUN = ('NS_PlayerStunOrbit', ['Star0', 'Star1', 'Star2', 'Halo'])


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
    if tool == 'GetEmitterInputValues' and data and not err:
        LOG.append('### %s %s' % (tool, args['emitterRef']['emitterName']))
        for m in data['returnValue']:
            LOG.append('  MODULE %s [%s]' % (m.get('moduleName'), m.get('scriptName', '')))
            for i in m.get('inputs', []):
                LOG.append('     %s = %s' % (i.get('name'), json.dumps(i.get('value'))[:220]))
    elif not quiet or err:
        LOG.append('### %s %s\nERR=%s\n%s' % (tool, json.dumps(args)[:500], err, (val or '')[:2500]))
    return data


def ref(system, emitter='', script='', module='', inputs=None, renderer=-1):
    return {'system': sysref(system), 'emitterName': emitter, 'scriptName': script, 'moduleName': module,
            'rendererIndex': renderer, 'inputNameStack': inputs or []}


def make_ring_material():
    """Unlit additive ring: emissive = ParticleColor.rgb * ring(UV) * ParticleColor.a * Boost."""
    if unreal.EditorAssetLibrary.does_asset_exist(MAT):
        ensure_ring_usage()
        return
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset('M_PlayerHitRing', DIR, unreal.Material, unreal.MaterialFactoryNew())
    # Without this usage flag the sprite renderer falls back to the grey default material.
    mat.set_editor_property('used_with_niagara_sprites', True)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    M = unreal.MaterialEditingLibrary
    X = lambda cls, x, y: M.create_material_expression(mat, cls, x, y)
    uv = X(unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    c = X(unreal.MaterialExpressionConstant2Vector, -1400, 150)
    c.set_editor_property('r', .5); c.set_editor_property('g', .5)
    dist = X(unreal.MaterialExpressionDistance, -1200, 50)
    M.connect_material_expressions(uv, '', dist, 'A'); M.connect_material_expressions(c, '', dist, 'B')
    radius = X(unreal.MaterialExpressionScalarParameter, -1200, 200)
    radius.set_editor_property('parameter_name', 'RingRadius'); radius.set_editor_property('default_value', .40)
    width = X(unreal.MaterialExpressionScalarParameter, -1000, 250)
    width.set_editor_property('parameter_name', 'RingWidth'); width.set_editor_property('default_value', .07)
    sub = X(unreal.MaterialExpressionSubtract, -1000, 50)
    M.connect_material_expressions(dist, '', sub, 'A'); M.connect_material_expressions(radius, '', sub, 'B')
    ab = X(unreal.MaterialExpressionAbs, -850, 50); M.connect_material_expressions(sub, '', ab, '')
    div = X(unreal.MaterialExpressionDivide, -700, 50)
    M.connect_material_expressions(ab, '', div, 'A'); M.connect_material_expressions(width, '', div, 'B')
    om = X(unreal.MaterialExpressionOneMinus, -550, 50); M.connect_material_expressions(div, '', om, '')
    sat = X(unreal.MaterialExpressionSaturate, -420, 50); M.connect_material_expressions(om, '', sat, '')
    sq = X(unreal.MaterialExpressionMultiply, -300, 50)
    M.connect_material_expressions(sat, '', sq, 'A'); M.connect_material_expressions(sat, '', sq, 'B')
    pc = X(unreal.MaterialExpressionParticleColor, -600, 350)
    m1 = X(unreal.MaterialExpressionMultiply, -150, 100)
    M.connect_material_expressions(sq, '', m1, 'A'); M.connect_material_expressions(pc, '', m1, 'B')
    m2 = X(unreal.MaterialExpressionMultiply, 0, 150)
    M.connect_material_expressions(m1, '', m2, 'A'); M.connect_material_expressions(pc, 'A', m2, 'B')
    boost = X(unreal.MaterialExpressionScalarParameter, 0, 320)
    boost.set_editor_property('parameter_name', 'EmissiveBoost'); boost.set_editor_property('default_value', 6.0)
    m3 = X(unreal.MaterialExpressionMultiply, 150, 150)
    M.connect_material_expressions(m2, '', m3, 'A'); M.connect_material_expressions(boost, '', m3, 'B')
    M.connect_material_property(m3, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    M.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MAT)
    LOG.append('MATERIAL created %s' % MAT)


def ensure_ring_usage():
    """AKI-163 fix: the ring material must be flagged for Niagara sprites, or it renders as the grey default material."""
    mat = unreal.load_asset(MAT)
    if not mat.get_editor_property('used_with_niagara_sprites'):
        mat.set_editor_property('used_with_niagara_sprites', True)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_asset(MAT, only_if_is_dirty=False)
    LOG.append('RING USAGE niagara_sprites=%s' % unreal.load_asset(MAT).get_editor_property('used_with_niagara_sprites'))


def create_system(name, emitters, template):
    path = DIR + '/' + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        call('CreateNiagaraSystem', {'assetName': name, 'assetPath': DIR,
                                     'templateSystem': {'refPath': '/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.MinimalLightweight'}})
    summ = call('GetSystemSummary', {'system': sysref(name)}, quiet=True)
    have = [e.get('emitterName') or e.get('name') for e in (summ or {}).get('returnValue', {}).get('emitters', [])] if summ else []
    LOG.append('EMITTERS %s %s' % (name, have))
    for em in emitters:
        if em not in have:
            call('AddEmitter', {'system': sysref(name), 'emitterName': em,
                                'templateEmitter': {'refPath': '/Niagara/DefaultAssets/Templates/Emitters/%s.%s' % (template, template)}})
    for n in have:
        if n not in emitters:
            call('RemoveEmitter', {'emitterToRemove': ref(name, n)})
    unreal.EditorAssetLibrary.save_asset(DIR + '/' + name)


def dump(name, emitters):
    call('GetSystemSummary', {'system': sysref(name)})
    for em in emitters:
        call('GetEmitterTopology', {'emitterRef': ref(name, em)})
        call('GetEmitterInputValues', {'emitterRef': ref(name, em)})
        call('GetRendererData', {'rendererRef': ref(name, em, renderer=0)})
    call('GetUserVariables', {'system': sysref(name)})


if STEP == 'create':
    make_ring_material()
    create_system(SHOCK[0], SHOCK[1], 'SimpleSpriteBurst')
    create_system(STUN[0], STUN[1], 'SimpleSpriteBurst')
    dump(*SHOCK)
    dump(STUN[0], STUN[1][:1])
elif STEP == 'configure':
    exec(open('C:/UnrealGames/REBoarGetch/SourceArt/HitReaction/Scripts/PlayerHitNiagaraConfigure.py', encoding='utf-8').read())
    for n in (SHOCK[0], STUN[0]):
        unreal.EditorAssetLibrary.save_asset(DIR + '/' + n)
    dump(*SHOCK)
    dump(*STUN)
elif STEP == 'fixmat':
    ensure_ring_usage()
else:
    dump(*SHOCK)
    dump(*STUN)
open(unreal.Paths.project_saved_dir() + 'PlayerHitNiagara_%s.txt' % STEP, 'w', encoding='utf-8').write('\n'.join(LOG))
unreal.log('[PHN] COMPLETE')
