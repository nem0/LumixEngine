#include "engine/shaders/common.hlsli"

// Selects clusters of meshes (see src/renderer/mesh_clusters.h) for the instances of models and writes one indexed draw per selected cluster.
// All the meshes of a view are done by the same dispatch, which reads what to do from tables: u_descs has a MeshDesc (128 bytes) for each mesh
// with the instances it is drawn for, and u_items has one 16 byte item (desc, instance, first cluster) for each group.
//
// A cluster is selected if its error is small enough on the screen and the error of its parent is not. Meshes with few clusters are done by the
// variant FLAT: a group tests 64 consecutive clusters of an instance, one thread each, no synchronizations.
// The clusters of the other meshes are in trees of nodes (MeshNodeGPU), a node has the smallest error and the largest parent error of its
// clusters. One thread group takes one instance and walks the trees from their roots, level by level: threads take nodes of the current level,
// skip a node (and everything under it) if it is outside of the frustum or no cluster of it can be selected at its distance, and add its
// children to the next level. The nodes of a level are in a list in `u_scratch`, there are two lists per group, which are swapped, and there are
// never more nodes in a list than nodes in the mesh.
//
// Input instances are in one of two layouts:
//   input_dynamic == 0: InstanceData of instanced models (32 bytes): float4(rotation xyz, lod), float4(position, uniform scale)
//   input_dynamic == 1: 96 bytes, the layout of the DYNAMIC variant of surface shaders:
//                         float4 rotation, float4(position, lod), float4(scale, -), the same for the previous frame
//
// Output:
//   u_out_instances: 96 bytes for each selected cluster, the layout of the DYNAMIC variant of surface shaders,
//                    the vertex shader pulls it by the draw id. The lod of the position is the index of the cluster.
//   u_out_indirect: [counters] ... [at u_command_offset: draw commands, 24 bytes each: draw id (root constant), indexed draw arguments]
//                   counters are three blocks of one u32 for each region: selected clusters, clusters culled by their normal cones, nodes and clusters visited.
// The draw id of each command is its index in u_out_instances, vertex shaders pull the instance by it.

cbuffer UniformData : register(b4) {
	float4 u_planes[6];
	float4 u_cull_offset;	// xyz: the camera of the culling - the camera of the drawing: positions in the tests of the culling are the frozen camera's
	float u_lod_k;			// cluster is accurate enough if error <= u_lod_k * distance
	uint u_is_ortho;
	uint u_out_instances;	// rw bindless
	uint u_out_indirect;	// rw bindless
	uint u_command_offset;
	uint u_descs;			// bindless, MeshDesc[], from u_descs_offset
	uint u_descs_offset;
	uint u_items;			// bindless, 16 bytes items, from u_items_offset
	uint u_items_offset;
	uint u_item_count;
	uint u_scratch;			// rw bindless, lists of nodes
	uint u_tree_depth;		// levels of the deepest tree of the meshes of the dispatch
	uint u_clear_first;		// the pass that clears the counters: the first region
	uint u_clear_count;		// and the number of regions
	float u_attribute_weight;	// the error of a cluster is its error of positions + this * its error of attributes
	uint u_padding1;
};

// threads of a group, CLUSTER_THREADS of pipeline.cpp
#define GROUP_SIZE 64

struct MeshDesc {
	float4 camera_offset;		// instance origin - camera position
	uint clusters;				// bindless, MeshClusterGPU[]
	uint nodes;					// bindless, MeshNodeGPU[node_count], then the indices of root_count roots
	uint node_count;
	uint root_count;
	uint roots_offset;			// byte offset of the roots in nodes
	uint tree_depth;
	uint scratch_offset;		// the first u32 of the lists of the instances of the mesh in u_scratch
	uint scratch_stride;		// u32 in the lists of an instance, two lists of node_count
	uint instances;				// bindless, input instances
	uint instances_offset;		// byte offset of the first input instance
	uint input_dynamic;
	uint instance_count;
	uint material_indices;		// bindless, material index of each mesh of each instance: [instance * mesh_count + mesh], 0 if not used
	uint material_indices_offset;
	uint material_index;		// of the mesh, used if there is no material_indices
	uint mesh_count;
	uint mesh_index;
	uint region_index;			// index of the counters
	uint region_base;			// first slot of the mesh in the output buffers
	uint region_cap;			// number of slots
	uint cone_culling;			// cull back facing clusters, set only for perspective views of materials that cull back faces
	uint cluster_count;
};

