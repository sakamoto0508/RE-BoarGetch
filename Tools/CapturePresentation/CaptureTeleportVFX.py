"""青白いテレポート（捕獲転送）VFX を NiagaraToolsets の JSON ツールで作る。
Usage: run_ue_py.sh <this file> -- (args via env CTV_STEP=create|configure|inspect|schema)
  create    -> NS_CaptureTeleportImpact / NS_CaptureTeleport を雛形 Emitter から作る（既存なら何もしない）
  configure -> CaptureTeleportVFXConfigure.py を実行（Material 作成・各 Emitter の設定・User Parameter）
  inspect   -> 現在値を Saved/CaptureTeleportVFX_inspect.txt へ
  schema    -> ツール一覧を Saved/CaptureTeleportVFX_schema.json へ"""
import unreal, json, os
LOG = []
DIR = '/Game/REBoarGetch/Art/VFX/Capture'
TS = 'NiagaraToolsets.NiagaraToolset_System'
STEP = os.environ.get('CTV_STEP', 'inspect')
TPL = '/Niagara/DefaultAssets/Templates/Emitters/'

SYSTEMS = {
    # 命中：白い中心光＋シアンの放射光＋小さな光粒
    'NS_CaptureTeleportImpact': [('Flash', 'SimpleSpriteBurst'), ('Streaks', 'OmnidirectionalBurst'), ('Sparks', 'OmnidirectionalBurst')],
    # 転送：Boar を包む光の柱（螺旋の光線）→ 上方へ収束 → 凝縮して消える
    'NS_CaptureTeleport': [('Swirl', 'Fountain'), ('Core', 'SimpleSpriteBurst'), ('Motes', 'Fountain'),
                           ('RibbonA', 'LocationBasedRibbon'), ('RibbonB', 'LocationBasedRibbon'), ('RibbonC', 'LocationBasedRibbon'),
                           ('RibbonD', 'LocationBasedRibbon'), ('RibbonE', 'LocationBasedRibbon'), ('RibbonF', 'LocationBasedRibbon'),
                           ('Pillar', 'SimpleSpriteBurst'), ('BaseDisc', 'SimpleSpriteBurst'), ('BurstUp', 'OmnidirectionalBurst')],
}


def sysref(name):
    return {'refPath': DIR + '/' + name + '.' + name}


def call(tool, args, quiet=False):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    err = r.get_editor_property('error')
    val = r.get_value_as_json_string()
    data = None
    try:
        data = json.loads(val) if val else None
        if isinstance(data, str):
            data = json.loads(data)
    except Exception:
        data = val
    if tool == 'GetEmitterInputValues' and isinstance(data, dict) and not err:
        LOG.append('### %s %s' % (tool, args['emitterRef']['emitterName']))
        for m in data['returnValue']:
            LOG.append('  MODULE %s [%s] (%s)' % (m.get('moduleName'), m.get('scriptName', ''), m.get('enabled', '')))
            for i in m.get('inputs', []):
                LOG.append('     %s = %s' % (i.get('name'), json.dumps(i.get('value'))[:300]))
    elif not quiet or err:
        LOG.append('### %s %s\nERR=%s\n%s' % (tool, json.dumps(args)[:400], err, (val or '')[:4000]))
    return data


def ref(system, emitter='', script='', module='', inputs=None, renderer=-1):
    return {'system': sysref(system), 'emitterName': emitter, 'scriptName': script, 'moduleName': module,
            'rendererIndex': renderer, 'inputNameStack': inputs or []}


def emitter_names(system):
    summ = call('GetSystemSummary', {'system': sysref(system)}, quiet=True)
    try:
        return [e.get('emitterName') or e.get('name') for e in summ['returnValue']['emitters']]
    except Exception:
        return []


def dump(system):
    call('GetSystemSummary', {'system': sysref(system)})
    for n in emitter_names(system):
        call('GetEmitterInputValues', {'emitterRef': ref(system, n)})
        call('GetRendererData', {'rendererRef': ref(system, n, renderer=0)})
    call('GetUserVariables', {'system': sysref(system)})


if STEP == 'schema':
    s = unreal.ToolsetRegistry.get_toolset_json_schema(unreal.NiagaraToolset_System)
    open(unreal.Paths.project_saved_dir() + 'CaptureTeleportVFX_schema.json', 'w', encoding='utf-8').write(s)
elif STEP == 'create':
    for name, emitters in SYSTEMS.items():
        path = DIR + '/' + name
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            call('CreateNiagaraSystem', {'assetName': name, 'assetPath': DIR,
                                         'templateSystem': {'refPath': '/Niagara/DefaultAssets/Templates/Systems/RadialBurst.RadialBurst'}})
        old = emitter_names(name)
        LOG.append('EMITTERS(before) %s %s' % (name, old))
        for em, tpl in emitters:
            if em not in old:
                call('AddEmitter', {'system': sysref(name), 'templateEmitter': {'refPath': TPL + tpl + '.' + tpl},
                                    'emitterName': em}, quiet=True)
        keep = [e for e, _ in emitters]
        for n in old:
            if n not in keep:
                call('RemoveEmitter', {'emitterToRemove': ref(name, n)})
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        dump(name)
elif STEP == 'probe':
    exec(open(os.environ['CTV_PROBE'], encoding='utf-8').read())
elif STEP == 'configure':
    exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'CaptureTeleportVFXConfigure.py'), encoding='utf-8').read())
    for name in SYSTEMS:
        unreal.EditorAssetLibrary.save_asset(DIR + '/' + name, only_if_is_dirty=False)
        dump(name)
else:
    for name in SYSTEMS:
        dump(name)
open(unreal.Paths.project_saved_dir() + 'CaptureTeleportVFX_%s.txt' % STEP, 'w', encoding='utf-8').write('\n'.join(LOG))
unreal.log('[CTV] COMPLETE')
