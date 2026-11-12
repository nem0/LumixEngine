// Vertex pulling: vertices are read from a buffer by the vertex shader, there are no per-vertex attributes in the input assembler.
//
// The layout of vertices is not data, it is generated into the program by Renderer::getSemanticDefines:
//   VERTEX_STRIDE
//   PULL_<SEMANTIC>_OFFSET, PULL_<SEMANTIC>_TYPE (gpu::AttributeType), PULL_<SEMANTIC>_COMPONENTS (0 if the mesh does not
//   have the attribute) and PULL_<SEMANTIC>_NORMALIZED
// so all offsets, formats, alignments and branches below are constants after compilation. The only thing that changes
// between draws is the buffer.

cbuffer VertexBuffer : register(b2) {
	uint u_vertex_buffer;		// bindless
	uint u_instance_buffer;		// bindless, instance data of shaders that pull them (MESH_INSTANCE), see Mesh::VertexBufferBinding
	uint u_instance_offset;		// in bytes
	uint u_bones_buffer;		// bindless, dual quaternions of bones (SKINNED), see Mesh::VertexBufferBinding
	#ifdef CLUSTER_MESH
		uint u_cluster_meshes;			// bindless, the mesh table of the GPU scene (GPUSceneMesh, 64 bytes)
		uint u_padding1;
		uint u_padding2;
		uint u_padding3;
	#endif
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

#ifdef POOLED_VERTICES
	// The vertices of many meshes are in one buffer and the vertex id of a draw does not have the base vertex of its command (SV_VertexID of D3D), so the
	// instances (96 bytes, gpu_scene_variants.hlsl) have it, in the word after the previous position
	uint pullBaseVertex(uint instance_id) {
		return bindless_buffers[u_instance_buffer].Load(pullInstanceAddress(instance_id, 96) + 76);
	}
#endif

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

#ifdef CLUSTER_MESH
// Vertices of clusters, see MeshClusterVertexGPU in mesh_clusters.h. The instance data of a draw is written by gpu_scene_variants.hlsl (INDIRECT_INSTANCE,
// MESH_INSTANCE layout), an instance is a selected cluster.
// Positions are integers of a lattice of the mesh, relative to the minimum of the cluster, bit packed. Attributes have a fixed stride:
// the tangent frame as a quaternion (smallest three), the UV as two u16 in the range of the cluster, and the other attributes as they are.

// `count` (at most 32) bits from the bit `bit` of the data of a mesh, which start at the byte `base` of the buffer (all meshes are in one buffer)
uint pullClusterBits(ByteAddressBuffer data, uint base, uint bit, uint count) {
	const uint2 words = data.Load2(base + (bit >> 5) * 4);
	const uint shift = bit & 31;
	const uint v = shift == 0 ? words.x : ((words.x >> shift) | (words.y << (32 - shift)));
	const uint r = count >= 32 ? v : v & ((1u << count) - 1);
	return count == 0 ? 0 : r;
}

// the frame is a rotation of (1, 0, 0) to the tangent and (0, 0, 1) to the normal, see packClusterFrame
void pullClusterFrame(uint packed, out float3 normal, out float3 tangent) {
	const uint largest = packed >> 30;
	const float3 abc = float3((int)(packed << 22) >> 22, (int)(packed << 12) >> 22, (int)(packed << 2) >> 22) * (0.70710678 / 511.0);
	const float d = sqrt(max(0, 1 - dot(abc, abc)));
	float4 q;
	if (largest == 0) q = float4(d, abc);
	else if (largest == 1) q = float4(abc.x, d, abc.y, abc.z);
	else if (largest == 2) q = float4(abc.x, abc.y, d, abc.z);
	else q = float4(abc, d);
	normal = float3(2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y));
	tangent = float3(1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.w * q.z), 2 * (q.x * q.z - q.w * q.y));
}

