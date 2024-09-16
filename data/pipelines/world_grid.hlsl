//@surface
//@include "pipelines/common.hlsli"
//@include "pipelines/surface_base.hlsli"

//@uniform "Material color", "color", {1, 1, 1, 1}
//@uniform "Roughness", "normalized_float", 1
//@uniform "Metallic", "normalized_float", 0
//@uniform "Emission", "float", 0
//@uniform "Translucency", "normalized_float", 0

Surface getSurface(VSOutput input) {
	Surface data;
	float3 t = fmod(abs(input.wpos.xyz + Global_camera_world_pos.xyz + 0.5), float3(2.0f.xxx));
	float ff = dot(floor(t), float3(1.0f.xxx));
	ff = fmod(ff, 2);
	float4 c = float4(u_material_color.xyzw);
	data.albedo = c.rgb * (ff < 1 ? 1.0f.xxx : 0.75f.xxx);
	data.alpha = c.a;
	data.ao = 1;
	data.roughness = u_roughness;
	data.metallic  = u_metallic;
	data.N = input.normal;
	data.emission = u_emission;
	data.translucency = u_translucency;
	data.shadow = 1;
	return data;
}