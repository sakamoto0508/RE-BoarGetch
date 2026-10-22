"""Creates/edits NS_StageClearStars through the engine NiagaraToolsets (ToolsetRegistry JSON tools).
Usage: run_ue_py.sh StageClearNiagara.py -- <step>   (steps: create, inspect, configure)"""
import unreal, json, sys
LOG = []
SYS_PATH = '/Game/REBoarGetch/Art/VFX/StageClear/NS_StageClearStars'
SYS = {'refPath': SYS_PATH + '.NS_StageClearStars'}
TS = 'NiagaraToolsets.NiagaraToolset_System'


def call(tool, args, toolset=TS):
    r = unreal.ToolsetRegistry.execute_tool(toolset, tool, json.dumps(args))
    err = r.get_editor_property('error')
    val = r.get_value_as_json_string()
    data = json.loads(val) if val else None
    if isinstance(data, str):
        data = json.loads(data)
    if tool == 'GetEmitterInputValues' and data:
        LOG.append('### %s %s ERR=%s' % (tool, args['emitterRef']['emitterName'], err))
        for m in data['returnValue']:
            LOG.append('  MODULE %s' % m.get('moduleName'))
            for i in m.get('inputs', []):
                LOG.append('     %s = %s' % (i.get('name'), json.dumps(i.get('value'))[:260]))
    else:
        LOG.append('### %s %s\nERR=%s\n%s' % (tool, json.dumps(args)[:300], err, (val or '')[:3000]))
    return data


def ref(emitter='', script='', module='', inputs=None, renderer=-1):
    return {'system': SYS, 'emitterName': emitter, 'scriptName': script, 'moduleName': module, 'rendererIndex': renderer, 'inputNameStack': inputs or []}


step = next(a for a in sys.argv[1:] if '=' not in a)
if step == 'create':
    if not unreal.EditorAssetLibrary.does_asset_exist(SYS_PATH):
        call('CreateNiagaraSystem', {'assetName': 'NS_StageClearStars', 'assetPath': '/Game/REBoarGetch/Art/VFX/StageClear',
                                     'templateSystem': {'refPath': '/Niagara/DefaultAssets/Templates/Systems/RadialBurst.RadialBurst'}})
    unreal.EditorAssetLibrary.save_asset(SYS_PATH)
    call('GetSystemSummary', {'system': SYS})
elif step == 'inspect':
    summ = call('GetSystemSummary', {'system': SYS})
    for e in [x for x in sys.argv if x.startswith('emitter=')]:
        name = e.split('=', 1)[1]
        call('GetEmitterTopology', {'emitterRef': ref(name)})
        call('GetEmitterInputValues', {'emitterRef': ref(name)})
        call('GetRendererData', {'rendererRef': ref(name, renderer=0)})
    call('GetUserVariables', {'system': SYS})
elif step == 'schema':
    for n in [x.split('=', 1)[1] for x in sys.argv if x.startswith('tool=')]:
        s = json.loads(unreal.ToolsetRegistry.get_toolset_json_schema(unreal.NiagaraToolset_System))
        for t in s['tools']:
            if t['name'].endswith('.' + n):
                LOG.append(json.dumps(t, indent=1)[:12000])
elif step == 'configure':
    exec(open('C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Verification/StageClearNiagaraConfigure.py', encoding='utf-8').read())
    unreal.EditorAssetLibrary.save_asset(SYS_PATH)
open(unreal.Paths.project_saved_dir() + 'StageClearNiagara_%s.txt' % step, 'w', encoding='utf-8').write('\n'.join(LOG))
unreal.log('[SCN] COMPLETE')
