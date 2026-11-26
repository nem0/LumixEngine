#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/job_system.h"
#include "core/span.h"
#include "core/sync.h"
#include "renderer/gpu/gpu.h"
#include "renderer/gpu_table.h"
#include "renderer/mesh_clusters.h"

namespace Lumix {

struct DrawStream;
struct Renderer;

// Owns the shared GPU cluster data pool, shared culling/indirect-command scratch buffers, and page streaming state.
// Extracted from RendererImpl so cluster memory management and streaming live in one self-contained module.
struct LUMIX_RENDERER_API ClusterStreamer {
	static constexpr u32 DATA_POOL_SIZE = 256 * 1024 * 1024;
	static constexpr u32 DATA_ALIGN = 16;
	static constexpr u32 DATA_FREE_DELAY = 4;				// frames
	static constexpr u32 SCRATCH_CAPACITY = 1 << 22;		// u32 in the scratch buffer of the culling of trees (16 MB)
	static constexpr u32 COMMANDS_CAPACITY = 262144;		// 256K clusters per draw group * 24 B = 6 MB

	static constexpr u32 STREAM_MAX_GROUPS = 1 << 20;		// pages with ids in the feedback buffer, 128 KB (1 bit each)
	static constexpr u32 STREAM_LOADS_PER_FRAME = 16;		// pages uploaded to the buffer of cluster data in a frame
	static constexpr u32 STREAM_READS_IN_FLIGHT = 64;		// pages that are being read from the files
	static constexpr u32 STREAM_RETRY_FRAMES = 120;			// frames before retrying a failed read
	static constexpr u32 STREAM_READBACK_PERIOD = 1;		// frames
	static constexpr u32 STREAM_REQUEST_FRAMES = 16;		// frames a GPU request stays active
	static constexpr u32 STREAM_MIN_RESIDENCY = 30;			// frames a loaded page stays unless nothing else can be evicted
	static constexpr u64 STREAM_STAGING_BYTES = 64ull << 20;
	static constexpr u64 STREAM_HEADROOM = 32ull << 20;

	ClusterStreamer(Renderer& renderer, IAllocator& allocator);
	void destroy(DrawStream& stream);

	// Shared GPU cluster data pool (ranges are aligned, freed ranges are reused after DATA_FREE_DELAY frames).
	bool allocData(const void* data, u32 size, u32& offset);
	bool allocRange(u32 size, u32 alignment, u32& offset, bool reclaim = true);
	void freeData(u32 offset, u32 size, bool immediate = false);
	void updateData(u32 offset, const void* data, u32 size);

	gpu::BufferHandle getDataBuffer() const { return m_data_buffer; }
	gpu::BufferHandle getScratchBuffer();
	u32 getScratchCapacity() const { return SCRATCH_CAPACITY; }
	gpu::BufferHandle getCommandsBuffer();
	u32 getCommandsCapacity() const { return COMMANDS_CAPACITY; }

	// Page streaming lifecycle.
	u32 allocStreamIds(u32 count);
	void registerStream(ClusterStream* stream);
	void unregisterStream(ClusterStream* stream);
	bool isEnabled() const { return m_enabled; }
	void onPageRead(ClusterStream* stream, u64 generation, u32 page_index, Span<const u8> data, bool success);
	gpu::BufferHandle getFeedbackBuffer() const { return m_feedback_buffer; }
	u32 getFeedbackCapacity() const { return m_feedback_buffer ? STREAM_MAX_GROUPS : 0; }
	void update();

private:
	struct DataRange {
		u32 offset;
		u32 size;
		u32 frame; // frame of free, only for pending ranges
	};
	struct StreamRef {
		ClusterStream* stream;
		u32 page;
	};

	void releaseDataRange(u32 offset, u32 size);
	bool tryAllocRange(u32 size, u32 alignment, u32& offset);
	u32 getLargestFreeRange();
	void flushPendingData();
	bool reclaimData(u32 size, u32 alignment);
	u64 getDataFreeBytes();

	void onFeedback(Span<const u8> data);
	u64 getStagedBytes() const;
	bool requestPage(ClusterStream& stream, u32 page_index, u32 stamp);
	void sortStreamRefs(Array<StreamRef>& refs);
	void uploadReadPages(u32 stamp);
	bool evictLeastRecentlyUsedPage(u32 stamp, bool emergency);
	bool makeStreamRoom(u32 bytes, u32 stamp);
	void processFeedback(u32 stamp);

	Renderer& m_renderer;
	IAllocator& m_allocator;

	jobs::Mutex m_data_mutex;
	gpu::BufferHandle m_data_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_scratch_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_commands_buffer = gpu::INVALID_BUFFER;
	Array<DataRange> m_data_free;
	Array<DataRange> m_data_pending;

	bool m_enabled = true;
	bool m_load_all = false;
	u64 m_budget = 0;
	jobs::Mutex m_stream_mutex;
	Array<ClusterStream*> m_streams;
	RangeAllocator m_id_ranges;
	Array<StreamRef> m_pending;
	Array<StreamRef> m_ready;
	u64 m_generation = 0;
	u32 m_reads = 0;
	u32 m_loads = 0;
	u32 m_evictions = 0;
	gpu::BufferHandle m_feedback_buffer = gpu::INVALID_BUFFER;
	Mutex m_feedback_mutex;
	Array<u8> m_feedback_incoming;
	Array<u8> m_feedback_local;
};

} // namespace Lumix
