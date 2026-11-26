#include "cluster_streaming.h"

#include "core/command_line_parser.h"
#include "core/crt.h"
#include "core/log.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "renderer/draw_stream.h"
#include "renderer/model.h"
#include "renderer/renderer.h"

namespace Lumix {

// -cluster_stream_mb <n>: the budget of the pages of clusters that are streamed (without the permanent ones)
static u64 getClusterStreamBudget() {
	u64 mb = 96;
	char tmp[4096];
	if (!os::getCommandLine(Span(tmp))) return mb << 20;
	CommandLineParser parser(tmp);
	while (parser.next()) {
		if (!parser.currentEquals("-cluster_stream_mb")) continue;
		if (!parser.next()) break;
		char value[32];
		parser.getCurrent(value, sizeof(value));
		u64 parsed = 0;
		for (const char* c = value; *c >= '0' && *c <= '9'; ++c) parsed = parsed * 10 + u64(*c - '0');
		if (parsed > 0) mb = parsed;
		break;
	}
	return mb << 20;
}

// ---- ClusterStream ----

ClusterStream::ClusterStream(IAllocator& allocator)
	: allocator(allocator)
	, groups(allocator)
	, group_page(allocator)
	, group_index_count(allocator)
	, page_parents(allocator)
	, page_children(allocator)
	, pages(allocator)
	, addresses(allocator)
	, nodes(allocator)
	, cluster_refined(allocator)
{}

bool ClusterStream::init(const MeshClusterGPU* cluster_array, u32 clusters, const MeshNodeGPU* node_array, u32 nodes_count, const MeshGroupGPU* group_array, u32 group_count
	, const MeshPagesView& page_data, u32 stream_size, ClusterStreamer& streamer)
{
	cluster_count = clusters;
	node_count = nodes_count;

	groups.resize(group_count);
	group_page.resize(group_count);
	group_index_count.resize(group_count);
	for (u32& c : group_index_count) c = 0;
	memcpy(groups.begin(), group_array, u64(group_count) * sizeof(MeshGroupGPU));
	for (const MeshGroupGPU& g : groups) {
		if (g.cluster_count == 0 || u64(g.first_cluster) + g.cluster_count > cluster_count || u64(g.stream_offset) + g.stream_size > stream_size || (g.stream_offset & 3) != 0) return false;
	}

	nodes.resize(node_count);
	memcpy(nodes.begin(), node_array, u64(node_count) * sizeof(MeshNodeGPU));
	cluster_refined.resize(cluster_count);
	for (u32 i = 0; i < cluster_count; ++i) {
		cluster_refined[i] = cluster_array[i].refined_group;
		if (cluster_array[i].group >= group_count) return false;
		if (cluster_array[i].refined_group != MeshClusters::NO_GROUP && cluster_array[i].refined_group >= group_count) return false;
		group_index_count[cluster_array[i].group] += clusterIndexCount(cluster_array[i]);
	}

	// The pages are made by the importer (buildMeshPages), and are valid: they have all the groups in order, a page needs the pages that have higher numbers.
	const u32 page_count = page_data.page_count;
	pages.resize(page_count);
	for (u32 p = 0; p < page_count; ++p) {
		MeshPageFile file_page;
		memcpy(&file_page, page_data.pages + p, sizeof(file_page));	// the file does not align it
		Page& page = pages[p];
		page.first_group = file_page.first_group;
		page.group_count = file_page.group_count;
		page.permanent = file_page.permanent != 0;
		page.parent_first = file_page.parent_first;
		page.parent_count = file_page.parent_count;
		page.child_first = file_page.child_first;
		page.child_count = file_page.child_count;
		page.file_offset = file_page.file_offset;
		page.priority = file_page.priority;
		const MeshGroupGPU& first_g = groups[page.first_group];
		const MeshGroupGPU& last_g = groups[page.first_group + page.group_count - 1];
		page.stream_bytes = last_g.stream_offset + last_g.stream_size - first_g.stream_offset;
		page.index_count = 0;
		for (u32 g = page.first_group; g < page.first_group + page.group_count; ++g) {
			group_page[g] = p;
			page.index_count += group_index_count[g];
		}
	}

	page_parents.resize(page_data.edge_count);
	memcpy(page_parents.begin(), page_data.parents, u64(page_data.edge_count) * sizeof(u32));

	page_children.resize(page_data.edge_count);
	memcpy(page_children.begin(), page_data.children, u64(page_data.edge_count) * sizeof(u32));

	root_bytes = page_data.root_bytes;

	// the feedback has an id for each page (a bit), the shaders get it from the address of every group of the page, see addressFlags
	global_base = streamer.allocStreamIds((u32)pages.size());

	addresses.resize(group_count);
	for (u32 g = 0; g < group_count; ++g) {
		addresses[g].stream_address = 0;
		addresses[g].index_address = 0;
		addresses[g].first_cluster = groups[g].first_cluster;
		addresses[g].flags = addressFlags(g);
	}
	return true;
}

u32 ClusterStream::addressFlags(u32 g) const {
	u32 flags = 0;
	if (isGroupLoaded(g)) flags |= MeshGroupAddressGPU::LOADED;
	if (pages[group_page[g]].permanent) flags |= MeshGroupAddressGPU::PERMANENT;
	// the id of the page of the group in the feedback buffer, shaders ignore ids that are out of the buffer
	const u32 id = global_base == NONE ? 0xffFFffu : minimum(global_base + group_page[g], 0xffFFffu);
	return flags | (id << 8);
}

ClusterStream::PageLayout ClusterStream::pageLayout(u32 page_index) const {
	const Page& page = pages[page_index];
	PageLayout r;
	r.stream_bytes = page.stream_bytes;
	r.index_count = page.index_count;
	r.file_bytes = (r.stream_bytes + r.index_count + 15) & ~15u;
	r.gpu_bytes = (r.stream_bytes + r.index_count * 2 + 15) & ~15u;
	return r;
}

u32 ClusterStream::pageBytes(u32 page_index) const {
	return pageLayout(page_index).file_bytes;
}

u32 ClusterStream::pageGpuBytes(u32 page_index) const {
	return pageLayout(page_index).gpu_bytes;
}

u32 ClusterStream::firstMissingParentPage(u32 page_index) const {
	for (u32 i = 0; i < pages[page_index].parent_count; ++i) {
		const u32 parent = page_parents[pages[page_index].parent_first + i];
		if (!pages[parent].isLoaded()) return parent;
	}
	return NONE;
}

void ClusterStream::markParentPagesWanted(u32 page_index, u32 stamp) {
	for (u32 i = 0; i < pages[page_index].parent_count; ++i) {
		pages[page_parents[pages[page_index].parent_first + i]].wanted_frame = stamp;
	}
}

bool ClusterStream::canEvict(u32 page_index) const {
	const Page& page = pages[page_index];
	if (!page.isLoaded() || page.permanent) return false;
	// the groups of the page must not be parents of loaded/in-flight/staged groups of other pages
	for (u32 i = 0; i < pages[page_index].child_count; ++i) {
		const Page& child = pages[page_children[pages[page_index].child_first + i]];
		if (child.state != Page::State::UNLOADED) return false;
	}
	return true;
}

bool ClusterStream::loadPage(ClusterStreamer& streamer, u32 page_index, u32 stamp, bool can_reclaim, const u8* data) {
	Page& page = pages[page_index];
	ASSERT(data);
	ASSERT(!page.isLoaded());
	ASSERT(firstMissingParentPage(page_index) == NONE);
	const MeshGroupGPU& first = groups[page.first_group];
	const PageLayout layout = pageLayout(page_index);
	u32 offset;
	if (!streamer.allocRange(layout.gpu_bytes, 16, offset, can_reclaim)) return false;
	// `data` is the page as it is in the file: vertex streams (4-byte aligned) followed by tightly packed u8 indices; widen indices to u16 on upload
	if (layout.stream_bytes > 0) streamer.updateData(offset, data, layout.stream_bytes);
	if (layout.index_count > 0) {
		const u32 padded_count = (layout.index_count + 1) & ~1u;
		Array<u16> widened(allocator);
		widened.resize(padded_count);
		const u8* src_indices = data + layout.stream_bytes;
		for (u32 i = 0; i < layout.index_count; ++i) widened[i] = src_indices[i];
		if (padded_count > layout.index_count) widened[layout.index_count] = 0;
		streamer.updateData(offset + layout.stream_bytes, widened.begin(), padded_count * sizeof(u16));
	}

	page.state = Page::State::LOADED;
	page.pool_offset = offset;
	page.pool_size = layout.gpu_bytes;
	page.loaded_frame = stamp;
	u32 page_index_cursor = 0;
	for (u32 g = page.first_group; g < page.first_group + page.group_count; ++g) {
		addresses[g].stream_address = offset + (groups[g].stream_offset - first.stream_offset);
		addresses[g].index_address = offset + layout.stream_bytes + page_index_cursor * 2;
		addresses[g].flags = addressFlags(g);
		page_index_cursor += group_index_count[g];
	}
	if (!page.permanent) streamed_bytes += layout.gpu_bytes;
	// the table of the GPU after the payload: both are copies in the stream of the frame, in this order
	if (group_table_offset != NONE) streamer.updateData(group_table_offset + page.first_group * (u32)sizeof(MeshGroupAddressGPU), &addresses[page.first_group], page.group_count * (u32)sizeof(MeshGroupAddressGPU));
	refreshNodes(streamer);
	return true;
}

void ClusterStream::dropReadData(u32 page_index) {
	Page& page = pages[page_index];
	if (!page.read_data) return;
	allocator.deallocate(page.read_data);
	page.read_data = nullptr;
	staged_bytes -= pageBytes(page_index);
	if (page.state == Page::State::STAGED) page.state = Page::State::UNLOADED;
}

void ClusterStream::evictPage(ClusterStreamer& streamer, u32 page_index, bool immediate) {
	Page& page = pages[page_index];
	ASSERT(page.isLoaded() && !page.permanent);
	page.state = Page::State::UNLOADED;
	for (u32 g = page.first_group; g < page.first_group + page.group_count; ++g) {
		addresses[g].flags = addressFlags(g);
	}
	// the groups are not loaded for the GPU before the range is freed (the range is reused after the frames in flight, see ClusterStreamer::freeData)
	if (group_table_offset != NONE) streamer.updateData(group_table_offset + page.first_group * (u32)sizeof(MeshGroupAddressGPU), &addresses[page.first_group], page.group_count * (u32)sizeof(MeshGroupAddressGPU));
	streamed_bytes -= page.pool_size;
	refreshNodes(streamer);
	streamer.freeData(page.pool_offset, page.pool_size, immediate);
	page.pool_offset = 0;
	page.pool_size = 0;
}

void ClusterStream::release(ClusterStreamer& streamer) {
	for (u32 p = 0; p < (u32)pages.size(); ++p) {
		Page& page = pages[p];
		dropReadData(p);
		if (!page.isLoaded()) {
			page.state = Page::State::UNLOADED;
			continue;
		}
		streamer.freeData(page.pool_offset, page.pool_size);
		page.state = Page::State::UNLOADED;
	}
	streamed_bytes = 0;
	staged_bytes = 0;
}

void ClusterStream::refreshNodes(ClusterStreamer& streamer) {
	u32 first_changed = NONE, last_changed = 0;
	for (u32 n = 0; n < node_count; ++n) {
		MeshNodeGPU& node = nodes[n];
		const u32 count = node.count_and_flags & 0x7fffFFFFu;
		// the children of a node have lower numbers than the node (see buildNodes)
		bool unrefined = false;
		for (u32 k = 0; k < count && !unrefined; ++k) {
			if (node.count_and_flags & MeshClusters::LEAF_FLAG) {
				const u32 c = node.first + k;
				const u32 refined = c < cluster_count ? cluster_refined[c] : NONE;
				unrefined = refined != MeshClusters::NO_GROUP && (refined >= (u32)groups.size() || !isGroupLoaded(refined));
			}
			else {
				const u32 child = node.first + k;
				unrefined = child >= n || nodes[child].unrefined != 0;
			}
		}
		const u32 value = unrefined ? 1 : 0;
		if (node.unrefined == value) continue;
		node.unrefined = value;
		if (first_changed == NONE) first_changed = n;
		last_changed = n;
	}
	if (first_changed != NONE && node_table_offset != NONE) {
		streamer.updateData(node_table_offset + first_changed * (u32)sizeof(MeshNodeGPU), &nodes[first_changed], (last_changed - first_changed + 1) * (u32)sizeof(MeshNodeGPU));
	}
}

// ---- ClusterStreamer ----

ClusterStreamer::ClusterStreamer(Renderer& renderer, IAllocator& allocator)
	: m_renderer(renderer)
	, m_allocator(allocator)
	, m_data_free(allocator)
	, m_data_pending(allocator)
	, m_enabled(!CommandLineParser::isOn("-cluster_stream_off"))
	, m_load_all(CommandLineParser::isOn("-cluster_stream_all"))
	, m_budget(getClusterStreamBudget())
	, m_streams(allocator)
	, m_id_ranges(allocator)
	, m_pending(allocator)
	, m_ready(allocator)
	, m_feedback_incoming(allocator)
	, m_feedback_local(allocator)
{}

void ClusterStreamer::destroy(DrawStream& stream) {
	if (m_data_buffer) stream.destroy(m_data_buffer);
	m_data_buffer = gpu::INVALID_BUFFER;
	if (m_scratch_buffer) stream.destroy(m_scratch_buffer);
	m_scratch_buffer = gpu::INVALID_BUFFER;
	if (m_commands_buffer) stream.destroy(m_commands_buffer);
	m_commands_buffer = gpu::INVALID_BUFFER;
	if (m_feedback_buffer) stream.destroy(m_feedback_buffer);
	m_feedback_buffer = gpu::INVALID_BUFFER;
}

void ClusterStreamer::releaseDataRange(u32 offset, u32 size) {
	u32 i = 0;
	while (i < (u32)m_data_free.size() && m_data_free[i].offset < offset) ++i;
	const bool merge_prev = i > 0 && m_data_free[i - 1].offset + m_data_free[i - 1].size == offset;
	const bool merge_next = i < (u32)m_data_free.size() && offset + size == m_data_free[i].offset;
	if (merge_prev && merge_next) {
		m_data_free[i - 1].size += size + m_data_free[i].size;
		m_data_free.erase(i);
	}
	else if (merge_prev) {
		m_data_free[i - 1].size += size;
	}
	else if (merge_next) {
		m_data_free[i].offset = offset;
		m_data_free[i].size += size;
	}
	else {
		m_data_free.insert(i, DataRange{offset, size, 0});
	}
}

bool ClusterStreamer::tryAllocRange(u32 size, u32 alignment, u32& offset) {
	if (size == 0 || alignment == 0) return false;
	const u32 aligned_size = (size + DATA_ALIGN - 1) & ~(DATA_ALIGN - 1);
	jobs::MutexGuard lock(m_data_mutex);

	if (!m_data_buffer) {
		m_data_buffer = m_renderer.createBuffer(Renderer::MemRef{ DATA_POOL_SIZE, nullptr, false }, gpu::BufferFlags::SHADER_BUFFER, "cluster data");
		if (!m_data_buffer) return false;
		m_data_free.push(DataRange{0, DATA_POOL_SIZE, 0});
	}

	const u32 frame_number = m_renderer.frameNumber();
	for (u32 i = 0; i < (u32)m_data_pending.size();) {
		const DataRange r = m_data_pending[i];
		if (frame_number - r.frame >= DATA_FREE_DELAY) {
			m_data_pending.swapAndPop(i);
			releaseDataRange(r.offset, r.size);
		}
		else {
			++i;
		}
	}

	u32 best = 0xffFFffFF;
	u64 best_start = 0;
	for (u32 i = 0; i < (u32)m_data_free.size(); ++i) {
		const DataRange& r = m_data_free[i];
		const u64 start = (u64(r.offset) + alignment - 1) / alignment * alignment;
		if (start + aligned_size > u64(r.offset) + r.size) continue;
		if (best == 0xffFFffFF || r.size < m_data_free[best].size) {
			best = i;
			best_start = start;
		}
	}
	if (best == 0xffFFffFF) return false;

	offset = (u32)best_start;
	const DataRange taken = m_data_free[best];
	m_data_free.erase(best);
	const u32 after = u32(u64(taken.offset) + taken.size - (best_start + aligned_size));
	const u32 before = offset - taken.offset;
	if (after) m_data_free.insert(best, DataRange{u32(best_start + aligned_size), after, 0});
	if (before) m_data_free.insert(best, DataRange{taken.offset, before, 0});
	return true;
}

bool ClusterStreamer::allocRange(u32 size, u32 alignment, u32& offset, bool reclaim) {
	if (size == 0 || alignment == 0) return false;
	if (tryAllocRange(size, alignment, offset)) return true;
	if (reclaim && reclaimData(size, alignment) && tryAllocRange(size, alignment, offset)) return true;
	logError("Buffer of data of clusters is full (", (u32)DATA_POOL_SIZE >> 20, " MB), a range with ", size, " bytes does not fit");
	return false;
}

bool ClusterStreamer::allocData(const void* data, u32 size, u32& offset) {
	if (!allocRange(size, DATA_ALIGN, offset, true)) return false;
	const u32 aligned_size = (size + DATA_ALIGN - 1) & ~(DATA_ALIGN - 1);
	m_renderer.uploadToBuffer(m_data_buffer, offset, data, size, aligned_size);
	return true;
}

void ClusterStreamer::freeData(u32 offset, u32 size, bool immediate) {
	if (size == 0) return;
	const u32 aligned_size = (size + DATA_ALIGN - 1) & ~(DATA_ALIGN - 1);
	jobs::MutexGuard lock(m_data_mutex);
	if (immediate) releaseDataRange(offset, aligned_size);
	else m_data_pending.push(DataRange{offset, aligned_size, m_renderer.frameNumber()});
}

void ClusterStreamer::updateData(u32 offset, const void* data, u32 size) {
	ASSERT(size % 4 == 0 && m_data_buffer);
	m_renderer.uploadToBuffer(m_data_buffer, offset, data, size, size);
}

gpu::BufferHandle ClusterStreamer::getScratchBuffer() {
	if (!m_scratch_buffer) {
		m_scratch_buffer = m_renderer.createBuffer(Renderer::MemRef{ SCRATCH_CAPACITY * sizeof(u32), nullptr, false }, gpu::BufferFlags::SHADER_BUFFER, "cluster scratch");
	}
	return m_scratch_buffer;
}

gpu::BufferHandle ClusterStreamer::getCommandsBuffer() {
	if (!m_commands_buffer) {
		m_commands_buffer = m_renderer.createBuffer(Renderer::MemRef{ COMMANDS_CAPACITY * 24, nullptr, false }, gpu::BufferFlags::SHADER_BUFFER, "cluster indirect commands");
	}
	return m_commands_buffer;
}

u32 ClusterStreamer::allocStreamIds(u32 count) {
	jobs::MutexGuard lock(m_stream_mutex);
	const u32 first = rangeAlloc(m_id_ranges, count);
	if (first + count > STREAM_MAX_GROUPS) {
		rangeFree(m_id_ranges, first, count);
		static bool reported = false;
		if (!reported) logError("Out of ids of groups of clusters, meshes that are loaded later are streamed without feedback");
		reported = true;
		return ClusterStream::NONE;
	}
	return first;
}

void ClusterStreamer::registerStream(ClusterStream* stream) {
	jobs::MutexGuard lock(m_stream_mutex);
	stream->feedback_from = m_renderer.frameNumber() + 1 + STREAM_REQUEST_FRAMES;
	stream->generation = ++m_generation;
	m_streams.push(stream);
}

void ClusterStreamer::unregisterStream(ClusterStream* stream) {
	jobs::MutexGuard lock(m_stream_mutex);
	const i32 idx = m_streams.indexOf(stream);
	if (idx >= 0) m_streams.swapAndPop(idx);
	for (u32 i = m_pending.size(); i > 0;) {
		--i;
		if (m_pending[i].stream == stream) m_pending.erase(i);
	}
	for (u32 i = m_ready.size(); i > 0;) {
		--i;
		if (m_ready[i].stream == stream) m_ready.erase(i);
	}
	if (stream->global_base != ClusterStream::NONE) {
		rangeFree(m_id_ranges, stream->global_base, (u32)stream->pages.size());
		stream->global_base = ClusterStream::NONE;
	}
}

void ClusterStreamer::onFeedback(Span<const u8> data) {
	MutexGuard guard(m_feedback_mutex);
	m_feedback_incoming.resize((u32)data.length());
	if (data.length() > 0) memcpy(m_feedback_incoming.begin(), data.begin(), data.length());
}

void ClusterStreamer::onPageRead(ClusterStream* stream, u64 generation, u32 page_index, Span<const u8> data, bool success) {
	jobs::MutexGuard lock(m_stream_mutex);
	if (m_reads > 0) --m_reads;
	if (!stream || m_streams.indexOf(stream) < 0 || stream->generation != generation) return;
	if (page_index >= (u32)stream->pages.size()) return;

	ClusterStream::Page& page = stream->pages[page_index];
	ASSERT(page.state == ClusterStream::Page::State::READING);
	const u32 bytes = stream->pageBytes(page_index);
	if (!success || data.length() != bytes) {
		page.state = ClusterStream::Page::State::UNLOADED;
		page.read_failed = m_renderer.frameNumber() + 1;
		logError("Failed to read a page of clusters (", bytes, " bytes at ", page.file_offset, ")");
		return;
	}
	page.read_data = (u8*)stream->allocator.allocate(bytes, 16);
	memcpy(page.read_data, data.begin(), bytes);
	page.state = ClusterStream::Page::State::STAGED;
	stream->staged_bytes += bytes;
	m_ready.push(StreamRef{stream, page_index});
}

u64 ClusterStreamer::getStagedBytes() const {
	u64 bytes = 0;
	for (const ClusterStream* s : m_streams) bytes += s->staged_bytes;
	return bytes;
}

bool ClusterStreamer::requestPage(ClusterStream& stream, u32 page_index, u32 stamp) {
	ClusterStream::Page& page = stream.pages[page_index];
	if (page.state != ClusterStream::Page::State::UNLOADED) return true;
	if (page.read_failed && stamp - page.read_failed < STREAM_RETRY_FRAMES) return false;
	if (!stream.owner) return false;
	stream.markParentPagesWanted(page_index, stamp);
	for (u32 i = 0; i < page.parent_count; ++i) {
		if (!requestPage(stream, stream.page_parents[page.parent_first + i], stamp)) return false;
	}
	if (m_reads >= STREAM_READS_IN_FLIGHT || getStagedBytes() + stream.pageBytes(page_index) > STREAM_STAGING_BYTES) return false;
	page.state = ClusterStream::Page::State::READING;
	++m_reads;
	stream.owner->requestClusterPage(stream.mesh_index, page_index);
	return true;
}

void ClusterStreamer::sortStreamRefs(Array<StreamRef>& refs) {
	if (refs.size() < 2) return;
	sort(refs.begin(), refs.end(), [](const StreamRef& a, const StreamRef& b) {
		const ClusterStream::Page& pa = a.stream->pages[a.page];
		const ClusterStream::Page& pb = b.stream->pages[b.page];
		if (pa.priority != pb.priority) return pa.priority > pb.priority;
		return pa.requested > pb.requested;
	});
}

void ClusterStreamer::uploadReadPages(u32 stamp) {
	sortStreamRefs(m_ready);
	bool progress = true;
	while (progress && m_loads < STREAM_LOADS_PER_FRAME) {
		progress = false;
		for (u32 i = 0; i < (u32)m_ready.size() && m_loads < STREAM_LOADS_PER_FRAME;) {
			const StreamRef r = m_ready[i];
			ClusterStream& stream = *r.stream;
			ClusterStream::Page& page = stream.pages[r.page];
			if (stream.firstMissingParentPage(r.page) != ClusterStream::NONE) {
				++i;
				continue;
			}
			stream.markParentPagesWanted(r.page, stamp);
			if (!m_load_all && !makeStreamRoom(stream.pageGpuBytes(r.page), stamp)) {
				++i;
				continue;
			}
			if (!stream.loadPage(*this, r.page, stamp, false, page.read_data)) {
				++i;
				continue;
			}
			stream.dropReadData(r.page);
			m_ready.erase(i);
			++m_loads;
			progress = true;
		}
	}
}

u32 ClusterStreamer::getLargestFreeRange() {
	jobs::MutexGuard lock(m_data_mutex);
	u32 largest = 0;
	for (const DataRange& r : m_data_free) largest = maximum(largest, r.size);
	return largest;
}

void ClusterStreamer::flushPendingData() {
	jobs::MutexGuard lock(m_data_mutex);
	for (const DataRange& r : m_data_pending) releaseDataRange(r.offset, r.size);
	m_data_pending.clear();
}

bool ClusterStreamer::reclaimData(u32 size, u32 alignment) {
	jobs::MutexGuard lock(m_stream_mutex);
	const u32 needed = ((size + DATA_ALIGN - 1) & ~(DATA_ALIGN - 1)) + alignment;
	flushPendingData();
	if (getLargestFreeRange() >= needed) return true;
	if (!m_enabled) return false;
	const u32 stamp = m_renderer.frameNumber() + 1;
	for (u32 i = 0; i < 4096; ++i) {
		if (!evictLeastRecentlyUsedPage(stamp, true)) break;
		if (getLargestFreeRange() >= needed) return true;
	}
	return getLargestFreeRange() >= size;
}

bool ClusterStreamer::evictLeastRecentlyUsedPage(u32 stamp, bool emergency) {
	ClusterStream* best_stream = nullptr;
	u32 best_page = 0, best_stamp = 0, best_priority = 0;
	for (ClusterStream* s : m_streams) {
		for (u32 p = 0; p < (u32)s->pages.size(); ++p) {
			const ClusterStream::Page& page = s->pages[p];
			if (!page.isLoaded() || page.permanent) continue;
			if (!emergency && (page.wanted_frame == stamp || page.loaded_frame + STREAM_MIN_RESIDENCY > stamp)) continue;
			const u32 used_at = maximum(page.last_used, page.loaded_frame);
			if (best_stream && (used_at > best_stamp || (used_at == best_stamp && page.priority >= best_priority))) continue;
			if (!s->canEvict(p)) continue;
			best_stream = s;
			best_page = p;
			best_stamp = used_at;
			best_priority = page.priority;
		}
	}
	if (!best_stream) return false;
	best_stream->evictPage(*this, best_page, emergency);
	++m_evictions;
	return true;
}

bool ClusterStreamer::makeStreamRoom(u32 bytes, u32 stamp) {
	for (;;) {
		u64 used = 0;
		for (const ClusterStream* s : m_streams) used += s->streamed_bytes;
		if (used + bytes <= m_budget) return true;
		if (!evictLeastRecentlyUsedPage(stamp, false)) return false;
	}
}

u64 ClusterStreamer::getDataFreeBytes() {
	jobs::MutexGuard lock(m_data_mutex);
	u64 free = 0;
	for (const DataRange& r : m_data_free) free += r.size;
	for (const DataRange& r : m_data_pending) free += r.size;
	return free;
}

void ClusterStreamer::processFeedback(u32 stamp) {
	const u32 id_count = (u32)m_feedback_local.size() / 4 * 32;
	const u32* feedback = (const u32*)m_feedback_local.begin();

	u32 used_count = 0, wanted_count = 0;
	for (ClusterStream* s : m_streams) {
		if (s->global_base == ClusterStream::NONE || stamp < s->feedback_from) continue;

		const u32 first_id = s->global_base;
		const u32 end_id = minimum(first_id + (u32)s->pages.size(), id_count);
		for (u32 id = first_id; id < end_id;) {
			const u32 word = feedback[id >> 5] >> (id & 31);
			if (!word) {
				id = (id | 31) + 1;
				continue;
			}

			if (word & 1) {
				const u32 page_index = id - first_id;
				ClusterStream::Page& page = s->pages[page_index];
				if (page.isLoaded()) {
					page.last_used = maximum(page.last_used, stamp);
					++used_count;
				}
				else {
					page.requested = stamp;
					++wanted_count;
					if (!page.pending) {
						page.pending = true;
						m_pending.push(StreamRef{s, page_index});
					}
				}
			}
			++id;
		}
	}

	u32 kept = 0;
	for (const StreamRef r : m_pending) {
		ClusterStream::Page& page = r.stream->pages[r.page];
		if (page.isLoaded() || stamp - page.requested > STREAM_REQUEST_FRAMES) {
			page.pending = false;
			continue;
		}
		m_pending[kept] = r;
		++kept;
		page.wanted_frame = stamp;
	}
	m_pending.shrink(kept);

	static const u32 used_counter = profiler::createCounter("Cluster feedback bits on loaded pages", 0);
	static const u32 wanted_counter = profiler::createCounter("Cluster feedback bits on missing pages", 0);
	profiler::pushCounter(used_counter, (float)used_count);
	profiler::pushCounter(wanted_counter, (float)wanted_count);
}

void ClusterStreamer::update() {
	PROFILE_FUNCTION();
	if (!m_enabled) return;
	jobs::MutexGuard lock(m_stream_mutex);
	if (m_streams.empty()) return;
	const u32 frame_number = m_renderer.frameNumber();
	const u32 stamp = frame_number + 1;
	m_loads = 0;
	m_evictions = 0;

	if (!m_feedback_buffer) {
		const u32 size = STREAM_MAX_GROUPS / 8;
		Renderer::MemRef mem = m_renderer.allocate(size);
		memset(mem.data, 0, size);
		m_feedback_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::SHADER_BUFFER, "cluster feedback");
	}

