"""Read-only asset audit; run with UE's PythonScript commandlet."""
import json
from pathlib import Path
import unreal

result = {'blueprints': {}, 'stages': {}, 'levels': []}
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
paths = ['/Game/BP/Widget/WBP_LobbyStageSelect', '/Game/BP/Widget/WBP_StageSelectEntry',
         '/Game/BP/Lobby/BP_StageEntrance_Stage01', '/Game/BP/Lobby/BP_PC_Lobby', '/Game/BP/Lobby/BP_GM_Lobby']
for path in paths:
    bp = unreal.load_asset(path)
    if bp is None:
        raise RuntimeError('Missing BP ' + path)
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    props = ['bUseCarousel', 'CarouselDuration', 'StageListWidgetName', 'StageStatusWidgetName',
             'StageProgressWidgetName', 'StageNameTextWidgetName', 'StageDescriptionTextWidgetName',
             'TargetCaptureCountTextWidgetName', 'StageThumbnailImageWidgetName', 'StartStageButtonWidgetName',
             'CancelButtonWidgetName', 'StageEntryClass', 'PreviewMaterial', 'StageConfig', 'StageCatalog', 'LobbyWidgetClass']
    values = {}
    for name in props:
        try:
            values[name] = str(cdo.get_editor_property(name))
        except Exception:
            pass
    deps = registry.get_dependencies(path, unreal.AssetRegistryDependencyOptions(True, True, False, False, False))
    result['blueprints'][path] = {'class': cls.get_path_name(), 'settings': values, 'dependencies': [str(d) for d in deps]}
for data in registry.get_assets_by_path('/Game/DataAssets', recursive=True):
    asset = data.get_asset()
    if isinstance(asset, unreal.StageConfig):
        result['stages'][asset.get_path_name()] = {n: str(asset.get_editor_property(n)) for n in
            ['StageId', 'DisplayName', 'Level', 'UnlockCondition', 'SpecialCoinDefinitions', 'BoarSpawnDefinitions']}
for data in registry.get_assets_by_path('/Game/Level', recursive=True):
    if str(data.asset_class_path.asset_name) == 'World':
        result['levels'].append(str(data.package_name))
(Path(__file__).parent / 'StageSelectAudit.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('[StageSelectAudit] ' + json.dumps(result, ensure_ascii=False))