MeshDesc loadDesc(uint index) {
	ByteAddressBuffer buffer = bindless_buffers[u_descs];
	const uint offset = u_descs_offset + index * 128;
	const float4 a = asfloat(buffer.Load4(offset));
	const uint4 b = buffer.Load4(offset + 16);
	const uint4 c = buffer.Load4(offset + 32);
	const uint4 d = buffer.Load4(offset + 48);
	const uint4 e = buffer.Load4(offset + 64);
	const uint4 f = buffer.Load4(offset + 80);
	const uint4 g = buffer.Load4(offset + 96);
	MeshDesc r;
	r.camera_offset = a;
	r.clusters = b.x; r.nodes = b.y; r.node_count = b.z; r.root_count = b.w;
	r.roots_offset = c.x; r.tree_depth = c.y; r.scratch_offset = c.z; r.scratch_stride = c.w;
	r.instances = d.x; r.instances_offset = d.y; r.input_dynamic = d.z; r.instance_count = d.w;
	r.material_indices = e.x; r.material_indices_offset = e.y; r.material_index = e.z; r.mesh_count = e.w;
	r.mesh_index = f.x; r.region_index = f.y; r.region_base = f.z; r.region_cap = f.w;
	r.cone_culling = g.x; r.cluster_count = g.y;
	return r;
}

struct Instance {
	float4 rot;
	float3 pos;
	float3 scale;
	float4 prev_rot;
	float3 prev_pos;
	float3 prev_scale;
};

Instance loadInstance(MeshDesc desc, uint index) {
	ByteAddressBuffer buffer = bindless_buffers[desc.instances];
	Instance r;
	if (desc.input_dynamic) {
		uint offset = desc.instances_offset + index * 96;
		r.rot = asfloat(buffer.Load4(offset));
		r.pos = asfloat(buffer.Load3(offset + 16));
		r.scale = asfloat(buffer.Load3(offset + 32));
		r.prev_rot = asfloat(buffer.Load4(offset + 48));
		r.prev_pos = asfloat(buffer.Load3(offset + 64));
		r.prev_scale = asfloat(buffer.Load3(offset + 80));
	}
	else {
		uint offset = desc.instances_offset + index * 32;
		float4 rot_lod = asfloat(buffer.Load4(offset));
		float4 pos_scale = asfloat(buffer.Load4(offset + 16));
		r.rot = float4(rot_lod.xyz, sqrt(saturate(1 - dot(rot_lod.xyz, rot_lod.xyz))));
		r.pos = pos_scale.xyz;
		r.scale = pos_scale.www;
		// not moving
		r.prev_rot = r.rot;
		r.prev_pos = r.pos;
		r.prev_scale = r.scale;
	}
	r.pos += desc.camera_offset.xyz;
	r.prev_pos += desc.camera_offset.xyz;
	return r;
}

bool isErrorSmall(float error, float3 center, float radius) {
	float dist = u_is_ortho ? 1 : max(length(center) - radius, 1e-4);
	// clusters without a parent have FLT_MAX error
	return error < 1e30 && error <= u_lod_k * dist;
}

bool insideFrustum(float3 center, float radius) {
	for (int i = 0; i < 6; ++i) {
		if (dot(u_planes[i], float4(center, 1)) < -radius) return false;
	}
	return true;
}

