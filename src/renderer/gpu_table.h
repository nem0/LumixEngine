#pragma once

#include "core/array.h"
#include "renderer/gpu/gpu.h"

namespace Lumix {

struct Renderer;

// Ranges of [0, size): the first free range that is large enough, adjacent free ranges are merged, `size` grows when nothing fits.
struct RangeAllocator {
	struct Range {
		u32 first;
		u32 count;
	};

	explicit RangeAllocator(IAllocator& allocator) : free_ranges(allocator) {}

	Array<Range> free_ranges;	// sorted by `first`, never touching each other or the end
	u32 size = 0;				// the end of the last used range
};

u32 rangeAlloc(RangeAllocator& ranges, u32 count);
void rangeFree(RangeAllocator& ranges, u32 first, u32 count);

// Copies from buffer to buffer, recorded by one job of the renderer (in the order of the jobs of the frame).
struct GPUUploadQueue {
	struct Copy {
		gpu::BufferHandle dst;
		u32 dst_offset;
		gpu::BufferHandle src;
		u32 src_offset;
		u32 size;
		bool src_transient;		// a slice of the transient pool, it gets a barrier before the copy
	};

	explicit GPUUploadQueue(IAllocator& allocator) : copies(allocator) {}

	Array<Copy> copies;
};

void queueCopy(GPUUploadQueue& queue, gpu::BufferHandle dst, u32 dst_offset, gpu::BufferHandle src, u32 src_offset, u32 size, bool src_transient);
// pushes a job of the renderer that records the copies, the queue is empty after it
void submitCopies(GPUUploadQueue& queue, Renderer& renderer, const char* name);

// A GPU buffer with a copy of its content in CPU memory. Writes mark the pages they touch, uploadMirror() sends the marked pages only. The buffer is made again (with the
// whole content) when it is too small, so its handle can change with any upload.
struct GPUMirror {
	enum : u32 {
		PAGE_SIZE = 4096,
		MIN_GPU_SIZE = 256
	};

	GPUMirror(IAllocator& allocator, const char* name) : data(allocator), dirty(allocator), name(name) {}

	Array<u8> data;
	Array<u32> dirty;		// a bit for each page
	bool any_dirty = false;
	gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
	u32 gpu_size = 0;
	const char* name;
};

void mirrorResize(GPUMirror& mirror, u32 bytes);				// the content is kept, new bytes are 0
u8* mirrorWrite(GPUMirror& mirror, u32 offset, u32 size);		// the bytes [offset, offset + size) change, the result points to them
// Must be called when the frame is set up (it can create the buffer, which is recorded into the draw stream of the frame), not from a job.
void mirrorUpload(GPUMirror& mirror, Renderer& renderer, GPUUploadQueue& queue);
void mirrorDestroy(GPUMirror& mirror, Renderer& renderer);

} // namespace Lumix
