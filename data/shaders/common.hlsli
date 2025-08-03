// global space - topmost space, objects in the engine are placed in this space
// world space - centered on camera, orientation same as global space, we send position from cpu to gpu in this space (in most cases)
// view space - centered on camera, orientation matches camera orientation
// local space - a.k.a. object space, centered on object, orientation matches object orientation 
// NDC space - normalized device coordinates, z is in [0, 1] range
// we use reverse z with infinite far plane
// space is usually specified in the name of the variable by postfix, WS for world space, VS for view space, etc.
// we use camera centered world space for rendering to avoid floating point precision issues with large world

#define M_PI 3.14159265359
#define ONE_BY_PI (1 / 3.14159265359)

static const float2 POISSON_DISK_4[4] = {
  float2( -0.94201624, -0.39906216 ),
  float2( 0.94558609, -0.76890725 ),
  float2( -0.094184101, -0.92938870 ),
  float2( 0.34495938, 0.29387760 )
};

static const float2 POISSON_DISK_16[16] = {
	float2(0.3568125,-0.5825516),
	float2(-0.2828444,-0.1149732),
	float2(-0.2575171,-0.579991),
	float2(0.3328768,-0.0916517),
	float2(-0.0177952,-0.9652126),
	float2(0.7636694,-0.3370355),
	float2(0.9381924,0.05975571),
	float2(0.6547356,0.373677),
	float2(-0.1999273,0.4483816),
	float2(0.167026,0.2838214),
	float2(0.2164582,0.6978411),
	float2(-0.7202712,-0.07400024),
	float2(-0.6624036,0.559697),
	float2(-0.1909649,0.8721116),
	float2(-0.6493049,-0.4945979),
	float2(0.6104985,0.7838438)
};

struct GBufferOutput {
	float4 gbuffer0 : SV_Target0;
	float4 gbuffer1 : SV_Target1;
	float4 gbuffer2 : SV_Target2;
	float4 gbuffer3 : SV_Target3;
};

struct SMSlice {
	float3x4 world_to_slice;
	float size;
	float rcp_size;
	float size_world;
	float texel_world;
};

struct Light {
	float4 pos_radius;
	float4 rot;
	float4 color_attn;
	int atlas_idx;
	float fov;
	float2 padding;
};

struct ReflectionProbe {
	float4 pos_layer;
	float4 rot;
	float4 half_extents;
};

struct EnvProbe {
	float4 pos;
	float4 rot;
	float4 inner_range;
	float4 outer_range;
	float4 sh_coefs0;
	float4 sh_coefs1;
	float4 sh_coefs2;
	float4 sh_coefs3;
	float4 sh_coefs4;
	float4 sh_coefs5;
	float4 sh_coefs6;
	float4 sh_coefs7;
	float4 sh_coefs8;
};

struct Cluster {
	int offset;
	int lights_count;
	int env_probes_count;
	int refl_probes_count;
};

struct Surface {
	float3 albedo;
	float alpha;
	float roughness;
	float metallic;
	float emission;
	float translucency;
	float ao;
	float shadow;
	float3 N;
	float3 V;
	float3 pos_ws;
	float2 motion;
};

cbuffer GlobalState : register(b0) {
	SMSlice Global_sm_slices[4];
	float4x4 Global_vs_to_ndc; // a.k.a. projection matrix
	float4x4 Global_prev_vs_to_ndc; // previous frame vs_to_ndc
	float4x4 Global_vs_to_ndc_no_jitter;
	float4x4 Global_prev_vs_to_ndc_no_jitter;
	float4x4 Global_ndc_to_vs;
	float4x4 Global_ws_to_vs; // a.k.a. view matrix
	float4x4 Global_vs_to_ws;
	float4x4 Global_ws_to_ndc; // a.k.a. view-projection matrix
	float4x4 Global_ws_to_ndc_no_jitter;
	float4x4 Global_prev_ws_to_ndc_no_jitter;
	float4x4 Global_ndc_to_ws;
	float4x4 Global_reprojection;
	float4 Global_camera_world_pos;
	float4 Global_view_dir;
	float4 Global_fog_scattering;
	float4 Global_to_prev_frame_camera_translation;
	float4 Global_light_dir;
	float4 Global_light_color;
	uint2 Global_random_uint2;
	float2 Global_random_float2_normalized;
	int2 Global_framebuffer_size;
	float2 Global_rcp_framebuffer_size;
	float2 Global_pixel_jitter;
	float2 Global_prev_pixel_jitter;
	float Global_fog_enabled;
	float Global_fog_top;
	float Global_light_intensity;
	float Global_light_indirect_intensity;
	float Global_time;
	float Global_frame_time_delta;
	float Global_shadow_depth_range;
	float Global_shadow_rcp_depth_range;
	uint Global_frame_index;
	uint Global_shadowmap;
	uint Global_shadow_atlas;
	uint Global_reflection_probes;
};

