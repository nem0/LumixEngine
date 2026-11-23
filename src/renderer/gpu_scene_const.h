#pragma once

#include "engine/lumix.h"

namespace Lumix {

namespace GPUSceneConst {
	// positions of the GPU scene are 32 bit fixed point, `POS_UNIT` (a power of two, so that the conversion to float is exact) per step, see gpu_scene.h
	constexpr double POS_UNIT = 1.0 / 1024.0;
	constexpr double POS_LIMIT = 2147483647.0 * POS_UNIT;
	constexpr u32 INVALID_SLOT = 0xffFFffFF;
	constexpr u32 INITIAL_CAPACITY = 1024;
	constexpr u32 INVALID_MODEL = 0xffFFffFF;
	constexpr u32 INVALID_SET = 0xffFFffFF;
	// The id of a model: the index in the table of its kind (gpu_cluster_models.h, gpu_lod_models.h), the ids of models without clusters have LOD_MODEL_BIT set.
	constexpr u32 LOD_MODEL_BIT = 0x80000000;
	// meshes with up to that many clusters are tested cluster by cluster (flat), groups of CLUSTER_THREADS clusters, see pipeline.cpp. The same in gpu_scene_cull.hlsl.
	constexpr u32 FLAT_MAX_CLUSTERS = 512;
	// A variant is what one command of an indirect draw draws: a mesh (meshes with clusters share one for all the meshes with the same program) with a material
	// program. Variants of the same program (and layer) are a draw group, which is drawn by one indirect call of one command for each variant.
	constexpr u32 MAX_VARIANTS = 8192;
	constexpr u32 MAX_DRAW_GROUPS = 2048;
	constexpr u32 INVALID_VARIANT = 0xffFFffFF;
	constexpr u32 INVALID_GROUP = 0xffFFffFF;
	constexpr u32 CLUSTER_THREADS = 64;
	// instances of an instanced model in one chunk of the GPU scene, see gpuSceneSetInstances
	constexpr u32 CHUNK_INSTANCES = 256;
}

inline bool isLodModelId(u32 id) { return id != GPUSceneConst::INVALID_MODEL && (id & GPUSceneConst::LOD_MODEL_BIT) != 0; }
inline u32 modelIndex(u32 id) { return id & ~GPUSceneConst::LOD_MODEL_BIT; }

} // namespace Lumix
