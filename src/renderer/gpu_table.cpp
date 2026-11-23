#include "gpu_table.h"

#include "core/arena_allocator.h"
#include "core/log.h"
#include "renderer/draw_stream.h"
#include "renderer/renderer.h"

namespace Lumix {

// ranges

u32 rangeAlloc(RangeAllocator& ranges, u32 count) {
	ASSERT(count > 0);
	for (u32 i = 0, c = ranges.free_ranges.size(); i < c; ++i) {
		RangeAllocator::Range& range = ranges.free_ranges[i];
		if (range.count < count) continue;
		const u32 first = range.first;
		range.first += count;
		range.count -= count;
		if (range.count == 0) ranges.free_ranges.erase(i);
		return first;
	}
	const u32 first = ranges.size;
	ranges.size += count;
	return first;
}

void rangeFree(RangeAllocator& ranges, u32 first, u32 count) {
	if (count == 0) return;
	ASSERT(first + count <= ranges.size);
	Array<RangeAllocator::Range>& list = ranges.free_ranges;
	u32 i = 0;
	while (i < (u32)list.size() && list[i].first < first) ++i;
	list.insert(i, { first, count });
	if (i + 1 < (u32)list.size() && list[i].first + list[i].count == list[i + 1].first) {
		list[i].count += list[i + 1].count;
		list.erase(i + 1);
	}
	if (i > 0 && list[i - 1].first + list[i - 1].count == list[i].first) {
		list[i - 1].count += list[i].count;
		list.erase(i);
		--i;
	}
	// a free range at the end is not a range, the end moves back
	if (list[i].first + list[i].count == ranges.size) {
		ranges.size = list[i].first;
		list.erase(i);
	}
}

// uploads

void queueCopy(GPUUploadQueue& queue, gpu::BufferHandle dst, u32 dst_offset, gpu::BufferHandle src, u32 src_offset, u32 size, bool src_transient) {
	if (size == 0) return;
	GPUUploadQueue::Copy& copy = queue.copies.emplace();
	copy.dst = dst;
	copy.dst_offset = dst_offset;
	copy.src = src;
	copy.src_offset = src_offset;
	copy.size = size;
	copy.src_transient = src_transient;
}

void submitCopies(GPUUploadQueue& queue, Renderer& renderer, const char* name) {
	const u32 count = queue.copies.size();
	if (count == 0) return;
	// the job runs later, it gets its own copy of the list
	GPUUploadQueue::Copy* copies = (GPUUploadQueue::Copy*)renderer.getCurrentFrameAllocator().allocate(sizeof(GPUUploadQueue::Copy) * count, alignof(GPUUploadQueue::Copy));
	memcpy(copies, queue.copies.begin(), sizeof(GPUUploadQueue::Copy) * count);
	queue.copies.clear();
	renderer.pushJob(name, [copies, count](DrawStream& stream) {
		for (u32 i = 0; i < count; ++i) {
			const GPUUploadQueue::Copy& copy = copies[i];
			if (copy.src_transient) stream.barrier(copy.src, gpu::BarrierType::READ);
			stream.copy(copy.dst, copy.src, copy.dst_offset, copy.src_offset, copy.size);
		}
	});
}

static void markDirty(GPUMirror& mirror, u32 offset, u32 size) {
	if (size == 0) return;
	const u32 first = offset / GPUMirror::PAGE_SIZE;
	const u32 last = (offset + size - 1) / GPUMirror::PAGE_SIZE;
	for (u32 page = first; page <= last; ++page) mirror.dirty[page >> 5] |= 1u << (page & 31);
	mirror.any_dirty = true;
}

static bool isDirty(const GPUMirror& mirror, u32 page) {
	return (mirror.dirty[page >> 5] & (1u << (page & 31))) != 0;
}

void mirrorResize(GPUMirror& mirror, u32 bytes) {
	const u32 old_size = mirror.data.size();
	if (bytes == old_size) return;
	mirror.data.resize(bytes);
	const u32 pages = (bytes + GPUMirror::PAGE_SIZE - 1) / GPUMirror::PAGE_SIZE;
	const u32 old_words = mirror.dirty.size();
	const u32 words = (pages + 31) / 32;
	mirror.dirty.resize(words);
	for (u32 i = old_words; i < words; ++i) mirror.dirty[i] = 0;
	if (bytes > old_size) {
		// the buffer can have old data there
		memset(mirror.data.begin() + old_size, 0, bytes - old_size);
		markDirty(mirror, old_size, bytes - old_size);
	}
}

u8* mirrorWrite(GPUMirror& mirror, u32 offset, u32 size) {
	ASSERT(offset + size <= (u32)mirror.data.size());
	markDirty(mirror, offset, size);
	return mirror.data.begin() + offset;
}

void mirrorUpload(GPUMirror& mirror, Renderer& renderer, GPUUploadQueue& queue) {
	const u32 size = mirror.data.size();
	const u32 needed = maximum(size, (u32)GPUMirror::MIN_GPU_SIZE);
	if (!mirror.buffer || mirror.gpu_size < needed) {
		// a new buffer with the whole content
		if (mirror.buffer) renderer.getEndFrameDrawStream().destroy(mirror.buffer);
		u32 gpu_size = maximum(mirror.gpu_size, (u32)GPUMirror::MIN_GPU_SIZE);
		while (gpu_size < needed) gpu_size *= 2;
		Renderer::MemRef mem = renderer.allocate(gpu_size);
		memset(mem.data, 0, gpu_size);
		if (size) memcpy(mem.data, mirror.data.begin(), size);
		mirror.buffer = renderer.createBuffer(mem, gpu::BufferFlags::SHADER_BUFFER, mirror.name);
		mirror.gpu_size = gpu_size;
		for (u32& word : mirror.dirty) word = 0;
		mirror.any_dirty = false;
		return;
	}
	if (!mirror.any_dirty) return;

	// the runs of dirty pages
	TransientPool& pool = renderer.getTransientPool();
	const u32 pages = (size + GPUMirror::PAGE_SIZE - 1) / GPUMirror::PAGE_SIZE;
	u32 page = 0;
	while (page < pages) {
		if (!isDirty(mirror, page)) {
			++page;
			continue;
		}
		u32 end = page;
		while (end < pages && isDirty(mirror, end)) ++end;
		const u32 from = page * GPUMirror::PAGE_SIZE;
		const u32 to = minimum(end * GPUMirror::PAGE_SIZE, size);
		const TransientSlice slice = alloc(pool, to - from);
		memcpy(slice.ptr, mirror.data.begin() + from, to - from);
		queueCopy(queue, mirror.buffer, from, slice.buffer, slice.offset, to - from, true);
		page = end;
	}
	for (u32& word : mirror.dirty) word = 0;
	mirror.any_dirty = false;
}

void mirrorDestroy(GPUMirror& mirror, Renderer& renderer) {
	if (mirror.buffer) renderer.getEndFrameDrawStream().destroy(mirror.buffer);
	mirror.buffer = gpu::INVALID_BUFFER;
	mirror.gpu_size = 0;
}

} // namespace Lumix