cbuffer PassState : register(b1) {
	float4x4 Pass_vs_to_ndc;
	float4x4 Pass_ndc_to_vs;
	float4x4 Pass_ws_to_vs;
	float4x4 Pass_vs_to_ws;
	float4x4 Pass_ws_to_ndc;
	float4x4 Pass_ndc_to_ws;
	float4 Pass_view_dir;
	float4 Pass_camera_up;
	float4 Pass_camera_planes[6];
	float4 Pass_shadow_to_camera;
};

cbuffer ShadowAtlas : register(b3) {
	float4x4 u_shadow_atlas_matrices[128];
};

// optimized mul(float4(pos, 1), m)
float4 transformPosition(float3 pos, float4x4 m) {
	return pos.x * m[0] + (pos.y * m[1] + (pos.z * m[2] + m[3]));
}


// assumes pos.w == 1
float4 transformPosition(float4 pos, float4x4 m) {
	return pos.x * m[0] + (pos.y * m[1] + (pos.z * m[2] + m[3]));
}

// assumes transformPosition(pos, m0).w == 1
// so dont use with something like ws_to_ndc in m0
float4 transformPosition(float3 pos, float4x4 m0, float4x4 m1) {
	float4 p = transformPosition(pos, m0);
	return p.x * m1[0] + (p.y * m1[1] + (p.z * m1[2] + m1[3]));
}

float3 rotateByQuat(float4 rot, float3 pos) {
	float3 uv = cross(rot.xyz, pos);
	float3 uuv = cross(rot.xyz, uv);
	uv *= (2.0 * rot.w);
	uuv *= 2.0;

	return pos + uv + uuv;
}

float unpackEmission(float emission) {
	return (exp2(emission) - 1) * 64;
}

float packEmission(float emission) {
	return log2(1 + emission / 64.0);
}

float2 raySphereIntersect(float3 r0, float3 rd, float3 s0, float sr) {
	float3 s0_r0 = s0 - r0;
	float tc = dot(s0_r0, rd);
	float d2 = dot(s0_r0, s0_r0) - tc * tc;
	float sr2 = sr * sr;
	[flatten] // to avoid compiler warning
	if (d2 > sr2) return -1;
	float td2 = sr2 - d2;
	float td = sqrt(td2);
	return float2(tc - td, tc + td);
}

float2 toScreenUV(float2 uv) {
	#ifdef _ORIGIN_BOTTOM_LEFT
		return uv;
	#else
		return float2(uv.x, 1 - uv.y);
	#endif
}

float luminance(float3 color) {
	return dot(float3(0.2126729, 0.7151522, 0.0721750), color);
}

uint2 textureSize(Texture2D<float4> Tex, uint Level) {
	uint2 ret;
	uint dummy;
	Tex.GetDimensions(Level, ret.x, ret.y, dummy);
	return ret;
}

uint2 textureSize(TextureCube<float4> Tex, uint Level) {
	uint2 ret;
	uint dummy;
	Tex.GetDimensions(Level, ret.x, ret.y, dummy);
	return ret;
}

// returns view vector, i.e. normalized vector in world-space pointing from camera to pixel
float3 getViewDirection(float2 screen_uv) {
	float4 pos_ndc = float4(toScreenUV(screen_uv) * 2 - 1, 1, 1.0);
	float4 pos_ws = mul(pos_ndc, Global_ndc_to_ws);
	return normalize(pos_ws.xyz);
}

