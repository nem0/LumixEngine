#include "core/arena_allocator.h"
#include "core/associative_array.h"
#include "core/atomic.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/page_allocator.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/simd_math.h"
#include "core/sort.h"
#include "core/stack_array.h"
#include "core/sync.h"
#include "culling_system.h"
#include "draw_stream.h"
#include "draw2d.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "evox_render.h"
#include "font.h"
#include "gpu/gpu.h"
#include "material.h"
#include "mesh_clusters.h"
#include "model.h"
#include "particle_system.h"
#include "pipeline.h"
#include "pose.h"
#include "render_module.h"
#include "renderer.h"
#include "shader.h"
#include "terrain.h"
#include "texture.h"

namespace Lumix {

namespace {

enum class DrawCommandTypes : u8 {
	MESH,
	AUTOINSTANCED,
	SKINNED,
	DECAL,
	CURVE_DECAL,
	PARTICLES,
	MESH_PARTICLES,
	RIBBONS,
	RIBBON_TUBES,
};

// converts float to u32 so it can be used in radix sort
// float float_value = 0;
// u32 sort_key = floatFlip(*(u32*)&float_value);
// http://stereopsis.com/radix.html
static LUMIX_FORCE_INLINE u32 floatFlip(u32 float_bits_value) {
	u32 mask = -i32(float_bits_value >> 31) | 0x80000000;
	return float_bits_value ^ mask;
}

// sort key:
// bucket 64-56
// instanced_flag 55
// depth bits 31 - 0; if bucket is depth sorted, must be not instanced
// mesh key 31 - 8; if bucket is not depth sorted and not instanced
// instancer 31 - 16; if instanced
// instance group 15 - 0; if instanced

static constexpr u64 SORT_KEY_BUCKET_SHIFT = 56;
static constexpr u64 SORT_KEY_INSTANCED_FLAG = (u64)1 << 55;

static constexpr u64 SORT_VALUE_INSTANCER_SHIFT = 16;
static constexpr u64 SORT_VALUE_EMITTER_SHIFT = 40;
static constexpr u32 SORT_VALUE_TYPE_MASK = (1 << 5) - 1;
static constexpr u64 SORT_VALUE_MESH_IDX_SHIFT = 40;
static constexpr u64 SORT_VALUE_TYPE_SHIFT = 32;

enum class SortKey : u64;
enum class SortValue : u64;

// sort key makers
LUMIX_FORCE_INLINE SortKey makeDecalSortKey(const Material* material, u8 bucket) {
	return SortKey(material->getSortKey() | ((u64)bucket << SORT_KEY_BUCKET_SHIFT));
}

LUMIX_FORCE_INLINE SortKey makeParticleSortKey(const Material* material, u8 bucket) {
	return SortKey(material->getSortKey() | ((u64)bucket << SORT_KEY_BUCKET_SHIFT));
}

LUMIX_FORCE_INLINE SortKey makeMeshSortKey(const MeshMaterial& mesh_math, u8 bucket) {
 	return SortKey(mesh_math.sort_key | ((u64)bucket << SORT_KEY_BUCKET_SHIFT));
}

LUMIX_FORCE_INLINE SortKey makeDepthSortKey(float depth_squared, u8 bucket) {
	const u32 depth_bits = floatFlip(*(u32*)&depth_squared);
	return SortKey(depth_bits | ((u64)bucket << SORT_KEY_BUCKET_SHIFT));
}

LUMIX_FORCE_INLINE SortKey makeAutoInstancedSortKey(i32 instancer_index, u8 bucket) {
	return SortKey(instancer_index | SORT_KEY_INSTANCED_FLAG | ((u64)bucket << SORT_KEY_BUCKET_SHIFT));
}

// sort value makers
LUMIX_FORCE_INLINE SortValue makeParticleSortValue(EntityPtr entity, u32 emitter_idx) {
	const u64 type_mask = (u64)DrawCommandTypes::PARTICLES << SORT_VALUE_TYPE_SHIFT;
	return SortValue(entity.index | type_mask | ((u64)emitter_idx << SORT_VALUE_EMITTER_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeMeshParticleSortValue(EntityPtr entity, u32 emitter_idx) {
	const u64 type_mask = (u64)DrawCommandTypes::MESH_PARTICLES << SORT_VALUE_TYPE_SHIFT;
	return SortValue(entity.index | type_mask | ((u64)emitter_idx << SORT_VALUE_EMITTER_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeRibbonsSortValue(EntityPtr entity, u32 emitter_idx) {
	const u64 type_mask = (u64)DrawCommandTypes::RIBBONS << SORT_VALUE_TYPE_SHIFT;
	return SortValue(entity.index | type_mask | ((u64)emitter_idx << SORT_VALUE_EMITTER_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeRibbonTubesSortValue(EntityPtr entity, u32 emitter_idx) {
	const u64 type_mask = (u64)DrawCommandTypes::RIBBON_TUBES << SORT_VALUE_TYPE_SHIFT;
	return SortValue(entity.index | type_mask | ((u64)emitter_idx << SORT_VALUE_EMITTER_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeDecalSortValue(EntityPtr entity) {
	return SortValue(entity.index | (u64(DrawCommandTypes::DECAL) << SORT_VALUE_TYPE_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeCurveDecalSortValue(EntityPtr entity) {
	return SortValue(entity.index | (u64(DrawCommandTypes::CURVE_DECAL) << SORT_VALUE_TYPE_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeSkinnedSortValue(EntityPtr entity, u32 mesh_idx) {
	return SortValue(entity.index | (u64(DrawCommandTypes::SKINNED) << SORT_VALUE_TYPE_SHIFT) | ((u64)mesh_idx << SORT_VALUE_MESH_IDX_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeMeshSortValue(EntityPtr entity, u32 mesh_idx) {
	return SortValue(entity.index | (u64(DrawCommandTypes::MESH) << SORT_VALUE_TYPE_SHIFT) | ((u64)mesh_idx << SORT_VALUE_MESH_IDX_SHIFT));
}

LUMIX_FORCE_INLINE SortValue makeAutoInstancedSortValue(u32 batch_idx, u32 instancer_idx) {
	return SortValue(batch_idx | (instancer_idx << SORT_VALUE_INSTANCER_SHIFT) | (u64(DrawCommandTypes::AUTOINSTANCED) << SORT_VALUE_TYPE_SHIFT));
}


struct Indirect {
	u32 draw_id;
	u32 vertex_count;
	u32 instance_count;
	u32 first_index;
	u32 base_vertex;
	u32 base_instance;
};

struct GlobalState {
	struct SMSlice {
		Matrix3x4 world_to_slice;
		float size;
		float rcp_size;
		float size_world;
		float texel_world;
	};
	SMSlice sm_slices[4];
	Matrix camera_projection;
	Matrix camera_prev_projection;
	Matrix camera_projection_no_jitter;
	Matrix camera_prev_projection_no_jitter;
	Matrix camera_inv_projection;
	Matrix camera_view;
	Matrix camera_inv_view;
	Matrix camera_view_projection;
	Matrix camera_view_projection_no_jitter;
	Matrix camera_prev_view_projection_no_jitter;
	Matrix camera_inv_view_projection;
	Matrix camera_reprojection;
	Vec4 cam_world_pos;
	Vec4 view_dir;
	Vec4 fog_scattering;
	Vec4 to_prev_frame_camera_translation;
	Vec4 light_direction;
	Vec4 light_color;
	IVec2 random_uint2;
	Vec2 random_vec2_normalized;
	IVec2 framebuffer_size;
	Vec2 rcp_framebuffer_size;
	Vec2 pixel_jitter;
	Vec2 prev_pixel_jitter;
	float fog_enabled;
	float fog_top;
	float light_intensity;
	float light_indirect_intensity;
	float time;
	float frame_time_delta;
	float shadow_cam_depth_range;
	float shadow_cam_rcp_depth_range;
	u32 frame_idx;
	gpu::BindlessHandle shadowmap_bindless; 
	gpu::BindlessHandle shadow_atlas_bindless;
	gpu::BindlessHandle reflection_probes_bindless;
};

struct ShadowAtlas {
	static constexpr u32 SIZE = 2048;
	static constexpr u32 SLOTS = 1 + 4 + 32; // sum of group sizes, see add()

	ShadowAtlas(IAllocator& allocator)
		: map(allocator)
	{
		for (EntityPtr& e : inv_map) e = INVALID_ENTITY;
	}

	void clear() {
		for (EntityPtr& e : inv_map) e = INVALID_ENTITY;
		map.clear();
	}

	// must match getShadowAtlasResolution in shader
	static Vec4 getUV(u32 idx) {
		switch(getGroup(idx)) {
			case 0: return Vec4(0, 0, 0.5f, 0.5f);
			case 1: return Vec4(
				0.5f + ((idx - 1) % 2) * 0.25f,
				((idx - 1) / 2) * 0.25f,
				0.25f,
				0.25f
			);
			case 2: return Vec4(
				((idx - 5) % 8) * 0.125f,
				0.5f + ((idx - 5) / 8) * 0.125f,
				0.125f,
				0.125f
			);
		}
		ASSERT(false);
		return Vec4(-1);
	}

	static u32 getGroup(u32 idx) {
		if (idx < 1) return 0;
		if (idx < 5) return 1;
		return 2;
	}

	u32 add(u32 group, EntityRef e) {
		ASSERT(group < 3);
		u32 group_starts[] = { 0, 1, 5 };
		u32 group_sizes[] = { 1, 4, 32 };

		for (u32 i = group_starts[group], c = group_starts[group] + group_sizes[group]; i < c; ++i) {
			if (!inv_map[i].isValid()) {
				map.insert(e, i);
				inv_map[i] = e;
				return i;
			}
		}
		ASSERT(false);
		return -1;
	}

	void remove(EntityRef e) {
		auto iter = map.find(e);
		u32 idx = iter.value();
		map.erase(iter);
		inv_map[idx] = INVALID_ENTITY;
	}

	gpu::TextureHandle texture = gpu::INVALID_TEXTURE;
	HashMap<EntityRef, u32> map;
	EntityPtr inv_map[64];
};

static const float SHADOW_CAM_FAR = 500.0f;

} // anonymous namespace

void bindPulledBuffers(DrawStream& stream, UniformPool& pool, gpu::BufferHandle vertices, gpu::BufferHandle instances, u32 instances_offset) {
	const Mesh::VertexBufferBinding binding = {
		gpu::getBindlessHandle(vertices).value,
		instances ? gpu::getBindlessHandle(instances).value : 0,
		instances_offset,
		0
	};
	const TransientSlice slice = alloc(pool, &binding, sizeof(binding));
	stream.bindUniformBuffer(UniformBuffer::VERTEX_BUFFER, slice.buffer, slice.offset, slice.size);
}

// use to wait for all jobs in the group to finish
struct JobsGroup {
	AtomicI32 counter = 0;
	jobs::Signal signal;

	// beginJob can only be called between begin()/end() pair
	void begin() {
		ASSERT(counter == 0);
		counter = 1;
		jobs::turnRed(&signal);
	}

	void end() {
		endJob();
	}

	void beginJob() {
		counter.inc();
	}

	void endJob() {
		if (counter.dec() == 1) {
			jobs::turnGreen(&signal);
		} 
	}

	// wait until all endJobs are called
	void wait() {
		jobs::wait(&signal);
	}
};

struct PipelineImpl final : Pipeline {
	struct Bucket {
		Bucket(Renderer& renderer) 
			: stream(renderer)
		{
			for (DrawStream*& s : substreams) {
				s = &stream.createSubstream();
			}
		}

		u8 layer;
		char layer_name[32];
		BucketDesc::Sort sort = BucketDesc::DEFAULT;
		u32 define_mask = 0;
		gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FN_GREATER;
		DrawStream stream;
		DrawStream* substreams[32];
	};

	struct Sorter {
		struct alignas(4096) Page {
			struct Header {
				Page* next = nullptr;
				u32 count = 0;
			} header;

			static constexpr u32 MAX_COUNT = (PageAllocator::PAGE_SIZE - sizeof(Header)) / sizeof(u64) / 2;
			SortKey keys[MAX_COUNT];
			SortValue values[MAX_COUNT];
		};

		static_assert(sizeof(Page) == PageAllocator::PAGE_SIZE);

		struct Inserter {
			Inserter(Sorter& sorter) 
				: sorter(sorter)
			{
				first_page = last_page = getNewPage();
			}

			Page* getNewPage() {
				void* mem = sorter.page_allocator.allocate();
				return new (NewPlaceholder(), mem) Page;
			}

			void push(SortKey key, SortValue value) {
				if (last_page->header.count == lengthOf(last_page->keys)) {
					Page* p = getNewPage();
					last_page->header.next = p;
					last_page = p;
				}
				const u32 c = last_page->header.count;
				last_page->keys[c] = key;
				last_page->values[c] = value;
				last_page->header.count = c + 1;
			}

			~Inserter() {
				jobs::MutexGuard guard(sorter.mutex);
				if (!sorter.first_page) {
					sorter.first_page = first_page;
					sorter.last_page = last_page;
					return;
				}

				sorter.last_page->header.next = first_page;
				sorter.last_page = last_page;
			}

			Page* first_page = nullptr;
			Page* last_page = nullptr;
			Sorter& sorter;
		};

		Sorter(IAllocator& allocator, PageAllocator& page_allocator)
			: allocator(allocator)
			, page_allocator(page_allocator)
			, keys(allocator)
			, values(allocator)
		{}

		Sorter(Sorter&& rhs)
			: allocator(rhs.allocator)
			, page_allocator(rhs.page_allocator)
			, first_page(rhs.first_page)
			, last_page(rhs.last_page)
			, keys(rhs.keys.move())
			, values(rhs.values.move())
		{
			// we can't move sorters after inserters have been created, since it holds reference to sorter
			// this check does not cover all cases, but it's better than nothing
			ASSERT(first_page == nullptr);
		}

		~Sorter() {
			Page* p = first_page;
			while (p) {
				Page* n = p->header.next;
				page_allocator.deallocate(p);
				p = n;
			}
		}

		void operator=(Sorter&&) = delete;

		void pack() {
			u32 count = 0;
			Page* p = first_page;
			while (p) {
				count += p->header.count;
				p = p->header.next;
			}

			keys.resize(count);
			values.resize(count);

			p = first_page;
			u32 offset =0 ;
			while (p) {
				if (p->header.count) {
					memcpy(&keys[offset], p->keys, sizeof(p->keys[0]) * p->header.count);
					memcpy(&values[offset], p->values, sizeof(p->values[0]) * p->header.count);
					offset += p->header.count;
				}
				p = p->header.next;
			}

			p = first_page;
			while (p) {
				Page* n = p->header.next;
				page_allocator.deallocate(p);
				p = n;
			}
			first_page = last_page = nullptr;
		}

		IAllocator& allocator;
		PageAllocator& page_allocator;
		Page* first_page = nullptr;
		Page* last_page = nullptr;
		jobs::Mutex mutex;
		Array<u64> keys;
		Array<u64> values;
	};

	// group the instances so the whole group is just one element when sorting
	struct AutoInstancer {
		struct alignas(4096) Page {
			struct Header {
				Page* next = nullptr;;
				u32 count = 0;
			};

			struct Group {
				u64 renderables[14];
				Group* next = nullptr;
				u32 count = 0;
				u32 offset = 0;
			};

			Header header;
			Group groups[(PageAllocator::PAGE_SIZE - sizeof(Header)) / sizeof(Group)];
		};
		
		static_assert(sizeof(Page) == PageAllocator::PAGE_SIZE);

		AutoInstancer(ArenaAllocator& allocator, PageAllocator& page_allocator)
			: instances(allocator)
			, page_allocator(page_allocator)
		{
			last_page = first_page = getNewPage();
		}

		AutoInstancer(AutoInstancer&& rhs)
			: instances(rhs.instances.move())
			, last_page(rhs.last_page)
			, first_page(rhs.first_page)
			, page_allocator(rhs.page_allocator)
		{
			ASSERT(rhs.first_page == rhs.last_page);
			rhs.last_page = rhs.first_page = nullptr;
		}

		void operator=(AutoInstancer&&) = delete;

		void init(u32 count) {
			instances.resize(count);
			memset(instances.begin(), 0, instances.byte_size());
		}

		~AutoInstancer() {
			Page* p = first_page;
			while (p) {
				Page* next = p->header.next;
				page_allocator.deallocate(p);
				p = next;
			}
		}

		void add(u32 sort_key, u64 renderable) {
			Page::Group* g = instances[sort_key].end;
			if (!g || g->count == lengthOf(g->renderables)) {
				Page::Group* n = getNewGroup();
				if (g) {
					n->offset = g->offset + g->count;
					g->next = n;
				}
				else {
					ASSERT(!instances[sort_key].begin);
					instances[sort_key].begin = n;
				}
				g = n;
				instances[sort_key].end = g;
			}

			g->renderables[g->count] = renderable;
			++g->count;
		}

		Page* getNewPage() {
			void* mem = page_allocator.allocate();
			Page* p = new (NewPlaceholder(), mem) Page;
			return p;
		}

		Page::Group* getNewGroup() {
			if (last_page->header.count == lengthOf(last_page->groups)) {
				Page* p = getNewPage();
				last_page->header.next = p;
				last_page = p;
			}
			Page::Group* g = &last_page->groups[last_page->header.count];
			++last_page->header.count;
			return g;
		}

		struct Instances {
			Page::Group* begin;
			Page::Group* end;
			TransientSlice slice;
		};

		Array<Instances> instances;
		Page* last_page = nullptr;
		Page* first_page = nullptr;
		PageAllocator& page_allocator;
	};
	
	// What nanite.hlsl needs to select the clusters of a mesh for all the instances it is drawn for in a view (MeshDesc there).
	struct ClusterMeshDesc {
		Vec4 camera_offset;
		gpu::BindlessHandle clusters;
		gpu::BindlessHandle nodes;
		u32 node_count;
		u32 root_count;
		u32 roots_offset;
		u32 tree_depth;
		u32 scratch_offset;
		u32 scratch_stride;
		gpu::BindlessHandle instances;
		u32 instances_offset;
		u32 input_dynamic;
		u32 instance_count;
		gpu::BindlessHandle material_indices;
		u32 material_indices_offset;
		u32 material_index;
		u32 mesh_count;
		u32 mesh_index;
		u32 region_index;
		u32 region_base;
		u32 region_cap;
		u32 cone_culling;
		u32 cluster_count;
		u32 padding[6];
	};
	static_assert(sizeof(ClusterMeshDesc) == 128);

	// A group of the culling of clusters: a mesh (ClusterMeshDesc of the view), its instance and the first of up to 64 clusters (not used by trees).
	struct ClusterWorkItem {
		u32 desc;
		u32 instance;
		u32 first_cluster;
		u32 padding;
	};

	// The uniforms of nanite.hlsl
	struct ClusterViewUniforms {
		Vec4 planes[6];
		Vec4 cull_offset;		// camera of the culling - camera of the drawing, added to the positions that culling tests
		float lod_k;
		u32 is_ortho;
		gpu::RWBindlessHandle out_instances;
		gpu::RWBindlessHandle out_indirect;
		u32 command_offset;
		gpu::BindlessHandle descs;
		u32 descs_offset;
		gpu::BindlessHandle items;
		u32 items_offset;
		u32 item_count;
		gpu::RWBindlessHandle scratch;
		u32 tree_depth;
		u32 clear_first;
		u32 clear_count;
		float attribute_weight;
		u32 padding1;
	};
	static_assert(sizeof(ClusterViewUniforms) % 16 == 0);

	struct View {
		View(ArenaAllocator& allocator, PageAllocator& page_allocator) 
			: sorter(allocator, page_allocator)
			, instancers(allocator)
			, cluster_instances(allocator)
			, cluster_descs(allocator)
			, cluster_flat_items(allocator)
			, cluster_tree_items(allocator)
			, buckets(allocator)
		{}
		~View() {
			ASSERT((ready.state & 1) == 0);
		}

		View(View&& rhs) = delete;
		View(const View& rhs) = delete;
		void operator=(View&&) = delete;

		Array<Bucket> buckets;
		Array<AutoInstancer> instancers;
		Array<Array<EntityRef>> cluster_instances; // per worker, model instances drawn with clusters, see encodeModelInstanceClusters
		// what encodeClusters collects for the meshes of the view and flushClusterCulling dispatches at once
		Array<ClusterMeshDesc> cluster_descs;
		Array<ClusterWorkItem> cluster_flat_items;	// meshes with few clusters, a group tests 64 clusters of an instance
		Array<ClusterWorkItem> cluster_tree_items;	// the other meshes, a group walks the trees of an instance
		u32 cluster_tree_depth = 0;
		Sorter sorter;
		CullResult* renderables = nullptr;
		CameraParams cp;
		u8 layer_to_bucket[255];
		jobs::Signal ready;
	};

	PipelineImpl(Renderer& renderer, PipelineType type, IAllocator& allocator)
		: m_allocator(allocator)
		, m_renderer(renderer)
		, m_type(type)
		, m_module(nullptr)
		, m_draw2d(m_allocator)
		, m_output(INVALID_RENDERBUFFER)
		, m_shadow_atlas(m_allocator)
		, m_textures(m_allocator)
		, m_buffers(m_allocator)
		, m_views(m_allocator)
		, m_render_states(m_allocator)
		, m_base_vertex_decl(gpu::PrimitiveType::TRIANGLES)
		, m_base_line_vertex_decl(gpu::PrimitiveType::LINES)
		, m_decal_decl(gpu::PrimitiveType::TRIANGLES)
		, m_2D_decl(gpu::PrimitiveType::TRIANGLES)
		, m_instance_data(m_allocator)
		, m_material_override_refresh_queue(m_allocator)
	{
		m_viewport.w = m_viewport.h = 800;
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_blit_shader = rm.load<Shader>(Path("engine/shaders/blit.hlsl"));
		m_draw2d_shader = rm.load<Shader>(Path("engine/shaders/draw2d.hlsl"));
		m_downscale_depth_shader = rm.load<Shader>(Path("engine/shaders/downscale_depth.hlsl"));
		m_debug_shape_shader = rm.load<Shader>(Path("engine/shaders/debug_shape.hlsl"));
		m_instancing_shader = rm.load<Shader>(Path("engine/shaders/instancing.hlsl"));
		m_cluster_shader = rm.load<Shader>(Path("engine/shaders/nanite.hlsl"));
		m_flatten_shader = rm.load<Shader>(Path("engine/shaders/flatten_cube.hlsl"));
		
		m_draw2d.clear({1, 1});

		float cube_verts[] = {
			-1, -1, -1,
			1, -1, -1,
			1, -1, 1,
			-1, -1, 1,
			-1, 1, -1,
			1, 1, -1,
			1, 1, 1,
			-1, 1, 1
		};
		const Renderer::MemRef vb_mem = m_renderer.copy(cube_verts, sizeof(cube_verts));
		m_cube_vb = m_renderer.createBuffer(vb_mem, gpu::BufferFlags::IMMUTABLE, "cube");

		u16 cube_indices[] = {
			0, 1, 2,
			0, 2, 3,
			4, 6, 5,
			4, 7, 6,
			0, 4, 5,
			0, 5, 1,
			2, 6, 7,
			2, 7, 3,
			0, 3, 7,
			0, 7, 4,
			1, 6, 2,
			1, 5, 6
		};

		const Renderer::MemRef ib_mem = m_renderer.copy(cube_indices, sizeof(cube_indices));
		m_cube_ib = m_renderer.createBuffer(ib_mem, gpu::BufferFlags::IMMUTABLE, "cube_indices");

		const Renderer::MemRef ind_mem = { 64 * 1024, nullptr, false }; // TODO size
		m_indirect_buffer = m_renderer.createBuffer(ind_mem, gpu::BufferFlags::SHADER_BUFFER, "indirect");
		createClusterBuffers();



		// decals pull vertices of the cube (float3 positions) and their instances, see decal.hlsl
		const AttributeSemantic cube_semantics[] = { AttributeSemantic::POSITION };
		gpu::VertexDecl cube_decl(gpu::PrimitiveType::TRIANGLES);
		cube_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		m_cube_pulling_defines = m_renderer.getSemanticDefines(Span(cube_semantics), cube_decl, 12, true);


		if (m_type == PipelineType::PREVIEW) m_clear_color = { 0.2f, 0.2f, 0.2f };

		m_autoinstanced_define_idx = m_renderer.getShaderDefineIdx("AUTOINSTANCED");
		m_dynamic_define_idx = m_renderer.getShaderDefineIdx("DYNAMIC");
		m_skinned_define_idx = m_renderer.getShaderDefineIdx("SKINNED");
		m_mesh_particle_define_idx = m_renderer.getShaderDefineIdx("MESH_PARTICLE");
	}

	~PipelineImpl()
	{
		for (void* ptr : m_instance_data) m_allocator.deallocate(ptr);
		for (RenderPlugin* plugin : m_renderer.getPlugins()) {
			plugin->pipelineDestroyed(*this);
		}

		DrawStream& stream = m_renderer.getEndFrameDrawStream();
		for (gpu::TextureHandle t : m_textures) stream.destroy(t);
		for (gpu::BufferHandle b : m_buffers) stream.destroy(b);

		m_blit_shader->decRefCount();
		m_draw2d_shader->decRefCount();
		m_downscale_depth_shader->decRefCount();
		m_debug_shape_shader->decRefCount();
		m_instancing_shader->decRefCount();
		m_cluster_shader->decRefCount();
		m_flatten_shader->decRefCount();

		stream.destroy(m_cube_ib);
		stream.destroy(m_cube_vb);
		stream.destroy(m_indirect_buffer);
		if (m_cluster_instances) stream.destroy(m_cluster_instances);
		if (m_cluster_indirect) stream.destroy(m_cluster_indirect);
		if (m_cluster_scratch) stream.destroy(m_cluster_scratch);
		stream.destroy(m_shadow_atlas.texture);
		stream.destroy(m_cluster_buffers.clusters.buffer);
		stream.destroy(m_cluster_buffers.lights.buffer);
		stream.destroy(m_cluster_buffers.maps.buffer);
		stream.destroy(m_cluster_buffers.env_probes.buffer);
		stream.destroy(m_cluster_buffers.refl_probes.buffer);
		
		if (m_blit_screen_program) {
			stream.destroy(m_blit_screen_program);
		}
	}

	const Viewport& getViewport() override {
		return m_viewport;
	}

	void setClearColor(Vec3 color) override { m_clear_color = color; }
	Vec3 getClearColor() const override { return m_clear_color; }
	gpu::BindlessHandle getShadowAtlasBindless() const override { return m_shadow_atlas.texture ? gpu::getBindlessHandle(m_shadow_atlas.texture) : gpu::INVALID_BINDLESS_HANDLE; }

	void setViewport(const Viewport& viewport) override 
	{
		m_viewport = viewport;
		m_display_size.x = m_viewport.w;
		m_display_size.y = m_viewport.h;
		m_viewport.w = i32(m_viewport.w / m_render_to_display_scale);
		m_viewport.h = i32(m_viewport.h / m_render_to_display_scale);
		if (m_first_set_viewport) {
			m_prev_viewport = viewport;
			m_first_set_viewport = false;
		}
	}

	void prepareShadowCameras(GlobalState& global_state)
	{
		for (int slice = 0; slice < 4; ++slice) {
			const int shadowmap_width = 1024;

			const World& world = m_module->getWorld();
			const EntityPtr light = m_module->getActiveEnvironment();
			const Vec4 cascades = light.isValid() ? m_module->getEnvironmentShadowmapCascades((EntityRef)light) : Vec4(3, 10, 60, 150);
			const Matrix light_mtx = light.isValid() ? world.getRelativeMatrix((EntityRef)light, m_viewport.pos) : Matrix::IDENTITY;

			const float camera_height = (float)m_viewport.h;
			const float camera_fov = m_viewport.fov;
			const float camera_ratio = m_viewport.w / camera_height;
			const float split_distances[] = { 0.1f, cascades.x, cascades.y, cascades.z, cascades.w };

			Frustum camera_frustum;
			if (m_viewport.is_ortho) {
				const float ratio = m_viewport.h > 0 ? m_viewport.w / (float)m_viewport.h : 1;
				camera_frustum.computeOrtho(Vec3::ZERO
					, m_viewport.rot * Vec3(0, 0, -1)
					, m_viewport.rot * Vec3(0, 1, 0)
					, m_viewport.ortho_size * ratio
					, m_viewport.ortho_size
					, split_distances[slice]
					, split_distances[slice + 1]);
			}
			else {
				camera_frustum.computePerspective(Vec3::ZERO,
					m_viewport.rot * Vec3(0, 0, -1),
					m_viewport.rot * Vec3(0, 1, 0),
					camera_fov,
					camera_ratio,
					split_distances[slice],
					split_distances[slice + 1]);
			}

			const Sphere frustum_bounding_sphere = camera_frustum.computeBoundingSphere();
			const float bb_size = frustum_bounding_sphere.radius;
			const Vec3 light_forward = light_mtx.getZVector();

			const Vec3 view_dir = m_viewport.rot * Vec3(0, 0, -1);
			const Vec3 xvec = normalize(cross(light_forward, view_dir));
			const Vec3 yvec = normalize(cross(light_forward, xvec));

			Vec2 min = Vec2(FLT_MAX);
			Vec2 max = Vec2(-FLT_MAX);
			for (u32 i = 0; i < 8; ++i) {
				const Vec2 proj = Vec2(dot(xvec, camera_frustum.points[i]), dot(yvec, camera_frustum.points[i]));
				min.x = minimum(min.x, proj.x);
				min.y = minimum(min.y, proj.y);
				max.x = maximum(max.x, proj.x);
				max.y = maximum(max.y, proj.y);
			}

			const float ortho_size = maximum(max.x - min.x, max.y - min.y) * 0.5f;
			Vec3 shadow_cam_pos = xvec * (max.x + min.x) * 0.5f;
			shadow_cam_pos += yvec * (max.y + min.y) * 0.5f;
			shadow_cam_pos -= light_forward * (SHADOW_CAM_FAR - 2 * bb_size);
			Matrix view_matrix;
			view_matrix.lookAt(shadow_cam_pos, shadow_cam_pos + light_forward, yvec);

			const float ymul = gpu::isOriginBottomLeft() ? 0.5f : -0.5f;
			const Matrix bias_matrix(
				Vec4(0.5, 0.0, 0.0, 0.0),
				Vec4(0.0, ymul, 0.0, 0.0),
				Vec4(0.0, 0.0, 1.0, 0.0),
				Vec4(0.5, 0.5, 0.0, 1.0));

			Viewport& vp = m_shadow_camera_viewports[slice];
			vp.is_ortho = true;
			vp.w = shadowmap_width;
			vp.h = shadowmap_width;
			vp.ortho_size = ortho_size;
			vp.pos = m_viewport.pos + shadow_cam_pos;
			vp.rot = view_matrix.getRotation().conjugated();
			vp.near = 0;
			vp.far = SHADOW_CAM_FAR + 2 * bb_size;

			view_matrix = vp.getView(m_viewport.pos);

			const Matrix projection_matrix = vp.getProjectionNoJitter();
			const Matrix m = bias_matrix * projection_matrix * view_matrix;

			global_state.sm_slices[slice].world_to_slice = Matrix4x3(m).transposed();
			global_state.sm_slices[slice].size = shadowmap_width;
			global_state.sm_slices[slice].rcp_size = 1.f / shadowmap_width;
			global_state.sm_slices[slice].size_world = bb_size * 2;
			global_state.sm_slices[slice].texel_world = global_state.sm_slices[slice].size_world * global_state.sm_slices[slice].rcp_size;
			global_state.shadow_cam_depth_range = SHADOW_CAM_FAR;
			global_state.shadow_cam_rcp_depth_range = 1.f / SHADOW_CAM_FAR;

			//findExtraShadowcasterPlanes(light_forward, camera_frustum, &cp.frustum);
		}
	}

	Vec2 getAtlasSize() const {
		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();
		if (!atlas_texture) return {1, 1};
		if (!atlas_texture->isReady()) return {1, 1};
		return {(float)atlas_texture->width, (float)atlas_texture->height};
	}

	bool bakeShadow(const PointLight& light, u32 atlas_idx) {
		PROFILE_FUNCTION();
		if (!m_flatten_shader->isReady()) return false;

		m_renderer.releaseRenderbuffer(m_output);
		UniformPool& uniform_pool = m_renderer.getUniformPool();
		m_output = INVALID_RENDERBUFFER;
		const World& world = m_module->getWorld();
		const Viewport backup_viewport = m_viewport;

		beginBlock("bake_shadow");
		const Vec4 uv = ShadowAtlas::getUV(atlas_idx);
		m_viewport = {};
		m_viewport.is_ortho = false;
		m_viewport.pos = world.getPosition(light.entity);
		m_viewport.near = 0.1f;
		m_viewport.far = light.range;
		m_viewport.w = u32(ShadowAtlas::SIZE * uv.z + 0.5f);
		m_viewport.h = u32(ShadowAtlas::SIZE * uv.w + 0.5f);
		
		const BucketDesc bucket[] = {{ .layer = "default", .define = "DEPTH"}};

		// we treat every light with fov > PI as omnidirecitonal
		if (light.fov > PI) {
			const u32 face_size = m_viewport.w;
			DrawStream& stream = m_renderer.getDrawStream();
			RenderBufferHandle depthbuf = m_renderer.createRenderbuffer({
				.size = {m_viewport.w, m_viewport.h},
				.format = gpu::TextureFormat::D32,
				.debug_name = "bake_shadow_depth",
			});

			const Quat faces_rot[] = {
				Quat(Vec3(0, 1, 0), -PI * 0.5f),
				Quat(Vec3(0, 1, 0), PI * 0.5f),
				Quat(Vec3(1, 0, 0), PI * 0.5f),
				Quat(Vec3(1, 0, 0), -PI * 0.5f),
				Quat(Vec3(0, 1, 0), 0),
				Quat(Vec3(0, 1, 0), PI)
			};
			
			for (u32 i = 0; i < 6; ++i) {
				// render scene
				m_renderer.setRenderTargets({}, depthbuf);
				clear(gpu::ClearFlags::ALL, 0, 0, 0, 1, 0);
				
				m_viewport.fov = PI * 0.5f;
				m_viewport.rot = faces_rot[i];
				CameraParams cp = getMainCamera();
				pass(cp);

				u32 view_idx = cull(cp, bucket);
				renderBucket(view_idx, 0);
				renderTerrains(cp, gpu::StateFlags::NONE, "DEPTH");

				// copy into atlas
				const gpu::TextureHandle src = m_renderer.toTexture(depthbuf);
				const gpu::TextureHandle dst = m_shadow_atlas.texture;
				stream.barrier(dst, gpu::BarrierType::WRITE);

				struct UBData {
					u32 dst_x;
					u32 dst_y;
					u32 w;
					u32 h;
					u32 face;
					gpu::BindlessHandle src;
					gpu::RWBindlessHandle dst;
				} ub = {
					.dst_x = u32(ShadowAtlas::SIZE * uv.x + 0.5f),
					.dst_y = u32(ShadowAtlas::SIZE * uv.y + 0.5f),
					.w = (u32)m_viewport.w,
					.h = (u32)m_viewport.h,
					.face = i,
					.src = toBindless(depthbuf, stream),
					.dst = gpu::getRWBindlessHandle(dst)
				};
				
				const TransientSlice ub_mem = alloc(uniform_pool, &ub, sizeof(ub));
				stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_mem.buffer, ub_mem.offset, ub_mem.size);

				ASSERT(m_viewport.w % 16 == 0);
				ASSERT(m_viewport.h % 16 == 0);
				const gpu::ProgramHandle shader = m_flatten_shader->getProgram(0);
				stream.useProgram(shader);
				stream.dispatch(m_viewport.w / 16, m_viewport.h / 16, 1);
			}
			
			// the flatten dispatches left the atlas in UAV state, it's sampled as SRV by the light pass
			stream.barrier(m_shadow_atlas.texture, gpu::BarrierType::READ);
			m_renderer.releaseRenderbuffer(depthbuf);


			/*const gpu::TextureHandle dst = m_shadow_atlas.texture;
			const u32 x = u32(ShadowAtlas::SIZE * uv.x + 0.5f);
			const u32 y = u32(ShadowAtlas::SIZE * uv.y + 0.5f);
			
			// TODO this should be a single blit from cubemap to 2d texture array in GPU
			for (u32 i = 0; i < 6; ++i) {
				m_renderer.getDrawStream().copy(dst, src, x, y + i * face_size, i);
			}*/
		}
		else {
			m_viewport.fov = light.fov;
			m_viewport.rot = world.getRotation(light.entity);
			RenderBufferHandle depthbuf = m_renderer.createRenderbuffer({
				.size = {m_viewport.w, m_viewport.h},
				.format = gpu::TextureFormat::D32,
				.debug_name = "bake_shadow_depth",

			});
			m_renderer.setRenderTargets({}, depthbuf);
			clear(gpu::ClearFlags::ALL, 0, 0, 0, 1, 0);
			CameraParams cp = getMainCamera();
			pass(cp);

			u32 view_idx = cull(cp, bucket);
			renderBucket(view_idx, 0);
			renderTerrains(cp, gpu::StateFlags::NONE, "DEPTH");
			
			const gpu::TextureHandle src = m_renderer.toTexture(depthbuf);
			const gpu::TextureHandle dst = m_shadow_atlas.texture;
			const u32 x = u32(ShadowAtlas::SIZE * uv.x + 0.5f);
			const u32 y = u32(ShadowAtlas::SIZE * uv.y + 0.5f);
			m_renderer.getDrawStream().copy(dst, src, x, y);
			m_renderer.releaseRenderbuffer(depthbuf);
		}
		m_viewport = backup_viewport;
		endBlock();
		return true;
	}

	void render3DUI(EntityRef e, const Draw2D& drawdata, Vec2 canvas_size, bool orient_to_cam) override {
		Matrix matrix = m_module->getWorld().getRelativeMatrix(e, m_viewport.pos);
		Matrix normalize(
			Vec4(1 / canvas_size.x, 0, 0, 0),
			Vec4(0, -1 / canvas_size.y, 0, 0),
			Vec4(0, 0, 1, 0),
			Vec4(-0.5f, 0.5f, 0, 1)
		);
		if (orient_to_cam) {
			const Transform tr = m_module->getWorld().getTransform(e);
			matrix = m_viewport.rot.toMatrix();
			matrix.setTranslation(Vec3(tr.pos - m_viewport.pos));
			matrix.multiply3x3(tr.scale);
		}
		matrix = m_viewport.getProjectionWithJitter() * m_viewport.getViewRotation() * matrix * normalize;
		renderUIHelper(drawdata, true, matrix, IVec2(m_viewport.w, m_viewport.h));
	}

	static Matrix computeReprojection(const Viewport& current, const Viewport& prev) {
		Matrix translation = Matrix::IDENTITY;
		translation.setTranslation(Vec3(current.pos - prev.pos));
		return prev.getProjectionNoJitter() * prev.getViewRotation() * translation * current.getViewRotation().inverted() * current.getProjectionNoJitter().inverted();
	}

	void clear(gpu::ClearFlags flags, float r, float g, float b, float a, float depth) override {
		const Vec4 color = Vec4(r, g, b, a);
		DrawStream& stream = m_renderer.getDrawStream();
		stream.clear(flags, &color.x, depth);
	}

	u32 cull(const CameraParams& cp, Span<const BucketDesc> buckets) override {
		PROFILE_FUNCTION();

		UniquePtr<View>& view = m_views.emplace();
		ArenaAllocator& allocator = m_renderer.getCurrentFrameAllocator();
		view = UniquePtr<View>::create(allocator, allocator, m_renderer.getEngine().getPageAllocator());
		view->cp = cp;
		memset(view->layer_to_bucket, 0xff, sizeof(view->layer_to_bucket));

		view->buckets.reserve(buckets.length());
		for (const BucketDesc& desc : buckets) {
			Bucket& bucket = view->buckets.emplace(m_renderer);
			bucket.layer = m_renderer.getLayerIdx(desc.layer);
			copyString(Span(bucket.layer_name), desc.layer);

			bucket.sort = desc.sort;
			if (desc.define) {
				bucket.define_mask = 1 << m_renderer.getShaderDefineIdx(desc.define);
			}
			bucket.state = desc.state;
		}

		for (i32 i = 0; i < view->buckets.size(); ++i) {
			Bucket& bucket = view->buckets[i];
			view->layer_to_bucket[bucket.layer] = i;
		}

		if (m_instancing_shader->isReady()) {
			const HashMap<EntityRef, InstancedModel>& ims = m_module->getInstancedModels();
			for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
				if (iter.value().dirty) {
					m_module->initInstancedModelGPUData(iter.key());
				}
			}
		}

		View* view_ptr = view.get();
		jobs::turnRed(&view->ready);

		m_sort_keys_group.beginJob();
		m_renderer.pushJob("prepare view", [this, view_ptr](DrawStream& stream) {
			setupParticles(*view_ptr);
			encodeInstancedModels(stream, *view_ptr);
			encodeProceduralGeometry(*view_ptr);

			updateClusterFreeze(*view_ptr);
			// with the frozen camera the instances of its frustum are culled, as the clusters are
			view_ptr->renderables = m_module->getRenderables(!view_ptr->cp.is_shadow && m_cluster_frozen ? m_cluster_frozen_cp.frustum : view_ptr->cp.frustum);

			if (view_ptr->renderables) {
				if (view_ptr->renderables->header.count != 0 || view_ptr->renderables->header.next) {
					createSortKeys(*view_ptr);
					encodeModelInstanceClusters(stream, *view_ptr);
				}
				view_ptr->renderables->free(m_renderer.getEngine().getPageAllocator());
				view_ptr->renderables = nullptr;
			}
			flushClusterCulling(stream, *view_ptr);
			m_sort_keys_group.endJob();
			view_ptr->sorter.pack();
				
			if (!view_ptr->sorter.keys.empty()) {
				radixSort(view_ptr->sorter.keys.begin(), view_ptr->sorter.values.begin(), view_ptr->sorter.keys.size());
				// Wait for all createSortKeys jobs to finish, ensuring no more pose processing jobs will be created.
				m_sort_keys_group.wait(); 
				// Wait for all pose processing jobs to finish, because we use the pose transient slices in createCommands.
				jobs::wait(&m_poses_done);
				createCommands(*view_ptr);
			}
			
			jobs::turnGreen(&view_ptr->ready);
		});

		return m_views.size() - 1;
	}

	void renderBucket(u32 view_idx, u32 bucket_idx) const override {
		View* view = m_views[view_idx].get();
		m_renderer.pushJob("render bucket", [view, bucket_idx](DrawStream& stream) {
			jobs::wait(&view->ready);
			Bucket& bucket = view->buckets[bucket_idx];
			stream.merge(bucket.stream);
		});
	}

	void pass(const CameraParams& cp) const override {
		PassState pass_state;
		pass_state.view = cp.view;
		pass_state.projection = cp.projection;
		pass_state.inv_projection = cp.projection.inverted();
		pass_state.inv_view = cp.view.fastInverted();
		pass_state.view_projection = cp.projection * cp.view;
		pass_state.inv_view_projection = pass_state.view_projection.inverted();
		pass_state.view_dir = Vec4(cp.view.inverted().transformVector(Vec3(0, 0, -1)), 0);
		pass_state.camera_up = Vec4(cp.view.inverted().transformVector(Vec3(0, 1, 0)), 0);
		toPlanes(cp, Span(pass_state.camera_planes));
		if (cp.is_shadow) {
			pass_state.shadow_to_camera = Vec4(Vec3(m_viewport.pos - cp.pos), 1);
		}

		UniformPool& uniform_pool = m_renderer.getUniformPool();
		DrawStream& stream = m_renderer.getDrawStream();
		const TransientSlice ub = alloc(uniform_pool, &pass_state, sizeof(PassState));
		stream.bindUniformBuffer(UniformBuffer::PASS, ub.buffer, ub.offset, ub.size);
	}

	void drawArray(u32 indices_offset, u32 indices_count, Shader& shader, u32 define_mask = 0, gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FN_GREATER) override {
		PROFILE_FUNCTION();
		if (!shader.isReady()) return;

		DrawStream& stream = m_renderer.getDrawStream();
		const gpu::ProgramHandle program = shader.getProgram(state, gpu::VertexDecl(gpu::PrimitiveType::TRIANGLE_STRIP), define_mask, "");
		stream.useProgram(program);
		stream.bindIndexBuffer(gpu::INVALID_BUFFER);
		stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
		stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
		stream.drawArrays(indices_offset, indices_count);
	}

	void blit(gpu::BindlessHandle src, gpu::RWBindlessHandle dst, IVec2 size, bool flip_x, bool flip_y) override {
		struct {
			Vec4 r_mask = Vec4(1, 0, 0, 0);
			Vec4 g_mask = Vec4(0, 1, 0, 0);
			Vec4 b_mask = Vec4(0, 0, 1, 0);
			Vec4 a_mask = Vec4(0, 0, 0, 1);;
			Vec4 offsets = Vec4(0, 0, 0, 1);
			IVec2 position = IVec2(0, 0);
			IVec2 scale = IVec2(1, 1);
			gpu::BindlessHandle src;
			gpu::RWBindlessHandle dst;
		} udata;
		udata.src = src;
		udata.dst= dst;
		if (flip_x) {
			udata.position.x = size.x - 1;
			udata.scale.x = -1;
		}
		if (flip_y) {
			udata.position.y = size.y - 1;
			udata.scale.y = -1;
		}
		setUniform(udata);
		dispatch(*m_blit_shader, (size.x + 15) / 16, (size.y + 15) / 16, 1);
	}

	void setUniformRaw(Span<const u8> mem, UniformBuffer::Enum bind_point = UniformBuffer::DRAWCALL) override {
		DrawStream& stream = m_renderer.getDrawStream();
		UniformPool& pool = m_renderer.getUniformPool();
		TransientSlice ub = alloc(pool, mem.begin(), mem.length());
		stream.bindUniformBuffer(bind_point, ub.buffer, ub.offset, ub.size);
	}

	InstanceData getData(u32 idx, u32 size, u32 align) override {
		auto iter = m_instance_data.find(idx);
		if (iter.isValid()) return { iter.value(), false };

		void* data = m_allocator.allocate(size, align);
		m_instance_data.insert(idx, data);
		return { data, true };
	}


	void dispatch(Shader& shader, u32 x, u32 y, u32 z, const char* define = nullptr) override {
		if (!shader.isReady()) return;

		gpu::ProgramHandle program = shader.getProgram(define ? 1 << m_renderer.getShaderDefineIdx(define) : 0);
		if (!program) return;

		DrawStream& stream = m_renderer.getDrawStream();
		stream.useProgram(program);
		stream.dispatch(x, y, z);
	}

	CameraParams getShadowCamera(u32 slice) const override {
		const Viewport& vp = m_shadow_camera_viewports[slice];
		CameraParams cp;
		cp.pos = vp.pos;
		cp.frustum = vp.getFrustum();
		cp.lod_multiplier = m_module->getCameraLODMultiplier(vp.fov, vp.is_ortho);
		cp.is_shadow = true;
		cp.view = vp.getView(cp.pos);
		cp.projection = vp.getProjectionNoJitter();
		return cp;
	}

	u32 getBucketCount(u32 view_idx) const override {
		return view_idx < (u32)m_views.size() ? (u32)m_views[view_idx]->buckets.size() : 0;
	}

	CameraParams getMainCamera() const override {
		CameraParams cp;
		cp.pos = m_viewport.pos;
		cp.frustum = m_viewport.getFrustum();
		cp.lod_multiplier = m_module->getCameraLODMultiplier(m_viewport.fov, m_viewport.is_ortho);
		cp.is_shadow = false;
		cp.view = m_viewport.getView(cp.pos);
		cp.projection = m_viewport.getProjectionWithJitter();
		return cp;
	}

	void copy(RenderBufferHandle dst, RenderBufferHandle src, IVec2 size, Vec4 r = Vec4(1, 0, 0, 0), Vec4 g = Vec4(0, 1, 0, 0), Vec4 b = Vec4(0, 0, 1, 0)) override {
		struct {
			Vec4 r_mask;
			Vec4 g_mask;
			Vec4 b_mask;
			Vec4 a_mask;
			Vec4 offsets;
			IVec2 position;
			IVec2 scale;
			gpu::BindlessHandle src;
			gpu::RWBindlessHandle dst;
		} copy_ub = {
			r,
			g,
			b,
			Vec4(0, 0, 0, 1),
			Vec4(0, 0, 0, 1),
			IVec2(0, 0),
			IVec2(1, 1)
		};
		DrawStream& stream = m_renderer.getDrawStream();
		copy_ub.src = toBindless(src, stream);
		copy_ub.dst = toRWBindless(dst, stream);
		setUniform(copy_ub);
		dispatch(*m_blit_shader, (size.x + 15) / 16, (size.y + 15) / 16, 1);
	}

	gpu::BindlessHandle toBindless(RenderBufferHandle rb_idx, DrawStream& stream) override { 
		if (rb_idx == INVALID_RENDERBUFFER) return gpu::INVALID_BINDLESS_HANDLE;
		gpu::TextureHandle tex = m_renderer.toTexture(rb_idx);
		stream.barrier(tex, gpu::BarrierType::READ);
		return gpu::getBindlessHandle(tex);
	}

	RenderBufferHandle getDownscaledDepth(RenderBufferHandle depth_buffer) override {
		if (m_downscaled_depth != INVALID_RENDERBUFFER) return m_downscaled_depth;
		if (!m_downscale_depth_shader->isReady()) return INVALID_RENDERBUFFER;

		m_downscaled_depth = m_renderer.createRenderbuffer({
			.size = {m_viewport.w, m_viewport.h},
			.format = gpu::TextureFormat::R32F,
			.flags = gpu::TextureFlags::COMPUTE_WRITE,
			.debug_name = "downscaled_depth"
		});

		DrawStream& stream = m_renderer.getDrawStream();
		DrawStream& end_frame_stream = m_renderer.getEndFrameDrawStream();

		stream.beginProfileBlock("downscale_depth", 0, false);
		gpu::TextureHandle mip_views[5];
		gpu::TextureHandle tex = m_renderer.toTexture(m_downscaled_depth);
		for (u32 i = 0; i < lengthOf(mip_views); ++i) {
			mip_views[i] = gpu::allocTextureHandle();
			stream.createTextureView(mip_views[i], tex, 0, i); 
			end_frame_stream.destroy(mip_views[i]);
		}

		struct {
			gpu::BindlessHandle input;
			gpu::RWBindlessHandle mip0;
			gpu::RWBindlessHandle mip1;
			gpu::RWBindlessHandle mip2;
			gpu::RWBindlessHandle mip3;
			gpu::RWBindlessHandle mip4;
		} udata = {
			toBindless(depth_buffer, stream),
			gpu::getRWBindlessHandle(mip_views[0]),
			gpu::getRWBindlessHandle(mip_views[1]),
			gpu::getRWBindlessHandle(mip_views[2]),
			gpu::getRWBindlessHandle(mip_views[3]),
			gpu::getRWBindlessHandle(mip_views[4]),
		};

		stream.barrier(m_renderer.toTexture(m_downscaled_depth), gpu::BarrierType::WRITE);

		setUniform(udata);
		dispatch(*m_downscale_depth_shader, (m_viewport.w + 7) / 8, (m_viewport.h + 7) / 8, 1);
		stream.endProfileBlock();

		return m_downscaled_depth;
	}

	gpu::RWBindlessHandle toRWBindless(RenderBufferHandle rb_idx, DrawStream& stream) override { 
		if (rb_idx == INVALID_RENDERBUFFER) return gpu::INVALID_RW_BINDLESS_HANDLE;
		gpu::TextureHandle tex = m_renderer.toTexture(rb_idx);
		stream.barrier(tex, gpu::BarrierType::WRITE);
		return gpu::getRWBindlessHandle(tex);
	}

	void render2DOnly() {
		m_renderer.releaseRenderbuffer(m_output);
		const RenderBufferHandle rb = m_renderer.createRenderbuffer({
			.size = m_display_size,
			.format = gpu::TextureFormat::SRGBA,
			.debug_name = "2D only"
			});
		m_renderer.setRenderTargets(Span(&rb, 1));
		clear(gpu::ClearFlags::ALL, m_clear_color.r, m_clear_color.g, m_clear_color.b, 1, 0);
		render2D(rb);
		m_output = rb;
	}

	void beginFrame3D() override {
		m_sort_keys_group.begin();
		fillClusters(m_renderer.getDrawStream(), getMainCamera());
		m_renderer.releaseRenderbuffer(m_output);
		m_output = INVALID_RENDERBUFFER;
		m_downscaled_depth = INVALID_RENDERBUFFER;
	}

	void bindGlobalState(RenderBufferHandle shadowmap) override {
		DrawStream& stream = m_renderer.getDrawStream();
		m_global_state.shadowmap_bindless = toBindless(shadowmap, stream);
		TransientSlice gsb = alloc(m_renderer.getUniformPool(), &m_global_state, sizeof(GlobalState));
		stream.bindUniformBuffer(UniformBuffer::GLOBAL, gsb.buffer, gsb.offset, sizeof(GlobalState));
	}

	void endFrame3D() override {
		m_sort_keys_group.end();
		m_sort_keys_group.wait();
	}

	// The 3D frame is a script (render.evox, core:render/frame). Without it (e.g. while the scripts are still compiling) only the UI is drawn.
	void renderMain() {
		RenderBufferHandle output;
		if (m_renderer.getRenderScript().renderFrame(output, *this)) {
			m_output = output;
			return;
		}
		render2DOnly();
	}

	void blitOutputToScreen() override {
		PROFILE_FUNCTION();
		DrawStream& stream = m_renderer.getDrawStream();
		stream.beginProfileBlock("blit_to_screen", 0, true);

		if (m_blit_screen_program == gpu::INVALID_PROGRAM) {
			const char* src =
				R"#(cbuffer State : register(b4) {
						uint c_texture;
					};

					struct VSOutput {
						float2 uv : TEXCOORD0;
						float4 position : SV_POSITION;
					};

					float2 toScreenUV(float2 uv) {
						#ifdef _ORIGIN_BOTTOM_LEFT
							return uv;
						#else
							return float2(uv.x, 1 - uv.y);
						#endif
					}

					float4 fullscreenQuad(int vertexID, out float2 screen_uv) {
						screen_uv = float2((vertexID & 1) * 2, vertexID & 2);
						return float4(toScreenUV(screen_uv) * 2 - 1, 0, 1);
					}

					VSOutput mainVS(uint vertex_id : SV_VertexID) {
						VSOutput output;
						output.position = fullscreenQuad(vertex_id, output.uv);
						return output;
					}

					float4 mainPS(VSOutput input) : SV_Target {
						return sampleBindlessLod(LinearSamplerClamp, c_texture, input.uv, 0);
					}
				)#";

			gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
			m_blit_screen_program = gpu::allocProgramHandle();
			stream.createProgram(m_blit_screen_program, gpu::StateFlags::NONE, decl, src, gpu::ShaderType::SURFACE, nullptr, 0, "blit to screen");
		}

		stream.setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
		stream.viewport(0, 0, m_viewport.w, m_viewport.h);
		stream.useProgram(m_blit_screen_program);

		gpu::BindlessHandle texture = gpu::getBindlessHandle(m_renderer.toTexture(m_output));
		UniformPool& uniform_pool = m_renderer.getUniformPool();

		const TransientSlice ub = alloc(uniform_pool, &texture, sizeof(texture));
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
		stream.drawArrays(0, 4);
		stream.endProfileBlock();
	}

	bool render(bool only_2d) override {
		PROFILE_FUNCTION();

		if (!m_module) {
			logError("Pipeline is missing world, render won't run");
			return false;
		}

		if (m_viewport.w <= 0 || m_viewport.h <= 0) {
			if (m_module) {
				m_module->clearDebugLines();
				m_module->clearDebugTriangles();
			}
			m_draw2d.clear(getAtlasSize());
			return false;
		}

		m_module->invalidateStaleMaterialOverrides();

		m_renderer.waitCanSetup();

		m_viewport.pixel_offset = Vec2(0);

		if (m_is_pixel_jitter_enabled) {
			m_viewport.pixel_offset.x = (halton(m_renderer.frameNumber() % 8 + 1, 2) * 2 - 1) / m_viewport.w;
			m_viewport.pixel_offset.y = (halton(m_renderer.frameNumber() % 8 + 1, 3) * 2 - 1) / m_viewport.h;
		}

		const Matrix view = m_viewport.getViewRotation();
		const Matrix prev_view = m_prev_viewport.getViewRotation();
		const Matrix projection = m_viewport.getProjectionWithJitter();
		const Matrix prev_projection = m_prev_viewport.getProjectionWithJitter();
		const Matrix projection_no_jitter = m_viewport.getProjectionNoJitter();
		const Matrix prev_projection_no_jitter = m_prev_viewport.getProjectionNoJitter();
		GlobalState& global_state = m_global_state;
		global_state.pixel_jitter = m_viewport.pixel_offset;
		global_state.prev_pixel_jitter = m_prev_viewport.pixel_offset;
		global_state.camera_projection = projection;
		global_state.camera_prev_projection = prev_projection;
		global_state.camera_projection_no_jitter = projection_no_jitter;
		global_state.camera_prev_projection_no_jitter = prev_projection_no_jitter;
		global_state.camera_inv_projection = projection.inverted();
		global_state.camera_view = view;
		global_state.camera_inv_view = view.fastInverted();
		global_state.camera_view_projection = projection * view;
		global_state.camera_view_projection_no_jitter = projection_no_jitter * view;
		global_state.camera_prev_view_projection_no_jitter = prev_projection_no_jitter * prev_view;
		global_state.camera_inv_view_projection = global_state.camera_view_projection.inverted();
		global_state.time = m_timer.getTimeSinceStart();
		global_state.frame_time_delta = m_timer.getTimeSinceTick();
		global_state.camera_reprojection = computeReprojection(m_viewport, m_prev_viewport);
		m_timer.tick();
		global_state.reflection_probes_bindless = gpu::getBindlessHandle(m_module->getReflectionProbesTexture());
		global_state.shadow_atlas_bindless = m_shadow_atlas.texture ? gpu::getBindlessHandle(m_shadow_atlas.texture) : gpu::INVALID_BINDLESS_HANDLE;
		global_state.frame_idx = m_renderer.frameNumber();
		global_state.random_uint2 = IVec2((i32)rand(), (i32)rand());
		global_state.random_vec2_normalized = Vec2(randFloat(), randFloat());
		global_state.framebuffer_size = IVec2(m_viewport.w, m_viewport.h);
		global_state.rcp_framebuffer_size = Vec2(1.f / m_viewport.w, 1.f / m_viewport.h);
		global_state.cam_world_pos = Vec4(Vec3(m_viewport.pos), 1);
		global_state.view_dir = Vec4(m_viewport.rot * Vec3(0, 0, -1), 0);
		global_state.to_prev_frame_camera_translation = Vec4(Vec3(m_viewport.pos - m_prev_viewport.pos), 1);
		m_prev_viewport = m_viewport;
		m_indirect_buffer_offset = 0;
		m_cluster_slot_offset = 0;
		m_cluster_scratch_offset = 0;
		const u32 previous_regions = (u32)m_cluster_region_offset;
		m_cluster_region_offset = 0;
		const i32 cluster_frame = m_cluster_frame.inc() + 1;
		if (m_cluster_instances) {
			// the previous frame was sampled: its counters are final now, they are read back at once, not one readback per mesh
			if (m_cluster_sampled) pushClusterReadback(previous_regions);
			pushClusterClear();
			m_cluster_sampled = cluster_frame % CLUSTER_STATS_INTERVAL == 0;
			if (m_cluster_sampled) {
				for (ClusterRegion& region : m_cluster_regions) region.used = false;
				pushClusterStats();
			}
		}

		if(m_module) {
			const EntityPtr env_entity = m_module->getActiveEnvironment();
			if(env_entity.isValid()) {
				EntityRef gl = (EntityRef)env_entity;
				const Environment& env = m_module->getEnvironment(gl);
				global_state.light_direction = Vec4(normalize(m_module->getWorld().getRotation(gl).rotate(Vec3(0, 0, -1))), 456); 
				global_state.light_color = Vec4(env.light_color, 456);
				global_state.light_intensity = env.direct_intensity;
				global_state.light_indirect_intensity = env.indirect_intensity * m_indirect_light_multiplier;
				global_state.fog_enabled = env.fog_density > 0;
				global_state.fog_scattering = Vec4(env.fog_scattering * env.fog_density * 0.0001f, 0);
				global_state.fog_top = env.fog_top;
			}
		}

		if (!only_2d) {
			prepareShadowCameras(global_state);
		}

		switch (m_type) {
			case PipelineType::PREVIEW: beginBlock("Preview"); break;
			case PipelineType::SCENE_VIEW: beginBlock("Scene view"); break;
			case PipelineType::GAME_VIEW: beginBlock("Game view"); break;
			case PipelineType::PROBE: beginBlock("Probe"); break;
			case PipelineType::GUI_EDITOR: beginBlock("GUI editor"); break;
		}

		DrawStream& stream = m_renderer.getDrawStream();
		UniformPool& uniform_pool = m_renderer.getUniformPool();
		const TransientSlice global_state_buffer = alloc(uniform_pool, &global_state, sizeof(GlobalState));

		stream.bindUniformBuffer(UniformBuffer::GLOBAL, global_state_buffer.buffer, global_state_buffer.offset, sizeof(GlobalState));
		stream.bindUniformBuffer(UniformBuffer::PASS, gpu::INVALID_BUFFER, 0, 0);
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, gpu::INVALID_BUFFER, 0, 0);
		stream.bindUniformBuffer(UniformBuffer::SHADOW, gpu::INVALID_BUFFER, 0, 0);
		static int tmp[12] = {};
		stream.update(m_renderer.getInstancedMeshesBuffer(), &tmp, sizeof(tmp));

		ASSERT(m_views.empty());
		
		if (only_2d) {
			render2DOnly();
		}
		else {
			renderMain();
		}

		for (RenderPlugin* plugin : m_renderer.getPlugins()) plugin->renderEnd(m_output, *this);

		endBlock();

		m_renderer.waitForCommandSetup();

		if (m_type == PipelineType::GAME_VIEW && !only_2d) m_renderer.saveScreenshotIfRequested(getOutput(), m_display_size);

		m_views.clear();
		refreshMaterialOverrides();

		return true;
	}

	void renderDebugTriangles() {
		const Array<DebugTriangle>& tris = m_module->getDebugTriangles();
		if (tris.empty() || !m_debug_shape_shader->isReady()) return;

		m_renderer.pushJob("debug triangles", [this](DrawStream& stream){
			struct BaseVertex {
				Vec3 pos;
				u32 color;
			};
			const Array<DebugTriangle>& tris = m_module->getDebugTriangles();
			const gpu::StateFlags state = gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::CULL_BACK;
			const gpu::ProgramHandle program = m_debug_shape_shader->getProgram(state, m_base_vertex_decl, 0, "");
			UniformPool& uniform_pool = m_renderer.getUniformPool();
			TransientPool& transient_pool = m_renderer.getTransientPool();
			const TransientSlice vb = alloc(transient_pool, sizeof(BaseVertex) * tris.size() * 3);
			struct {
				Matrix mtx;
				u32 vertices; // bindless
				u32 vertices_offset;
			} ub_data = { Matrix::IDENTITY, gpu::getBindlessHandle(vb.buffer).value, vb.offset };
			const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
			BaseVertex* vertices = (BaseVertex*)vb.ptr;
			for (u32 i = 0, c = tris.size(); i < c; ++i) {
				vertices[3 * i + 0].color = tris[i].color.abgr();
				vertices[3 * i + 0].pos = Vec3(tris[i].p0 - m_viewport.pos);
				vertices[3 * i + 1].color = tris[i].color.abgr();
				vertices[3 * i + 1].pos = Vec3(tris[i].p1 - m_viewport.pos);
				vertices[3 * i + 2].color = tris[i].color.abgr();
				vertices[3 * i + 2].pos = Vec3(tris[i].p2 - m_viewport.pos);
			}
			m_module->clearDebugTriangles();

			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
			stream.useProgram(program);
			stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			stream.drawArrays(0, vb.size / sizeof(BaseVertex));
		});
	}

	void renderDebugLines()	{
		const Array<DebugLine>& lines = m_module->getDebugLines();
		if (lines.empty() || !m_debug_shape_shader->isReady()) return;

		m_renderer.pushJob("debug lines", [this](DrawStream& stream){
			struct BaseVertex {
				Vec3 pos;
				u32 color;
			};
			const Array<DebugLine>& lines = m_module->getDebugLines();
			const gpu::StateFlags state = gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE;
			const gpu::ProgramHandle program = m_debug_shape_shader->getProgram(state, m_base_line_vertex_decl, 0, "");
			UniformPool& uniform_pool = m_renderer.getUniformPool();
			TransientPool& transient_pool = m_renderer.getTransientPool();
			const TransientSlice vb = alloc(transient_pool, sizeof(BaseVertex) * lines.size() * 2);
			struct {
				Matrix mtx;
				u32 vertices; // bindless
				u32 vertices_offset;
			} ub_data = { Matrix::IDENTITY, gpu::getBindlessHandle(vb.buffer).value, vb.offset };
			const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
			BaseVertex* vertices = (BaseVertex*)vb.ptr;
			for (u32 i = 0, c = lines.size(); i < c; ++i) {
				vertices[2 * i + 0].color = lines[i].color.abgr();
				vertices[2 * i + 0].pos = Vec3(lines[i].from - m_viewport.pos);
				vertices[2 * i + 1].color = lines[i].color.abgr();
				vertices[2 * i + 1].pos = Vec3(lines[i].to - m_viewport.pos);
			}
			m_module->clearDebugLines();

			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
			stream.useProgram(program);
			stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			stream.drawArrays(0, vb.size / sizeof(BaseVertex));
		});
	}

	void renderDebugShapes(RenderBufferHandle output, RenderBufferHandle depth) override {
		if (m_debug_shape_shader->isReady() && (!m_module->getDebugTriangles().empty() || !m_module->getDebugLines().empty())) {
			m_renderer.setRenderTargets(Span(&output, 1), depth);
		}
		renderDebugTriangles();
		renderDebugLines();
		//renderDebugPoints();
	}

	void render2D(RenderBufferHandle input) override {
		const IVec2 display_size = getDisplaySize();
		Matrix matrix;
		matrix.setOrtho(0, (float)display_size.x, (float)display_size.y, 0, 0, 1, false);
		m_renderer.setRenderTargets(Span(&input, 1));
		renderUIHelper(m_draw2d, false, matrix, display_size);
		m_draw2d.clear(getAtlasSize());
	}

	void renderUIHelper(const Draw2D& data, bool is_3d, const Matrix& matrix, const IVec2& size) {
		if (!m_draw2d_shader->isReady()) return;
		if (data.getIndices().empty()) return;

		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();

		DrawStream& stream = m_renderer.getDrawStream();
		UniformPool& uniform_pool = m_renderer.getUniformPool();
		TransientPool& transient_pool = m_renderer.getTransientPool();
		
		const TransientSlice idx_buffer_mem = alloc(transient_pool, data.getIndices().byte_size());
		const TransientSlice vtx_buffer_mem = alloc(transient_pool, data.getVertices().byte_size());
		memcpy(idx_buffer_mem.ptr, data.getIndices().begin(), data.getIndices().byte_size());
		memcpy(vtx_buffer_mem.ptr, data.getVertices().begin(), data.getVertices().byte_size());

		gpu::StateFlags state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
		if (is_3d) state = state | gpu::StateFlags::DEPTH_FN_GREATER; 
		const gpu::ProgramHandle program = m_draw2d_shader->getProgram(state, m_2D_decl, 0, "");

		stream.pushDebugGroup("draw2d");
		// vertices are pulled by the vertex shader
		struct {
			Matrix mtx;
			gpu::BindlessHandle texture;
			u32 vertices;
			u32 vertices_offset;
		} ubdata = {
			matrix,
			gpu::BindlessHandle(),
			gpu::getBindlessHandle(vtx_buffer_mem.buffer).value,
			vtx_buffer_mem.offset
		};
		u32 elem_offset = 0;
		stream.useProgram(program);
		stream.bindIndexBuffer(idx_buffer_mem.buffer);

		for (Draw2D::Cmd& cmd : data.getCmds()) {
			if(cmd.clip_size.x < 0) {
				stream.scissor(0, 0, size.x, size.y);
			}
			else {
				const u32 h = u32(clamp(cmd.clip_size.y, 0.f, 65535.f));
				if (gpu::isOriginBottomLeft()) {
					stream.scissor(u32(maximum(cmd.clip_pos.x, 0.0f)),
						size.y - u32(maximum(cmd.clip_pos.y, 0.0f)) - h,
						u32(minimum(cmd.clip_size.x, 65535.0f)),
						u32(minimum(cmd.clip_size.y, 65535.0f)));
				}
				else {
					stream.scissor(u32(maximum(cmd.clip_pos.x, 0.0f)),
						u32(maximum(cmd.clip_pos.y, 0.0f)),
						u32(minimum(cmd.clip_size.x, 65535.0f)),
						u32(minimum(cmd.clip_size.y, 65535.0f)));
				}
			}
			
			gpu::TextureHandle texture_id = atlas_texture->handle;
			if (cmd.texture != gpu::INVALID_TEXTURE) texture_id = cmd.texture;
			if (!texture_id) texture_id = atlas_texture->handle;
			if (cmd.texture != gpu::INVALID_TEXTURE) stream.barrier(texture_id, gpu::BarrierType::READ);

			ubdata.texture = gpu::getBindlessHandle(texture_id);
			setUniform(ubdata);
			stream.drawIndexed(idx_buffer_mem.offset + elem_offset * sizeof(u32), cmd.indices_count, gpu::DataType::U32);

			elem_offset += cmd.indices_count;
		}
		stream.popDebugGroup();
	}

	void setWorld(World* world) override {
		RenderModule* module = world ? (RenderModule*)world->getModule("renderer") : nullptr;
		if (m_module == module) return;
		m_module = module;
		m_shadow_atlas.clear();
	}
	
	Renderer& getRenderer() const override { return m_renderer; }

	RenderModule* getModule() const override { return m_module; }

	void renderTerrains(const CameraParams& cp, gpu::StateFlags state, const char* define) override {
		const u32 define_mask = define ? 1 << m_renderer.getShaderDefineIdx(define) : 0;
		m_renderer.pushJob("terrain", [this, cp, state, define_mask](DrawStream& stream){
			UniformPool& uniform_pool = m_renderer.getUniformPool();
			TransientPool& transient_pool = m_renderer.getTransientPool();
			const HashMap<EntityRef, Terrain*>& terrains = m_module->getTerrains();
			if(terrains.empty()) return;

			World& world = m_module->getWorld();
			gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
			for (const Terrain* terrain : terrains) {
				if (!terrain->m_heightmap) continue;
				if (!terrain->m_heightmap->isReady()) continue;
				if (!terrain->m_material || !terrain->m_material->isReady()) continue;

				const Transform& tr = world.getTransform(terrain->m_entity);
				const Vec3 pos = Vec3(tr.pos- cp.pos);
				Vec3 ref_pos = Vec3(tr.pos - m_viewport.pos);
				const Quat rot = tr.rot;
				const Vec3 scale = terrain->getScale();
				const Vec2 hm_size = terrain->getSize();
				Shader* shader = terrain->m_material->getShader();
				const gpu::ProgramHandle program = shader->getProgram(state | terrain->m_material->m_render_states, decl, define_mask | terrain->m_material->getDefineMask(), "");
				const Material* material = terrain->m_material;
				if (isinf(pos.x) || isinf(pos.y) || isinf(pos.z)) continue;

				struct Quad {
					IVec4 from_to;
					IVec4 from_to_sup;
					Vec4 pos;
					Vec4 lpos;
					Vec4 terrain_scale;
					Vec2 hm_size;
					float cell_size;
					MaterialIndex material_index;
				};

				Quad quad;
				quad.pos = Vec4(pos, 0);
				quad.lpos = Vec4(rot.conjugated().rotate(-pos), 0);
				quad.hm_size = hm_size;
				quad.material_index = material->getIndex();

				ref_pos = rot.conjugated().rotate(-ref_pos);
				IVec4 prev_from_to;
				float s = scale.x / terrain->m_tesselation;
				bool first = true;
				
				stream.useProgram(program);
				// TODO indexed rendering 
				stream.bindIndexBuffer(gpu::INVALID_BUFFER);
				stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

				for (;;) {
					// round 
					IVec2 from = IVec2((ref_pos.xz() + Vec2(0.5f * s)) / float(s)) - IVec2(terrain->m_base_grid_res / 2);
					from.x = from.x & ~1;
					from.y = from.y & ~1;
					IVec2 to = from + IVec2(terrain->m_base_grid_res);

					// clamp
					quad.from_to_sup = IVec4(from, to);
					
					from.x = clamp(from.x, 0, (int)ceil(hm_size.x / s));
					from.y = clamp(from.y, 0, (int)ceil(hm_size.y / s));
					to.x = clamp(to.x, 0, (int)ceil(hm_size.x / s));
					to.y = clamp(to.y, 0, (int)ceil(hm_size.y / s));

					auto draw_rect = [&](const IVec2& subfrom, const IVec2& subto){
						if (subfrom.x >= subto.x || subfrom.y >= subto.y) return;
						quad.from_to = IVec4(subfrom, subto);
						quad.terrain_scale = Vec4(scale, 0);
						quad.cell_size = s;
					
						const TransientSlice ub = alloc(uniform_pool, &quad, sizeof(quad));

						stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream.drawArraysInstanced((subto.x - subfrom.x) * 2 + 2, subto.y - subfrom.y);
					};

					if (first) {
						draw_rect(from, to);
						first = false;
					}
					else {
						draw_rect(from, IVec2(to.x, prev_from_to.y));
						draw_rect(IVec2(from.x, prev_from_to.w), to);
						
						draw_rect(IVec2(prev_from_to.z, prev_from_to.y), IVec2(to.x, prev_from_to.w));
						draw_rect(IVec2(from.x, prev_from_to.y), IVec2(prev_from_to.x, prev_from_to.w));
					}
					
					if (from.x <= 0 && from.y <= 0 && to.x * s >= hm_size.x && to.y * s >= hm_size.y) break;

					s *= 2;
					prev_from_to = IVec4(from / 2, to / 2);
				}
			}
		});
	}
	
	void renderEntities(Span<const EntityRef> entities, gpu::StateFlags state, const char* define) override {
		PROFILE_FUNCTION();
		if (entities.size() == 0) return;
		const u32 define_mask = define ? 1 << m_renderer.getShaderDefineIdx(define) : 0;
		const u32 count = entities.size();
		// the job runs later, so it gets its own copy of the list
		EntityRef* list = (EntityRef*)m_renderer.getCurrentFrameAllocator().allocate(sizeof(EntityRef) * count, alignof(EntityRef));
		for (u32 i = 0; i < count; ++i) list[i] = entities[i];

		m_renderer.pushJob("render entities", [this, list, count, state, define_mask](DrawStream& stream) {
			UniformPool& uniform_pool = m_renderer.getUniformPool();
			TransientPool& transient_pool = m_renderer.getTransientPool();
			const World& world = m_module->getWorld();
			const u32 skinned_define = 1 << m_renderer.getShaderDefineIdx("SKINNED");
			const DVec3 view_pos = m_viewport.pos;
			Array<DualQuat> dq_pose(m_renderer.getAllocator());
			for (u32 ei = 0; ei < count; ++ei) {
				const EntityRef e = list[ei];
				if (!world.hasComponent(e, types::model_instance)) continue;

				const Model* model = m_module->getModelInstanceModel(e);
				if (!model || !model->isReady()) continue;

				const Pose* pose = m_module->lockPose(e);
				for (int i = 0; i <= model->getLODIndices()[0].to; ++i) {
					const Mesh& mesh = model->getMesh(i);
					const MeshMaterial& mesh_mat = model->getMeshMaterial(i);

					Material* material = mesh_mat.material;
					u32 mask = material->getDefineMask() | define_mask;
					const Matrix mtx = world.getRelativeMatrix(e, view_pos);
					dq_pose.clear();
					if (pose && pose->count > 0 && mesh.type == Mesh::SKINNED) {
						mask |= skinned_define;
						dq_pose.reserve(pose->count);
						for (int j = 0, c = pose->count; j < c; ++j) {
							const LocalRigidTransform& inv_bind = model->getInverseBindTransform(j);
							const LocalRigidTransform tmp = {pose->positions[j], pose->rotations[j]};
							dq_pose.push((tmp * inv_bind).toDualQuat());
						}
					}

					if (dq_pose.empty()) {
						struct UBData {
							Matrix mtx;
							MaterialIndex material_index;
						} ub_data = {
							mtx,
							material->getIndex()
						};
						TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
						stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
						gpu::ProgramHandle program = mesh_mat.material->getShader()->getProgram(material->m_render_states | state, pullingDecl(mesh), mask, mesh.pulling_semantics_defines);
						stream.useProgram(program);
					}
					else {
						struct SkinnedInstanceData {
							IVec3 idata;
							Vec3 pos;
							Quat rot;
							Vec3 scale;
							Vec3 prev_pos;
							Quat prev_rot;
							Vec3 prev_scale;
						};

						TransientSlice inst_buf = alloc(transient_pool, sizeof(SkinnedInstanceData));
						TransientSlice bones = alloc(transient_pool, dq_pose.byte_size());
						SkinnedInstanceData* idata = (SkinnedInstanceData*)inst_buf.ptr;

						Transform tr = world.getTransform(e);
						idata->pos = Vec3(tr.pos - view_pos);
						idata->prev_pos = idata->pos;
						idata->rot = tr.rot;
						idata->prev_rot = idata->rot;
						idata->scale = tr.scale;
						idata->prev_scale = idata->scale;
						idata->idata.x = (i32)material->getIndex();
						idata->idata.y = gpu::getBindlessHandle(bones.buffer).value;
						idata->idata.z = bones.offset;

						memcpy(bones.ptr, dq_pose.begin(), dq_pose.byte_size());
						// instance data is pulled by the vertex shader
						bindMeshVertices(stream, mesh, inst_buf);
						gpu::ProgramHandle program = mesh_mat.material->getShader()->getProgram(material->m_render_states | state, pullingDecl(mesh), gpu::VertexDecl(gpu::PrimitiveType::NONE), mask, mesh.pulling_semantics_defines);
						stream.useProgram(program);
					}

					stream.bindIndexBuffer(mesh.index_buffer_handle);
					if (dq_pose.empty()) bindMeshVertices(stream, mesh);
					stream.drawIndexed(0, mesh.indices_count, mesh.index_type);
				}
				m_module->unlockPose(e, false);
			}
		});
	}

	void renderGrass(CameraParams cp, gpu::StateFlags state = gpu::StateFlags::NONE, u32 define_mask = 0) override {
		PROFILE_FUNCTION();
		if (!cp.is_shadow) {
			for (Terrain* terrain : m_module->getTerrains()) {
				const Transform tr = m_module->getWorld().getTransform(terrain->m_entity);
				Transform rel_tr = tr;
				rel_tr.pos = tr.pos - cp.pos;
				terrain->createGrass(Vec2(-(float)rel_tr.pos.x, -(float)rel_tr.pos.z), m_renderer.frameNumber());
			}
		}

		define_mask |= 1 << m_renderer.getShaderDefineIdx("GRASS");

		m_renderer.pushJob("grass", [this, cp, define_mask, state](DrawStream& stream){
			// instances are pulled by vertex shaders
			const gpu::VertexDecl grass_instance_decl(gpu::PrimitiveType::NONE);
			const HashMap<EntityRef, Terrain*>& terrains = m_module->getTerrains();
			const World& world = m_module->getWorld();
			const float global_lod_multiplier = m_renderer.getLODMultiplier();

			u32 quad_count = 0;
			u32 culled_count = 0;
			u32 total_instance_count = 0;

			for (const Terrain* terrain : terrains) {
				const Transform tr = world.getTransform(terrain->m_entity);
				Transform rel_tr = tr;
				rel_tr.pos = tr.pos - cp.pos;
				const Vec3 rel_pos(rel_tr.pos);
				const Vec3 ref_lod_pos = Vec3(m_viewport.pos - tr.pos);
				const Frustum frustum = cp.frustum.getRelative(tr.pos);

				for (const Terrain::GrassType& type : terrain->m_grass_types) {
					if (!type.m_grass_model || !type.m_grass_model->isReady()) continue;

					const i32 to_mesh = type.m_grass_model->getLODIndices()[0].to;
					const HashMap<u64, Terrain::GrassQuad>& quads = type.m_quads;
					if (quads.empty()) continue;

					for (i32 i = 0; i <= to_mesh; ++i) {
						const Mesh& mesh = type.m_grass_model->getMesh(i);
						const MeshMaterial& mesh_mat = type.m_grass_model->getMeshMaterial(i);

						const Material* material = mesh_mat.material;
						Shader* shader = material->getShader();
						ASSERT(shader->isReady());

 
						gpu::ProgramHandle program = shader->getProgram(state | material->m_render_states, pullingDecl(mesh), grass_instance_decl, define_mask | material->getDefineMask(), mesh.pulling_semantics_defines);
						stream.useProgram(program);
						stream.bindIndexBuffer(mesh.index_buffer_handle);
						
						for (const Terrain::GrassQuad& quad : quads) {
							if (quad.instances_count == 0) continue;
							if (!frustum.intersectAABB(quad.aabb)) {
								++culled_count;
								continue;
							}

							const Vec2 quad_size(type.m_spacing * 32);
							const Vec2 quad_center = Vec2(quad.ij) * quad_size + quad_size * 0.5f;
							const float distance = length(quad_center - ref_lod_pos.xz());

							const float half_range = type.m_distance * 0.5f * global_lod_multiplier;
							float count_scale = 1 - clamp(distance - half_range, 0.f, half_range) / half_range; 
							count_scale *= count_scale;
							count_scale *= count_scale;

							const u32 instance_count = u32(quad.instances_count * count_scale);
							if (instance_count == 0) {
								++culled_count;
								continue;
							}

							struct {
								Vec3 rel_pos;
								float dist;
								MaterialIndex material_index;
							} drawcall_data = {
								rel_pos,
								distance,
								material->getIndex()
							};
							UniformPool& uniform_pool = m_renderer.getUniformPool();

							const TransientSlice drawcall_ub = alloc(uniform_pool, &drawcall_data, sizeof(drawcall_data));
							stream.bindUniformBuffer(UniformBuffer::DRAWCALL, drawcall_ub.buffer, drawcall_ub.offset, drawcall_ub.size);
							bindMeshVertices(stream, mesh, quad.instances, 0);
							stream.drawIndexedInstanced(mesh.indices_count, instance_count, mesh.index_type);
							++quad_count;
							total_instance_count += instance_count;
						}
					}
				}
			}
			profiler::pushInt("Quad count", quad_count);
			profiler::pushInt("Culled", culled_count);
			profiler::pushInt("Instances", total_instance_count);

			stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
			stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
		});
	}

	void setupParticles(View& view) {
		PROFILE_FUNCTION();

		if (view.cp.is_shadow) return;

		const auto& particle_systems = m_module->getParticleEmitters();
		if (particle_systems.size() == 0) return;
			
		Sorter::Inserter inserter(view.sorter);
		// TODO culling

		TransientPool& transient_pool = m_renderer.getTransientPool();
		jobs::forEach(particle_systems.capacity(), 1, [&](i32 idx, i32){
			const ParticleSystem* system = particle_systems.getFromIndex(idx);
			if (!system) return;
			
			PROFILE_BLOCK("setup particles");
			for (ParticleSystem::Emitter& emitter : system->getEmitters()) {
				const Material* material = emitter.resource_emitter.material;
				const Model* model = emitter.resource_emitter.model; 

				if (material) {
					const u8 bucket_idx = view.layer_to_bucket[material->getLayer()];
					if (bucket_idx == 0xff) continue;
				}
				else if (model) {
					if (model->getMeshCount() < 1) continue;
					const u8 layer = model->getMeshMaterial(0).material->getLayer();
					const u8 bucket_idx = view.layer_to_bucket[layer];
					if (bucket_idx == 0xff) continue;
				}
				else {
					continue;
				}

				const u32 size = emitter.getParticlesDataSizeBytes();
				if (size == 0) continue;

				emitter.slice = alloc(transient_pool, size);
				emitter.fillInstanceData((float*)emitter.slice.ptr, m_renderer.getEngine().getPageAllocator());
			}
		});

		for (const ParticleSystem& system : particle_systems) {
			for (ParticleSystem::Emitter& emitter : system.getEmitters()) {
				const Material* material = emitter.resource_emitter.material;
				const Model* model = emitter.resource_emitter.model; 

				u8 bucket_idx;
				if (material) {
					bucket_idx = view.layer_to_bucket[material->getLayer()];
				}
				else if (model) {
					if (model->getMeshCount() < 1) continue;
					const u8 layer = model->getMeshMaterial(0).material->getLayer();
					bucket_idx = view.layer_to_bucket[layer];
				}
				else {
					continue;
				}
				if (bucket_idx == 0xff) continue;

				const u32 size = emitter.getParticlesDataSizeBytes();
				if (size == 0) continue;
				
				ASSERT(emitter.particles_count > 0);

				const u32 emitter_idx = u32(&emitter - system.getEmitters().begin());
				
				if (material) {
					if (emitter.resource_emitter.max_ribbons) {
						if (emitter.resource_emitter.tube_segments > 0) {
							SortValue value = makeRibbonTubesSortValue(system.m_entity, emitter_idx);
							SortKey key = makeParticleSortKey(material, bucket_idx);
							inserter.push(key, value);
						}
						else {
							SortValue value = makeRibbonsSortValue(system.m_entity, emitter_idx);
							SortKey key = makeParticleSortKey(material, bucket_idx);
							inserter.push(key, value);
						}
					}
					else {
						SortValue value = makeParticleSortValue(system.m_entity, emitter_idx);
						SortKey key = makeParticleSortKey(material, bucket_idx);
						inserter.push(key, value);
					}
				}
				else if (model) {
					Material* mesh_mat = model->getMeshMaterial(0).material;
					SortValue value = makeMeshParticleSortValue(system.m_entity, emitter_idx);
					SortKey key = makeParticleSortKey(mesh_mat, bucket_idx);
					inserter.push(key, value);
				}
			}
		}
	}

	static void toPlanes(const CameraParams& cp, Span<Vec4> planes) {
		ASSERT(planes.length() >= 6);
		for (int i = 0; i < 6; ++i) {
			planes[i].x = cp.frustum.xs[i];
			planes[i].y = cp.frustum.ys[i];
			planes[i].z = cp.frustum.zs[i];
			planes[i].w = cp.frustum.ds[i];
		}
	}

	static void findExtraShadowcasterPlanes(const Vec3& light_forward
		, const Frustum& camera_frustum
		, ShiftedFrustum* shadow_camera_frustum)
	{
		static const Frustum::Planes planes[] = {
			Frustum::Planes::LEFT, Frustum::Planes::TOP, Frustum::Planes::RIGHT, Frustum::Planes::BOTTOM };
		bool prev_side = dot(light_forward, camera_frustum.getNormal(planes[lengthOf(planes) - 1])) < 0;
		int out_plane = (int)Frustum::Planes::EXTRA0;
		Vec3 camera_frustum_center = camera_frustum.computeBoundingSphere().position;
		for (u32 i = 0; i < lengthOf(planes); ++i)
		{
			bool side = dot(light_forward, camera_frustum.getNormal(planes[i])) < 0;
			if (prev_side != side)
			{
				Vec3 n0 = camera_frustum.getNormal(planes[i]);
				Vec3 n1 = camera_frustum.getNormal(planes[(i + lengthOf(planes) - 1) % lengthOf(planes)]);
				Vec3 line_dir = cross(n1, n0);
				Vec3 n = normalize(cross(light_forward, line_dir));
				if (dot(camera_frustum_center, n) < 0) n = -n;
				shadow_camera_frustum->setPlane((Frustum::Planes)out_plane, n, Vec3::ZERO);
				++out_plane;
				if (out_plane >(int)Frustum::Planes::EXTRA1) break;
			}
			prev_side = side;
		}
	}

	void enablePixelJitter(bool enable) override {
		m_is_pixel_jitter_enabled = enable;
	}

	Matrix getShadowMatrix(const PointLight& light, u32 atlas_idx) {
		Matrix prj;
		prj.setPerspective(light.fov, 1, 0.1f);
		const Quat rot = -m_module->getWorld().getRotation(light.entity);
		
		const float ymul = gpu::isOriginBottomLeft() ? 0.5f : -0.5f;
		const Matrix bias_matrix(
				Vec4(0.5, 0.0, 0.0, 0.0),
				Vec4(0.0, ymul, 0.0, 0.0),
				Vec4(0.0, 0.0, 1.0, 0.0),
				Vec4(0.5, 0.5, 0.0, 1.0));

		const Vec4 uv = ShadowAtlas::getUV(atlas_idx);

		const Matrix to_tile(
				Vec4(uv.z, 0.0, 0.0, 0.0),
				Vec4(0.0, uv.w, 0.0, 0.0),
				Vec4(0.0, 0.0, 1.0, 0.0),
				Vec4(uv.x, uv.y, 0.0, 1.0));

		Matrix view = rot.toMatrix();
		return to_tile * bias_matrix * prj * view;
	}

	struct AtlasSorter {
		void push(u32 light_idx, float priority, EntityRef e) {
			u32 idx = 0;
			while (idx < count) {
				if (lights[idx].priority < priority) break;
				++idx;
			}

			if (idx == lengthOf(lights)) return;

			if (count == lengthOf(lights)) --count;
			
			memmove(&lights[idx + 1], &lights[idx], sizeof(lights[0]) * (count - idx));
			lights[idx].idx = light_idx;
			lights[idx].priority = priority;
			lights[idx].entity = e;
			++count;
		}

		struct Light {
			u32 idx;
			float priority;
			EntityRef entity;
		};
		u32 count = 0;
		Light lights[64];
	};

	void encodeProceduralGeometry(View& view) {
		const World& world = m_module->getWorld();
		const HashMap<EntityRef, ProceduralGeometry>& geometries = m_module->getProceduralGeometries();
		const DVec3 camera_pos = view.cp.pos;
		UniformPool& uniform_pool = m_renderer.getUniformPool();
		for (auto iter = geometries.begin(), end = geometries.end(); iter != end; ++iter) {
			const ProceduralGeometry& pg = iter.value();
			if (!pg.vertex_buffer) continue;
			if (!pg.material || !pg.material->isReady()) continue;

			u8 bucket_idx = view.layer_to_bucket[pg.material->getLayer()];
			if (bucket_idx == 0xff) continue;

			Bucket& bucket = view.buckets[bucket_idx];
			const gpu::StateFlags render_state = bucket.state;
			struct {
				Matrix mtx;
				MaterialIndex material_index;
				u32 vertices; // bindless, vertices are pulled by the vertex shader
			} ub_data = {
				world.getRelativeMatrix(iter.key(), camera_pos),
				pg.material->getIndex(),
				gpu::getBindlessHandle(pg.vertex_buffer).value
			};
			const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));

			bucket.stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
			const gpu::StateFlags state = pg.material->m_render_states | render_state;
			const gpu::ProgramHandle program = pg.material->getShader()->getProgram(state, gpu::VertexDecl(pg.vertex_decl.primitive_type), bucket.define_mask | pg.material->getDefineMask(), "");

			const u32 stride = pg.vertex_decl.getStride();
			
			bucket.stream.useProgram(program);
			bucket.stream.bindIndexBuffer(pg.index_buffer);

			if (pg.index_buffer) {
				const u32 count = (u32)pg.index_data.size() / (pg.index_type == gpu::DataType::U16 ? 2 : 4);
				bucket.stream.drawIndexed(0, count, pg.index_type);
			}
			else {
				const u32 vertex_count = (u32)pg.vertex_data.size() / stride;
				bucket.stream.drawArrays(0, vertex_count);
			}
		}	
	}

	// Counters of cluster rendering, sampled every CLUSTER_STATS_INTERVAL frames and pushed to the profiler.
	// The GPU numbers arrive a few frames late, but all of them from a sampled frame arrive before the next one.
	struct ClusterStats {
		AtomicI32 tested = 0;		// (instance, cluster) pairs the cull shader looked at
		AtomicI32 selected = 0;		// clusters that were drawn
		AtomicI32 dropped = 0;		// selected clusters that did not fit into their region
		AtomicI32 culled = 0;		// clusters rejected because they face away from the camera (the normal cones)
		AtomicI32 reports = 0;
	};

	// What encodeClusters knows about a region (the counter of one mesh draw) in a sampled frame.
	struct ClusterRegion {
		bool used = false;			// the cull shader was dispatched for the region
		u32 cap = 0;				// clusters that fit
		StaticString<64> name;
	};

	// Reads the numbers of clusters the cull shaders wanted to select, to report when that is more than fits into a region.
	// The clusters over the cap are not drawn, which shows as holes in the model.
	// All regions of a sampled frame are read back at once, see pushClusterReadback.
	struct ClusterReadback {
		ClusterReadback(IAllocator& allocator, ClusterStats& stats) : allocator(allocator), stats(stats), regions(allocator) {}

		// the buffer starts with the counters of selected clusters of CLUSTER_REGIONS regions, then there are the counters of culled ones

		void callback(Span<const u8> data) {
			const u32 count = minimum((u32)(data.length() / (3 * sizeof(u32))), (u32)regions.size());
			const u32 stride = (u32)(data.length() / (3 * sizeof(u32)));
			for (u32 i = 0; i < count; ++i) {
				const ClusterRegion& region = regions[i];
				if (!region.used) continue;
				u32 selected;
				memcpy(&selected, data.begin() + i * sizeof(u32), sizeof(selected));
				u32 culled;
				memcpy(&culled, data.begin() + (stride + i) * sizeof(u32), sizeof(culled));
				stats.culled.add((i32)culled);
				u32 visited;
				memcpy(&visited, data.begin() + (2 * stride + i) * sizeof(u32), sizeof(visited));
				stats.tested.add((i32)visited);
				const u32 dropped = selected > region.cap ? selected - region.cap : 0;
				stats.selected.add((i32)(selected - dropped));
				stats.dropped.add((i32)dropped);
				// report the first overflow and then once in a while
				if (dropped && (stats.reports.inc() % 20) == 0) {
					logWarning("Cluster selection overflow of ", region.name, ": ", selected, " clusters selected, but only ", region.cap, " fit. The rest is not drawn.");
				}
			}
			LUMIX_DELETE(allocator, this);
		}

		IAllocator& allocator;
		ClusterStats& stats;
		Array<ClusterRegion> regions;
	};

	void createClusterBuffers() {
		// views of the editor can show many models, previews show one
		m_cluster_slots = m_type == PipelineType::SCENE_VIEW || m_type == PipelineType::GAME_VIEW ? CLUSTER_SLOTS : CLUSTER_SLOTS_SMALL;
		const Renderer::MemRef instances_mem = { m_cluster_slots * CLUSTER_INSTANCE_SIZE, nullptr, false };
		m_cluster_instances = m_renderer.createBuffer(instances_mem, gpu::BufferFlags::SHADER_BUFFER, "cluster instances");
		const Renderer::MemRef cluster_indirect_mem = { CLUSTER_COMMANDS_OFFSET + m_cluster_slots * 24, nullptr, false };
		m_cluster_indirect = m_renderer.createBuffer(cluster_indirect_mem, gpu::BufferFlags::SHADER_BUFFER, "cluster indirect");
		m_cluster_scratch_capacity = m_cluster_slots == CLUSTER_SLOTS ? CLUSTER_SCRATCH : CLUSTER_SCRATCH_SMALL;
		const Renderer::MemRef scratch_mem = { m_cluster_scratch_capacity * sizeof(u32), nullptr, false };
		m_cluster_scratch = m_renderer.createBuffer(scratch_mem, gpu::BufferFlags::SHADER_BUFFER, "cluster scratch");
	}

	// Reads the counters of the `region_count` regions of the previous (sampled) frame with one readback. It is called when a frame is set up,
	// before the jobs of the frame, so the copy is ordered after all the cull and draw commands of the previous frame.
	void pushClusterReadback(u32 region_count) {
		region_count = minimum(region_count, (u32)CLUSTER_REGIONS);
		if (region_count == 0) return;
		ClusterReadback* readback = LUMIX_NEW(m_allocator, ClusterReadback)(m_allocator, m_cluster_stats);
		readback->regions.resize(region_count);
		for (u32 i = 0; i < region_count; ++i) readback->regions[i] = m_cluster_regions[i];
		m_renderer.pushJob("cluster stats", [this, readback](DrawStream& stream) {
			stream.readBuffer(m_cluster_indirect, 0, 3 * CLUSTER_REGIONS * sizeof(u32), makeDelegate<&ClusterReadback::callback>(readback));
		});
	}

	void pushClusterStats() {
		static u32 tested_counter = profiler::createCounter("Nanite nodes and clusters visited", 0);
		static u32 selected_counter = profiler::createCounter("Nanite clusters selected", 0);
		static u32 dropped_counter = profiler::createCounter("Nanite clusters dropped", 0);
		static u32 culled_counter = profiler::createCounter("Nanite clusters culled (back faces)", 0);
		const i32 tested = m_cluster_stats.tested;
		const i32 selected = m_cluster_stats.selected;
		const i32 dropped = m_cluster_stats.dropped;
		const i32 culled = m_cluster_stats.culled;
		m_cluster_stats.tested.subtract(tested);
		m_cluster_stats.selected.subtract(selected);
		m_cluster_stats.dropped.subtract(dropped);
		m_cluster_stats.culled.subtract(culled);
		profiler::pushCounter(tested_counter, (float)tested);
		profiler::pushCounter(selected_counter, (float)selected);
		profiler::pushCounter(dropped_counter, (float)dropped);
		profiler::pushCounter(culled_counter, (float)culled);
	}

	void bindMeshVertices(DrawStream& stream, const Mesh& mesh, const TransientSlice& instances) {
		bindMeshVertices(stream, mesh, instances.buffer, instances.offset);
	}

	void bindMeshVertices(DrawStream& stream, const Mesh& mesh) {
		bindMeshVertices(stream, mesh, gpu::INVALID_BUFFER, 0);
	}

	// Vertices of meshes are pulled by vertex shaders (see surface_base.hlsli), meshes have no per-vertex attributes in the input assembler.
	static gpu::VertexDecl pullingDecl(const Mesh& mesh) { return gpu::VertexDecl(mesh.vertex_decl.primitive_type); }

	// the only per draw data of vertex pulling are buffers
	// `instances` is pulled instance data of the draw (from the offset in bytes), if there is any
	void bindPulledBuffers(DrawStream& stream, gpu::BufferHandle vertices, gpu::BufferHandle instances, u32 instances_offset) {
		Lumix::bindPulledBuffers(stream, m_renderer.getUniformPool(), vertices, instances, instances_offset);
	}

	void bindMeshVertices(DrawStream& stream, const Mesh& mesh, gpu::BufferHandle instances, u32 instances_offset) {
		bindPulledBuffers(stream, mesh.vertex_buffer_handle, instances, instances_offset);
	}

	// Renders an instanced model with continuous, GPU selected LOD.
	// For each mesh, a compute shader (nanite.hlsl) tests all clusters of all instances and writes one indexed
	// draw (and one instance data entry) for each selected cluster. The draws are then executed with a single
	// ExecuteIndirect, using the same vertex buffers and shaders as ordinary instanced meshes.
	void encodeClusterModel(DrawStream& stream, View& view, EntityRef entity, const InstancedModel& im) {
		if (im.instances.empty()) return;
		const Transform origin = m_module->getWorld().getTransform(entity);
		ClusterInstances instances;
		instances.buffer = im.gpu_data;
		instances.count = im.instances.size();
		instances.camera_offset = Vec3(origin.pos - view.cp.pos);
		encodeClusters(stream, view, *im.model, instances);
	}

	struct ClusterInstances {
		gpu::BufferHandle buffer;
		u32 offset = 0;			// in bytes
		u32 count = 0;
		// false: InstanceData of instanced models (32 bytes, rotation xyz and lod, position and uniform scale)
		// true: 96 bytes, the layout of the DYNAMIC variant of surface shaders (with previous frame transform)
		bool dynamic = false;
		// material index of each mesh of each instance, [instance * mesh_count + mesh]. If not set, materials of the model are used.
		gpu::BufferHandle material_indices = gpu::INVALID_BUFFER;
		u32 material_indices_offset = 0;
		Vec3 camera_offset = Vec3(0); // added to positions to make them relative to the camera
	};

	// Selects the clusters of the meshes of a model for the instances of it. It allocates their regions of the output buffers, records the draws, and
	// adds the meshes to the tables of the view, flushClusterCulling dispatches the culling of all of them.
	void encodeClusters(DrawStream& stream, View& view, const Model& model, const ClusterInstances& instances) {
		ASSERT(m_cluster_instances);
		const u32 instance_count = instances.count;
		if (instance_count == 0) return;

		UniformPool& uniform_pool = m_renderer.getUniformPool();
		const bool is_ortho = view.cp.projection.columns[3].w == 1.f;

		// clusters are drawn as dynamic meshes, they have the transform of the previous frame (for motion vectors),
		// scale and material index of each instance
		u32 dynamic_define = 1 << m_dynamic_define_idx;
		// instances of cluster draws are addressed by the draw id of indirect commands, not by SV_InstanceID
		dynamic_define |= 1 << m_renderer.getShaderDefineIdx("INDIRECT_INSTANCE");
		if (m_debug_show == DebugShow::MESH_CLUSTERS) dynamic_define |= 1 << m_renderer.getShaderDefineIdx("CLUSTER_DEBUG");
		if (m_debug_show == DebugShow::MESH_TRIANGLES) dynamic_define |= (1 << m_renderer.getShaderDefineIdx("CLUSTER_DEBUG")) | (1 << m_renderer.getShaderDefineIdx("CLUSTER_DEBUG_TRIANGLES"));

		// instances are pulled by vertex shaders too
		const gpu::VertexDecl no_instance_decl(gpu::PrimitiveType::NONE);

		const i32 end = model.getClusterMeshesEnd();

		// the meshes of the model have consecutive regions (their counters are cleared once in a frame, see pushClusterClear)
		const u32 mesh_count = (u32)(end + 1);
		const u32 first_region = (u32)m_cluster_region_offset.add((i32)mesh_count);
		if (first_region >= CLUSTER_REGIONS) return;

		stream.barrier(instances.buffer, gpu::BarrierType::READ);
		if (instances.material_indices) stream.barrier(instances.material_indices, gpu::BarrierType::READ);

		for (i32 i = 0; i <= end; ++i) {
			const Mesh& mesh = model.getMesh(i);
			const MeshMaterial& mesh_mat = model.getMeshMaterial(i);
			const u8 bucket_idx = view.layer_to_bucket[mesh_mat.material->getLayer()];
			if (bucket_idx == 0xff) continue;

			// a selection of clusters has at most as many clusters as the finest level, that are the first ones
			const u32 total = instance_count * mesh.leaf_cluster_count;
			const u32 cap = minimum(minimum(total, (u32)CLUSTER_MAX_PER_REGION), m_cluster_slots);
			const u32 region = first_region + (u32)i;
			const u32 base = (u32)m_cluster_slot_offset.add(cap);
			if (region >= CLUSTER_REGIONS || base + cap > m_cluster_slots) continue;

			ClusterMeshDesc desc;
			memset(&desc, 0, sizeof(desc));
			desc.camera_offset = Vec4(instances.camera_offset, 1);
			desc.clusters = gpu::getBindlessHandle(mesh.cluster_buffer);
			desc.nodes = gpu::getBindlessHandle(mesh.node_buffer);
			desc.node_count = mesh.node_count;
			desc.root_count = mesh.root_count;
			desc.roots_offset = mesh.node_count * (u32)sizeof(MeshNodeGPU);
			desc.tree_depth = mesh.tree_depth;
			desc.scratch_stride = 2 * mesh.node_count;
			desc.instances = gpu::getBindlessHandle(instances.buffer);
			desc.instances_offset = instances.offset;
			desc.input_dynamic = instances.dynamic ? 1 : 0;
			desc.instance_count = instance_count;
			desc.material_indices = instances.material_indices ? gpu::getBindlessHandle(instances.material_indices) : gpu::BindlessHandle();
			desc.material_indices_offset = instances.material_indices_offset;
			desc.material_index = (u32)mesh_mat.material_index;
			desc.mesh_count = mesh_count;
			desc.mesh_index = (u32)i;
			desc.region_index = region;
			desc.region_base = base;
			desc.region_cap = cap;
			desc.cluster_count = mesh.cluster_count;
			// back facing clusters are culled only if the draw culls back faces too (shadow views can cull front faces or none), and
			// not in orthographic views, where the clusters do not face the position of the camera
			const gpu::StateFlags draw_state = mesh_mat.material->m_render_states | view.buckets[bucket_idx].state;
			const bool culls_back = ((u64)draw_state & (u64)gpu::StateFlags::CULL_BACK) && !((u64)draw_state & (u64)gpu::StateFlags::CULL_FRONT);
			desc.cone_culling = m_cluster_backface_culling && !is_ortho && culls_back ? 1 : 0;

			// Meshes with few clusters are not worth a tree: a group that tests 64 clusters of an instance is cheaper than one that walks the trees. The
			// trees of the instances of a mesh have each their lists of nodes in the scratch buffer, a part of it (two lists with as many entries as
			// the mesh has nodes). Meshes that do not fit into what is left of it are done without the trees.
			bool flat = mesh.cluster_count <= CLUSTER_FLAT_MAX;
			if (!flat) {
				const u64 scratch_needed = u64(instance_count) * desc.scratch_stride;
				const u64 scratch_at = (u64)(u32)m_cluster_scratch_offset.add((i32)minimum(scratch_needed, (u64)m_cluster_scratch_capacity));
				if (scratch_needed <= m_cluster_scratch_capacity && scratch_at + scratch_needed <= m_cluster_scratch_capacity) desc.scratch_offset = (u32)scratch_at;
				else flat = true;
			}
			const u32 desc_index = (u32)view.cluster_descs.size();
			view.cluster_descs.push(desc);
			if (flat) {
				const u32 groups_per_instance = (mesh.cluster_count + CLUSTER_THREADS - 1) / CLUSTER_THREADS;
				for (u32 instance = 0; instance < instance_count; ++instance) {
					for (u32 group = 0; group < groups_per_instance; ++group) view.cluster_flat_items.push({ desc_index, instance, group * CLUSTER_THREADS, 0 });
				}
				if (m_cluster_sampled) m_cluster_stats.tested.add((i32)(instance_count * mesh.cluster_count));
			}
			else {
				for (u32 instance = 0; instance < instance_count; ++instance) view.cluster_tree_items.push({ desc_index, instance, 0, 0 });
				view.cluster_tree_depth = maximum(view.cluster_tree_depth, mesh.tree_depth);
			}

			if (m_cluster_sampled) {
				// the counters are read back in the next frame, see pushClusterReadback
				ClusterRegion& info = m_cluster_regions[region];
				info.used = true;
				info.cap = cap;
				info.name = mesh.name.c_str();
			}

			Bucket& bucket = view.buckets[bucket_idx];
			const Material* material = mesh_mat.material;
			const gpu::StateFlags state = material->m_render_states | bucket.state;
			// vertices are pulled by the vertex shader, there are no per-vertex attributes in the input assembler
			const gpu::VertexDecl pulling_decl(mesh.vertex_decl.primitive_type);
			const gpu::ProgramHandle program = material->getShader()->getProgram(state, pulling_decl, no_instance_decl, bucket.define_mask | dynamic_define | material->getDefineMask(), mesh.pulling_semantics_defines);

			Mesh::VertexBufferBinding vertex_buffer_binding = { gpu::getBindlessHandle(mesh.vertex_buffer_handle).value, gpu::getBindlessHandle(m_cluster_instances).value, 0, 0 };
			const TransientSlice layout_ub = alloc(uniform_pool, &vertex_buffer_binding, sizeof(vertex_buffer_binding));

			bucket.stream.useProgram(program);
			bucket.stream.bindIndexBuffer(mesh.cluster_index_buffer);
			bucket.stream.bindUniformBuffer(UniformBuffer::VERTEX_BUFFER, layout_ub.buffer, layout_ub.offset, layout_ub.size);
			bucket.stream.barrier(m_cluster_instances, gpu::BarrierType::READ);
			bucket.stream.bindIndirectBuffer(m_cluster_indirect);
			bucket.stream.drawIndirectCount(gpu::DataType::U32, CLUSTER_COMMANDS_OFFSET + base * 24, cap, region * sizeof(u32));

			bucket.stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
			bucket.stream.bindIndexBuffer(gpu::INVALID_BUFFER);
		}
	}

	// Remembers the camera of the main view when the freezing of the camera of the clusters is turned on, forgets it when it is turned off.
	void updateClusterFreeze(const View& view) {
		if (view.cp.is_shadow) return;
		if (!m_cluster_freeze_camera) {
			m_cluster_frozen = false;
			return;
		}
		if (m_cluster_frozen) return;
		m_cluster_frozen_cp = view.cp;
		m_cluster_frozen_height = m_viewport.h > 1 ? (float)m_viewport.h : 1.f;
		m_cluster_frozen = true;
	}

	// Dispatches the culling of the clusters of all the meshes that encodeClusters collected for a view: one dispatch for the meshes with few
	// clusters and one for the others, instead of dispatches for each mesh.
	void flushClusterCulling(DrawStream& stream, View& view) {
		if (view.cluster_descs.empty()) return;
		if (!m_cluster_shader->isReady()) {
			view.cluster_descs.clear();
			view.cluster_flat_items.clear();
			view.cluster_tree_items.clear();
			return;
		}

		UniformPool& uniform_pool = m_renderer.getUniformPool();
		TransientPool& transient_pool = m_renderer.getTransientPool();

		// a cluster is accurate enough if its error projected to the screen is below `lod_k * distance`, which is one pixel
		// the camera that chooses the clusters: the one of the view, or the frozen one (the main view only)
		const bool frozen = !view.cp.is_shadow && m_cluster_frozen;
		const CameraParams& cull_cp = frozen ? m_cluster_frozen_cp : view.cp;
		const Matrix& projection = cull_cp.projection;
		const float lod_multiplier = maximum(m_renderer.getLODMultiplier(), 0.01f);
		const float viewport_height = frozen ? m_cluster_frozen_height : (m_viewport.h > 1 ? (float)m_viewport.h : 1.f);
		ClusterViewUniforms ub;
		memset(&ub, 0, sizeof(ub));
		toPlanes(cull_cp, Span(ub.planes));
		// positions of the instances are relative to the camera of the view, the frustum and the distances are the ones of the frozen camera
		ub.cull_offset = frozen ? Vec4(Vec3(view.cp.pos - m_cluster_frozen_cp.pos), 0) : Vec4(0, 0, 0, 0);
		ub.lod_k = 2.f * maximum(m_cluster_error_pixels * (view.cp.is_shadow ? m_cluster_shadow_error_scale : 1.f), 0.01f) / (viewport_height * maximum(fabsf(projection.columns[1].y), 1e-6f) * lod_multiplier);
		ub.is_ortho = projection.columns[3].w == 1.f ? 1 : 0;
		ub.attribute_weight = maximum(m_cluster_attribute_weight, 0.f);
		if (!view.cp.is_shadow) {
			// what the main view uses to choose clusters: the largest error (object space) that is accurate enough at a distance of 100 units, and the height of the view
			static u32 error_counter = profiler::createCounter("Nanite accepted error at 100 units", 0);
			static u32 height_counter = profiler::createCounter("Nanite view height (px)", 0);
			profiler::pushCounter(error_counter, ub.lod_k * 100.f);
			profiler::pushCounter(height_counter, viewport_height);
		}
		ub.out_instances = gpu::getRWBindlessHandle(m_cluster_instances);
		ub.out_indirect = gpu::getRWBindlessHandle(m_cluster_indirect);
		ub.command_offset = CLUSTER_COMMANDS_OFFSET;
		ub.scratch = gpu::getRWBindlessHandle(m_cluster_scratch);

		const TransientSlice descs = alloc(transient_pool, (u32)view.cluster_descs.byte_size());
		memcpy(descs.ptr, view.cluster_descs.begin(), view.cluster_descs.byte_size());
		ub.descs = gpu::getBindlessHandle(descs.buffer);
		ub.descs_offset = descs.offset;

		stream.barrier(m_cluster_indirect, gpu::BarrierType::WRITE);
		stream.barrier(m_cluster_instances, gpu::BarrierType::WRITE);
		stream.barrier(m_cluster_scratch, gpu::BarrierType::WRITE);
		stream.barrier(descs.buffer, gpu::BarrierType::READ);

		auto dispatch_items = [&](const Array<ClusterWorkItem>& items, gpu::ProgramHandle program, u32 tree_depth) {
			if (items.empty()) return;
			const TransientSlice slice = alloc(transient_pool, (u32)items.byte_size());
			memcpy(slice.ptr, items.begin(), items.byte_size());
			stream.barrier(slice.buffer, gpu::BarrierType::READ);
			ub.items = gpu::getBindlessHandle(slice.buffer);
			ub.items_offset = slice.offset;
			ub.item_count = (u32)items.size();
			ub.tree_depth = tree_depth;
			const TransientSlice ub_slice = alloc(uniform_pool, &ub, sizeof(ub));
			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_slice.buffer, ub_slice.offset, sizeof(ub));
			stream.useProgram(program);
			const u32 count = (u32)items.size();
			stream.dispatch(minimum(count, 65535u), (count + 65534) / 65535, 1);
		};
		dispatch_items(view.cluster_flat_items, m_cluster_shader->getProgram(1 << m_renderer.getShaderDefineIdx("FLAT")), 0);
		dispatch_items(view.cluster_tree_items, m_cluster_shader->getProgram(0), view.cluster_tree_depth);

		stream.memoryBarrier(m_cluster_indirect);
		stream.memoryBarrier(m_cluster_instances);

		view.cluster_descs.clear();
		view.cluster_flat_items.clear();
		view.cluster_tree_items.clear();
		view.cluster_tree_depth = 0;
	}

	// Clears the counters of all the regions at the start of a frame, with one dispatch. It is a job, like the readback of the counters of the
	// previous frame (pushClusterReadback), that has to be pushed before it.
	void pushClusterClear() {
		m_renderer.pushJob("clear clusters", [this](DrawStream& stream) {
			if (!m_cluster_shader->isReady() || !m_cluster_indirect) return;
			ClusterViewUniforms ub;
			memset(&ub, 0, sizeof(ub));
			ub.out_indirect = gpu::getRWBindlessHandle(m_cluster_indirect);
			ub.command_offset = CLUSTER_COMMANDS_OFFSET;
			ub.clear_first = 0;
			ub.clear_count = CLUSTER_REGIONS;
			const TransientSlice ub_slice = alloc(m_renderer.getUniformPool(), &ub, sizeof(ub));
			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_slice.buffer, ub_slice.offset, sizeof(ub));
			stream.barrier(m_cluster_indirect, gpu::BarrierType::WRITE);
			stream.useProgram(m_cluster_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS0")));
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(m_cluster_indirect);
		});
	}

	// Draws model instances collected by createSortKeys: all instances of a model are drawn together, as if they
	// were an instanced model, with instance data written to a transient buffer.
	void encodeModelInstanceClusters(DrawStream& stream, View& view) {
		if (!m_cluster_shader->isReady()) return;

		const ModelInstance* LUMIX_RESTRICT model_instances = m_module->getModelInstances().begin();
		const Transform* LUMIX_RESTRICT transforms = m_module->getWorld().getTransforms();
		const DVec3 camera_pos = view.cp.pos;
		const bool is_shadow = view.cp.is_shadow;

		// instances of the same model are next to each other
		Array<Model*> models(m_allocator);
		Array<u32> counts(m_allocator);
		for (const Array<EntityRef>& list : view.cluster_instances) {
			for (const EntityRef e : list) {
				Model* model = model_instances[e.index].model;
				i32 idx = models.indexOf(model);
				if (idx < 0) {
					idx = models.size();
					models.push(model);
					counts.push(0);
				}
				++counts[idx];
			}
		}
		if (models.empty()) return;

		TransientPool& transient_pool = m_renderer.getTransientPool();
		Array<ClusterInstances> instances(m_allocator);
		Array<TransientSlice> transform_slices(m_allocator);
		Array<TransientSlice> material_slices(m_allocator);
		Array<u32> written(m_allocator);
		for (u32 i = 0; i < (u32)models.size(); ++i) {
			const u32 mesh_count = models[i]->getClusterMeshesEnd() + 1;
			transform_slices.push(alloc(transient_pool, counts[i] * CLUSTER_INSTANCE_SIZE));
			material_slices.push(alloc(transient_pool, counts[i] * mesh_count * sizeof(u32)));
			written.push(0);

			ClusterInstances& in = instances.emplace();
			in.buffer = transform_slices[i].buffer;
			in.offset = transform_slices[i].offset;
			in.count = counts[i];
			in.dynamic = true;
			in.material_indices = material_slices[i].buffer;
			in.material_indices_offset = material_slices[i].offset;
		}

		for (const Array<EntityRef>& list : view.cluster_instances) {
			for (const EntityRef e : list) {
				const ModelInstance& mi = model_instances[e.index];
				const i32 idx = models.indexOf(mi.model);
				const Transform& tr = transforms[e.index];
				// instances that did not move, and all instances in shadows, have no motion
				const bool moved = !is_shadow && (mi.flags & ModelInstance::MOVED);
				const Transform& prev_tr = moved ? mi.prev_frame_transform : tr;
				const Vec3 pos = Vec3(tr.pos - camera_pos);
				const Vec3 prev_pos = Vec3(prev_tr.pos - camera_pos);

				// the layout of DYNAMIC variant of surface shaders, the lods are the indices of clusters, see nanite.hlsl
				const float data[24] = {
					tr.rot.x, tr.rot.y, tr.rot.z, tr.rot.w,
					pos.x, pos.y, pos.z, 0.f,
					tr.scale.x, tr.scale.y, tr.scale.z, 0.f,
					prev_tr.rot.x, prev_tr.rot.y, prev_tr.rot.z, prev_tr.rot.w,
					prev_pos.x, prev_pos.y, prev_pos.z, 0.f,
					prev_tr.scale.x, prev_tr.scale.y, prev_tr.scale.z, 0.f
				};
				static_assert(sizeof(data) == CLUSTER_INSTANCE_SIZE);
				memcpy(transform_slices[idx].ptr + written[idx] * sizeof(data), data, sizeof(data));

				// instances can override materials, but the program of a draw is the one of the model, so only
				// materials of the same shader can be used
				const Model& model = *mi.model;
				const u32 mesh_count = model.getClusterMeshesEnd() + 1;
				u32* material_indices = (u32*)material_slices[idx].ptr + written[idx] * mesh_count;
				for (u32 i = 0; i < mesh_count; ++i) {
					const MeshMaterial& model_mat = model.getMeshMaterial(i);
					const bool can_override = i < mi.mesh_materials.length() && mi.mesh_materials[i].material->getShader() == model_mat.material->getShader();
					material_indices[i] = (u32)(can_override ? mi.mesh_materials[i].material_index : model_mat.material_index);
				}
				++written[idx];
			}
		}

		for (u32 i = 0; i < (u32)models.size(); ++i) {
			encodeClusters(stream, view, *models[i], instances[i]);
		}
	}

	void encodeInstancedModels(DrawStream& stream, View& view) {
		PROFILE_FUNCTION();
		if (!m_instancing_shader->isReady()) return;

		const float global_lod_multiplier = m_renderer.getLODMultiplier();
		const World& world = m_module->getWorld();
		const HashMap<EntityRef, InstancedModel>& ims = m_module->getInstancedModels();
		if (ims.empty()) return;
		
		struct UBValues {
			Vec4 camera_offset;
			Vec4 lod_distances;
			IVec4 lod_indices;
			u32 indirect_offset;
			float radius;
			u32 batch_size;
			float padding;
			Vec4 camera_planes[6];
			IVec4 indices_count[32];
			gpu::RWBindlessHandle culled_buffer;
			gpu::RWBindlessHandle instanced_data;
			gpu::RWBindlessHandle indirect_buffer;
		};

		UBValues ub_values;
		toPlanes(view.cp, Span(ub_values.camera_planes));

		const gpu::BufferHandle culled_buffer = m_renderer.getInstancedMeshesBuffer();
		//stream.bindShaderBuffer(m_indirect_buffer, 2, gpu::BindShaderBufferFlags::OUTPUT);
		const gpu::ProgramHandle gather_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS3"));
		const gpu::ProgramHandle indirect_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS2"));
		u32 cull_shader_defines = 1 << m_renderer.getShaderDefineIdx("PASS1");
		if (!view.cp.is_shadow) cull_shader_defines |= 1 << m_renderer.getShaderDefineIdx("UPDATE_LODS");
		const gpu::ProgramHandle cull_shader = m_instancing_shader->getProgram(cull_shader_defines);
		const gpu::ProgramHandle init_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS0"));
		const gpu::ProgramHandle update_lods_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("UPDATE_LODS"));
		UniformPool& uniform_pool = m_renderer.getUniformPool();

		for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
			const InstancedModel& im = iter.value();
			Model* m = im.model;
			if (!m || !m->isReady()) continue;

			// models with clusters are always drawn with clusters
			if (m->hasClusters()) {
				if (m_cluster_shader->isReady()) {
					if (!m_cluster_draw_logged) {
						m_cluster_draw_logged = true;
						logInfo(m->getPath(), ": drawing ", im.instances.size(), " instances with clusters");
					}
					encodeClusterModel(stream, view, iter.key(), im);
				}
				else if (m_cluster_shader->isFailure() && !m_cluster_shader_failure_logged) {
					m_cluster_shader_failure_logged = true;
					logError("Cluster shader nanite.hlsl failed to load, ", m->getPath(), " is not drawn");
				}
				continue;
			}

			auto getDrawDistance = [](const Model& model) {
				const LODMeshIndices* lod_indices = model.getLODIndices();
				float dist = 0;
				for (u32 i = 0; i < 4; ++i) {
					if (lod_indices[i].to != -1) {
						dist = model.getLODDistances()[i];
					}
				}
				return sqrtf(dist);
			};

			const float draw_distance = getDrawDistance(*m);

			const Transform origin = world.getTransform(iter.key());
			const Frustum frustum = view.cp.frustum.getRelative(origin.pos);
			const float radius = m->getOriginBoundingRadius();

			struct {
				u32 offset;
				u32 count;
				bool visible;
				TransientSlice ub;
			} cells[16];
			u32 cell_count = 0;

			for (u32 i = 0; i < 16; ++i) {
				const InstancedModel::Grid::Cell& cell = im.grid.cells[i];

				if (cell.instance_count > 0) {
					const bool visible = frustum.intersectAABBWithOffset(cell.aabb, radius);
					const Vec3 cell_center = (cell.aabb.max + cell.aabb.min) * 0.5f;
					const Vec3 cell_half_extents = (cell.aabb.max - cell.aabb.min) * 0.5f;
					const float cell_radius = length(cell_half_extents);
					if (length(origin.pos - view.cp.pos + cell_center) - cell_radius < draw_distance) {
						const bool can_merge = cell_count > 0 && cells[cell_count - 1].visible == visible  && cells[cell_count - 1].offset + cells[cell_count - 1].count == cell.from_instance;
						if (can_merge) {
							cells[cell_count - 1].count += cell.instance_count;
							u32* tmp =(u32*)cells[cell_count - 1].ub.ptr;
							tmp[1] += cell.instance_count;
						}
						else {
							cells[cell_count].visible = visible;
							cells[cell_count].count = cell.instance_count;
							cells[cell_count].offset = cell.from_instance;
							const TransientSlice ub = alloc(uniform_pool, sizeof(u32) * 2);
							u32* tmp =(u32*)ub.ptr;
							tmp[0] = cell.from_instance;
							tmp[1] = cell.instance_count;
							cells[cell_count].ub = ub;
							++cell_count;
						}
					}
				}
			}
				
			if (cell_count == 0) continue;

			Vec4 lod_distances = *(Vec4*)m->getLODDistances() * global_lod_multiplier;
			if (lod_distances.w < 0) lod_distances.w = FLT_MAX;
			if (lod_distances.z < 0) lod_distances.z = FLT_MAX;
			if (lod_distances.y < 0) lod_distances.y = FLT_MAX;
			if (lod_distances.x < 0) lod_distances.x = FLT_MAX;
			IVec4 lod_indices;
			lod_indices.x = m->getLODIndices()[0].to;
			lod_indices.y = maximum(lod_indices.x, m->getLODIndices()[1].to);
			lod_indices.z = maximum(lod_indices.y, m->getLODIndices()[2].to);
			lod_indices.w = maximum(lod_indices.z, m->getLODIndices()[3].to);
			const u32 instance_count = im.instances.size();
			
			const u32 indirect_offset = m_indirect_buffer_offset.add(m->getMeshCount());

			ub_values.camera_offset = Vec4(Vec3(origin.pos - view.cp.pos), 1);
			ub_values.lod_distances = lod_distances;
			ub_values.lod_indices = lod_indices;
			ub_values.indirect_offset = indirect_offset;
			ub_values.radius = m->getOriginBoundingRadius();
			ub_values.batch_size = instance_count;
			ub_values.culled_buffer = gpu::getRWBindlessHandle(culled_buffer);
			ub_values.instanced_data = gpu::getRWBindlessHandle(im.gpu_data);
			ub_values.indirect_buffer = gpu::getRWBindlessHandle(m_indirect_buffer);
			ASSERT((u32)m->getMeshCount() < lengthOf(ub_values.indices_count)); // TODO
			for (i32 i = 0; i < m->getMeshCount(); ++i) {
				const Mesh& mesh = m->getMesh(i);
				ub_values.indices_count[i].x = mesh.indices_count;
			}

			const TransientSlice drawcall_ub = alloc(uniform_pool, &ub_values, sizeof(ub_values));

			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, drawcall_ub.buffer, drawcall_ub.offset, sizeof(UBValues));

			stream.barrier(culled_buffer, gpu::BarrierType::WRITE);
			stream.barrier(m_indirect_buffer, gpu::BarrierType::WRITE);
			stream.barrier(culled_buffer, gpu::BarrierType::WRITE);
			//stream.bindShaderBuffer(culled_buffer, 1, gpu::BindShaderBufferFlags::OUTPUT);
			stream.useProgram(init_shader);
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(culled_buffer);

			if (view.cp.is_shadow) {
				stream.barrier(im.gpu_data, gpu::BarrierType::READ);
				//stream.bindShaderBuffer(im.gpu_data, 0, gpu::BindShaderBufferFlags::NONE);
			}
			else {
				stream.barrier(im.gpu_data, gpu::BarrierType::WRITE);
				//stream.bindShaderBuffer(im.gpu_data, 0, gpu::BindShaderBufferFlags::OUTPUT);
				stream.useProgram(update_lods_shader);
				for (u32 i = 0; i < cell_count; ++i) {
					if (!cells[i].visible) {
						stream.bindUniformBuffer(UniformBuffer::DRAWCALL2, cells[i].ub.buffer, cells[i].ub.offset, cells[i].ub.size);
						stream.dispatch((cells[i].count + 255) / 256, 1, 1);
					}
				}
			}
				
			stream.useProgram(cull_shader);
			for (u32 i = 0; i < cell_count; ++i) {
				if (cells[i].visible) {
					stream.bindUniformBuffer(UniformBuffer::DRAWCALL2, cells[i].ub.buffer, cells[i].ub.offset, cells[i].ub.size);
					stream.dispatch((cells[i].count + 255) / 256, 1, 1);
				}
			}
			stream.memoryBarrier(culled_buffer);

			if (!view.cp.is_shadow) {
				stream.barrier(im.gpu_data, gpu::BarrierType::READ);
				//stream.bindShaderBuffer(im.gpu_data, 0, gpu::BindShaderBufferFlags::NONE);
			}

			stream.useProgram(indirect_shader);
			stream.dispatch((m->getMeshCount() + 255) / 256, 1, 1);
			stream.memoryBarrier(m_indirect_buffer);

			stream.useProgram(gather_shader);
			for (u32 i = 0; i < cell_count; ++i) {
				if (cells[i].visible) {
					stream.bindUniformBuffer(UniformBuffer::DRAWCALL2, cells[i].ub.buffer, cells[i].ub.offset, cells[i].ub.size);
					stream.dispatch((cells[i].count + 255) / 256, 1, 1);
				}
			}

			stream.memoryBarrier(culled_buffer);

			// instances are pulled by vertex shaders, SV_InstanceID does not include the start instance of the indirect draws
			const gpu::VertexDecl no_instance_decl(gpu::PrimitiveType::NONE);
			const u32 instanced_define = (1 << m_renderer.getShaderDefineIdx("INSTANCED")) | (1 << m_renderer.getShaderDefineIdx("INDIRECT_INSTANCE"));
			for (i32 i = 0; i < m->getMeshCount(); ++i) {
				const Mesh& mesh = m->getMesh(i);
				const MeshMaterial& mesh_mat = m->getMeshMaterial(i);
				const u8 bucket_idx = view.layer_to_bucket[mesh_mat.material->getLayer()];
				if (bucket_idx == 0xff) continue;
				
				Bucket& bucket = view.buckets[bucket_idx];
				const u32 instanced_define_mask = bucket.define_mask | instanced_define;
				const Material* material = mesh_mat.material;
				Shader* shader = material->getShader();
				const gpu::StateFlags state = material->m_render_states | bucket.state;
				const gpu::ProgramHandle program = shader->getProgram(state, pullingDecl(mesh), no_instance_decl, instanced_define_mask | material->getDefineMask(), mesh.pulling_semantics_defines);

				MaterialIndex material_index = material->getIndex();
				TransientSlice ub_slice = alloc(uniform_pool, &material_index, sizeof(material_index));

				bucket.stream.useProgram(program);
				bucket.stream.bindIndexBuffer(mesh.index_buffer_handle);
				bucket.stream.barrier(culled_buffer, gpu::BarrierType::READ);
				bindMeshVertices(bucket.stream, mesh, culled_buffer, 48);
				bucket.stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_slice.buffer, ub_slice.offset, ub_slice.size);
				
				bucket.stream.bindIndirectBuffer(m_indirect_buffer);
				bucket.stream.drawIndirect(mesh.index_type, u32(sizeof(Indirect) * (indirect_offset + i)));

				bucket.stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
				bucket.stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			}
		}

		stream.memoryBarrier(m_indirect_buffer);
			
		//stream.bindShaderBuffer(gpu::INVALID_BUFFER, 0, gpu::BindShaderBufferFlags::NONE);
		//stream.bindShaderBuffer(gpu::INVALID_BUFFER, 1, gpu::BindShaderBufferFlags::NONE);
		//stream.bindShaderBuffer(gpu::INVALID_BUFFER, 2, gpu::BindShaderBufferFlags::NONE);	

		stream.memoryBarrier(m_indirect_buffer);
	}

	void computeSkeletonDualQuats(const ModelInstance* mi) {
		DualQuat* output = (DualQuat*)mi->pose->slice.ptr;
		ASSERT((uintptr)output % 16 == 0); // we store with f4Stream, which needs aligned memory

		const Quat* rotations = mi->pose->rotations;
		ASSERT((uintptr)rotations % 16 == 0); // we load with f4Load, which needs aligned memory
		const Vec3* positions = mi->pose->positions;
		Model& model = *mi->model;

		#if 1 // SIMD
			const u32 num_batches = mi->pose->count / 4;
		#else 
			const u32 num_batches = 0;
		#endif
		SOATransform inv_bind = model.getInverseBindPose();
		for (u32 batch = 0; batch < num_batches; ++batch) {
			SIMDLocalRigidTransform tmp;

			// load pose transforms
			const u32 base = 4 * batch;
			tmp.pos.x = f4Init(positions[base].x, positions[base + 1].x, positions[base + 2].x, positions[base + 3].x);
			tmp.pos.y = f4Init(positions[base].y, positions[base + 1].y, positions[base + 2].y, positions[base + 3].y);
			tmp.pos.z = f4Init(positions[base].z, positions[base + 1].z, positions[base + 2].z, positions[base + 3].z);
			
			tmp.rot.x = f4Load(&rotations[base + 0].x);
			tmp.rot.y = f4Load(&rotations[base + 1].x);
			tmp.rot.z = f4Load(&rotations[base + 2].x);
			tmp.rot.w = f4Load(&rotations[base + 3].x);
			f4Transpose(tmp.rot.x, tmp.rot.y, tmp.rot.z, tmp.rot.w);

			// load inverse bind transform
			SIMDLocalRigidTransform inv_bind_tr;
			inv_bind_tr.pos.x =	f4Load(inv_bind.px + base);
			inv_bind_tr.pos.y =	f4Load(inv_bind.py + base);
			inv_bind_tr.pos.z =	f4Load(inv_bind.pz + base);

			inv_bind_tr.rot.x =	f4Load(inv_bind.rx + base);
			inv_bind_tr.rot.y =	f4Load(inv_bind.ry + base);
			inv_bind_tr.rot.z =	f4Load(inv_bind.rz + base);
			inv_bind_tr.rot.w =	f4Load(inv_bind.rw + base);

			// convert
			SIMDDualQuat out_dual = toDualQuat(tmp * inv_bind_tr);

			// store output
			f4Transpose(out_dual.r.x, out_dual.r.y, out_dual.r.z, out_dual.r.w);

			f4Stream(&output[base].r.x, out_dual.r.x);
			f4Stream(&output[base + 1].r.x, out_dual.r.y);
			f4Stream(&output[base + 2].r.x, out_dual.r.z);
			f4Stream(&output[base + 3].r.x, out_dual.r.w);

			f4Transpose(out_dual.d.x, out_dual.d.y, out_dual.d.z, out_dual.d.w);

			f4Stream(&output[base].d.x, out_dual.d.x);
			f4Stream(&output[base + 1].d.x, out_dual.d.y);
			f4Stream(&output[base + 2].d.x, out_dual.d.z);
			f4Stream(&output[base + 3].d.x, out_dual.d.w);
		}

		for (int j = 4 * num_batches, c = mi->pose->count; j < c; ++j) {
			const Model::Bone& bone = model.getBone(j);
			const LocalRigidTransform tmp = {positions[j], rotations[j]};
			output[j] = (tmp * model.getInverseBindTransform(j)).toDualQuat();
		}
	}

	void createCommands(View& view)
	{
		PROFILE_FUNCTION();
		const u32 keys_count = (u32)view.sorter.keys.size();
		profiler::pushInt("Count", keys_count);
		if (keys_count == 0) return;
		
		const u64* LUMIX_RESTRICT renderables = view.sorter.values.begin();
		const u64* LUMIX_RESTRICT sort_keys = view.sorter.keys.begin();

		const World& world = m_module->getWorld();
		const ShiftedFrustum frustum = view.cp.frustum;
		ModelInstance* LUMIX_RESTRICT model_instances = m_module->getModelInstances().begin();
		const Transform* LUMIX_RESTRICT transforms = world.getTransforms(); 
		const DVec3 camera_pos = view.cp.pos;
		
		// instances are pulled by vertex shaders too
		const gpu::VertexDecl no_instance_decl(gpu::PrimitiveType::NONE);

		UniformPool& uniform_pool = m_renderer.getUniformPool();
		TransientPool& transient_pool = m_renderer.getTransientPool();

		const u32 num_batches = lengthOf(view.buckets[0].substreams);
		jobs::forEach(num_batches, 1, [&](u32 batch_idx, u32){
			PROFILE_BLOCK("create commands");
			const u32 step = (keys_count + num_batches - 1) / num_batches;
			const u32 from = batch_idx * step;
			u32 to = from + step;
			if (from >= keys_count) return;
			if (to > keys_count) to = keys_count;

			u64 instance_key_mask;
			u32 define_mask;
			u32 autoinstanced_define_mask;
			u32 mesh_particle_define_mask;
			u32 dynamic_define_mask;
			u32 skinned_define_mask;
			gpu::StateFlags render_state;
			DrawStream* stream = nullptr;
			u8 prev_bucket = (sort_keys[from] >> SORT_KEY_BUCKET_SHIFT) + 1; // make sure it's different from the first bucket, so first iteration is properly initialized

			for (u32 i = from; i < to; ++i) {
				const EntityRef entity {int(renderables[i] & 0xFFffFFff)};
				const DrawCommandTypes type = DrawCommandTypes((renderables[i] >> SORT_VALUE_TYPE_SHIFT) & SORT_VALUE_TYPE_MASK);
				const u8 bucket = sort_keys[i] >> SORT_KEY_BUCKET_SHIFT;

				if (bucket != prev_bucket) {
					prev_bucket = bucket;
					stream = view.buckets[bucket].substreams[batch_idx];
					define_mask = view.buckets[bucket].define_mask;
					autoinstanced_define_mask = define_mask | (1 << m_autoinstanced_define_idx);
					dynamic_define_mask = define_mask | (1 << m_dynamic_define_idx);
					skinned_define_mask = define_mask | (1 << m_skinned_define_idx);
					mesh_particle_define_mask = define_mask | (1 << m_mesh_particle_define_idx);
					
					const bool sort_depth = view.buckets[bucket].sort == BucketDesc::DEPTH;
					instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
					render_state = view.buckets[bucket].state;
				}

				switch(type) {
					case DrawCommandTypes::RIBBONS: {
						const u32 emitter_idx = u32(renderables[i] >> SORT_VALUE_EMITTER_SHIFT);
						const ParticleSystem& particle_system = m_module->getParticleEmitter(entity);
						const ParticleSystem::Emitter& emitter = particle_system.getEmitter(emitter_idx);
						const Material* material = emitter.resource_emitter.material;

						const Transform tr = world.getTransform(*particle_system.m_entity);
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const gpu::StateFlags state = material->m_render_states | render_state;
						gpu::VertexDecl inst_decl(gpu::PrimitiveType::TRIANGLE_STRIP);
						inst_decl.addAttribute(0, 4, gpu::AttributeType::U32, gpu::Attribute::INSTANCED);
						u32 stride = inst_decl.getStride();

						gpu::ProgramHandle program = material->getShader()->getProgram(state, inst_decl, define_mask | material->getDefineMask(), "");
						const TransientSlice point_slice = emitter.slice;
						const Matrix mtx(lpos, tr.rot);

						struct {
							Matrix mtx;
							MaterialIndex material_index;
						} ub_data = {
							mtx,
							material->getIndex()
						};
						const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
						stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream->useProgram(program);
						stream->bindIndexBuffer(gpu::INVALID_BUFFER);
						stream->bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);

						u32 points_offset = 0;
						u32 offset = 0;
						const gpu::VertexDecl& decl = emitter.resource_emitter.vertex_decl;
						TransientSlice slice = alloc(transient_pool, sizeof(u32) * 4 * emitter.ribbons.size());
						u32* inst_data = (u32*)slice.ptr;
						for (const auto& ribbon : emitter.ribbons) {
							inst_data[0] = gpu::getBindlessHandle(point_slice.buffer).value;
							inst_data[1] = point_slice.offset + points_offset;
							inst_data[2] = ribbon.offset;
							inst_data[3] = ribbon.length;
							stream->bindVertexBuffer(1, slice.buffer, slice.offset + offset, stride);
							// TODO instanced draw / indirect instanced
							if(ribbon.length > 1) {
								stream->drawArraysInstanced(2 * ribbon.length, 1);
							}
							offset += 4 * sizeof(u32);
							points_offset += ribbon.length * decl.getStride();
							inst_data += 4;
						}
						break;
					}
					case DrawCommandTypes::RIBBON_TUBES: {
						const u32 emitter_idx = u32(renderables[i] >> SORT_VALUE_EMITTER_SHIFT);
						const ParticleSystem& particle_system = m_module->getParticleEmitter(entity);
						const ParticleSystem::Emitter& emitter = particle_system.getEmitter(emitter_idx);
						const Material* material = emitter.resource_emitter.material;
						const u32 tube_segments = emitter.resource_emitter.tube_segments;

						const Transform tr = world.getTransform(*particle_system.m_entity);
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const gpu::StateFlags state = material->m_render_states | render_state;
						gpu::VertexDecl inst_decl(gpu::PrimitiveType::TRIANGLE_STRIP);
						inst_decl.addAttribute(0, 4, gpu::AttributeType::U32, gpu::Attribute::INSTANCED);
						u32 stride = inst_decl.getStride();

						gpu::ProgramHandle program = material->getShader()->getProgram(state, inst_decl, define_mask | material->getDefineMask(), "");
						const TransientSlice point_slice = emitter.slice;
						const Matrix mtx(lpos, tr.rot);

						struct {
							Matrix mtx;
							MaterialIndex material_index;
							u32 tube_segments;
						} ub_data = {
							mtx,
							material->getIndex(),
							tube_segments
						};
						const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
						stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream->useProgram(program);
						stream->bindIndexBuffer(gpu::INVALID_BUFFER);
						stream->bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);

						u32 points_offset = 0;
						u32 offset = 0;
						const gpu::VertexDecl& decl = emitter.resource_emitter.vertex_decl;
						TransientSlice slice = alloc(transient_pool, sizeof(u32) * 4 * emitter.ribbons.size());
						u32* inst_data = (u32*)slice.ptr;
						for (const auto& ribbon : emitter.ribbons) {
							inst_data[0] = gpu::getBindlessHandle(point_slice.buffer).value;
							inst_data[1] = point_slice.offset + points_offset;
							inst_data[2] = ribbon.offset;
							inst_data[3] = emitter.resource_emitter.max_ribbon_length;
							stream->bindVertexBuffer(1, slice.buffer, slice.offset + offset, stride);
							if(ribbon.length > 1) {
								// For tube: each ring transition needs (tube_segments + 1) * 2 vertices
								const u32 ring_transitions = ribbon.length - 1;
								const u32 vertex_count = ring_transitions * (tube_segments + 1) * 2;
								stream->drawArraysInstanced(vertex_count, 1);
							}
							offset += 4 * sizeof(u32);
							points_offset += ribbon.length * decl.getStride();
							inst_data += 4;
						}
						break;
					}
					case DrawCommandTypes::MESH_PARTICLES: {
						const u32 emitter_idx = u32(renderables[i] >> SORT_VALUE_EMITTER_SHIFT);
						const ParticleSystem& particle_system = m_module->getParticleEmitter(entity);
						const ParticleSystem::Emitter& emitter = particle_system.getEmitter(emitter_idx);
						const Model* model = emitter.resource_emitter.model;
						const u32 particles_count = emitter.particles_count;
						const MeshMaterial& mesh_mat = model->getMeshMaterial(0);
						const Material* material = mesh_mat.material;
						const Mesh& mesh = model->getMesh(0);

						const Transform tr = world.getTransform(*particle_system.m_entity);
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const gpu::VertexDecl& inst_decl = emitter.resource_emitter.vertex_decl;
						const gpu::StateFlags state = material->m_render_states | render_state;
						const u32 defines = mesh_particle_define_mask | material->getDefineMask();
						Shader* shader = material->getShader();
						gpu::ProgramHandle program = shader->getProgram(state, pullingDecl(mesh), inst_decl, defines, mesh.pulling_semantics_defines);

						struct UBData {
							Vec3 pos;
							MaterialIndex material_index;
						} ub_data = {
							lpos,
							material->getIndex()
						};

						TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));

						stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream->useProgram(program);
						stream->bindIndexBuffer(mesh.index_buffer_handle);
						bindMeshVertices(*stream, mesh);
						stream->bindVertexBuffer(1, emitter.slice.buffer, emitter.slice.offset, inst_decl.getStride());
						stream->drawIndexedInstanced(mesh.indices_count, particles_count, mesh.index_type);
						break;
					}
					case DrawCommandTypes::PARTICLES: {
						const u32 emitter_idx = u32(renderables[i] >> SORT_VALUE_EMITTER_SHIFT);
						const ParticleSystem& particle_system = m_module->getParticleEmitter(entity);
						const ParticleSystem::Emitter& emitter = particle_system.getEmitter(emitter_idx);
						const Material* material = emitter.resource_emitter.material;
						const u32 particles_count = emitter.particles_count;

						const Transform tr = world.getTransform(*particle_system.m_entity);
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const gpu::VertexDecl& decl = emitter.resource_emitter.vertex_decl;
						const gpu::StateFlags state = material->m_render_states | render_state;
						gpu::ProgramHandle program = material->getShader()->getProgram(state, decl, define_mask | material->getDefineMask(), "");
						const TransientSlice slice = emitter.slice;
						const Matrix mtx(lpos, tr.rot);

						struct {
							Matrix mtx;
							MaterialIndex material_index;
						} ub_data = {
							mtx,
							material->getIndex()
						};

						const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
						stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream->useProgram(program);
						stream->bindIndexBuffer(gpu::INVALID_BUFFER);
						stream->bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
						stream->bindVertexBuffer(1, slice.buffer, slice.offset, decl.getStride());
						stream->drawArraysInstanced(4, particles_count);
						break;
					}
					case DrawCommandTypes::AUTOINSTANCED: {
						const u32 group_idx = renderables[i] & 0xffFF;
						const u32 instancer_idx = (renderables[i] >> SORT_VALUE_INSTANCER_SHIFT) & 0xffFF;
						const AutoInstancer::Instances& instances = view.instancers[instancer_idx].instances[group_idx];
						const u32 total_count = instances.end->offset + instances.end->count;
						const u64 renderable = instances.begin->renderables[0];
						const u32 entity_index = u32(renderable & 0xffFFffFF);
						const u32 mesh_idx = u32(renderable >> SORT_VALUE_MESH_IDX_SHIFT);
						const ModelInstance& mi = model_instances[entity_index];
						const Mesh& mesh = mi.model->getMesh(mesh_idx);
						const MeshMaterial& mesh_mat = mi.mesh_materials[mesh_idx];

						const Material* material = mesh_mat.material;
						Shader* shader = material->getShader();
						const gpu::StateFlags state = material->m_render_states | render_state;
						const u32 defines = autoinstanced_define_mask | material->getDefineMask();
						const gpu::ProgramHandle program = shader->getProgram(state, pullingDecl(mesh), no_instance_decl, defines, mesh.pulling_semantics_defines);

						bindMeshVertices(*stream, mesh, instances.slice);
						gpu::Drawcall& dc = stream->draw();
						dc = {
							.program = program,
							.index_buffer = mesh.index_buffer_handle,
							.vertex_buffers = { gpu::INVALID_BUFFER, gpu::INVALID_BUFFER },
							.vertex_buffer_offsets = { 0, 0 },
							.vertex_buffer_sizes = { 0, 0 },
							.indices_count = mesh.indices_count,
							.instances_count = total_count,
							.index_type = mesh.index_type,
						};
						break;
					}
					case DrawCommandTypes::MESH: {
						const u32 mesh_idx = u32(renderables[i] >> SORT_VALUE_MESH_IDX_SHIFT);
						ModelInstance* LUMIX_RESTRICT mi = &model_instances[entity.index];
						const Mesh& mesh = mi->meshes[mesh_idx];
						const MeshMaterial& mesh_mat = mi->mesh_materials[mesh_idx];
						const float mesh_lod = mesh.lod;
						const Material* material = mesh_mat.material;
						Shader* shader = material->getShader();
						const gpu::StateFlags state = material->m_render_states | render_state;

						if (mi->flags & ModelInstance::MOVED) {
							const i32 start_i = i;
							const u64 key = sort_keys[i];
							while (i < to && sort_keys[i] == key) {
								++i;
							}

							const u32 count = u32(i - start_i);
							const TransientSlice slice = alloc(transient_pool, count * (sizeof(Vec4) * 6));
							u8* instance_data = slice.ptr;

							for (i32 j = start_i; j < start_i + (i32)count; ++j) {
								const EntityRef e { i32(renderables[j] & 0xFFffFFff) };
								const Transform& tr = transforms[e.index];
								const Vec3 pos_ws = Vec3(tr.pos - camera_pos);
								const float lod_d = model_instances[e.index].lod - mesh_lod;
								ModelInstance* mi2 = &model_instances[e.index];
								const Transform prev_tr = mi2->prev_frame_transform;
								const Vec3 prev_pos_ws = Vec3(prev_tr.pos - camera_pos);
							
								#define WRITE(X) memcpy(instance_data, &X, sizeof(X)); instance_data += sizeof(X)
								WRITE(tr.rot);
								WRITE(pos_ws);
								WRITE(lod_d);
								WRITE(tr.scale);
								instance_data += sizeof(float); // padding
							
								WRITE(prev_tr.rot);
								WRITE(prev_pos_ws);
								WRITE(lod_d);
								WRITE(prev_tr.scale);
								const MaterialIndex material_index = mi2->mesh_materials[mesh_idx].material_index;
								WRITE(material_index);
								#undef WRITE
							}

							const u32 defines = dynamic_define_mask | material->getDefineMask();
							const gpu::ProgramHandle program = shader->getProgram(state, pullingDecl(mesh), no_instance_decl, defines, mesh.pulling_semantics_defines);
					
							stream->useProgram(program);
							stream->bindIndexBuffer(mesh.index_buffer_handle);
							bindMeshVertices(*stream, mesh, slice);
							stream->drawIndexedInstanced(mesh.indices_count, count, mesh.index_type);
							--i;
						}
						else {
							int start_i = i;
							const u64 key = sort_keys[i] & instance_key_mask;
							while (i < to && (sort_keys[i] & instance_key_mask) == key) {
								++i;
							}
							const u32 count = u32(i - start_i);
							const TransientSlice slice = alloc(transient_pool, count * (sizeof(Vec4) * 3));
							u8* instance_data = slice.ptr;
							for (int j = start_i; j < start_i + (i32)count; ++j) {
								const EntityRef e { i32(renderables[j] & 0xFFffFFff) };
								const Transform& tr = transforms[e.index];
								const Vec3 lpos = Vec3(tr.pos - camera_pos);
								const float lod_d = model_instances[e.index].lod - mesh_lod;
								memcpy(instance_data, &tr.rot, sizeof(tr.rot));
								instance_data += sizeof(tr.rot);
								memcpy(instance_data, &lpos, sizeof(lpos));
								instance_data += sizeof(lpos);
								memcpy(instance_data, &lod_d, sizeof(lod_d));
								instance_data += sizeof(lod_d);
								memcpy(instance_data, &tr.scale, sizeof(tr.scale));
								instance_data += sizeof(tr.scale);
								MaterialIndex material_index = material->getIndex();
								memcpy(instance_data, &material_index, sizeof(material_index));
								instance_data += sizeof(material_index);
							}

							const u32 defines = autoinstanced_define_mask | material->getDefineMask();
							const gpu::ProgramHandle program = shader->getProgram(state, pullingDecl(mesh), no_instance_decl, defines, mesh.pulling_semantics_defines);

							stream->useProgram(program);
							stream->bindIndexBuffer(mesh.index_buffer_handle);
							bindMeshVertices(*stream, mesh, slice);
							stream->drawIndexedInstanced(mesh.indices_count, count, mesh.index_type);
							--i;
						}
						break;
					}
					case DrawCommandTypes::SKINNED: {
						const u32 mesh_idx = u32(renderables[i] >> SORT_VALUE_MESH_IDX_SHIFT);
						ModelInstance* LUMIX_RESTRICT mi = &model_instances[entity.index];
						const MeshMaterial& mesh_mat = mi->mesh_materials[mesh_idx];
						u32 defines = skinned_define_mask | mesh_mat.material->getDefineMask();
						const gpu::StateFlags state = mesh_mat.material->m_render_states | render_state;

						const u32 start_i = i;
						const u64 key = sort_keys[i];
						while (i < to && sort_keys[i] == key) {
							++i;
						}
						const u32 num_instances = i - start_i;
						--i;
						
						struct SkinnedInstanceData {
							IVec3 idata;
							Vec3 pos;
							Quat rot;
							Vec3 scale;
							Vec3 prev_pos;
							Quat prev_rot;
							Vec3 prev_scale;
						};

						const TransientSlice instance_buffer = alloc(transient_pool, sizeof(SkinnedInstanceData) * num_instances);
						for (u32 j = start_i; j <= i; ++j) {
							SkinnedInstanceData* skin_draw_data = (SkinnedInstanceData*)instance_buffer.ptr + j - start_i;

							const EntityRef e {int(renderables[j] & 0xFFffFFff)};
							ModelInstance* mi2 = &model_instances[e.index];
							const Transform& tr = transforms[e.index];
							
							const Vec3 rel_pos = Vec3(tr.pos - camera_pos);
							const Material& material = *mi2->mesh_materials[mesh_idx].material;

							skin_draw_data->pos = rel_pos;
							skin_draw_data->rot = tr.rot;
							skin_draw_data->scale = tr.scale;

							const Vec3 prev_rel_pos = Vec3(mi2->prev_frame_transform.pos - camera_pos);
							skin_draw_data->prev_pos = prev_rel_pos;
							skin_draw_data->prev_rot = mi2->prev_frame_transform.rot;
							skin_draw_data->prev_scale = mi2->prev_frame_transform.scale;

							const TransientSlice bones_slice = mi2->pose->slice;

							skin_draw_data->idata.x = (i32)material.getIndex();
							skin_draw_data->idata.y = gpu::getBindlessHandle(bones_slice.buffer).value;
							skin_draw_data->idata.z = bones_slice.offset;
						}

						Shader* shader = mesh_mat.material->getShader();
						const Mesh& mesh = mi->meshes[mesh_idx];
						const gpu::ProgramHandle program = shader->getProgram(state, pullingDecl(mesh), no_instance_decl, defines, mesh.pulling_semantics_defines);
						stream->useProgram(program);
						stream->bindIndexBuffer(mesh.index_buffer_handle);
						bindMeshVertices(*stream, mesh, instance_buffer);
						stream->drawIndexedInstanced(mesh.indices_count, num_instances, mesh.index_type);
						break;
					}
					case DrawCommandTypes::DECAL: {
						const Material* material = m_module->getDecal(entity).material;

						int start_i = i;
						const u64 key = sort_keys[i];
						while (i < to && sort_keys[i] == key) {
							++i;
						}
						const u32 count = i - start_i;
						struct DecalData {
							Vec3 pos;
							Quat rot;
							Vec3 half_extents;
							Vec2 uv_scale;
							MaterialIndex material_index;
						};
						const TransientSlice slice = alloc(transient_pool, count * (sizeof(DecalData)));

						DecalData* beg = (DecalData*)slice.ptr;
						DecalData* end = (DecalData*)(slice.ptr + (count - 1) * sizeof(DecalData));
						for(u32 j = start_i; j < i; ++j) {
							const EntityRef e {int(renderables[j] & 0x00ffFFff)};
							const Transform& tr = transforms[e.index];
							const Vec3 lpos = Vec3(tr.pos - camera_pos);
							const Decal& decal = m_module->getDecal(e);
							const float m = maximum(decal.half_extents.x, decal.half_extents.y, decal.half_extents.z);
							const bool intersecting = frustum.intersectNearPlane(tr.pos, m * SQRT3);
						
							DecalData* iter = intersecting ? end : beg;
							iter->pos = lpos;
							iter->rot = tr.rot;
							iter->half_extents = decal.half_extents;
							iter->uv_scale = decal.uv_scale;
							iter->material_index = material->getIndex();
							intersecting ? --end : ++beg;
						}

						stream->bindIndexBuffer(m_cube_ib);

						gpu::StateFlags state = material->m_render_states | render_state;
						state = state & ~gpu::StateFlags::CULL_FRONT | gpu::StateFlags::CULL_BACK;
						const u32 nonintersecting_count = u32(beg - (DecalData*)slice.ptr);
						if (nonintersecting_count) {
							stream->useProgram(material->getShader()->getProgram(state, m_decal_decl, define_mask | material->getDefineMask(), m_cube_pulling_defines));
							bindPulledBuffers(*stream, m_cube_vb, slice.buffer, slice.offset);
							stream->drawIndexedInstanced(36, nonintersecting_count, gpu::DataType::U16);
						}

						if (count - nonintersecting_count) {
							state = state & ~gpu::StateFlags::DEPTH_FUNCTION;
							state = state & ~gpu::StateFlags::CULL_BACK;
							state = state | gpu::StateFlags::CULL_FRONT;
							stream->useProgram(material->getShader()->getProgram(state, m_decal_decl, define_mask | material->getDefineMask(), m_cube_pulling_defines));
							const u32 offs = slice.offset + sizeof(DecalData) * nonintersecting_count;
							bindPulledBuffers(*stream, m_cube_vb, slice.buffer, offs);
							stream->drawIndexedInstanced(36, count - nonintersecting_count, gpu::DataType::U16);
						}
						--i;
						break;
					}
					case DrawCommandTypes::CURVE_DECAL: {
						const Material* material = m_module->getCurveDecal(entity).material;

						int start_i = i;
						const u64 key = sort_keys[i];
						while (i < to && sort_keys[i] == key) {
							++i;
						}
						const u32 count = i - start_i;
						struct DecalData {
							Vec3 pos;
							Quat rot;
							Vec3 half_extents;
							Vec2 uv_scale;
							Vec4 bezier;
							MaterialIndex material_index;
						};
						const TransientSlice slice = alloc(transient_pool, count * (sizeof(DecalData)));

						DecalData* beg = (DecalData*)slice.ptr;
						DecalData* end = (DecalData*)(slice.ptr + (count - 1) * sizeof(DecalData));
						for(u32 j = start_i; j < i; ++j) {
							const EntityRef e {int(renderables[j] & 0x00ffFFff)};
							const Transform& tr = transforms[e.index];
							const Vec3 lpos = Vec3(tr.pos - camera_pos);
							const CurveDecal& decal = m_module->getCurveDecal(e);
							const float m = maximum(decal.half_extents.x, decal.half_extents.y, decal.half_extents.z);
							const bool intersecting = frustum.intersectNearPlane(tr.pos, m * SQRT3);
						
							DecalData* iter = intersecting ? end : beg;
							iter->pos = lpos;
							iter->rot = tr.rot;
							iter->half_extents = decal.half_extents;
							iter->uv_scale = decal.uv_scale;
							iter->bezier = Vec4(decal.bezier_p0, decal.bezier_p2);
							iter->material_index = material->getIndex();
							intersecting ? --end : ++beg;
						}

						stream->bindIndexBuffer(m_cube_ib);

						gpu::StateFlags state = material->m_render_states | render_state;
						state = state & ~gpu::StateFlags::CULL_FRONT | gpu::StateFlags::CULL_BACK;
						const u32 nonintersecting_count = u32(beg - (DecalData*)slice.ptr);
						if (nonintersecting_count) {
							stream->useProgram(material->getShader()->getProgram(state, m_decal_decl, define_mask | material->getDefineMask(), m_cube_pulling_defines));
							bindPulledBuffers(*stream, m_cube_vb, slice.buffer, slice.offset);
							stream->drawIndexedInstanced(36, nonintersecting_count, gpu::DataType::U16);
						}

						if (count - nonintersecting_count) {
							state = state & ~gpu::StateFlags::DEPTH_FUNCTION;
							state = state & ~gpu::StateFlags::CULL_BACK;
							state = state | gpu::StateFlags::CULL_FRONT;
							stream->useProgram(material->getShader()->getProgram(state, m_decal_decl, define_mask | material->getDefineMask(), m_cube_pulling_defines));
							const u32 offs = slice.offset + sizeof(DecalData) * nonintersecting_count;
							bindPulledBuffers(*stream, m_cube_vb, slice.buffer, offs);
							stream->drawIndexedInstanced(36, count - nonintersecting_count, gpu::DataType::U16);
						}
						--i;
						break;
					}
				}
			}
		});
	}

	static float computeShadowPriority(float fov, float light_radius, const DVec3& light_pos, const DVec3& cam_pos) {
		float lr3 = light_radius * light_radius * light_radius;
		return float(fov * lr3 / length(cam_pos - light_pos));
	};

	void fillClusters(DrawStream& stream, const CameraParams& cp) {
		PROFILE_FUNCTION();
		ASSERT(cp.frustum.xs[0] == cp.frustum.xs[0]);
		ASSERT(cp.pos.x == cp.pos.x);
		struct ClusterLight {
			Vec3 pos;
			float radius;
			Quat rot;
			Vec3 color;
			float attenuation_param;
			u32 atlas_idx;
			float fov;
			Vec2 padding;
		};

		struct Cluster {
			u32 offset;
			u32 lights_count;
			u32 env_probes_count;
			u32 refl_probes_count;
		};

		struct ClusterEnvProbe {
			Vec3 pos;
			float pad0;
			Quat rot;
			Vec3 inner_range;
			float pad1;
			Vec3 outer_range;
			float pad2;
			Vec4 sh_coefs[9];
		};

		struct ClusterReflProbe {
			Vec3 pos;
			u32 layer;
			Quat rot;
			Vec3 half_extents;
			float pad1;
		};

		ArenaAllocator& frame_allocator = m_renderer.getCurrentFrameAllocator();
		const IVec3 size(
			(m_viewport.w + 63) / 64,
			(m_viewport.h + 63) / 64,
			16);

		const u32 clusters_count = size.x * size.y * size.z;
		Cluster* clusters = (Cluster*)frame_allocator.allocate(sizeof(Cluster) * clusters_count, alignof(Cluster));
		memset(clusters, 0, sizeof(Cluster) * clusters_count);

		const DVec3 cam_pos = cp.pos;
		const World& world = m_module->getWorld();
		CullResult* light_entities = m_module->getRenderables(cp.frustum, RenderableTypes::LOCAL_LIGHT);
		const u32 lights_count = light_entities ? light_entities->count() : 0;
		ClusterLight* lights = (ClusterLight*)frame_allocator.allocate(sizeof(ClusterLight) * lights_count, alignof(ClusterLight));

		AtlasSorter atlas_sorter;
		if (light_entities) {
			u32 i = 0;
			light_entities->forEach([&](EntityRef e){
				PointLight& pl = m_module->getPointLight(e);
				ClusterLight& light = lights[i];
				light.radius = pl.range;
				const DVec3 light_pos = world.getPosition(e);
				light.pos = Vec3(light_pos - cam_pos);
				light.rot = world.getRotation(e);
				light.fov = pl.fov;
				light.color = pl.color * pl.intensity;
				light.attenuation_param = pl.attenuation_param;

				auto iter = m_shadow_atlas.map.find(e);
				if (pl.flags & PointLight::CAST_SHADOWS) {
					light.atlas_idx = iter.isValid() ? iter.value() : -1;
					atlas_sorter.push(i, computeShadowPriority(light.fov, light.radius, light_pos, cam_pos), e);
				}
				else {
					light.atlas_idx = -1;
					if(iter.isValid()) {
						m_shadow_atlas.remove(e);
					}
				}
				++i;
			});
			light_entities->free(m_renderer.getEngine().getPageAllocator());
		}

		// release every slot whose light is not ranked into the same group this frame
		// this includes lights that are not in the sorter at all (outside of the frustum, destroyed, shadows turned off, ...),
		// otherwise their slots would never be freed
		for (u32 slot = 0; slot < ShadowAtlas::SLOTS; ++slot) {
			if (!m_shadow_atlas.inv_map[slot].isValid()) continue;

			const EntityRef slot_entity = EntityRef(m_shadow_atlas.inv_map[slot]);
			bool keep = false;
			for (u32 i = 0; i < atlas_sorter.count && i < ShadowAtlas::SLOTS; ++i) {
				if (atlas_sorter.lights[i].entity != slot_entity) continue;

				keep = ShadowAtlas::getGroup(i) == ShadowAtlas::getGroup(slot);
				break;
			}
			if (!keep) m_shadow_atlas.remove(slot_entity);
		}
		for (u32 i = 0; i < atlas_sorter.count; ++i) {
			auto iter = m_shadow_atlas.map.find(atlas_sorter.lights[i].entity);
			lights[atlas_sorter.lights[i].idx].atlas_idx = iter.isValid() ? iter.value() : -1;
		}

		if (!m_shadow_atlas.texture && atlas_sorter.count > 0) {
			m_shadow_atlas.texture = m_renderer.createTexture(ShadowAtlas::SIZE, ShadowAtlas::SIZE, 1, gpu::TextureFormat::D32, gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE, Renderer::MemRef(), "shadow_atlas");
		}
		Matrix shadow_atlas_matrices[128];
		for (u32 i = 0; i < atlas_sorter.count && i < ShadowAtlas::SLOTS; ++i) {
			ClusterLight& light = lights[atlas_sorter.lights[i].idx];
			EntityRef e = atlas_sorter.lights[i].entity;
			PointLight& pl = m_module->getPointLight(e);
			if (light.atlas_idx == -1) {
				light.atlas_idx = m_shadow_atlas.add(ShadowAtlas::getGroup(i), e);
				if (light.atlas_idx == -1) continue;

				bakeShadow(pl, light.atlas_idx);
			}
			else if (pl.flags & PointLight::DYNAMIC) {
				bakeShadow(pl, light.atlas_idx);
			}
			shadow_atlas_matrices[light.atlas_idx] = getShadowMatrix(pl, light.atlas_idx);
		}
		UniformPool& uniform_pool = m_renderer.getUniformPool();

		const TransientSlice shadow_matrices_ub = alloc(uniform_pool, &shadow_atlas_matrices, sizeof(shadow_atlas_matrices));
		stream.bindUniformBuffer(UniformBuffer::SHADOW, shadow_matrices_ub.buffer, shadow_matrices_ub.offset, shadow_matrices_ub.size);

		m_renderer.pushJob("fill clusters", [clusters_count, clusters, lights, lights_count, cam_pos, &world, size, cp, this, &frame_allocator](DrawStream& stream){
			auto bind = [](auto& buffer, const void* data, u32 size, DrawStream& stream, const char* debug_name){
				if (!size) return;
				const u32 capacity = (size + 15) & ~15;
				if (buffer.capacity < capacity) {
					if (buffer.buffer) stream.destroy(buffer.buffer);
					buffer.buffer = gpu::allocBufferHandle();
					stream.createBuffer(buffer.buffer, gpu::BufferFlags::SHADER_BUFFER, capacity, nullptr, debug_name);
					buffer.capacity = capacity;
				}
				stream.update(buffer.buffer, data, size);
				stream.barrier(buffer.buffer, gpu::BarrierType::READ);
			};
			const Span<const ReflectionProbe> module_refl_probes = m_module->getReflectionProbes();
			const Span<const EnvironmentProbe> module_env_probes = m_module->getEnvironmentProbes();
	
			ClusterEnvProbe* env_probes = (ClusterEnvProbe*)frame_allocator.allocate(sizeof(ClusterEnvProbe) * module_env_probes.length(), alignof(ClusterEnvProbe));
			ClusterReflProbe* refl_probes = (ClusterReflProbe*)frame_allocator.allocate(sizeof(ClusterReflProbe) * module_refl_probes.length(), alignof(ClusterReflProbe));
	
			const ShiftedFrustum& frustum = cp.frustum;
			Vec4 xplanes[65];
			Vec4 yplanes[65];
			Vec4 zplanes[17];
	
			const Vec3 cam_dir = normalize(cross(frustum.points[2] - frustum.points[0], frustum.points[1] - frustum.points[0]));
		
			for (i32 i = 0; i < size.z + 1; ++i) {
				// TODO do not hardcode constants
				float znear = 0.1f;
				float zfar = 10000.0f;
				const float z = znear * powf(zfar / znear, i / (float)size.z);
				const Vec3 p = cam_dir * z;
				zplanes[i] = makePlane(cam_dir, p);
			}
	
			for (i32 i = 0; i < size.y + 1; ++i) {
				const float t = i / (float)size.y;
				const Vec3 a = lerp(frustum.points[0], frustum.points[3], t);
				const Vec3 b = lerp(frustum.points[1], frustum.points[2], t);
				const Vec3 c = lerp(frustum.points[4], frustum.points[7], t);
				const Vec3 n = normalize(cross(b - a, c - a));
				yplanes[i] = makePlane(n, a);
			}
	
			for (i32 i = 0; i < size.x + 1; ++i) {
				const float t = i / (float)size.x;
				const Vec3 a = lerp(frustum.points[1], frustum.points[0], t);
				const Vec3 b = lerp(frustum.points[2], frustum.points[3], t);
				const Vec3 c = lerp(frustum.points[5], frustum.points[4], t);
				const Vec3 n = normalize(cross(b - a, c - a));
				xplanes[i] = makePlane(n, a);
			}
	
			ASSERT(lengthOf(xplanes) >= (u32)size.x);
			ASSERT(lengthOf(yplanes) >= (u32)size.y);
	
			const Span<EntityRef> refl_probe_entities = m_module->getReflectionProbesEntities();
			for (u32 i = 0, c = module_refl_probes.length(); i < c; ++i) {
				const ReflectionProbe& refl_probe = module_refl_probes[i];
				if (!isFlagSet(refl_probe.flags, ReflectionProbe::ENABLED)) continue;
				const EntityRef e = refl_probe_entities[i];
				ClusterReflProbe& probe =  refl_probes[i];
				probe.pos = Vec3(world.getPosition(e) - cam_pos);
				probe.rot = world.getRotation(e).conjugated();
				probe.half_extents = refl_probe.half_extents;
				probe.layer = refl_probe.texture_id;
			}
	
			sort(refl_probes, refl_probes + module_refl_probes.length(), [](const ClusterReflProbe& a, const ClusterReflProbe& b){
				const float a3 = a.half_extents.x * a.half_extents.y * a.half_extents.z;
				const float b3 = b.half_extents.x * b.half_extents.y * b.half_extents.z;
				return a3 < b3;
			});

			const Span<EntityRef> env_probe_entities = m_module->getEnvironmentProbesEntities();
			for (u32 probe_idx = 0, c = module_env_probes.length(); probe_idx < c; ++probe_idx) {
				const EnvironmentProbe& env_probe = module_env_probes[probe_idx];
				if (!isFlagSet(env_probe.flags, EnvironmentProbe::ENABLED)) continue;
	
				const EntityRef e = env_probe_entities[probe_idx];
				ClusterEnvProbe& probe =  env_probes[probe_idx];
				probe.pos = Vec3(world.getPosition(e) - cam_pos);
				probe.rot = world.getRotation(e).conjugated();
				probe.inner_range = env_probe.inner_range;
				probe.outer_range = env_probe.outer_range;
				for (u32 i = 0; i < 9; ++i) {
					probe.sh_coefs[i] = Vec4(env_probe.sh_coefs[i], 0);
				}
			}
	
			sort(env_probes, env_probes + module_env_probes.length(), [](const ClusterEnvProbe& a, const ClusterEnvProbe& b){
				const float a3 = a.outer_range.x * a.outer_range.y * a.outer_range.z;
				const float b3 = b.outer_range.x * b.outer_range.y * b.outer_range.z;
				return a3 < b3;
			});
	
			auto range = [](const Vec3& p, float r, i32 size, const Vec4* planes){
				IVec2 range = { -1, -1 };
				if (planeDist(planes[0], p) < -r) return range;
				
				for (i32 i = 0; i < size; ++i) {
					const float dist = planeDist(planes[i + 1], p);
					if (dist > r) continue;
	
					range.x = i;
					
					for (i32 i2 = i + 1; i2 < size + 1; ++i2) {
						const float d = planeDist(planes[i2], p);
						if (d < -r) {
							range.y = i2;
							return range;
						}
					}
					range.y = size;
					return range;
				}
				return range;
			};
	
			// TODO tighter fit
			auto for_each_light_pair = [&](auto f){
				for (i32 i = 0, c = lights_count; i < c; ++i) {
					ClusterLight& light = lights[i];
					const float r = light.radius;
					const Vec3 p = light.pos;
	
					const IVec2 xrange = range(p, r, size.x, xplanes);
					const IVec2 yrange = range(p, r, size.y, yplanes);
					const IVec2 zrange = range(p, r, size.z, zplanes);
	
					for (i32 z = zrange.x; z < zrange.y; ++z) {
						for (i32 y = yrange.x; y < yrange.y; ++y) {
							for (i32 x = xrange.x; x < xrange.y; ++x) {
								const u32 idx = x + y * size.x + z * size.x * size.y;
								Cluster& cluster = clusters[idx];
								f(cluster, i);
							}
						}
					}
				}
			};
		
			auto for_each_env_probe_pair = [&](auto f){
				for (i32 i = 0, c = module_env_probes.length(); i < c; ++i) {
					const Vec3 p = env_probes[i].pos;
					const float r = length(env_probes[i].outer_range);
			
					const IVec2 xrange = range(p, r, size.x, xplanes);
					const IVec2 yrange = range(p, r, size.y, yplanes);
					const IVec2 zrange = range(p, r, size.z, zplanes);
	
					for (i32 z = zrange.x; z < zrange.y; ++z) {
						for (i32 y = yrange.x; y < yrange.y; ++y) {
							for (i32 x = xrange.x; x < xrange.y; ++x) {
								const u32 idx = x + y * size.x + z * size.x * size.y;
								Cluster& cluster = clusters[idx];
								f(cluster, i);
							}
						}
					}
				}
			};
	
			auto for_each_refl_probe_pair = [&](auto f){
				for (i32 i = 0, c = module_refl_probes.length(); i < c; ++i) {
					const Vec3 p = refl_probes[i].pos;
					const float r = length(refl_probes[i].half_extents);
			
					const IVec2 xrange = range(p, r, size.x, xplanes);
					const IVec2 yrange = range(p, r, size.y, yplanes);
					const IVec2 zrange = range(p, r, size.z, zplanes);
	
					for (i32 z = zrange.x; z < zrange.y; ++z) {
						for (i32 y = yrange.x; y < yrange.y; ++y) {
							for (i32 x = xrange.x; x < xrange.y; ++x) {
								const u32 idx = x + y * size.x + z * size.x * size.y;
								Cluster& cluster = clusters[idx];
								f(cluster, i);
							}
						}
					}
				}
			};
	
			for_each_light_pair([](Cluster& cluster, i32 light_idx){
				++cluster.lights_count;
			});
	
			for_each_env_probe_pair([](Cluster& cluster, i32){
				++cluster.env_probes_count;
			});
	
			for_each_refl_probe_pair([](Cluster& cluster, i32){
				++cluster.refl_probes_count;
			});
	
			u32 offset = 0;
			for (u32 i = 0; i < clusters_count; ++i) {
				Cluster& cluster = clusters[i];
				cluster.offset = offset;
				offset += cluster.lights_count + cluster.env_probes_count + cluster.refl_probes_count;
			}
		
			i32* map = (i32*)frame_allocator.allocate(offset * sizeof(i32), alignof(i32));
	
			for_each_light_pair([&](Cluster& cluster, i32 light_idx){
				map[cluster.offset] = light_idx;
				++cluster.offset;
			});
	
			for_each_env_probe_pair([&](Cluster& cluster, i32 probe_idx){
				map[cluster.offset] = probe_idx;
				++cluster.offset;
			});
	
			for_each_refl_probe_pair([&](Cluster& cluster, i32 probe_idx){
				map[cluster.offset] = probe_idx;
				++cluster.offset;
			});
	
			for (u32 i = 0; i < clusters_count; ++i) {
				Cluster& cluster = clusters[i];
				cluster.offset -= cluster.lights_count + cluster.env_probes_count + cluster.refl_probes_count;
			}
		
			bind(m_cluster_buffers.lights, lights, lights_count * sizeof(lights[0]), stream, "lights");
			bind(m_cluster_buffers.clusters, clusters, sizeof(clusters[0]) * clusters_count, stream, "clusters");
			bind(m_cluster_buffers.maps, map, offset * sizeof(i32), stream, "cluster_map");
			bind(m_cluster_buffers.env_probes, env_probes, module_env_probes.length() * sizeof(env_probes[0]), stream, "env_probes");
			bind(m_cluster_buffers.refl_probes, refl_probes, module_refl_probes.length() * sizeof(refl_probes[0]), stream, "refl_probes");
			gpu::BufferHandle sbs[] = {
				m_cluster_buffers.lights.buffer,
				m_cluster_buffers.clusters.buffer,
				m_cluster_buffers.maps.buffer,
				m_cluster_buffers.env_probes.buffer,
				m_cluster_buffers.refl_probes.buffer,
				m_renderer.getMaterialUniformBuffer()
			};
			const u32 strides[] = {
				sizeof(ClusterLight),
				sizeof(Cluster),
				sizeof(i32),
				sizeof(ClusterEnvProbe),
				sizeof(ClusterReflProbe),
				0 // material uniform buffer, not a StructuredBuffer
			};
			stream.bindShaderBuffers(sbs, strides);
		});
	}

	void refreshMaterialOverrides() {
		Span<ModelInstance> instances = m_module->getModelInstances();
		for (EntityRef e : m_material_override_refresh_queue) {
			ModelInstance& mi = instances[e.index];
			if (!mi.dirty) continue;
			
			ASSERT(mi.model->isReady());
			const bool has_override = mi.mesh_materials.begin() != mi.model->getMeshMaterials().begin();
			if (!has_override) {
				mi.dirty = false;
				continue;
			}

			bool updated = true;
			for (u32 i = 0; i < mi.mesh_count; ++i) {
				MeshMaterial& mat = mi.mesh_materials[i];
				if (!mat.material) {
					mat.material = mi.model->getMeshMaterial(i).material;
					mat.material->incRefCount();
				}
				if (!mat.material->isReady()) {
					updated = false;
					break;
				}
				
				if (mat.sort_key == 0) {
					mat.sort_key = m_module->computeSortKey(*mat.material, mi.meshes[i]);
				}
				if ((u32)mat.material_index == 0) {
					mat.material_index = mat.material->getIndex();
					mat.flags = mat.flags & ~MeshMaterial::OWN_MATERIAL_INDEX;
				}
			}

			if (updated) mi.dirty = false;
		}
		m_material_override_refresh_queue.clear();
	}

	void queueMaterialOverrideRefresh(EntityRef e) {
		jobs::MutexGuard guard(m_material_override_refresh_mutex);
		m_material_override_refresh_queue.push(e);
	}

	struct PoseProcessor {
		struct Batch {
			u32 count = 0;
			ModelInstance* elements[128];
			PipelineImpl* pipeline;
		};

		PipelineImpl& pipeline;
		ArenaAllocator& allocator;
		Batch* batch = nullptr;

		PoseProcessor(PipelineImpl& pipeline, ArenaAllocator& allocator)
			: allocator(allocator)
			, pipeline(pipeline)
		{}

		~PoseProcessor() {
			if (batch) processBatch();
		}

		void push(ModelInstance* mi) {
			if (!batch) {
				batch = LUMIX_NEW(allocator, Batch);
				batch->pipeline = &pipeline;
			}

			batch->elements[batch->count] = mi;
			++batch->count;
			if (batch->count == lengthOf(batch->elements)) {
				processBatch();
			}
		}

		void processBatch() {
			jobs::run(batch, [](void* ptr){
				PROFILE_BLOCK("compute gpu pose");
				Batch* batch = (Batch*)ptr;
				u32 num_bones = 0;
				for (u32 i = 0; i < batch->count; ++i) {
					num_bones += batch->elements[i]->pose->count;
				}

				profiler::pushInt("bones", num_bones);
				TransientPool& pool = batch->pipeline->m_renderer.getTransientPool();
				TransientSlice slice = alloc(pool, num_bones * sizeof(DualQuat)); 
				u32 offset = 0;
				for (u32 i = 0; i < batch->count; ++i) {
					ModelInstance& mi = *batch->elements[i];
					mi.pose->slice = slice;
					mi.pose->slice.ptr += offset;
					mi.pose->slice.offset += offset;
					batch->pipeline->computeSkeletonDualQuats(&mi);
					offset += mi.pose->count * sizeof(DualQuat);
				}
			}, &pipeline.m_poses_done);
			batch = nullptr;
		}
	};

	void createSortKeys(PipelineImpl::View& view) {
		PagedListIterator<const CullResult> iterator(view.renderables);

		view.instancers.reserve(jobs::getWorkersCount());
		ArenaAllocator& allocator = m_renderer.getCurrentFrameAllocator();
		for (u8 i = 0; i < jobs::getWorkersCount(); ++i) {
			view.instancers.emplace(allocator, m_renderer.getEngine().getPageAllocator());
			view.cluster_instances.emplace(allocator);
		}

		const float global_lod_multiplier = m_renderer.getLODMultiplier();
		const float global_lod_multiplier_rcp = 1 / global_lod_multiplier;
		const float time_delta = m_renderer.getEngine().getLastTimeDelta();
		AtomicI32 worker_idx = 0;

		u32 bucket_map[255];
		for (u32 i = 0; i < 255; ++i) {
			bucket_map[i] = view.layer_to_bucket[i];
			if (bucket_map[i] == 0xff) {
				bucket_map[i] = 0xffFFffFF;
			}
			else if (view.buckets[bucket_map[i]].sort == BucketDesc::DEPTH) {
				bucket_map[i] |= 0x100;
			}
		}
		TransientPool& transient_pool = m_renderer.getTransientPool();
		u32 frame_number = m_renderer.frameNumber() % 0xffFFffFF; // we keep 0xffFFffFF to mark invalid data

		jobs::runOnWorkers([&](){
			PROFILE_BLOCK("create keys");
			
			PoseProcessor pose_processor(*this, m_renderer.getCurrentFrameAllocator());

			int total = 0;
			ModelInstance* LUMIX_RESTRICT model_instances = m_module->getModelInstances().begin();
			const Transform* LUMIX_RESTRICT transforms = m_module->getWorld().getTransforms();
			const DVec3 camera_pos = view.cp.pos;
			const DVec3 lod_ref_point = m_viewport.pos;
			Sorter::Inserter inserter(view.sorter);

			const i32 instancer_idx = worker_idx.inc();
			AutoInstancer& instancer = view.instancers[instancer_idx];
			instancer.init(m_renderer.getMaxSortKey() + 1);

			for(;;) {
				const CullResult* page = iterator.next();
				if(!page) break;
				total += page->header.count;
				const EntityRef* LUMIX_RESTRICT renderables = page->entities;
				const RenderableTypes type = (RenderableTypes)page->header.type;
				
				switch(type) {
					case RenderableTypes::LOCAL_LIGHT: break;
					case RenderableTypes::DECAL: {
						for (int i = 0, c = page->header.count; i < c; ++i) {
							const EntityRef e = renderables[i];
							const Material* material = m_module->getDecal(e).material;
							const int layer = material->getLayer();
							const u8 bucket = bucket_map[layer];
							if (bucket < 0xff) {
								SortKey key = makeDecalSortKey(material, bucket);
								SortValue value = makeDecalSortValue(e);
								inserter.push(key, value);
							}
						}
						break;
					}
					case RenderableTypes::CURVE_DECAL: {
						for (int i = 0, c = page->header.count; i < c; ++i) {
							const EntityRef e = renderables[i];
							const Material* material = m_module->getCurveDecal(e).material;
							const int layer = material->getLayer();
							const u8 bucket = bucket_map[layer];
							if (bucket < 0xff) {
								SortKey key = makeDecalSortKey(material, bucket);
								SortValue value = makeCurveDecalSortValue(e);
								inserter.push(key, value);
							}
						}
						break;
					}
					case RenderableTypes::MESH: {
						const bool is_shadow = view.cp.is_shadow;
						for (int i = 0, c = page->header.count; i < c; ++i) {
							const EntityRef e = renderables[i];
							const DVec3 pos = transforms[e.index].pos;
							ModelInstance& mi = model_instances[e.index];

							const float squared_length = float(squaredLength(pos - lod_ref_point));
							const u32 lod_idx = mi.model->getLODMeshIndices(squared_length * global_lod_multiplier_rcp);

							if (mi.dirty) {
								queueMaterialOverrideRefresh(e);
								continue;
							}

							// models with clusters are always drawn with clusters, see encodeModelInstanceClusters
							if (mi.model->hasClusters()) {
								view.cluster_instances[instancer_idx].push(e);
								continue;
							}

							auto create_key = [&](const LODMeshIndices& lod){
								for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
									const MeshMaterial& mesh_mat = mi.mesh_materials[mesh_idx];
									const u32 bucket = bucket_map[mesh_mat.material->getLayer()];
									const u32 mesh_sort_key = mesh_mat.sort_key;
									if (bucket == 0xffFFffFF) continue; // layer is not drawn in this view
									if (mi.meshes[mesh_idx].type == Mesh::SKINNED) {
										// TODO do this only once per model, not for each mesh
										for (;;) {
											u32 frame = (u32)mi.pose->frame;
											if (frame == frame_number) break;
											if (mi.pose->frame.compareExchange(frame_number, frame)) {
												pose_processor.push(&mi);
												break;
											}
										}
										
										SortValue value = makeSkinnedSortValue(e, mesh_idx);
										SortKey key = makeMeshSortKey(mesh_mat, bucket);
										inserter.push(key, value);
									}
									else if (mi.flags & ModelInstance::MOVED && !is_shadow) {
										// moved and unmoved meshes can't be drawn in single drawcall as they need different instance data
										// but autoinstancer groups all instances of a mesh in single drawcall
										// so we don't autoinstance moved meshes, only unmoved
										SortValue value = makeMeshSortValue(e, mesh_idx);
										SortKey key = makeMeshSortKey(mesh_mat, bucket);
										inserter.push(key, value);
									}
									else if (bucket < 0xff) {
										const u64 value = e.index | ((u64)mesh_idx << SORT_VALUE_MESH_IDX_SHIFT);
										instancer.add(mesh_sort_key, value);
									} else if (bucket < 0xffFF) {
										// depth sorted
										const DVec3 pos = transforms[e.index].pos;
										const DVec3 rel_pos = pos - camera_pos;
										const float squared_length = float(rel_pos.x * rel_pos.x + rel_pos.y * rel_pos.y + rel_pos.z * rel_pos.z);
										SortValue value = makeMeshSortValue(e, mesh_idx);
										SortKey key = makeDepthSortKey(squared_length, bucket);
										inserter.push(key, value);
									}
								}
							};

							// first time this instance is seen (e.g. just spawned), use the correct lod without transition
							if (mi.lod < 0) mi.lod = float(lod_idx);

							if (mi.lod != lod_idx) {
								const float d = lod_idx - mi.lod;
								const float ad = fabsf(d);
									
								if (ad <= time_delta) {
									mi.lod = float(lod_idx);
									create_key(mi.model->getLODIndices()[lod_idx]);
								}
								else {
									if (!is_shadow) mi.lod += d / ad * time_delta;
									const u32 cur_lod_idx = u32(mi.lod);
									create_key(mi.model->getLODIndices()[cur_lod_idx]);
									if (cur_lod_idx < 3) create_key(mi.model->getLODIndices()[cur_lod_idx + 1]);
								}
							}
							else {
								const LODMeshIndices& lod = mi.model->getLODIndices()[lod_idx];
								create_key(lod);
							}
						}
						break;
					}
					case RenderableTypes::PARTICLES:
					case RenderableTypes::COUNT:
						ASSERT(false);
						break;
				}
			}
			profiler::pushInt("count", total);

			for (u32 i = 0, c = (u32)instancer.instances.size(); i < c; ++i) {
				if (!instancer.instances[i].begin) continue;

				const u64 renderable = instancer.instances[i].begin->renderables[0];
				const u32 entity_index = renderable & 0xffFFff;
				const u32 mesh_idx = u32(renderable >> SORT_VALUE_MESH_IDX_SHIFT);
				const u8 layer = model_instances[entity_index].mesh_materials[mesh_idx].material->getLayer();
				const u8 bucket = view.layer_to_bucket[layer];
				SortValue value = makeAutoInstancedSortValue(i, instancer_idx);
				SortKey key = makeAutoInstancedSortKey(i, bucket);
				inserter.push(key, value);
			}

			PROFILE_BLOCK("fill instance data");
			u32 num_instances = 0;
			u32 num_meshes = 0;
			TransientPool& transient_pool = m_renderer.getTransientPool();
			for (AutoInstancer::Instances& instances : instancer.instances) {
				const AutoInstancer::Page::Group* group = instances.begin;
				if (!group) continue;

				++num_meshes;
				const u32 count = instances.end->offset + instances.end->count;
				instances.slice = alloc(transient_pool, count * (3 * sizeof(Vec4)));
				u8* instance_data = instances.slice.ptr;
				const u32 sort_key = u32(&instances - instancer.instances.begin());
				const u64 renderable = instances.begin->renderables[0];
				const u32 entity_index = renderable & 0xffFFff;
				const u32 mesh_idx = u32(renderable >> SORT_VALUE_MESH_IDX_SHIFT);
				const ModelInstance& mi = model_instances[entity_index];
				const Mesh& mesh = mi.model->getMesh(mesh_idx);
				const MeshMaterial& mesh_mat = mi.model->getMeshMaterial(mesh_idx);

				const float mesh_lod = mesh.lod;

				while (group) {
					for (u32 i = 0; i < group->count; ++i) {
						const EntityRef e { (i32)group->renderables[i] };
						const Transform& tr = transforms[e.index];
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const float lod_d = model_instances[e.index].lod - mesh_lod;
						memcpy(instance_data, &tr.rot, sizeof(tr.rot));
						instance_data += sizeof(tr.rot);
						memcpy(instance_data, &lpos, sizeof(lpos));
						instance_data += sizeof(lpos);
						memcpy(instance_data, &lod_d, sizeof(lod_d));
						instance_data += sizeof(lod_d);
						memcpy(instance_data, &tr.scale, sizeof(tr.scale));
						instance_data += sizeof(tr.scale);

						const MaterialIndex material_index = model_instances[e.index].mesh_materials[mesh_idx].material_index;
						memcpy(instance_data, &material_index, sizeof(material_index));
						instance_data += sizeof(material_index);
					}
					group = group->next;
				}
				num_instances += count;
			}
			profiler::pushInt("Instances", num_instances);
			profiler::pushInt("Meshes", num_meshes);
		});
	}

	struct Histogram {
		static constexpr u32 BITS = 11;
		static constexpr u32 SIZE = 1 << BITS;
		static constexpr u32 BIT_MASK = SIZE - 1;
		static constexpr i32 STEP = 512;
		static constexpr i32 NUM_PASSES = 6;
		static constexpr u32 SHIFTS[NUM_PASSES] = {
			0, BITS, BITS * 2, BITS * 3, BITS * 4, BITS * 5
		};

		alignas(16) u32 m_histogram[NUM_PASSES][SIZE];
		jobs::Mutex m_mutex[NUM_PASSES];
		Local<AtomicI32> m_merge_idx[NUM_PASSES];

		void compute(const u64* keys, const u64* values, i32 size) {
			AtomicI32 counter = 0;
			for (u32 i = 0; i < lengthOf(m_merge_idx); ++i) {
				m_merge_idx[i].create(0);
			}

			AtomicI32 worker_idx_counter = 0;
			auto work = [&](){
				i32 worker_idx = worker_idx_counter.inc();
				i32 begin = counter.add(STEP);
				if (begin >= size) return; // other jobs were faster than us
				
				alignas(16) u32 histogram[NUM_PASSES][SIZE];
				PROFILE_BLOCK("compute histogram");
				memset(histogram, 0, sizeof(histogram));
				do {
					const i32 end = minimum(size, begin + STEP);

					for (i32 i = begin; i < end; ++i) {
						u64 key = keys[i];
						
						const u16 index0 = (key /*>> SHIFTS[0]*/) & BIT_MASK;
						const u16 index1 = (key >> SHIFTS[1]) & BIT_MASK;
						const u16 index2 = (key >> SHIFTS[2]) & BIT_MASK;
						const u16 index3 = (key >> SHIFTS[3]) & BIT_MASK;
						const u16 index4 = (key >> SHIFTS[4]) & BIT_MASK;
						 // we don't need the & BIT_MASK here, since we shift 64bit number by 55 bits
						const u16 index5 = u16(key >> SHIFTS[5]) /*& BIT_MASK*/;
						++histogram[0][index0];
						++histogram[1][index1];
						++histogram[2][index2];
						++histogram[3][index3];
						++histogram[4][index4];
						++histogram[5][index5];
					}
					begin = counter.add(STEP);
				} while (begin < size);
				
				// each worker starts at different index to limit contention on the mutex
				for (i32 j = worker_idx; j < worker_idx + NUM_PASSES; ++j) {
					i32 pass = j % NUM_PASSES;
					jobs::MutexGuard lock(m_mutex[pass]);
					if (m_merge_idx[pass]->inc() == 0) { 
						memcpy(m_histogram[pass], histogram[pass], sizeof(m_histogram[pass]));
					}
					else {
						for (u32 i = 0; i < SIZE; i += 4) {
							int4 a = i4Load(&m_histogram[pass][i]);
							int4 b = i4Load(&histogram[pass][i]);
							int4 c = i4Add(a, b);
							i4Store(&m_histogram[pass][i], c);
						}
					}
				}
			};

			if (size < STEP) {
				work();
			}
			else {
				jobs::runOnWorkers(work);
			}
		}
	};


	void radixSort(u64* _keys, u64* _values, int size) {
		PROFILE_FUNCTION();
		profiler::pushInt("count", size);
		if (size == 0) return;

		Array<u64> tmp_mem(m_allocator);
		tmp_mem.resize(size * 2);

		u64* keys = _keys;
		u64* values = _values;
		u64* tmp_keys = tmp_mem.begin();
		u64* tmp_values = &tmp_mem[size];

		Histogram histogram;
		u16 shift = 0;
		histogram.compute(keys, values, size);

		for (int pass = 0; pass < 6; ++pass) {
			u32 offset = 0;
			for (int i = 0; i < Histogram::SIZE; ++i) {
				const u32 count = histogram.m_histogram[pass][i];
				histogram.m_histogram[pass][i] = offset;
				offset += count;
			}

			if (histogram.m_histogram[pass][1] != size) {
				for (int i = 0; i < size; ++i) {
					const u64 key = keys[i];
					const u16 index = (key >> shift) & Histogram::BIT_MASK;
					const u32 dest = histogram.m_histogram[pass][index]++;
					tmp_keys[dest] = key;
					tmp_values[dest] = values[i];
				}
				swap(tmp_keys, keys);
				swap(tmp_values, values);
			}

			shift += Histogram::BITS;
		}

		if (keys == _keys) {
			memcpy(_keys, keys, size * sizeof(u64));
			memcpy(_values, values, size * sizeof(u64));
		}
	}

	void viewport(int x, int y, int w, int h) override {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.viewport(x, y, w, h);
	}

	PipelineType getType() const override {
		return m_type;
	}

	void beginBlock(const char* name, bool stats = false) override {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.beginProfileBlock(name, 0, stats);
	}

	void endBlock() override {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.endProfileBlock();
	}
	
	bool environmentCastShadows() {
		if (!m_module) return false;
		const EntityPtr env = m_module->getActiveEnvironment();
		if (!env.isValid()) return false;
		return m_module->getEnvironmentCastShadows((EntityRef)env);
	}

	float getRenderToDisplayRatio() const override { return m_render_to_display_scale; }
	void setRenderToDisplayRatio(float scale) override { m_render_to_display_scale = maximum(scale, 1.f); }

	void clearDraw2D() override { return m_draw2d.clear(getAtlasSize()); }
	Draw2D& getDraw2D() override { return m_draw2d; }
	
	gpu::TextureHandle getOutput() override { 
		return m_renderer.toTexture(m_output);
	}
	
	void setIndirectLightMultiplier(float value) override { m_indirect_light_multiplier = value; }
	
	const IVec2& getDisplaySize() const override { return m_display_size; }

	IAllocator& m_allocator;
	Renderer& m_renderer;
	PipelineType m_type;
	Vec3 m_clear_color = Vec3(0, 0, 0);
	Array<gpu::StateFlags> m_render_states;
	RenderModule* m_module;
	Draw2D m_draw2d;
	Shader* m_blit_shader = nullptr;
	Shader* m_draw2d_shader = nullptr;
	Shader* m_downscale_depth_shader = nullptr;
	gpu::ProgramHandle m_blit_screen_program = gpu::INVALID_PROGRAM;
	Array<UniquePtr<View>> m_views;
	jobs::Signal m_buckets_ready;
	Viewport m_viewport;
	bool m_is_pixel_jitter_enabled = false;
	Viewport m_prev_viewport;
	IVec2 m_display_size;
	float m_render_to_display_scale = 1;
	float m_indirect_light_multiplier = 1;
	bool m_first_set_viewport = true;
	RenderBufferHandle m_output = INVALID_RENDERBUFFER;
	RenderBufferHandle m_downscaled_depth = INVALID_RENDERBUFFER;
	Shader* m_debug_shape_shader;
	Shader* m_instancing_shader;
	Shader* m_cluster_shader;
	Shader* m_flatten_shader;
	Array<gpu::TextureHandle> m_textures;
	Array<gpu::BufferHandle> m_buffers;
	os::Timer m_timer;
	AtomicI32 m_indirect_buffer_offset = 0;
	gpu::BufferHandle m_indirect_buffer;

	// buffers of cluster rendered instanced models, see encodeClusterModel
	// every (mesh, view) pair uses its own region, a part of both buffers
	enum : u32 {
		CLUSTER_INSTANCE_SIZE = 96,					// bytes per selected cluster, see nanite.hlsl
		CLUSTER_SLOTS = 1 << 19,					// selected clusters per frame, all views
		CLUSTER_SLOTS_SMALL = 1 << 16,				// the same for previews and other small pipelines
		CLUSTER_THREADS = 64,						// threads of a group of nanite.hlsl
		CLUSTER_FLAT_MAX = 512,						// meshes with up to that many clusters are tested cluster by cluster, with no tree
		CLUSTER_SCRATCH = 1 << 22,					// u32 in the scratch buffer of the lists of nodes, scene and game views
		CLUSTER_SCRATCH_SMALL = 1 << 18,			// previews and other small pipelines
		CLUSTER_MAX_PER_REGION = 1 << 17,			// clusters over this are silently dropped, which shows as holes
		CLUSTER_REGIONS = 1024,
		CLUSTER_STATS_INTERVAL = 8,					// the cluster counters are sampled, one readback of all regions per sampled frame
		CLUSTER_COMMANDS_OFFSET = 3 * CLUSTER_REGIONS * sizeof(u32) // counters (of selected clusters, of clusters culled by cones, of visited nodes and clusters) are stored before draw commands
	};
	u32 m_cluster_slots = 0;
	bool m_cluster_draw_logged = false;
	bool m_cluster_shader_failure_logged = false;
	AtomicI32 m_cluster_slot_offset = 0;
	AtomicI32 m_cluster_region_offset = 0;
	AtomicI32 m_cluster_frame = 0;
	bool m_cluster_sampled = false;					// the current frame records the regions for pushClusterReadback
	ClusterRegion m_cluster_regions[CLUSTER_REGIONS];	// of the sampled frame, indexed by region
	ClusterStats m_cluster_stats;
	gpu::BufferHandle m_cluster_instances = gpu::INVALID_BUFFER;	// per cluster instance data, used as vertex buffer
	gpu::BufferHandle m_cluster_indirect = gpu::INVALID_BUFFER;		// counters and draw commands
	gpu::BufferHandle m_cluster_scratch = gpu::INVALID_BUFFER;		// the lists of nodes that the culling of clusters visits, see nanite.hlsl
	u32 m_cluster_scratch_capacity = 0;								// in u32
	bool m_cluster_frozen = false;									// the camera that chooses the clusters is m_cluster_frozen_cp, see m_cluster_freeze_camera
	CameraParams m_cluster_frozen_cp;
	float m_cluster_frozen_height = 1.f;
	AtomicI32 m_cluster_scratch_offset = 0;
	// no attributes, vertices of debug shapes are pulled
	gpu::VertexDecl m_base_vertex_decl;
	gpu::VertexDecl m_base_line_vertex_decl;
	gpu::VertexDecl m_2D_decl; // no attributes, vertices of draw2d are pulled
	gpu::VertexDecl m_decal_decl; // no attributes, decals pull everything
	const char* m_cube_pulling_defines = "";
	gpu::BufferHandle m_cube_vb;
	gpu::BufferHandle m_cube_ib;
	ShadowAtlas m_shadow_atlas;
	CameraParams m_custom_camera_params;
	HashMap<u32, void*> m_instance_data;
	JobsGroup m_sort_keys_group;
	jobs::Counter m_poses_done;
	u8 m_autoinstanced_define_idx;
	u8 m_dynamic_define_idx;
	u8 m_skinned_define_idx;
	u8 m_mesh_particle_define_idx;

	struct {
		struct Buffer {
			gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
			u32 capacity = 0;
		};
		Buffer lights;
		Buffer clusters;
		Buffer maps;
		Buffer env_probes;
		Buffer refl_probes;
	} m_cluster_buffers;
	Viewport m_shadow_camera_viewports[4];
	GlobalState m_global_state;
	Array<EntityRef> m_material_override_refresh_queue;
	jobs::Mutex m_material_override_refresh_mutex;
};


UniquePtr<Pipeline> Pipeline::create(Renderer& renderer, PipelineType type)
{
	return UniquePtr<PipelineImpl>::create(renderer.getAllocator(), renderer, type, renderer.getAllocator());
}


} // namespace Lumix

