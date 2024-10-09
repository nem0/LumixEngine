//@surface

#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float u_filter_roughness;
	int u_face;
	int u_mip;
	TextureHandle u_texture;
};

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

VSOutput mainVS(uint vertex_id : SV_VertexID) {
	VSOutput output;
	float4 pos = fullscreenQuad(vertex_id, output.uv);
	pos.xy = pos.xy;
	pos.y = -pos.y;
	output.position = pos;
	return output;
}

static const uint SAMPLE_COUNT = 128u;

// https://github.com/google/filament/blob/master/shaders/src/light_indirect.fs
float prefilteredImportanceSampling(float ipdf) {
	const float numSamples = float(SAMPLE_COUNT);
	const float invNumSamples = 1.0 / float(SAMPLE_COUNT);
	float dim = 128;
	const float omegaP = (4.0 * M_PI) / (6.0 * dim * dim);
	const float invOmegaP = 1.0 / omegaP;
	const float K = 4.0;
	const float iblRoughnessOneLevel = 4;
	float omegaS = invNumSamples * ipdf;
	float mipLevel = clamp(log2(K * omegaS * invOmegaP) * 0.5, 0.0, iblRoughnessOneLevel);
	return mipLevel;
}

// https://github.com/JoeyDeVries/LearnOpenGL/blob/master/src/6.pbr/2.2.1.ibl_specular/2.2.1.prefilter.fs
float RadicalInverse_VdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N) {
	return float2(float(i)/float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
	float a = roughness*roughness;

	float phi = 2.0 * M_PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

	// from spherical coordinates to cartesian coordinates
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	// from tangent-space vector to world-space sample vector
	float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	float3 tangent   = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);

	float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}

float4 mainPS(float2 in_uv : TEXCOORD0) : SV_TARGET {
	float2 uv = in_uv * 2 - 1;
	uv.y *= -1; 
	float3 N = 0;

	switch (u_face) {
		case 0: N = float3(1, -uv.y, -uv.x); break;
		case 1: N = float3(-1, -uv.y, uv.x); break;
		case 2: N = float3(uv.x, 1, uv.y); break;
		case 3: N = float3(uv.x, -1, -uv.y); break;
		case 4: N = float3(uv.x, -uv.y, 1); break;
		case 5: N = float3(-uv.x, -uv.y, -1); break;
	}

	if (u_mip == 0) {
		return float4(sampleCubeBindless(LinearSampler, u_texture, N).rgb, 1);
	}

	N = normalize(N);
	float3 R = N;
	float3 V = R;

	float totalWeight = 0.0;
	float3 prefilteredColor = 0;
	for(uint i = 0u; i < SAMPLE_COUNT; ++i) {
		float2 Xi = Hammersley(i, SAMPLE_COUNT);
		float3 H  = ImportanceSampleGGX(Xi, N, u_filter_roughness);
		float3 L  = normalize(2.0 * dot(V, H) * H - V);

		float NdotL = dot(N, L);
		if(NdotL > 0.0) {
			float LdotH = dot(L, H);
			float NdotH = dot(N, H);
			float ipdf = (4.0 * LdotH) / (D_GGX(NdotH, u_filter_roughness) * NdotH);
			float mipLevel = prefilteredImportanceSampling(ipdf);

			float3 c = sampleCubeBindlessLod(LinearSampler, u_texture, L, mipLevel).rgb;
			prefilteredColor += c * NdotL;
			totalWeight      += NdotL;
		}
	}
	prefilteredColor = prefilteredColor / totalWeight;

	return float4(prefilteredColor, 1.0);
}
