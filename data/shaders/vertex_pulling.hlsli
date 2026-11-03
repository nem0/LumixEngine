// Vertex pulling: vertices are read from a buffer by the vertex shader, there are no per-vertex attributes in the input assembler.
// Only for shaders compiled with VERTEX_PULLING.
//
// The layout of vertices is not data, it is generated into the program by Renderer::getSemanticDefines:
//   VERTEX_STRIDE
//   PULL_<SEMANTIC>_OFFSET, PULL_<SEMANTIC>_TYPE (gpu::AttributeType), PULL_<SEMANTIC>_COMPONENTS (0 if the mesh does not
//   have the attribute) and PULL_<SEMANTIC>_NORMALIZED
// so all offsets, formats, alignments and branches below are constants after compilation. The only thing that changes
// between draws is the buffer.
#ifdef VERTEX_PULLING

cbuffer VertexBuffer : register(b2) {
	uint u_vertex_buffer;		// bindless
	uint u_instance_buffer;		// bindless, instance data of shaders that pull them (DYNAMIC), see Mesh::VertexBufferBinding
	uint u_instance_offset;		// in bytes
	uint u_vertex_buffer_padding;
};

#ifdef INDIRECT_INSTANCE
	// set by the commands of indirect draws (SV_InstanceID does not include the start instance of a draw)
	cbuffer DrawId : register(b6) {
		uint u_draw_id;
	};
#endif

// Instance data is pulled too. Instances are in the buffer u_instance_buffer from the offset u_instance_offset,
// the instance of a vertex is `instance_id` (SV_InstanceID), plus the draw id of indirect draws.
uint pullInstanceAddress(uint instance_id, uint stride) {
	#ifdef INDIRECT_INSTANCE
		instance_id += u_draw_id;
	#endif
	return u_instance_offset + instance_id * stride;
}

// gpu::AttributeType
#define ATTR_U8 0
#define ATTR_FLOAT 1
#define ATTR_I16 2
#define ATTR_I8 3
#define ATTR_U16 4
#define ATTR_U32 5

// depth and shadow passes need only positions, unless alpha cutout reads the rest
#if defined DEPTH && !defined ALPHA_CUTOUT
	#define PULL_ONLY_POSITION
#endif

uint pullAttributeSize(uint type) {
	if (type == ATTR_FLOAT || type == ATTR_U32) return 4;
	if (type == ATTR_I16 || type == ATTR_U16) return 2;
	return 1;
}

// 32 bits from any byte address, used only if the attribute is not 4 byte aligned
uint pullLoadUnaligned(ByteAddressBuffer vb, uint address) {
	uint aligned = address & ~3u;
	uint shift = (address & 3u) * 8;
	uint lo = vb.Load(aligned);
	if (shift == 0) return lo;
	uint hi = vb.Load(aligned + 4);
	return (lo >> shift) | (hi << (32 - shift));
}

// component `c` of an attribute whose first (at most) 8 bytes are in `raw`
uint pullBits(uint2 raw, uint c, uint size) {
	uint bits = c * size * 8;
	uint word = bits >= 32 ? raw.y : raw.x;
	uint v = word >> (bits & 31);
	return size == 1 ? (v & 0xff) : (size == 2 ? (v & 0xffff) : v);
}

// a component as the input assembler converts it
float pullConvert(uint type, bool normalized, uint v) {
	if (type == ATTR_FLOAT) return asfloat(v);
	if (type == ATTR_U8) return normalized ? v * (1.0 / 255.0) : (float)v;
	if (type == ATTR_U16) return normalized ? v * (1.0 / 65535.0) : (float)v;
	if (type == ATTR_I8) { float s = (float)((int)(v << 24) >> 24); return normalized ? max(s * (1.0 / 127.0), -1.0) : s; }
	if (type == ATTR_I16) { float s = (float)((int)(v << 16) >> 16); return normalized ? max(s * (1.0 / 32767.0), -1.0) : s; }
	return (float)v;
}

