import unreal, json
from pathlib import Path

project = Path(unreal.Paths.project_dir()).resolve()
existing_report = Path(__file__).with_name('OrganizeContent.json')
if existing_report.exists() and json.loads(existing_report.read_text(encoding='utf-8')).get('fresh_process_verification', {}).get('passed'):
    raise SystemExit('Migration verified; use VerifyOrganizedContent.py instead.')
backup = project / 'Saved/ContentOrganizationBackup/20261021_224855'
moves = json.loads((backup / 'moves.json').read_text(encoding='utf-8'))
E = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
# Restore a temporary loadable source for the BlendSpace before opening its AnimBlueprint.
# The first move left an unloaded graph referencing this old package.
blend_src = '/Game/Animaiton/Boar/BS_BoarLocomotion'
blend_dst = moves[blend_src]
if not E.does_asset_exist(blend_src):
    assert E.duplicate_asset(blend_dst, blend_src)
redirectors = []
for src, dst in moves.items():
    dest = E.load_asset(dst)
    assert dest and dest.get_path_name().split('.')[0] == dst, dst
    data = ar.get_asset_by_object_path(src + '.' + src.rsplit('/', 1)[1])
    if data.is_valid():
        old = data.get_asset()
        if str(data.asset_class_path.asset_name) == 'ObjectRedirector':
            redirectors.append(old)
        else:
            # Assets referenced by native constructors can leave the original package on disk.
            assert src.startswith('/Game/BP/Cage/Materials/') or src == blend_src, src
            assert E.consolidate_assets(dest, [old]), src
            data = ar.get_asset_by_object_path(src + '.' + src.rsplit('/', 1)[1])
            if data.is_valid() and str(data.asset_class_path.asset_name) == 'ObjectRedirector':
                redirectors.append(data.get_asset())
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True, include_searchable_names=False, include_soft_management_references=True, include_hard_management_references=True)
ref_packages = set()
for src in moves:
    ref_packages.update(str(p) for p in (ar.get_referencers(src, options) or []) if str(p).startswith('/Game/'))
packages = [unreal.load_package(p) for p in sorted(ref_packages)]
assert all(packages)
if packages:
    assert unreal.EditorLoadingAndSavingUtils.save_packages(packages, False)
boar_anim = E.load_asset('/Game/Animation/Boar/ABP_NormalBoar')
for node in unreal.ObjectIterator(unreal.AnimGraphNode_BlendSpacePlayer):
    if node.get_path_name().startswith(boar_anim.get_path_name() + ':'):
        state = node.get_editor_property('node')
        state.set_editor_property('blend_space', E.load_asset(blend_dst))
        node.set_editor_property('node', state)
unreal.BlueprintEditorLibrary.compile_blueprint(boar_anim)
assert E.save_loaded_asset(boar_anim, False)
for redirector in redirectors:
    assert redirector.get_class().get_name() == 'ObjectRedirector'
    assert E.delete_loaded_asset(redirector)
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
ar.scan_paths_synchronous(['/Game/BP', '/Game/Animation', '/Game/Animaiton', '/Game/Art/Materials/Cage', '/Game/AI', '/Game/DataAssets'], True)
checks = []
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True, include_searchable_names=False, include_soft_management_references=True, include_hard_management_references=True)
for src, dst in moves.items():
    asset = E.load_asset(dst)
    old = ar.get_asset_by_object_path(src + '.' + src.rsplit('/', 1)[1])
    checks.append({'source': src, 'destination': dst, 'class': asset.get_class().get_name(), 'source_remaining': old.is_valid(), 'old_referencers': [str(p) for p in (ar.get_referencers(src, options) or [])]})
remaining = [{'path': str(a.package_name), 'class': str(a.asset_class_path.asset_name)} for a in ar.get_assets_by_path('/Game/BP', recursive=True) if str(a.asset_class_path.asset_name) not in ('Blueprint', 'WidgetBlueprint')]
# Check concrete properties on reloaded runtime assets, in addition to registry paths.
player = E.load_asset('/Game/BP/Player/BP_PlayerCharacter')
boar = E.load_asset('/Game/BP/Boar/BP_NormalBoar')
controller = E.load_asset('/Game/BP/Player/BP_BoarPlayerController')
mesh_classes = {}
for bp in (player, boar):
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
    mesh_classes[bp.get_name()] = str(mesh.get_editor_property('anim_class').get_path_name())
component = unreal.get_default_object(controller.generated_class()).get_component_by_class(unreal.StageClearPresentationComponent)
montage = component.get_editor_property('victory_montage')
result = {'backup': str(backup), 'moves': checks, 'non_blueprint_assets_in_BP': remaining, 'animation_classes': mesh_classes, 'victory_montage': montage.get_path_name()}
Path(__file__).with_name('OrganizeContent.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
assert not remaining, remaining
assert not any(c['source_remaining'] or c['old_referencers'] for c in checks), checks
assert all(p.startswith('/Game/Animation/') for p in mesh_classes.values()), mesh_classes
assert montage.get_path_name().startswith('/Game/Animation/'), montage
unreal.log('CONTENT_ORGANIZATION_COMPLETE 12 assets; no old assets or referencers')
