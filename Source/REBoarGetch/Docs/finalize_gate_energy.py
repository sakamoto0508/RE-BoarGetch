import unreal,math,json
from pathlib import Path
root=Path(__file__).parent
exec((root/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
m=unreal.load_asset('/Game/Lobby/Materials/M_StageSelect_HoloText');ML.delete_all_material_expressions(m)
glyph=node(m,unreal.MaterialExpressionFontSampleParameter,parameter_name='Font',font=unreal.load_asset('/Engine/EngineFonts/RobotoDistanceField'),font_texture_page=0)
sub=node(m,unreal.MaterialExpressionSubtract,const_b=.45);link(glyph,sub,'A','R')
div=node(m,unreal.MaterialExpressionDivide,const_b=.1);link(sub,div,'A')
sat=node(m,unreal.MaterialExpressionSaturate);link(div,sat);output(sat,unreal.MaterialProperty.MP_OPACITY)
output(node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.2,2.2,3.3,1)),unreal.MaterialProperty.MP_EMISSIVE_COLOR);ML.recompile_material(m);save(m)
GROUPS={}
def p(x,ry,rz,t):return(x,ry*math.cos(t),380+rz*math.sin(t))
for i in range(12):
    a=math.pi*i/12;b=math.pi*(i+1)/12
    quad('Ivory',p(1100,240,360,a),p(1100,425,490,a),p(1100,425,490,b),p(1100,240,360,b))
    quad('Ivory',p(1310,240,360,b),p(1310,425,490,b),p(1310,425,490,a),p(1310,240,360,a))
    quad('Ivory',p(1100,425,490,a),p(1310,425,490,a),p(1310,425,490,b),p(1100,425,490,b))
    quad('Navy',p(1094,244,366,a),p(1094,302,424,a),p(1094,302,424,b),p(1094,244,366,b))
    quad('Navy',p(1100,240,360,a),p(1100,240,360,b),p(1310,240,360,b),p(1310,240,360,a))
    quad('Glow',p(1087,252,374,a),p(1087,272,394,a),p(1087,272,394,b),p(1087,252,374,b))
for n,faces in GROUPS.items():
    verts=[];inds=[]
    for f in faces:
        i=len(verts);verts.extend(unreal.Vector(*v) for v in f);inds.append(unreal.IntVector(i,i+2,i+1))
    dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=inds,uv0=[unreal.Vector2D(v.y/100,v.z/100) for v in verts]));dm.set_per_face_normals()
    mesh=unreal.load_asset('/Game/Lobby/Meshes/SM_GateEnergy_'+n)
    unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm,mesh,unreal.GeometryScriptCopyMeshToAssetOptions(),unreal.GeometryScriptMeshWriteLOD());save(mesh)
for a in EA.get_all_level_actors():
    if a.get_actor_label()=='Stage1Sign':
        t=a.get_components_by_class(unreal.TextRenderComponent)[0];t.set_vertical_alignment(unreal.VerticalTextAligment.EVRTA_TEXT_CENTER);t.set_world_size(90)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