// All arguments except `base` are constants, so the branches below are resolved when compiling.
// Missing components are 0, and w is 1, like in the input assembler.
float4 pullFloat4(ByteAddressBuffer vb, uint base, uint offset, uint type, uint components, bool normalized) {
	float4 r = float4(0, 0, 0, 1);
	if (components == 0) return r;

	const uint address = base + offset;
	const uint size = pullAttributeSize(type);
	const uint bytes = components * size;
	const bool aligned = (VERTEX_STRIDE & 3) == 0 && (offset & 3) == 0;

	if (type == ATTR_FLOAT && aligned) {
		// one wide load
		if (components == 1) r.x = asfloat(vb.Load(address));
		else if (components == 2) r.xy = asfloat(vb.Load2(address));
		else if (components == 3) r.xyz = asfloat(vb.Load3(address));
		else r = asfloat(vb.Load4(address));
		return r;
	}

	if (aligned && bytes <= 8) {
		// packed attribute: one load, all components are unpacked from it
		uint2 raw = bytes > 4 ? vb.Load2(address) : uint2(vb.Load(address), 0);
		if (components > 0) r.x = pullConvert(type, normalized, pullBits(raw, 0, size));
		if (components > 1) r.y = pullConvert(type, normalized, pullBits(raw, 1, size));
		if (components > 2) r.z = pullConvert(type, normalized, pullBits(raw, 2, size));
		if (components > 3) r.w = pullConvert(type, normalized, pullBits(raw, 3, size));
		return r;
	}

	// not aligned
	uint mask = size == 1 ? 0xff : (size == 2 ? 0xffff : 0xffFFffFF);
	if (components > 0) r.x = pullConvert(type, normalized, pullLoadUnaligned(vb, address) & mask);
	if (components > 1) r.y = pullConvert(type, normalized, pullLoadUnaligned(vb, address + size) & mask);
	if (components > 2) r.z = pullConvert(type, normalized, pullLoadUnaligned(vb, address + size * 2) & mask);
	if (components > 3) r.w = pullConvert(type, normalized, pullLoadUnaligned(vb, address + size * 3) & mask);
	return r;
}

// integer attributes (joint indices)
uint4 pullUint4(ByteAddressBuffer vb, uint base, uint offset, uint type, uint components) {
	uint4 r = uint4(0, 0, 0, 0);
	if (components == 0) return r;

	const uint address = base + offset;
	const uint size = pullAttributeSize(type);
	const uint bytes = components * size;
	const bool aligned = (VERTEX_STRIDE & 3) == 0 && (offset & 3) == 0;

	if (aligned && bytes <= 8) {
		uint2 raw = bytes > 4 ? vb.Load2(address) : uint2(vb.Load(address), 0);
		if (components > 0) r.x = pullBits(raw, 0, size);
		if (components > 1) r.y = pullBits(raw, 1, size);
		if (components > 2) r.z = pullBits(raw, 2, size);
		if (components > 3) r.w = pullBits(raw, 3, size);
		return r;
	}

	uint mask = size == 1 ? 0xff : (size == 2 ? 0xffff : 0xffFFffFF);
	if (components > 0) r.x = pullLoadUnaligned(vb, address) & mask;
	if (components > 1) r.y = pullLoadUnaligned(vb, address + size) & mask;
	if (components > 2) r.z = pullLoadUnaligned(vb, address + size * 2) & mask;
	if (components > 3) r.w = pullLoadUnaligned(vb, address + size * 3) & mask;
	return r;
}

// `vb` and `base` are variables of pullVertex
#define PULL_FLOAT4(SEMANTIC) pullFloat4(vb, base, PULL_##SEMANTIC##_OFFSET, PULL_##SEMANTIC##_TYPE, PULL_##SEMANTIC##_COMPONENTS, PULL_##SEMANTIC##_NORMALIZED != 0)
#define PULL_UINT4(SEMANTIC) pullUint4(vb, base, PULL_##SEMANTIC##_OFFSET, PULL_##SEMANTIC##_TYPE, PULL_##SEMANTIC##_COMPONENTS)

// what the input assembler would pass to the vertex shader
struct VSVertex {
	float3 position;
	float3 normal;
	float2 uv;
	float ao;
	float3 tangent;
	uint4 indices;
	float4 weights;
};

VSVertex pullVertex(uint vertex) {
	ByteAddressBuffer vb = bindless_buffers[u_vertex_buffer];
	const uint base = vertex * VERTEX_STRIDE;

	VSVertex v = (VSVertex)0;
	v.position = PULL_FLOAT4(POSITION).xyz;
	#ifndef PULL_ONLY_POSITION
		v.normal = PULL_FLOAT4(NORMAL).xyz;
		#ifdef UV0_ATTR
			v.uv = PULL_FLOAT4(TEXCOORD0).xy;
		#endif
		#ifdef AO_ATTR
			v.ao = PULL_FLOAT4(AO).x;
		#endif
		#ifdef TANGENT_ATTR
			v.tangent = PULL_FLOAT4(TANGENT).xyz;
		#endif
	#endif
	#ifdef INDICES_ATTR
		// skinning needs them for positions too
		v.indices = PULL_UINT4(JOINTS);
		v.weights = PULL_FLOAT4(WEIGHTS);
	#endif
	return v;
}

#endif

