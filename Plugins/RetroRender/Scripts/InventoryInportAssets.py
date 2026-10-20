"""Lists every dependency of RetroRender on /Game/InportAssets (git-ignored in this repository).

UnrealEditor-Cmd REBoarGetch.uproject -run=pythonscript -script=<this> -unattended -nosplash -NullRHI
Writes Plugins/RetroRender/Docs/InportAssetsDependencies.json and .md:
  - modified : InportAssets packages RetroRender changes (patched materials, extended MPC, reparented MIs)
  - referenced : InportAssets packages that tracked RetroRender assets / patched materials depend on
  - present : whether each package exists in this checkout
"""
from pathlib import Path
import json
import unreal

HERE = Path(__file__).resolve().parent
DOCS = HERE.parent / 'Docs'
INPORT = '/Game/InportAssets/'
RECORD = HERE / 'PatchRetroMaterials.json'
registry = unreal.AssetRegistryHelpers.get_asset_registry()
lib = unreal.EditorAssetLibrary

MODIFIED = {
    '/Game/InportAssets/LevelPrototyping/Materials/M_FlatCol': 'patched parent material (vertex snap)',
    '/Game/InportAssets/LPRiverForest/Materials/M_LowPoly': 'patched parent material (vertex snap, _LP)',
    '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass': 'patched parent material (vertex snap, _LP)',
    '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater': 'patched parent material (vertex snap, _LP)',
    '/Game/InportAssets/LPRiverForest/Materials/Common/MPC_UnlitStrength': '+4 snap scalars (default 0)',
}
if RECORD.exists():
    for mi in json.loads(RECORD.read_text(encoding='utf-8')).get('reparented', []):
        MODIFIED.setdefault(mi, 'material instance reparented to M_RetroFBXPhong')

# Tracked assets that RetroRender owns or patches (dependencies are followed one level deep).
ROOTS = ['/Game/Art/Rendering/Retro', '/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World',
         '/Game/Stage01/Ground/Materials/M_Ground_LowPoly_World', '/Game/BP/Cage/Materials']
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                                include_searchable_names=False, include_soft_management_references=False,
                                                include_hard_management_references=False)
packages = set()
for root in ROOTS:
    if lib.does_directory_exist(root):
        packages |= {p.split('.')[0] for p in lib.list_assets(root, recursive=True, include_folder=False)}
    elif lib.does_asset_exist(root):
        packages.add(root)
referenced = {}
for package in sorted(packages):
    for dep in registry.get_dependencies(package, options) or []:
        dep = str(dep)
        if dep.startswith(INPORT):
            referenced.setdefault(dep, []).append(package)

def present(path):
    return lib.does_asset_exist(path)

result = {
    'modified': [{'package': p, 'change': c, 'present': present(p)} for p, c in sorted(MODIFIED.items())],
    'referenced': [{'package': p, 'by': sorted(set(v)), 'present': present(p)} for p, v in sorted(referenced.items())],
}
DOCS.mkdir(exist_ok=True)
(DOCS / 'InportAssetsDependencies.json').write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding='utf-8')
lines = ['# RetroRender の InportAssets 依存（自動生成: InventoryInportAssets.py）', '',
         '`Content/InportAssets/` は `.gitignore` 対象のため、以下はリポジトリに含まれません。', '',
         '## RetroRender が変更するパッケージ', '', '| パッケージ | 変更 | この環境に存在 |', '|---|---|---|']
lines += ['| `%s` | %s | %s |' % (r['package'], r['change'], 'yes' if r['present'] else '**no**') for r in result['modified']]
lines += ['', '## 追跡対象アセットから参照されるパッケージ', '', '| パッケージ | 参照元 | この環境に存在 |', '|---|---|---|']
lines += ['| `%s` | %s | %s |' % (r['package'], '<br>'.join('`%s`' % b for b in r['by'][:4]) + (' ほか' if len(r['by']) > 4 else ''),
                                  'yes' if r['present'] else '**no**') for r in result['referenced']]
(DOCS / 'InportAssetsDependencies.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
unreal.log('[RetroInventory] modified=%d referenced=%d missing=%d' % (
    len(result['modified']), len(result['referenced']),
    sum(not r['present'] for r in result['modified'] + result['referenced'])))
unreal.log('[RetroInventory] COMPLETE')
