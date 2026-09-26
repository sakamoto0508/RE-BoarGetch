import unreal,math,json,traceback
from pathlib import Path
root=Path(__file__).parent
exec((root/'build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
REPORT={'assets':[]}
BASE='/Game/UI/StagePreview'
def finish(m):ML.layout_material_expressions(m);ML.recompile_material(m);save(m);return m
def ui(name):
    m=create(BASE+'/'+name,unreal.Material,unreal.MaterialFactoryNew());m.set_editor_property('material_domain',unreal.MaterialDomain.MD_UI);m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);return m
def custom_ui(name,code):
    m=ui(name);uv=node(m,unreal.MaterialExpressionTextureCoordinate)
    inp=unreal.CustomInput();inp.set_editor_property('input_name','UV')
    n=node(m,unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,inputs=[inp]);link(uv,n,'UV')
    rgb=node(m,unreal.MaterialExpressionComponentMask,r=True,g=True,b=True,a=False);link(n,rgb);output(rgb,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha=node(m,unreal.MaterialExpressionComponentMask,r=False,g=False,b=False,a=True);link(n,alpha);output(alpha,unreal.MaterialProperty.MP_OPACITY);return finish(m)
def run():
    mats={}
    for name,col in {'Grass':(.27,.56,.065),'Earth':(.31,.16,.065),'Dirt':(.66,.43,.16),'Water':(.015,.48,.65),'White':(.82,.9,.94),'Navy':(.035,.085,.14),'Cyan':(.02,.7,.9),'Yellow':(1,.67,.045),'Leaf':(.2,.47,.035)}.items():
        m=create(BASE+'/M_Preview_'+name,unreal.Material,unreal.MaterialFactoryNew());m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        normal=node(m,unreal.MaterialExpressionVertexNormalWS);direction=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(-.35,-.45,.82,1))
        dot=node(m,unreal.MaterialExpressionDotProduct);link(normal,dot,'A');link(direction,dot,'B')
        mult=node(m,unreal.MaterialExpressionMultiply,const_b=.3);link(dot,mult,'A');add=node(m,unreal.MaterialExpressionAdd,const_b=.7);link(mult,add,'A')
        colnode=node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*col,1));shade=node(m,unreal.MaterialExpressionMultiply);link(colnode,shade,'A');link(add,shade,'B');output(shade,unreal.MaterialProperty.MP_EMISSIVE_COLOR);mats[name]=finish(m)
    m=ui('M_UI_Diorama')
    # RenderTargets are created per visible preview instance at runtime.
    rt=create(BASE+'/RT_Diorama_Default',unreal.TextureRenderTarget2D,unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property('size_x',1024);rt.set_editor_property('size_y',768);rt.set_editor_property('render_target_format',unreal.TextureRenderTargetFormat.RTF_RGBA16F);rt.set_editor_property('clear_color',unreal.LinearColor(0,0,0,1));save(rt)
    sample=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='PreviewTexture',texture=rt,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    gray=node(m,unreal.MaterialExpressionDesaturation);link(sample,gray,'','RGB');link(constant(m,1),gray,'Fraction')
    sat=node(m,unreal.MaterialExpressionLinearInterpolate);link(gray,sat,'A');link(sample,sat,'B','RGB');link(scalar(m,'Saturation',1),sat,'Alpha');output(sat,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    inv=node(m,unreal.MaterialExpressionOneMinus);link(sample,inv,'','A');output(inv,unreal.MaterialProperty.MP_OPACITY);finish(m)
    custom_ui('M_UI_StageBackdrop','float2 g=abs(frac(UV*float2(96,54))-.5); float grid=1-smoothstep(.475,.495,max(g.x,g.y)); float v=saturate(length((UV-.5)*float2(1.4,1))); float scan=.5+.5*sin(UV.y*1800); return float4(float3(.003,.009,.025)+float3(.005,.045,.07)*(1-grid)*.32, .88+v*.055+scan*.008);')
    custom_ui('M_UI_DioramaRing','float2 p=(UV-.5)*2; p.y*=1.12; float r=length(p); float a=atan2(p.y,p.x); float line=exp(-abs(r-.84)*220)+.45*exp(-abs(r-.94)*300); float halo=.13*exp(-abs(r-.84)*35); float ticks=step(.82,r)*step(r,.90)*step(.92,frac(a*14)); float amber=step(.96,frac(a*3+.2)); float3 c=lerp(float3(.01,.65,1),float3(1,.75,.08),amber); return float4(c, saturate(line+halo+ticks*.45)*.8);')
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.BoarStagePreviewActor)
    bp=create(BASE+'/BP_Stage01_Diorama',unreal.Blueprint,factory)
    cdo=unreal.get_default_object(bp.generated_class());parts=[]
    meshes={n:unreal.load_asset('/Engine/BasicShapes/'+n) for n in ['Cube','Cylinder','Cone']}
    def part(shape,mat,p,size,rot=(0,0,0)):
        v=unreal.BoarPreviewPart();v.set_editor_property('mesh',meshes[shape]);v.set_editor_property('material',mats[mat]);v.set_editor_property('transform',unreal.Transform(location=unreal.Vector(*p),rotation=unreal.Rotator(*rot),scale=unreal.Vector(*(v/100 for v in size))));parts.append(v)
    part('Cylinder','Earth',(0,0,-40),(870,760,90));part('Cylinder','Grass',(0,0,8),(850,745,15))
    # The loop and four primary landmarks mirror Stage 1's visual roles, not its gameplay.
    for i in range(16):
        a=i*math.tau/16;part('Cube','Dirt',(250*math.cos(a),210*math.sin(a),19),(115,65,5),(0,math.degrees(a)+90,0))
    part('Cylinder','White',(0,0,24),(220,220,12));part('Cylinder','Cyan',(0,0,32),(190,190,4))
    part('Cube','Navy',(0,0,38),(130,130,10));part('Cube','Yellow',(0,0,140),(146,146,12))
    for i in [-1,0,1]:
        for side in [-1,1]:
            part('Cube','White',(side*63,i*58,90),(7,7,96));part('Cube','White',(i*58,side*63,90),(7,7,96))
    part('Cylinder','Water',(-270,90,22),(235,190,10));part('Cube','Dirt',(-240,70,35),(45,210,12),(0,-20,0))
    part('Cube','Earth',(255,165,38),(205,170,70));part('Cube','Grass',(255,165,80),(210,175,15))
    part('Cube','White',(265,175,124),(115,100,80));part('Cube','Navy',(265,175,173),(142,124,20));part('Cube','Cyan',(205,175,134),(4,70,35))
    part('Cone','White',(-180,-230,78),(105,105,135));part('Cube','Navy',(-236,-230,135),(14,30,30))
    for a in [0,90]:part('Cube','Yellow',(-246,-230,150),(8,175,17),(a,0,0))
    for x,y in [(330,-120),(160,-290),(-330,-80),(80,295)]:
        part('Cube','Earth',(x,y,42),(20,20,55));part('Cone','Leaf',(x,y,110),(120,120,140))
    cdo.set_editor_property('parts',parts);cdo.set_editor_property('ortho_width',1250)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp);save(bp)
    stage=unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig');stage.set_editor_property('preview_actor_class',bp.generated_class());save(stage)
    widget=unreal.load_asset('/Game/BP/Widget/WBP_LobbyStageSelect');unreal.get_default_object(widget.generated_class()).set_editor_property('preview_material',m);save(widget)
    REPORT['parts']=len(parts)
try:run()
except Exception:REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
finally:(root/'create_stage_diorama.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
