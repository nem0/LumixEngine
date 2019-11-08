#define M_PI 3.14159265359
#define ONE_BY_PI (1 / 3.14159265359)


const vec2 POISSON_DISK_16[16] = vec2[](
	vec2(0.3568125,-0.5825516),
	vec2(-0.2828444,-0.1149732),
	vec2(-0.2575171,-0.579991),
	vec2(0.3328768,-0.0916517),
	vec2(-0.0177952,-0.9652126),
	vec2(0.7636694,-0.3370355),
	vec2(0.9381924,0.05975571),
	vec2(0.6547356,0.373677),
	vec2(-0.1999273,0.4483816),
	vec2(0.167026,0.2838214),
	vec2(0.2164582,0.6978411),
	vec2(-0.7202712,-0.07400024),
	vec2(-0.6624036,0.559697),
	vec2(-0.1909649,0.8721116),
	vec2(-0.6493049,-0.4945979),
	vec2(0.6104985,0.7838438)
);

const vec2 POISSON_DISK_64[64] = {
	vec2( -0.04117257f, -0.1597612f ),
	vec2( 0.06731031f, -0.4353096f ),
	vec2( -0.206701f, -0.4089882f ),
	vec2( 0.1857469f, -0.2327659f ),
	vec2( -0.2757695f, -0.159873f ),
	vec2( -0.2301117f, 0.1232693f ),
	vec2( 0.05028719f, 0.1034883f ),
	vec2( 0.236303f, 0.03379251f ),
	vec2( 0.1467563f, 0.364028f ),
	vec2( 0.516759f, 0.2052845f ),
	vec2( 0.2962668f, 0.2430771f ),
	vec2( 0.3650614f, -0.1689287f ),
	vec2( 0.5764466f, -0.07092822f ),
	vec2( -0.5563748f, -0.4662297f ),
	vec2( -0.3765517f, -0.5552908f ),
	vec2( -0.4642121f, -0.157941f ),
	vec2( -0.2322291f, -0.7013807f ),
	vec2( -0.05415121f, -0.6379291f ),
	vec2( -0.7140947f, -0.6341782f ),
	vec2( -0.4819134f, -0.7250231f ),
	vec2( -0.7627537f, -0.3445934f ),
	vec2( -0.7032605f, -0.13733f ),
	vec2( 0.8593938f, 0.3171682f ),
	vec2( 0.5223953f, 0.5575764f ),
	vec2( 0.7710021f, 0.1543127f ),
	vec2( 0.6919019f, 0.4536686f ),
	vec2( 0.3192437f, 0.4512939f ),
	vec2( 0.1861187f, 0.595188f ),
	vec2( 0.6516209f, -0.3997115f ),
	vec2( 0.8065675f, -0.1330092f ),
	vec2( 0.3163648f, 0.7357415f ),
	vec2( 0.5485036f, 0.8288581f ),
	vec2( -0.2023022f, -0.9551743f ),
	vec2( 0.165668f, -0.6428169f ),
	vec2( 0.2866438f, -0.5012833f ),
	vec2( -0.5582264f, 0.2904861f ),
	vec2( -0.2522391f, 0.401359f ),
	vec2( -0.428396f, 0.1072979f ),
	vec2( -0.06261792f, 0.3012581f ),
	vec2( 0.08908027f, -0.8632499f ),
	vec2( 0.9636437f, 0.05915006f ),
	vec2( 0.8639213f, -0.309005f ),
	vec2( -0.03422072f, 0.6843638f ),
	vec2( -0.3734946f, -0.8823979f ),
	vec2( -0.3939881f, 0.6955767f ),
	vec2( -0.4499089f, 0.4563405f ),
	vec2( 0.07500362f, 0.9114207f ),
	vec2( -0.9658601f, -0.1423837f ),
	vec2( -0.7199838f, 0.4981934f ),
	vec2( -0.8982374f, 0.2422346f ),
	vec2( -0.8048639f, 0.01885651f ),
	vec2( -0.8975322f, 0.4377489f ),
	vec2( -0.7135055f, 0.1895568f ),
	vec2( 0.4507209f, -0.3764598f ),
	vec2( -0.395958f, -0.3309633f ),
	vec2( -0.6084799f, 0.02532744f ),
	vec2( -0.2037191f, 0.5817568f ),
	vec2( 0.4493394f, -0.6441184f ),
	vec2( 0.3147424f, -0.7852007f ),
	vec2( -0.5738106f, 0.6372389f ),
	vec2( 0.5161195f, -0.8321754f ),
	vec2( 0.6553722f, -0.6201068f ),
	vec2( -0.2554315f, 0.8326268f ),
	vec2( -0.5080366f, 0.8539945f )
};