// Tests a cluster and writes its draw if it is selected.
void processCluster(MeshDesc desc, Instance instance, float max_scale, uint instance_idx, uint cluster, RWByteAddressBuffer indirect) {
	ByteAddressBuffer clusters = bindless_buffers[desc.clusters];
	uint offset = cluster * 96;
	float4 bounds = asfloat(clusters.Load4(offset));
	float4 lod_bounds = asfloat(clusters.Load4(offset + 16));
	float4 parent_bounds = asfloat(clusters.Load4(offset + 32));
	uint4 misc = clusters.Load4(offset + 48);
	const float2 attribute_errors = asfloat(clusters.Load2(offset + 80)); // of the cluster and of its parent
	// the error of a cluster is the one of its positions and the one of its attributes, which counts as much as the weight says
	float error = (asfloat(misc.x) + u_attribute_weight * attribute_errors.x) * max_scale;
	// clusters without a parent have FLT_MAX error
	float parent_error = asfloat(misc.y) < 1e30 ? (asfloat(misc.y) + u_attribute_weight * attribute_errors.y) * max_scale : asfloat(misc.y);

	// the tests are done from the camera of the culling (the same as the one of the drawing, if it is not frozen), the instance is written as it is
	const float3 cull_pos = instance.pos + u_cull_offset.xyz;

	// the cluster is drawn if it is accurate enough and its parent is not
	float3 lod_center = cull_pos + rotateByQuat(instance.rot, lod_bounds.xyz * instance.scale);
	float3 parent_center = cull_pos + rotateByQuat(instance.rot, parent_bounds.xyz * instance.scale);
	if (!isErrorSmall(error, lod_center, lod_bounds.w * max_scale)) return;
	if (isErrorSmall(parent_error, parent_center, parent_bounds.w * max_scale)) return;

	// frustum culling
	float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);
	float radius = bounds.w * max_scale;
	if (!insideFrustum(center, radius)) return;

	// back facing clusters: all triangles face away from the camera. Positions are relative to the camera. The cone is rotated like
	// a normal, so it is only right for uniform positive scale, other instances are not culled.
	float4 cone = asfloat(clusters.Load4(offset + 64)); // xyz: axis, w: cutoff
	float min_scale = min(instance.scale.x, min(instance.scale.y, instance.scale.z));
	[branch] if (desc.cone_culling && cone.w < 1.0 && min_scale > 0 && max_scale - min_scale < max_scale * 0.001) {
		float3 axis = rotateByQuat(instance.rot, cone.xyz);
		if (dot(center, axis) >= cone.w * length(center) + radius) {
			uint culled;
			indirect.InterlockedAdd(desc.region_index * 4 + u_command_offset / 3, 1, culled);
			return;
		}
	}

	uint slot;
	indirect.InterlockedAdd(desc.region_index * 4, 1, slot);
	if (slot >= desc.region_cap) return;

	uint material_index = desc.material_index;
	if (desc.material_indices) {
		material_index = bindless_buffers[desc.material_indices].Load(desc.material_indices_offset + (instance_idx * desc.mesh_count + desc.mesh_index) * 4);
	}

	uint global_slot = desc.region_base + slot;
	RWByteAddressBuffer out_instances = bindless_rw_buffers[u_out_instances];
	uint out_offset = global_slot * 96;
	// the index of the cluster is only read by the CLUSTER_DEBUG variant of surface shaders (exact up to 2^24)
	out_instances.Store4(out_offset, asuint(instance.rot));
	out_instances.Store4(out_offset + 16, asuint(float4(instance.pos, (float)cluster)));
	out_instances.Store4(out_offset + 32, asuint(float4(instance.scale, 0)));
	out_instances.Store4(out_offset + 48, asuint(instance.prev_rot));
	out_instances.Store4(out_offset + 64, asuint(float4(instance.prev_pos, (float)cluster)));
	out_instances.Store4(out_offset + 80, uint4(asuint(instance.prev_scale.x), asuint(instance.prev_scale.y), asuint(instance.prev_scale.z), material_index));

	uint command = u_command_offset + global_slot * 24;
	indirect.Store(command, global_slot);	// draw id
	indirect.Store(command + 4, misc.w);	// index count
	indirect.Store(command + 8, 1);			// instance count
	indirect.Store(command + 12, misc.z);	// first index
	indirect.Store(command + 16, 0);		// base vertex
	indirect.Store(command + 20, 0);		// base instance
}

// false if no cluster of the node can be selected, then the node and everything under it is skipped
bool isNodeNeeded(float4 bounds, float4 lod_bounds, float min_error, float max_parent_error, float min_attribute_error, float max_parent_attribute_error, Instance instance, float max_scale) {
	const float3 cull_pos = instance.pos + u_cull_offset.xyz;
	float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);
	if (!insideFrustum(center, bounds.w * max_scale)) return false;

	// The distance of a cluster is measured from the sphere of its lod bounds (or the one of its parent), which is inside the lod sphere of the node:
	// it is not nearer than the nearest point of the node sphere and not farther than the farthest one.
	float3 lod_center = cull_pos + rotateByQuat(instance.rot, lod_bounds.xyz * instance.scale);
	float lod_radius = lod_bounds.w * max_scale;
	float to_center = length(lod_center);
	float near_dist = u_is_ortho ? 1 : max(to_center - lod_radius, 1e-4);
	float far_dist = u_is_ortho ? 1 : to_center + lod_radius;
	// none of the clusters is accurate enough (the smallest error of positions and the smallest of attributes are not of the same cluster, the sum is a lower bound)
	if ((min_error + u_attribute_weight * min_attribute_error) * max_scale > u_lod_k * far_dist) return false;
	// the parents of all the clusters are accurate enough, they are drawn instead (clusters without a parent have FLT_MAX); the sum of the largest ones is an upper bound
	if (max_parent_error < 1e30 && (max_parent_error + u_attribute_weight * max_parent_attribute_error) * max_scale <= u_lod_k * near_dist) return false;
	return true;
}