	{
		MutexGuard guard(m_feedback_mutex);
		m_feedback_local.clear();
		if (!m_feedback_incoming.empty()) m_feedback_local.swap(m_feedback_incoming);
	}
	if (!m_feedback_local.empty()) {
		PROFILE_BLOCK("cluster feedback");
		processFeedback(stamp);
	}
	m_feedback_local.clear();

	uploadReadPages(stamp);

	if (m_data_buffer) {
		PROFILE_BLOCK("cluster headroom");
		for (u32 i = 0; i < STREAM_LOADS_PER_FRAME && getDataFreeBytes() < STREAM_HEADROOM; ++i) {
			if (!evictLeastRecentlyUsedPage(stamp, false)) break;
		}
	}

	sortStreamRefs(m_pending);
	for (const StreamRef& w : m_pending) {
		if (m_reads >= STREAM_READS_IN_FLIGHT) break;
		requestPage(*w.stream, w.page, stamp);
	}
	if (m_load_all) {
		for (ClusterStream* s : m_streams) {
			for (u32 p = (u32)s->pages.size(); p > 0 && m_reads < STREAM_READS_IN_FLIGHT;) {
				--p;
				requestPage(*s, p, stamp);
			}
		}
	}

	{
		static const u32 streamed_counter = profiler::createCounter("Cluster streamed (MB)", 0);
		static const u32 pages_counter = profiler::createCounter("Cluster pages streamed in", 0);
		static const u32 loads_counter = profiler::createCounter("Cluster page loads", 0);
		static const u32 evictions_counter = profiler::createCounter("Cluster page evictions", 0);
		static const u32 wanted_counter = profiler::createCounter("Cluster pages wanted", 0);
		static const u32 reads_counter = profiler::createCounter("Cluster page reads in flight", 0);
		static const u32 staged_counter = profiler::createCounter("Cluster pages staged (MB)", 0);
		u64 bytes = 0;
		u32 loaded_pages = 0;
		for (const ClusterStream* s : m_streams) {
			bytes += s->streamed_bytes;
			for (const ClusterStream::Page& page : s->pages) {
				if (page.isLoaded() && !page.permanent) ++loaded_pages;
			}
		}
		profiler::pushCounter(streamed_counter, float(double(bytes) / (1024.0 * 1024.0)));
		profiler::pushCounter(pages_counter, (float)loaded_pages);
		profiler::pushCounter(loads_counter, (float)m_loads);
		profiler::pushCounter(evictions_counter, (float)m_evictions);
		profiler::pushCounter(wanted_counter, (float)m_pending.size());
		profiler::pushCounter(reads_counter, (float)m_reads);
		profiler::pushCounter(staged_counter, float(double(getStagedBytes()) / (1024.0 * 1024.0)));
		if (m_loads || m_evictions) {
			logInfo("Cluster streaming: ", m_loads, " pages loaded, ", m_evictions, " evicted, ", loaded_pages, " pages (", u32(bytes >> 10), " KB) streamed in, ", (u32)m_pending.size(), " wanted");
		}
	}

	if (m_id_ranges.size > 0 && frame_number % STREAM_READBACK_PERIOD == 0) {
		const u32 feedback_bytes = ((m_id_ranges.size + 31) / 32) * 4;
		static u32 zeros[STREAM_MAX_GROUPS / 32];
		m_renderer.getDrawStream().readBuffer(m_feedback_buffer, 0, feedback_bytes, makeDelegate<&ClusterStreamer::onFeedback>(this));
		m_renderer.getDrawStream().update(m_feedback_buffer, zeros, feedback_bytes);
	}
}

} // namespace Lumix
