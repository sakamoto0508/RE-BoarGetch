"""Read-only integrity check of patched materials: WPO goes through the snap function (with the original chain
where one existed) and every texel-snapped sample has UV + gradient inputs. Prints [RetroCheck] OK/FAIL lines."""
from pathlib import Path as _Path
import sys as _sys
import unreal
_sys.path.insert(0, str(_Path(__file__).resolve().parent))
import retro_project  # noqa: E402  同じフォルダの共通モジュール
mel = unreal.MaterialEditingLibrary
retro_project.scan_plugin_content()
# 対象: プロジェクト設定の targets + プラグイン内の M_RetroFBXPhong + レベルスキャンで見つかったもの（パッチ記録）。
_config = retro_project.load_config()
CHAINED = _config['chained']
TARGETS = list(_config['targets']) + [retro_project.FBX_COPY]
TARGETS += [t for t in retro_project.load_record().get('auto_targets', {}) if t not in TARGETS]
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