float rand(vec3 seed)
{
	float dot_product = dot(seed, vec3(12.9898,78.233,45.164));
	return fract(sin(dot_product) * 43758.5453);
}

float rand(vec4 co){
	float dot_product = dot(co, vec4(12.9898,78.233,45.164,94.673));
	return fract(sin(dot_product) * 43758.5453);
}

struct PixelData {
	vec4 albedo;
	float roughness;
	float metallic;
	float emission;
	vec3 normal;
	vec3 wpos;
} data;


float saturate(float a) { return clamp(a, 0, 1); }
vec2 saturate(vec2 a) { return clamp(a, vec2(0), vec2(1)); }
vec3 saturate(vec3 a) { return clamp(a, vec3(0), vec3(1)); }
vec4 saturate(vec4 a) { return clamp(a, vec4(0), vec4(1)); }

vec4 fullscreenQuad(int vertexID, out vec2 uv) {
	uv = vec2((vertexID & 1), (vertexID & 2) * 0.5);
	#ifdef _ORIGIN_BOTTOM_LEFT
		return vec4((vertexID & 1) * 2 - 1, (vertexID & 2) - 1, 0, 1);
	#else
		return vec4((vertexID & 1) * 2 - 1, -(vertexID & 2) + 1, 0, 1);
	#endif
}

float packEmission(float emission)
{
	return log2(1 + emission / 64);
}


float unpackEmission(float emission)
{
	return (exp2(emission) - 1) * 64;
}

float shadowmapValue(float frag_z)
{
	return u_shadow_near_plane - frag_z * (u_shadow_near_plane - u_shadow_far_plane);
}

#define PCSS_LIGHT_SIZE (16 / 1024.0)
#define PCSS_BLOCKER_SEARCH_NUM_SAMPLES 16
#define PCF_NUM_SAMPLES 16

#ifdef LUMIX_FRAGMENT_SHADER

vec2 findBlocker(sampler2D shadowmap, vec3 uv, float cascade_scale) {
	vec2 width = PCSS_LIGHT_SIZE * cascade_scale * vec2(1, 4);
	float sum = 0;
	float num = 0;
	
	for (int i = 0; i < PCSS_BLOCKER_SEARCH_NUM_SAMPLES; i++) {
		vec2 coord = uv.xy + POISSON_DISK_16[i] * width;
		float smap = textureLod(shadowmap, coord, 0).r - 0.05;
		if (smap > uv.z) {
			sum += smap;
			num++;
		}
	}
	return vec2(sum / num, num);
}


float pcf(sampler2D shadowmap, vec2 uv, float z_receiver, float filter_radius) {
	float sum = 0;
	vec2 filter_r = vec2(1, 4) * filter_radius;
	float theta = rand(vec4(uv, gl_FragCoord.xy));
	mat2 rotation = mat2(vec2(cos(theta), sin(theta)), vec2(-sin(theta), cos(theta)));
	for (int i = 0; i < PCF_NUM_SAMPLES; ++i) {
		vec2 offset = rotation * POISSON_DISK_16[i] * filter_r;
		vec2 texOffset = uv + offset;
		bvec2 outside = greaterThan(texOffset, vec2(1.0,1.0));
		bvec2 inside = lessThan(texOffset, vec2(0,0));
		float shadow = texture(shadowmap, texOffset).r - 0.05 < z_receiver ? 1 : 0;
		sum += ((any(outside)||any(inside)) ? 1.0f : shadow);
	}
	return sum / PCF_NUM_SAMPLES;
}

