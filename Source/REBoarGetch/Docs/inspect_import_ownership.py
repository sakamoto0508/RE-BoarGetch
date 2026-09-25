import unreal, json
out = {}
for path, names in [('/Game/StylizedEnvironment/Demo/Characters/Mannequins/Meshes/SK_Mannequin', ['sockets']), ('/Game/StylizedEnvironment/Textures/GateBaked_Bake1_pbr_normal', ['asset_import_data','thumbnail_info'])]:
    obj = unreal.load_asset(path)
    out[path] = {}
    for name in names:
        try:
            value = obj.get_editor_property(name)
            out[path][name] = str(value)
        except Exception as e:
            out[path][name] = str(e)
out['duplicate_object'] = str(getattr(unreal, 'duplicate_object', None))
unreal.log('IMPORT_OWNERSHIP ' + json.dumps(out))
