import unreal
mel = unreal.MaterialEditingLibrary
def walk(exprs, getf, seen, out):
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionCollectionParameter):
            c = e.get_editor_property('collection')
            out.add((c.get_path_name() if c else None, str(e.get_editor_property('parameter_name'))))
        if isinstance(e, unreal.MaterialExpressionMaterialFunctionCall):
            f = e.get_editor_property('material_function')
            if f and f.get_path_name() not in seen and isinstance(f, unreal.MaterialFunction):
                seen.add(f.get_path_name())
                walk(mel.get_material_function_expressions(f), getf, seen, out)
for p in ['/Game/InportAssets/LPRiverForest/Materials/M_LowPoly', '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass', '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater', '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyTerrain']:
    out = set()
    walk(mel.get_material_expressions(unreal.load_asset(p)), None, set(), out)
    unreal.log('[MPC] %s %s' % (p.split('/')[-1], sorted(out, key=str)))
unreal.log('[MPC] COMPLETE')
