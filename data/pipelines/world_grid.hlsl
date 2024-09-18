//@surface
//@include "pipelines/common.hlsli"
//@include "pipelines/surface_base.hlsli"

//@uniform "Material color", "color", {1, 1, 1, 1}
//@uniform "Roughness", "normalized_float", 1
//@uniform "Metallic", "normalized_float", 0
//@uniform "Emission", "float", 0
//@uniform "Translucency", "normalized_float", 0

Surface getSurface(VSOutput input) {
	float3 t = fmod(abs(input.wpos.xyz + Global_camera_world_pos.xyz + 0.5), 2.0f.xxx);
	float ff = dot(floor(t), 1.0f.xxx);
	ff = fmod(ff, 2);
	float4 c = float4(u_material_color.xyzw);
	
	Surface surface;
	surface.albedo = c.rgb * (ff < 1 ? 1.0 : 0.75);
	surface.alpha = c.a;
	surface.ao = 1;
	surface.roughness = u_roughness;
	surface.metallic  = u_metallic;
	surface.N = input.normal;
	surface.emission = u_emission;
	surface.translucency = u_translucency;
	surface.shadow = 1;
	return surface;
}