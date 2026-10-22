"""Read-only probe of the Blueprint graph DSL toolset (schema). Writes Saved/ProbeBlueprintDsl.txt"""
import unreal, json
out = []
for s in json.loads(unreal.ToolsetRegistry.get_all_toolset_json_schemas()):
    if 'lueprint' not in s['name']:
        continue
    out.append('TOOLSET ' + s['name'])
    for t in s.get('tools', []):
        out.append('TOOL %s %s\n    %s' % (t['name'], json.dumps(t.get('inputSchema', {}).get('properties', {}))[:700], t.get('description', '')[:400].replace('\n', ' ')))
open(unreal.Paths.project_saved_dir() + 'ProbeBlueprintDsl.txt', 'w', encoding='utf-8').write('\n'.join(out))
unreal.log('[PBD] COMPLETE')
