#include "gpu/gpu.h"
#include "foundation/crt.h"
#include "foundation/allocators.h"
#include "foundation/associative_array.h"
#include "foundation/atomic.h"
#include "engine/engine.h"
#include "foundation/file_system.h"
#include "foundation/geometry.h"
#include "foundation/hash.h"
#include "foundation/job_system.h"
#include "foundation/log.h"
#include "engine/lua_wrapper.h"
#include "foundation/math.h"
#include "foundation/sync.h"
#include "foundation/os.h"
#include "foundation/page_allocator.h"
#include "foundation/path.h"
#include "foundation/profiler.h"
#include "engine/resource_manager.h"
#include "foundation/stack_array.h"
#include "engine/world.h"
#include "lua_script/lua_script.h"
#include "culling_system.h"
#include "draw2d.h"
#include "draw_stream.h"
#include "font.h"
#include "material.h"
#include "model.h"
#include "particle_system.h"
#include "pipeline.h"
#include "pose.h"
#include "renderer.h"
#include "render_module.h"
#include "shader.h"
#include "terrain.h"
#include "texture.h"

namespace Lumix
{


// sort key:
// bucket 64-56
// instanced_flag 55
// depth bits 31 - 0; if bucket is depth sorted, must be not instanced
// mesh key 31 - 8; if bucket is not depth sorted and not instanced
// instancer 31 - 16; if instanced
// instance group 15 - 0; if instanced

static constexpr u32 INSTANCED_MESHES_BUFFER_SIZE = 64 * 1024 * 1024; // TODO dynamic
static constexpr u32 SORT_VALUE_TYPE_MASK = (1 << 5) - 1;
static constexpr u64 SORT_KEY_BUCKET_SHIFT = 56;
static constexpr u64 SORT_KEY_INSTANCED_FLAG = (u64)1 << 55;
static constexpr u64 SORT_KEY_INSTANCER_SHIFT = 16;
static constexpr u64 SORT_KEY_MESH_IDX_SHIFT = 40;
static constexpr u64 SORT_KEY_EMITTER_SHIFT = 40;

struct CameraParams
{
	ShiftedFrustum frustum;
	DVec3 pos;
	float lod_multiplier;
	bool is_shadow;
	Matrix view;
	Matrix projection;
};

using CameraParamsHandle = u32;

struct PipelineTexture {
	enum Type {
		RENDERBUFFER,
		RESOURCE,
		RAW
	} type;

	union {
		u32 renderbuffer;
		gpu::TextureHandle raw;
		Engine::LuaResourceHandle resource;
	};
};

using RenderStateHandle = u32;
struct RenderState {
	gpu::StateFlags value;
};

namespace {
	struct Indirect {
		u32 vertex_count;
		u32 instance_count;
		u32 first_index;
		u32 base_vertex;
		u32 base_instance;
	};
}

using LuaBufferHandle = gpu::BufferHandle;

namespace LuaWrapper {

	template <> inline PipelineTexture toType(lua_State* L, int idx) {
		PipelineTexture res;
		switch (lua_type(L, idx)) {
			case LUA_TTABLE: {
				const int type1 = LuaWrapper::getField(L, idx, "lumix_resource");
				if (type1 != LUA_TSTRING) luaL_error(L, "texture expected");
				const char* res_type = LuaWrapper::toType<const char*>(L, -1);
				
				if (equalStrings(res_type, "renderbuffer")) {
					lua_pop(L, 1);
					if (!LuaWrapper::checkField(L, idx, "value", &res.renderbuffer)) luaL_error(L, "texture expected");
					res.type = PipelineTexture::RENDERBUFFER;
					return res;
				}
				if (equalStrings(res_type, "raw_texture")) {
					lua_pop(L, 1);
					if (!LuaWrapper::checkField(L, idx, "value", &res.raw)) luaL_error(L, "texture expected");
					res.type = PipelineTexture::RAW;
					return res;
				}

				luaL_error(L, "texture expected");
				break;
			}
			case LUA_TNUMBER: {
				res.type = PipelineTexture::RESOURCE;
				res.resource = LuaWrapper::toType<i32>(L, idx);
				break;
			}
			default:
				luaL_error(L, "texture expected");
				break;
		}
		return res;
	}

