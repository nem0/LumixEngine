#include "cluster_builder.h"

#include "core/crt.h"
#include "core/profiler.h"

namespace Lumix {

// edges key[i] -> value[i] as a list for each key, without duplicates: the values of the key `k` are list[first[k] .. first[k] + count[k])
static void buildAdjacency(const Array<u32>& key, const Array<u32>& value, u32 key_count, Array<u32>& first, Array<u32>& count, Array<u32>& list) {
	first.resize(key_count + 1);
	count.resize(key_count);
	for (u32& f : first) f = 0;
	for (u32& c : count) c = 0;
	for (u32 k : key) ++first[k + 1];
	for (u32 i = 0; i < key_count; ++i) first[i + 1] += first[i];
	list.resize(key.size());
	for (u32 i = 0; i < (u32)key.size(); ++i) {
		const u32 k = key[i];
		bool exists = false;
		for (u32 j = 0; j < count[k]; ++j) exists = exists || list[first[k] + j] == value[i];
		if (!exists) list[first[k] + count[k]++] = value[i];
	}
}

bool buildMeshPages(const MeshClusterGPU* clusters, u32 cluster_count, const MeshGroupGPU* groups, u32 group_count, MeshPages& out, IAllocator& allocator) {
	PROFILE_FUNCTION();
	out.pages.clear();
	out.parents.clear();
	out.children.clear();
	out.root_bytes = 0;
	if (group_count == 0) return false;
	Array<u32> group_index_bytes(allocator);
	group_index_bytes.resize(group_count);
	for (u32& b : group_index_bytes) b = 0;
	for (u32 i = 0; i < cluster_count; ++i) {
		if (clusters[i].group >= group_count) return false;
		if (clusters[i].refined_group != MeshClusters::NO_GROUP && clusters[i].refined_group >= group_count) return false;
		group_index_bytes[clusters[i].group] += clusterIndexCount(clusters[i]);
	}

	// Permanent groups are never unloaded:
	//  - the groups that have no parent (their clusters have no parent error): nothing coarser covers their part of the mesh, so without them the mesh would have holes
	//    that nothing would ever ask for. They are the coarsest level, or groups that could not be simplified, whatever their size,
	//  - and the coarsest of the others, as long as the permanent groups are not larger than PERMANENT_BYTES (the groups of a mesh are in the order of the levels, the
	//    parents of a group have higher numbers).
	auto payload_bytes = [&](u32 g) -> u64 { return u64(groups[g].stream_size) + u64(group_index_bytes[g]); };
	Array<u8> permanent(allocator);
	permanent.resize(group_count);
	for (u8& p : permanent) p = 0;
	for (u32 i = 0; i < cluster_count; ++i) {
		if (clusters[i].parent_error >= 1e30f) permanent[clusters[i].group] = 1;
	}
	u64 permanent_bytes = 0;
	for (u32 g = 0; g < group_count; ++g) {
		if (permanent[g]) permanent_bytes += payload_bytes(g);
	}
	out.root_bytes = (u32)minimum(permanent_bytes, (u64)0xffFFffFFu);
	for (u32 g = group_count; g > 0;) {
		--g;
		if (permanent[g]) continue;
		const u64 bytes = payload_bytes(g);
		if (permanent_bytes + bytes > ClusterStream::PERMANENT_BYTES) break;
		permanent[g] = 1;
		permanent_bytes += bytes;
	}

	Array<u32> group_page(allocator);
	group_page.resize(group_count);
	u64 page_bytes = 0;
	for (u32 g = 0; g < group_count; ++g) {
		const u64 bytes = payload_bytes(g);
		const u32 is_permanent = permanent[g] ? 1 : 0;
		if (out.pages.empty() || out.pages[out.pages.size() - 1].permanent != is_permanent || (page_bytes > 0 && page_bytes + bytes > ClusterStream::PAGE_BYTES)) {
			out.pages.push(MeshPageFile{g, 0, is_permanent, 0, 0, 0, 0});
			page_bytes = 0;
		}
		out.pages[out.pages.size() - 1].group_count++;
		group_page[g] = (u32)out.pages.size() - 1;
		page_bytes += bytes;
	}

	// The priority of a page: the largest error that it removes, that is the parent error of the clusters of its groups (the error of what is drawn until the page is loaded).
	// Non-negative floats compare as integers. Permanent pages are never requested.
	Array<float> page_error(allocator);
	page_error.resize((u32)out.pages.size());
	for (float& e : page_error) e = 0;
	for (u32 i = 0; i < cluster_count; ++i) {
		const float e = clusters[i].parent_error;
		if (e >= 1e30f || !(e > 0)) continue;
		float& page_max = page_error[group_page[clusters[i].group]];
		page_max = maximum(page_max, e);
	}
	for (u32 p = 0; p < (u32)out.pages.size(); ++p) {
		if (out.pages[p].permanent) {
			out.pages[p].priority = 0xffFFffFFu;
			continue;
		}
		memcpy(&out.pages[p].priority, &page_error[p], sizeof(u32));
	}

	// the edges between the pages (a page is loaded and unloaded as a whole, so the ones inside of a page are not needed)
	Array<u32> child_page(allocator), parent_page(allocator);
	for (u32 i = 0; i < cluster_count; ++i) {
		if (clusters[i].refined_group == MeshClusters::NO_GROUP) continue;
		const u32 c = group_page[clusters[i].refined_group];	// the finer clusters
		const u32 p = group_page[clusters[i].group];			// the group that they were simplified to
		if (c == p) continue;
		if (p < c) return false;	// not in the order of the levels, the streamer needs it to know that there is no cycle
		child_page.push(c);
		parent_page.push(p);
	}
	Array<u32> first(allocator), count(allocator), list(allocator);
	buildAdjacency(child_page, parent_page, (u32)out.pages.size(), first, count, list);
	for (u32 p = 0; p < (u32)out.pages.size(); ++p) {
		out.pages[p].parent_first = (u32)out.parents.size();
		out.pages[p].parent_count = count[p];
		for (u32 j = 0; j < count[p]; ++j) {
			out.parents.push(list[first[p] + j]);
			++out.pages[list[first[p] + j]].child_count;
		}
	}
	// the same edges the other way round
	u32 child_at = 0;
	for (MeshPageFile& page : out.pages) {
		page.child_first = child_at;
		child_at += page.child_count;
		page.child_count = 0;	// counted again while the children are added
	}
	out.children.resize(out.parents.size());
	for (u32 p = 0; p < (u32)out.pages.size(); ++p) {
		for (u32 j = 0; j < out.pages[p].parent_count; ++j) {
			MeshPageFile& parent = out.pages[out.parents[out.pages[p].parent_first + j]];
			out.children[parent.child_first + parent.child_count++] = p;
		}
	}
	return true;
}

} // namespace Lumix
