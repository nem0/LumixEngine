#include "shaders/common.hlsli"

// arrow from https://www.shadertoy.com/view/4s23DG
static const int   ARROW_V_STYLE = 1;
static const int   ARROW_LINE_STYLE = 2;

// Choose your arrow head style
static const int   ARROW_STYLE = ARROW_LINE_STYLE;
static const float ARROW_TILE_SIZE = 32.0;

// How sharp should the arrow head be? Used
static const float ARROW_HEAD_ANGLE = 45.0 * M_PI / 180.0;

// Used for ARROW_LINE_STYLE
static const float ARROW_HEAD_LENGTH = ARROW_TILE_SIZE / 6.0;
static const float ARROW_SHAFT_THICKNESS = 3.0;

// Computes the center pixel of the tile containing pixel pos
float2 arrowTileCenterCoord(float2 pos) {
	return (floor(pos / ARROW_TILE_SIZE) + 0.5) * ARROW_TILE_SIZE;
}

float arrow(float2 p, float2 v) {
	// Make everything relative to the center, which may be fractional
	p -= arrowTileCenterCoord(p);
		
	float mag_v = length(v), mag_p = length(p);
	
	if (mag_v > 0.0) {
		// Non-zero velocity case
		float2 dir_p = p / mag_p, dir_v = v / mag_v;
		
		// We can't draw arrows larger than the tile radius, so clamp magnitude.
		// Enforce a minimum length to help see direction
		mag_v = clamp(mag_v, 5.0, ARROW_TILE_SIZE / 2.0);

		// Arrow tip location
		v = dir_v * mag_v;
		
		// Define a 2D implicit surface so that the arrow is antialiased.
		// In each line, the left expression defines a shape and the right controls
		// how quickly it fades in or out.

		float dist;		
		if (ARROW_STYLE == ARROW_LINE_STYLE) {
			// Signed distance from a line segment based on https://www.shadertoy.com/view/ls2GWG by 
			// Matthias Reitinger, @mreitinger
			
			// Line arrow style
			dist = 
				max(
					// Shaft
					ARROW_SHAFT_THICKNESS / 4.0 - 
						max(abs(dot(p, float2(dir_v.y, -dir_v.x))), // Width
							abs(dot(p, dir_v)) - mag_v + ARROW_HEAD_LENGTH / 2.0), // Length
						
					// Arrow head
					min(0.0, dot(v - p, dir_v) - cos(ARROW_HEAD_ANGLE / 2.0) * length(v - p)) * 2.0 + // Front sides
					min(0.0, dot(p, dir_v) + ARROW_HEAD_LENGTH - mag_v)); // Back
		} else {
			// V arrow style
			dist = min(0.0, mag_v - mag_p) * 2.0 + // length
				min(0.0, dot(normalize(v - p), dir_v) - cos(ARROW_HEAD_ANGLE / 2.0)) * 2.0 * length(v - p) + // head sides
				min(0.0, dot(p, dir_v) + 1.0) + // head back
				min(0.0, cos(ARROW_HEAD_ANGLE / 2.0) - dot(normalize(v * 0.33 - p), dir_v)) * mag_v * 0.8; // cutout
		}
		
		return clamp(1.0 + dist, 0.0, 1.0);
	} else {
		// Center of the pixel is always on the arrow
		return max(0.0, 1.2 - mag_p);
	}
}

cbuffer Constants : register(b4) {
	TextureHandle u_velocity_buffer;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 velocity = bindless_textures[u_velocity_buffer][thread_id.xy].xy;
	//if (length(velocity) < 1e-5) return;
	
	float arrow_mask = arrow(thread_id.xy, velocity * 5000);
	float4 out_color = bindless_rw_textures[u_output][thread_id.xy]; 
	if ((thread_id.x + thread_id.y) & 1) {
		out_color.rg = velocity * 100;
	}
	if (arrow_mask > 0) out_color = 0;
	bindless_rw_textures[u_output][thread_id.xy] = out_color;
}

