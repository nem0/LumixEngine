#include "shaders/common.hlsli"

struct Indirect {
	uint vertex_count;
	uint instance_count;
	uint first_index;
	uint base_vertex;
	uint base_instance;
};

struct InstanceData {
	float4 rot_lod;
	float4 pos_scale;
};

/*
u_culled_buffer format
struct OutData {
	uint4 b_batch_offset;
	uint4 b_lod_count;
	uint4 b_lod_offset;
	InstanceData b_output[];
};
*/

cbuffer UniformData : register(b4){
	float4 u_camera_offset;
	float4 u_lod_distances;
	int4 u_lod_indices;
	uint u_indirect_offset;
	float u_radius;
	float2 padding;
	float4 u_camera_planes[6];
	uint4 u_indices_count[32];
	uint u_culled_buffer;
	uint u_instance_data;
	uint u_indirect_buffer;
};

cbuffer UniformData2 : register(b5) {
	uint u_from_instance;
	uint u_instance_count;
	uint u_input_buffer;
	uint u_output_buffer0;
	uint u_output_buffer1;
};

groupshared uint group_count[5];

uint4 getBatchOffset() { return bindless_rw_buffers[u_culled_buffer].Load4(0); }
uint4 getLodCount() { return bindless_rw_buffers[u_culled_buffer].Load4(16); }
uint4 getLodOffset() { return bindless_rw_buffers[u_culled_buffer].Load4(32); }

void setBatchOffset(uint4 value) { bindless_rw_buffers[u_culled_buffer].Store4(0, value); }
void setLodCount(uint4 value) { bindless_rw_buffers[u_culled_buffer].Store4(16, value); }
void setLodOffset(uint4 value) { bindless_rw_buffers[u_culled_buffer].Store4(32, value); }

int addInterlockedLodOffset(uint coord, uint value) {
	uint orig;
	bindless_rw_buffers[u_culled_buffer].InterlockedAdd(32 + coord * 4, value, orig);
	return (int)orig;
}

int addInterlockedLodCount(uint coord, uint value) {
	uint orig;
	bindless_rw_buffers[u_culled_buffer].InterlockedAdd(16 + coord * 4, value, orig);
	return (int)orig;
}

void storePass3InstanceData(uint idx, InstanceData data) {
	bindless_rw_buffers[u_culled_buffer].Store4(48 + idx * 32, asuint(data.rot_lod));
	bindless_rw_buffers[u_culled_buffer].Store4(48 + idx * 32 + 16, asuint(data.pos_scale));
}

void storeIndirect(uint idx, Indirect indirect) {
	RWByteAddressBuffer b_indirect = bindless_rw_buffers[u_indirect_buffer];
	b_indirect.Store(20 * idx, indirect.vertex_count);
	b_indirect.Store(20 * idx + 4, indirect.instance_count);
	b_indirect.Store(20 * idx + 8, indirect.first_index);
	b_indirect.Store(20 * idx + 12, indirect.base_vertex);
	b_indirect.Store(20 * idx + 16, indirect.base_instance);
}

void storeInstanceLod(uint idx, float lod) {
	bindless_rw_buffers[u_instance_data].Store(32 * idx + 12, asuint(lod));
}

InstanceData getInstanceData(uint idx) {
	InstanceData result;
	result.rot_lod = asfloat(bindless_rw_buffers[u_instance_data].Load4(idx * 32));
	result.pos_scale = asfloat(bindless_rw_buffers[u_instance_data].Load4(idx * 32 + 16));
	return result;
}

uint getLOD(uint id) {
	float3 p = getInstanceData(id).pos_scale.xyz + u_camera_offset.xyz;
	float d = dot(p, p);
	if (d > u_lod_distances.w) return 4;
	else if (d > u_lod_distances.z) return 3;
	else if (d > u_lod_distances.y) return 2;
	else if (d > u_lod_distances.x) return 1;
	return 0;
}

bool cull(uint id) {
	InstanceData instance_data = getInstanceData(id);
	float scale = instance_data.pos_scale.w;
	float4 cullp = float4(instance_data.pos_scale.xyz + u_camera_offset.xyz, 1);
	float scaled_radius = u_radius * scale;
	for (int i = 0; i < 6; ++i) {
		if (dot(u_camera_planes[i], cullp) < -scaled_radius) {
			return false;
		}
	}
	return true;
}

