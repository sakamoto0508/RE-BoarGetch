import unreal,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
out={}
try:
 path='/Game/Lobby/Materials/M_DataPortal_Tunnel'
 m=create(path,unreal.Material,unreal.MaterialFactoryNew())
 m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);m.set_editor_property('two_sided',True)
 params={'EmissiveStrength':2.0,'GridDensity':7.0,'GridBrightness':.20,'ScrollSpeed':.18,'NoiseStrength':.16,'DistortionStrength':.006,'CenterBlackness':.96,'DataBrightness':.5,'RimBrightness':.7}
 inputs={'UV':node(m,unreal.MaterialExpressionTextureCoordinate),'Clock':node(m,unreal.MaterialExpressionTime)}
 inputs.update({k:scalar(m,k,v) for k,v in params.items()})
 inputs['PortalColor']=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='PortalColor',default_value=unreal.LinearColor(.015,.45,.85,1))
 ci=[]
 for k in inputs:
  x=unreal.CustomInput();x.set_editor_property('input_name',k);ci.append(x)
 custom=node(m,unreal.MaterialExpressionCustom,code=(ROOT/'DataPortal.usf').read_text(),output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,inputs=ci)
 for k,v in inputs.items():link(v,custom,k)
 rgb=node(m,unreal.MaterialExpressionComponentMask,r=True,g=True,b=True,a=False);link(custom,rgb);output(rgb,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 alpha=node(m,unreal.MaterialExpressionComponentMask,r=False,g=False,b=False,a=True);link(custom,alpha);output(alpha,unreal.MaterialProperty.MP_OPACITY_MASK)
 ML.layout_material_expressions(m);ML.recompile_material(m);save(m)
 mi=create('/Game/Lobby/Materials/MI_DataPortal_Default',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());ML.set_material_instance_parent(mi,m)
 for k,v in params.items():ML.set_material_instance_scalar_parameter_value(mi,k,v)
 ML.set_material_instance_vector_parameter_value(mi,'PortalColor',unreal.LinearColor(.015,.45,.85,1));save(mi)
 verts=[unreal.Vector(-8.8,0,3),unreal.Vector(8.8,0,3),unreal.Vector(8.8,0,26),unreal.Vector(-8.8,0,26)]
 dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=[unreal.IntVector(0,1,2),unreal.IntVector(0,2,3)],uv0=[unreal.Vector2D(0,1),unreal.Vector2D(1,1),unreal.Vector2D(1,0),unreal.Vector2D(0,0)]));dm.set_per_face_normals()
 smp='/Game/Lobby/Meshes/SM_DataPortal_Surface';assert not AS.does_asset_exist(smp)
 opt=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opt.set_editor_property('enable_collision',False);opt.set_editor_property('enable_nanite',False)
 sm,result=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,smp,opt);assert sm;sm.set_material(0,mi);save(sm)
 out={'assets':REPORT['assets'],'parameters':params}
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
(ROOT/'build_data_portal_material.json').write_text(json.dumps(out,indent=2))
