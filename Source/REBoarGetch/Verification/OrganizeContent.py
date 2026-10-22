"""Run with Unreal Python commandlet. Move through AssetTools, never filesystem moves."""
raise SystemExit('Historical migration already completed; use VerifyOrganizedContent.py for verification.')
import unreal, json, shutil
from pathlib import Path
from datetime import datetime

E = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
project = Path(unreal.Paths.project_dir()).resolve()
report = Path(__file__).with_suffix('.json')
moves = {
    '/Game/BP/Boar/A_Boar_Idle': '/Game/Animation/Boar/A_Boar_Idle',
    '/Game/BP/Boar/ABP_NormalBoar': '/Game/Animation/Boar/ABP_NormalBoar',
    '/Game/BP/Player/ABP_Player': '/Game/Animation/Player/ABP_Player',
    '/Game/BP/Boar/BoarDataAsset': '/Game/DataAssets/Boar/DA_Boar_Default',
    '/Game/BP/Boar/ST_Boar_Normal': '/Game/AI/Boar/ST_Boar_Normal',
}
for a in ar.get_assets_by_path('/Game/Animaiton', recursive=True):
    p = str(a.package_name)
    moves[p] = p.replace('/Game/Animaiton/', '/Game/Animation/')
for a in ar.get_assets_by_path('/Game/BP/Cage/Materials', recursive=True):
    p = str(a.package_name)
    moves[p] = p.replace('/Game/BP/Cage/Materials/', '/Game/Art/Materials/Cage/')
assert all(E.does_asset_exist(s) and not E.does_asset_exist(d) for s, d in moves.items())
assert len(moves) == 12, 'Scope changed; expected the approved 12 assets'
backup = project / 'Saved' / 'ContentOrganizationBackup' / datetime.now().strftime('%Y%m%d_%H%M%S')
packages, pending = set(moves), list(moves)
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True, include_searchable_names=False, include_soft_management_references=True, include_hard_management_references=True)
while pending:
    for ref in ar.get_referencers(pending.pop(), options):
        ref = str(ref)
        if ref.startswith('/Game/') and ref not in packages:
            packages.add(ref)
            pending.append(ref)
for package in sorted(packages):
    for suffix in ('.uasset', '.umap', '.uexp', '.ubulk'):
        rel = Path('Content') / (package[len('/Game/'):] + suffix)
        source = project / rel
        if source.is_file():
            target = backup / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
backup.mkdir(parents=True, exist_ok=True)
(backup / 'moves.json').write_text(json.dumps(moves, indent=2), encoding='utf-8')
for rel in ['Config/DefaultEngine.ini', 'Source/REBoarGetch/Private/Cage/CageVisualComponent.cpp', 'Source/REBoarGetch/Private/Stage/GadgetTestTarget.cpp']:
    dest = backup / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(project / rel, dest)
for source in (project / 'Source/REBoarGetch').rglob('*'):
    if source.is_file() and source.suffix in ('.cpp', '.h', '.py', '.txt', '.md'):
        dest = backup / source.relative_to(project)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
for src, dst in moves.items():
    assert E.rename_asset(src, dst), 'Move failed: ' + src
    unreal.log('ORGANIZED %s -> %s' % (src, dst))
redirectors = [ar.get_asset_by_object_path(s + '.' + s.rsplit('/', 1)[1]).get_asset() for s in moves]
redirectors = [r for r in redirectors if r and r.get_class().get_name() == 'ObjectRedirector']
raise RuntimeError('Migration already executed. Use FinishContentOrganization.py for the recorded migration; do not rerun moves.')
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
checks = []
for src, dst in moves.items():
    asset = E.load_asset(dst)
    assert asset and asset.get_path_name().split('.')[0] == dst, dst
    # Registry checks use the actual source asset entry, without following redirects.
    old = ar.get_asset_by_object_path(src + '.' + src.rsplit('/', 1)[1])
    checks.append({'source': src, 'destination': dst, 'class': asset.get_class().get_name(), 'source_remaining': old.is_valid()})
remaining = [{'path': str(a.package_name), 'class': str(a.asset_class_path.asset_name)} for a in ar.get_assets_by_path('/Game/BP', recursive=True) if str(a.asset_class_path.asset_name) not in ('Blueprint', 'WidgetBlueprint', 'ObjectRedirector')]
result = {'backup': str(backup), 'backed_up_packages': len(packages), 'moves': checks, 'non_blueprint_assets_in_BP': remaining}
report.write_text(json.dumps(result, indent=2), encoding='utf-8')
assert not remaining, remaining
assert not any(c['source_remaining'] for c in checks), 'Unresolved redirectors'
unreal.log('CONTENT_ORGANIZATION_COMPLETE %d assets' % len(moves))
