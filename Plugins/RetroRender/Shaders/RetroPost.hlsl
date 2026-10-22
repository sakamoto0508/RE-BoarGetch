// Custom-node body embedded by Scripts/SetupRetroAssets.py into M_RetroPost (BL_SceneColorAfterTonemapping).
// Every input below is a Material Scalar with the same name as FRetroRenderParameters (one source of truth).
// Work in viewport UV; convert to scene-texture UV only when sampling (handles PIE offsets / shared RTs).
float2 size = max(GetSceneTextureViewSize(14).xy, 1.0);
float2 viewUV = GetViewportUV(Parameters);
float2 centered = viewUV * 2.0 - 1.0;
float2 uv = (centered * (1.0 + CRTCurvature * dot(centered, centered))) * 0.5 + 0.5;
float inside = all(uv >= 0.0) && all(uv <= 1.0);

// Virtual pixel grid. X=0 keeps square pixels (width follows the real aspect: 240 rows -> 427 cols on 16:9,
// 320 on 4:3). An explicit X reproduces the stretched pixels of a 4:3 console image.
float rows = clamp(PixelResolutionY, 1.0, size.y);
if (IntegerScale > 0.5) rows = size.y / max(floor(size.y / rows), 1.0); // even block sizes, e.g. 1080p/240 -> 270 rows
float cols = PixelResolutionX > 0.5 ? min(PixelResolutionX, size.x) : rows * size.x / size.y;
float2 grid = max(float2(cols, rows), 1.0);
float2 cell = floor(saturate(uv) * grid - 1e-4 * Pixelation);
float2 snapped = (cell + 0.5) / grid;
float2 sampleUV = lerp(uv, snapped, Pixelation);

#define RETRO_TAP(p) SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(p), 14), 14), 14, false).rgb
float3 color = RETRO_TAP(sampleUV);

// Box filter inside the virtual pixel (4 taps at the quarter points): sub-pixel features of the native-resolution
// image (1px seams, shadow slivers, specular sparkle) become a tint instead of whole black/white virtual pixels.
if (PixelFilter > 0.001)
{
	float2 q = 0.25 / grid;
	float3 box = (RETRO_TAP(snapped + float2(-q.x, -q.y)) + RETRO_TAP(snapped + float2(q.x, -q.y))
	            + RETRO_TAP(snapped + float2(-q.x, q.y)) + RETRO_TAP(snapped + float2(q.x, q.y))) * 0.25;
	color = lerp(color, box, PixelFilter * Pixelation);
}

// N64: 3-point (triangle) filter between neighbouring virtual pixels instead of nearest.
if (SoftSampling > 0.001)
{
	float2 p = uv * grid - 0.5;
	float2 base = (floor(p) + 0.5) / grid;
	float2 f = frac(p);
	float3 a = RETRO_TAP(base);
	float3 b = RETRO_TAP(base + float2(1.0 / grid.x, 0));
	float3 c = RETRO_TAP(base + float2(0, 1.0 / grid.y));
	float3 d = RETRO_TAP(base + 1.0 / grid);
	float3 tri = (f.x + f.y <= 1.0) ? a + f.x * (b - a) + f.y * (c - a)
	                                : d + (1.0 - f.x) * (c - d) + (1.0 - f.y) * (b - d);
	color = lerp(color, tri, SoftSampling);
}

// Composite-video colour bleed: chroma smeared horizontally by whole virtual pixels, luma kept sharp.
if (ColorBleed > 0.001)
{
	float2 o = float2(ColorBleed / grid.x, 0);
	float3 l = RETRO_TAP(sampleUV - o);
	float3 r = RETRO_TAP(sampleUV + o);
	float3 avg = (l + color + r) / 3.0;
	float3 w = float3(.299, .587, .114);
	color = dot(color, w) + (avg - dot(avg, w));
}
if (ChromaticAberration > 0.001)
{
	float2 offset = float2(ChromaticAberration / size.x, 0) * dot(centered, centered);
	color.r = RETRO_TAP(sampleUV + offset).r;
	color.b = RETRO_TAP(sampleUV - offset).b;
}

// Depth fog (N64 style). Sampled at the virtual pixel so fog edges stay blocky.
if (FogStrength > 0.001)
{
	float depth = SceneTextureLookup(ClampSceneTextureUV(ViewportUVToSceneTextureUV(saturate(sampleUV), 1), 1), 1, false).r;
	float fog = saturate((depth - FogStart) / max(FogEnd - FogStart, 1.0)) * FogStrength;
	color = lerp(color, FogColor.rgb, fog);
}

float luma = dot(color, float3(.2126, .7152, .0722));
color = saturate((lerp(luma.xxx, color, Saturation) - .5) * Contrast + .5);
color = saturate(color * Brightness);

// Ordered dithering + per-channel quantisation. Bayer cell is anchored to the virtual pixel (stable, no temporal noise).
if (ColorQuantization > 0.0001)
{
	int2 q = int2(cell);
	int z = q.x ^ q.y;
	float threshold;
	if (DitherMatrixSize > 6.0)
	{
		int m = ((z & 1) << 5) | ((q.y & 1) << 4) | ((z & 2) << 2) | ((q.y & 2) << 1) | ((z & 4) >> 1) | ((q.y & 4) >> 2);
		threshold = (m + 0.5) / 64.0 - 0.5;
	}
	else
	{
		int m = ((z & 1) << 3) | ((q.y & 1) << 2) | (z & 2) | ((q.y & 2) >> 1);
		threshold = (m + 0.5) / 16.0 - 0.5;
	}
	float steps = max(ColorLevels, 2.0) - 1.0;
	float3 quantized = floor(saturate(color) * steps + 0.5 + threshold * DitheringStrength) / steps;
	color = lerp(color, saturate(quantized), ColorQuantization);
}

// CRT display layer: scanlines follow the virtual rows, phosphor mask the physical pixels.
float scan = .5 + .5 * cos(uv.y * rows * 6.2831853);
color *= 1.0 - ScanlineStrength * scan;
color *= 1.0 - VignetteStrength * saturate(pow(length(centered) * .70710678, 2.0));
int stripe = int(floor(viewUV.x * size.x)) % 3;
float3 mask = stripe == 0 ? float3(1, .65, .65) : (stripe == 1 ? float3(.65, 1, .65) : float3(.65, .65, 1));
color *= lerp(float3(1, 1, 1), mask, PhosphorMask);
#undef RETRO_TAP
return saturate(color) * inside + SceneDependency.rgb * 0.0 + DepthDependency.rgb * 0.0;
