"""Read-only: for each old/new pair from the content organization, report class, redirector state and referencers (hard+soft).
Writes Saved/AuditDuplicateAssets.json. No saves."""
import unreal, json
PAIRS = [('/Game/BP/Boar/ABP_NormalBoar', '/Game/Animation/Boar/ABP_NormalBoar'),
         ('/Game/BP/Boar/A_Boar_Idle', '/Game/Animation/Boar/A_Boar_Idle'),
         ('/Game/BP/Player/ABP_Player', '/Game/Animation/Player/ABP_Player'),
         ('/Game/BP/Boar/BoarDataAsset', '/Game/DataAssets/Boar/DA_Boar_Default'),
         ('/Game/BP/Boar/ST_Boar_Normal', '/Game/AI/Boar/ST_Boar_Normal'),
         ('/Game/BP/Cage/Materials/M_Cage_White', '/Game/Art/Materials/Cage/M_Cage_White'),
         ('/Game/BP/Cage/Materials/M_Cage_Navy', '/Game/Art/Materials/Cage/M_Cage_Navy'),
         ('/Game/BP/Cage/Materials/M_Cage_Yellow', '/Game/Art/Materials/Cage/M_Cage_Yellow'),
         ('/Game/BP/Cage/Materials/M_Cage_Glow', '/Game/Art/Materials/Cage/M_Cage_Glow'),
         ('/Game/Animaiton/Boar/BS_BoarLocomotion', '/Game/Animation/Boar/BS_BoarLocomotion')]
ar = unreal.AssetRegistryHelpers.get_asset_registry()
hard = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True)
soft = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=False)
out = []
def info(p):
    a = ar.get_assets_by_package_name(p)
    return {'path': p, 'class': [str(x.asset_class_path.asset_name) for x in a],
            'hard_referencers': [str(x) for x in (ar.get_referencers(p, hard) or [])],
            'soft_referencers': [str(x) for x in (ar.get_referencers(p, soft) or [])],
            'dependencies': [str(x) for x in (ar.get_dependencies(p, hard) or []) if str(x).startswith('/Game')]}
for old, new in PAIRS:
    out.append({'old': info(old), 'new': info(new)})
open(unreal.Paths.project_saved_dir() + 'AuditDuplicateAssets.json', 'w', encoding='utf-8').write(json.dumps(out, indent=1, ensure_ascii=False))
unreal.log('[ADA] COMPLETE')
