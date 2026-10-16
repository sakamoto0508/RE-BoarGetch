// Custom-node body embedded by SetupRetroAssets.py. One after-tonemap pass.
// Work in viewport UV; convert AFTER snapping/warping for the current input rect.
float2 size = max(GetSceneTextureViewSize(14).xy, 1.0);
float2 uv = GetViewportUV(Parameters);
float2 centered = uv * 2.0 - 1.0;
float2 warped = centered * (1.0 + CRTCurvature * dot(centered, centered));
uv = warped * 0.5 + 0.5;
float inside = all(uv >= 0.0) && all(uv <= 1.0);
float rows = min(TargetVerticalResolution, size.y);
float2 grid = max(float2(rows * size.x / size.y, rows), 1.0);
float2 snapped = (floor(uv * grid) + 0.5) / grid;
float2 sampleUV = lerp(uv, snapped, Pixelation);
float2 sceneUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(sampleUV),14),14);
float3 color = SceneTextureLookup(sceneUV,14,false).rgb;
// Four taps interpolate on the virtual pixel grid. This softens N64 at ANY output size,
// unlike bilinear filtering a single source texel at a fixed snapped coordinate.
if (SoftSampling > 0.001)
{
 float2 p = uv * grid - 0.5;
 float2 base = (floor(p) + 0.5) / grid;
 float2 f = frac(p);
 float2 a = ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(base),14),14);
 float2 b = ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(base+float2(1.0/grid.x,0)),14),14);
 float2 c = ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(base+float2(0,1.0/grid.y)),14),14);
 float2 d = ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(base+1.0/grid),14),14);
 float3 soft = lerp(lerp(SceneTextureLookup(a,14,true).rgb,SceneTextureLookup(b,14,true).rgb,f.x),
                    lerp(SceneTextureLookup(c,14,true).rgb,SceneTextureLookup(d,14,true).rgb,f.x),f.y);
 color = lerp(color,soft,SoftSampling);
}
if (ChromaticAberration > 0.001)
{
 float2 offset = float2(ChromaticAberration/size.x,0) * dot(centered,centered);
 color.r = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(sampleUV+offset),14),14),14,true).r;
 color.b = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(sampleUV-offset),14),14),14,true).b;
}
float luma = dot(color,float3(.2126,.7152,.0722));
color = saturate((lerp(luma.xxx,color,Saturation)-.5)*Contrast+.5);
color = saturate(color*Brightness);
// Static Bayer 4x4 anchored to virtual pixels: no time/random temporal noise.
if (ColorQuantization > 0.0001)
{
 const float bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 int2 cell = int2(floor(uv*grid)) & 3;
 float threshold = (bayer[cell.y*4+cell.x]+.5)/16.0-.5;
 float levels = exp2(8.0-6.0*ColorQuantization)-1.0;
 color = floor(saturate(color+threshold*DitheringStrength/levels)*levels+.5)/levels;
}
float scan = .5+.5*cos(uv.y*rows*6.2831853);
color *= 1.0-ScanlineStrength*scan;
color *= 1.0-VignetteStrength*saturate(pow(length(centered)*.70710678,2.0));
int stripe = int(floor(GetViewportUV(Parameters).x*size.x))%3;
float3 mask = stripe==0 ? float3(1,.65,.65) : (stripe==1 ? float3(.65,1,.65) : float3(.65,.65,1));
color *= lerp(float3(1,1,1),mask,PhosphorMask);
return saturate(color)*inside + SceneDependency.rgb*0.0;
