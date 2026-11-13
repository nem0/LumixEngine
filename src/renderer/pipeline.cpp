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
#include "gpu_scene.h"
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
// depth bits 31 - 0; if bucket is depth sorted
// mesh key 31 - 8; if bucket is not depth sorted

static constexpr u64 SORT_KEY_BUCKET_SHIFT = 56;

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

void bindPulledBuffers(DrawStream& stream, UniformPool& pool, gpu::BufferHandle vertices, gpu::BufferHandle instances, u32 instances_offset, gpu::BufferHandle bones) {
	const Mesh::VertexBufferBinding binding = {
		gpu::getBindlessHandle(vertices).value,
		instances ? gpu::getBindlessHandle(instances).value : 0,
		instances_offset,
		bones ? gpu::getBindlessHandle(bones).value : 0
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

	// The uniforms of nanite.hlsl
	struct ClusterViewUniforms {
		Vec4 planes[6];
		Vec4 cull_offset;		// camera of the culling - camera of the drawing, added to the positions that culling tests
		float lod_k;
		u32 is_ortho;
		float attribute_weight;
		u32 tree_depth;
		gpu::BindlessHandle items;
		u32 items_offset;
		u32 item_capacity;
		gpu::RWBindlessHandle scratch;
		u32 scratch_size;
		u32 selected_capacity;
		gpu::RWBindlessHandle selected;
		gpu::BindlessHandle counts;
		i32 cam_p[3];			// the camera in the fixed point of the GPU scene
		u32 padding;
		float cam_frac[3];
		gpu::BindlessHandle meshes;
		gpu::BindlessHandle scene_instances;
		gpu::BindlessHandle groups;		// the groups of instanced models of the GPU scene
		gpu::BindlessHandle cluster_data;	// the buffer with clusters and nodes of all meshes
		u32 padding1;
		gpu::RWBindlessHandle feedback;	// streaming of clusters, see Renderer::getClusterFeedbackBuffer
		u32 frame;						// the stamp of this frame, ids are appended to the feedback once per frame
		u32 feedback_capacity;			// ids that the feedback accepts, 0: no feedback
		u32 padding2;
	};
	static_assert(sizeof(ClusterViewUniforms) % 16 == 0);

	struct View {
		View(ArenaAllocator& allocator, PageAllocator& page_allocator) 
			: sorter(allocator, page_allocator)
			, buckets(allocator)
		{}
		~View() {
			ASSERT((ready.state & 1) == 0);
		}

		View(View&& rhs) = delete;
		View(const View& rhs) = delete;
		void operator=(View&&) = delete;

		Array<Bucket> buckets;
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
		, m_instance_data(m_allocator)
	{
		m_viewport.w = m_viewport.h = 800;
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_blit_shader = rm.load<Shader>(Path("engine/shaders/blit.hlsl"));
		m_draw2d_shader = rm.load<Shader>(Path("engine/shaders/draw2d.hlsl"));
		m_downscale_depth_shader = rm.load<Shader>(Path("engine/shaders/downscale_depth.hlsl"));
		m_debug_shape_shader = rm.load<Shader>(Path("engine/shaders/debug_shape.hlsl"));
		m_cluster_shader = rm.load<Shader>(Path("engine/shaders/nanite.hlsl"));
		m_scene_cull_shader = rm.load<Shader>(Path("engine/shaders/gpu_scene_cull.hlsl"));
		m_scene_variants_shader = rm.load<Shader>(Path("engine/shaders/gpu_scene_variants.hlsl"));
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

		createClusterBuffers();



		// decals pull vertices of the cube (float3 positions) and their instances, see decal.hlsl
		const AttributeSemantic cube_semantics[] = { AttributeSemantic::POSITION };
		VertexDecl cube_decl;
		cube_decl.addAttribute(0, 3, AttributeType::FLOAT, 0);
		m_cube_pulling_defines = m_renderer.getSemanticDefines(Span(cube_semantics), cube_decl, 12);


		if (m_type == PipelineType::PREVIEW) m_clear_color = { 0.2f, 0.2f, 0.2f };

		m_mesh_instance_define_idx = m_renderer.getShaderDefineIdx("MESH_INSTANCE");
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
		m_cluster_shader->decRefCount();
		m_scene_cull_shader->decRefCount();
		m_scene_variants_shader->decRefCount();
		m_flatten_shader->decRefCount();

		stream.destroy(m_cube_ib);
		stream.destroy(m_cube_vb);
		if (m_cluster_scratch) stream.destroy(m_cluster_scratch);
		if (m_cluster_iota) stream.destroy(m_cluster_iota);
		if (m_scene_cull_counter) stream.destroy(m_scene_cull_counter);
		if (m_scene_cull_args) stream.destroy(m_scene_cull_args);
		if (m_scene_flat_items) stream.destroy(m_scene_flat_items);
		if (m_scene_tree_items) stream.destroy(m_scene_tree_items);
		if (m_scene_selected) stream.destroy(m_scene_selected);
		if (m_scene_counters) stream.destroy(m_scene_counters);
		if (m_scene_commands) stream.destroy(m_scene_commands);
		if (m_scene_out) stream.destroy(m_scene_out);
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

		const HashMap<EntityRef, InstancedModel>& ims = m_module->getInstancedModels();
		for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
			if (iter.value().dirty) {
				m_module->initInstancedModelGPUData(iter.key());
			}
		}

		View* view_ptr = view.get();
		jobs::turnRed(&view->ready);

		m_sort_keys_group.beginJob();
		m_renderer.pushJob("prepare view", [this, view_ptr](DrawStream& stream) {
			setupParticles(*view_ptr);
			encodeProceduralGeometry(*view_ptr);

			updateClusterFreeze(*view_ptr);
			// with the frozen camera the instances of its frustum are culled, as the clusters are
			view_ptr->renderables = m_module->getRenderables(!view_ptr->cp.is_shadow && m_cluster_frozen ? m_cluster_frozen_cp.frustum : view_ptr->cp.frustum);

			// Model instances that the GPU scene can draw (meshes with clusters or with buffers of their own, skinned too) and instanced models with clusters are
			// culled and drawn by the GPU. They are not in the culling system, so this does not depend on its result (it is empty if there are no lights or decals in the view).
			runGPUSceneCulling(stream, *view_ptr);

			if (view_ptr->renderables) {
				if (view_ptr->renderables->header.count != 0 || view_ptr->renderables->header.next) {
					createSortKeys(*view_ptr);
				}
				view_ptr->renderables->free(m_renderer.getEngine().getPageAllocator());
				view_ptr->renderables = nullptr;
			}
			m_sort_keys_group.endJob();
			view_ptr->sorter.pack();
				
			if (!view_ptr->sorter.keys.empty()) {
				radixSort(view_ptr->sorter.keys.begin(), view_ptr->sorter.values.begin(), view_ptr->sorter.keys.size());
				// Wait for all createSortKeys jobs to finish
				m_sort_keys_group.wait(); 
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
		const gpu::ProgramHandle program = shader.getProgram(state, gpu::PrimitiveType::TRIANGLE_STRIP, define_mask, "");
		stream.useProgram(program);
		stream.bindIndexBuffer(gpu::INVALID_BUFFER);
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

			m_blit_screen_program = gpu::allocProgramHandle();
			stream.createProgram(m_blit_screen_program, gpu::StateFlags::NONE, gpu::PrimitiveType::TRIANGLE_STRIP, src, gpu::ShaderType::SURFACE, nullptr, 0, "blit to screen");
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
		m_module->flushGPUScene();	// resolves the material overrides that became ready too
		ensureSceneWorkBuffers();
		prepareSkinnedPoses();

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
		m_scene_view_count = 0;
		pushSceneDrawClear();

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
			const gpu::ProgramHandle program = m_debug_shape_shader->getProgram(state, gpu::PrimitiveType::TRIANGLES, 0, "");
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
			const gpu::ProgramHandle program = m_debug_shape_shader->getProgram(state, gpu::PrimitiveType::LINES, 0, "");
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
		const gpu::ProgramHandle program = m_draw2d_shader->getProgram(state, gpu::PrimitiveType::TRIANGLES, 0, "");

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
				const gpu::ProgramHandle program = shader->getProgram(state | terrain->m_material->m_render_states, gpu::PrimitiveType::TRIANGLE_STRIP, define_mask | terrain->m_material->getDefineMask(), "");
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
			const u32 mesh_instance_define = 1 << m_mesh_instance_define_idx;
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
					if (!mesh.index_buffer_handle) continue; // cluster meshes have no plain index buffer

					Material* material = mesh_mat.material;
					u32 mask = material->getDefineMask() | define_mask;
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
						// one instance with the layout of the MESH_INSTANCE variant of surface shaders (the previous state is the current one)
						struct DynamicInstanceData {
							Quat rot;
							Vec3 pos;
							float lod;
							Vec3 scale;
							float padding;
							Quat prev_rot;
							Vec3 prev_pos;
							float prev_lod;
							Vec3 prev_scale;
							MaterialIndex material_index;
						};
						static_assert(sizeof(DynamicInstanceData) == 96);

						const Transform tr = world.getTransform(e);
						TransientSlice inst_buf = alloc(transient_pool, sizeof(DynamicInstanceData));
						DynamicInstanceData* idata = (DynamicInstanceData*)inst_buf.ptr;
						idata->rot = idata->prev_rot = tr.rot;
						idata->pos = idata->prev_pos = Vec3(tr.pos - view_pos);
						idata->scale = idata->prev_scale = tr.scale;
						idata->lod = idata->prev_lod = 0;
						idata->padding = 0;
						idata->material_index = material->getIndex();

						mask |= mesh_instance_define;
						// instance data is pulled by the vertex shader
						bindPulledBuffers(stream, mesh.vertex_buffer_handle, inst_buf.buffer, inst_buf.offset);
						gpu::ProgramHandle program = mesh_mat.material->getShader()->getProgram(material->m_render_states | state, mesh.primitive_type, mask, mesh.semantics_defines);
						stream.useProgram(program);
					}
					else {
						// the layout of the MESH_INSTANCE variant, the word after the scale is the offset of the bones in the buffer of bones of the draw
						struct SkinnedInstanceData {
							Quat rot;
							Vec3 pos;
							float lod;
							Vec3 scale;
							u32 bones_offset;
							Quat prev_rot;
							Vec3 prev_pos;
							float prev_lod;
							Vec3 prev_scale;
							MaterialIndex material_index;
						};
						static_assert(sizeof(SkinnedInstanceData) == 96);

						TransientSlice inst_buf = alloc(transient_pool, sizeof(SkinnedInstanceData));
						TransientSlice bones = alloc(transient_pool, dq_pose.byte_size());
						SkinnedInstanceData* idata = (SkinnedInstanceData*)inst_buf.ptr;

						Transform tr = world.getTransform(e);
						idata->pos = idata->prev_pos = Vec3(tr.pos - view_pos);
						idata->rot = idata->prev_rot = tr.rot;
						idata->scale = idata->prev_scale = tr.scale;
						idata->lod = idata->prev_lod = 0;
						idata->bones_offset = bones.offset;
						idata->material_index = material->getIndex();

						memcpy(bones.ptr, dq_pose.begin(), dq_pose.byte_size());
						// instance data is pulled by the vertex shader (SKINNED is in the mask, it has the layout of MESH_INSTANCE, but must not have the define)
						bindPulledBuffers(stream, mesh.vertex_buffer_handle, inst_buf.buffer, inst_buf.offset, bones.buffer);
						gpu::ProgramHandle program = mesh_mat.material->getShader()->getProgram(material->m_render_states | state, mesh.primitive_type, mask, mesh.semantics_defines);
						stream.useProgram(program);
					}

					stream.bindIndexBuffer(mesh.index_buffer_handle);
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

 
						gpu::ProgramHandle program = shader->getProgram(state | material->m_render_states, mesh.primitive_type, define_mask | material->getDefineMask(), mesh.semantics_defines);
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
							bindPulledBuffers(stream, mesh.vertex_buffer_handle, quad.instances, 0);
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
			const gpu::ProgramHandle program = pg.material->getShader()->getProgram(state, pg.primitive_type, bucket.define_mask | pg.material->getDefineMask(), "");

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

	void createClusterBuffers() {
		// views of the editor can show many models, previews show one
		const bool large = m_type == PipelineType::SCENE_VIEW || m_type == PipelineType::GAME_VIEW;
		m_cluster_scratch_capacity = large ? CLUSTER_SCRATCH : CLUSTER_SCRATCH_SMALL;
		const Renderer::MemRef scratch_mem = { m_cluster_scratch_capacity * sizeof(u32), nullptr, false };
		m_cluster_scratch = m_renderer.createBuffer(scratch_mem, gpu::BufferFlags::SHADER_BUFFER, "cluster scratch");

		// 0, 1, 2, ...: the index buffer of the draws of clusters, see pullVertex in vertex_pulling.hlsli
		Renderer::MemRef iota_mem = m_renderer.allocate(65536 * sizeof(u16));
		u16* iota = (u16*)iota_mem.data;
		for (u32 i = 0; i < 65536; ++i) iota[i] = (u16)i;
		m_cluster_iota = m_renderer.createBuffer(iota_mem, gpu::BufferFlags::IMMUTABLE, "cluster iota");

		// the counters of the work items: [1] flat items, [2] tree items
		const Renderer::MemRef counter_mem = { 16, nullptr, false };
		m_scene_cull_counter = m_renderer.createBuffer(counter_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene cull counter");
		const Renderer::MemRef args_mem = { 64, nullptr, false };
		m_scene_cull_args = m_renderer.createBuffer(args_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene cull args");
	}

	// Lists of the work of clusters that the culling of instances generates, 16 bytes each. Buffers are created into the draw stream of the renderer
	// in the order of calls, so this must be called when the frame is set up, not from a job (the job would use the buffers before they exist).
	void ensureSceneWorkBuffers() {
		if (m_scene_flat_items) return;
		const bool large = m_type == PipelineType::SCENE_VIEW || m_type == PipelineType::GAME_VIEW;	// previews show one model
		m_scene_work_items = large ? SCENE_WORK_ITEMS : SCENE_WORK_ITEMS_SMALL;
		m_scene_selected_capacity = large ? SCENE_SELECTED : SCENE_SELECTED_SMALL;
		m_scene_draw_slots = large ? SCENE_DRAW_SLOTS : SCENE_DRAW_SLOTS_SMALL;
		const Renderer::MemRef items_mem = { m_scene_work_items * 16, nullptr, false };
		m_scene_flat_items = m_renderer.createBuffer(items_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene flat items");
		m_scene_tree_items = m_renderer.createBuffer(items_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene tree items");
		const Renderer::MemRef selected_mem = { 16 + m_scene_selected_capacity * 16, nullptr, false };
		m_scene_selected = m_renderer.createBuffer(selected_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene selected clusters");
		const Renderer::MemRef counters_mem = { 64 + GPUSceneConst::MAX_VARIANTS * 12, nullptr, false };
		m_scene_counters = m_renderer.createBuffer(counters_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene variant counters");
		const Renderer::MemRef commands_mem = { SCENE_VIEWS * GPUSceneConst::MAX_VARIANTS * 24, nullptr, false };
		m_scene_commands = m_renderer.createBuffer(commands_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene commands");
		const Renderer::MemRef out_mem = { m_scene_draw_slots * 96, nullptr, false };
		m_scene_out = m_renderer.createBuffer(out_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene draw instances");
	}


	struct SceneVariantsUniforms {
		i32 cam_p[3];
		u32 selected_capacity;
		float cam_frac[3];
		u32 out_capacity;
		gpu::RWBindlessHandle selected;
		gpu::BindlessHandle instances;
		gpu::RWBindlessHandle counters;
		gpu::RWBindlessHandle commands;
		gpu::BindlessHandle meshes;
		gpu::BindlessHandle variants;
		u32 variant_count;
		gpu::BindlessHandle group_base;
		u32 group_base_offset;
		gpu::RWBindlessHandle out;
		gpu::RWBindlessHandle args;
		gpu::BindlessHandle groups;
		gpu::BindlessHandle bone_table;
		gpu::BindlessHandle material_sets;
		u32 padding1;
		u32 command_base;
		u32 bone_table_offset;
		u32 padding[3];
	};
	static_assert(sizeof(SceneVariantsUniforms) == 112);

	// At the start of a frame all the instances of the draws of the GPU scene are free, the views of the frame take them one after another.
	void pushSceneDrawClear() {
		if (!m_scene_counters || !m_scene_variants_shader) return;
		m_renderer.pushJob("clear scene draws", [this](DrawStream& stream) {
			if (!m_scene_variants_shader->isReady()) return;
			SceneVariantsUniforms ub;
			memset(&ub, 0, sizeof(ub));
			ub.counters = gpu::getRWBindlessHandle(m_scene_counters);
			const TransientSlice ub_slice = alloc(m_renderer.getUniformPool(), &ub, sizeof(ub));
			stream.barrier(m_scene_counters, gpu::BarrierType::WRITE);
			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_slice.buffer, ub_slice.offset, sizeof(ub));
			stream.useProgram(m_scene_variants_shader->getProgram(1 << m_renderer.getShaderDefineIdx("FRAME_CLEAR")));
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(m_scene_counters);
		});
	}

	// Says which draw groups of the scene the view draws (the ones with a layer that has a bucket in the view) and where their commands are: the commands of the
	// groups that are drawn follow each other. The result is a u32 for each draw group, its first command in this view or 0xffffffff if the view does not draw it.
	// The variants, the table of meshes and the material sets do not depend on the view, they are in the GPU scene. The buffer of the result is invalid if there is
	// nothing to draw.
	TransientSlice makeSceneViewGroups(View& view, GPUScene& scene) {
		const u32 group_count = scene.getDrawGroupCount();
		if (!m_scene_variants_shader->isReady() || group_count == 0 || scene.getVariantCount() == 0) return {};

		const TransientSlice slice = alloc(m_renderer.getTransientPool(), group_count * sizeof(u32));
		u32* group_base = (u32*)slice.ptr;
		u32 commands = 0;
		for (u32 g = 0; g < group_count; ++g) {
			const GPUScene::DrawGroup& group = scene.getDrawGroup(g);
			if (view.layer_to_bucket[group.layer] == 0xff) {
				group_base[g] = 0xffFFffFF;
				continue;
			}
			group_base[g] = commands;
			commands += group.variant_count;
		}
		return slice;
	}

	// Converts the poses of all the skinned instances of the GPU scene into dual quaternions once in a frame (all views draw from them): one buffer of bones
	// and a table with the offset of the bones of each slot, see GPUScene::FrameBones. Animation is evaluated by the CPU, the GPU only skins.
	void prepareSkinnedPoses() {
		PROFILE_FUNCTION();
		GPUScene& scene = m_module->getGPUScene();
		GPUScene::FrameBones& frame_bones = scene.getFrameBones();
		const u32 frame = m_renderer.frameNumber();
		if (frame_bones.frame == frame) return;
		frame_bones.frame = frame;
		frame_bones.bones = frame_bones.table = gpu::INVALID_BUFFER;

		struct Item {
			const ModelInstance* mi;
			u32 slot;
			u32 offset;
		};
		Array<Item> items(m_allocator);
		u32 total = 0;
		const Span<const ModelInstance> instances = m_module->getModelInstances();
		for (u32 i = 0, c = instances.length(); i < c; ++i) {
			const ModelInstance& mi = instances[i];
			if (mi.gpu_slot == GPUSceneConst::INVALID_SLOT || !mi.model || !mi.model->isReady() || !mi.pose || mi.pose->count == 0) continue;
			if (!mi.model->hasSkinnedMeshes()) continue;
			items.push({ &mi, mi.gpu_slot, total * (u32)sizeof(DualQuat) });
			total += mi.pose->count;
		}
		if (items.empty()) return;

		TransientPool& pool = m_renderer.getTransientPool();
		const TransientSlice bones = alloc(pool, total * sizeof(DualQuat));
		const u32 table_bytes = maximum(scene.getCapacity(), 1u) * 4;
		const TransientSlice table = alloc(pool, table_bytes);
		memset(table.ptr, 0, table_bytes);
		for (const Item& item : items) {
			if (item.slot < scene.getCapacity()) ((u32*)table.ptr)[item.slot] = bones.offset + item.offset;
		}
		jobs::forEach(items.size(), 1, [&](u32 i, u32){
			PROFILE_BLOCK("compute gpu pose");
			computeSkeletonDualQuats(items[i].mi, (DualQuat*)(bones.ptr + items[i].offset));
		});
		frame_bones.bones = bones.buffer;
		frame_bones.bones_offset = bones.offset;
		frame_bones.table = table.buffer;
		frame_bones.table_offset = table.offset;
	}

	// Culls the instances of the GPU scene for a view, selects their clusters and sorts them into variants on the GPU, and records the draws of the groups. It does
	// it for every view of a frame, these are the only draws of the model instances and instanced models of the GPU scene. If something is not ready (the shaders of the first frames,
	// more than SCENE_VIEWS views in a frame) the instances of the view are not drawn. The main view culls from the frozen camera of the clusters if there is one. Views use the buffers one after another (their passes are executed in the order of the views), what the draws
	// need, which are executed later, is not shared: the instances of the draws are taken from a counter of the frame and the commands of a view are its own.
	void runGPUSceneCulling(DrawStream& stream, View& view) {
		GPUScene& scene = m_module->getGPUScene();
		if (m_cluster_shader->isFailure() && !m_cluster_shader_failure_logged) {
			m_cluster_shader_failure_logged = true;
			logError("Cluster shader nanite.hlsl failed to load, models with clusters are not drawn");
		}
		// nothing is drawn until everything is ready
		if (!m_scene_cull_shader->isReady() || !m_scene_variants_shader->isReady() || !m_cluster_shader->isReady()) return;
		if (!m_scene_cull_counter || !m_scene_cull_args || !scene.isUploaded()) return;
		if (!scene.getModelTableBuffer() || !scene.getMeshTableBuffer() || !scene.getGroupTableBuffer() || !scene.getMaterialSetBuffer() || !scene.getVariantBuffer()) return;
		if (!m_scene_flat_items || !m_scene_tree_items || !m_scene_selected || !m_scene_counters || !m_scene_commands || !m_scene_out) return;	// created by ensureSceneWorkBuffers when the frame is set up
		if (!m_cluster_scratch || !m_cluster_iota) return;

		// the draw groups of the view are known before the clusters are selected
		const TransientSlice group_base = makeSceneViewGroups(view, scene);
		if (!group_base.buffer) return;
		// the draws of a view are executed after the passes of all the views, each view has its own commands
		const i32 view_slot = m_scene_view_count.inc();
		if (view_slot >= SCENE_VIEWS) {
			static bool reported = false;
			if (!reported) logError("GPU scene: more than ", (u32)SCENE_VIEWS, " views in a frame, the model instances of the GPU scene are not drawn in the others");
			reported = true;
			return;
		}
		const u32 command_base = (u32)view_slot * GPUSceneConst::MAX_VARIANTS * 24;

		// The camera that culls the instances and chooses the levels of detail is the frozen one (the main view only) if the camera of the clusters is frozen,
		// the instances are drawn from the camera of the view: their positions are relative to it, the culling adds the difference of the cameras (u_cull_offset)
		const bool frozen = !view.cp.is_shadow && m_cluster_frozen;
		const CameraParams& cull_cp = frozen ? m_cluster_frozen_cp : view.cp;
		const GPUSceneView scene_view = makeGPUSceneView(view.cp.pos);
		const GPUSceneView cull_view = frozen ? makeGPUSceneView(cull_cp.pos) : scene_view;
		struct Uniforms {
			Vec4 planes[6];
			i32 cam_p[3];
			float unit;
			float cam_frac[3];
			u32 capacity;
			gpu::BindlessHandle bounds;
			gpu::RWBindlessHandle counter;
			gpu::RWBindlessHandle args;
			u32 selected_capacity;
			gpu::BindlessHandle instances;
			u32 model_count;
			gpu::BindlessHandle models;
			gpu::BindlessHandle meshes;
			gpu::RWBindlessHandle flat_items;
			gpu::RWBindlessHandle tree_items;
			u32 item_capacity;
			u32 padding2;
			gpu::RWBindlessHandle selected;
			gpu::BindlessHandle groups;
			u32 group_count;
			u32 group_instances;
			float lod_ref[3];		// where the LODs of models without clusters are chosen from, relative to the camera of the culling
			float lod_scale;
		};
		static_assert(sizeof(Uniforms) == 208);
		Uniforms ub;
		memset(&ub, 0, sizeof(ub));
		toPlanes(cull_cp, Span(ub.planes));
		memcpy(ub.cam_p, cull_view.cam_p, sizeof(ub.cam_p));
		memcpy(ub.cam_frac, cull_view.cam_frac, sizeof(ub.cam_frac));
		ub.unit = cull_view.unit;
		ub.capacity = scene.getCapacity();
		ub.bounds = gpu::getBindlessHandle(scene.getBoundsBuffer());
		ub.counter = gpu::getRWBindlessHandle(m_scene_cull_counter);
		ub.args = gpu::getRWBindlessHandle(m_scene_cull_args);
		ub.instances = gpu::getBindlessHandle(scene.getInstancesBuffer());
		ub.model_count = minimum(scene.getModelTableSize(), (u32)GPUSceneConst::MAX_MODELS);
		ub.models = gpu::getBindlessHandle(scene.getModelTableBuffer());
		ub.meshes = gpu::getBindlessHandle(scene.getMeshTableBuffer());
		ub.flat_items = gpu::getRWBindlessHandle(m_scene_flat_items);
		ub.tree_items = gpu::getRWBindlessHandle(m_scene_tree_items);
		ub.item_capacity = m_scene_work_items;
		ub.selected = gpu::getRWBindlessHandle(m_scene_selected);
		ub.groups = gpu::getBindlessHandle(scene.getGroupTableBuffer());
		ub.group_count = scene.getGroupCount();
		ub.group_instances = scene.getGroupInstanceCount();
		ub.selected_capacity = m_scene_selected_capacity;
		{
			const Vec3 lod_ref = Vec3(m_viewport.pos - cull_cp.pos);
			ub.lod_ref[0] = lod_ref.x;
			ub.lod_ref[1] = lod_ref.y;
			ub.lod_ref[2] = lod_ref.z;
			ub.lod_scale = 1.f / maximum(m_renderer.getLODMultiplier(), 0.01f);
		}
		const TransientSlice ub_slice = alloc(m_renderer.getUniformPool(), &ub, sizeof(ub));

		// the culling of instances: it clears the counters and writes the groups of its own dispatch, then it generates the work of the clusters
		stream.barrier(scene.getBoundsBuffer(), gpu::BarrierType::READ);
		stream.barrier(scene.getInstancesBuffer(), gpu::BarrierType::READ);
		stream.barrier(scene.getModelTableBuffer(), gpu::BarrierType::READ);
		stream.barrier(scene.getMeshTableBuffer(), gpu::BarrierType::READ);
		stream.barrier(m_scene_flat_items, gpu::BarrierType::WRITE);
		stream.barrier(m_scene_tree_items, gpu::BarrierType::WRITE);
		stream.barrier(m_scene_cull_counter, gpu::BarrierType::WRITE);
		stream.barrier(m_scene_cull_args, gpu::BarrierType::WRITE);
		stream.barrier(m_scene_selected, gpu::BarrierType::WRITE);
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_slice.buffer, ub_slice.offset, sizeof(ub));
		stream.beginProfileBlock("scene instance culling", 0, false);
		stream.useProgram(m_scene_cull_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS0")));
		stream.dispatch(1, 1, 1);
		stream.memoryBarrier(m_scene_cull_counter);
		stream.memoryBarrier(m_scene_cull_args);
		stream.memoryBarrier(m_scene_selected);
		stream.useProgram(m_scene_cull_shader->getProgram(0));
		stream.bindIndirectBuffer(m_scene_cull_args);
		stream.dispatchIndirect(0);
		stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
		stream.memoryBarrier(m_scene_cull_counter);
		stream.memoryBarrier(m_scene_flat_items);
		stream.memoryBarrier(m_scene_tree_items);
		stream.memoryBarrier(m_scene_selected);
		stream.endProfileBlock();

		// the instances of instanced models: one thread for each, they add their work to the same lists
		if (scene.getGroupInstanceCount() > 0) {
			stream.beginProfileBlock("scene group instance culling", 0, false);
			stream.barrier(scene.getGroupTableBuffer(), gpu::BarrierType::READ);
			for (const gpu::BufferHandle instances : scene.getGroupInstanceBuffers()) stream.barrier(instances, gpu::BarrierType::READ);
			stream.useProgram(m_scene_cull_shader->getProgram(1 << m_renderer.getShaderDefineIdx("GROUPS")));
			const u32 thread_groups = (scene.getGroupInstanceCount() + 63) / 64;
			stream.dispatch(minimum(thread_groups, 65535u), (thread_groups + 65534) / 65535, 1);
			stream.memoryBarrier(m_scene_cull_counter);
			stream.memoryBarrier(m_scene_flat_items);
			stream.memoryBarrier(m_scene_tree_items);
			stream.endProfileBlock();
		}

		// the selection of clusters: the groups of its dispatches come from the counters of the work, computed by the GPU
		stream.beginProfileBlock("scene cluster args", 0, false);
		stream.barrier(m_scene_cull_args, gpu::BarrierType::WRITE);
		stream.barrier(m_scene_selected, gpu::BarrierType::WRITE);
		stream.useProgram(m_scene_cull_shader->getProgram(1 << m_renderer.getShaderDefineIdx("ARGS")));
		stream.dispatch(1, 1, 1);
		stream.memoryBarrier(m_scene_cull_args);
		stream.memoryBarrier(m_scene_selected);
		stream.endProfileBlock();

		ClusterViewUniforms cu;
		memset(&cu, 0, sizeof(cu));
		toPlanes(cull_cp, Span(cu.planes));
		cu.cull_offset = frozen ? Vec4(Vec3(view.cp.pos - m_cluster_frozen_cp.pos), 0) : Vec4(0, 0, 0, 0);
		const Matrix& projection = cull_cp.projection;
		const float lod_multiplier = maximum(m_renderer.getLODMultiplier(), 0.01f);
		const float viewport_height = frozen ? m_cluster_frozen_height : (m_viewport.h > 1 ? (float)m_viewport.h : 1.f);
		cu.lod_k = 2.f * maximum(m_cluster_error_pixels * (view.cp.is_shadow ? m_cluster_shadow_error_scale : 1.f), 0.01f) / (viewport_height * maximum(fabsf(projection.columns[1].y), 1e-6f) * lod_multiplier);
		cu.is_ortho = projection.columns[3].w == 1.f ? 1 : 0;
		cu.attribute_weight = maximum(m_cluster_attribute_weight, 0.f);
		cu.scratch = gpu::getRWBindlessHandle(m_cluster_scratch);
		cu.scratch_size = m_cluster_scratch_capacity;	// in u32
		cu.item_capacity = m_scene_work_items;			// of the lists, the counts are in counters
		cu.tree_depth = scene.getMaxTreeDepth();
		memcpy(cu.cam_p, scene_view.cam_p, sizeof(cu.cam_p));
		memcpy(cu.cam_frac, scene_view.cam_frac, sizeof(cu.cam_frac));
		cu.selected_capacity = m_scene_selected_capacity;
		cu.meshes = gpu::getBindlessHandle(scene.getMeshTableBuffer());
		cu.scene_instances = gpu::getBindlessHandle(scene.getInstancesBuffer());
		cu.selected = gpu::getRWBindlessHandle(m_scene_selected);
		cu.counts = gpu::getBindlessHandle(m_scene_cull_counter);
		cu.groups = gpu::getBindlessHandle(scene.getGroupTableBuffer());
		cu.cluster_data = gpu::getBindlessHandle(m_renderer.getClusterDataBuffer());
		const gpu::BufferHandle feedback = m_renderer.getClusterFeedbackBuffer();
		if (feedback) {
			cu.feedback = gpu::getRWBindlessHandle(feedback);
			cu.feedback_capacity = m_renderer.getClusterFeedbackCapacity();
			cu.frame = m_renderer.frameNumber() + 1;
			stream.barrier(feedback, gpu::BarrierType::WRITE);
		}

		stream.barrier(m_renderer.getClusterDataBuffer(), gpu::BarrierType::READ);
		stream.barrier(m_scene_flat_items, gpu::BarrierType::READ);
		stream.barrier(m_scene_tree_items, gpu::BarrierType::READ);
		stream.barrier(m_scene_cull_counter, gpu::BarrierType::READ);
		stream.barrier(m_cluster_scratch, gpu::BarrierType::WRITE);
		stream.bindIndirectBuffer(m_scene_cull_args);

		cu.items = gpu::getBindlessHandle(m_scene_flat_items);
		TransientSlice cu_slice = alloc(m_renderer.getUniformPool(), &cu, sizeof(cu));
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, cu_slice.buffer, cu_slice.offset, sizeof(cu));
		stream.beginProfileBlock("cluster selection flat", 0, false);
		stream.useProgram(m_cluster_shader->getProgram(1 << m_renderer.getShaderDefineIdx("FLAT")));
		stream.dispatchIndirect(16);
		stream.endProfileBlock();

		cu.items = gpu::getBindlessHandle(m_scene_tree_items);
		cu_slice = alloc(m_renderer.getUniformPool(), &cu, sizeof(cu));
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, cu_slice.buffer, cu_slice.offset, sizeof(cu));
		stream.beginProfileBlock("cluster selection tree", 0, false);
		stream.useProgram(m_cluster_shader->getProgram(0));
		stream.dispatchIndirect(32);
		stream.endProfileBlock();

		stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
		stream.memoryBarrier(m_scene_selected);
		stream.memoryBarrier(m_cluster_scratch);

		// Sorts the selected clusters and meshes of the view into variants on the GPU (gpu_scene_variants.hlsl): the GPU counts, assigns the slots of the variants and writes the
		// instance data of the draws and their commands. It also records the draws of the groups the view draws into the buckets of the view, one indirect call with
		// the commands of all the variants of a group.
		{

			SceneVariantsUniforms draw_ub;
			memset(&draw_ub, 0, sizeof(draw_ub));
			memcpy(draw_ub.cam_p, scene_view.cam_p, sizeof(draw_ub.cam_p));
			memcpy(draw_ub.cam_frac, scene_view.cam_frac, sizeof(draw_ub.cam_frac));
			draw_ub.selected_capacity = m_scene_selected_capacity;
			draw_ub.out_capacity = m_scene_draw_slots;
			draw_ub.selected = gpu::getRWBindlessHandle(m_scene_selected);
			draw_ub.instances = gpu::getBindlessHandle(scene.getInstancesBuffer());
			draw_ub.counters = gpu::getRWBindlessHandle(m_scene_counters);
			draw_ub.commands = gpu::getRWBindlessHandle(m_scene_commands);
			draw_ub.meshes = gpu::getBindlessHandle(scene.getMeshTableBuffer());
			draw_ub.variants = gpu::getBindlessHandle(scene.getVariantBuffer());
			draw_ub.variant_count = scene.getVariantCount();
			draw_ub.group_base = gpu::getBindlessHandle(group_base.buffer);
			draw_ub.group_base_offset = group_base.offset;
			draw_ub.out = gpu::getRWBindlessHandle(m_scene_out);
			draw_ub.args = gpu::getRWBindlessHandle(m_scene_cull_args);
			draw_ub.groups = gpu::getBindlessHandle(scene.getGroupTableBuffer());
			draw_ub.material_sets = gpu::getBindlessHandle(scene.getMaterialSetBuffer());
			draw_ub.command_base = command_base;
			const GPUScene::FrameBones& frame_bones = scene.getFrameBones();
			if (frame_bones.table && frame_bones.bones) {
				draw_ub.bone_table = gpu::getBindlessHandle(frame_bones.table);
				draw_ub.bone_table_offset = frame_bones.table_offset;
			}
			const TransientSlice draw_ub_slice = alloc(m_renderer.getUniformPool(), &draw_ub, sizeof(draw_ub));

			auto program = [&](const char* define) { return m_scene_variants_shader->getProgram(1 << m_renderer.getShaderDefineIdx(define)); };
			stream.barrier(group_base.buffer, gpu::BarrierType::READ);
			stream.barrier(scene.getVariantBuffer(), gpu::BarrierType::READ);
			stream.barrier(scene.getMeshTableBuffer(), gpu::BarrierType::READ);
			stream.barrier(scene.getMaterialSetBuffer(), gpu::BarrierType::READ);
			if (frame_bones.table) stream.barrier(frame_bones.table, gpu::BarrierType::READ);
			stream.barrier(scene.getGroupTableBuffer(), gpu::BarrierType::READ);
			stream.barrier(scene.getInstancesBuffer(), gpu::BarrierType::READ);
			stream.barrier(m_scene_selected, gpu::BarrierType::WRITE);
			stream.barrier(m_scene_counters, gpu::BarrierType::WRITE);
			stream.barrier(m_scene_commands, gpu::BarrierType::WRITE);
			stream.barrier(m_scene_out, gpu::BarrierType::WRITE);
			stream.barrier(m_scene_cull_args, gpu::BarrierType::WRITE);
			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, draw_ub_slice.buffer, draw_ub_slice.offset, sizeof(draw_ub));

			stream.beginProfileBlock("variants clear", 0, false);
			stream.useProgram(program("CLEAR"));
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(m_scene_counters);
			stream.useProgram(program("BIN_ARGS"));
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(m_scene_cull_args);
			stream.endProfileBlock();

			stream.beginProfileBlock("variants count", 0, false);
			stream.useProgram(program("COUNT"));
			stream.bindIndirectBuffer(m_scene_cull_args);
			stream.dispatchIndirect(48);
			stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
			stream.memoryBarrier(m_scene_counters);
			stream.endProfileBlock();

			stream.beginProfileBlock("variants scan", 0, false);
			stream.useProgram(program("SCAN"));
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(m_scene_counters);
			stream.memoryBarrier(m_scene_commands);
			stream.endProfileBlock();

			stream.beginProfileBlock("variants scatter", 0, false);
			stream.useProgram(program("SCATTER"));
			stream.bindIndirectBuffer(m_scene_cull_args);
			stream.dispatchIndirect(48);
			stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
			stream.memoryBarrier(m_scene_out);
			stream.memoryBarrier(m_scene_counters);
			stream.endProfileBlock();

			// One indirect call for each draw group the view draws, the instance counts are written by the GPU: the commands are the ones of SCAN. Vertex shaders pull the
			// instances by the draw id, the vertices of the clusters by their meshes (CLUSTER_MESH)
			u32 mesh_instance_define = 1 << m_mesh_instance_define_idx;
			mesh_instance_define |= 1 << m_renderer.getShaderDefineIdx("INDIRECT_INSTANCE");
			mesh_instance_define |= 1 << m_renderer.getShaderDefineIdx("CLUSTER_MESH");
			if (m_debug_show == DebugShow::MESH_CLUSTERS) mesh_instance_define |= 1 << m_renderer.getShaderDefineIdx("CLUSTER_DEBUG");
			if (m_debug_show == DebugShow::MESH_TRIANGLES) mesh_instance_define |= (1 << m_renderer.getShaderDefineIdx("CLUSTER_DEBUG")) | (1 << m_renderer.getShaderDefineIdx("CLUSTER_DEBUG_TRIANGLES"));

			// Draws of meshes without clusters pull the same instances by the draw id, without CLUSTER_MESH. The SKINNED variant has the layout of
			// MESH_INSTANCE but is chosen by its own define, with both the shader is not skinned.
			const u32 indirect_define = 1 << m_renderer.getShaderDefineIdx("INDIRECT_INSTANCE");
			const u32 pooled_define = 1 << m_renderer.getShaderDefineIdx("POOLED_VERTICES");	// the base vertex of the command is in the instance
			const u32 plain_mesh_define = (1 << m_mesh_instance_define_idx) | indirect_define | pooled_define;

			// the layout of VertexBuffer in vertex_pulling.hlsli with CLUSTER_MESH
			struct SceneVertexBufferBinding {
				Mesh::VertexBufferBinding binding;
				u32 meshes;			// bindless, the mesh table of the scene
				u32 padding[3];
			};
			const SceneVertexBufferBinding vertex_buffer_binding = {
				{ gpu::getBindlessHandle(m_renderer.getClusterDataBuffer()).value, gpu::getBindlessHandle(m_scene_out).value, 0, 0 },
				gpu::getBindlessHandle(scene.getMeshTableBuffer()).value,
				{ 0, 0, 0 }
			};
			const u32* group_bases = (const u32*)group_base.ptr;
			UniformPool& uniform_pool = m_renderer.getUniformPool();
			for (u32 g = 0, c = scene.getDrawGroupCount(); g < c; ++g) {
				if (group_bases[g] == 0xffFFffFF) continue;
				const GPUScene::DrawGroup& group = scene.getDrawGroup(g);
				Bucket& bucket = view.buckets[view.layer_to_bucket[group.layer]];
				const u32 define_mask = group.define_mask | bucket.define_mask;
				const gpu::StateFlags state = group.state | bucket.state;
				const u32 command_offset = command_base + group_bases[g] * 24;
				if (group.plain) {
					// the vertices and indices of the meshes are in the buffer of cluster data, a command has the first index and the base vertex of its mesh
					const bool skinned = (group.define_mask & (1u << m_skinned_define_idx)) != 0;
					const gpu::ProgramHandle plain_program = group.shader->getProgram(state, group.primitive_type, define_mask | (skinned ? (indirect_define | pooled_define) : plain_mesh_define), group.semantics_defines);
					bucket.stream.useProgram(plain_program);
					bucket.stream.bindIndexBuffer(m_renderer.getClusterDataBuffer());
					Lumix::bindPulledBuffers(bucket.stream, uniform_pool, m_renderer.getClusterDataBuffer(), m_scene_out, 0, frame_bones.bones);
					bucket.stream.barrier(m_scene_out, gpu::BarrierType::READ);
					bucket.stream.barrier(m_renderer.getClusterDataBuffer(), gpu::BarrierType::READ);
					if (skinned && frame_bones.bones) bucket.stream.barrier(frame_bones.bones, gpu::BarrierType::READ);
					bucket.stream.bindIndirectBuffer(m_scene_commands);
					bucket.stream.drawIndirect(group.index_type, command_offset, group.variant_count);
					bucket.stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
					bucket.stream.bindIndexBuffer(gpu::INVALID_BUFFER);
					continue;
				}
				const gpu::ProgramHandle cluster_program = group.shader->getProgram(state, group.primitive_type, define_mask | mesh_instance_define, group.semantics_defines);
				const TransientSlice layout_ub = alloc(uniform_pool, &vertex_buffer_binding, sizeof(vertex_buffer_binding));
				bucket.stream.useProgram(cluster_program);
				bucket.stream.bindIndexBuffer(m_cluster_iota);
				bucket.stream.bindUniformBuffer(UniformBuffer::VERTEX_BUFFER, layout_ub.buffer, layout_ub.offset, layout_ub.size);
				bucket.stream.barrier(m_scene_out, gpu::BarrierType::READ);
				bucket.stream.barrier(scene.getMeshTableBuffer(), gpu::BarrierType::READ);
				bucket.stream.barrier(m_renderer.getClusterDataBuffer(), gpu::BarrierType::READ);
				bucket.stream.bindIndirectBuffer(m_scene_commands);
				bucket.stream.drawIndirect(gpu::DataType::U16, command_offset, group.variant_count);
				bucket.stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
				bucket.stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			}
		}
	}

	// the only per draw data of vertex pulling are buffers
	// `instances` is pulled instance data of the draw (from the offset in bytes), if there is any
	void bindPulledBuffers(DrawStream& stream, gpu::BufferHandle vertices, gpu::BufferHandle instances, u32 instances_offset, gpu::BufferHandle bones = gpu::INVALID_BUFFER) {
		Lumix::bindPulledBuffers(stream, m_renderer.getUniformPool(), vertices, instances, instances_offset, bones);
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

	void computeSkeletonDualQuats(const ModelInstance* mi, DualQuat* output) {
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
		const Transform* LUMIX_RESTRICT transforms = world.getTransforms(); 
		const DVec3 camera_pos = view.cp.pos;
		
		// instances are pulled by vertex shaders too

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

			u32 define_mask;
			u32 mesh_particle_define_mask;
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
					mesh_particle_define_mask = define_mask | (1 << m_mesh_particle_define_idx);
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
						gpu::ProgramHandle program = material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLE_STRIP, define_mask | material->getDefineMask(), "");
						const TransientSlice point_slice = emitter.slice;
						const Matrix mtx(lpos, tr.rot);

						stream->useProgram(program);
						stream->bindIndexBuffer(gpu::INVALID_BUFFER);

						// points are pulled by the vertex shader, see ribbon.hlsl
						u32 points_offset = 0;
						const u32 vertex_stride = emitter.resource_emitter.vertex_stride;
						for (const auto& ribbon : emitter.ribbons) {
							if (ribbon.length > 1) {
								struct {
									Matrix mtx;
									MaterialIndex material_index;
									u32 points_buffer;
									u32 points_offset;
									u32 ribbon_offset;
									u32 ribbon_max_length;
								} ub_data = {
									mtx,
									material->getIndex(),
									gpu::getBindlessHandle(point_slice.buffer).value,
									point_slice.offset + points_offset,
									ribbon.offset,
									ribbon.length
								};
								const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
								stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
								// TODO instanced draw / indirect instanced
								stream->drawArraysInstanced(2 * ribbon.length, 1);
							}
							points_offset += ribbon.length * vertex_stride;
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
						gpu::ProgramHandle program = material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLE_STRIP, define_mask | material->getDefineMask(), "");
						const TransientSlice point_slice = emitter.slice;
						const Matrix mtx(lpos, tr.rot);

						stream->useProgram(program);
						stream->bindIndexBuffer(gpu::INVALID_BUFFER);

						// points are pulled by the vertex shader
						u32 points_offset = 0;
						const u32 vertex_stride = emitter.resource_emitter.vertex_stride;
						for (const auto& ribbon : emitter.ribbons) {
							if (ribbon.length > 1) {
								struct {
									Matrix mtx;
									MaterialIndex material_index;
									u32 tube_segments;
									u32 points_buffer;
									u32 points_offset;
									u32 ribbon_offset;
									u32 ribbon_max_length;
								} ub_data = {
									mtx,
									material->getIndex(),
									tube_segments,
									gpu::getBindlessHandle(point_slice.buffer).value,
									point_slice.offset + points_offset,
									ribbon.offset,
									emitter.resource_emitter.max_ribbon_length
								};
								const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
								stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
								// For tube: each ring transition needs (tube_segments + 1) * 2 vertices
								const u32 ring_transitions = ribbon.length - 1;
								const u32 vertex_count = ring_transitions * (tube_segments + 1) * 2;
								stream->drawArraysInstanced(vertex_count, 1);
							}
							points_offset += ribbon.length * vertex_stride;
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
						const gpu::StateFlags state = material->m_render_states | render_state;
						const u32 defines = mesh_particle_define_mask | material->getDefineMask();
						Shader* shader = material->getShader();
						gpu::ProgramHandle program = shader->getProgram(state, mesh.primitive_type, defines, mesh.semantics_defines);

						struct UBData {
							Vec3 pos;
							MaterialIndex material_index;
							u32 particle_stride;
						} ub_data = {
							lpos,
							material->getIndex(),
							emitter.resource_emitter.vertex_stride
						};

						TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));

						stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream->useProgram(program);
						stream->bindIndexBuffer(mesh.index_buffer_handle);
						// particles are pulled instances
						bindPulledBuffers(*stream, mesh.vertex_buffer_handle, emitter.slice.buffer, emitter.slice.offset);
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
						const gpu::StateFlags state = material->m_render_states | render_state;
						gpu::ProgramHandle program = material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLE_STRIP, define_mask | material->getDefineMask(), "");
						const TransientSlice slice = emitter.slice;
						const Matrix mtx(lpos, tr.rot);

						// particles are pulled by the vertex shader, see particles.hlsl
						struct {
							Matrix mtx;
							MaterialIndex material_index;
							u32 particles_buffer;
							u32 particles_offset;
							u32 particles_stride;
						} ub_data = {
							mtx,
							material->getIndex(),
							gpu::getBindlessHandle(slice.buffer).value,
							slice.offset,
							emitter.resource_emitter.vertex_stride
						};

						const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
						stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
						stream->useProgram(program);
						stream->bindIndexBuffer(gpu::INVALID_BUFFER);
						stream->drawArraysInstanced(4, particles_count);
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
							stream->useProgram(material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLES, define_mask | material->getDefineMask(), m_cube_pulling_defines));
							bindPulledBuffers(*stream, m_cube_vb, slice.buffer, slice.offset);
							stream->drawIndexedInstanced(36, nonintersecting_count, gpu::DataType::U16);
						}

						if (count - nonintersecting_count) {
							state = state & ~gpu::StateFlags::DEPTH_FUNCTION;
							state = state & ~gpu::StateFlags::CULL_BACK;
							state = state | gpu::StateFlags::CULL_FRONT;
							stream->useProgram(material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLES, define_mask | material->getDefineMask(), m_cube_pulling_defines));
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
							stream->useProgram(material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLES, define_mask | material->getDefineMask(), m_cube_pulling_defines));
							bindPulledBuffers(*stream, m_cube_vb, slice.buffer, slice.offset);
							stream->drawIndexedInstanced(36, nonintersecting_count, gpu::DataType::U16);
						}

						if (count - nonintersecting_count) {
							state = state & ~gpu::StateFlags::DEPTH_FUNCTION;
							state = state & ~gpu::StateFlags::CULL_BACK;
							state = state | gpu::StateFlags::CULL_FRONT;
							stream->useProgram(material->getShader()->getProgram(state, gpu::PrimitiveType::TRIANGLES, define_mask | material->getDefineMask(), m_cube_pulling_defines));
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

	void createSortKeys(PipelineImpl::View& view) {
		PagedListIterator<const CullResult> iterator(view.renderables);

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

		jobs::runOnWorkers([&](){
			PROFILE_BLOCK("create keys");
			
			int total = 0;
			Sorter::Inserter inserter(view.sorter);

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
					// meshes are drawn by the GPU, see runGPUSceneCulling, they are not in the culling system
					case RenderableTypes::MESH:
					case RenderableTypes::PARTICLES:
					case RenderableTypes::COUNT:
						ASSERT(false);
						break;
				}
			}
			profiler::pushInt("count", total);
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

		// the result is in the temporary memory if an odd number of passes was done (passes in which all keys have the same digit are skipped)
		if (keys != _keys) {
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
	Shader* m_cluster_shader;
	Shader* m_scene_variants_shader;									// sorts the selected clusters and meshes into variants of draws, see gpu_scene_variants.hlsl
	Shader* m_scene_cull_shader;									// counts the slots of the GPU scene inside a frustum, see runGPUSceneCulling
	gpu::BufferHandle m_scene_cull_counter = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_scene_cull_args = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_scene_flat_items = gpu::INVALID_BUFFER;		// work items of clusters generated by the culling of instances, see gpu_scene_cull.hlsl
	gpu::BufferHandle m_scene_tree_items = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_scene_counters = gpu::INVALID_BUFFER;			// counters of the variants, see gpu_scene_variants.hlsl
	gpu::BufferHandle m_scene_commands = gpu::INVALID_BUFFER;	// a draw command for each variant of a view
	gpu::BufferHandle m_scene_out = gpu::INVALID_BUFFER;			// instance data of the draws
	u32 m_scene_work_items = 0;										// capacities of the buffers of the GPU scene, set when they are created
	u32 m_scene_selected_capacity = 0;
	u32 m_scene_draw_slots = 0;
	gpu::BufferHandle m_scene_selected = gpu::INVALID_BUFFER;		// clusters selected from the work items: three counters, then 16 bytes each (slot, mesh, cluster)
	AtomicI32 m_scene_view_count = 0;								// views drawn from the GPU scene in this frame
	Shader* m_flatten_shader;
	Array<gpu::TextureHandle> m_textures;
	Array<gpu::BufferHandle> m_buffers;
	os::Timer m_timer;

	// buffers of the clusters of the GPU scene, see runGPUSceneCulling
	enum : u32 {
		CLUSTER_SCRATCH = 1 << 22,					// u32 in the scratch buffer of the lists of nodes, scene and game views
		CLUSTER_SCRATCH_SMALL = 1 << 18,			// previews and other small pipelines
		SCENE_VIEWS = 32,							// views of a frame that are drawn from the GPU scene, each has its own commands
		SCENE_DRAW_SLOTS = 1 << 18,					// instances of these draws (96 bytes each), scene and game views
		SCENE_DRAW_SLOTS_SMALL = 1 << 14,			// the same for previews and other small pipelines
		SCENE_SELECTED = 1 << 20,					// capacity of the list of clusters selected from the lists of work (nanite.hlsl)
		SCENE_SELECTED_SMALL = 1 << 16,
		SCENE_WORK_ITEMS = 1 << 20,					// capacity of each list of work items generated by the culling of instances (gpu_scene_cull.hlsl)
		SCENE_WORK_ITEMS_SMALL = 1 << 16,
	};
	bool m_cluster_shader_failure_logged = false;
	gpu::BufferHandle m_cluster_scratch = gpu::INVALID_BUFFER;		// the lists of nodes that the culling of clusters visits, see nanite.hlsl
	gpu::BufferHandle m_cluster_iota = gpu::INVALID_BUFFER;			// u16 0..65535, the index buffer of the draws of clusters
	u32 m_cluster_scratch_capacity = 0;								// in u32
	bool m_cluster_frozen = false;									// the camera that chooses the clusters is m_cluster_frozen_cp, see m_cluster_freeze_camera
	CameraParams m_cluster_frozen_cp;
	float m_cluster_frozen_height = 1.f;
	const char* m_cube_pulling_defines = "";
	gpu::BufferHandle m_cube_vb;
	gpu::BufferHandle m_cube_ib;
	ShadowAtlas m_shadow_atlas;
	CameraParams m_custom_camera_params;
	HashMap<u32, void*> m_instance_data;
	JobsGroup m_sort_keys_group;
	u8 m_mesh_instance_define_idx;
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
};


UniquePtr<Pipeline> Pipeline::create(Renderer& renderer, PipelineType type)
{
	return UniquePtr<PipelineImpl>::create(renderer.getAllocator(), renderer, type, renderer.getAllocator());
}


} // namespace Lumix