// get world-space position of pixel at `screen_uv`
float3 getPositionWS(uint depth_buffer, float2 screen_uv) {
	float z = sampleBindlessLod(LinearSamplerClamp, depth_buffer, screen_uv, 0).r;
	float4 pos_ndc = float4(toScreenUV(screen_uv) * 2 - 1, z, 1.0);
	float4 pos_ws = mul(pos_ndc, Global_ndc_to_ws);
	return pos_ws.xyz / pos_ws.w;
}

// get world-space position of pixel at `tex_coord`, also returns its NDC depth
float3 getPositionWS(uint depth_buffer, float2 screen_uv, out float ndc_depth) {
	float z = sampleBindlessLod(LinearSamplerClamp, depth_buffer, screen_uv, 0).r;
	float4 pos_ndc = float4(toScreenUV(screen_uv) * 2 - 1, z, 1.0);
	float4 pos_ws = mul(pos_ndc, Global_ndc_to_ws);
	ndc_depth = z;
	return pos_ws.xyz / pos_ws.w;
}

Surface unpackSurface(float2 uv, uint gbuffer0, uint gbuffer1, uint gbuffer2, uint gbuffer3, uint gbuffer_depth, out float ndc_depth) {
	float4 gb0 = sampleBindless(LinearSamplerClamp, gbuffer0, uv);
	float4 gb1 = sampleBindless(LinearSamplerClamp, gbuffer1, uv);
	float4 gb2 = sampleBindless(LinearSamplerClamp, gbuffer2, uv);
	float4 gb3 = sampleBindless(LinearSamplerClamp, gbuffer3, uv);

	Surface surface;
	surface.albedo = gb0.rgb;
	surface.N = gb1.rgb * 2 - 1;
	surface.roughness = gb0.a;
	surface.metallic = gb2.z;
	surface.emission = unpackEmission(gb2.x);
	surface.pos_ws = getPositionWS(gbuffer_depth, uv, ndc_depth);
	surface.V = normalize(-surface.pos_ws);
	surface.translucency = gb2.y;
	surface.ao = gb1.w;
	surface.shadow = gb2.w;
	surface.motion = gb3.xy;
	return surface;
}

GBufferOutput packSurface(Surface surface) {
	GBufferOutput res;
	res.gbuffer0 = float4(surface.albedo.rgb, surface.roughness);
	res.gbuffer1 = float4(surface.N * 0.5 + 0.5, surface.ao);
	res.gbuffer2 = float4(packEmission(surface.emission), surface.translucency, surface.metallic, surface.shadow);
	res.gbuffer3 = float4(surface.motion, 0, 0);
	return res;
}

