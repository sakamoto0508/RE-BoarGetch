"""Read-only integrity check of patched materials: WPO goes through the snap function (with the original chain
where one existed) and every texel-snapped sample has UV + gradient inputs. Prints [RetroCheck] OK/FAIL lines."""
import unreal
mel = unreal.MaterialEditingLibrary
CHAINED = {'M_LowPoly': 'MaterialExpressionAdd_5', 'M_LowPolyGrass': 'MaterialExpressionMultiply_2',
           'M_LowPolyWater': 'MaterialExpressionReroute_13'}
TARGETS = ['/Game/InportAssets/LevelPrototyping/Materials/M_FlatCol', '/Game/InportAssets/LPRiverForest/Materials/M_LowPoly',
           '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass', '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater',
           '/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World', '/Game/Stage01/Ground/Materials/M_Ground_LowPoly_World',
           '/Game/BP/Cage/Materials/M_Cage_White', '/Game/BP/Cage/Materials/M_Cage_Yellow', '/Game/BP/Cage/Materials/M_Cage_Glow',
           '/Game/BP/Cage/Materials/M_Cage_Navy', '/Game/Art/Rendering/Retro/Materials/M_RetroFBXPhong',
           '/Game/Art/Materials/Cage/M_Cage_White', '/Game/Art/Materials/Cage/M_Cage_Yellow', '/Game/Art/Materials/Cage/M_Cage_Glow',
           '/Game/Art/Materials/Cage/M_Cage_Navy', '/Game/InportAssets/Farm_House/Materials/MM_Hybrid_Trim',
           '/Game/Lobby/Materials/M_Lobby_CyanEdge', '/Game/Lobby/Materials/M_Architecture_SoftWhite',
           '/Game/Lobby/Materials/M_Architecture_CeilingWhite', '/Game/Lobby/Materials/M_StageSelect_HoloPanel',
           '/Game/Lobby/Materials/M_Facility_ExhibitHologram', '/Game/Lobby/Materials/M_Lobby_ResearchHologram',
           '/Game/Lobby/Materials/M_DataPortal_Tunnel', '/Game/InportAssets/Clinic/Materials/M_Default_Master',
           '/Game/InportAssets/Clinic/Materials/M_Translucent_Master', '/Game/InportAssets/Clinic/Materials/M_Lights',
           '/Game/InportAssets/Clinic/Materials/M_Screen', '/Game/InportAssets/Clinic/Materials/M_Grid',
           '/Game/InportAssets/Clinic/Materials/M_Grid2']
from pathlib import Path as _Path
import json as _json
_record = _Path(__file__).resolve().parent / 'PatchRetroMaterials.json'
if _record.exists():
    TARGETS += [t for t in _json.loads(_record.read_text(encoding='utf-8')).get('auto_targets', {}) if t not in TARGETS]
TARGETS = [t for t in TARGETS if unreal.EditorAssetLibrary.does_asset_exist(t)]
fails = 0
for path in TARGETS:
    m = unreal.load_asset(path)
    name = path.split('/')[-1]
    problems = []
    snap = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    if not snap or not str(snap.get_editor_property('desc')).startswith('RetroRender'):
        problems.append('WPO not through snap')
    elif name in CHAINED:
        src = mel.get_inputs_for_material_expression(m, snap)[0]
        if not src or src.get_name() != CHAINED[name]:
            problems.append('snap input %s != %s' % (src.get_name() if src else None, CHAINED[name]))
    for e in mel.get_material_expressions(m):
        if isinstance(e, unreal.MaterialExpressionTextureSample) and e.get_editor_property('mip_value_mode') == unreal.TextureMipValueMode.TMVM_DERIVATIVE:
            ins = dict(zip(mel.get_material_expression_input_names(e), mel.get_inputs_for_material_expression(m, e)))
            missing = [k for k in ('UVs', 'DDX(UVs)', 'DDY(UVs)') if not ins.get(k)]
            if missing:
                problems.append('%s missing %s' % (e.get_name(), missing))
    errors = mel.recompile_material(m)
    if errors:
        problems.append('compile %s' % errors[:1])
    fails += bool(problems)
    unreal.log('[RetroCheck] %s %s %s' % ('FAIL' if problems else 'OK', name, '; '.join(problems)))
unreal.log('[RetroCheck] COMPLETE fails=%d' % fails)
