import unreal,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'facility_equipment_structure.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
out={}
try:
 p='/Game/Lobby/Materials/M_Architecture_CeilingWhite';mat=unreal.load_asset(p)
 if not mat:
  mat=AT.create_asset('M_Architecture_CeilingWhite','/Game/Lobby/Materials',unreal.Material,unreal.MaterialFactoryNew());mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
  c=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector);c.set_editor_property('constant',unreal.LinearColor(.62,.65,.68,1));unreal.MaterialEditingLibrary.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);unreal.MaterialEditingLibrary.recompile_material(mat);assert AS.save_loaded_asset(mat)
 for level in ['L_GadgetLab','L_Lobby']:
  assert LS.load_level('/Game/Level/'+level);G.clear()
  if level=='L_GadgetLab':
   for x in [-800,0,800]:
    for y in [-450,450]:box('Ceiling',(x,y,749),(780,875,1))
  else:
   for x in [-1350,-450,450,1350]:
    for y in [-1000,0,1000]:box('Ceiling',(x,y,1601),(880,975,1))
  path='/Game/Lobby/Meshes/SM_'+level+'_CeilingDiffuser';assert not AS.does_asset_exist(path)
  verts=[];idx=[]
  for t in G['Ceiling']:
   i=len(verts);verts.extend(unreal.Vector(*v) for v in t);idx.append(unreal.IntVector(i,i+1,i+2))
  dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=idx,uv0=[unreal.Vector2D() for v in verts]));dm.set_per_face_normals()
  opts=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opts.set_editor_property('enable_collision',False);opts.set_editor_property('enable_nanite',False)
  sm,status=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opts);assert sm;sm.set_material(0,mat);assert AS.save_loaded_asset(sm)
  a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(),unreal.Rotator());a.set_actor_label('Architecture_CeilingDiffuser');a.set_folder_path('Art/Architecture')
  c=a.static_mesh_component;c.set_static_mesh(sm);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('can_ever_affect_navigation',False);c.set_cast_shadow(False)
  for a in EA.get_all_level_actors():
   if a.get_actor_label().startswith('Architecture_') and isinstance(a,unreal.RectLight):a.get_component_by_class(unreal.RectLightComponent).set_editor_property('intensity',500 if level=='L_GadgetLab' else 1200)
  assert LS.save_current_level();out[level]=path
 assert LS.load_level('/Game/Level/L_GadgetLab')
except Exception:out['error']=traceback.format_exc()
(ROOT/'tune_architecture_ceiling.json').write_text(json.dumps(out,indent=2))