float3 ACESFilm(float3 x) {
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float2 computeStaticObjectMotionVector(float3 pos_ws) {
	float4 p = transformPosition(pos_ws, Global_ws_to_ndc_no_jitter);
	float4 pos_projected = transformPosition(pos_ws + Global_to_prev_frame_camera_translation.xyz, Global_prev_ws_to_ndc_no_jitter);
	return pos_projected.xy / pos_projected.w - p.xy / p.w;	
}

// can be used in VS to draw a triangle covering whole screen
float4 fullscreenQuad(int vertexID, out float2 screen_uv) {
	screen_uv = float2((vertexID & 1) * 2, vertexID & 2);
	return float4(toScreenUV(screen_uv) * 2 - 1, 0, 1);
}

// converts ndc depth to linear depth, using Global_vs_to_ndc
// we assume reversed z with infinite far plane
float toLinearDepth(float ndc_depth) {
	// float4 pos_proj = float4(0, 0, ndc_depth, 1.0);
	// float4 view_pos = mul(pos_proj, inv_proj);
	// return view_pos.z / view_pos.w;
	// for reversed z with infinite far plane, this is equivalent to:
	return Global_vs_to_ndc[3].z / ndc_depth;
}

StructuredBuffer<Light> b_lights : register(t0);
StructuredBuffer<Cluster> b_clusters : register(t1);
StructuredBuffer<int> b_cluster_map : register(t2);
StructuredBuffer<EnvProbe> b_env_probes : register(t3);
StructuredBuffer<ReflectionProbe> b_refl_probes : register(t4);

Cluster getClusterLinearDepth(float linear_depth, float2 frag_coord) {
	int3 cluster;
	int2 fragcoord = int2(frag_coord.xy);
	#ifndef _ORIGIN_BOTTOM_LEFT
		fragcoord.y = Global_framebuffer_size.y - fragcoord.y - 1;
	#endif

	cluster = int3(fragcoord.xy / 64.0, 0);
	cluster.z = int(log(linear_depth) * 16 / (log(10000 / 0.1)) - log(0.1) * 16 / log(10000 / 0.1));
	cluster.z = min(cluster.z, 15);
	int2 tiles = int2((Global_framebuffer_size + 63) / 64.0);
	uint idx = cluster.x + cluster.y * tiles.x + cluster.z * tiles.x * tiles.y;
	return b_clusters[idx];
}

// TODO uint2 frag_coord
Cluster getCluster(float ndc_depth, float2 frag_coord) {
	int2 ifrag_coord = int2(frag_coord.xy);
	#ifndef _ORIGIN_BOTTOM_LEFT
		ifrag_coord.y = Global_framebuffer_size.y - ifrag_coord.y - 1;
	#endif

	int3 cluster = int3(ifrag_coord.xy / 64.0, 0);
	float linear_depth = toLinearDepth(ndc_depth);
	cluster.z = int(log(linear_depth) * 16 / (log(10000 / 0.1)) - log(0.1) * 16 / log(10000 / 0.1));
	cluster.z = min(cluster.z, 15);
	int2 tiles = int2((Global_framebuffer_size + 63) / 64.0);
	cluster.y = tiles.y - 1 - cluster.y;
	cluster = clamp(cluster, 0, int3(tiles - 1, 15));
	uint idx = cluster.x + cluster.y * tiles.x + cluster.z * tiles.x * tiles.y;
	return b_clusters[idx];
}

Cluster getCluster(float ndc_depth, uint2 frag_coord, out uint3 cluster_coord) {
	#ifndef _ORIGIN_BOTTOM_LEFT
		frag_coord.y = Global_framebuffer_size.y - frag_coord.y - 1;
	#endif

	uint3 cluster = uint3(frag_coord.xy / 64.0, 0);
	float linear_depth = toLinearDepth(ndc_depth);
	cluster.z = int(log(linear_depth) * 16 / (log(10000 / 0.1)) - log(0.1) * 16 / log(10000 / 0.1));
	cluster.z = min(cluster.z, 15);
	int2 tiles = int2((Global_framebuffer_size + 63) / 64.0);
	cluster.y = tiles.y - 1 - cluster.y;
	cluster = clamp(cluster, 0, int3(tiles - 1, 15));
	uint idx = cluster.x + cluster.y * tiles.x + cluster.z * tiles.x * tiles.y;
	cluster_coord = cluster;
	return b_clusters[idx];
}

#define GLYPH_WIDTH 4
#define GLYPH_HEIGHT 6

// https://www.shadertoy.com/view/MfsBWf
// mask for single digit
int getGlyphMask(int2 pos, int glyph) {
	if (pos.x >= 0 && pos.x < GLYPH_WIDTH && pos.y >= 0 && pos.y < GLYPH_HEIGHT) {
		glyph >>= GLYPH_WIDTH * (GLYPH_HEIGHT - 1 - pos.y) + GLYPH_WIDTH - 1 - pos.x;
		return glyph & 1;
	}
	return 0;
}

// mask for whole number
int getNumberMask(int2 pos, uint number) {
	static const int char_spacing = 1;
	static const int glyphs[] = { 6920598, 2499111, 6885967, 6889878, 1268209, 16310678, 6875542, 15803460, 6908310, 6919958 };
	uint num_digits = uint(log10(number)) + 1;
	uint index = pos.x / (GLYPH_WIDTH + char_spacing);
	pos.x -= index * (GLYPH_WIDTH + char_spacing);

	if (index >= num_digits) return 0;
	int glyph = (number / pow(10, num_digits - index - 1)) % 10;
	if (index == 0 && glyph == 0) return 0;
	return getGlyphMask(pos, glyphs[glyph]);
}

// use this to print numbers in a shader
// usage: color = computeColor(frag_coord); print(color, frag_coord - number_origin, number_to_print);
void print(in out float3 color, int2 pos_glyph_space, uint number) {
	if (getNumberMask(pos_glyph_space, number)) color = 1;
}

float hash(float3 seed) {
	float dot_product = dot(seed, float3(12.9898,78.233,45.164));
	return frac(sin(dot_product) * 43758.5453);
}

float hash(float2 st) {
	return frac(sin(dot(st.xy, float2(12.9898,78.233))) * 43758.5453123);
}

float getShadowSimple(uint shadowmap, float3 wpos) {
	float4 pos = float4(wpos, 1);

	for (int slice = 0; slice < 4; ++slice) {
		float3 sc = mul(Global_sm_slices[slice].world_to_slice, pos).xyz;
		if (all(sc.xyz < 0.99) && all(sc.xyz > 0.01)) {
			float2 sm_uv = float2(sc.x * 0.25 + slice * 0.25, sc.y);
			float shadow = 0;
			float receiver = sc.z;
			float occluder = sampleBindlessLod(LinearSamplerClamp, shadowmap, sm_uv, 0).r;
			return saturate((receiver - occluder) * 10e3);
		}
	}
	return 1;
}

float getShadow(uint shadowmap, float3 wpos, float3 N, float2 frag_coord) {
	float NdL = saturate(dot(N, Global_light_dir.xyz));
	float4 pos = float4(wpos, 1);
	
	for (int slice = 0; slice < 4; ++slice) {
		float3 sc = mul(Global_sm_slices[slice].world_to_slice, pos);
		
		if (all(sc.xyz < 0.99) && all(sc.xyz > 0.01)) {
			float c = hash(frag_coord) * 2 - 1;
			float s = sqrt(1 - c * c); 
			float2x2 rot = float2x2(c, s, -s, c);
			float2 sm_uv = float2(sc.x * 0.25 + slice * 0.25, sc.y);
			float shadow = 0;
			float receiver = sc.z;
			
			float bias = (0.01 + Global_sm_slices[slice].texel_world / max(NdL, 0.1)) * Global_shadow_rcp_depth_range;
			for (int j = 0; j < 16; ++j) {
				float2 pcf_offset = mul(rot, POISSON_DISK_16[j]);
				float2 uv = sm_uv + pcf_offset * float2(0.25, 1) * Global_sm_slices[slice].rcp_size * 3;

				float occluder = sampleBindlessLod(LinearSamplerClamp, shadowmap, uv, 0).r;
				shadow += receiver > occluder - length(pcf_offset) * bias * 3 ? 1 : 0;
			}
			return shadow / 16.0;
		}
	}
	return 1;
}

float3 F_Schlick(float cos_theta, float3 F0) {
	return lerp(F0, 1.0, pow(1.0 - cos_theta, 5.0)); 
}

float3 computeDirectLight(Surface surface, float3 L, float3 light_color) {
	float3 F0 = 0.04;
	F0 = lerp(F0, surface.albedo, surface.metallic);		
	
	float ndotv = abs(dot(surface.N, surface.V)) + 1e-5f;
	float3 H = normalize(surface.V + L);
	float ldoth = saturate(dot(L, H));
	float ndoth = saturate(dot(surface.N, H));
	float ndotl_full = dot(surface.N, L);
	float ndotl = saturate(ndotl_full);
	float hdotv = saturate(dot(H, surface.V));
	
	// D GGX
	float a = surface.roughness * surface.roughness;
	float a2 = a * a;
	float f = max(1e-5, (ndoth * ndoth) * (a2 - 1) + 1);
	float D = a2 / (f * f * M_PI);

	// V Smith GGX height-correlated approximation
	float GGXV = ndotl * (ndotv * (1.0 - a) + a);
	float GGXL = ndotv * (ndotl * (1.0 - a) + a);
	float V = 0.5 / max(1e-5, (GGXV + GGXL));

	// F Schlick 
	float3 F = F_Schlick(hdotv, F0);// mix(F0, float3(1), pow(1.0 - hdotv, 5.0)); 
	float3 specular = D * V * F;
	
	float kD = 1.0 - surface.metallic;
	
	float3 diffuse = kD * surface.albedo / M_PI;
	return (diffuse + specular) * light_color * ndotl
		+ surface.translucency * diffuse * light_color * max(0, -ndotl_full);
}

// must match ShadowAtlas::getUV
float getShadowAtlasResolution(int idx) {
	[flatten] // to avoid compiler warning
	if (idx == 0) return 1024;
	if (idx < 5) return 512;
	return 256;
}

// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
// https://x.com/Stubbesaurus/status/937994790553227264
float3 fromOctahedral(float2 f) {
	f = f * 2.0 - 1.0;
	float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = saturate(-n.z);
	n.xy += n.xy >= 0.0 ? -t : t;
	return normalize(n);
}

float2 toOctahedral(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	float2 wrapped = (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
	n.xy = n.z >= 0.0 ? n.xy : wrapped;
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}

struct AtlasTile {
	float2 offset;
	float scale;
};

AtlasTile getShadowAtlasTile(uint atlas_idx) {
	AtlasTile res;
	if (atlas_idx < 1) {
		res.offset = float2(0, 0);
		res.scale = 0.5f;
	}
	else if (atlas_idx < 5) {
		res.offset.x = 0.5f + ((atlas_idx - 1) % 2) * 0.25f;
		res.offset.y = ((atlas_idx - 1) / 2) * 0.25f;
		res.scale = 0.25f;
	}
	else {
		res.offset.x = ((atlas_idx - 5) % 8) * 0.125f;
		res.offset.y = 0.5f + ((atlas_idx - 5) / 8) * 0.125f;
		res.scale = 0.125f;
	}
	return res;
}

// TODO clean this up
float3 pointLightsLighting(Cluster cluster, Surface surface, uint shadow_atlas, float2 frag_coord) {
	float3 res = 0;
	for (int i = cluster.offset; i < cluster.offset + cluster.lights_count; ++i) {
		Light light = b_lights[b_cluster_map[i]];
		float3 lpos = surface.pos_ws.xyz - light.pos_radius.xyz;
		float dist = length(lpos);
		float attn = pow(max(0, 1 - dist / light.pos_radius.w), light.color_attn.w);
		float3 L = -lpos / dist;
		if (attn > 1e-5) {
			float3 direct_light = computeDirectLight(surface, L, light.color_attn.rgb);
			int atlas_idx = light.atlas_idx;
			float fov = light.fov;
			
			if (atlas_idx >= 0) {
				if (fov > M_PI) {
					AtlasTile tile = getShadowAtlasTile(atlas_idx);
					float2 oct_uv = toOctahedral(-L) * tile.scale + tile.offset;
					float3 abslpos = abs(lpos);
					float receiver = max(abslpos.z, max(abslpos.x, abslpos.y)) * 0.99;
					
					float c = hash(frag_coord) * 2 - 1;
					float s = sqrt(1 - c * c); 
					float2x2 rot = float2x2(c, s, -s, c);

					// TODO PCF
					// TODO do not hardcode near plane
					float occluder = 0.1 / sampleBindlessLod(LinearSamplerClamp, shadow_atlas, oct_uv, 0).r;
					float shadow = saturate((occluder - receiver) * 1e2);
					
					attn *= shadow;
				}
				else {
					float4 proj_pos = transformPosition(lpos, u_shadow_atlas_matrices[atlas_idx]);
					proj_pos /= proj_pos.w;

					float2 shadow_uv = proj_pos.xy;

					float c = hash(frag_coord) * 2 - 1;
					float s = sqrt(1 - c * c); 
					float2x2 rot = float2x2(c, s, -s, c);
					float shadow = 0;
					float receiver = proj_pos.z;
					for (int j = 0; j < 16; ++j) {
						float2 pcf_offset = mul(rot, POISSON_DISK_16[j]);
						float2 uv = shadow_uv + pcf_offset * float2(0.25, 1) / getShadowAtlasResolution(atlas_idx) * 3;

						float occluder = sampleBindlessLod(LinearSamplerClamp, shadow_atlas, uv, 0).r;
						shadow += receiver * 1.02 > occluder ? 1 : 0;
					}
					attn *= shadow / 16.0;
				}
			}

			if (fov < M_PI) {
				// TODO replace rot with dir
				float3 dir = rotateByQuat(light.rot, float3(0, 0, -1));
				float3 L = lpos / max(dist, 1e-5);
				float cosDir = dot(normalize(dir), L);
				float cosCone = cos(fov * 0.5);

				attn *= cosDir < cosCone ? 0 : (cosDir - cosCone) / (1 - cosCone);
			}

			res += direct_light * attn;
		}
	}
	return res;
}

float3 computeIndirectDiffuse(float3 irradiance, Surface surface) {
	float ndotv = abs(dot(surface.N , surface.V)) + 1e-5f;
	float3 F0 = lerp(0.04, surface.albedo, surface.metallic);
	float3 F = F_Schlick(ndotv, F0);
	float3 kd = lerp(1 - F, 0, surface.metallic);
	return surface.albedo * irradiance;
}

float3 transformByDualQuat(float2x4 dq, float3 pos) {
	return pos 
		+ 2 * cross(dq[0].xyz, cross(dq[0].xyz, pos) + dq[0].w * pos) 
		+ 2 * (dq[0].w * dq[1].xyz - dq[1].w * dq[0].xyz + cross(dq[0].xyz, dq[1].xyz));
}

float3 evalSH(EnvProbe probe, float3 N) {
	return probe.sh_coefs0.rgb
	+ probe.sh_coefs1.rgb * (N.y)
	+ probe.sh_coefs2.rgb * (N.z)
	+ probe.sh_coefs3.rgb * (N.x)
	+ probe.sh_coefs4.rgb * (N.y * N.x)
	+ probe.sh_coefs5.rgb * (N.y * N.z)
	+ probe.sh_coefs6.rgb * (3.0 * N.z * N.z - 1.0)
	+ probe.sh_coefs7.rgb * (N.z * N.x)
	+ probe.sh_coefs8.rgb * (N.x * N.x - N.y * N.y);
}

float3 envProbesLighting(Cluster cluster, Surface surface) {
	float remaining_w = 1;
	float3 probe_light = 0;
	int from = cluster.offset + cluster.lights_count;
	int to = from + cluster.env_probes_count;
	for (int i = from; i < to; ++i) {
		int probe_idx = b_cluster_map[i]; 
		float3 lpos = b_env_probes[probe_idx].pos.xyz - surface.pos_ws.xyz;
		float4 rot = b_env_probes[probe_idx].rot;
		float3 outer_range = b_env_probes[probe_idx].outer_range.xyz;
		float3 inner_range = b_env_probes[probe_idx].inner_range.xyz;
			
		lpos = rotateByQuat(rot, lpos);
		lpos = max(abs(lpos) - inner_range, 0);
		float3 range = max(outer_range - inner_range, 1e-5f);

		float3 rel = saturate(abs(lpos / range));
		float w = 1 - max(max(rel.x, rel.z), rel.y);
		if (w < 1e-5) continue;
			
		w = min(remaining_w, w);
		remaining_w -= w;

		float3 irradiance = evalSH(b_env_probes[probe_idx], surface.N);
		irradiance = max(0, irradiance);
		float3 indirect = computeIndirectDiffuse(irradiance, surface);
		probe_light += indirect * w;
		if (remaining_w <= 0) break;
	}
	return (remaining_w < 1 ? probe_light / (1 - remaining_w) / M_PI : 0) * surface.ao * Global_light_indirect_intensity;
}

float3 env_brdf_approx(float3 F0, float roughness, float NoV) {
	float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
	float4 c1 = float4(1, 0.0425, 1.0, -0.04);
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}

float3 reflProbesLighting(Cluster cluster, Surface surface, uint reflection_probes) {
	int from = cluster.offset + cluster.lights_count + cluster.env_probes_count;
	int to = from + cluster.refl_probes_count;
	float ndotv = abs(dot(surface.N , surface.V)) + 1e-5f;
	float3 F0 = lerp(0.04, surface.albedo, surface.metallic);		
	float3 brdf = env_brdf_approx(F0, surface.roughness, ndotv);
	float lod = surface.roughness * 5;
	float3 RV = reflect(-surface.V, surface.N);
	
	float remaining_w = 1;
	float3 res = 0;
	for (int i = from; i < to; ++i) {
		int probe_idx = b_cluster_map[i];
		ReflectionProbe probe = b_refl_probes[probe_idx];
		float4 rot = probe.rot;
		float3 lpos = probe.pos_layer.xyz - surface.pos_ws;
		uint layer = asuint(probe.pos_layer.w);
		float4 radiance_rgbm = sampleCubeArrayBindlessLod(LinearSamplerClamp, reflection_probes, float4(RV, layer), lod);
		float3 radiance = radiance_rgbm.rgb * radiance_rgbm.a * 4;

		lpos = rotateByQuat(rot, lpos);
		float3 half_extents = probe.half_extents.xyz;
		float3 rpos = saturate(abs(lpos) / half_extents * 2 - 1);
		float w = 1 - max(rpos.x, max(rpos.y, rpos.z));
		w = min(remaining_w, w);
		remaining_w -= w;
		res += radiance * w;
	}

	return (remaining_w > 0.999 ? 0 : res * brdf / (1 - remaining_w)) * surface.ao * Global_light_indirect_intensity;
}

void makeAscending(inout float3 a, inout float3 b) {
	if (a.y > b.y) {
		float3 tmp = a;
		a = b;
		b = tmp;
	}
}

float distanceInFog(float3 a, float3 b) {
	makeAscending(a, b); // TODO remove and make sure argument are correct

	[flatten] // to avoid compiler warning
	if (a.y > Global_fog_top) return 0;
	if (b.y > Global_fog_top) {
		float3 dir = b - a;
		float3 diry1 = dir / dir.y;
		b -= diry1 * (b.y - Global_fog_top);
	}
	return length(a - b);
}

float3 computeLighting(Cluster cluster, Surface surface, float3 light_direction, float3 light, uint shadowmap, uint shadow_atlas, uint reflection_probes, float2 frag_coord) {
	float shadow = min(surface.shadow, getShadow(shadowmap, surface.pos_ws, surface.N, frag_coord));

	float dist = max(0, (Global_fog_top - surface.pos_ws.y - Global_camera_world_pos.y) * Global_light_dir.y);
	float3 fog_transmittance = Global_fog_enabled > 0 ? exp(-dist * Global_fog_scattering.rgb * 10) : 1;

	float3 res = computeDirectLight(surface, light_direction, light * shadow * fog_transmittance);
	res += surface.emission * surface.albedo;
	res += envProbesLighting(cluster, surface);
	res += reflProbesLighting(cluster, surface, reflection_probes);
	res += pointLightsLighting(cluster, surface, shadow_atlas, frag_coord);
	return res;
}

float2 cameraReproject(float2 uv, float ndc_depth) {
	float4 v = transformPosition(float3(toScreenUV(uv) * 2 - 1, ndc_depth), Global_reprojection);
	float2 res = (v.xy / v.w) * 0.5 + 0.5;
	return toScreenUV(res);
}

float D_GGX(float ndoth, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float f = max(1e-5, (ndoth * ndoth) * (a2 - 1) + 1);
	return a2 / (f * f * M_PI);
}

bool ditherLOD(float lod, float2 frag_coord){
	// interleaved gradient noise by Jorge Jimenez
	float s = frac(52.9829189 * frac(0.06711056 * frag_coord.x + 0.00583715 * frag_coord.y));
	float ret = lod < 0.0 ? step(s, lod + 1.0) : step(lod, s);
	return ret < 1e-3;
}
