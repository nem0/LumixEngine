//@surface
#include "shaders/common.hlsli"
#include "shaders/surface_base.hlsli"

// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
Surface getSurface(VSOutput input, uint dummy) {
	float3 pos_gs = input.pos_ws.xyz + Global_camera_world_pos.xyz;
	float3 line_width = 0.04;
	line_width *= sqrt(saturate(1.0 - input.normal * input.normal));

	float3 uvw_DDX = ddx(pos_gs);
	float3 uvw_DDY = ddy(pos_gs);
	float3 uvw_deriv = float3(
		length(float2(uvw_DDX.x,uvw_DDY.x)),
		length(float2(uvw_DDX.y,uvw_DDY.y)),
		length(float2(uvw_DDX.z,uvw_DDY.z))
	);
	uvw_deriv = max(uvw_deriv, 0.00001);

	float3 draw_width = clamp(line_width, uvw_deriv, 0.5);
	float3 line_aa = uvw_deriv * 1.5;
	float3 grid_uv = abs(frac(pos_gs) * 2.0 - 1.0);
	grid_uv = 1.0 - grid_uv;
	float3 grid3 = smoothstep(draw_width + line_aa, draw_width - line_aa, grid_uv);
	grid3 *= saturate(line_width / draw_width);
	grid3 = lerp(grid3, line_width, saturate(uvw_deriv * 2.0 - 1.0));
	grid3 = grid3;

	float3 blend_factor = input.normal / dot(1, input.normal);
	float3 blend_width = max(fwidth(blend_factor), 0.0001);

	float3 blend_edge = float3(
		max(blend_factor.y, blend_factor.z),
		max(blend_factor.x, blend_factor.z),
		max(blend_factor.x, blend_factor.y)
		);

	grid3 *= saturate((blend_edge - blend_factor) / blend_width + 1.0);

	float grid = 1 - lerp(lerp(grid3.x, 1.0, grid3.y), 1.0, grid3.z);

	Surface surface;
	surface.albedo = grid;
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