float pcss(sampler2D shadowmap, vec3 uv, float cascade_scale) {
	vec2 blockers = findBlocker(shadowmap, uv, cascade_scale);
	if (blockers.y < 1) return 1.0;

	float penumbraRatio = (uv.z - blockers.x) / blockers.x;
	float filterRadius = penumbraRatio * PCSS_LIGHT_SIZE * 160 * cascade_scale;
	
	return pcf(shadowmap, uv.xy, uv.z, filterRadius);
}

float getShadow(sampler2D shadowmap, vec3 wpos)
{
	vec4 pos = vec4(wpos, 1);
	
	for (int i = 0; i < 4; ++i) {
		vec4 sc = u_shadowmap_matrices[i] * pos;
		sc = sc / sc.w;
		if (all(lessThan(sc.xyz, vec3(0.99))) && all(greaterThan(sc.xyz, vec3(0.01)))) {
			vec2 sm_uv = vec2(sc.x * 0.25 + i * 0.25, sc.y);
			float receiver = shadowmapValue(sc.z);
			#if 1
				return pcss(shadowmap, vec3(sm_uv, receiver), u_shadow_cascades.x / u_shadow_cascades[i]);
			#else
				float occluder = textureLod(shadowmap, sm_uv, 0).r;
				float m =  receiver / occluder;
				return clamp(1 - (1 - m) * 64, 0.0, 1.0);
			#endif
		}
	}
	return 1;
}

#endif

vec3 getViewPosition(sampler2D depth_buffer, mat4 inv_view_proj, vec2 tex_coord)
{
	float z = texture(depth_buffer, tex_coord).r;
	#ifdef _ORIGIN_BOTTOM_LEFT
		vec4 pos_proj = vec4(vec2(tex_coord.x, tex_coord.y) * 2 - 1, z, 1.0);
	#else 
		vec4 pos_proj = vec4(vec2(tex_coord.x, 1-tex_coord.y) * 2 - 1, z, 1.0);
	#endif
	vec4 view_pos = inv_view_proj * pos_proj;
	return view_pos.xyz / view_pos.w;
}

vec3 getTranslucency(vec3 albedo, float translucency, vec3 V, vec3 L, vec3 N, float shadow)
{
	float w = pow(max(0, dot(-V, L)), 64) * shadow;
	w += abs(dot(V, N)) * 0.1;
	w *= max(0.5, dot(-L, N));
	w *= max(0.5, dot(N, V));
	return vec3(albedo * translucency * w);
}

float D_GGX(float ndoth, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float f = (ndoth * ndoth) * (a2 - 1) + 1;
	return a2 / (f * f * M_PI);
}
		

float G_SmithSchlickGGX(float ndotl, float ndotv, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	float l = ndotl / (ndotl * (1.0 - k) + k);
	float v = ndotv / (ndotv * (1.0 - k) + k);
	return l * v;
}


vec3 F_Schlick(float cos_theta, vec3 F0)
{
	return mix(F0, vec3(1), pow(1.0 - cos_theta, 5.0)); 
}


