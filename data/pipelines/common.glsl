#define M_PI 3.14159265359
#define ONE_BY_PI (1 / 3.14159265359)

layout (binding=15) uniform samplerCube u_radiancemap;

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
	return exp(38 / 5000.0 * (u_shadow_near_plane - frag_z * (u_shadow_near_plane - u_shadow_far_plane)));
}


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

float getShadow(sampler2D shadowmap, vec3 wpos)
{
	vec4 pos = vec4(wpos, 1);
	
	for (int i = 0; i < 4; ++i) {
		vec4 sc = u_shadowmap_matrices[i] * pos;
		sc = sc / sc.w;
		if (all(lessThan(sc.xyz, vec3(0.99))) && all(greaterThan(sc.xyz, vec3(0.01)))) {
			vec2 sm_uv = vec2(sc.x * 0.25 + i * 0.25, sc.y);
			float occluder = textureLod(shadowmap, sm_uv, 0).r;
			float receiver = shadowmapValue(sc.z);
			float m =  receiver / occluder;
			return clamp(1 - (1 - m) * 2048, 0.0, 1.0);
		}
	}

	return 1;
}


float D_GGX(float ndoth, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float f = max(1e-5, (ndoth * ndoth) * (a2 - 1) + 1);
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
	
	float ndotv = abs(dot(N, V)) + 1e-5f;
	vec3 H = normalize(V + L);
	float ldoth = saturate(dot(L, H));
	float ndoth = saturate(dot(N, H));
	float ndotl = saturate(dot(N, L));
	float hdotv = saturate(dot(H, V));
	
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

vec3 PBR_ComputeIndirectDiffuse(vec3 irradiance, vec3 albedo, float metallic, vec3 N, vec3 V) {
	float ndotv = clamp(dot(N , V), 1e-5f, 1);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);		
	vec3 F = F_Schlick(ndotv, F0);
	vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metallic);
	return kd * albedo * irradiance;
}

vec3 PBR_ComputeIndirectDiffuse(samplerCube irradiancemap, vec3 albedo, float metallic, vec3 N, vec3 V) {
	vec3 irradiance = texture(irradiancemap, N).rgb;
	return PBR_ComputeIndirectDiffuse(irradiance, albedo, metallic, N, V);
}

vec3 PBR_ComputeIndirectSpecular(samplerCube radiancemap, vec3 albedo, float metallic, float roughness, vec3 N, vec3 V) {
	float ndotv = clamp(dot(N , V ), 1e-5, 1.0);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);		
	float lod = roughness * 8;
	vec3 RV = reflect(-V, N);
	vec3 radiance = textureLod(radiancemap, RV, lod).rgb;    
	return radiance * env_brdf_approx(F0, roughness, ndotv);
}

vec3 PBR_ComputeIndirectLight(vec3 albedo, float roughness, float metallic, vec3 N, vec3 V)
{
	// TODO diffuse
	//vec3 diffuse = PBR_ComputeIndirectDiffuse(u_irradiancemap, albedo, metallic, N, V);
	vec3 specular = PBR_ComputeIndirectSpecular(u_radiancemap, albedo, metallic, roughness, N, V);
	
	return /*diffuse + */specular;
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

float rand(vec3 seed)
{
	float dot_product = dot(seed, vec3(12.9898,78.233,45.164));
	return fract(sin(dot_product) * 43758.5453);
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

vec3 vegetationAnim(vec3 obj_pos, vec3 vertex_pos) {
	obj_pos += u_camera_world_pos.xyz;
	vertex_pos.x += vertex_pos.y > 0.1 ? cos((obj_pos.x + obj_pos.y + obj_pos.z * 2) * 0.3 + u_time * 2) * vertex_pos.y * 0.03 : 0;
	return vertex_pos;
}