groupshared uint g_current_count;
groupshared uint g_next_count;

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID) {
	RWByteAddressBuffer indirect = bindless_rw_buffers[u_out_indirect];
	#ifdef PASS0
		// clears the counters of u_clear_count consecutive regions, from u_clear_first, one group does it
		if (group_id.x == 0) {
			for (uint i = group_thread_id.x; i < u_clear_count; i += GROUP_SIZE) {
				indirect.Store((u_clear_first + i) * 4, 0);
				indirect.Store((u_clear_first + i) * 4 + u_command_offset / 3, 0);		// culled by cones
				indirect.Store((u_clear_first + i) * 4 + u_command_offset / 3 * 2, 0);	// visited
			}
		}
	#else
		const uint group_index = group_id.y * 65535 + group_id.x;
		if (group_index >= u_item_count) return; // the same for all threads of the group
		const uint4 item = bindless_buffers[u_items].Load4(u_items_offset + group_index * 16); // mesh, instance, first cluster
		const MeshDesc desc = loadDesc(item.x);
		const uint instance_idx = item.y;
		Instance instance = loadInstance(desc, instance_idx);
		// bounds are spheres, so non-uniform scale is handled by the largest axis
		float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));

		#ifdef FLAT
			// 64 consecutive clusters of an instance
			const uint cluster = item.z + group_thread_id.x;
			if (cluster < desc.cluster_count) processCluster(desc, instance, max_scale, instance_idx, cluster, indirect);
		#else
			ByteAddressBuffer nodes = bindless_buffers[desc.nodes];
			RWByteAddressBuffer scratch = bindless_rw_buffers[u_scratch];
			const uint scratch_base = (desc.scratch_offset + instance_idx * desc.scratch_stride) * 4;
			const uint list_size = desc.node_count * 4;
			uint visited = 0;

			// the roots are the first nodes to visit
			for (uint r = group_thread_id.x; r < desc.root_count; r += GROUP_SIZE) {
				scratch.Store(scratch_base + r * 4, nodes.Load(desc.roots_offset + r * 4));
			}
			if (group_thread_id.x == 0) {
				g_current_count = desc.root_count;
				g_next_count = 0;
			}
			DeviceMemoryBarrierWithGroupSync();

			// A loop with the depth of the deepest tree of the meshes of the dispatch, a uniform value, so that the synchronizations are in flow
			// control that is the same for all threads (the shader compiler can not tell that a loop that ends when a value in groupshared memory is
			// 0 ends for all threads at the same time). Levels after the last one of a shallower tree have no nodes, they cost two synchronizations.
			uint list = 0;
			[loop] for (uint depth = 0; depth < u_tree_depth; ++depth) {
				const uint count = g_current_count; // all threads read it between the same two synchronizations
				const uint from = scratch_base + list * list_size;
				const uint to = scratch_base + (1 - list) * list_size;

				for (uint i = group_thread_id.x; i < count; i += GROUP_SIZE) {
					const uint node_id = scratch.Load(from + i * 4);
					const uint node_offset = node_id * 64;
					const float4 bounds = asfloat(nodes.Load4(node_offset));
					const float4 lod_bounds = asfloat(nodes.Load4(node_offset + 16));
					const uint4 misc = nodes.Load4(node_offset + 32); // min error, max parent error, first, count and flags
					const float2 attribute_errors = asfloat(nodes.Load2(node_offset + 48)); // the smallest and the largest of parents
					++visited;
					if (!isNodeNeeded(bounds, lod_bounds, asfloat(misc.x), asfloat(misc.y), attribute_errors.x, attribute_errors.y, instance, max_scale)) continue;

					const uint child_count = misc.w & 0x7fffffffu;
					if (misc.w & 0x80000000u) {
						// a leaf: clusters
						for (uint c = 0; c < child_count; ++c) {
							++visited;
							processCluster(desc, instance, max_scale, instance_idx, misc.z + c, indirect);
						}
					}
					else {
						uint at;
						InterlockedAdd(g_next_count, child_count, at);
						for (uint c = 0; c < child_count; ++c) scratch.Store(to + (at + c) * 4, misc.z + c);
					}
				}

				DeviceMemoryBarrierWithGroupSync(); // the nodes of the next level are written
				if (group_thread_id.x == 0) {
					g_current_count = g_next_count;
					g_next_count = 0;
				}
				GroupMemoryBarrierWithGroupSync();
				list = 1 - list;
			}

			if (visited > 0) {
				uint previous;
				indirect.InterlockedAdd(desc.region_index * 4 + u_command_offset / 3 * 2, visited, previous);
			}
		#endif
	#endif
}