vec3 PBR_ComputeDirectLight(vec3 albedo
	, vec3 N
	, vec3 L
	, vec3 V
	, vec3 light_color
	, float roughness
	, float metallic)
{
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);		
	
	float ndotv = abs( dot (N , V )) + 1e-5f;
	vec3 H = normalize (V + L);
	float ldoth = saturate ( dot (L , H ));
	float ndoth = saturate ( dot (N , H ));
	float ndotl = saturate ( dot (N , L ));
	float hdotv = saturate ( dot (H , V ));
	
	float D = D_GGX(ndoth, roughness);
	float G = G_SmithSchlickGGX(ndotl, ndotv, roughness);
	vec3 F = F_Schlick(hdotv, F0);
	vec3 specular = (D * G * F) / max(4 * ndotv * ndotl, 0.001);
	
	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic;	  
	return (kD * albedo / M_PI + specular) * light_color * ndotl;
}	


vec3 env_brdf_approx (vec3 F0, float roughness, float NoV)
{
	vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022 );
	vec4 c1 = vec4(1, 0.0425, 1.0, -0.04 );
	vec4 r = roughness * c0 + c1;
	float a004 = min( r.x * r.x, exp2( -9.28 * NoV ) ) * r.x + r.y;
	vec2 AB = vec2( -1.04, 1.04 ) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}


vec3 PBR_ComputeIndirectLight(vec3 albedo, float roughness, float metallic, vec3 N, vec3 V)
{
	float ndotv = clamp(dot(N , V ), 1e-5f, 1);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);		
	vec3 irradiance = texture(u_irradiancemap, N).rgb;
	vec3 F = F_Schlick(ndotv, F0);
	vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metallic);
	vec3 diffuse = kd * albedo * irradiance;

	float lod = roughness * 8;
	vec3 RV = reflect(-V, N);
	vec3 radiance = textureLod(u_radiancemap, RV, lod).rgb;    
	vec3 specular = radiance * env_brdf_approx(F0, roughness, ndotv);
	
	return diffuse + specular;
}


vec3 rotateByQuat(vec4 rot, vec3 pos)
{
	vec3 uv = cross(rot.xyz, pos);
	vec3 uuv = cross(rot.xyz, uv);
	uv *= (2.0 * rot.w);
	uuv *= 2.0;

	return pos + uv + uuv;
}
	

vec3 pbr(vec3 albedo
	, float roughness
	, float metallic
	, float emission
	, vec3 N
	, vec3 V
	, vec3 L
	, float shadow
	, vec3 light_color
	, float indirect_intensity)
{
	vec3 indirect = PBR_ComputeIndirectLight(albedo, roughness, metallic, N, V);

	vec3 direct = PBR_ComputeDirectLight(albedo
		, N
		, L
		, V
		, light_color
		, roughness
		, metallic);

	return 
		+ direct * shadow
		+ indirect * indirect_intensity
		+ emission * albedo
	;
}

float getFogFactor(float cam_height
	, float frag_height
	, vec3 to_fragment
	, float fog_density
	, float fog_bottom
	, float fog_height) 
{ 
	float fog_top = fog_bottom + fog_height;
	frag_height = min(frag_height, fog_top);
	float len = length(to_fragment);
	vec3 view_dir = to_fragment / len;
	float y_dir = abs(view_dir.y);
	
	cam_height = min(cam_height, fog_top);
	float avg_y = (frag_height + cam_height) * 0.5;
	float avg_density = fog_density * clamp(1.0 - (avg_y - fog_bottom) / fog_height, 0, 1);

	float dist = abs(cam_height - frag_height);
	if (y_dir <= 0) {
		dist = len;
	}
	else {
		dist = dist / y_dir; 
	}
	float res = exp(-pow(avg_density * dist, 2));
	return 1 - clamp(res, 0.0, 1.0);
}

vec3 vegetationAnim(vec3 obj_pos, vec3 vertex_pos) {
	obj_pos += u_camera_world_pos.xyz;
	vertex_pos.x += vertex_pos.y > 0.1 ? cos((obj_pos.x + obj_pos.y + obj_pos.z * 2) * 0.3 + u_time * 2) * vertex_pos.y * 0.03 : 0;
	return vertex_pos;
}