[numthreads(256, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID) {
	uint id = thread_id.y * 256 + thread_id.x;
	#ifdef PASS0
		if (id == 0) {
			uint4 batch_offset = getBatchOffset();
			batch_offset.x += uint(dot(getLodCount(), 1));
			setBatchOffset(batch_offset);
			setLodOffset(batch_offset);
			setLodCount(0);
		}
	#elif defined PASS1
		bool master = group_thread_id.x == 0;
		if (master) {
			group_count[0] = 0;
			group_count[1] = 0;
			group_count[2] = 0;
			group_count[3] = 0;
		}
		GroupMemoryBarrierWithGroupSync();

		if (id < u_instance_count) {
			id += u_from_instance;
			#ifdef UPDATE_LODS
				float dst_lod = getLOD(id);
				float src_lod = getInstanceData(id).rot_lod.w;
				float d = dst_lod - src_lod;
				float td = Global_frame_time_delta * 2;
				float lod = abs(d) < td ? dst_lod : src_lod + td * sign(d);
				storeInstanceLod(id, lod);
			#else
				float lod = getInstanceData(id).rot_lod.w;
			#endif
			if (lod <= 3 && cull(id)) {
				float t = frac(lod);
				uint ilod = uint(lod);
				InterlockedAdd(group_count[ilod], 1);
				if (t > 0.01) {
					InterlockedAdd(group_count[ilod + 1], 1);
				}
			}
		}

		GroupMemoryBarrierWithGroupSync();

		if (master) {
			addInterlockedLodCount(0, group_count[0]);
			addInterlockedLodCount(1, group_count[1]);
			addInterlockedLodCount(2, group_count[2]);
			addInterlockedLodCount(3, group_count[3]);
			addInterlockedLodOffset(1, group_count[0]);
			addInterlockedLodOffset(2, group_count[1] + group_count[0]);
			addInterlockedLodOffset(3, group_count[2] + group_count[1] + group_count[0]);
		}
	#elif defined PASS2
		int iid = int(id);
		if (iid > u_lod_indices.w) return;

		Indirect indirect;
		if (iid <= u_lod_indices.x) {
			indirect.instance_count = getLodCount().x;
			indirect.base_instance = getLodOffset().x;
		}
		else if (iid <= u_lod_indices.y) {
			indirect.instance_count = getLodCount().y;
			indirect.base_instance = getLodOffset().y;
		}
		else if (iid <= u_lod_indices.z) {
			indirect.instance_count = getLodCount().z;
			indirect.base_instance = getLodOffset().z;
		}
		else {
			indirect.instance_count = getLodCount().w;
			indirect.base_instance = getLodOffset().w;
		}

		indirect.base_vertex = 0;
		indirect.first_index = 0;
		indirect.vertex_count = u_indices_count[id].x;
		storeIndirect(id + u_indirect_offset, indirect);
	#elif defined PASS3
		if (id >= u_instance_count) return;

		id += u_from_instance;
		InstanceData inp = getInstanceData(id);

		float lod = inp.rot_lod.w;

		uint idx;
		if (lod > 3 || !cull(id)) return;

		float t = frac(lod);
		uint ilod = uint(lod);

		if (ilod == 0) {
			idx = addInterlockedLodOffset(0, 1);
		}
		else if (ilod == 1) {
			idx = addInterlockedLodOffset(1, 1);
		}
		else if (ilod == 2) {
			idx = addInterlockedLodOffset(2, 1);
		}
		else if (ilod == 3) {
			idx = addInterlockedLodOffset(3, 1);
		}
		else return;

		InstanceData instance_data;
		instance_data.rot_lod.xyz = inp.rot_lod.xyz;
		instance_data.rot_lod.w = t;
		instance_data.pos_scale = inp.pos_scale + float4(u_camera_offset.xyz, 0);
		storePass3InstanceData(idx, instance_data);

		if (t > 0.01) {
			if (ilod == 0) {
				idx = addInterlockedLodOffset(1, 1);
			}
			else if (ilod == 1) {
				idx = addInterlockedLodOffset(2, 1);
			}
			else if (ilod == 2) {
				idx = addInterlockedLodOffset(3, 1);
			}
			
			instance_data.rot_lod.xyz = inp.rot_lod.xyz;
			instance_data.rot_lod.w = t - 1;
			instance_data.pos_scale = inp.pos_scale + float4(u_camera_offset.xyz, 0);

			storePass3InstanceData(idx, instance_data);
		}
	#elif defined UPDATE_LODS
		if (id < u_instance_count) {
			id += u_from_instance;
			storeInstanceLod(id, getLOD(id));
		}
	#endif
}