	template <> inline RenderState toType(lua_State* L, int idx)
	{
		gpu::StencilFuncs stencil_func = gpu::StencilFuncs::DISABLE;
		u8 stencil_write_mask = 0xff;
		u8 stencil_ref = 0;
		u8 stencil_mask = 0;
		gpu::StencilOps stencil_sfail = gpu::StencilOps::KEEP;
		gpu::StencilOps stencil_zfail = gpu::StencilOps::KEEP;
		gpu::StencilOps stencil_zpass = gpu::StencilOps::KEEP;

		char tmp[64];
		gpu::StateFlags rs = gpu::StateFlags::DEPTH_WRITE;
		if (LuaWrapper::getOptionalStringField(L, idx, "blending", Span(tmp))) {
			if(equalIStrings(tmp, "add")) {
				rs = rs | gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
			}
			else if(equalIStrings(tmp, "alpha")) {
				rs = rs | gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
			}
			else if(equalIStrings(tmp, "multiply")) {
				rs = rs | gpu::getBlendStateBits(gpu::BlendFactors::DST_COLOR, gpu::BlendFactors::ZERO, gpu::BlendFactors::ONE, gpu::BlendFactors::ZERO);
			}
			else if(equalIStrings(tmp, "dual")) {
				rs = rs | gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::SRC1_COLOR, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
			}
			else if(equalIStrings(tmp, "")) {
			}
			else {
				luaL_error(L, "Unknown blending mode");
			}
		}

		u32 depth_func = 0;
		LuaWrapper::getOptionalFlagField(L, idx, "wireframe", &rs, gpu::StateFlags::WIREFRAME, false);
		LuaWrapper::getOptionalFlagField(L, idx, "depth_write", &rs, gpu::StateFlags::DEPTH_WRITE, true);
		if (LuaWrapper::getOptionalField(L, idx, "depth_func", &depth_func)) {
			rs = rs | gpu::StateFlags(depth_func);
		}
		else {
			LuaWrapper::getOptionalFlagField(L, idx, "depth_test", &rs, gpu::StateFlags::DEPTH_FN_GREATER, true);
		}
		LuaWrapper::getOptionalField(L, idx, "stencil_func", reinterpret_cast<u8*>(&stencil_func));
		LuaWrapper::getOptionalField(L, idx, "stencil_write_mask", &stencil_write_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_ref", &stencil_ref);
		LuaWrapper::getOptionalField(L, idx, "stencil_mask", &stencil_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_sfail", reinterpret_cast<u8*>(&stencil_sfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zfail", reinterpret_cast<u8*>(&stencil_zfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zpass", reinterpret_cast<u8*>(&stencil_zpass));

		rs = rs | gpu::getStencilStateBits(stencil_write_mask, stencil_func, stencil_ref, stencil_mask, stencil_sfail, stencil_zfail, stencil_zpass);

		return {rs};
	}
	
	template <> inline bool isType<RenderState>(lua_State* L, int index)
	{
		return lua_istable(L, index);
	}

	template <> inline bool isType<PipelineTexture>(lua_State* L, int index)
	{
		return lua_istable(L, index) || lua_isnumber(L, index);
	}

	template <> inline bool isType<Color>(lua_State* L, int index)
	{
		return lua_isnumber(L, index) != 0 || lua_istable(L, index);
	}

	template <> inline Color toType<Color>(lua_State* L, int index)
	{
		if (lua_isnumber(L, index)) {
			return Color((u32)lua_tointeger(L, index));
		}
		else if (lua_istable(L, index)) {
			Color c(0);
			lua_rawgeti(L, index, 1);
			c.r = (u8)lua_tointeger(L, -1);
			lua_pop(L, 1);

			lua_rawgeti(L, index, 2);
			c.g = (u8)lua_tointeger(L, -1);
			lua_pop(L, 1);

			lua_rawgeti(L, index, 3);
			c.b = (u8)lua_tointeger(L, -1);
			lua_pop(L, 1);

			lua_rawgeti(L, index, 4);
			c.a = (u8)lua_tointeger(L, -1);
			lua_pop(L, 1);

			return c;
	
		}
		return Color(0);
	}

	void push(lua_State* L, const PipelineTexture& tex) {
		switch (tex.type) {
			case PipelineTexture::RESOURCE:
				LuaWrapper::push(L, tex.resource);
				break;
			case PipelineTexture::RAW:
				lua_newtable(L);
				LuaWrapper::setField(L, -1, "lumix_resource", "raw_texture");
				LuaWrapper::setField(L, -1, "value", tex.raw);
				break;
			case PipelineTexture::RENDERBUFFER:
				lua_newtable(L);
				LuaWrapper::setField(L, -1, "lumix_resource", "renderbuffer");
				LuaWrapper::setField(L, -1, "value", tex.renderbuffer);
				break;
		}
	}
}


struct GlobalState
{
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
	Vec4 to_prev_frame_camera_translation;
	Vec4 light_direction;
	Vec4 light_color;
	IVec2 framebuffer_size;
	Vec2 pixel_jitter;
	Vec2 prev_pixel_jitter;
	Vec2 padding_;
	float light_intensity;
	float light_indirect_intensity;
	float time;
	float frame_time_delta;
	float shadow_cam_depth_range;
	float shadow_cam_rcp_depth_range;
};


struct ShadowAtlas {
	static constexpr u32 SIZE = 2048;
	
	ShadowAtlas(IAllocator& allocator)
		: map(allocator)
	{
		for (EntityPtr& e : inv_map) e = INVALID_ENTITY;
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


struct PipelineImpl final : Pipeline
{
	using RenderbufferDescHandle = u32;
	struct RenderbufferDesc {
		enum Type {
			FIXED,
			RELATIVE,
			DISPLAY_SIZE
		};
		Type type = FIXED;
		IVec2 fixed_size;
		Vec2 rel_size;
		gpu::TextureFormat format;
		gpu::TextureFlags flags;
		StaticString<32> debug_name;
	};

	struct Renderbuffer {
		gpu::TextureHandle handle;
		int frame_counter;
		IVec2 size;
		gpu::TextureFormat format;
		gpu::TextureFlags flags;
	};

	struct ShaderRef {
		Lumix::Shader* res;
		int id;
	};

	struct Bucket {
		Bucket(Renderer& renderer) : stream(renderer) {}
		enum Sort {
			DEFAULT,
			DEPTH
		};
		u8 layer;
		char layer_name[32];
		Sort sort = DEFAULT;
		u32 define_mask = 0;
		gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FN_GREATER;
		DrawStream stream;
	};

	struct Sorter {
		struct alignas(4096) Page {
			struct Header {
				Page* next = nullptr;
				u32 count = 0;
			} header;

			static constexpr u32 MAX_COUNT = (PageAllocator::PAGE_SIZE - sizeof(Header)) / sizeof(u64) / 2;
			u64 keys[MAX_COUNT];
			u64 values[MAX_COUNT];
		};

		static_assert(sizeof(Page) == PageAllocator::PAGE_SIZE);

		struct Inserter {
			Inserter(Sorter& sorter) 
				: sorter(sorter)
			{
				first_page = last_page = getNewPage();
			}

			Page* getNewPage() {
				void* mem = sorter.page_allocator.allocate(true);
				return new (NewPlaceholder(), mem) Page;
			}

			void push(u64 key, u64 value) {
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
			page_allocator.lock();
			while (p) {
				Page* n = p->header.next;
				page_allocator.deallocate(p, false);
				p = n;
			}
			page_allocator.unlock();
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
			page_allocator.lock();
			while (p) {
				Page* n = p->header.next;
				page_allocator.deallocate(p, false);
				p = n;
			}
			page_allocator.unlock();
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

		AutoInstancer(LinearAllocator& allocator, PageAllocator& page_allocator)
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
			page_allocator.lock();
			while (p) {
				Page* next = p->header.next;
				page_allocator.deallocate(p, false);
				p = next;
			}
			page_allocator.unlock();
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
			void* mem = page_allocator.allocate(true);
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
			Renderer::TransientSlice slice;
		};

		Array<Instances> instances;
		Page* last_page = nullptr;
		Page* first_page = nullptr;
		PageAllocator& page_allocator;
	};
	
	struct View {
		View(LinearAllocator& allocator, PageAllocator& page_allocator) 
			: sorter(allocator, page_allocator)
			, instancers(allocator)
			, buckets(allocator)
		{}

		View(View&& rhs) = delete;
		View(const View& rhs) = delete;
		void operator=(View&&) = delete;

		Array<Bucket> buckets;
		Array<AutoInstancer> instancers;
		Sorter sorter;
		CullResult* renderables = nullptr;
		CameraParams cp;
		u8 layer_to_bucket[255];
		jobs::Signal ready;
	};

	// converts float to u32 so it can be used in radix sort
	// float float_value = 0;
	// u32 sort_key = floatFlip(*(u32*)&float_value);
	// http://stereopsis.com/radix.html
	static LUMIX_FORCE_INLINE u32 floatFlip(u32 float_bits_value) {
		u32 mask = -i32(float_bits_value >> 31) | 0x80000000;
		return float_bits_value ^ mask;
	}


	PipelineImpl(Renderer& renderer, LuaScript* resource, const char* define, IAllocator& allocator)
		: m_allocator(allocator)
		, m_renderer(renderer)
		, m_resource(resource)
		, m_lua_state(nullptr)
		, m_custom_commands_handlers(m_allocator)
		, m_define(define)
		, m_module(nullptr)
		, m_draw2d(m_allocator)
		, m_output(-1)
		, m_renderbuffer_descs(m_allocator)
		, m_renderbuffers(m_allocator)
		, m_shaders(m_allocator)
		, m_shadow_atlas(m_allocator)
		, m_textures(m_allocator)
		, m_buffers(m_allocator)
		, m_views(m_allocator)
		, m_render_states(m_allocator)
		, m_base_vertex_decl(gpu::PrimitiveType::TRIANGLES)
		, m_base_line_vertex_decl(gpu::PrimitiveType::LINES)
		, m_decal_decl(gpu::PrimitiveType::TRIANGLES)
		, m_curve_decal_decl(gpu::PrimitiveType::TRIANGLES)
		, m_2D_decl(gpu::PrimitiveType::TRIANGLES)
	{
		m_viewport.w = m_viewport.h = 800;
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_draw2d_shader = rm.load<Shader>(Path("pipelines/draw2d.shd"));
		m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));
		m_instancing_shader = rm.load<Shader>(Path("pipelines/instancing.shd"));
		
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
		m_cube_vb = m_renderer.createBuffer(vb_mem, gpu::BufferFlags::IMMUTABLE);

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
		m_cube_ib = m_renderer.createBuffer(ib_mem, gpu::BufferFlags::IMMUTABLE);

		m_resource->onLoaded<&PipelineImpl::onStateChanged>(this);

		const Renderer::MemRef im_mem = { INSTANCED_MESHES_BUFFER_SIZE, nullptr, false };
		m_instanced_meshes_buffer = m_renderer.createBuffer(im_mem, gpu::BufferFlags::COMPUTE_WRITE | gpu::BufferFlags::SHADER_BUFFER);

		const Renderer::MemRef ind_mem = { 64 * 1024, nullptr, false }; // TODO size
		m_indirect_buffer = m_renderer.createBuffer(ind_mem, gpu::BufferFlags::COMPUTE_WRITE | gpu::BufferFlags::SHADER_BUFFER);

		m_base_vertex_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		m_base_vertex_decl.addAttribute(12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		m_base_line_vertex_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		m_base_line_vertex_decl.addAttribute(12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		m_decal_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		m_decal_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(12, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(28, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(40, 2, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		m_curve_decal_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		m_curve_decal_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(12, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(28, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(40, 2, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(48, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		m_2D_decl.addAttribute(0, 2, gpu::AttributeType::FLOAT, 0);
		m_2D_decl.addAttribute(8, 2, gpu::AttributeType::FLOAT, 0);
		m_2D_decl.addAttribute(16, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
	}

	~PipelineImpl()
	{
		for (RenderPlugin* plugin : m_renderer.getPlugins()) {
			plugin->pipelineDestroyed(*this);
		}

		DrawStream& stream = m_renderer.getEndFrameDrawStream();
		for (gpu::TextureHandle t : m_textures) stream.destroy(t);
		for (gpu::BufferHandle b : m_buffers) stream.destroy(b);

		m_draw2d_shader->decRefCount();
		m_debug_shape_shader->decRefCount();
		m_instancing_shader->decRefCount();

		for (const Renderbuffer& rb : m_renderbuffers) {
			stream.destroy(rb.handle);
		}

		for(ShaderRef& shader : m_shaders) {
			shader.res->decRefCount();
		}
		if (m_resource) m_resource->decRefCount();

		stream.destroy(m_cube_ib);
		stream.destroy(m_cube_vb);
		stream.destroy(m_instanced_meshes_buffer);
		stream.destroy(m_indirect_buffer);
		stream.destroy(m_shadow_atlas.texture);
		stream.destroy(m_cluster_buffers.clusters.buffer);
		stream.destroy(m_cluster_buffers.lights.buffer);
		stream.destroy(m_cluster_buffers.maps.buffer);
		stream.destroy(m_cluster_buffers.env_probes.buffer);
		stream.destroy(m_cluster_buffers.refl_probes.buffer);

		clearBuffers();
	}

	void callInitModule() {
		PROFILE_FUNCTION();
		LuaWrapper::DebugGuard guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "initModule");
		if (lua_type(m_lua_state, -1) == LUA_TFUNCTION) {
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != 0)
			{
				logError(lua_tostring(m_lua_state, -1));
				lua_pop(m_lua_state, 1);
			}
		}
		lua_pop(m_lua_state, 2);
	}
	
	void cleanup()
	{
		if (m_lua_state)
		{
			LuaWrapper::releaseRef(m_renderer.getEngine().getState(), m_lua_thread_ref);
			LuaWrapper::releaseRef(m_lua_state, m_lua_env);
			m_lua_state = nullptr;
		}
	}

	void setDefine()
	{
		if (m_define == "") return;
		StaticString<256> tmp(m_define, " = true");

		bool errors = LuaWrapper::luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp), m_resource->getPath().c_str()) != 0;
		if (errors)
		{
			logError(m_resource->getPath(), ": ", lua_tostring(m_lua_state, -1));
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			logError(m_resource->getPath(), ": ", lua_tostring(m_lua_state, -1));
			lua_pop(m_lua_state, 1);
		}
	}

	void executeCustomCommand(const char* name)
	{
		const RuntimeHash name_hash(name);
		for (CustomCommandHandler& handler : m_custom_commands_handlers) {
			if (handler.hash == name_hash) {
				handler.callback.invoke();
				break;
			}
		}
	}

	void exposeCustomCommandToLua(const CustomCommandHandler& handler)
	{
		if (!m_lua_state) return;

		StaticString<1024> tmp("function ", handler.name, "() executeCustomCommand(\"", handler.name, "\") end");

		bool errors = LuaWrapper::luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp), "exposeCustomCommandToLua") != 0;
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = errors || lua_pcall(m_lua_state, 0, 0, 0) != 0;

		if (errors)
		{
			logError(lua_tostring(m_lua_state, -1));
			lua_pop(m_lua_state, 1);
		}
	}
	
	void onStateChanged(Resource::State, Resource::State new_state, Resource&)
	{
		if (new_state != Resource::State::READY) return;

		cleanup();

		lua_State* L = m_renderer.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		m_lua_state = lua_newthread(L);
		LuaWrapper::DebugGuard guard2(m_lua_state);
		m_lua_thread_ref = LuaWrapper::createRef(L);
		lua_pop(L, 1);

		lua_newtable(m_lua_state);
		lua_pushvalue(m_lua_state, -1);
		m_lua_env = LuaWrapper::createRef(m_lua_state);

		lua_setmetatable(m_lua_state, -2);
		lua_pushvalue(m_lua_state, LUA_GLOBALSINDEX);
		lua_setfield(m_lua_state, -2, "__index");

		lua_pushlightuserdata(m_lua_state, this);
		lua_setfield(m_lua_state, -2, "this");

		registerLuaAPI(m_lua_state);
		for (auto& handler : m_custom_commands_handlers)
		{
			exposeCustomCommandToLua(handler);
		}

		setDefine();
	
		StringView content = m_resource->getSourceCode();
		bool errors = LuaWrapper::luaL_loadbuffer(m_lua_state, content.begin, content.size(), m_resource->getPath().c_str()) != 0;
		if (errors)
		{
			logError(m_resource->getPath(), ": ", lua_tostring(m_lua_state, -1));
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			logError(m_resource->getPath(), ": ", lua_tostring(m_lua_state, -1));
			lua_pop(m_lua_state, 2);
			return;
		}
		
		m_viewport.w = m_viewport.h = 800;
		if (m_module) callInitModule();
		lua_pop(m_lua_state, 1);
	}

	void clearBuffers() {
		PROFILE_FUNCTION();
		for (Renderbuffer& rb : m_renderbuffers) {
			++rb.frame_counter;
			if (rb.frame_counter > 2 && rb.handle) {
				m_renderer.getEndFrameDrawStream().destroy(rb.handle);
				rb.handle = gpu::INVALID_TEXTURE;
			}
		}
	}

	void define(const char* define, bool enable) override {
		LuaWrapper::DebugGuard guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, define, enable);
		lua_pop(m_lua_state, 1);
	}

	Viewport getViewport() override {
		return m_viewport;
	}

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
			const Vec4 cascades = light.isValid() ? m_module->getShadowmapCascades((EntityRef)light) : Vec4(3, 10, 60, 150);
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
		const World& world = m_module->getWorld();
		const Viewport backup_viewport = m_viewport;

		const Vec4 uv = ShadowAtlas::getUV(atlas_idx);
		m_viewport.is_ortho = false;
		m_viewport.pos = world.getPosition(light.entity);
		m_viewport.rot = world.getRotation(light.entity);
		m_viewport.fov = light.fov;
		m_viewport.near = 0.1f;
		m_viewport.far = light.range;
		m_viewport.w = u32(ShadowAtlas::SIZE * uv.z + 0.5f);
		m_viewport.h = u32(ShadowAtlas::SIZE * uv.w + 0.5f);;

		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, "viewport_w", m_viewport.w);
		LuaWrapper::setField(m_lua_state, -1, "viewport_h", m_viewport.h);
		LuaWrapper::setField(m_lua_state, -1, "display_w", m_display_size.x);
		LuaWrapper::setField(m_lua_state, -1, "display_h", m_display_size.y);
		lua_getfield(m_lua_state, -1, "main_shadowmap");
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			logError(getPath(), ": can not bake shadows because main_shadowmap() is missing");
			return false;
		}
		LuaWrapper::pcall(m_lua_state, 0, 0);
		lua_pop(m_lua_state, 1);

		const gpu::TextureHandle src = getOutput();
		m_renderbuffers[m_output].frame_counter = 1;
		if (!src) {
			logError(getPath(), ": can not bake shadows because the pipeline has no output");
			return false;
		}

		const gpu::TextureHandle dst = m_shadow_atlas.texture;
		m_renderer.pushJob("bake_shadow_copy", [uv, src, dst](DrawStream& stream){
			const u32 x = u32(ShadowAtlas::SIZE * uv.x + 0.5f);
			const u32 y = u32(ShadowAtlas::SIZE * uv.y + 0.5f);
			stream.copy(dst, src, x, y);
		});

		m_viewport = backup_viewport;
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, "viewport_w", m_viewport.w);
		LuaWrapper::setField(m_lua_state, -1, "viewport_h", m_viewport.h);
		LuaWrapper::setField(m_lua_state, -1, "display_w", m_display_size.x);
		LuaWrapper::setField(m_lua_state, -1, "display_h", m_display_size.y);
		lua_pop(m_lua_state, 1);
		return true;
	}

	void render3DUI(EntityRef e, const Draw2D& drawdata, Vec2 canvas_size, bool orient_to_cam) override {
		Matrix matrix = m_module->getWorld().getRelativeMatrix(e, m_viewport.pos);
		Matrix normalize(
			Vec4(1 / canvas_size.x, 0, 0, 0),
			Vec4(0, -1 / canvas_size.x, 0, 0),
			Vec4(0, 0, 1, 0),
			Vec4(-0.5f, 0.5f * canvas_size.y / canvas_size.x, 0, 1)
		);
		if (orient_to_cam) {
			const Transform tr = m_module->getWorld().getTransform(e);
			matrix = m_viewport.rot.toMatrix();
			matrix.setTranslation(Vec3(tr.pos - m_viewport.pos));
			matrix.multiply3x3(tr.scale);
		}
		matrix = m_viewport.getProjectionWithJitter() * m_viewport.getViewRotation() * matrix * normalize;
		renderUIHelper(drawdata, true, matrix);
	}

	static Matrix computeReprojection(const Viewport& current, const Viewport& prev) {
		Matrix translation = Matrix::IDENTITY;
		translation.setTranslation(Vec3(current.pos - prev.pos));
		return prev.getProjectionNoJitter() * prev.getViewRotation() * translation * current.getViewRotation().inverted() * current.getProjectionNoJitter().inverted();
	}

	bool render(bool only_2d) override
	{
		PROFILE_FUNCTION();

		if (!isReady() || m_viewport.w <= 0 || m_viewport.h <= 0) {
			if (m_module) {
				m_module->clearDebugLines();
				m_module->clearDebugTriangles();
			}
			m_draw2d.clear(getAtlasSize());
			return false;
		}

		m_renderer.waitCanSetup();
		clearBuffers();

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
		GlobalState global_state;
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
		global_state.framebuffer_size.x = m_viewport.w;
		global_state.framebuffer_size.y = m_viewport.h;
		global_state.cam_world_pos = Vec4(Vec3(m_viewport.pos), 1);
		global_state.to_prev_frame_camera_translation = Vec4(Vec3(m_viewport.pos - m_prev_viewport.pos), 1);
		m_prev_viewport = m_viewport;
		m_indirect_buffer_offset = 0;

		if(m_module) {
			const EntityPtr global_light = m_module->getActiveEnvironment();
			if(global_light.isValid()) {
				EntityRef gl = (EntityRef)global_light;
				const Environment& env = m_module->getEnvironment(gl);
				global_state.light_direction = Vec4(normalize(m_module->getWorld().getRotation(gl).rotate(Vec3(0, 0, -1))), 456); 
				global_state.light_color = Vec4(env.light_color, 456);
				global_state.light_intensity = env.direct_intensity;
				global_state.light_indirect_intensity = env.indirect_intensity * m_indirect_light_multiplier;
			}
		}

		if (!only_2d) {
			prepareShadowCameras(global_state);
		}

		beginBlock(m_define);

		DrawStream& stream = m_renderer.getDrawStream();
		const Renderer::TransientSlice global_state_buffer = m_renderer.allocUniform(&global_state, sizeof(GlobalState));

		stream.bindUniformBuffer(UniformBuffer::GLOBAL, global_state_buffer.buffer, global_state_buffer.offset, sizeof(GlobalState));
		stream.bindUniformBuffer(UniformBuffer::PASS, gpu::INVALID_BUFFER, 0, 0);
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, gpu::INVALID_BUFFER, 0, 0);
		stream.bindUniformBuffer(UniformBuffer::SHADOW, gpu::INVALID_BUFFER, 0, 0);
		static int tmp[12] = {};
		stream.update(m_instanced_meshes_buffer, &tmp, sizeof(tmp));

		ASSERT(m_views.empty());
		
		if (!only_2d) fillClusters(stream, resolveCameraParams((CameraParamsHandle)CameraParamsEnum::MAIN));

		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		LuaWrapper::pushRef(m_lua_state, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, "viewport_w", m_viewport.w);
		LuaWrapper::setField(m_lua_state, -1, "viewport_h", m_viewport.h);
		LuaWrapper::setField(m_lua_state, -1, "display_w", m_display_size.x);
		LuaWrapper::setField(m_lua_state, -1, "display_h", m_display_size.y);
		lua_getfield(m_lua_state, -1, "main");
		bool has_main = true;
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			if (m_module) {
				m_module->clearDebugLines();
				m_module->clearDebugTriangles();
			}
			has_main = false;
		}
		if (has_main) {
			PROFILE_BLOCK("lua pipeline main");
			profiler::blockColor(0xff, 0x7f, 0x7f);
			LuaWrapper::pcall(m_lua_state, 0, 0);
			lua_pop(m_lua_state, 1);
		}

		endBlock();

		m_renderer.waitForCommandSetup();

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
			const gpu::ProgramHandle program = m_debug_shape_shader->getProgram(state, m_base_vertex_decl, 0, "");
			const Renderer::TransientSlice vb = m_renderer.allocTransient(sizeof(BaseVertex) * tris.size() * 3);
			const Renderer::TransientSlice ub = m_renderer.allocUniform(&Matrix::IDENTITY.columns[0].x, sizeof(Matrix));
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

			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, sizeof(Matrix));
			stream.useProgram(program);
			stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			stream.bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(BaseVertex));
			stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
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
			const Renderer::TransientSlice vb = m_renderer.allocTransient(sizeof(BaseVertex) * lines.size() * 2);
			const Renderer::TransientSlice ub = m_renderer.allocUniform(&Matrix::IDENTITY.columns[0].x, sizeof(Matrix));
			BaseVertex* vertices = (BaseVertex*)vb.ptr;
			for (u32 i = 0, c = lines.size(); i < c; ++i) {
				vertices[2 * i + 0].color = lines[i].color.abgr();
				vertices[2 * i + 0].pos = Vec3(lines[i].from - m_viewport.pos);
				vertices[2 * i + 1].color = lines[i].color.abgr();
				vertices[2 * i + 1].pos = Vec3(lines[i].to - m_viewport.pos);
			}
			m_module->clearDebugLines();

			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, sizeof(Matrix));
			stream.useProgram(program);
			stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			stream.bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(BaseVertex));
			stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			stream.drawArrays(0, vb.size / sizeof(BaseVertex));
		});
	}

	void renderDebugShapes()
	{
		renderDebugTriangles();
		renderDebugLines();
		//renderDebugPoints();
	}

	void render2D() {
		Matrix matrix;
		matrix.setOrtho(0, (float)m_viewport.w, (float)m_viewport.h, 0, 0, 1, false);
		renderUIHelper(m_draw2d, false, matrix);
		m_draw2d.clear(getAtlasSize());
	}

	void renderUIHelper(const Draw2D& data, bool is_3d, const Matrix& matrix) {
		if (!m_draw2d_shader->isReady()) return;
		if (data.getIndices().empty()) return;

		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();

		DrawStream& stream = m_renderer.getDrawStream();
		
		const Renderer::TransientSlice idx_buffer_mem = m_renderer.allocTransient(data.getIndices().byte_size());
		const Renderer::TransientSlice vtx_buffer_mem = m_renderer.allocTransient(data.getVertices().byte_size());
		memcpy(idx_buffer_mem.ptr, data.getIndices().begin(), data.getIndices().byte_size());
		memcpy(vtx_buffer_mem.ptr, data.getVertices().begin(), data.getVertices().byte_size());

		gpu::StateFlags state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
		state = state | gpu::StateFlags::SCISSOR_TEST;
		if (is_3d) state = state | gpu::StateFlags::DEPTH_FN_GREATER; 
		const gpu::ProgramHandle program = m_draw2d_shader->getProgram(state, m_2D_decl, 0, "");
		const Renderer::TransientSlice ub = m_renderer.allocUniform(&matrix, sizeof(matrix));

		stream.pushDebugGroup("draw2d");

		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, sizeof(Matrix));
		u32 elem_offset = 0;
		stream.useProgram(program);
		stream.bindIndexBuffer(idx_buffer_mem.buffer);
		stream.bindVertexBuffer(0, vtx_buffer_mem.buffer, vtx_buffer_mem.offset, 20);
		stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

		for (Draw2D::Cmd& cmd : data.getCmds()) {
			if(cmd.clip_size.x < 0) {
				stream.scissor(0, 0, m_viewport.w, m_viewport.h);
			}
			else {
				const u32 h = u32(clamp(cmd.clip_size.y, 0.f, 65535.f));
				if (gpu::isOriginBottomLeft()) {
					stream.scissor(u32(maximum(cmd.clip_pos.x, 0.0f)),
						m_viewport.h - u32(maximum(cmd.clip_pos.y, 0.0f)) - h,
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
			if (cmd.texture) texture_id = *cmd.texture;
			if (!texture_id) texture_id = atlas_texture->handle;

			stream.bindTextures(&texture_id, 0, 1);
			stream.drawIndexed(idx_buffer_mem.offset + elem_offset * sizeof(u32), cmd.indices_count, gpu::DataType::U32);

			elem_offset += cmd.indices_count;
		}
		stream.popDebugGroup();
	}

	void setWorld(World* world) override {
		RenderModule* module = world ? (RenderModule*)world->getModule("renderer") : nullptr;
		if (m_module == module) return;
		m_module = module;
		if (m_lua_state && m_module) callInitModule();
	}
	
	Renderer& getRenderer() const override { return m_renderer; }

	RenderModule* getModule() const override { return m_module; }

	CustomCommandHandler& addCustomCommandHandler(const char* name) override 
	{
		auto& handler = m_custom_commands_handlers.emplace();
		copyString(handler.name, name);
		handler.hash = RuntimeHash(name);
		exposeCustomCommandToLua(handler);
		return handler;
	}

	static gpu::TextureFormat getFormat(const char* name)
	{
		static const struct
		{
			const char* name;
			gpu::TextureFormat value;
		} FORMATS[] = {
			{"depth32", gpu::TextureFormat::D32},
			{"depth24stencil8", gpu::TextureFormat::D24S8},
			{"rg8", gpu::TextureFormat::RG8},
			{"rgba8", gpu::TextureFormat::RGBA8},
			{"srgba", gpu::TextureFormat::SRGBA},
			{"srgb", gpu::TextureFormat::SRGB},
			{"rgba16", gpu::TextureFormat::RGBA16},
			{"rgba16f", gpu::TextureFormat::RGBA16F},
			{"rgba32f", gpu::TextureFormat::RGBA32F},
			{"r16f", gpu::TextureFormat::R16F},
			{"r16", gpu::TextureFormat::R16},
			{"rg16", gpu::TextureFormat::RG16},
			{"r8", gpu::TextureFormat::R8},
			{"r32f", gpu::TextureFormat::R32F},
			{"rg32f", gpu::TextureFormat::RG32F},
			{"rg16f", gpu::TextureFormat::RG16F},
			{"rgb32f", gpu::TextureFormat::RGB32F},
			{"r11g11b10f", gpu::TextureFormat::R11G11B10F },
		};

		for (auto& i : FORMATS)
		{
			if (equalStrings(i.name, name)) return i.value;
		}
		logError("Unknown texture format ", name);
		return gpu::TextureFormat::RGBA8;
	}

	i32 toRenderbufferIdx(lua_State* L, int idx) const {
		LuaWrapper::checkTableArg(L, idx);
		const int type = LuaWrapper::getField(L, idx, "value");
		if (type != LUA_TNUMBER) {
			LuaWrapper::argError(L, idx, "renderbuffer");
		}
		const i32 rb = LuaWrapper::toType<i32>(L, -1);
		lua_pop(L, 1);

		if (rb < 0 || rb >= m_renderbuffers.size()) {
			luaL_argerror(L, idx, "invalid renderbuffer");
		}

		return rb;
	}

	void releaseRenderbuffer(lua_State* L) {
		const i32 rb = toRenderbufferIdx(L, 1);

		if (rb >= 0 && rb < m_renderbuffers.size()) {
			m_renderbuffers[rb].frame_counter = 2;
		}
	}

	void keepRenderbufferAlive(lua_State* L) {
		const i32 rb = toRenderbufferIdx(L, 1);

		if (rb >= 0 && rb < m_renderbuffers.size()) {
			--m_renderbuffers[rb].frame_counter;
		}
	}

	PipelineTexture createRenderbuffer(RenderbufferDescHandle desc_handle) {
		PipelineTexture res;
		res.type = PipelineTexture::RENDERBUFFER;

		const RenderbufferDesc& desc = m_renderbuffer_descs[desc_handle];
		IVec2 size;
		switch (desc.type) {
			case RenderbufferDesc::FIXED: size = desc.fixed_size; break;
			case RenderbufferDesc::RELATIVE: size = IVec2(i32(desc.rel_size.x * m_viewport.w), i32(desc.rel_size.y * m_viewport.h)); break;
			case RenderbufferDesc::DISPLAY_SIZE: size = m_display_size; break;
		}
		for (Renderbuffer& rb : m_renderbuffers) {
			if (!rb.handle) {
				rb.frame_counter = 0;
				StaticString<128> name(desc.debug_name, " ", u32(&rb - m_renderbuffers.begin()));
				rb.handle = m_renderer.createTexture(size.x, size.y, 1, desc.format, desc.flags, Renderer::MemRef(), name);
				rb.flags = desc.flags;
				rb.format = desc.format;
				rb.size = size;
				res.renderbuffer = u32(&rb - m_renderbuffers.begin());
				return res;
			}
			else {
				if (rb.frame_counter < 2) continue;
				if (rb.size != size) continue;
				if (rb.format != desc.format) continue;
				if (rb.flags != desc.flags) continue;
			}
			rb.frame_counter = 0;
			res.renderbuffer = u32(&rb - m_renderbuffers.begin());
			StaticString<128> name(desc.debug_name, " ", u32(&rb - m_renderbuffers.begin()));
			m_renderer.getDrawStream().setDebugName(rb.handle, name);
			return res;
		}

		Renderbuffer& rb = m_renderbuffers.emplace();
		rb.handle = m_renderer.createTexture(size.x, size.y, 1, desc.format, desc.flags, Renderer::MemRef(), desc.debug_name);
		rb.frame_counter = 0;
		rb.flags = desc.flags;
		rb.format = desc.format;
		rb.size = size;
		res.renderbuffer = m_renderbuffers.size() - 1;
		return res;
	}

	RenderbufferDescHandle createRenderbufferDesc(lua_State* L) {
		PROFILE_FUNCTION();

		LuaWrapper::checkTableArg(L, 1);
		IVec2 fixed_size;
		Vec2 rel_size;
		char format_str[64];
		char debug_name[64] = "";
		bool point_filter = false;
		bool compute_write = false;
		RenderbufferDesc::Type type = RenderbufferDesc::FIXED;
		if (!LuaWrapper::getOptionalField(L, 1, "size", &fixed_size)) {
			type = RenderbufferDesc::RELATIVE;
			if (!LuaWrapper::getOptionalField(L, 1, "rel_size", &rel_size)) {
				bool display_size = false;
				if (LuaWrapper::getOptionalField(L, 1, "display_size", &display_size) && display_size) {
					type = RenderbufferDesc::DISPLAY_SIZE;
				}
				else {
					rel_size = Vec2(1, 1);
				}
			}
		}
		else {
			type = RenderbufferDesc::FIXED;
		}

		if (!LuaWrapper::checkStringField(L, 1, "format", Span(format_str))) luaL_argerror(L, 1, "missing format");
		LuaWrapper::getOptionalStringField(L, 1, "debug_name", Span(debug_name));
		LuaWrapper::getOptionalField(L, 1, "point_filter", &point_filter);
		LuaWrapper::getOptionalField(L, 1, "compute_write", &compute_write);
		gpu::TextureFlags flags = gpu::TextureFlags::RENDER_TARGET 
			| gpu::TextureFlags::NO_MIPS
			| gpu::TextureFlags::CLAMP_U
			| gpu::TextureFlags::CLAMP_V
			| (compute_write ? gpu::TextureFlags::COMPUTE_WRITE : gpu::TextureFlags::NONE);
		if (point_filter) flags = flags | gpu::TextureFlags::POINT_FILTER;

		const gpu::TextureFormat format = getFormat(format_str);

		RenderbufferDesc& rb = m_renderbuffer_descs.emplace();
		rb.fixed_size = fixed_size;
		rb.rel_size = rel_size;
		rb.format = format;
		rb.flags = flags;
		rb.type = type;
		rb.debug_name = debug_name;

		return m_renderbuffer_descs.size() - 1;
	}

	enum class CameraParamsEnum : CameraParamsHandle {
		MAIN,
		SHADOW0,
		SHADOW1,
		SHADOW2,
		SHADOW3
	};

	CameraParams resolveCameraParams(CameraParamsHandle handle) {
		switch ((CameraParamsEnum)handle) {
			case CameraParamsEnum::MAIN: {
				CameraParams cp;
				cp.pos = m_viewport.pos;
				cp.frustum = m_viewport.getFrustum();
				cp.lod_multiplier = m_module->getCameraLODMultiplier(m_viewport.fov, m_viewport.is_ortho);
				cp.is_shadow = false;
				cp.view = m_viewport.getView(cp.pos);
				cp.projection = m_viewport.getProjectionWithJitter();
				return cp;
			}
			case CameraParamsEnum::SHADOW0:
			case CameraParamsEnum::SHADOW1:
			case CameraParamsEnum::SHADOW2:
			case CameraParamsEnum::SHADOW3: {
				const Viewport& vp = m_shadow_camera_viewports[(u32)handle - (u32)CameraParamsEnum::SHADOW0];
				CameraParams cp;
				cp.pos = vp.pos;
				cp.frustum = vp.getFrustum();
				cp.lod_multiplier = m_module->getCameraLODMultiplier(vp.fov, vp.is_ortho);
				cp.is_shadow = true;
				cp.view = vp.getView(cp.pos);
				cp.projection = vp.getProjectionNoJitter();
				return cp;
			}
		}
		return {};
	}

	void renderTerrains(CameraParamsHandle cp_handle, RenderStateHandle render_state_handle, LuaWrapper::Optional<const char*> define) {
		const CameraParams cp = resolveCameraParams(cp_handle);
		const u32 define_mask = define.valid && define.value[0] ? 1 << m_renderer.getShaderDefineIdx(define.value) : 0;
		const gpu::StateFlags render_state = m_render_states[render_state_handle];

		m_renderer.pushJob("terrain", [this, cp, render_state, define_mask](DrawStream& stream){
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
				const gpu::ProgramHandle program = shader->getProgram(render_state | terrain->m_material->m_render_states, decl, define_mask | terrain->m_material->getDefineMask(), "");
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
				};

				Quad quad;
				quad.pos = Vec4(pos, 0);
				quad.lpos = Vec4(rot.conjugated().rotate(-pos), 0);
				quad.hm_size = hm_size;

				ref_pos = rot.conjugated().rotate(-ref_pos);
				IVec4 prev_from_to;
				float s = scale.x / terrain->m_tesselation;
				bool first = true;
				
				stream.useProgram(program);
				stream.bindIndexBuffer(gpu::INVALID_BUFFER);
				stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

				stream.bind(0, material->m_bind_group);
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
					
						const Renderer::TransientSlice ub = m_renderer.allocUniform(&quad, sizeof(quad));

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
	
	void renderGrass(lua_State* L, CameraParamsHandle cp_handle, LuaWrapper::Optional<RenderStateHandle> state_handle) {
		PROFILE_FUNCTION();
		const CameraParams cp = resolveCameraParams(cp_handle);
		if (!cp.is_shadow) {
			for (Terrain* terrain : m_module->getTerrains()) {
				const Transform tr = m_module->getWorld().getTransform(terrain->m_entity);
				Transform rel_tr = tr;
				rel_tr.pos = tr.pos - cp.pos;
				terrain->createGrass(Vec2(-(float)rel_tr.pos.x, -(float)rel_tr.pos.z), m_renderer.frameNumber());
			}
		}

		u32 define_mask = 0;
		if (lua_istable(L, 2)) {
			const char* define = "";
			LuaWrapper::getOptionalField<const char*>(L, 2, "define", &define);
			define_mask = define[0] ? 1 << m_renderer.getShaderDefineIdx(define) : 0;

			if (LuaWrapper::getField(L, 2, "defines") != LUA_TNIL) {
				LuaWrapper::forEachArrayItem<const char*>(L, -1, "array of strings expeceted", [&](const char* define){
					define_mask |= 1 << m_renderer.getShaderDefineIdx(define);
				});
			}
			lua_pop(L, 1);
		}
		define_mask |= 1 << m_renderer.getShaderDefineIdx("GRASS");
		gpu::StateFlags render_state = state_handle.valid ? m_render_states[state_handle.value] : gpu::StateFlags::NONE;

		gpu::VertexDecl grass_instance_decl(gpu::PrimitiveType::NONE);
		grass_instance_decl.addAttribute(0, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		grass_instance_decl.addAttribute(16, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		m_renderer.pushJob("grass", [this, cp, define_mask, render_state, grass_instance_decl](DrawStream& stream){
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

						Shader* shader = mesh.material->getShader();
						ASSERT(shader->isReady());

						const Material* material = mesh.material;
						gpu::StateFlags state = render_state | material->m_render_states;

						gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, grass_instance_decl, define_mask | material->getDefineMask(), mesh.semantics_defines);
						stream.useProgram(program);
						stream.bind(0, material->m_bind_group);
						stream.bindIndexBuffer(mesh.index_buffer_handle);
						stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
						
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
							if (instance_count  == 0) {
								++culled_count;
								continue;
							}

							const Vec4 drawcall_data(rel_pos, distance);
							const Renderer::TransientSlice drawcall_ub = m_renderer.allocUniform(&drawcall_data, sizeof(drawcall_data));
							stream.bindUniformBuffer(UniformBuffer::DRAWCALL, drawcall_ub.buffer, drawcall_ub.offset, drawcall_ub.size);
							stream.bindVertexBuffer(1, quad.instances, 0, sizeof(Vec4) * 2);
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

		const auto& particle_systems = m_module->getParticleSystems();
		if (particle_systems.size() == 0) return;
			
		Sorter::Inserter inserter(view.sorter);
		// TODO culling

		jobs::forEach(particle_systems.capacity(), 1, [&](i32 idx, i32){
			const ParticleSystem* system = particle_systems.getFromIndex(idx);
			if (!system) return;
			
			PROFILE_BLOCK("setup particles");
			for (ParticleSystem::Emitter& emitter : system->getEmitters()) {
				const Material* material = emitter.resource_emitter.material;
				if (!material) continue;

				const u8 bucket_idx = view.layer_to_bucket[material->getLayer()];
				if (bucket_idx == 0xff) continue;

				const u32 size = emitter.getParticlesDataSizeBytes();
				if (size == 0) continue;

				emitter.slice = m_renderer.allocTransient(size);
				emitter.fillInstanceData((float*)emitter.slice.ptr, m_renderer.getEngine().getPageAllocator());
			}
		});

		for (const ParticleSystem& system : particle_systems) {
			for (ParticleSystem::Emitter& emitter : system.getEmitters()) {
				const Material* material = emitter.resource_emitter.material;
				if (!material) continue;

				const u8 bucket_idx = view.layer_to_bucket[material->getLayer()];
				if (bucket_idx == 0xff) continue;

				const u32 size = emitter.getParticlesDataSizeBytes();
				if (size == 0) continue;
				
				ASSERT(emitter.particles_count > 0);

				const u64 type_mask = (u64)RenderableTypes::PARTICLES << 32;
				const u32 emitter_idx = u32(&emitter - system.getEmitters().begin());
				const u64 subrenderable = system.m_entity.index | type_mask | ((u64)emitter_idx << SORT_KEY_EMITTER_SHIFT);
				inserter.push(material->getSortKey() | ((u64)bucket_idx << SORT_KEY_BUCKET_SHIFT), subrenderable);
			}
		}
	}

	void bindShaderBuffer(LuaBufferHandle buffer_handle, u32 binding_point, bool writable) {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.bindShaderBuffer(buffer_handle, binding_point, writable ? gpu::BindShaderBufferFlags::OUTPUT : gpu::BindShaderBufferFlags::NONE);
	}
	
	LuaBufferHandle createBuffer(u32 size) {
		Renderer::MemRef mem;
		mem.own = false;
		mem.data = nullptr;
		mem.size = size;
		const gpu::BufferHandle buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::COMPUTE_WRITE | gpu::BufferFlags::SHADER_BUFFER);
		m_buffers.push(buffer);
		return buffer;
	}
	
	PipelineTexture createTexture3D(u32 width, u32 height, u32 depth, const char* format_str, LuaWrapper::Optional<const char*> debug_name) {
		const gpu::TextureFormat format = getFormat(format_str);
		const gpu::TextureHandle texture = m_renderer.createTexture(width
			, height
			, depth
			, format
			, gpu::TextureFlags::IS_3D | gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS
			, Renderer::MemRef()
			, debug_name.get("lua_texture"));
		m_textures.push(texture);

		PipelineTexture res;
		res.type = PipelineTexture::RAW;
		res.raw = texture;
		return res;
	}
	
	PipelineTexture createTextureArray(u32 width, u32 height, u32 depth, const char* format_str, LuaWrapper::Optional<const char*> debug_name) {
		const gpu::TextureFormat format = getFormat(format_str);
		const gpu::TextureHandle texture = m_renderer.createTexture(width
			, height
			, depth
			, format
			, gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS
			, Renderer::MemRef()
			, debug_name.get("lua_texture"));
		m_textures.push(texture);

		PipelineTexture res;
		res.type = PipelineTexture::RAW;
		res.raw = texture;
		return res;
	}

	PipelineTexture createTexture2D(u32 width, u32 height, const char* format_str, LuaWrapper::Optional<const char*> debug_name) {
		const gpu::TextureFormat format = getFormat(format_str);
		const gpu::TextureHandle texture = m_renderer.createTexture(width
			, height
			, 1
			, format
			, gpu::TextureFlags::CLAMP_U | gpu::TextureFlags::CLAMP_V | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE
			, Renderer::MemRef()
			, debug_name.get("lua_texture"));
		m_textures.push(texture);

		PipelineTexture res;
		res.type = PipelineTexture::RAW;
		res.raw = texture;
		return res;
	}

	static int drawcallUniforms(lua_State* L) {
		PipelineImpl* pipeline = LuaWrapper::getClosureObject<PipelineImpl>(L);
		const int len = lua_gettop(L);

		const Renderer::TransientSlice ub = pipeline->m_renderer.allocUniform(sizeof(float) * len);

		float* values = (float*)ub.ptr;
		for(int i = 0; i < len; ++i) {
			values[i] = LuaWrapper::checkArg<float>(L, i + 1);
		}

		DrawStream& stream = pipeline->m_renderer.getDrawStream();
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
		return 0;
	}

	void dispatch(u32 shader_id, u32 num_groups_x, u32 num_groups_y, u32 num_groups_z, LuaWrapper::Optional<const char*> define) {
		Shader* shader = nullptr;
		for (const ShaderRef& s : m_shaders) {
			if(s.id == shader_id) {
				shader = s.res;
				break;
			}
		}
		if (!shader || !shader->isReady()) return;

		u32 defines = 0;
		if (define.valid) {
			defines |= 1 << m_renderer.getShaderDefineIdx(define.value);
		}
		gpu::ProgramHandle program = shader->getProgram(defines);
		if (!program) return;

		DrawStream& stream = m_renderer.getDrawStream();
		stream.useProgram(program);
		stream.dispatch(num_groups_x, num_groups_y, num_groups_z);
	}

	gpu::TextureHandle toHandle(PipelineTexture tex) const {
		switch (tex.type) {
			case PipelineTexture::RENDERBUFFER: return tex.renderbuffer < (u32)m_renderbuffers.size() ? m_renderbuffers[tex.renderbuffer].handle : gpu::INVALID_TEXTURE;
			case PipelineTexture::RAW: return tex.raw;
			case PipelineTexture::RESOURCE: {
				if (tex.resource == -2) return m_shadow_atlas.texture;
				if (tex.resource == -3) return m_module->getReflectionProbesTexture();

				Resource* res = m_renderer.getEngine().getLuaResource(tex.resource);
				if (res->getType() != Texture::TYPE) return gpu::INVALID_TEXTURE;
				return ((Texture*)res)->handle;
			}
		}
		ASSERT(false);
		return gpu::INVALID_TEXTURE;
	}

	void bindImageTexture(PipelineTexture texture, u32 unit) {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.bindImageTexture(toHandle(texture), unit);
	}

	void bindTextures(lua_State* L, LuaWrapper::Array<PipelineTexture, 16> textures, LuaWrapper::Optional<u32> offset) 	{
		StackArray<gpu::TextureHandle, 16> textures_handles(m_allocator);
		for (u32 i = 0; i < textures.size; ++i) {
			textures_handles.push(toHandle(textures[i]));
		}

		DrawStream& stream = m_renderer.getDrawStream();
		stream.bindTextures(textures_handles.begin(), offset.get(0), textures_handles.size());
	};
	
	void pass(CameraParamsHandle cp_handle) {
		const CameraParams cp = resolveCameraParams(cp_handle);
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

		const Renderer::TransientSlice ub = m_renderer.allocUniform(&pass_state, sizeof(PassState));
		DrawStream& stream = m_renderer.getDrawStream();
		stream.bindUniformBuffer(UniformBuffer::PASS, ub.buffer, ub.offset, ub.size);
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

	void drawArray(lua_State* L, i32 indices_offset, i32 indices_count, i32 shader_id) {
		PROFILE_FUNCTION();
		LuaWrapper::DebugGuard guard(L);
		if (lua_gettop(L) > 3) LuaWrapper::checkTableArg(L, 4);
		
		LuaWrapper::Optional<RenderStateHandle> state_handle = LuaWrapper::checkArg<LuaWrapper::Optional<RenderStateHandle>>(L, 5);
		const gpu::StateFlags rs = state_handle.valid ? m_render_states[state_handle.value] : gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FN_GREATER;

		Shader* shader = nullptr;
		for (const ShaderRef& s : m_shaders) {
			if(s.id == shader_id) {
				shader = s.res;
				break;
			}
		}
		if (!shader) {
			luaL_error(L, "Unknown shader id %d.", shader_id);
			return;
		}

		if (shader->isFailure()) {
			luaL_error(L, "Shader %s failed to load. `drawArrays` has no effect.", shader->getPath().c_str());
			return;
		}
		if (!shader->isReady()) return;

		
		StackArray<gpu::TextureHandle, 16> textures_handles(m_allocator);
		u32 define_mask = 0;
		if(lua_gettop(L) > 3) {
			const u32 len = (u32)lua_objlen(L, 4);

			for(u32 i = 0; i < len; ++i) {
				lua_rawgeti(L, 4, i + 1);
				if (!LuaWrapper::isType<PipelineTexture>(L, -1)) luaL_argerror(L, 4, "expected textures");
				const PipelineTexture tex = LuaWrapper::toType<PipelineTexture>(L, -1);
				textures_handles.push(toHandle(tex));
				lua_pop(L, 1);
			}
		
			if (lua_isstring(L, 6)) {
				const char* define = lua_tostring(L, 6);
				define_mask = 1 << m_renderer.getShaderDefineIdx(define);
			}
			else if (lua_istable(L, 6)) {
				lua_pushnil(L);
				while (lua_next(L, 6) != 0) {
					if(lua_type(L, -1) != LUA_TSTRING) {
						luaL_error(L, "%s", "Incorrect define arguments of drawArrays");
					}
					const char* define = lua_tostring(L, -1);
					define_mask |= 1 << m_renderer.getShaderDefineIdx(define);
					lua_pop(L, 1);
				}
			}
		}
	
		DrawStream& stream = m_renderer.getDrawStream();
		stream.bindTextures(textures_handles.begin(), 0, textures_handles.size());
		const gpu::ProgramHandle program = shader->getProgram(rs, gpu::VertexDecl(gpu::PrimitiveType::TRIANGLE_STRIP), define_mask, "");
		stream.useProgram(program);
		stream.bindIndexBuffer(gpu::INVALID_BUFFER);
		stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
		stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
		stream.drawArrays(indices_offset, indices_count);
	}

	CameraParamsHandle getCameraParams() { return (CameraParamsHandle)CameraParamsEnum::MAIN; }

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

	void renderUI() {
		PROFILE_FUNCTION();
		for (RenderPlugin* plugin : m_renderer.getPlugins()) {
			plugin->renderUI(*this);
		}
	}

	void renderOpaque() {
		PROFILE_FUNCTION();
		for (RenderPlugin* plugin : m_renderer.getPlugins()) {
			plugin->renderOpaque(*this);
		}
	}

	void renderTransparent() {
		PROFILE_FUNCTION();
		for (RenderPlugin* plugin : m_renderer.getPlugins()) {
			plugin->renderTransparent(*this);
		}
	}

	bool renderAA(PipelineTexture color, PipelineTexture velocity, PipelineTexture depth, PipelineTexture output) {
		PROFILE_FUNCTION();
		for (RenderPlugin* plugin : m_renderer.getPlugins()) {
			if (plugin->renderAA(*this, toHandle(color), toHandle(velocity), toHandle(depth), toHandle(output))) return true;
		}
		return false;
	}

	void enablePixelJitter(bool enable) override {
		m_is_pixel_jitter_enabled = enable;
	}

	Matrix getShadowMatrix(const PointLight& light, u32 atlas_idx) {
		Matrix prj;
		prj.setPerspective(light.fov, 1, 0.1f, light.range, true);
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

	void setupFur(View& view) {
		if (view.cp.is_shadow) return;

		HashMap<EntityRef, FurComponent>& furs = m_module->getFurs();
		if (furs.empty()) return;

		Span<const ModelInstance> mi = m_module->getModelInstances();
		Sorter::Inserter inserter(view.sorter);
		
		const u64 type_mask = (u64)RenderableTypes::FUR << 32;
		
		// TODO frustum culling
		// TODO render correct LOD
		for (auto iter = furs.begin(); iter.isValid(); ++iter) {
			const EntityRef e = iter.key();
			if (e.index >= (i32)mi.length()) continue;
			if ((mi[e.index].flags & ModelInstance::VALID) == 0) continue;
			if (!iter.value().enabled) continue;

			const Model* model = mi[e.index].model;
			if (!model) continue;
			if (!model->isReady()) continue;

			for (i32 i = 0; i < model->getMeshCount(); ++i) {
				const Mesh& mesh = model->getMesh(i);
				if (mesh.type != Mesh::SKINNED) continue;

				const u8 bucket_id = view.layer_to_bucket[mesh.material->getLayer()];
				if (bucket_id != 0xff) {
					const u64 key = mesh.sort_key | ((u64)bucket_id << SORT_KEY_BUCKET_SHIFT);
					const u64 subrenderable = e.index | type_mask | ((u64)i << SORT_KEY_MESH_IDX_SHIFT);
				
					inserter.push(key, subrenderable);
				}
			}
		}
	}

	void encodeProceduralGeometry(View& view) {
		const World& world = m_module->getWorld();
		const HashMap<EntityRef, ProceduralGeometry>& geometries = m_module->getProceduralGeometries();
		const DVec3 camera_pos = view.cp.pos;
		for (auto iter = geometries.begin(), end = geometries.end(); iter != end; ++iter) {
			const ProceduralGeometry& pg = iter.value();
			if (!pg.vertex_buffer) continue;
			if (!pg.material || !pg.material->isReady()) continue;

			u8 bucket_idx = view.layer_to_bucket[pg.material->getLayer()];
			if (bucket_idx == 0xff) continue;

			Bucket& bucket = view.buckets[bucket_idx];
			const gpu::StateFlags render_state = bucket.state;
			const Matrix mtx = world.getRelativeMatrix(iter.key(), camera_pos);
			const Renderer::TransientSlice ub = m_renderer.allocUniform(&mtx, sizeof(Matrix));

			bucket.stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
			const gpu::StateFlags state = pg.material->m_render_states | render_state;
			const gpu::ProgramHandle program = pg.material->getShader()->getProgram(state, pg.vertex_decl, bucket.define_mask | pg.material->getDefineMask(), "");

			const u32 stride = pg.vertex_decl.getStride();
			
			bucket.stream.useProgram(program);
			bucket.stream.bind(0, pg.material->m_bind_group);
			bucket.stream.bindIndexBuffer(pg.index_buffer);
			bucket.stream.bindVertexBuffer(0, pg.vertex_buffer, 0, stride);
			bucket.stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

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

	void encodeInstancedModels(DrawStream& stream, View& view) {
		PROFILE_FUNCTION();
		if (!m_instancing_shader->isReady()) return;

		const float global_lod_multiplier = m_renderer.getLODMultiplier();
		const World& world = m_module->getWorld();
		const HashMap<EntityRef, InstancedModel>& ims = m_module->getInstancedModels();
		
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
		};

		UBValues ub_values;
		toPlanes(view.cp, Span(ub_values.camera_planes));

		const gpu::BufferHandle culled_buffer = m_instanced_meshes_buffer;
		stream.bindShaderBuffer(m_indirect_buffer, 2, gpu::BindShaderBufferFlags::OUTPUT);
		const gpu::ProgramHandle gather_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS3"));
		const gpu::ProgramHandle indirect_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS2"));
		u32 cull_shader_defines = 1 << m_renderer.getShaderDefineIdx("PASS1");
		if (!view.cp.is_shadow) cull_shader_defines |= 1 << m_renderer.getShaderDefineIdx("UPDATE_LODS");
		const gpu::ProgramHandle cull_shader = m_instancing_shader->getProgram(cull_shader_defines);
		const gpu::ProgramHandle init_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("PASS0"));
		const gpu::ProgramHandle update_lods_shader = m_instancing_shader->getProgram(1 << m_renderer.getShaderDefineIdx("UPDATE_LODS"));

		for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
			const InstancedModel& im = iter.value();
			Model* m = im.model;
			if (!m || !m->isReady()) continue;

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
				Renderer::TransientSlice ub;
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
							const Renderer::TransientSlice ub = m_renderer.allocUniform(sizeof(u32) * 2);
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
			ASSERT((u32)m->getMeshCount() < lengthOf(ub_values.indices_count)); // TODO
			for (i32 i = 0; i < m->getMeshCount(); ++i) {
				const Mesh& mesh = m->getMesh(i);
				ub_values.indices_count[i].x = mesh.indices_count;
			}

			const Renderer::TransientSlice drawcall_ub = m_renderer.allocUniform(&ub_values, sizeof(ub_values));

			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, drawcall_ub.buffer, drawcall_ub.offset, sizeof(UBValues));

			stream.bindShaderBuffer(culled_buffer, 1, gpu::BindShaderBufferFlags::OUTPUT);
			stream.useProgram(init_shader);
			stream.dispatch(1, 1, 1);
			stream.memoryBarrier(gpu::MemoryBarrierType::SSBO, culled_buffer);

			if (view.cp.is_shadow) {
				stream.bindShaderBuffer(im.gpu_data, 0, gpu::BindShaderBufferFlags::NONE);
			}
			else {
				stream.bindShaderBuffer(im.gpu_data, 0, gpu::BindShaderBufferFlags::OUTPUT);
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
			stream.memoryBarrier(gpu::MemoryBarrierType::SSBO, culled_buffer);

			if (!view.cp.is_shadow) {
				stream.bindShaderBuffer(im.gpu_data, 0, gpu::BindShaderBufferFlags::NONE);
			}

			stream.useProgram(indirect_shader);
			stream.dispatch((m->getMeshCount() + 255) / 256, 1, 1);
			stream.memoryBarrier(gpu::MemoryBarrierType::SSBO, m_indirect_buffer);

			stream.useProgram(gather_shader);
			for (u32 i = 0; i < cell_count; ++i) {
				if (cells[i].visible) {
					stream.bindUniformBuffer(UniformBuffer::DRAWCALL2, cells[i].ub.buffer, cells[i].ub.offset, cells[i].ub.size);
					stream.dispatch((cells[i].count + 255) / 256, 1, 1);
				}
			}

			stream.memoryBarrier(gpu::MemoryBarrierType::SSBO, culled_buffer);

			const u32 instanced_define = 1 << m_renderer.getShaderDefineIdx("INSTANCED");
			for (i32 i = 0; i < m->getMeshCount(); ++i) {
				const Mesh& mesh = m->getMesh(i);
				const u8 bucket_idx = view.layer_to_bucket[mesh.layer];
				if (bucket_idx == 0xff) continue;
				
				Bucket& bucket = view.buckets[bucket_idx];
				const u32 instanced_define_mask = bucket.define_mask | instanced_define;
				Shader* shader = mesh.material->getShader();
				const Material* material = mesh.material;
				const gpu::StateFlags state = material->m_render_states | bucket.state;
				const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, instanced_define_mask | mesh.material->getDefineMask(), mesh.semantics_defines);

				bucket.stream.useProgram(program);
				bucket.stream.bind(0, material->m_bind_group);
				bucket.stream.bindIndexBuffer(mesh.index_buffer_handle);
				bucket.stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
				bucket.stream.bindVertexBuffer(1, m_instanced_meshes_buffer, 48, 32);
				
				bucket.stream.bindIndirectBuffer(m_indirect_buffer);
				bucket.stream.drawIndirect(mesh.index_type, u32(sizeof(Indirect) * (indirect_offset + i)));

				bucket.stream.bindIndirectBuffer(gpu::INVALID_BUFFER);
				bucket.stream.bindIndexBuffer(gpu::INVALID_BUFFER);
				bucket.stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				bucket.stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			}
		}

		stream.memoryBarrier(gpu::MemoryBarrierType::COMMAND, m_indirect_buffer);
			
		stream.bindShaderBuffer(gpu::INVALID_BUFFER, 0, gpu::BindShaderBufferFlags::NONE);
		stream.bindShaderBuffer(gpu::INVALID_BUFFER, 1, gpu::BindShaderBufferFlags::NONE);
		stream.bindShaderBuffer(gpu::INVALID_BUFFER, 2, gpu::BindShaderBufferFlags::NONE);	

		stream.memoryBarrier(gpu::MemoryBarrierType::COMMAND, m_indirect_buffer);
	}

	static int cull(lua_State* L) {
		PROFILE_FUNCTION();
		
		PipelineImpl* pipeline = LuaWrapper::getClosureObject<PipelineImpl>(L);
		const CameraParamsHandle cp_handle = LuaWrapper::checkArg<CameraParamsHandle>(L, 1);
		const CameraParams cp = pipeline->resolveCameraParams(cp_handle);
		const i32 bucket_count = lua_gettop(L) - 1;
		for (i32 i = 0; i < bucket_count; ++i) LuaWrapper::checkTableArg(L, 2 + i);
		
		UniquePtr<View>& view = pipeline->m_views.emplace();
		LinearAllocator& allocator = pipeline->m_renderer.getCurrentFrameAllocator();
		view = UniquePtr<View>::create(allocator, allocator, pipeline->m_renderer.getEngine().getPageAllocator());
		view->cp = cp;
		memset(view->layer_to_bucket, 0xff, sizeof(view->layer_to_bucket));

		view->buckets.reserve(bucket_count);
		for (i32 i = 0; i < bucket_count; ++i) {
			char layer[32];
			if (!LuaWrapper::checkStringField(L, 2 + i, "layer", Span(layer))) {
				LuaWrapper::argError(L, 2 + i, "expected table with `layer` key");
			}
			if (equalStrings(layer, "view")) {
				LuaWrapper::argError(L, 2 + i, "layer name `view` is reserved");
			}
			Bucket& bucket = view->buckets.emplace(pipeline->m_renderer);
			bucket.layer = pipeline->m_renderer.getLayerIdx(layer);
			copyString(Span(bucket.layer_name), layer); 
			
			char sort[32];
			if (LuaWrapper::getOptionalStringField(L, 2 + i, "sort", Span(sort))) {
				bucket.sort = equalIStrings(sort, "depth") ? Bucket::DEPTH : Bucket::DEFAULT;
			}

			char define[32];
			if (LuaWrapper::getOptionalStringField(L, 2 + i, "define", Span(define))) {
				bucket.define_mask = 1 << pipeline->m_renderer.getShaderDefineIdx(define);
			}

			RenderStateHandle state_handle;
			if (LuaWrapper::getOptionalField<RenderStateHandle>(L, 2 + i, "state", &state_handle)) {
				bucket.state = pipeline->m_render_states[state_handle];
			}
		}

		for (i32 i = 0; i < view->buckets.size(); ++i) {
			Bucket& bucket = view->buckets[i];
			view->layer_to_bucket[bucket.layer] = i;
		}

		if (pipeline->m_instancing_shader->isReady()) {
			const HashMap<EntityRef, InstancedModel>& ims = pipeline->m_module->getInstancedModels();
			for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
				if (iter.value().dirty) {
					pipeline->m_module->initInstancedModelGPUData(iter.key());
				}
			}
		}

		View* view_ptr = view.get();
		jobs::setRed(&view->ready);
		pipeline->m_renderer.pushJob("prepare view", [pipeline, view_ptr](DrawStream& stream){
			pipeline->setupFur(*view_ptr);
			pipeline->setupParticles(*view_ptr);
			pipeline->encodeInstancedModels(stream, *view_ptr);
			pipeline->encodeProceduralGeometry(*view_ptr);

			view_ptr->renderables = pipeline->m_module->getRenderables(view_ptr->cp.frustum);
			
			if (view_ptr->renderables) {
				pipeline->createSortKeys(*view_ptr);
				view_ptr->renderables->free(pipeline->m_renderer.getEngine().getPageAllocator());
				if (!view_ptr->sorter.keys.empty()) {
					pipeline->radixSort(view_ptr->sorter.keys.begin(), view_ptr->sorter.values.begin(), view_ptr->sorter.keys.size());
					pipeline->createCommands(*view_ptr);
				}
			}

			jobs::setGreen(&view_ptr->ready);
		});

		lua_newtable(L);
		const u32 view_id = pipeline->m_views.size() - 1;
		LuaWrapper::setField(L, -1, "view", view_id);
		for (u32 i = 0; i < (u32)view->buckets.size(); ++i) {
			LuaWrapper::setField(L, -1, view->buckets[i].layer_name, (view_id << 16) | i);
		}
		return 1;
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
		
		u64 instance_key_mask;
		u32 define_mask;
		u32 autoinstanced_define_mask;
		u32 dynamic_define_mask;
		u32 skinned_define_mask;
		u32 fur_define_mask;
		const Mesh** sort_key_to_mesh = m_renderer.getSortKeyToMeshMap();
		u8 prev_bucket = (sort_keys[0] >> SORT_KEY_BUCKET_SHIFT) + 1; // make sure it's different from the first bucket, so first iteration is properly initialized
		DrawStream* stream = nullptr;
		gpu::StateFlags render_state;

		gpu::VertexDecl dyn_instance_decl(gpu::PrimitiveType::NONE);
		dyn_instance_decl.addAttribute(0, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		dyn_instance_decl.addAttribute(16, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		dyn_instance_decl.addAttribute(32, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		dyn_instance_decl.addAttribute(48, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		dyn_instance_decl.addAttribute(64, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		dyn_instance_decl.addAttribute(80, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		gpu::VertexDecl instanced_decl(gpu::PrimitiveType::NONE);
		instanced_decl.addAttribute(0, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		instanced_decl.addAttribute(16, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		instanced_decl.addAttribute(32, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		for (u32 i = 0; i < keys_count; ++i) {
			const EntityRef entity = {int(renderables[i] & 0xFFffFFff)};
			const RenderableTypes type = RenderableTypes((renderables[i] >> 32) & SORT_VALUE_TYPE_MASK);
			const u8 bucket = sort_keys[i] >> SORT_KEY_BUCKET_SHIFT;

			if (bucket != prev_bucket) {
				prev_bucket = bucket;
				stream = &view.buckets[bucket].stream;
				define_mask = view.buckets[bucket].define_mask;
				autoinstanced_define_mask = define_mask | (1 << m_renderer.getShaderDefineIdx("AUTOINSTANCED"));
				dynamic_define_mask = define_mask | (1 << m_renderer.getShaderDefineIdx("DYNAMIC"));
				skinned_define_mask = define_mask | (1 << m_renderer.getShaderDefineIdx("SKINNED"));
				fur_define_mask = define_mask | (1 << m_renderer.getShaderDefineIdx("FUR"));
				const bool sort_depth = view.buckets[bucket].sort == Bucket::DEPTH;
				instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
				render_state = view.buckets[bucket].state;
			}

			switch(type) {
				case RenderableTypes::PARTICLES: {
					const u32 emitter_idx = u32(renderables[i] >> SORT_KEY_EMITTER_SHIFT);
					const ParticleSystem& particle_system = m_module->getParticleSystem(entity);
					const ParticleSystem::Emitter& emitter = particle_system.getEmitter(emitter_idx);
					const Material* material = emitter.resource_emitter.material;
					const u32 particles_count = emitter.particles_count;

					const Transform tr = world.getTransform(*particle_system.m_entity);
					const Vec3 lpos = Vec3(tr.pos - camera_pos);
					const gpu::VertexDecl& decl = emitter.resource_emitter.vertex_decl;
					const gpu::StateFlags state = material->m_render_states | render_state;
					gpu::ProgramHandle program = material->getShader()->getProgram(state, decl, define_mask | material->getDefineMask(), "");
					const Renderer::TransientSlice slice = emitter.slice;
					const Matrix mtx(lpos, tr.rot);

					const Renderer::TransientSlice ub = m_renderer.allocUniform(&mtx, sizeof(Matrix));
					stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
					stream->bind(0, material->m_bind_group);
					stream->useProgram(program);
					stream->bindIndexBuffer(gpu::INVALID_BUFFER);
					stream->bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
					stream->bindVertexBuffer(1, slice.buffer, slice.offset, decl.getStride());
					stream->drawArraysInstanced(4, particles_count);
					break;
				}
				case RenderableTypes::MESH_MATERIAL_OVERRIDE: {
					const u32 mesh_idx = u32(renderables[i] >> SORT_KEY_MESH_IDX_SHIFT);
					const ModelInstance* LUMIX_RESTRICT mi = &model_instances[entity.index];
					const Mesh& mesh = mi->meshes[mesh_idx];

					const Renderer::TransientSlice slice = m_renderer.allocTransient(sizeof(Vec4) * 3);
					u8* instance_data = slice.ptr;
					const Transform& tr = transforms[entity.index];
					const float lod_d = model_instances[entity.index].lod - mesh.lod;
					const Vec3 lpos = Vec3(tr.pos - camera_pos);
					memcpy(instance_data, &tr.rot, sizeof(tr.rot));
					instance_data += sizeof(tr.rot);
					memcpy(instance_data, &lpos, sizeof(lpos));
					instance_data += sizeof(lpos);
					memcpy(instance_data, &lod_d, sizeof(lod_d));
					instance_data += sizeof(lod_d);
					memcpy(instance_data, &tr.scale, sizeof(tr.scale));
					instance_data += sizeof(tr.scale) + sizeof(float)/*padding*/;

					if (mi->custom_material->isReady()) {
						Shader* shader = mi->custom_material->getShader();
						const Material* material =  mi->custom_material;

						const gpu::StateFlags state = material->m_render_states | render_state;
						const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, instanced_decl, autoinstanced_define_mask | material->getDefineMask(), mesh.semantics_defines);
						stream->useProgram(program);
						stream->bind(0, material->m_bind_group);
						stream->bindIndexBuffer(mesh.index_buffer_handle);
						stream->bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
						stream->bindVertexBuffer(1, slice.buffer, slice.offset, 48);
						stream->drawIndexedInstanced(mesh.indices_count, 1, mesh.index_type);
					}
					break;
				}
				case RenderableTypes::MESH: {
					if (sort_keys[i] & SORT_KEY_INSTANCED_FLAG) {
						const u32 group_idx = renderables[i] & 0xffFF;
						const u32 instancer_idx = (renderables[i] >> SORT_KEY_INSTANCER_SHIFT) & 0xffFF;
						const AutoInstancer::Instances& instances = view.instancers[instancer_idx].instances[group_idx];
						const u32 total_count = instances.end->offset + instances.end->count;
						const Mesh& mesh = *sort_key_to_mesh[group_idx];

						const Material* material = mesh.material;
						Shader* shader = material->getShader();
						const gpu::StateFlags state = material->m_render_states | render_state;
						const u32 defines = autoinstanced_define_mask | material->getDefineMask();
						const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, instanced_decl, defines, mesh.semantics_defines);
						
						stream->useProgram(program);
						stream->bind(0, material->m_bind_group);
						stream->bindIndexBuffer(mesh.index_buffer_handle);
						stream->bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
						stream->bindVertexBuffer(1, instances.slice.buffer, instances.slice.offset, 48);
						stream->drawIndexedInstanced(mesh.indices_count, total_count, mesh.index_type);
					}
					else {
						const u32 mesh_idx = u32(renderables[i] >> SORT_KEY_MESH_IDX_SHIFT);
						ModelInstance* LUMIX_RESTRICT mi = &model_instances[entity.index];
						const Mesh& mesh = mi->meshes[mesh_idx];
						const float mesh_lod = mesh.lod;
						const Material* material = mesh.material;
						Shader* shader = material->getShader();
						const gpu::StateFlags state = material->m_render_states | render_state;

						if (mi->flags & ModelInstance::MOVED) {
							// we could merge drawcalls here, same as we do for transparent meshes in next `else`
							const Renderer::TransientSlice slice = m_renderer.allocTransient((sizeof(Vec4) * 6));
							u8* instance_data = slice.ptr;
							const EntityRef e = { i32(renderables[i] & 0xFFffFFff) };
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
							instance_data += sizeof(tr.scale) + sizeof(float)/*padding*/;

							const Transform prev_tr = mi->prev_frame_transform;
							const Vec3 prev_lpos = Vec3(prev_tr.pos - camera_pos);

							memcpy(instance_data, &prev_tr.rot, sizeof(prev_tr.rot));
							instance_data += sizeof(prev_tr.rot);
							memcpy(instance_data, &prev_lpos, sizeof(prev_lpos));
							instance_data += sizeof(prev_lpos);
							memcpy(instance_data, &lod_d, sizeof(lod_d));
							instance_data += sizeof(lod_d);
							memcpy(instance_data, &prev_tr.scale, sizeof(prev_tr.scale));
							instance_data += sizeof(prev_tr.scale) + sizeof(float)/*padding*/;

							const u32 defines = dynamic_define_mask | material->getDefineMask();
							const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, dyn_instance_decl, defines, mesh.semantics_defines);
						
							stream->useProgram(program);
							stream->bind(0, material->m_bind_group);
							stream->bindIndexBuffer(mesh.index_buffer_handle);
							stream->bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
							stream->bindVertexBuffer(1, slice.buffer, slice.offset, 96);
							stream->drawIndexedInstanced(mesh.indices_count, 1, mesh.index_type);
						}
						else {
							int start_i = i;
							const u64 key = sort_keys[i] & instance_key_mask;
							while (i < keys_count && (sort_keys[i] & instance_key_mask) == key) {
								++i;
							}
							const u32 count = u32(i - start_i);
							const Renderer::TransientSlice slice = m_renderer.allocTransient(count * (sizeof(Vec4) * 3));
							u8* instance_data = slice.ptr;
							for (int j = start_i; j < start_i + (i32)count; ++j) {
								const EntityRef e = { i32(renderables[j] & 0xFFffFFff) };
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
								instance_data += sizeof(tr.scale) + sizeof(float)/*padding*/;
							}

							const u32 defines = autoinstanced_define_mask | material->getDefineMask();
							const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, instanced_decl, defines, mesh.semantics_defines);
						
							stream->useProgram(program);
							stream->bind(0, material->m_bind_group);
							stream->bindIndexBuffer(mesh.index_buffer_handle);
							stream->bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
							stream->bindVertexBuffer(1, slice.buffer, slice.offset, 48);
							stream->drawIndexedInstanced(mesh.indices_count, count, mesh.index_type);
							--i;
						}
					}
					break;
				}
				case RenderableTypes::FUR:
				case RenderableTypes::SKINNED: {
					const u32 mesh_idx = u32(renderables[i] >> SORT_KEY_MESH_IDX_SHIFT);
					ModelInstance* LUMIX_RESTRICT mi = &model_instances[entity.index];
					const Transform& tr = transforms[entity.index];
					const Vec3 rel_pos = Vec3(tr.pos - camera_pos);
					const Mesh& mesh = mi->meshes[mesh_idx];
					Shader* shader = mesh.material->getShader();
					u32 defines = skinned_define_mask | mesh.material->getDefineMask();
					if (type == RenderableTypes::FUR) defines |= fur_define_mask;

					const Quat* rotations = mi->pose->rotations;
					const Vec3* positions = mi->pose->positions;

					Model& model = *mi->model;

					struct UBPrefix {
						float fur_scale;
						float gravity;
						float layers;
						float padding;
						Matrix model_mtx;
						Matrix prev_model_mtx;
					};

					const Renderer::TransientSlice ub = m_renderer.allocUniform(sizeof(DualQuat) * mi->pose->count + sizeof(UBPrefix));
					UBPrefix* prefix = (UBPrefix*)ub.ptr;
					prefix->model_mtx = Matrix(rel_pos, tr.rot);
					prefix->model_mtx.multiply3x3(tr.scale);

					const Vec3 prev_rel_pos = Vec3(mi->prev_frame_transform.pos - camera_pos);
					prefix->prev_model_mtx = Matrix(prev_rel_pos, mi->prev_frame_transform.rot);
					prefix->prev_model_mtx.multiply3x3(mi->prev_frame_transform.scale);
							
					u32 layers = 1;
					if (type == RenderableTypes::FUR) {
						FurComponent& fur = m_module->getFur(entity);
						layers = fur.layers;
						prefix->fur_scale = fur.scale;
						prefix->gravity = fur.gravity;
					}
					prefix->layers = float(layers);

					DualQuat* bones_ub_array = (DualQuat*)(ub.ptr + sizeof(UBPrefix));
					for (int j = 0, c = mi->pose->count; j < c; ++j) {
						const Model::Bone& bone = model.getBone(j);
						const LocalRigidTransform tmp = {positions[j], rotations[j]};
						bones_ub_array[j] = (tmp * bone.inv_bind_transform).toDualQuat();
					}
					
					const Material* material = mesh.material;
					stream->bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
					const gpu::StateFlags state = material->m_render_states | render_state;
					const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, defines, mesh.semantics_defines);
					stream->useProgram(program);
					stream->bind(0, material->m_bind_group);
					stream->bindIndexBuffer(mesh.index_buffer_handle);
					stream->bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
					stream->bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
					stream->drawIndexedInstanced(mesh.indices_count, layers, mesh.index_type);
					break;
				}
				case RenderableTypes::DECAL: {
					const Material* material = m_module->getDecal(entity).material;

					int start_i = i;
					const u64 key = sort_keys[i];
					while (i < keys_count && sort_keys[i] == key) {
						++i;
					}
					const u32 count = i - start_i;
					struct DecalData {
						Vec3 pos;
						Quat rot;
						Vec3 half_extents;
						Vec2 uv_scale;
					};
					const Renderer::TransientSlice slice = m_renderer.allocTransient(count * (sizeof(DecalData)));

					DecalData* beg = (DecalData*)slice.ptr;
					DecalData* end = (DecalData*)(slice.ptr + (count - 1) * sizeof(DecalData));
					for(u32 j = start_i; j < i; ++j) {
						const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
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
						intersecting ? --end : ++beg;
					}

					stream->bind(0, material->m_bind_group);
					stream->bindIndexBuffer(m_cube_ib);
					stream->bindVertexBuffer(0, m_cube_vb, 0, 12);

					gpu::StateFlags state = material->m_render_states | render_state;
					state = state & ~gpu::StateFlags::CULL_FRONT | gpu::StateFlags::CULL_BACK;
					const u32 nonintersecting_count = u32(beg - (DecalData*)slice.ptr);
					if (nonintersecting_count) {
						stream->useProgram(material->getShader()->getProgram(state, m_decal_decl, define_mask | material->getDefineMask(), ""));
						stream->bindVertexBuffer(1, slice.buffer, slice.offset, 48);
						stream->drawIndexedInstanced(36, nonintersecting_count, gpu::DataType::U16);
					}

					if (count - nonintersecting_count) {
						state = state & ~gpu::StateFlags::DEPTH_FUNCTION;
						state = state & ~gpu::StateFlags::CULL_BACK;
						state = state | gpu::StateFlags::CULL_FRONT;
						stream->useProgram(material->getShader()->getProgram(state, m_decal_decl, define_mask | material->getDefineMask(), ""));
						const u32 offs = slice.offset + sizeof(float) * 12 * nonintersecting_count;
						stream->bindVertexBuffer(1, slice.buffer, offs, 48);
						stream->drawIndexedInstanced(36, count - nonintersecting_count, gpu::DataType::U16);
					}
					--i;
					break;
				}
				case RenderableTypes::CURVE_DECAL: {
					const Material* material = m_module->getCurveDecal(entity).material;

					int start_i = i;
					const u64 key = sort_keys[i];
					while (i < keys_count && sort_keys[i] == key) {
						++i;
					}
					const u32 count = i - start_i;
					struct DecalData {
						Vec3 pos;
						Quat rot;
						Vec3 half_extents;
						Vec2 uv_scale;
						Vec4 bezier;
					};
					const Renderer::TransientSlice slice = m_renderer.allocTransient(count * (sizeof(DecalData)));

					DecalData* beg = (DecalData*)slice.ptr;
					DecalData* end = (DecalData*)(slice.ptr + (count - 1) * sizeof(DecalData));
					for(u32 j = start_i; j < i; ++j) {
						const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
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
						intersecting ? --end : ++beg;
					}

					stream->bind(0, material->m_bind_group);
					stream->bindIndexBuffer(m_cube_ib);
					stream->bindVertexBuffer(0, m_cube_vb, 0, 12);

					gpu::StateFlags state = material->m_render_states | render_state;
					state = state & ~gpu::StateFlags::CULL_FRONT | gpu::StateFlags::CULL_BACK;
					const u32 nonintersecting_count = u32(beg - (DecalData*)slice.ptr);
					if (nonintersecting_count) {
						stream->useProgram(material->getShader()->getProgram(state, m_curve_decal_decl, define_mask | material->getDefineMask(), ""));
						stream->bindVertexBuffer(1, slice.buffer, slice.offset, 64);
						stream->drawIndexedInstanced(36, nonintersecting_count, gpu::DataType::U16);
					}

					if (count - nonintersecting_count) {
						state = state & ~gpu::StateFlags::DEPTH_FUNCTION;
						state = state & ~gpu::StateFlags::CULL_BACK;
						state = state | gpu::StateFlags::CULL_FRONT;
						stream->useProgram(material->getShader()->getProgram(state, m_curve_decal_decl, define_mask | material->getDefineMask(), ""));
						const u32 offs = slice.offset + sizeof(float) * 16 * nonintersecting_count;
						stream->bindVertexBuffer(1, slice.buffer, offs, 64);
						stream->drawIndexedInstanced(36, count - nonintersecting_count, gpu::DataType::U16);
					}
					--i;
					break;
				}
				case RenderableTypes::COUNT:
				case RenderableTypes::LOCAL_LIGHT:
					ASSERT(false);
					break;
			}
		}
	}

	void renderBucket(u32 viewbucket_id) {
		View* view = m_views[viewbucket_id >> 16].get();
		const u16 bucket_id = viewbucket_id & 0xffFF;
		m_renderer.pushJob("render bucket", [view, bucket_id](DrawStream& stream){
			jobs::wait(&view->ready);
			Bucket& bucket= view->buckets[bucket_id];
			stream.merge(bucket.stream);
		});
	}
	
	static float computeShadowPriority(float light_radius, const DVec3& light_pos, const DVec3& cam_pos) {
		return float(light_radius / length(cam_pos - light_pos));
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

		LinearAllocator& frame_allocator = m_renderer.getCurrentFrameAllocator();
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
					atlas_sorter.push(i, computeShadowPriority(light.radius, light_pos, cam_pos), e);
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

		for (u32 i = 0; i < atlas_sorter.count; ++i) {
			ClusterLight& light = lights[atlas_sorter.lights[i].idx];
			if (light.atlas_idx != -1 && ShadowAtlas::getGroup(i) != ShadowAtlas::getGroup(light.atlas_idx)) {
				m_shadow_atlas.remove(atlas_sorter.lights[i].entity);
				light.atlas_idx = -1;
			}
		}

		if (!m_shadow_atlas.texture) {
			m_shadow_atlas.texture = m_renderer.createTexture(ShadowAtlas::SIZE, ShadowAtlas::SIZE, 1, gpu::TextureFormat::D32, gpu::TextureFlags::NO_MIPS, Renderer::MemRef(), "shadow_atlas");
		}

		Matrix shadow_atlas_matrices[128];
		for (u32 i = 0; i < atlas_sorter.count; ++i) {
			ClusterLight& light = lights[atlas_sorter.lights[i].idx];
			EntityRef e = atlas_sorter.lights[i].entity;
			PointLight& pl = m_module->getPointLight(e);
			// TODO bakeShadow reenters m_lua_state, this is not safe since it's inside another job
			if (light.atlas_idx == -1) {
				light.atlas_idx = m_shadow_atlas.add(ShadowAtlas::getGroup(i), e);
				bakeShadow(pl, light.atlas_idx);
			}
			else if (pl.flags & PointLight::DYNAMIC) {
				bakeShadow(pl, light.atlas_idx);
			}
			shadow_atlas_matrices[light.atlas_idx] = getShadowMatrix(pl, light.atlas_idx);
		}
		const Renderer::TransientSlice shadow_matrices_ub = m_renderer.allocUniform(&shadow_atlas_matrices, sizeof(shadow_atlas_matrices));
		stream.bindUniformBuffer(UniformBuffer::SHADOW, shadow_matrices_ub.buffer, shadow_matrices_ub.offset, shadow_matrices_ub.size);

		m_renderer.pushJob([clusters_count, clusters, lights, lights_count, cam_pos, &world, size, cp, this, &frame_allocator](DrawStream& stream){
			auto bind = [](auto& buffer, const void* data, u32 size, i32 idx, DrawStream& stream){
				if (!size) return;
				const u32 capacity = (size + 15) & ~15;
				if (buffer.capacity < capacity) {
					if (buffer.buffer) stream.destroy(buffer.buffer);
					buffer.buffer = gpu::allocBufferHandle();
					stream.createBuffer(buffer.buffer, gpu::BufferFlags::SHADER_BUFFER, capacity, nullptr);
					buffer.capacity = capacity;
				}
				stream.update(buffer.buffer, data, size);
				stream.bindShaderBuffer(buffer.buffer, idx, gpu::BindShaderBufferFlags::NONE);
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
	
			qsort(refl_probes, module_refl_probes.length(), sizeof(ClusterReflProbe), [](const void* a, const void* b){
				const ClusterReflProbe* m = (const ClusterReflProbe*)a;
				const ClusterReflProbe* n = (const ClusterReflProbe*)b;
				const float m3 = m->half_extents.x * m->half_extents.y * m->half_extents.z;
				const float n3 = n->half_extents.x * n->half_extents.y * n->half_extents.z;
				if (m3 < n3) return -1;
				return m3 > n3 ? 1 : 0;
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
	
			qsort(env_probes, module_env_probes.length(), sizeof(ClusterEnvProbe), [](const void* a, const void* b){
				const ClusterEnvProbe* m = (const ClusterEnvProbe*)a;
				const ClusterEnvProbe* n = (const ClusterEnvProbe*)b;
				const float m3 = m->outer_range.x * m->outer_range.y * m->outer_range.z;
				const float n3 = n->outer_range.x * n->outer_range.y * n->outer_range.z;
				if (m3 < n3) return -1;
				return m3 > n3 ? 1 : 0;
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
		
			bind(m_cluster_buffers.lights, lights, lights_count * sizeof(lights[0]), 11, stream);
			bind(m_cluster_buffers.maps, map, offset * sizeof(i32), 13, stream);
			bind(m_cluster_buffers.env_probes, env_probes, module_env_probes.length() * sizeof(env_probes[0]), 14, stream);
			bind(m_cluster_buffers.refl_probes, refl_probes, module_refl_probes.length() * sizeof(refl_probes[0]), 15, stream);
			bind(m_cluster_buffers.clusters, clusters, sizeof(clusters[0]) * clusters_count, 12, stream);
		});
	}

	CameraParamsHandle getShadowCameraParams(i32 slice) { return (CameraParamsHandle)CameraParamsEnum::SHADOW0 + slice; }
	
	void setRenderTargets(Span<gpu::TextureHandle> renderbuffers, gpu::TextureHandle ds, bool readonly_ds, bool srgb, IVec2 viewport_size) {
		gpu::FramebufferFlags flags = srgb ? gpu::FramebufferFlags::SRGB : gpu::FramebufferFlags::NONE;
		if (readonly_ds) {
			flags = flags | gpu::FramebufferFlags::READONLY_DEPTH_STENCIL;
		}
		DrawStream& stream = m_renderer.getDrawStream();
		stream.setFramebuffer(renderbuffers.begin(), renderbuffers.length(), ds, flags);
		stream.viewport(0, 0, viewport_size.x, viewport_size.y);
	}

	static int setRenderTargets(lua_State* L, bool has_ds, bool readonly_ds) {
		PROFILE_FUNCTION();
		PipelineImpl* pipeline = LuaWrapper::getClosureObject<PipelineImpl>(L);

		const u32 rb_count = lua_gettop(L) - (has_ds ? 1 : 0);
		gpu::TextureHandle rbs[16];
		if(rb_count > lengthOf(rbs)) {
			luaL_error(L, "%s", "Too many render buffers");	
			return 0;
		}

		IVec2 size;
		size.x = pipeline->m_viewport.w;
		size.y = pipeline->m_viewport.h;

		for(u32 i = 0; i < rb_count; ++i) {
			const i32 rb_idx = pipeline->toRenderbufferIdx(L, i + 1);
			rbs[i] = pipeline->m_renderbuffers[rb_idx].handle;
			size = pipeline->m_renderbuffers[rb_idx].size;
		}

		gpu::TextureHandle ds = gpu::INVALID_TEXTURE;
		if (has_ds) {
			const int ds_idx = pipeline->toRenderbufferIdx(L, rb_count + 1);
			ds = pipeline->m_renderbuffers[ds_idx].handle;
		}

		pipeline->setRenderTargets(Span(rbs, rb_count), ds, readonly_ds, true, size);
		return 0;
	}

	static int setRenderTargets(lua_State* L) { 
		return setRenderTargets(L, false, false);
	}

	static int setRenderTargetsReadonlyDS(lua_State* L) { 
		return setRenderTargets(L, true, true);
	}

	static int setRenderTargetsDS(lua_State* L) {
		return setRenderTargets(L, true, false);
	}

	void createSortKeys(PipelineImpl::View& view) {
		if (view.renderables->header.count == 0 && !view.renderables->header.next) return;
		PagedListIterator<const CullResult> iterator(view.renderables);

		view.instancers.reserve(jobs::getWorkersCount());
		LinearAllocator& allocator = m_renderer.getCurrentFrameAllocator();
		for (u8 i = 0; i < jobs::getWorkersCount(); ++i) {
			view.instancers.emplace(allocator, m_renderer.getEngine().getPageAllocator());
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
			else if (view.buckets[bucket_map[i]].sort == Bucket::DEPTH) {
				bucket_map[i] |= 0x100;
			}
		}
		jobs::runOnWorkers([&](){
			PROFILE_BLOCK("create keys");
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
				const u64 type_mask = (u64)type << 32;
				
				switch(type) {
					case RenderableTypes::LOCAL_LIGHT: break;
					case RenderableTypes::DECAL: {
						for (int i = 0, c = page->header.count; i < c; ++i) {
							const EntityRef e = renderables[i];
							const Material* material = m_module->getDecal(e).material;
							const int layer = material->getLayer();
							const u8 bucket = bucket_map[layer];
							if (bucket < 0xff) {
								// TODO material can have the same sort key as mesh
								const u64 subrenderable = e.index | type_mask;
								inserter.push(material->getSortKey() | ((u64)bucket << SORT_KEY_BUCKET_SHIFT), subrenderable);
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
								// TODO material can have the same sort key as mesh
								const u64 subrenderable = e.index | type_mask;
								inserter.push(material->getSortKey() | ((u64)bucket << SORT_KEY_BUCKET_SHIFT), subrenderable);
							}
						}
						break;
					}
					case RenderableTypes::SKINNED:
					case RenderableTypes::MESH_MATERIAL_OVERRIDE: {
						for (int i = 0, c = page->header.count; i < c; ++i) {
							const EntityRef e = renderables[i];
							const DVec3 pos = transforms[e.index].pos;
							ModelInstance& mi = model_instances[e.index];
							const float squared_length = float(squaredLength(pos - lod_ref_point));
								
							const u32 lod_idx = mi.model->getLODMeshIndices(squared_length * global_lod_multiplier_rcp);

							auto create_key = [&](const LODMeshIndices& lod){
								for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
									const Mesh& mesh = mi.meshes[mesh_idx];
									const u8 layer = mi.custom_material ? mi.custom_material->getLayer() : mesh.layer;
									const u32 bucket = bucket_map[layer];
									const u32 mesh_sort_key = mi.custom_material ? 0x00FFffFF : mesh.sort_key;
									ASSERT(!mi.custom_material || mesh_idx == 0);
									const u64 subrenderable = e.index | type_mask | ((u64)mesh_idx << SORT_KEY_MESH_IDX_SHIFT);
									if (bucket < 0xff) {
										const u64 key = mesh_sort_key | ((u64)bucket << SORT_KEY_BUCKET_SHIFT);
										inserter.push(key, subrenderable);
									} else if (bucket < 0xffFF) {
										const DVec3 pos = transforms[e.index].pos;
										const DVec3 rel_pos = pos - camera_pos;
										const float squared_length = float(rel_pos.x * rel_pos.x + rel_pos.y * rel_pos.y + rel_pos.z * rel_pos.z);
										const u32 depth_bits = floatFlip(*(u32*)&squared_length);
										const u64 key = ((u64)bucket << SORT_KEY_BUCKET_SHIFT) | depth_bits;
										inserter.push(key, subrenderable);
									}
								}
							};

							if (mi.lod != lod_idx) {
								if (view.cp.is_shadow) {
									const u32 shadow_lod_idx = maximum((u32)mi.lod, lod_idx);
									create_key(mi.model->getLODIndices()[shadow_lod_idx]);
								}
								else {
									const float d = lod_idx - mi.lod;
									const float ad = fabsf(d);
									
									if (ad <= time_delta) {
										mi.lod = float(lod_idx);
										create_key(mi.model->getLODIndices()[lod_idx]);
									}
									else {
										mi.lod += d / ad * time_delta;
										const u32 cur_lod_idx = u32(mi.lod);
										create_key(mi.model->getLODIndices()[cur_lod_idx]);
										if (cur_lod_idx < 3) create_key(mi.model->getLODIndices()[cur_lod_idx + 1]);
									}
								}
							}
							else {
								const LODMeshIndices& lod = mi.model->getLODIndices()[lod_idx];
								create_key(lod);
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

							auto create_key = [&](const LODMeshIndices& lod){
								for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
									const Mesh& mesh = mi.meshes[mesh_idx];
									const u32 bucket = bucket_map[mesh.layer];
									ASSERT(!mi.custom_material);
									const u64 subrenderable = e.index | type_mask | ((u64)mesh_idx << SORT_KEY_MESH_IDX_SHIFT);
									if (mi.flags & ModelInstance::MOVED && !is_shadow) {
										// moved and unmoved meshes can't be drawn in single drawcall as they need different instance data
										// but autoinstancer groups all instances of a mesh in single drawcall
										// so we don't autoinstance moved meshes, only unmoved
										const u64 key = ((u64)bucket << SORT_KEY_BUCKET_SHIFT);
										inserter.push(key, subrenderable);
									}
									else if (bucket < 0xff) {
										instancer.add(mesh.sort_key, subrenderable);
									} else if (bucket < 0xffFF) {
										const DVec3 pos = transforms[e.index].pos;
										const DVec3 rel_pos = pos - camera_pos;
										const float squared_length = float(rel_pos.x * rel_pos.x + rel_pos.y * rel_pos.y + rel_pos.z * rel_pos.z);
										const u32 depth_bits = floatFlip(*(u32*)&squared_length);
										const u64 key = ((u64)bucket << SORT_KEY_BUCKET_SHIFT) | depth_bits;
										inserter.push(key, subrenderable);
									}
								}
							};

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
					case RenderableTypes::FUR:
					case RenderableTypes::COUNT:
						ASSERT(false);
						break;
				}
			}
			profiler::pushInt("count", total);

			const Mesh** sort_key_to_mesh = m_renderer.getSortKeyToMeshMap();
			for (u32 i = 0, c = (u32)instancer.instances.size(); i < c; ++i) {
				if (!instancer.instances[i].begin) continue;

				const Mesh* mesh = sort_key_to_mesh[i];
				const u8 bucket = view.layer_to_bucket[mesh->layer];
				inserter.push(SORT_KEY_INSTANCED_FLAG | i | ((u64)bucket << SORT_KEY_BUCKET_SHIFT), i | (instancer_idx << SORT_KEY_INSTANCER_SHIFT));
			}

			PROFILE_BLOCK("fill instance data");
			for (AutoInstancer::Instances& instances : instancer.instances) {
				const AutoInstancer::Page::Group* group = instances.begin;
				if (!group) continue;

				const u32 count = instances.end->offset + instances.end->count;
				instances.slice = m_renderer.allocTransient(count * (3 * sizeof(Vec4)));
				u8* instance_data = instances.slice.ptr;
				const u32 sort_key = u32(&instances - instancer.instances.begin());
				const Mesh* mesh = sort_key_to_mesh[sort_key];

				const float mesh_lod = mesh->lod;

				while (group) {
					for (u32 i = 0; i < group->count; ++i) {
						const EntityRef e = { (i32)group->renderables[i] };
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
						instance_data += sizeof(tr.scale) + sizeof(float) /*padding to vec4*/;
					}
					group = group->next;
				}
			}
		});

		view.sorter.pack();
	}

	struct Histogram {
		static constexpr u32 BITS = 11;
		static constexpr u32 SIZE = 1 << BITS;
		static constexpr u32 BIT_MASK = SIZE - 1;
		static constexpr i32 STEP = 4096;

		u32 m_histogram[SIZE];
		bool m_sorted;
		jobs::Mutex m_cs;

		void compute(const u64* keys, const u64* values, int size, u16 shift) {
			memset(m_histogram, 0, sizeof(m_histogram));
			m_sorted = true;

			AtomicI32 counter = 0;
			auto work = [&](){
				PROFILE_BLOCK("compute histogram");
				u32 histogram[SIZE];
				bool sorted = true;
				memset(histogram, 0, sizeof(histogram));

				i32 begin = counter.add(STEP);

				while (begin < size) {
					const i32 end = minimum(size, begin + STEP);

					u64 key = begin > 0 ? keys[begin - 1] : keys[0];
					u64 prev_key = key;
					for (i32 i = begin; i < end; ++i) {
						key = keys[i];
						const u16 index = (key >> shift) & BIT_MASK;
						++histogram[index];
						sorted &= prev_key <= key;
						prev_key = key;
					}
					begin = counter.add(STEP);
				}

				jobs::MutexGuard lock(m_cs);
				m_sorted &= sorted;
				for (u32 i = 0; i < lengthOf(m_histogram); ++i) {
					m_histogram[i] += histogram[i]; 
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

		u64* keys = _keys;
		u64* values = _values;
		u64* tmp_keys = nullptr;
		u64* tmp_values = nullptr;

		Histogram histogram;
		u16 shift = 0;

		for (int pass = 0; pass < 6; ++pass) {
			histogram.compute(keys, values, size, shift);

			if (histogram.m_sorted) {
				if (pass & 1) {
					memcpy(_keys, tmp_mem.begin(), tmp_mem.byte_size() / 2);
					memcpy(_values, &tmp_mem[size], tmp_mem.byte_size() / 2);
				}
				return;
			}

			if (!tmp_keys) {
				tmp_mem.resize(size * 2);
				tmp_keys = tmp_mem.begin();
				tmp_values = &tmp_mem[size];
			}

			u32 offset = 0;
			for (int i = 0; i < Histogram::SIZE; ++i) {
				const u32 count = histogram.m_histogram[i];
				histogram.m_histogram[i] = offset;
				offset += count;
			}

			for (int i = 0; i < size; ++i) {
				const u64 key = keys[i];
				const u16 index = (key >> shift) & Histogram::BIT_MASK;
				const u32 dest = histogram.m_histogram[index]++;
				tmp_keys[dest] = key;
				tmp_values[dest] = values[i];
			}

			swap(tmp_keys, keys);
			swap(tmp_values, values);

			shift += Histogram::BITS;
		}
	}

	void clear(u32 flags, float r, float g, float b, float a, float depth) {
		const Vec4 color = Vec4(r, g, b, a);
		DrawStream& stream = m_renderer.getDrawStream();
		stream.clear((gpu::ClearFlags)flags, &color.x, depth);
	}

	void viewport(int x, int y, int w, int h) {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.viewport(x, y, w, h);
	}

	void beginBlock(const char* name) {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.beginProfileBlock(name, 0);
	}

	void endBlock() {
		DrawStream& stream = m_renderer.getDrawStream();
		stream.endProfileBlock();
	}
	
	void setOutput(lua_State* L, PipelineTexture tex) {
		if (tex.type != PipelineTexture::RENDERBUFFER) LuaWrapper::argError(L, 1, "renderbuffer");
		
		m_output = tex.renderbuffer;
	}

	bool environmentCastShadows() {
		if (!m_module) return false;
		const EntityPtr env = m_module->getActiveEnvironment();
		if (!env.isValid()) return false;
		return m_module->getEnvironmentCastShadows((EntityRef)env);
	}

	RenderStateHandle createRenderState(RenderState state) {
		for (const gpu::StateFlags& s : m_render_states) {
			if (s == state.value) return RenderStateHandle(&s - m_render_states.begin());
		}
		m_render_states.push(state.value);
		return m_render_states.size() - 1;
	}

	int preloadShader(const char* path)
	{
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		ShaderRef s;
		s.res = rm.load<Shader>(Path(path));
		s.id = 0;
		for(ShaderRef& i : m_shaders) {
			if(i.id >= s.id) {
				s.id = i.id + 1;
			}
		}
		m_shaders.push(s);
		return s.id;
	}

	void callLuaFunction(const char* function) override 
	{
		if (!m_lua_state) return;

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, function);
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION)
		{
			lua_pop(m_lua_state, 2);
			return;
		}

		if (lua_pcall(m_lua_state, 0, 0, 0) != 0)
		{
			logWarning(lua_tostring(m_lua_state, -1));
			lua_pop(m_lua_state, 1);
		}
		lua_pop(m_lua_state, 1);
	}

	void saveRenderbuffer(lua_State* L) { 
		const i32 render_buffer = toRenderbufferIdx(L, 1);
		Path path = Path(LuaWrapper::checkArg<const char*>(L, 2));
		DrawStream& stream = m_renderer.getDrawStream();

		FileSystem* fs = &m_renderer.getEngine().getFileSystem();
		const Renderer::MemRef mem = m_renderer.allocate(sizeof(u32) * m_viewport.w * m_viewport.h);
		const gpu::TextureHandle staging = gpu::allocTextureHandle();
		const gpu::TextureFlags flags = gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::READBACK;
		stream.createTexture(staging, m_viewport.w, m_viewport.h, 1, gpu::TextureFormat::RGBA8, flags, "staging_buffer");
		stream.copy(staging, m_renderbuffers[render_buffer].handle, 0, 0);
		stream.readTexture(staging, 0, Span((u8*)mem.data, mem.size));
		stream.destroy(staging);
		const u32 w = m_viewport.w;
		const u32 h = m_viewport.h;
		stream.pushLambda([fs, path, w, h, mem, this](){
			os::OutputFile file;
			if (fs->open(path, file)) {
				Texture::saveTGA(&file, w, h, gpu::TextureFormat::RGBA8, (u8*)mem.data, false, path, m_renderer.getAllocator());
				file.close();
			}
			else {
				logError("Failed to save ", path);
			}
		});
		stream.freeMemory(mem.data, m_renderer.getAllocator());
	}

	float getRenderToDisplayRatio() const { return m_render_to_display_scale; }
	void setRenderToDisplayRatio(float scale) { m_render_to_display_scale = scale; }

	void registerLuaAPI(lua_State* L)
	{
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);

		auto registerCFunction = [L, this](const char* name, lua_CFunction function) {
			lua_pushlightuserdata(L, this);
			lua_pushcclosure(L, function, name, 1);
			lua_setfield(L, -3, name);
		};

		auto registerConst = [L](const char* name, u32 value)
		{
			lua_pushinteger(L, value);
			lua_setfield(L, -2, name);
		};

		#define REGISTER_FUNCTION(name) \
			do {\
				auto f = &LuaWrapper::wrapMethodClosure<&PipelineImpl::name>; \
				registerCFunction(#name, f); \
			} while(false) \

		REGISTER_FUNCTION(beginBlock);
		REGISTER_FUNCTION(bindImageTexture);
		REGISTER_FUNCTION(bindShaderBuffer);
		REGISTER_FUNCTION(bindTextures);
		REGISTER_FUNCTION(clear);
		REGISTER_FUNCTION(createBuffer);
		REGISTER_FUNCTION(createRenderbufferDesc);
		REGISTER_FUNCTION(createRenderbuffer);
		REGISTER_FUNCTION(createRenderState);
		REGISTER_FUNCTION(createTextureArray);
		REGISTER_FUNCTION(createTexture2D);
		REGISTER_FUNCTION(createTexture3D);
		REGISTER_FUNCTION(dispatch);
		REGISTER_FUNCTION(drawArray);
		REGISTER_FUNCTION(enablePixelJitter);
		REGISTER_FUNCTION(endBlock);
		REGISTER_FUNCTION(environmentCastShadows);
		REGISTER_FUNCTION(executeCustomCommand);
		REGISTER_FUNCTION(getCameraParams);
		REGISTER_FUNCTION(getShadowCameraParams);
		REGISTER_FUNCTION(getRenderToDisplayRatio);
		REGISTER_FUNCTION(keepRenderbufferAlive);
		REGISTER_FUNCTION(pass);
		REGISTER_FUNCTION(preloadShader);
		REGISTER_FUNCTION(releaseRenderbuffer);
		REGISTER_FUNCTION(render2D);
		REGISTER_FUNCTION(renderBucket);
		REGISTER_FUNCTION(renderDebugShapes);
		REGISTER_FUNCTION(renderGrass);
		REGISTER_FUNCTION(renderTerrains);
		REGISTER_FUNCTION(renderOpaque);
		REGISTER_FUNCTION(renderTransparent);
		REGISTER_FUNCTION(renderAA);
		REGISTER_FUNCTION(renderUI);
		REGISTER_FUNCTION(saveRenderbuffer);
		REGISTER_FUNCTION(setOutput);
		REGISTER_FUNCTION(setRenderToDisplayRatio);
		REGISTER_FUNCTION(viewport);

		lua_pushinteger(L, -2); lua_setfield(L, -2, "SHADOW_ATLAS");
		lua_pushinteger(L, -3); lua_setfield(L, -2, "REFLECTION_PROBES");

		registerConst("CLEAR_DEPTH", (u32)gpu::ClearFlags::DEPTH);
		registerConst("CLEAR_COLOR", (u32)gpu::ClearFlags::COLOR);
		registerConst("CLEAR_ALL", (u32)gpu::ClearFlags::COLOR | (u32)gpu::ClearFlags::DEPTH | (u32)gpu::ClearFlags::STENCIL);

		registerConst("DEPTH_FN_GREATER", (u32)gpu::StateFlags::DEPTH_FN_GREATER);
		registerConst("DEPTH_FN_EQUAL", (u32)gpu::StateFlags::DEPTH_FN_EQUAL);

		registerConst("STENCIL_ALWAYS", (u32)gpu::StencilFuncs::ALWAYS);
		registerConst("STENCIL_EQUAL", (u32)gpu::StencilFuncs::EQUAL);
		registerConst("STENCIL_NOT_EQUAL", (u32)gpu::StencilFuncs::NOT_EQUAL);
		registerConst("STENCIL_DISABLE", (u32)gpu::StencilFuncs::DISABLE);
		registerConst("STENCIL_KEEP", (u32)gpu::StencilOps::KEEP);
		registerConst("STENCIL_REPLACE", (u32)gpu::StencilOps::REPLACE);

		registerCFunction("cull", PipelineImpl::cull);
		registerCFunction("drawcallUniforms", PipelineImpl::drawcallUniforms);
		registerCFunction("setRenderTargets", PipelineImpl::setRenderTargets);
		registerCFunction("setRenderTargetsDS", PipelineImpl::setRenderTargetsDS);
		registerCFunction("setRenderTargetsReadonlyDS", PipelineImpl::setRenderTargetsReadonlyDS);

		#define REGISTER_DRAW2D_FUNCTION(fn_name) \
			do { \
				auto f = &LuaWrapper::wrapMethodClosure<&Draw2D::fn_name>; \
				lua_getfield(L, -1, "Draw2D");		 \
				if (lua_type(L, -1) == LUA_TNIL) {	 \
					lua_pop(L, 1);					 \
					lua_newtable(L);				 \
					lua_setfield(L, -2, "Draw2D");	 \
					lua_getfield(L, -1, "Draw2D");	 \
				}									 \
				lua_pushlightuserdata(L, &m_draw2d); \
				lua_pushcclosure(L, f, #fn_name, 1); \
				lua_setfield(L, -2, #fn_name);		 \
				lua_pop(L, 1);						 \
			} while(false)							 \

		REGISTER_DRAW2D_FUNCTION(addLine);
		REGISTER_DRAW2D_FUNCTION(addRect);
		REGISTER_DRAW2D_FUNCTION(addRectFilled);

		#undef REGISTER_DRAW2D_FUNCTION

		lua_pop(L, 1); // pop env

		#undef REGISTER_FUNCTION
	}

	bool isReady() const override { return m_resource->isReady(); }
	const Path& getPath() override { return m_resource->getPath(); }

	void clearDraw2D() override { return m_draw2d.clear(getAtlasSize()); }
	Draw2D& getDraw2D() override { return m_draw2d; }
	
	gpu::TextureHandle getOutput() override { 
		if (m_output < 0 || m_output >= m_renderbuffers.size()) return gpu::INVALID_TEXTURE;
		return m_renderbuffers[m_output].handle;
	}
	
	void setIndirectLightMultiplier(float value) override { m_indirect_light_multiplier = value; }
	
	IVec2 getDisplaySize() const override { return m_display_size; }

	IAllocator& m_allocator;
	Renderer& m_renderer;
	LuaScript* m_resource;
	lua_State* m_lua_state;
	int m_lua_thread_ref;
	int m_lua_env;
	StaticString<32> m_define;
	Array<gpu::StateFlags> m_render_states;
	RenderModule* m_module;
	Draw2D m_draw2d;
	Shader* m_draw2d_shader;
	Array<UniquePtr<View>> m_views;
	jobs::Signal m_buckets_ready;
	Viewport m_viewport;
	bool m_is_pixel_jitter_enabled = false;
	Viewport m_prev_viewport;
	IVec2 m_display_size;
	float m_render_to_display_scale = 1;
	float m_indirect_light_multiplier = 1;
	bool m_first_set_viewport = true;
	int m_output;
	Shader* m_debug_shape_shader;
	Shader* m_instancing_shader;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<RenderbufferDesc> m_renderbuffer_descs;
	Array<Renderbuffer> m_renderbuffers;
	Array<ShaderRef> m_shaders;
	Array<gpu::TextureHandle> m_textures;
	Array<gpu::BufferHandle> m_buffers;
	os::Timer m_timer;
	AtomicI32 m_indirect_buffer_offset = 0;
	gpu::BufferHandle m_instanced_meshes_buffer;
	gpu::BufferHandle m_indirect_buffer;
	gpu::VertexDecl m_base_vertex_decl;
	gpu::VertexDecl m_base_line_vertex_decl;
	gpu::VertexDecl m_2D_decl;
	gpu::VertexDecl m_decal_decl;
	gpu::VertexDecl m_curve_decal_decl;
	gpu::BufferHandle m_cube_vb;
	gpu::BufferHandle m_cube_ib;
	
	ShadowAtlas m_shadow_atlas;

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
};


UniquePtr<Pipeline> Pipeline::create(Renderer& renderer, LuaScript* resource, const char* define)
{
	return UniquePtr<PipelineImpl>::create(renderer.getAllocator(), renderer, resource, define, renderer.getAllocator());
}


} // namespace Lumix