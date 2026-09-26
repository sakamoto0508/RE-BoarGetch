import unreal
from pathlib import Path
root=Path(__file__).parent
exec((root/'create_stage_diorama.py').read_text(encoding='utf-8').split('try:run()')[0])
original_create=create
def create(path,cls,factory):
    a=unreal.load_asset(path)
    if a:ML.delete_all_material_expressions(a);return a
    return original_create(path,cls,factory)
custom_ui('M_UI_StageBackdrop','float2 g=abs(frac(UV*float2(96,54))-.5); float gridMask=smoothstep(.475,.495,max(g.x,g.y)); float vig=saturate(length((UV-.5)*float2(1.4,1))); float scanMask=.5+.5*sin(UV.y*1800); float2 cells=floor(UV*float2(32,18)); float seed=frac(sin(dot(cells,float2(12.9898,78.233)))*43758.5453); float2 q=abs(frac(UV*float2(32,18))-.5); float squares=step(.975,seed)*(1-step(.065,max(q.x,q.y))); return float4(float3(.003,.009,.025)+float3(.005,.045,.07)*gridMask*.32+float3(.01,.16,.3)*squares, .88+vig*.055+scanMask*.008);')
custom_ui('M_UI_DioramaRing','float2 p=(UV-.5)*2; p.y*=1.12; float r=length(p); float a=atan2(p.y,p.x); float rimMask=exp(-abs(r-.84)*220)+.45*exp(-abs(r-.94)*300); float halo=.13*exp(-abs(r-.84)*35); float ticks=step(.82,r)*step(r,.90)*step(.92,frac(a*14)); float amber=step(.96,frac(a*3+.2)); float3 col=lerp(float3(.01,.65,1),float3(1,.75,.08),amber); return float4(col, saturate(rimMask+halo+ticks*.45)*.8);')
