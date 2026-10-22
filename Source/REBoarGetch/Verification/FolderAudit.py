import unreal, json
from pathlib import Path
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
roots = ['/Game/BP', '/Game/Animaiton', '/Game/DataAssets', '/Game/REBoarGetch', '/Game/Level', '/Game/Maps']
rows = [{'path': str(a.package_name), 'class': str(a.asset_class_path)} for p in roots for a in ar.get_assets_by_path(p, recursive=True)]
Path(__file__).with_suffix('.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
unreal.log('FOLDER_AUDIT_COMPLETE %d' % len(rows))
