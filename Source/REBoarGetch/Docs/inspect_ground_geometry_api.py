import unreal
for name in ['DynamicMesh','GeometryScript_MeshEdits','GeometryScript_NewAssetUtils','GeometryScriptSimpleMeshBuffers','GeometryScriptCreateNewStaticMeshAssetOptions','GeometryScript_Normals']:
    cls = getattr(unreal, name, None)
    unreal.log('GROUND_API ' + name + ' ' + str(cls))
    if cls:
        for method in ['append_buffers_to_mesh','create_new_static_mesh_asset_from_mesh','set_per_face_normals','set_triangle_material_id','enable_material_ids']:
            fn = getattr(cls, method, None)
            if fn: unreal.log('GROUND_API ' + str(fn.__doc__))
