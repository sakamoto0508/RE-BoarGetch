"""Removes the leftover copies from the content organization (old paths) only when nothing but other leftover copies references them.
The live copies (new paths) are referenced by BP_NormalBoar / BP_PlayerCharacter / BP_BoarAIController / L_Archive / C++.
Also reports the dependencies of the presentation BPs. Writes Saved/CleanupDuplicateAssets.json"""
import unreal, json
E = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
OLD = ['/Game/BP/Boar/ABP_NormalBoar', '/Game/BP/Boar/A_Boar_Idle', '/Game/BP/Player/ABP_Player', '/Game/BP/Boar/BoarDataAsset',
       '/Game/BP/Boar/ST_Boar_Normal', '/Game/BP/Cage/Materials/M_Cage_White', '/Game/BP/Cage/Materials/M_Cage_Navy',
       '/Game/BP/Cage/Materials/M_Cage_Yellow', '/Game/BP/Cage/Materials/M_Cage_Glow', '/Game/Animaiton/Boar/BS_BoarLocomotion']
out = {'deps': {}, 'blocked': {}, 'deleted': [], 'remaining': []}
for bp in ['/Game/BP/Presentation/BP_StageClearPresentation', '/Game/BP/Presentation/BP_GameOverPresentation', '/Game/BP/Player/BP_BoarPlayerController']:
    out['deps'][bp] = [str(d) for d in ar.get_dependencies(bp, opt) if str(d).startswith('/Game')]
    out['deps'][bp + ' missing'] = [d for d in out['deps'][bp] if not E.does_asset_exist(d)]
for p in OLD:
    refs = [str(r) for r in (ar.get_referencers(p, opt) or []) if str(r) not in OLD]
    if refs:
        out['blocked'][p] = refs
for p in OLD:
    if p in out['blocked'] or not E.does_asset_exist(p):
        continue
    if E.delete_asset(p):
        out['deleted'].append(p)
for p in OLD:
    if E.does_asset_exist(p):
        out['remaining'].append(p)
for folder in ['/Game/Animaiton/Boar', '/Game/Animaiton', '/Game/BP/Cage/Materials']:
    if E.does_directory_exist(folder) and not E.list_assets(folder, recursive=True, include_folder=False):
        out.setdefault('removed_folders', []).append((folder, E.delete_directory(folder)))
open(unreal.Paths.project_saved_dir() + 'CleanupDuplicateAssets.json', 'w', encoding='utf-8').write(json.dumps(out, indent=1, ensure_ascii=False))
unreal.log('[CDA] COMPLETE')
