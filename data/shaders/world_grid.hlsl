//@surface
#include "shaders/common.hlsli"
#include "shaders/surface_base.hlsli"

Surface getSurface(VSOutput input) {
	float3 pos_gs = input.pos_ws.xyz + Global_camera_world_pos.xyz;
	float3 cell_pos = floor(pos_gs);
	float mask = dot(cell_pos, 1.0);
	mask = fmod(abs(mask), 2);
	
	Surface surface;
	surface.albedo = mask < 1 ? 1.0 : 0.75;
	surface.alpha = 1;
	surface.ao = 1;
	surface.roughness = 1;
	surface.metallic  = 0;
	surface.N = input.normal;
	surface.emission = 0;
	surface.translucency = 0;
	surface.shadow = 1;
	return surface;
}