// `corner` is the number of the index in the draw (SV_VertexID of a draw with a sequential index buffer). A draw has the clusters of many meshes (the variants of
// gpu_scene_variants.hlsl) and all of its instances draw the same range of indices, so the vertex of the corner is found here: the mesh of the instance is in its
// prev_pos.w (an index of the mesh table of the GPU scene, u_cluster_meshes), which has the buffers of its vertices and indices, the cluster is in its
// scale.w. The draw has as many indices as the largest mesh of its variant, the corners after the ones of the mesh are degenerate triangles.
VSVertex pullVertex(uint corner, uint instance_id) {
	const uint instance_address = pullInstanceAddress(instance_id, 96);
	const uint cluster = bindless_buffers[u_instance_buffer].Load(instance_address + 44);
	const uint mesh = bindless_buffers[u_instance_buffer].Load(instance_address + 76);
	ByteAddressBuffer mesh_table = bindless_buffers[u_cluster_meshes];
	const uint index_stride = mesh_table.Load(mesh * 64 + 28);
	const uint groups_at = mesh_table.Load(mesh * 64 + 48);	// byte offset of the table of groups in u_vertex_buffer
	// the vertices, indices and everything else of all meshes are in one buffer, a mesh has its ranges of it
	ByteAddressBuffer data = bindless_buffers[u_vertex_buffer];
	// the data of a cluster is where its group is: the group of each cluster is in the table that the mesh has (the first field of the mesh table)
	const uint group_id = data.Load(mesh_table.Load(mesh * 64) + cluster * 4);
	const uint4 group_address = data.Load4(groups_at + group_id * 32 + 4);	// header address, stream address, index address, first cluster
	const uint base = group_address.y;
	const uint index_address = group_address.z + (cluster - group_address.w) * index_stride + corner;
	const uint index_word = data.Load(index_address & ~3u);
	const uint vertex = corner >= index_stride ? 0 : ((index_word >> ((index_address & 3) * 8)) & 0xff);
	const uint header = group_address.x + (cluster - group_address.w) * 48;
	const uint4 h0 = data.Load4(header);			// start of positions, bits
	const uint4 h1 = data.Load4(header + 16);		// bits of UV, vertex count, bit offset of the first vertex (in the stream of its group), step
	const float4 h2 = asfloat(data.Load4(header + 32)); // UV range

	const uint bits_x = h0.w & 0xff, bits_y = (h0.w >> 8) & 0xff, bits_z = (h0.w >> 16) & 0xff;
	// vertices are interleaved: the position, then the attributes, none of them aligned. The bits of attributes of a vertex are in the top byte
	const uint position_bits = bits_x + bits_y + bits_z;
	const uint bit = h1.z + vertex * (position_bits + (h0.w >> 24));
	int3 lattice = asint(h0.xyz);
	lattice.x += (int)pullClusterBits(data, base, bit, bits_x);
	lattice.y += (int)pullClusterBits(data, base, bit + bits_x, bits_y);
	lattice.z += (int)pullClusterBits(data, base, bit + bits_x + bits_y, bits_z);

	VSVertex v = (VSVertex)0;
	v.position = float3(lattice) * asfloat(h1.w);

	#ifndef PULL_ONLY_POSITION
		// order: frame (32 bits), UV (bits of u and v are in h1.x), AO (8 bits), color
		uint attribute = bit + position_bits;
		pullClusterFrame(pullClusterBits(data, base, attribute, 32), v.normal, v.tangent);
		attribute += 32;
		#ifdef UV0_ATTR
			const uint bits_u = h1.x & 0xff, bits_v = (h1.x >> 8) & 0xff;
			const float2 q = float2(pullClusterBits(data, base, attribute, bits_u), pullClusterBits(data, base, attribute + bits_u, bits_v));
			v.uv = h2.xy + q / max(float2((1u << bits_u) - 1, (1u << bits_v) - 1), 1) * h2.zw;
			attribute += bits_u + bits_v;
		#endif
		#ifdef AO_ATTR
			v.ao = pullClusterBits(data, base, attribute, 8) * (1.0 / 255.0);
		#endif
	#endif
	return v;
}
#else
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


