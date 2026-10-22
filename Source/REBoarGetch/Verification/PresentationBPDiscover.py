"""Creates (if missing) the two presentation BPs and dumps node type ids / pins needed for their graphs.
Writes Saved/PresentationBPDiscover.txt"""
import unreal, json
TS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
E = unreal.EditorAssetLibrary
out = []
def call(tool, args):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    return r.get_editor_property('error'), r.get_value_as_json_string() or ''
def ensure(name, parent):
    path = '/Game/BP/Presentation/' + name
    if not E.does_asset_exist(path):
        f = unreal.BlueprintFactory()
        f.set_editor_property('parent_class', parent)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, '/Game/BP/Presentation', unreal.Blueprint, f)
        E.save_asset(path)
    return path
sc = ensure('BP_StageClearPresentation', unreal.StageClearPresentationDirector)
go = ensure('BP_GameOverPresentation', unreal.GameOverPresentationDirector)
for bp in [sc, go]:
    name = bp.split('/')[-1]
    out.append('GRAPHS %s %s' % (name, call('list_graphs', {'blueprint': {'refPath': bp + '.' + name}})))
    g = {'refPath': bp + '.' + name + ':EventGraph'}
    for q in ['Delay', 'Stage Clear Presentation|', 'GameOver Presentation|', 'Presentation|', 'OnPresentationStarted', 'OnResultShown', 'OnGameOverUIShown', 'FinishPresentation']:
        out.append('FIND %s %s -> %s' % (name, q, str(call('find_node_types', {'graph': g, 'type_id_filter': q, 'context_pins': []}))[:3000]))
open(unreal.Paths.project_saved_dir() + 'PresentationBPDiscover.txt', 'w', encoding='utf-8').write('\n'.join(out))
unreal.log('[PBDISC] COMPLETE')
