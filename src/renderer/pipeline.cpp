#include "gpu/gpu.h"
#include "engine/crt.h"
#include "engine/allocators.h"
#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/atomic.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/sync.h"
#include "engine/os.h"
#include "engine/page_allocator.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "culling_system.h"
#include "draw2d.h"
#include "font.h"
#include "material.h"
#include "model.h"
#include "particle_system.h"
#include "pipeline.h"
#include "pose.h"
#include "renderer.h"
#include "render_scene.h"
#include "shader.h"
#include "terrain.h"
#include "texture.h"
//-V:WRITE:568
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
static constexpr u64 SORT_KEY_DEPTH_MASK = 0xffFFffFF;
static constexpr u64 SORT_KEY_INSTANCER_SHIFT = 16;

struct CameraParams
{
	ShiftedFrustum frustum;
	DVec3 pos;
	float lod_multiplier;
	bool is_shadow;
	Matrix view;
	Matrix projection;
};

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
		gpu::StateFlags rs = gpu::StateFlags::DEPTH_TEST | gpu::StateFlags::DEPTH_WRITE;
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

		LuaWrapper::getOptionalFlagField(L, idx, "depth_test", &rs, gpu::StateFlags::DEPTH_TEST, true);
		LuaWrapper::getOptionalFlagField(L, idx, "wireframe", &rs, gpu::StateFlags::WIREFRAME, false);
		LuaWrapper::getOptionalFlagField(L, idx, "depth_write", &rs, gpu::StateFlags::DEPTH_WRITE, true);
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

	template <>
	CameraParams toType(lua_State* L, int idx)
	{
		CameraParams cp;

		lua_getfield(L, idx, "view");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			luaL_error(L, "View matrix is not a table");
		}

		for (int i = 0; i < 16; ++i) {
			lua_rawgeti(L, -1, i + 1);
			if (!LuaWrapper::isType<float>(L, -1)) {
				lua_pop(L, 2);
				luaL_error(L, "View matrix must contain exactly 16 floats");
			}
			cp.view[i] = LuaWrapper::toType<float>(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "projection");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			luaL_error(L, "Projection matrix is not a table");
		}

		for (int i = 0; i < 16; ++i) {
			lua_rawgeti(L, -1, i + 1);
			if (!LuaWrapper::isType<float>(L, -1)) {
				lua_pop(L, 2);
				luaL_error(L, "Projection matrix must contain exactly 16 floats");
			}
			cp.projection[i] = LuaWrapper::toType<float>(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "frustum");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			luaL_error(L, "Frustum is not a table");
		}
		if(!LuaWrapper::checkField(L, -1, "origin", &cp.frustum.origin)) {
				lua_pop(L, 1);
				luaL_error(L, "Frustum without origin");
		}
		auto load_floats = [L](float* data, int count, int offset) {
			for (int i = 0; i < count; ++i) {
				lua_rawgeti(L, -1, offset + i + 1);
				if(!LuaWrapper::isType<float>(L, -1)) {
					lua_pop(L, 2);
					luaL_error(L, "Invalid frustum");
				}
				data[i] = LuaWrapper::toType<float>(L, -1);
				lua_pop(L, 1);
			}
		};
		load_floats(cp.frustum.xs, (int)Frustum::Planes::COUNT, 0);
		load_floats(cp.frustum.ys, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT);
		load_floats(cp.frustum.zs, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT * 2);
		load_floats(cp.frustum.ds, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT * 3);
		load_floats(&cp.frustum.points[0].x, 24, (int)Frustum::Planes::COUNT * 4);
		
		lua_pop(L, 1);
		cp.frustum.setPlanesFromPoints();
		
		if(!LuaWrapper::checkField(L, idx, "lod_multiplier", &cp.lod_multiplier)) {
			luaL_error(L, "Missing lod_multiplier in camera params");
		}

		if (!LuaWrapper::checkField(L, idx, "is_shadow", &cp.is_shadow)) {
			luaL_error(L, "Missing is_shadow in camera params");
		}

		if(!LuaWrapper::checkField(L, idx, "position", &cp.pos)) {
			luaL_error(L, "Missing position in camera params");
		}

		return cp;
	}

	template <> inline bool isType<RenderState>(lua_State* L, int index)
	{
		return lua_istable(L, index);
	}

	template <> inline bool isType<PipelineTexture>(lua_State* L, int index)
	{
		return lua_istable(L, index) || lua_isnumber(L, index);
	}

	template <> inline bool isType<CameraParams>(lua_State* L, int index)
	{
		return lua_istable(L, index);
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
			default: ASSERT(false); return;
		}
	}

	void push(lua_State* L, const CameraParams& params)
	{
		lua_createtable(L, 0, 4);

		lua_createtable(L, 32+24, 0);
		auto push_floats = [L](const float* values, int count, int offset){
			for(int i = 0; i < count; ++i) {
				LuaWrapper::push(L, values[i]);
				lua_rawseti(L, -2, offset + i + 1);
			}
		};

		push_floats(params.frustum.xs, (int)Frustum::Planes::COUNT, 0);
		push_floats(params.frustum.ys, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT);
		push_floats(params.frustum.zs, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT * 2);
		push_floats(params.frustum.ds, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT * 3);
		push_floats(&params.frustum.points[0].x, 24, (int)Frustum::Planes::COUNT * 4);

		LuaWrapper::push(L, params.frustum.origin);
		lua_setfield(L, -2, "origin");
		lua_setfield(L, -2, "frustum");

		LuaWrapper::setField(L, -1, "is_shadow", params.is_shadow);
		LuaWrapper::setField(L, -1, "position", params.pos);
		LuaWrapper::setField(L, -1, "lod_multiplier", params.lod_multiplier);

		lua_createtable(L, 16, 0);
		for (int i = 0; i < 16; ++i) {
			LuaWrapper::push(L, params.view[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "view");

		lua_createtable(L, 16, 0);
		for (int i = 0; i < 16; ++i) {
			LuaWrapper::push(L, params.projection[i]);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "projection");
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
	Matrix camera_inv_projection;
	Matrix camera_view;
	Matrix camera_inv_view;
	Matrix camera_view_projection;
	Matrix camera_inv_view_projection;
	Matrix camera_reprojection;
	Vec4 cam_world_pos;
	Vec4 light_direction;
	Vec4 light_color;
	IVec2 framebuffer_size;
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


ResourceType PipelineResource::TYPE("pipeline");


void PipelineResource::unload()
{
	content.clear();
}


bool PipelineResource::load(u64 size, const u8* mem)
{
	content.resize((int)size);
	memcpy(content.begin(), mem, size);
	return true;
}


PipelineResource::PipelineResource(const Path& path, ResourceManager& owner, Renderer&, IAllocator& allocator)
	: Resource(path, owner, allocator)
	, content(allocator) 
{}


struct alignas(4096) CmdPage
{
	struct {
		CmdPage* next = nullptr;
		int size = 0;
		u8 bucket = 0;
	} header;
	u8 data[PageAllocator::PAGE_SIZE - sizeof(header)];
};


struct PipelineImpl final : Pipeline
{
	struct Renderbuffer {
		u32 width;
		u32 height;
		gpu::TextureFormat format;
		gpu::TextureHandle handle;
		int frame_counter;
		bool persistent;
		gpu::TextureFlags flags;
	};

	struct ShaderRef {
		Lumix::Shader* res;
		int id;
	};

	struct Bucket {
		enum Sort {
			DEFAULT,
			DEPTH
		};
		u8 layer;
		char layer_name[32];
		Sort sort = DEFAULT;
		u32 define_mask = 0;
		CmdPage* cmd_page = nullptr; 
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
				page_allocator.deallocate(p, false);
				p = p->header.next;
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
	
	struct InstancedMeshes {
		InstancedMeshes(IAllocator& allocator) : models(allocator) {}

		struct Model {
			Model(IAllocator& allocator) : meshes(allocator) {}

			struct MeshRenderData {
				Mesh::RenderData* mesh_rd;
				Material::RenderData* material;
				gpu::ProgramHandle program;
				const Mesh* mesh;
				u8 layer;
			};

			struct Cell {
				u32 offset;
				u32 count;
				bool visible;
				Renderer::TransientSlice ub;
			};

			Cell cells[16];
			u32 cell_count = 0;
			Renderer::TransientSlice drawcall_ub;
			Transform origin;
			gpu::BufferHandle instance_data;
			u32 instance_count;
			u32 indirect_offset;
			float radius;
			Vec4 lod_distances;
			IVec4 lod_indices;
			Array<MeshRenderData> meshes;
			u32 offset;
		};

		Array<Model> models;
		InstancedMeshes* next = nullptr;
	};

	struct View {
		View(LinearAllocator& allocator, PageAllocator& page_allocator) 
			: sorter(allocator, page_allocator)
			, instancers(allocator)
			, buckets(allocator)
		{
			instanced_meshes = LUMIX_NEW(allocator, InstancedMeshes)(allocator);
		}

		View(View&& rhs) = delete;
		View(const View& rhs) = delete;
		void operator=(View&&) = delete;

		Array<Bucket> buckets;
		Array<AutoInstancer> instancers;
		InstancedMeshes* instanced_meshes;
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


	PipelineImpl(Renderer& renderer, PipelineResource* resource, const char* define, IAllocator& allocator)
		: m_allocator(allocator)
		, m_renderer(renderer)
		, m_resource(resource)
		, m_lua_state(nullptr)
		, m_custom_commands_handlers(allocator)
		, m_define(define)
		, m_scene(nullptr)
		, m_draw2d(allocator)
		, m_output(-1)
		, m_renderbuffers(allocator)
		, m_shaders(allocator)
		, m_shadow_atlas(allocator)
		, m_textures(allocator)
		, m_buffers(allocator)
		, m_views(allocator)
	{
		m_viewport.w = m_viewport.h = 800;
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_draw2d_shader = rm.load<Shader>(Path("pipelines/draw2d.shd"));
		m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));
		m_place_grass_shader = rm.load<Shader>(Path("pipelines/place_grass.shd"));
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

		m_base_vertex_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
		m_base_vertex_decl.addAttribute(1, 12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		m_simple_cube_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);

		m_decal_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
		m_decal_decl.addAttribute(1, 0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(2, 12, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(3, 28, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(4, 40, 2, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		m_curve_decal_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
		m_curve_decal_decl.addAttribute(1, 0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(2, 12, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(3, 28, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(4, 40, 2, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_curve_decal_decl.addAttribute(5, 48, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		m_2D_decl.addAttribute(0, 0, 2, gpu::AttributeType::FLOAT, 0);
		m_2D_decl.addAttribute(1, 8, 2, gpu::AttributeType::FLOAT, 0);
		m_2D_decl.addAttribute(2, 16, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		m_3D_pos_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);

		m_point_light_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0); // vpos
		m_point_light_decl.addAttribute(1, 0, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // rot
		m_point_light_decl.addAttribute(2, 16, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // pos
		m_point_light_decl.addAttribute(3, 28, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // radius
		m_point_light_decl.addAttribute(4, 32, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // attn
		m_point_light_decl.addAttribute(5, 36, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // color
		m_point_light_decl.addAttribute(6, 48, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // dir
		m_point_light_decl.addAttribute(7, 60, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // fov
	}

	~PipelineImpl()
	{
		for (gpu::TextureHandle t : m_textures) m_renderer.destroy(t);
		for (gpu::BufferHandle b : m_buffers) m_renderer.destroy(b);

		m_renderer.frame();
		m_renderer.frame();
		m_renderer.frame();

		m_draw2d_shader->decRefCount();
		m_debug_shape_shader->decRefCount();
		m_place_grass_shader->decRefCount();
		m_instancing_shader->decRefCount();

		for (const Renderbuffer& rb : m_renderbuffers) {
			m_renderer.destroy(rb.handle);
		}

		for(ShaderRef& shader : m_shaders) {
			shader.res->decRefCount();
		}
		if (m_resource) m_resource->decRefCount();

		m_renderer.destroy(m_cube_ib);
		m_renderer.destroy(m_cube_vb);
		m_renderer.destroy(m_instanced_meshes_buffer);
		m_renderer.destroy(m_indirect_buffer);
		m_renderer.destroy(m_shadow_atlas.texture);
		m_renderer.destroy(m_cluster_buffers.clusters.buffer);
		m_renderer.destroy(m_cluster_buffers.lights.buffer);
		m_renderer.destroy(m_cluster_buffers.maps.buffer);
		m_renderer.destroy(m_cluster_buffers.env_probes.buffer);
		m_renderer.destroy(m_cluster_buffers.refl_probes.buffer);

		clearBuffers();
	}

	void callInitScene()
	{
		PROFILE_FUNCTION();
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "initScene");
		if (lua_type(m_lua_state, -1) == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != 0)
			{
				logError(lua_tostring(m_lua_state, -1));
				lua_pop(m_lua_state, 1);
			}
		}
		else
		{
			lua_pop(m_lua_state, 1);
		}
	}
	
	void cleanup()
	{
		if (m_lua_state)
		{
			luaL_unref(m_renderer.getEngine().getState(), LUA_REGISTRYINDEX, m_lua_thread_ref);
			luaL_unref(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
			m_lua_state = nullptr;
		}
	}

	void setDefine()
	{
		if (m_define == "") return;
		StaticString<256> tmp(m_define, " = true");

		bool errors = luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp.data), m_resource->getPath().c_str()) != 0;
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
		u32 name_hash = crc32(name);
		for(CustomCommandHandler& handler : m_custom_commands_handlers)
		{
			if(handler.hash == name_hash)
			{
				handler.callback.invoke();
				break;
			}
		}
	}

	void exposeCustomCommandToLua(const CustomCommandHandler& handler)
	{
		if (!m_lua_state) return;

		char tmp[1024];
		copyString(tmp, "function ");
		catString(tmp, handler.name);
		catString(tmp, "() executeCustomCommand(\"");
		catString(tmp, handler.name);
		catString(tmp, "\") end");

		bool errors = luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp), "exposeCustomCommandToLua") != 0;
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

		m_lua_state = lua_newthread(m_renderer.getEngine().getState());
		m_lua_thread_ref = luaL_ref(m_renderer.getEngine().getState(), LUA_REGISTRYINDEX);

		lua_newtable(m_lua_state);
		lua_pushvalue(m_lua_state, -1);
		m_lua_env = luaL_ref(m_lua_state, LUA_REGISTRYINDEX);
		lua_pushvalue(m_lua_state, -1);
		lua_setmetatable(m_lua_state, -2);
		lua_pushvalue(m_lua_state, LUA_GLOBALSINDEX);
		lua_setfield(m_lua_state, -2, "__index");

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_pushstring(m_lua_state, m_renderer.getEngine().getFileSystem().getBasePath());
		lua_setfield(m_lua_state, -2, "LUA_PATH");

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_pushlightuserdata(m_lua_state, this);
		lua_setfield(m_lua_state, -2, "this");

		registerLuaAPI(m_lua_state);
		for (auto& handler : m_custom_commands_handlers)
		{
			exposeCustomCommandToLua(handler);
		}

		setDefine();

		const char* content = m_resource->content.begin();
		const int content_size = m_resource->content.size();
		bool errors =
			luaL_loadbuffer(m_lua_state, content, content_size, m_resource->getPath().c_str()) != 0;
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
			return;
		}

		m_viewport.w = m_viewport.h = 800;
		if (m_scene) callInitScene();
	}

	void clearBuffers() {
		PROFILE_FUNCTION();
		for (Renderbuffer& rb : m_renderbuffers) {
			++rb.frame_counter;
			if (rb.frame_counter > 2 && !rb.persistent && rb.handle) {
				m_renderer.destroy(rb.handle);
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
		if (m_first_set_viewport) {
			m_prev_viewport = viewport;
			m_first_set_viewport = false;
		}
	}

	void prepareShadowCameras(GlobalState& global_state)
	{
		for (int slice = 0; slice < 4; ++slice) {
			const int shadowmap_width = 1024;

			const Universe& universe = m_scene->getUniverse();
			const EntityPtr light = m_scene->getActiveEnvironment();
			const Vec4 cascades = light.isValid() ? m_scene->getShadowmapCascades((EntityRef)light) : Vec4(3, 10, 60, 150);
			const Matrix light_mtx = light.isValid() ? universe.getRelativeMatrix((EntityRef)light, m_viewport.pos) : Matrix::IDENTITY;

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

			const Matrix projection_matrix = vp.getProjection();
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
		const Universe& universe = m_scene->getUniverse();
		const Viewport backup_viewport = m_viewport;

		const Vec4 uv = ShadowAtlas::getUV(atlas_idx);
		m_viewport.is_ortho = false;
		m_viewport.pos = universe.getPosition(light.entity);
		m_viewport.rot = universe.getRotation(light.entity);
		m_viewport.fov = light.fov;
		m_viewport.near = 0.1f;
		m_viewport.far = light.range;
		m_viewport.w = u32(ShadowAtlas::SIZE * uv.z + 0.5f);
		m_viewport.h = u32(ShadowAtlas::SIZE * uv.w + 0.5f);;

		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, "viewport_w", m_viewport.w);
		LuaWrapper::setField(m_lua_state, -1, "viewport_h", m_viewport.h);
		lua_getfield(m_lua_state, -1, "main_shadowmap");
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			logError(getPath(), ": can not bake shadows because main_shadowmap() is missing");
			return false;
		}
		LuaWrapper::pcall(m_lua_state, 0, 0);
		lua_pop(m_lua_state, 1);

		struct BlitJob : Renderer::RenderJob {
			void execute() override { gpu::copy(dst, src, x, y); }
			void setup() override {}

			gpu::TextureHandle src;
			gpu::TextureHandle dst;
			u32 x, y;
		};

		const gpu::TextureHandle src = getOutput();
		m_renderbuffers[m_output].frame_counter = 1;
		if (!src) {
			logError(getPath(), ": can not bake shadows because the pipeline has no output");
			return false;
		}

		BlitJob& job = m_renderer.createJob<BlitJob>();
		job.dst = m_shadow_atlas.texture;
		job.src = src;
		job.x = u32(ShadowAtlas::SIZE * uv.x + 0.5f);
		job.y = u32(ShadowAtlas::SIZE * uv.y + 0.5f);

		m_renderer.queue(job, m_profiler_link);
		m_viewport = backup_viewport;
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, "viewport_w", m_viewport.w);
		LuaWrapper::setField(m_lua_state, -1, "viewport_h", m_viewport.h);
		lua_pop(m_lua_state, 1);
		return true;
	}

	void render3DUI(EntityRef e, const Draw2D& drawdata, Vec2 canvas_size, bool orient_to_cam) override {
		if (!m_draw2d_shader->isReady()) return;
		if (drawdata.getIndices().empty()) return;

		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();

		IAllocator& allocator = m_renderer.getAllocator();
		Draw2DJob& cmd = m_renderer.createJob<Draw2DJob>(allocator);
		cmd.pipeline = this;
		cmd.atlas_texture = atlas_texture->handle;
		cmd.matrix = m_scene->getUniverse().getRelativeMatrix(e, m_viewport.pos);
		cmd.is_3d = true;
		Matrix normalize(
			Vec4(1 / canvas_size.x, 0, 0, 0),
			Vec4(0, -1 / canvas_size.x, 0, 0),
			Vec4(0, 0, 1, 0),
			Vec4(-0.5f, 0.5f * canvas_size.y / canvas_size.x, 0, 1)
		);
		if (orient_to_cam) {
			const Transform tr = m_scene->getUniverse().getTransform(e);
			cmd.matrix = m_viewport.rot.toMatrix();
			cmd.matrix.setTranslation(Vec3(tr.pos - m_viewport.pos));
			cmd.matrix.multiply3x3(tr.scale);
		}
		cmd.matrix = m_viewport.getProjection() * m_viewport.getViewRotation() * cmd.matrix * normalize;
		cmd.prepare(drawdata);
		m_renderer.queue(cmd, m_profiler_link);
	}

	static Matrix computeReprojection(const Viewport& current, const Viewport& prev) {
		Matrix translation = Matrix::IDENTITY;
		translation.setTranslation(Vec3(current.pos - prev.pos));
		if (gpu::isOriginBottomLeft()) {
			return prev.getProjection() * prev.getViewRotation() * translation * current.getViewRotation().inverted() * current.getProjection().inverted();
		}

		Matrix flip = Matrix::IDENTITY;
		flip.columns[1].y = -1;
		return flip * prev.getProjection() * prev.getViewRotation() * translation * current.getViewRotation().inverted() * current.getProjection().inverted() * flip;
	}

	bool render(bool only_2d) override
	{
		PROFILE_FUNCTION();

		if (!isReady() || m_viewport.w <= 0 || m_viewport.h <= 0) {
			if (m_scene) {
				m_scene->clearDebugLines();
				m_scene->clearDebugTriangles();
			}
			m_draw2d.clear(getAtlasSize());
			return false;
		}

		m_renderer.waitCanSetup();
		clearBuffers();

		const Matrix view = m_viewport.getViewRotation();
		const Matrix projection = m_viewport.getProjection();
		GlobalState global_state;
		global_state.camera_projection = projection;
		global_state.camera_inv_projection = projection.inverted();
		global_state.camera_view = view;
		global_state.camera_inv_view = view.fastInverted();
		global_state.camera_view_projection = projection * view;
		global_state.camera_inv_view_projection = global_state.camera_view_projection.inverted();
		global_state.time = m_timer.getTimeSinceStart();
		global_state.frame_time_delta = m_timer.getTimeSinceTick();
		global_state.camera_reprojection = computeReprojection(m_viewport, m_prev_viewport);
		m_timer.tick();
		global_state.framebuffer_size.x = m_viewport.w;
		global_state.framebuffer_size.y = m_viewport.h;
		global_state.cam_world_pos = Vec4(Vec3(m_viewport.pos), 1);
		m_prev_viewport = m_viewport;
		m_indirect_buffer_offset = 0;

		if(m_scene) {
			const EntityPtr global_light = m_scene->getActiveEnvironment();
			if(global_light.isValid()) {
				EntityRef gl = (EntityRef)global_light;
				const Environment& env = m_scene->getEnvironment(gl);
				global_state.light_direction = Vec4(normalize(m_scene->getUniverse().getRotation(gl).rotate(Vec3(0, 0, -1))), 456); 
				global_state.light_color = Vec4(env.light_color, 456);
				global_state.light_intensity = env.direct_intensity;
				global_state.light_indirect_intensity = env.indirect_intensity;
			}
		}

		if (!only_2d) {
			prepareShadowCameras(global_state);
		}

		struct StartPipelineJob : Renderer::RenderJob {
			void execute() override {
				PROFILE_FUNCTION();
				pipeline->m_renderer.beginProfileBlock(pipeline->m_define, 0, true);
				gpu::bindUniformBuffer(UniformBuffer::GLOBAL, global_state_buffer.buffer, global_state_buffer.offset, sizeof(GlobalState));
				gpu::bindUniformBuffer(UniformBuffer::PASS, gpu::INVALID_BUFFER, 0, 0);
				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, gpu::INVALID_BUFFER, 0, 0);
				gpu::bindUniformBuffer(UniformBuffer::SHADOW, gpu::INVALID_BUFFER, 0, 0);
				int tmp[12] = {};
				gpu::update(pipeline->m_instanced_meshes_buffer, &tmp, sizeof(tmp));
			}
			void setup() override {
				global_state_buffer = pipeline->m_renderer.allocUniform(sizeof(GlobalState));
				memcpy(global_state_buffer.ptr, &global_state, sizeof(global_state));
			}

			Renderer::TransientSlice global_state_buffer;
			PipelineImpl* pipeline;
			GlobalState global_state;
			PassState pass_state;
		};

		StartPipelineJob& start_job = m_renderer.createJob<StartPipelineJob>();
		start_job.pipeline = this;
		start_job.global_state = global_state;
		m_renderer.queue(start_job, 0);
		
		m_views.clear();
		
		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, "viewport_w", m_viewport.w);
		LuaWrapper::setField(m_lua_state, -1, "viewport_h", m_viewport.h);
		lua_getfield(m_lua_state, -1, "main");
		bool has_main = true;
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			if (m_scene) {
				m_scene->clearDebugLines();
				m_scene->clearDebugTriangles();
			}
			has_main = false;
		}
		if (has_main) {
			PROFILE_BLOCK("lua pipeline main");
			profiler::blockColor(0xff, 0x7f, 0x7f);
			LuaWrapper::pcall(m_lua_state, 0, 0);
			lua_pop(m_lua_state, 1);
		}

		struct EndPipelineJob : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				pipeline->m_renderer.endProfileBlock();
				while (instanced_meshes) {
					InstancedMeshes* i = instanced_meshes;
					i->~InstancedMeshes();
					instanced_meshes = instanced_meshes->next;
				}
			}

			PipelineImpl* pipeline;
			IAllocator* allocator;
			InstancedMeshes* instanced_meshes; 
		};

		EndPipelineJob& end_job = m_renderer.createJob<EndPipelineJob>();
		end_job.pipeline = this;
		end_job.allocator = &m_allocator;
		end_job.instanced_meshes = m_views.empty() ? nullptr : m_views[0]->instanced_meshes;
		m_renderer.queue(end_job, 0);
		m_renderer.waitForCommandSetup();

		m_views.clear();

		return true;
	}

	void renderDebugTriangles() {
		struct Cmd : Renderer::RenderJob
		{
			struct BaseVertex {
				Vec3 pos;
				u32 color;
			};

			void setup() override {
				PROFILE_FUNCTION();
				const Array<DebugTriangle>& tris = pipeline->m_scene->getDebugTriangles();

				program = pipeline->m_debug_shape_shader->getProgram(pipeline->m_base_vertex_decl, 0);
				vb = pipeline->m_renderer.allocTransient(sizeof(BaseVertex) * tris.size() * 3);
				ub = pipeline->m_renderer.allocUniform(sizeof(Matrix));
				memcpy(ub.ptr, &Matrix::IDENTITY.columns[0].x, sizeof(Matrix));
				BaseVertex* vertices = (BaseVertex*)vb.ptr;
				for (u32 i = 0, c = tris.size(); i < c; ++i) {
					vertices[3 * i + 0].color = tris[i].color;
					vertices[3 * i + 0].pos = Vec3(tris[i].p0 - viewport_pos);
					vertices[3 * i + 1].color = tris[i].color;
					vertices[3 * i + 1].pos = Vec3(tris[i].p1 - viewport_pos);
					vertices[3 * i + 2].color = tris[i].color;
					vertices[3 * i + 2].pos = Vec3(tris[i].p2 - viewport_pos);
				}
				pipeline->m_scene->clearDebugTriangles();
			}


			void execute() override {
				PROFILE_FUNCTION();
				gpu::pushDebugGroup("debug triangles");
				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, sizeof(Matrix));
				gpu::setState(gpu::StateFlags::DEPTH_TEST | gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::CULL_BACK);
				gpu::useProgram(program);
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(BaseVertex));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::drawArrays(gpu::PrimitiveType::TRIANGLES, 0, vb.size / sizeof(BaseVertex));
				gpu::popDebugGroup();
			}

			PipelineImpl* pipeline;
			DVec3 viewport_pos;
			gpu::ProgramHandle program;
			Renderer::TransientSlice vb;
			Renderer::TransientSlice ub;
		};

		const Array<DebugTriangle>& tris = m_scene->getDebugTriangles();
		if (tris.empty() || !m_debug_shape_shader->isReady()) return;

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.pipeline = this;
		cmd.viewport_pos = m_viewport.pos;
		m_renderer.queue(cmd, m_profiler_link);
	}

	void renderDebugLines()	{
		struct Cmd : Renderer::RenderJob
		{
			struct BaseVertex {
				Vec3 pos;
				u32 color;
			};

			void setup() override
			{
				PROFILE_FUNCTION();
				const Array<DebugLine>& lines = pipeline->m_scene->getDebugLines();

				program = pipeline->m_debug_shape_shader->getProgram(pipeline->m_base_vertex_decl, 0);
				vb = pipeline->m_renderer.allocTransient(sizeof(BaseVertex) * lines.size() * 2);
				ub = pipeline->m_renderer.allocUniform(sizeof(Matrix));
				memcpy(ub.ptr, &Matrix::IDENTITY.columns[0].x, sizeof(Matrix));
				BaseVertex* vertices = (BaseVertex*)vb.ptr;
				for (u32 i = 0, c = lines.size(); i < c; ++i) {
					vertices[2 * i + 0].color = lines[i].color;
					vertices[2 * i + 0].pos = Vec3(lines[i].from - viewport_pos);
					vertices[2 * i + 1].color = lines[i].color;
					vertices[2 * i + 1].pos = Vec3(lines[i].to - viewport_pos);
				}
				pipeline->m_scene->clearDebugLines();
			}


			void execute() override {
				PROFILE_FUNCTION();
				if (vb.size == 0) return;
				gpu::pushDebugGroup("debug lines");
				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, sizeof(Matrix));
				gpu::setState(gpu::StateFlags::DEPTH_TEST | gpu::StateFlags::DEPTH_WRITE);
				gpu::useProgram(program);
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(BaseVertex));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::drawArrays(gpu::PrimitiveType::LINES, 0, vb.size / sizeof(BaseVertex));
				gpu::popDebugGroup();
			}

			PipelineImpl* pipeline;
			DVec3 viewport_pos;
			gpu::ProgramHandle program;
			Renderer::TransientSlice vb;
			Renderer::TransientSlice ub;
		};


		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_shape_shader->isReady()) return;

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.pipeline = this;
		cmd.viewport_pos = m_viewport.pos;
		m_renderer.queue(cmd, m_profiler_link);
	}

	void renderDebugShapes()
	{
		renderDebugTriangles();
		renderDebugLines();
		//renderDebugPoints();
	}

	struct Draw2DJob : Renderer::RenderJob {
		Draw2DJob(IAllocator& allocator) : cmd_buffer(allocator) {}

		void prepare(const Draw2D& draw2d) {
			PROFILE_FUNCTION();

			num_indices = draw2d.getIndices().size();
			num_vertices = draw2d.getVertices().size();

			idx_buffer_mem = pipeline->m_renderer.allocTransient(draw2d.getIndices().byte_size());
			vtx_buffer_mem = pipeline->m_renderer.allocTransient(draw2d.getVertices().byte_size());
			memcpy(idx_buffer_mem.ptr, draw2d.getIndices().begin(), draw2d.getIndices().byte_size());
			memcpy(vtx_buffer_mem.ptr, draw2d.getVertices().begin(), draw2d.getVertices().byte_size());
			cmd_buffer.resize(draw2d.getCmds().size());
			memcpy(&cmd_buffer[0], draw2d.getCmds().begin(), sizeof(cmd_buffer[0]) * cmd_buffer.size());

			program = pipeline->m_draw2d_shader->getProgram(pipeline->m_2D_decl, 0);
			ub = pipeline->m_renderer.allocUniform(sizeof(matrix));
			memcpy(ub.ptr, &matrix, sizeof(matrix));
		}

		void setup() override {}

		void execute() override {
			PROFILE_FUNCTION();

			if (cmd_buffer.empty()) return;

			gpu::pushDebugGroup("draw2d");

			gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, sizeof(Matrix));
			u32 elem_offset = 0;
			gpu::StateFlags state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
			state = state | gpu::StateFlags::SCISSOR_TEST;
			if (is_3d) state = state | gpu::StateFlags::DEPTH_TEST; 
			gpu::setState(state);
			gpu::useProgram(program);
			gpu::bindIndexBuffer(idx_buffer_mem.buffer);
			gpu::bindVertexBuffer(0, vtx_buffer_mem.buffer, vtx_buffer_mem.offset, 20);
			gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

			for (Draw2D::Cmd& cmd : cmd_buffer) {
				if(cmd.clip_size.x < 0) {
					gpu::scissor(0, 0, pipeline->m_viewport.w, pipeline->m_viewport.h);
				}
				else {
					const u32 h = u32(clamp(cmd.clip_size.y, 0.f, 65535.f));
					if (gpu::isOriginBottomLeft()) {
						gpu::scissor(u32(maximum(cmd.clip_pos.x, 0.0f)),
							pipeline->m_viewport.h - u32(maximum(cmd.clip_pos.y, 0.0f)) - h,
							u32(minimum(cmd.clip_size.x, 65535.0f)),
							u32(minimum(cmd.clip_size.y, 65535.0f)));
					}
					else {
						gpu::scissor(u32(maximum(cmd.clip_pos.x, 0.0f)),
							u32(maximum(cmd.clip_pos.y, 0.0f)),
							u32(minimum(cmd.clip_size.x, 65535.0f)),
							u32(minimum(cmd.clip_size.y, 65535.0f)));
					}
				}
			
				gpu::TextureHandle texture_id = atlas_texture;
				if (cmd.texture) texture_id = *cmd.texture;
				if (!texture_id) texture_id = atlas_texture;

				gpu::bindTextures(&texture_id, 0, 1);
				gpu::drawElements(gpu::PrimitiveType::TRIANGLES, idx_buffer_mem.offset + elem_offset * sizeof(u32), cmd.indices_count, gpu::DataType::U32);

				elem_offset += cmd.indices_count;
			}

			gpu::popDebugGroup();
		}

		gpu::TextureHandle atlas_texture;
		Renderer::TransientSlice idx_buffer_mem;
		Renderer::TransientSlice vtx_buffer_mem;
		Renderer::TransientSlice ub;
		int num_indices;
		int num_vertices;
		Array<Draw2D::Cmd> cmd_buffer;
		PipelineImpl* pipeline;
		gpu::ProgramHandle program;
		Matrix matrix;
		bool is_3d = false;
	};

	void render2D() {
		if (!m_draw2d_shader->isReady()) {
			m_draw2d.clear(getAtlasSize());
			return;
		}

		if (m_draw2d.getIndices().empty()) {
			m_draw2d.clear(getAtlasSize());
			return;
		}

		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();

		IAllocator& allocator = m_renderer.getAllocator();
		Draw2DJob& cmd = m_renderer.createJob<Draw2DJob>(allocator);
		cmd.pipeline = this;
		cmd.atlas_texture = atlas_texture->handle;
		cmd.matrix.setOrtho(0, (float)m_viewport.w, (float)m_viewport.h, 0, 0, 1, false);
		cmd.prepare(m_draw2d);
		m_draw2d.clear(getAtlasSize());
		m_renderer.queue(cmd, m_profiler_link);
	}

	void setUniverse(Universe* universe) override {
		RenderScene* scene = universe ? (RenderScene*)universe->getScene("renderer") : nullptr;
		if (m_scene == scene) return;
		m_scene = scene;
		if (m_lua_state && m_scene) callInitScene();
	}
	
	Renderer& getRenderer() const override { return m_renderer; }

	RenderScene* getScene() const override { return m_scene; }

	CustomCommandHandler& addCustomCommandHandler(const char* name) override 
	{
		auto& handler = m_custom_commands_handlers.emplace();
		copyString(handler.name, name);
		handler.hash = crc32(name);
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
			{"r8", gpu::TextureFormat::R8},
			{"r32f", gpu::TextureFormat::R32F},
			{"rg32f", gpu::TextureFormat::RG32F},
			{"r11g11b10f", gpu::TextureFormat::R11G11B10F },
		};

		for (auto& i : FORMATS)
		{
			if (equalStrings(i.name, name)) return i.value;
		}
		logError("Uknown texture format ", name);
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
			m_renderbuffers[rb].persistent = false;
		}
	}

	void keepRenderbufferAlive(lua_State* L) {
		const i32 rb = toRenderbufferIdx(L, 1);

		if (rb >= 0 && rb < m_renderbuffers.size()) {
			--m_renderbuffers[rb].frame_counter;
		}
	}

	PipelineTexture createRenderbuffer(lua_State* L) {
		PROFILE_FUNCTION();

		LuaWrapper::checkTableArg(L, 1);
		u32 rb_w, rb_h;
		char format_str[64];
		char debug_name[64] = "";
		bool persistent = false;
		bool point_filter = false;
		bool compute_write = false;
		if (!LuaWrapper::checkField(L, 1, "width", &rb_w)) luaL_argerror(L, 1, "missing width");
		if (!LuaWrapper::checkField(L, 1, "height", &rb_h)) luaL_argerror(L, 1, "missing height");
		if (!LuaWrapper::checkStringField(L, 1, "format", Span(format_str))) luaL_argerror(L, 1, "missing format");
		LuaWrapper::getOptionalStringField(L, 1, "debug_name", Span(debug_name));
		LuaWrapper::getOptionalField(L, 1, "persistent", &persistent);
		LuaWrapper::getOptionalField(L, 1, "point_filter", &point_filter);
		LuaWrapper::getOptionalField(L, 1, "compute_write", &compute_write);
		gpu::TextureFlags flags = gpu::TextureFlags::RENDER_TARGET 
			| gpu::TextureFlags::NO_MIPS
			| gpu::TextureFlags::CLAMP_U
			| gpu::TextureFlags::CLAMP_V
			| (compute_write ? gpu::TextureFlags::COMPUTE_WRITE : gpu::TextureFlags::NONE);
		if (point_filter) flags = flags | gpu::TextureFlags::POINT_FILTER;

		const gpu::TextureFormat format = getFormat(format_str);

		for (int i = 0, n = m_renderbuffers.size(); i < n; ++i)
		{
			Renderbuffer& rb = m_renderbuffers[i];
			if (!rb.handle) {
				rb.handle = m_renderer.createTexture(rb_w, rb_h, 1, format, flags, Renderer::MemRef(), debug_name);
				rb.width = rb_w;
				rb.height = rb_h;
				rb.format = format;
				rb.flags = flags;
			}
			else {
				if (rb.frame_counter < 2) continue;
				if (rb.persistent) continue;
				if (rb.width != rb_w) continue;
				if (rb.height != rb_h) continue;
				if (rb.format != format) continue;
				if (rb.flags != flags) continue;
			}

			rb.frame_counter = 0;
			rb.persistent = persistent;
			PipelineTexture res;
			res.type = PipelineTexture::RENDERBUFFER;
			res.renderbuffer = i;
			return res;
		}

		Renderbuffer& rb = [&]() -> Renderbuffer& {
			for (int i = 0, n = m_renderbuffers.size(); i < n; ++i) {
				if (!m_renderbuffers[i].handle) {
					return m_renderbuffers[i];
				}
			}	
			return m_renderbuffers.emplace();
		}();
		rb.frame_counter = 0;
		rb.width = rb_w;
		rb.height = rb_h;
		rb.format = format;
		rb.persistent = persistent;
		rb.flags = flags;
		rb.handle = m_renderer.createTexture(rb_w, rb_h, 1, format, flags, Renderer::MemRef(), debug_name);

		PipelineTexture res;
		res.type = PipelineTexture::RENDERBUFFER;
		res.renderbuffer = m_renderbuffers.size() - 1;
		return res;
	}

	void renderTerrains(lua_State* L, CameraParams cp, RenderState state)
	{
		PROFILE_FUNCTION();
		LinearAllocator& current_frame_allocator = m_renderer.getCurrentFrameAllocator();
		IAllocator& allocator = m_renderer.getAllocator();
		RenderTerrainsJob& cmd = m_renderer.createJob<RenderTerrainsJob>(current_frame_allocator, allocator);

		const char* define = "";
		LuaWrapper::getOptionalField<const char*>(L, 2, "define", &define);

		cmd.m_define_mask = define[0] ? 1 << m_renderer.getShaderDefineIdx(define) : 0;
		cmd.m_render_state = state.value;
		cmd.m_pipeline = this;
		cmd.m_camera_params = cp;

		m_renderer.queue(cmd, m_profiler_link);
	}
	
	void renderGrass(lua_State* L, CameraParams cp, LuaWrapper::Optional<RenderState> state)
	{
		PROFILE_FUNCTION();
		if (!m_place_grass_shader->isReady()) return;

		IAllocator& allocator = m_renderer.getAllocator();
		RenderGrassJob& cmd = m_renderer.createJob<RenderGrassJob>(allocator);

		cmd.m_define_mask = 0;
		if (lua_istable(L, 2)) {
			const char* define = "";
			LuaWrapper::getOptionalField<const char*>(L, 2, "define", &define);
			cmd.m_define_mask = define[0] ? 1 << m_renderer.getShaderDefineIdx(define) : 0;

			if (LuaWrapper::getField(L, 2, "defines") != LUA_TNIL) {
				LuaWrapper::forEachArrayItem<const char*>(L, -1, "array of strings expeceted", [&](const char* define){
					cmd.m_define_mask |= 1 << m_renderer.getShaderDefineIdx(define);
				});
			}
			lua_pop(L, 1);
		}
		cmd.m_render_state = state.get({gpu::StateFlags::NONE}).value;
		cmd.m_pipeline = this;
		cmd.m_camera_params = cp;
		cmd.m_compute_shader = m_place_grass_shader->getProgram(0);

		m_renderer.queue(cmd, m_profiler_link);
	}

	void renderParticles(CameraParams cp)
	{
		PROFILE_FUNCTION();
		struct Cmd : Renderer::RenderJob
		{
			Cmd(LinearAllocator& allocator) : m_drawcalls(allocator) {}

			void setup() override
			{
				PROFILE_FUNCTION();
				const auto& emitters = m_pipeline->m_scene->getParticleEmitters();
				if (emitters.size() == 0) return;
				
				Universe& universe = m_pipeline->m_scene->getUniverse();

				m_drawcalls.reserve(emitters.size());

				gpu::VertexDecl decl;
				decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// pos
				decl.addAttribute(1, 12, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// scale
				decl.addAttribute(2, 16, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// color
				decl.addAttribute(3, 32, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);  // rot
				decl.addAttribute(4, 36, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);  // frame

				for (const ParticleEmitter& emitter : emitters) {
					if (!emitter.getResource() || !emitter.getResource()->isReady()) continue;
					
					const int size = emitter.getParticlesDataSizeBytes();
					if (size == 0) continue;

					const Transform tr = universe.getTransform((EntityRef)emitter.m_entity);
					const Vec3 lpos = Vec3(tr.pos - m_camera_params.pos);

					const Material* material = emitter.getResource()->getMaterial();
					if (!material) continue;

					Drawcall& dc = m_drawcalls.emplace();
					dc.program = material->getShader()->getProgram(decl, 0);
					dc.material = material->getRenderData();
					dc.size = size;
					dc.particles_count = emitter.getParticlesCount();
					dc.slice = m_pipeline->m_renderer.allocTransient(emitter.getParticlesDataSizeBytes());
					emitter.fillInstanceData((float*)dc.slice.ptr);
					dc.ub = m_pipeline->m_renderer.allocUniform(sizeof(Matrix));
					Matrix mtx = tr.rot.toMatrix();
					mtx.setTranslation(lpos);
					memcpy(dc.ub.ptr, &mtx, sizeof(mtx));
				}
			}

			void execute() override
			{
				PROFILE_FUNCTION();
				
				gpu::pushDebugGroup("particles");
				
				const gpu::StateFlags blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
				gpu::setState(blend_state | gpu::StateFlags::DEPTH_TEST);
				const gpu::BufferHandle material_ub = m_pipeline->m_renderer.getMaterialUniformBuffer();
				u32 material_ub_idx = 0xffFFffFF;
				for (const Drawcall& dc : m_drawcalls) {
					if (material_ub_idx != dc.material->material_constants) {
						gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, dc.material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
						material_ub_idx = dc.material->material_constants;
					}

					gpu::bindTextures(dc.material->textures, 0, dc.material->textures_count);
					gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, dc.ub.buffer, dc.ub.offset, dc.ub.size);
					gpu::useProgram(dc.program);
					gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
					gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
					gpu::bindVertexBuffer(1, dc.slice.buffer, dc.slice.offset, 40);
					gpu::drawArraysInstanced(gpu::PrimitiveType::TRIANGLE_STRIP, 4, dc.particles_count);
				}
				gpu::popDebugGroup();
			}

			struct Drawcall {
				gpu::ProgramHandle program;
				Material::RenderData* material;
				int size;
				int particles_count;
				Renderer::TransientSlice slice; 
				Renderer::TransientSlice ub; 
			};

			Array<Drawcall> m_drawcalls; 
			PipelineImpl* m_pipeline;
			CameraParams m_camera_params;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>(m_renderer.getCurrentFrameAllocator());
		cmd.m_pipeline = this;
		cmd.m_camera_params = cp;

		m_renderer.queue(cmd, m_profiler_link);
	}

	static CameraParams checkCameraParams(lua_State* L, int idx)
	{
		CameraParams cp;

		lua_getfield(L, idx, "view");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			luaL_error(L, "View matrix is not a table");
		}

		for (int i = 0; i < 16; ++i) {
			lua_rawgeti(L, -1, i + 1);
			if (!LuaWrapper::isType<float>(L, -1)) {
				lua_pop(L, 2);
				luaL_error(L, "View matrix must contain exactly 16 floats");
			}
			cp.view[i] = LuaWrapper::toType<float>(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "projection");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			luaL_error(L, "Projection matrix is not a table");
		}

		for (int i = 0; i < 16; ++i) {
			lua_rawgeti(L, -1, i + 1);
			if (!LuaWrapper::isType<float>(L, -1)) {
				lua_pop(L, 2);
				luaL_error(L, "Projection matrix must contain exactly 16 floats");
			}
			cp.projection[i] = LuaWrapper::toType<float>(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		lua_getfield(L, idx, "frustum");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			luaL_error(L, "Frustum is not a table");
		}
		if(!LuaWrapper::checkField(L, -1, "origin", &cp.frustum.origin)) {
				lua_pop(L, 1);
				luaL_error(L, "Frustum without origin");
		}
		auto load_floats = [L](float* data, int count, int offset) {
			for (int i = 0; i < count; ++i) {
				lua_rawgeti(L, -1, offset + i + 1);
				if(!LuaWrapper::isType<float>(L, -1)) {
					lua_pop(L, 2);
					luaL_error(L, "Invalid frustum");
				}
				data[i] = LuaWrapper::toType<float>(L, -1);
				lua_pop(L, 1);
			}
		};
		load_floats(cp.frustum.xs, (int)Frustum::Planes::COUNT, 0);
		load_floats(cp.frustum.ys, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT);
		load_floats(cp.frustum.zs, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT * 2);
		load_floats(cp.frustum.ds, (int)Frustum::Planes::COUNT, (int)Frustum::Planes::COUNT * 3);
		load_floats(&cp.frustum.points[0].x, 24, (int)Frustum::Planes::COUNT * 4);
		
		lua_pop(L, 1);
		cp.frustum.setPlanesFromPoints();
		
		if(!LuaWrapper::checkField(L, idx, "lod_multiplier", &cp.lod_multiplier)) {
			luaL_error(L, "Missing lod_multiplier in camera params");
		}

		if (!LuaWrapper::checkField(L, idx, "is_shadow", &cp.is_shadow)) {
			luaL_error(L, "Missing is_shadow in camera params");
		}

		if(!LuaWrapper::checkField(L, idx, "position", &cp.pos)) {
			luaL_error(L, "Missing position in camera params");
		}

		return cp;
	}
	
	void bindShaderBuffer(LuaBufferHandle buffer_handle, u32 binding_point, bool writable) {
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::bindShaderBuffer(buffer, binding_point, writable ? gpu::BindShaderBufferFlags::OUTPUT : gpu::BindShaderBufferFlags::NONE);
			}
			u32 binding_point;
			bool writable;
			gpu::BufferHandle buffer;
		};
		
		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.binding_point = binding_point;
		cmd.buffer = buffer_handle;
		cmd.writable = writable;
		m_renderer.queue(cmd, m_profiler_link);
	}
	
	void bindUniformBuffer(LuaBufferHandle buffer_handle, u32 binding_point, u32 size) {
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::bindUniformBuffer(binding_point, buffer, 0, size);
			}
			u32 binding_point;
			u32 size;
			gpu::BufferHandle buffer;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.binding_point = binding_point;
		cmd.buffer = (gpu::BufferHandle)(uintptr)buffer_handle;
		cmd.size = size;
		m_renderer.queue(cmd, m_profiler_link);
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

	static PipelineImpl* getClosureThis(lua_State* L) {
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		return LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);	
	}

	static int drawcallUniforms(lua_State* L) {
		PipelineImpl* pipeline = getClosureThis(L);
		const int len = lua_gettop(L);

		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, drawcall_ub.buffer, drawcall_ub.offset, drawcall_ub.size);
			}
			Renderer::TransientSlice drawcall_ub;
		};

		Cmd& cmd = pipeline->m_renderer.createJob<Cmd>();
		cmd.drawcall_ub = pipeline->m_renderer.allocUniform(sizeof(float) * len);

		float* values = (float*)cmd.drawcall_ub.ptr;
		for(int i = 0; i < len; ++i) {
			values[i] = LuaWrapper::checkArg<float>(L, i + 1);
		}

		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);
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

		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::useProgram(program);
				gpu::dispatch(num_groups_x, num_groups_y, num_groups_z);
			}
			gpu::ProgramHandle program;
			u32 num_groups_x;
			u32 num_groups_y;
			u32 num_groups_z;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.num_groups_x = num_groups_x;
		cmd.num_groups_y = num_groups_y;
		cmd.num_groups_z = num_groups_z;
		cmd.program = program;
		m_renderer.queue(cmd, m_profiler_link);
	}

	gpu::TextureHandle toHandle(PipelineTexture tex) const {
		switch (tex.type) {
		case PipelineTexture::RENDERBUFFER: return tex.renderbuffer < (u32)m_renderbuffers.size() ? m_renderbuffers[tex.renderbuffer].handle : gpu::INVALID_TEXTURE;
			case PipelineTexture::RAW: return tex.raw;
			case PipelineTexture::RESOURCE: {
				if (tex.resource == -2) return m_shadow_atlas.texture;
				if (tex.resource == -3) return m_scene->getReflectionProbesTexture();

				Resource* res = m_renderer.getEngine().getLuaResource(tex.resource);
				if (res->getType() != Texture::TYPE) return gpu::INVALID_TEXTURE;
				return ((Texture*)res)->handle;
			}
			default:
				ASSERT(false);
				return gpu::INVALID_TEXTURE;
		}
	}

	void bindImageTexture(PipelineTexture texture, u32 unit) {
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::bindImageTexture(texture, unit);
			}
			gpu::TextureHandle texture;
			u32 unit;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.texture = toHandle(texture);
		cmd.unit = unit;
		m_renderer.queue(cmd, m_profiler_link);
	}

	void bindTextures(lua_State* L, LuaWrapper::Array<PipelineTexture, 16> textures, LuaWrapper::Optional<u32> offset) 	{
		struct Cmd : Renderer::RenderJob {
			Cmd(IAllocator& allocator) 
				: m_allocator(allocator)
				, m_textures_handles(m_allocator) {}
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				gpu::bindTextures(m_textures_handles.begin(), m_offset, m_textures_handles.size());
			}

			StackAllocator<sizeof(gpu::TextureHandle) * 16, alignof(gpu::TextureHandle)> m_allocator; 
			Array<gpu::TextureHandle> m_textures_handles;
			i32 m_offset = 0;
		};
		
		Cmd& cmd = m_renderer.createJob<Cmd>(m_allocator);
		cmd.m_offset = offset.get(0);
		
		for (u32 i = 0; i < textures.size; ++i) {
			cmd.m_textures_handles.push(toHandle(textures[i]));
		}

		m_renderer.queue(cmd, m_profiler_link);
	};
	
	void pass(CameraParams cp)
	{
		PROFILE_FUNCTION();
		struct PushPassStateCmd : Renderer::RenderJob {
			void execute() override {
				PROFILE_FUNCTION();
				gpu::bindUniformBuffer(UniformBuffer::PASS, buf.buffer, buf.offset, sizeof(PassState));
			}

			void setup() override {
				PROFILE_FUNCTION();
				PassState* pass_state = (PassState*)buf.ptr;
				pass_state->view = cp.view;
				pass_state->projection = cp.projection;
				pass_state->inv_projection = cp.projection.inverted();
				pass_state->inv_view = cp.view.fastInverted();
				pass_state->view_projection = cp.projection * cp.view;
				pass_state->inv_view_projection = pass_state->view_projection.inverted();
				pass_state->view_dir = Vec4(cp.view.inverted().transformVector(Vec3(0, 0, -1)), 0);
				pass_state->camera_up = Vec4(cp.view.inverted().transformVector(Vec3(0, 1, 0)), 0);
				toPlanes(cp, Span(pass_state->camera_planes));
				if (cp.is_shadow) {
					pass_state->shadow_to_camera = Vec4(Vec3(viewport_pos - cp.pos), 1);
				}
			}

			Renderer::TransientSlice buf;
			CameraParams cp;
			DVec3 viewport_pos;
		};

		PushPassStateCmd& cmd = m_renderer.createJob<PushPassStateCmd>();
		cmd.cp = cp;
		cmd.viewport_pos = m_viewport.pos;
		cmd.buf = m_renderer.allocUniform(sizeof(PassState));
		
		m_renderer.queue(cmd, m_profiler_link);
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

	void drawArray(lua_State* L, i32 indices_offset, i32 indices_count, i32 shader_id)
	{
		struct Cmd : Renderer::RenderJob {
			Cmd(IAllocator& allocator)
				: m_allocator(allocator)
				, m_textures_handles(m_allocator)
			{}
			void setup() override { m_program = m_shader->getProgram(gpu::VertexDecl(), m_define_mask); }
			void execute() override 
			{
				PROFILE_FUNCTION();

				gpu::setState(m_render_state);

				gpu::bindTextures(m_textures_handles.begin(), 0, m_textures_handles.size());

				gpu::useProgram(m_program);
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::drawArrays(gpu::PrimitiveType::TRIANGLE_STRIP, m_indices_offset, m_indices_count);
			}

			PipelineImpl* m_pipeline;
			StackAllocator<sizeof(gpu::TextureHandle) * 16, alignof(gpu::TextureHandle)> m_allocator; 
			Array<gpu::TextureHandle> m_textures_handles;
			Shader* m_shader;
			int m_indices_count;
			int m_indices_offset;
			u32 m_define_mask = 0;
			gpu::StateFlags m_render_state;
			gpu::ProgramHandle m_program;

		};

		LuaWrapper::DebugGuard guard(L);

		if (lua_gettop(L) > 3) LuaWrapper::checkTableArg(L, 4);
		
		const gpu::StateFlags rs = [&](){
			if(lua_gettop(L) > 4) {
				LuaWrapper::checkTableArg(L, 5);
				return getState(L, 5);
			}
			return gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_TEST;
		}();

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

		Cmd& cmd = m_renderer.createJob<Cmd>(m_allocator);
		if(lua_gettop(L) > 3) {
			const u32 len = (u32)lua_objlen(L, 4);

			for(u32 i = 0; i < len; ++i) {
				lua_rawgeti(L, 4, i + 1);
				if (!LuaWrapper::isType<PipelineTexture>(L, -1)) luaL_argerror(L, 4, "expected textures");
				const PipelineTexture tex = LuaWrapper::toType<PipelineTexture>(L, -1);
				cmd.m_textures_handles.push(toHandle(tex));
				lua_pop(L, 1);
			}
		
			if (lua_isstring(L, 6)) {
				const char* define = lua_tostring(L, 6);
				cmd.m_define_mask = 1 << m_renderer.getShaderDefineIdx(define);
			}
			else if (lua_istable(L, 6)) {
				lua_pushnil(L);
				while (lua_next(L, 6) != 0) {
					if(lua_type(L, -1) != LUA_TSTRING) {
						m_renderer.destroyJob(cmd);
						luaL_error(L, "%s", "Incorrect define arguments of drawArrays");
					}
					const char* define = lua_tostring(L, -1);
					cmd.m_define_mask |= 1 << m_renderer.getShaderDefineIdx(define);
					lua_pop(L, 1);
				}
			}

		}
	
		cmd.m_pipeline = this;
		cmd.m_render_state = rs;
		cmd.m_shader = shader;
		cmd.m_indices_count = indices_count;
		cmd.m_indices_offset = indices_offset;
		m_renderer.queue(cmd, m_profiler_link);
	}

	CameraParams getCameraParams()
	{
		CameraParams cp;
		cp.pos = m_viewport.pos;
		cp.frustum = m_viewport.getFrustum();
		cp.lod_multiplier = m_scene->getCameraLODMultiplier(m_viewport.fov, m_viewport.is_ortho);
		cp.is_shadow = false;
		cp.view = m_viewport.getView(cp.pos);
		cp.projection = m_viewport.getProjection();
		return cp;
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


	static Vec3 shadowmapTexelAlign(const Vec3& shadow_cam_pos,
		float shadowmap_width,
		float frustum_radius,
		const Matrix& light_mtx)
	{
		Matrix inv = light_mtx.fastInverted();
		Vec3 out = inv.transformPoint(shadow_cam_pos);
		float align = 2 * frustum_radius / (shadowmap_width * 0.5f - 2);
		out.x -= fmodf(out.x, align);
		out.y -= fmodf(out.y, align);
		out = light_mtx.transformPoint(out);
		return out;
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

	void renderLocalLights(const char* define, int shader_idx, CmdPage* cmds)
	{
		PROFILE_FUNCTION();
		struct RenderJob : Renderer::RenderJob
		{
			void setup() override {}

			void execute() override
			{
				// inline in debug
				#define READ(T, N) \
					T N = *(T*)cmd; \
					cmd += sizeof(T); \
					do {} while(false)

				PROFILE_FUNCTION();
				if(m_cmds->header.size == 0 && m_cmds->header.next == nullptr) {
					m_pipeline->m_renderer.getEngine().getPageAllocator().deallocate(m_cmds, true);
					return;
				}
				
				const gpu::StateFlags blend_state = gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
				CmdPage* page = m_cmds;
				while (page) {
					const u8* cmd = page->data;
					const u8* cmd_end = page->data + page->header.size;
					while (cmd != cmd_end) {
						READ(const RenderableTypes, type);
						ASSERT(type == RenderableTypes::LOCAL_LIGHT);

						READ(u32, total_count);
						READ(u32, nonintersecting_count);
						READ(const gpu::BufferHandle, buffer);
						READ(const u32, offset);

						gpu::useProgram(m_program);

						if(total_count - nonintersecting_count) {
							gpu::setState(blend_state | gpu::StateFlags::CULL_FRONT);
							const u32 offs = offset + sizeof(float) * 16 * nonintersecting_count;
							gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
							gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);
							gpu::bindVertexBuffer(1, buffer, offs, 64);
							gpu::drawTrianglesInstanced(36, total_count - nonintersecting_count, gpu::DataType::U16);
						}

						if (nonintersecting_count) {
							gpu::setState(blend_state | gpu::StateFlags::DEPTH_TEST | gpu::StateFlags::CULL_BACK);
							gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
							gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);
							gpu::bindVertexBuffer(1, buffer, offset, 64);
							gpu::drawTrianglesInstanced(36, nonintersecting_count, gpu::DataType::U16);
						}
					}
					CmdPage* next = page->header.next;
					m_pipeline->m_renderer.getEngine().getPageAllocator().deallocate(page, true);
					page = next;
				}
				#undef READ
			}

			gpu::ProgramHandle m_program;
			PipelineImpl* m_pipeline;
			CmdPage* m_cmds;
		};

		Shader* shader = [&]() -> Shader* {
			for (const ShaderRef& s : m_shaders) {
				if(s.id == shader_idx) {
					return ((Shader*)s.res);
				}
			}
			return nullptr;
		}();

		if (!shader || !shader->isReady()) return;

		RenderJob& job = m_renderer.createJob<RenderJob>();
		const u32 define_mask = define[0] ? 1 << m_renderer.getShaderDefineIdx(define) : 0;
		job.m_pipeline = this;
		job.m_cmds = cmds;
		job.m_program = shader->getProgram(m_point_light_decl, define_mask);
		m_renderer.queue(job, m_profiler_link);
	}

	static gpu::StateFlags getState(lua_State* L, int idx)
	{
		gpu::StencilFuncs stencil_func = gpu::StencilFuncs::DISABLE;
		u8 stencil_write_mask = 0xff;
		u8 stencil_ref = 0;
		u8 stencil_mask = 0;
		gpu::StencilOps stencil_sfail = gpu::StencilOps::KEEP;
		gpu::StencilOps stencil_zfail = gpu::StencilOps::KEEP;
		gpu::StencilOps stencil_zpass = gpu::StencilOps::KEEP;

		char tmp[64];
		gpu::StateFlags rs = gpu::StateFlags::DEPTH_TEST | gpu::StateFlags::DEPTH_WRITE;
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

		LuaWrapper::getOptionalFlagField(L, idx, "depth_test", &rs, gpu::StateFlags::DEPTH_TEST, true);
		LuaWrapper::getOptionalFlagField(L, idx, "wireframe", &rs, gpu::StateFlags::WIREFRAME, false);
		LuaWrapper::getOptionalFlagField(L, idx, "depth_write", &rs, gpu::StateFlags::DEPTH_WRITE, true);
		LuaWrapper::getOptionalField(L, idx, "stencil_func", reinterpret_cast<u8*>(&stencil_func));
		LuaWrapper::getOptionalField(L, idx, "stencil_write_mask", &stencil_write_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_ref", &stencil_ref);
		LuaWrapper::getOptionalField(L, idx, "stencil_mask", &stencil_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_sfail", reinterpret_cast<u8*>(&stencil_sfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zfail", reinterpret_cast<u8*>(&stencil_zfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zpass", reinterpret_cast<u8*>(&stencil_zpass));

		rs = rs | gpu::getStencilStateBits(stencil_write_mask, stencil_func, stencil_ref, stencil_mask, stencil_sfail, stencil_zfail, stencil_zpass);

		return rs;
	}

	// TODO optimize
	struct FillClustersJob : Renderer::RenderJob {
		FillClustersJob(LinearAllocator& current_frame_allocator, IAllocator& allocator)
			: m_clusters(current_frame_allocator)
			, m_map(current_frame_allocator)
			, m_point_lights(allocator)
			, m_env_probes(current_frame_allocator)
			, m_refl_probes(current_frame_allocator)
		{}

		void setup() override {
			PROFILE_FUNCTION();
			const IVec3 size = {
				(m_pipeline->m_viewport.w + 63) / 64,
				(m_pipeline->m_viewport.h + 63) / 64,
				16 };
			Array<Cluster>& clusters = m_clusters;
			clusters.resize(size.x * size.y * size.z);
			for (Cluster& cluster : clusters) {
				cluster.point_lights_count = 0;
				cluster.env_probes_count = 0;
				cluster.refl_probes_count = 0;
			}

			m_shadow_matrices_ub = m_pipeline->m_renderer.allocUniform(sizeof(m_shadow_atlas_matrices));
			memcpy(m_shadow_matrices_ub.ptr, &m_shadow_atlas_matrices, sizeof(m_shadow_atlas_matrices));

			if (m_is_clear) return;

			Array<ClusterPointLight>& point_lights = m_point_lights;
			Array<ClusterEnvProbe>& env_probes = m_env_probes;
			Array<ClusterReflProbe>& refl_probes = m_refl_probes;
			Array<i32>& map = m_map;
			RenderScene* scene = m_pipeline->m_scene;
			const DVec3 cam_pos = m_camera_params.pos;
			Universe& universe = scene->getUniverse();
			const ShiftedFrustum& frustum = m_camera_params.frustum;
			Vec4 xplanes[65];
			Vec4 yplanes[65];
			Vec4 zplanes[17];

			const Vec3 cam_dir = normalize(cross(frustum.points[2] - frustum.points[0], frustum.points[1] - frustum.points[0]));
			
			Vec3 near = (frustum.points[0] + frustum.points[2]) * 0.5f;
			Vec3 far = (frustum.points[4] + frustum.points[6]) * 0.5f;
			
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

			const Span<const ReflectionProbe> scene_refl_probes = scene->getReflectionProbes();
			const Span<EntityRef> refl_probe_entities = scene->getReflectionProbesEntities();
			refl_probes.reserve(scene_refl_probes.length());
			for (u32 i = 0, c = scene_refl_probes.length(); i < c; ++i) {
				const ReflectionProbe& refl_probe = scene_refl_probes[i];
				if (!refl_probe.flags.isSet(ReflectionProbe::ENABLED)) continue;
				const EntityRef e = refl_probe_entities[i];
				ClusterReflProbe& probe =  refl_probes.emplace();
				probe.pos = Vec3(universe.getPosition(e) - cam_pos);
				probe.rot = universe.getRotation(e).conjugated();
				probe.half_extents = refl_probe.half_extents;
				probe.layer = refl_probe.texture_id;
			}

			qsort(refl_probes.begin(), refl_probes.size(), sizeof(ClusterReflProbe), [](const void* a, const void* b){
				const ClusterReflProbe* m = (const ClusterReflProbe*)a;
				const ClusterReflProbe* n = (const ClusterReflProbe*)b;
				const float m3 = m->half_extents.x * m->half_extents.y * m->half_extents.z;
				const float n3 = n->half_extents.x * n->half_extents.y * n->half_extents.z;
				if (m3 < n3) return -1;
				return m3 > n3 ? 1 : 0;
			});


			const Span<const EnvironmentProbe> scene_env_probes = scene->getEnvironmentProbes();
			const Span<EntityRef> env_probe_entities = scene->getEnvironmentProbesEntities();
			env_probes.reserve(scene_env_probes.length());
			for (u32 i = 0, c = scene_env_probes.length(); i < c; ++i) {
					const EnvironmentProbe& env_probe = scene_env_probes[i];
					if (!env_probe.flags.isSet(EnvironmentProbe::ENABLED)) continue;
					const EntityRef e = env_probe_entities[i];
					ClusterEnvProbe& probe =  env_probes.emplace();
					probe.pos = Vec3(universe.getPosition(e) - cam_pos);
					probe.rot = universe.getRotation(e).conjugated();
					probe.inner_range = env_probe.inner_range;
					probe.outer_range = env_probe.outer_range;
					for (u32 i = 0; i < 9; ++i) {
						probe.sh_coefs[i] = Vec4(env_probe.sh_coefs[i], 0);
					}
			}

			qsort(env_probes.begin(), env_probes.size(), sizeof(ClusterEnvProbe), [](const void* a, const void* b){
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
					const float d = planeDist(planes[i + 1], p);
					if (d > r) continue;

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
				for (i32 i = 0, c = point_lights.size(); i < c; ++i) {
					ClusterPointLight& light = point_lights[i];
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
				for (i32 i = 0, c = env_probes.size(); i < c; ++i) {
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
				for (i32 i = 0, c = refl_probes.size(); i < c; ++i) {
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
				++cluster.point_lights_count;
			});

			for_each_env_probe_pair([](Cluster& cluster, i32){
				++cluster.env_probes_count;
			});

			for_each_refl_probe_pair([](Cluster& cluster, i32){
				++cluster.refl_probes_count;
			});

			u32 offset = 0;
			for (Cluster& cluster : clusters) {
				cluster.offset = offset;
				offset += cluster.point_lights_count + cluster.env_probes_count + cluster.refl_probes_count;
			}
			
			map.resize(offset);
			
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

			for (Cluster& cluster : clusters) {
				cluster.offset -= cluster.point_lights_count + cluster.env_probes_count + cluster.refl_probes_count;
			}
		}

		void execute() override {
			PROFILE_FUNCTION();

			gpu::bindUniformBuffer(UniformBuffer::SHADOW, m_shadow_matrices_ub.buffer, m_shadow_matrices_ub.offset, m_shadow_matrices_ub.size);

			auto bind = [](auto& buffer, const auto& data, i32 idx){
				const u32 capacity = (data.byte_size() + 15) & ~15;
				if (buffer.capacity < capacity) {
					if (buffer.buffer) gpu::destroy(buffer.buffer);
					buffer.buffer = gpu::allocBufferHandle();
					gpu::createBuffer(buffer.buffer, gpu::BufferFlags::SHADER_BUFFER, capacity, nullptr);
					buffer.capacity = capacity;
				}
				if (!data.empty()) {
					gpu::update(buffer.buffer, data.begin(), data.byte_size());
					gpu::bindShaderBuffer(buffer.buffer, idx, gpu::BindShaderBufferFlags::NONE);
				}
			};

			bind(m_pipeline->m_cluster_buffers.lights, m_point_lights, 11);
			bind(m_pipeline->m_cluster_buffers.clusters, m_clusters, 12);
			bind(m_pipeline->m_cluster_buffers.maps, m_map, 13);
			bind(m_pipeline->m_cluster_buffers.env_probes, m_env_probes, 14);
			bind(m_pipeline->m_cluster_buffers.refl_probes, m_refl_probes, 15);
		}


		struct Cluster {
			u32 offset;
			u32 point_lights_count;
			u32 env_probes_count;
			u32 refl_probes_count;
		};

		struct ClusterPointLight {
			Vec3 pos;
			float radius;
			Quat rot;
			Vec3 color;
			float attenuation_param;
			u32 atlas_idx;
			float fov;
			Vec2 padding;
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

		Array<i32> m_map;
		Array<Cluster> m_clusters;
		Array<ClusterPointLight> m_point_lights;
		Array<ClusterEnvProbe> m_env_probes;
		Array<ClusterReflProbe> m_refl_probes;

		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		bool m_is_clear = false;
		Matrix m_shadow_atlas_matrices[128];
		Renderer::TransientSlice m_shadow_matrices_ub;
	};
	
	Matrix getShadowMatrix(const PointLight& light, u32 atlas_idx) {
		Matrix prj;
		prj.setPerspective(light.fov, 1, 0.1f, light.range, true);
		const Quat rot = -m_scene->getUniverse().getRotation(light.entity);
		
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

	static float computePriority(const FillClustersJob::ClusterPointLight& light, const DVec3& light_pos, const DVec3& cam_pos) {
		return float(light.radius / length(cam_pos - light_pos));
	}

	struct PrepareViewJob : Renderer::RenderJob {
		PrepareViewJob(IAllocator& allocator)
			: m_allocator(allocator)
		{
		}
		
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

		void execute() override {
			PROFILE_FUNCTION();
			if (!m_instanced_meshes) return;
			if (m_instanced_meshes->models.empty()) return;

			const gpu::BufferHandle culled_buffer = m_pipeline->m_instanced_meshes_buffer;
			gpu::bindShaderBuffer(m_pipeline->m_indirect_buffer, 2, gpu::BindShaderBufferFlags::OUTPUT);
			
			m_pipeline->m_renderer.beginProfileBlock("cull instanced models", 0);
			for (const InstancedMeshes::Model& g : m_instanced_meshes->models) {
				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, g.drawcall_ub.buffer, g.drawcall_ub.offset, sizeof(UBValues));

				gpu::bindShaderBuffer(culled_buffer, 1, gpu::BindShaderBufferFlags::OUTPUT);
				gpu::useProgram(m_init_shader);
				gpu::dispatch(1, 1, 1);
				gpu::memoryBarrier(gpu::MemoryBarrierType::SSBO, culled_buffer);

				if (m_camera_params.is_shadow) {
					gpu::bindShaderBuffer(g.instance_data, 0, gpu::BindShaderBufferFlags::NONE);
				}
				else {
					gpu::bindShaderBuffer(g.instance_data, 0, gpu::BindShaderBufferFlags::OUTPUT);
					gpu::useProgram(m_update_lods_shader);
					for (u32 i = 0; i < g.cell_count; ++i) {
						if (!g.cells[i].visible) {
							gpu::bindUniformBuffer(UniformBuffer::DRAWCALL2, g.cells[i].ub.buffer, g.cells[i].ub.offset, g.cells[i].ub.size);
							gpu::dispatch((g.cells[i].count + 255) / 256, 1, 1);
						}
					}
				}
				
				gpu::useProgram(m_cull_shader);
				for (u32 i = 0; i < g.cell_count; ++i) {
					if (g.cells[i].visible) {
						gpu::bindUniformBuffer(UniformBuffer::DRAWCALL2, g.cells[i].ub.buffer, g.cells[i].ub.offset, g.cells[i].ub.size);
						gpu::dispatch((g.cells[i].count + 255) / 256, 1, 1);
					}
				}
				gpu::memoryBarrier(gpu::MemoryBarrierType::SSBO, culled_buffer);

				if (!m_camera_params.is_shadow) {
					gpu::bindShaderBuffer(g.instance_data, 0, gpu::BindShaderBufferFlags::NONE);
				}

				gpu::useProgram(m_indirect_shader);
				gpu::dispatch((g.meshes.size() + 255) / 256, 1, 1);
				gpu::memoryBarrier(gpu::MemoryBarrierType::SSBO, m_pipeline->m_indirect_buffer);

				gpu::useProgram(m_gather_shader);
				for (u32 i = 0; i < g.cell_count; ++i) {
					if (g.cells[i].visible) {
						gpu::bindUniformBuffer(UniformBuffer::DRAWCALL2, g.cells[i].ub.buffer, g.cells[i].ub.offset, g.cells[i].ub.size);
						gpu::dispatch((g.cells[i].count + 255) / 256, 1, 1);
					}
				}

				gpu::memoryBarrier(gpu::MemoryBarrierType::SSBO, culled_buffer);
			}
			gpu::memoryBarrier(gpu::MemoryBarrierType::COMMAND, m_pipeline->m_indirect_buffer);
			
			gpu::bindShaderBuffer(gpu::INVALID_BUFFER, 0, gpu::BindShaderBufferFlags::NONE);
			gpu::bindShaderBuffer(gpu::INVALID_BUFFER, 1, gpu::BindShaderBufferFlags::NONE);
			gpu::bindShaderBuffer(gpu::INVALID_BUFFER, 2, gpu::BindShaderBufferFlags::NONE);
			
			m_pipeline->m_renderer.endProfileBlock();
		}

		static float getDrawDistance(const Model& model) {
			const LODMeshIndices* lod_indices = model.getLODIndices();
			float dist = 0;
			for (u32 i = 0; i < 4; ++i) {
				if (lod_indices[i].to != -1) {
					dist = model.getLODDistances()[i];
				}
			}
			return sqrtf(dist);
		}

		void setupFur() {
			if (m_camera_params.is_shadow) return;

			RenderScene* scene = m_pipeline->m_scene;
			HashMap<EntityRef, FurComponent>& furs = scene->getFurs();
			if (furs.empty()) return;

			Span<const ModelInstance> mi = scene->getModelInstances();
			Sorter::Inserter inserter(m_view->sorter);
		
			const u64 type_mask = (u64)RenderableTypes::FUR << 32;
		
			// TODO handle sort order
			// TODO frustum culling
			// TODO render correct LOD
			for (auto iter = furs.begin(); iter.isValid(); ++iter) {
				const EntityRef e = iter.key();
				if (e.index >= (i32)mi.length()) continue;
				if (!mi[e.index].flags.isSet(ModelInstance::VALID)) continue;
				if (!iter.value().enabled) continue;

				const Model* model = mi[e.index].model;
				if (!model) continue;
				if (!model->isReady()) continue;

				for (i32 i = 0; i < model->getMeshCount(); ++i) {
					const Mesh& mesh = model->getMesh(i);
					if (mesh.type != Mesh::SKINNED) continue;

					const u8 bucket_id = m_view->layer_to_bucket[mesh.material->getLayer()];
					if (bucket_id != 0xff) {
						const u64 key = mesh.sort_key | ((u64)bucket_id << SORT_KEY_BUCKET_SHIFT);
						const u64 subrenderable = e.index | type_mask | ((u64)i << 40);
				
						inserter.push(key, subrenderable);
					}
				}
			}
		}

		void setup() override {
			PROFILE_FUNCTION();

			setupFur();

			m_view->renderables = m_pipeline->m_scene->getRenderables(m_view->cp.frustum);
			
			if (m_view->renderables) {
				m_pipeline->createSortKeys(*m_view);
				m_view->renderables->free(m_pipeline->m_renderer.getEngine().getPageAllocator());
				if (!m_view->sorter.keys.empty()) {
					m_pipeline->radixSort(m_view->sorter.keys.begin(), m_view->sorter.values.begin(), m_view->sorter.keys.size());
					m_pipeline->createCommands(*m_view);
				}
			}

			// instanced meshes
			if (m_instanced_meshes) {
				const Universe& universe = m_pipeline->m_scene->getUniverse();
				const HashMap<EntityRef, InstancedModel>& ims = m_pipeline->m_scene->getInstancedModels();
				Renderer& renderer = m_pipeline->m_renderer;
				const u32 instanced_define_mask = m_define_mask | (1 << renderer.getShaderDefineIdx("INSTANCED"));
				m_instanced_meshes->models.reserve(ims.size());
				UBValues ub_values;
				toPlanes(m_camera_params, Span(ub_values.camera_planes));

				for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
					const InstancedModel& im = iter.value();
					Model* m = im.model;
					if (!m || !m->isReady()) continue;

					float draw_distance = getDrawDistance(*m);
					InstancedMeshes::Model& g = m_instanced_meshes->models.emplace(m_allocator);

					g.origin = universe.getTransform(iter.key());
					Frustum frustum = m_camera_params.frustum.getRelative(g.origin.pos);
					const float radius = m->getOriginBoundingRadius();

					for (u32 i = 0; i < 16; ++i) {
						const InstancedModel::Grid::Cell& cell = im.grid.cells[i];

						if (cell.instance_count > 0) {
							const bool visible = frustum.intersectAABBWithOffset(cell.aabb, radius);
							const Vec3 cell_center = (cell.aabb.max + cell.aabb.min) * 0.5f;
							const Vec3 cell_half_extents = (cell.aabb.max - cell.aabb.min) * 0.5f;
							const float cell_radius = length(cell_half_extents);
							if (length(g.origin.pos - m_camera_params.pos + cell_center) - cell_radius < draw_distance) {
								const bool can_merge = g.cell_count > 0 && g.cells[g.cell_count - 1].visible == visible  && g.cells[g.cell_count - 1].offset + g.cells[g.cell_count - 1].count == cell.from_instance;
								if (can_merge) {
									g.cells[g.cell_count - 1].count += cell.instance_count;
									u32* tmp =(u32*)g.cells[g.cell_count - 1].ub.ptr;
									tmp[1] += cell.instance_count;
								}
								else {
									g.cells[g.cell_count].visible = visible;
									g.cells[g.cell_count].count = cell.instance_count;
									g.cells[g.cell_count].offset = cell.from_instance;
									Renderer::TransientSlice ub = m_pipeline->m_renderer.allocUniform(sizeof(u32) * 2);
									u32* tmp =(u32*)ub.ptr;
									tmp[0] = cell.from_instance;
									tmp[1] = cell.instance_count;
									g.cells[g.cell_count].ub = ub;
									++g.cell_count;
								}
							}
						}
					}
				
					if (g.cell_count == 0) {
						m_instanced_meshes->models.pop();
						continue;
					}

					g.lod_distances = *(Vec4*)m->getLODDistances();
					g.lod_indices.x = m->getLODIndices()[0].to;
					g.lod_indices.y = maximum(g.lod_indices.x, m->getLODIndices()[1].to);
					g.lod_indices.z = maximum(g.lod_indices.y, m->getLODIndices()[2].to);
					g.lod_indices.w = maximum(g.lod_indices.z, m->getLODIndices()[3].to);
					g.radius = m->getOriginBoundingRadius();
					g.instance_count = im.instances.size();
					g.instance_data = im.gpu_data;
					g.indirect_offset = atomicAdd(&m_pipeline->m_indirect_buffer_offset, m->getMeshCount());
					g.meshes.reserve(m->getMeshCount());
					for (i32 i = 0; i < m->getMeshCount(); ++i) {
						const Mesh& mesh = m->getMesh(i);
						InstancedMeshes::Model::MeshRenderData& m = g.meshes.emplace();
						m.mesh_rd = mesh.render_data;
						m.mesh = &mesh;
						m.material = mesh.material->getRenderData();
						m.layer = mesh.layer;
					}

					ub_values.camera_offset = Vec4(Vec3(g.origin.pos - m_camera_params.pos), 1);
					ub_values.lod_distances = g.lod_distances;
					ub_values.lod_indices = g.lod_indices;
					ub_values.indirect_offset = g.indirect_offset;
					ub_values.radius = g.radius;
					ub_values.batch_size = g.instance_count;
					ASSERT((u32)g.meshes.size() < lengthOf(ub_values.indices_count)); // TODO
					for (const auto& m : g.meshes) {
						ub_values.indices_count[&m - g.meshes.begin()].x = m.mesh_rd->indices_count;
					}

					g.drawcall_ub = m_pipeline->m_renderer.allocUniform(sizeof(ub_values));
					memcpy(g.drawcall_ub.ptr, &ub_values, sizeof(ub_values));
				}
			}
			jobs::setGreen(&m_view->ready);
		}

		IAllocator& m_allocator;
		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		u32 m_define_mask = 0;
		InstancedMeshes* m_instanced_meshes = nullptr;
		gpu::ProgramHandle m_gather_shader;
		gpu::ProgramHandle m_indirect_shader;
		gpu::ProgramHandle m_cull_shader;
		gpu::ProgramHandle m_init_shader;
		gpu::ProgramHandle m_update_lods_shader;
		View* m_view;
	};

	static int cull(lua_State* L) {
		const CameraParams cp = LuaWrapper::checkArg<CameraParams>(L, 1);
		PipelineImpl* pipeline = getClosureThis(L);
		const i32 bucket_count = lua_gettop(L) - 1;
		for (i32 i = 0; i < bucket_count; ++i) LuaWrapper::checkTableArg(L, 2 + i);
		
		PROFILE_FUNCTION();
		Array<UniquePtr<View>>& views = pipeline->m_views;
		UniquePtr<View>& view = views.emplace();
		LinearAllocator& allocator = pipeline->m_renderer.getCurrentFrameAllocator();
		view = UniquePtr<View>::create(allocator
			, allocator
			, pipeline->m_renderer.getEngine().getPageAllocator());
		view->cp = cp;
		if (views.size() > 1) {
			views[views.size() - 2]->instanced_meshes->next = view->instanced_meshes;
		}
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
			Bucket& bucket = view->buckets.emplace();
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
		}

		for (i32 i = 0; i < view->buckets.size(); ++i) {
			Bucket& bucket = view->buckets[i];
			view->layer_to_bucket[bucket.layer] = i;
		}

		PrepareViewJob& job = pipeline->m_renderer.createJob<PrepareViewJob>(allocator);
		job.m_pipeline = pipeline;
		job.m_camera_params = cp;
		job.m_view = view.get();

		if (pipeline->m_instancing_shader->isReady()) {
			const HashMap<EntityRef, InstancedModel>& ims = pipeline->m_scene->getInstancedModels();
			for (auto iter = ims.begin(), end = ims.end(); iter != end; ++iter) {
				if (iter.value().dirty) {
					pipeline->m_scene->initInstancedModelGPUData(iter.key());
				}
			}

			job.m_instanced_meshes = view->instanced_meshes;
			job.m_gather_shader = pipeline->m_instancing_shader->getProgram(1 << pipeline->m_renderer.getShaderDefineIdx("PASS3"));
			job.m_indirect_shader = pipeline->m_instancing_shader->getProgram(1 << pipeline->m_renderer.getShaderDefineIdx("PASS2"));
			u32 cull_shader_defines = 1 << pipeline->m_renderer.getShaderDefineIdx("PASS1");
			if (!cp.is_shadow) cull_shader_defines |= 1 << pipeline->m_renderer.getShaderDefineIdx("UPDATE_LODS");
			job.m_cull_shader = pipeline->m_instancing_shader->getProgram(cull_shader_defines);
			job.m_init_shader = pipeline->m_instancing_shader->getProgram(1 << pipeline->m_renderer.getShaderDefineIdx("PASS0"));
			job.m_update_lods_shader = pipeline->m_instancing_shader->getProgram(1 << pipeline->m_renderer.getShaderDefineIdx("UPDATE_LODS"));
		}
		jobs::setRed(&view->ready);
		pipeline->m_renderer.queue(job, pipeline->m_profiler_link);

		lua_newtable(L);
		const u32 view_id = pipeline->m_views.size() - 1;
		LuaWrapper::setField(L, -1, "view", view_id);
		for (u32 i = 0; i < (u32)view->buckets.size(); ++i) {
			LuaWrapper::setField(L, -1, view->buckets[i].layer_name, (view_id << 16) | i);
		}
		return 1;
	}

	struct RenderBucketJob : Renderer::RenderJob {
		void setup() override {
			PROFILE_FUNCTION();
			jobs::wait(&m_view->ready);

			const Bucket& bucket= m_view->buckets[m_bucket_id];
			m_cmds = bucket.cmd_page;

			const u32 instanced_define_mask = bucket.define_mask | (1 << m_pipeline->m_renderer.getShaderDefineIdx("INSTANCED"));
			for (const InstancedMeshes::Model& g : m_instanced_meshes->models) {
				for (auto& m : g.meshes) {
					if (m.layer == m_layer) {
						m.program = m.mesh->material->getShader()->getProgram(m.mesh->vertex_decl, instanced_define_mask | m.mesh->material->getDefineMask());
					}
				}
			}
		}

		void drawInstancedMeshes() {
			if (m_instanced_meshes->models.empty()) return;
			m_pipeline->m_renderer.beginProfileBlock("draw instanced models", 0);

			gpu::memoryBarrier(gpu::MemoryBarrierType::COMMAND, m_pipeline->m_indirect_buffer);
			const gpu::BufferHandle material_ub = m_pipeline->m_renderer.getMaterialUniformBuffer();
			for (const InstancedMeshes::Model& g : m_instanced_meshes->models) {
				for (const auto& m : g.meshes) {
					if (m.layer == m_layer) {
						gpu::bindTextures(m.material->textures, 0, m.material->textures_count);
						gpu::setState(m.material->render_states | m_render_state);
						gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, m.material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));

						gpu::useProgram(m.program);

						gpu::bindIndexBuffer(m.mesh_rd->index_buffer_handle);
						gpu::bindVertexBuffer(0, m.mesh_rd->vertex_buffer_handle, 0, m.mesh_rd->vb_stride);
						gpu::bindVertexBuffer(1, m_pipeline->m_instanced_meshes_buffer, 48, 32);

						gpu::bindIndirectBuffer(m_pipeline->m_indirect_buffer);

						gpu::drawIndirect(m.mesh_rd->index_type, u32(sizeof(Indirect) * (g.indirect_offset + (&m - g.meshes.begin()))));
					}
				}
			}
			
			gpu::bindIndirectBuffer(gpu::INVALID_BUFFER);
			gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
			gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
			gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

			m_pipeline->m_renderer.endProfileBlock();
		}

		void execute() override {
			PROFILE_FUNCTION();

			drawInstancedMeshes();
			if (!m_cmds) return;

			// inline in debug
			#define READ(T, N) \
				const T N = *(T*)cmd; \
				cmd += sizeof(T); \
				do {} while(false)
			if(m_cmds->header.size == 0 && !m_cmds->header.next) {
				m_pipeline->m_renderer.getEngine().getPageAllocator().deallocate(m_cmds, true);
				return;
			}

			Renderer& renderer = m_pipeline->m_renderer;
			renderer.beginProfileBlock("render_bucket", 0);
			const gpu::BufferHandle material_ub = m_pipeline->m_renderer.getMaterialUniformBuffer();

			const gpu::StateFlags render_states = m_render_state;
			u32 material_ub_idx = 0xffFFffFF;
			CmdPage* page = m_cmds;
			while (page) {
				const u8* cmd = page->data;
				const u8* cmd_end = page->data + page->header.size;
				while (cmd != cmd_end) {
					READ(RenderableTypes, type);
					switch(type) {
						case RenderableTypes::MESH:
						case RenderableTypes::MESH_MATERIAL_OVERRIDE: {
							READ(Mesh::RenderData*, mesh);
							READ(Material::RenderData*, material);
							READ(gpu::ProgramHandle, program);
							READ(u32, instances_count);
							READ(gpu::BufferHandle, buffer);
							READ(u32, offset);

							gpu::bindTextures(material->textures, 0, material->textures_count);
							gpu::setState(material->render_states | render_states);
							if (material_ub_idx != material->material_constants) {
								gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
								material_ub_idx = material->material_constants;
							}

							gpu::useProgram(program);

							gpu::bindIndexBuffer(mesh->index_buffer_handle);
							gpu::bindVertexBuffer(0, mesh->vertex_buffer_handle, 0, mesh->vb_stride);
							gpu::bindVertexBuffer(1, buffer, offset, 32);

							gpu::drawTrianglesInstanced(mesh->indices_count, instances_count, mesh->index_type);
							break;
						}
						case RenderableTypes::FUR:
						case RenderableTypes::SKINNED: {
							READ(Mesh::RenderData*, mesh);
							READ(Material::RenderData*, material);
							READ(gpu::ProgramHandle, program);
							READ(i32, bones_count);
							READ(u32, layers);
							READ(gpu::BufferHandle, ub_buffer);
							READ(u32, ub_offset);
							READ(u32, ub_size);

							gpu::bindTextures(material->textures, 0, material->textures_count);

							gpu::setState(material->render_states | render_states);
							if (material_ub_idx != material->material_constants) {
								gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
								material_ub_idx = material->material_constants;
							}

							gpu::useProgram(program);

							gpu::bindIndexBuffer(mesh->index_buffer_handle);
							gpu::bindVertexBuffer(0, mesh->vertex_buffer_handle, 0, mesh->vb_stride);
							gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
							
							gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, ub_buffer, ub_offset, ub_size);
							gpu::drawTrianglesInstanced(mesh->indices_count, layers, mesh->index_type);
							break;
						}
						case RenderableTypes::CURVE_DECAL: {
							READ(Material::RenderData*, material);
							READ(gpu::ProgramHandle, program);
							READ(gpu::BufferHandle, buffer);
							READ(u32, offset);
							READ(u32, count);
							READ(u32, nonintersecting_count);
								
							gpu::bindTextures(material->textures, 0, material->textures_count);
							if (material_ub_idx != material->material_constants) {
								gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
								material_ub_idx = material->material_constants;
							}
							gpu::useProgram(program);
							gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
							gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);

							gpu::StateFlags state = material->render_states | render_states;
							state = state & ~gpu::StateFlags::CULL_FRONT | gpu::StateFlags::CULL_BACK;
							if (nonintersecting_count) {
								gpu::setState(state);
								gpu::bindVertexBuffer(1, buffer, offset, 64);
								gpu::drawTrianglesInstanced(36, nonintersecting_count, gpu::DataType::U16);
							}

							if (count - nonintersecting_count) {
								state = state & ~gpu::StateFlags::DEPTH_TEST;
								state = state & ~gpu::StateFlags::CULL_BACK;
								state = state | gpu::StateFlags::CULL_FRONT;
								gpu::setState(state);
								const u32 offs = offset + sizeof(float) * 16 * nonintersecting_count;
								gpu::bindVertexBuffer(1, buffer, offs, 64);
								gpu::drawTrianglesInstanced(36, count - nonintersecting_count, gpu::DataType::U16);
							}
							break;
						}
						case RenderableTypes::DECAL: {
							READ(Material::RenderData*, material);
							READ(gpu::ProgramHandle, program);
							READ(gpu::BufferHandle, buffer);
							READ(u32, offset);
							READ(u32, count);
							READ(u32, nonintersecting_count);
								
							gpu::bindTextures(material->textures, 0, material->textures_count);
							if (material_ub_idx != material->material_constants) {
								gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
								material_ub_idx = material->material_constants;
							}
							gpu::useProgram(program);
							gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
							gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);

							gpu::StateFlags state = material->render_states | render_states;
							state = state & ~gpu::StateFlags::CULL_FRONT | gpu::StateFlags::CULL_BACK;
							if (nonintersecting_count) {
								gpu::setState(state);
								gpu::bindVertexBuffer(1, buffer, offset, 48);
								gpu::drawTrianglesInstanced(36, nonintersecting_count, gpu::DataType::U16);
							}

							if (count - nonintersecting_count) {
								state = state & ~gpu::StateFlags::DEPTH_TEST;
								state = state & ~gpu::StateFlags::CULL_BACK;
								state = state | gpu::StateFlags::CULL_FRONT;
								gpu::setState(state);
								const u32 offs = offset + sizeof(float) * 12 * nonintersecting_count;
								gpu::bindVertexBuffer(1, buffer, offs, 48);
								gpu::drawTrianglesInstanced(36, count - nonintersecting_count, gpu::DataType::U16);
							}
							break;
						}
						default: ASSERT(false); break;
					}
				}
				CmdPage* next = page->header.next;
				m_pipeline->m_renderer.getEngine().getPageAllocator().deallocate(page, true);
				page = next;
			}
			renderer.endProfileBlock();
			#undef READ
		}

		CmdPage* m_cmds;
		PipelineImpl* m_pipeline;
		u32 m_bucket_id;
		View* m_view;
		InstancedMeshes* m_instanced_meshes;
		u8 m_layer;
		gpu::StateFlags m_render_state;
	};

	static Vec4 packRotationLOD(const Quat& rot, float lod) {
		return rot.w > 0 ? Vec4(rot.x, rot.y, rot.z, lod) : Vec4(-rot.x, -rot.y, -rot.z, lod);
	}

	void createCommands(View& view
		, CmdPage* first_page
		, const u64* LUMIX_RESTRICT renderables
		, const u64* LUMIX_RESTRICT sort_keys
		, int count)
	{
		PROFILE_BLOCK("create cmds");
		// because of inlining in debug
		#define WRITE(x) do { \
			memcpy(out, &(x), sizeof(x)); \
			out += sizeof(x); \
		} while(false)
				
		#define WRITE_FN(x) do { \
			auto* p = x; \
			memcpy(out, &p, sizeof(p)); \
			out += sizeof(p); \
		} while(false)

		const Universe& universe = m_scene->getUniverse();
		Renderer& renderer = m_renderer;
		RenderScene* scene = m_scene;
		PageAllocator& page_allocator = m_renderer.getEngine().getPageAllocator();
		const ShiftedFrustum frustum = view.cp.frustum;
		const ModelInstance* LUMIX_RESTRICT model_instances = scene->getModelInstances().begin();
		const Transform* LUMIX_RESTRICT entity_data = universe.getTransforms(); 
		const DVec3 camera_pos = view.cp.pos;
				
		CmdPage* cmd_page = first_page;
		while (cmd_page->header.next) cmd_page = cmd_page->header.next;

		if (cmd_page->header.size > 0 && cmd_page->header.bucket != sort_keys[0] >> 56) {
			cmd_page->header.next = new (NewPlaceholder(), page_allocator.allocate(true))(CmdPage);
			cmd_page = cmd_page->header.next;
		}

		cmd_page->header.bucket = sort_keys[0] >> 56;
		const bool sort_depth = view.buckets[cmd_page->header.bucket].sort == Bucket::DEPTH;
		u64 instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
		u8* out = cmd_page->data;
		u32 define_mask = view.buckets[cmd_page->header.bucket].define_mask;

		u32 instanced_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("INSTANCED"));
		u32 skinned_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("SKINNED"));
		u32 fur_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("FUR"));

		auto new_page = [&](u8 bucket){
			cmd_page->header.size = int(out - cmd_page->data);
			CmdPage* new_page = new (NewPlaceholder(), page_allocator.allocate(true)) CmdPage;
			cmd_page->header.next = new_page;
			cmd_page = new_page;
			new_page->header.bucket = bucket;
			out = cmd_page->data;
			define_mask = view.buckets[bucket].define_mask;
			instanced_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("INSTANCED"));
			skinned_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("SKINNED"));
			fur_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("FUR"));
			const bool sort_depth = view.buckets[cmd_page->header.bucket].sort == Bucket::DEPTH;
			instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
		};

		const Mesh** sort_key_to_mesh = m_renderer.getSortKeyToMeshMap();

		for (u32 i = 0, c = count; i < c; ++i) {
			const EntityRef e = {int(renderables[i] & 0xFFffFFff)};
			const RenderableTypes type = RenderableTypes((renderables[i] >> 32) & SORT_VALUE_TYPE_MASK);
			const u8 bucket = sort_keys[i] >> 56;
			if(bucket != cmd_page->header.bucket) {
				new_page(bucket);
			}

			switch(type) {
				case RenderableTypes::MESH_MATERIAL_OVERRIDE: {
					const u32 mesh_idx = renderables[i] >> 40;
					const ModelInstance* LUMIX_RESTRICT mi = &model_instances[e.index];
					const Mesh& mesh = mi->meshes[mesh_idx];

					const Renderer::TransientSlice slice = renderer.allocTransient(sizeof(Vec4) * 2);
					u8* instance_data = slice.ptr;
					const EntityRef e = { int(renderables[i] & 0xFFffFFff) };
					const Transform& tr = entity_data[e.index];
					const float lod_d = model_instances[e.index].lod - mesh.lod;
					const Vec4 rot_lod = packRotationLOD(tr.rot, lod_d);
					const Vec3 lpos = Vec3(tr.pos - camera_pos);
					memcpy(instance_data, &rot_lod, sizeof(rot_lod));
					instance_data += sizeof(rot_lod);
					memcpy(instance_data, &lpos, sizeof(lpos));
					instance_data += sizeof(lpos);
					memcpy(instance_data, &tr.scale, sizeof(tr.scale));
					instance_data += sizeof(tr.scale);
					if ((cmd_page->data + sizeof(cmd_page->data) - out) < 41) {
						new_page(bucket);
					}


					if (mi->custom_material->isReady()) {
						Shader* shader = mi->custom_material->getShader();
						const gpu::ProgramHandle prog = shader->getProgram(mesh.vertex_decl, instanced_define_mask | mi->custom_material->getDefineMask());
						WRITE(type);
						WRITE(mesh.render_data);
						WRITE_FN(mi->custom_material->getRenderData());
						WRITE(prog);
						u32 count = 1;
						WRITE(count);
						WRITE(slice.buffer);
						WRITE(slice.offset);
					}
							
					break;
				}
				case RenderableTypes::MESH: {
					if (sort_keys[i] & SORT_KEY_INSTANCED_FLAG) {
						const u32 group_idx = renderables[i] & 0xffFF;
						const u32 instancer_idx = (renderables[i] >> 16) & 0xffFF;
						const AutoInstancer::Instances& instances = view.instancers[instancer_idx].instances[group_idx];
						const u32 total_count = instances.end->offset + instances.end->count;
						const Mesh& mesh = *sort_key_to_mesh[group_idx];
						if ((cmd_page->data + sizeof(cmd_page->data) - out) < 41) {
							new_page(bucket);
						}

						Shader* shader = mesh.material->getShader();
						const gpu::ProgramHandle prog = shader->getProgram(mesh.vertex_decl, instanced_define_mask | mesh.material->getDefineMask());

						WRITE(type);
						WRITE(mesh.render_data);
						WRITE_FN(mesh.material->getRenderData());
						WRITE(prog);
						WRITE(total_count);
						WRITE(instances.slice.buffer);
						WRITE(instances.slice.offset);
					}
					else {
						const u32 mesh_idx = renderables[i] >> 40;
						const ModelInstance* LUMIX_RESTRICT mi = &model_instances[e.index];
						const Mesh& mesh = mi->meshes[mesh_idx];
						const float mesh_lod = mesh.lod;
						int start_i = i;
						const u64 key = sort_keys[i] & instance_key_mask;
						while (i < c && (sort_keys[i] & instance_key_mask) == key) {
							++i;
						}
						const u32 count = u32(i - start_i);
						const Renderer::TransientSlice slice = renderer.allocTransient(count * (sizeof(Vec4) * 2));
						u8* instance_data = slice.ptr;
						for (int j = start_i; j < start_i + (i32)count; ++j) {
							const EntityRef e = { int(renderables[j] & 0xFFffFFff) };
							const Transform& tr = entity_data[e.index];
							const Vec3 lpos = Vec3(tr.pos - camera_pos);
							const float lod_d = model_instances[e.index].lod - mesh_lod;
							const Vec4 rot_lod = packRotationLOD(tr.rot, lod_d);
							memcpy(instance_data, &rot_lod, sizeof(rot_lod));
							instance_data += sizeof(rot_lod);
							memcpy(instance_data, &lpos, sizeof(lpos));
							instance_data += sizeof(lpos);
							memcpy(instance_data, &tr.scale, sizeof(tr.scale));
							instance_data += sizeof(tr.scale);
						}
						if ((cmd_page->data + sizeof(cmd_page->data) - out) < 41) {
							new_page(bucket);
						}

						Shader* shader = mesh.material->getShader();
						const gpu::ProgramHandle prog = shader->getProgram(mesh.vertex_decl, instanced_define_mask | mesh.material->getDefineMask());

						WRITE(type);
						WRITE(mesh.render_data);
						WRITE_FN(mesh.material->getRenderData());
						WRITE(prog);
						WRITE(count);
						WRITE(slice.buffer);
						WRITE(slice.offset);
							
						--i;
					}
					break;
				}
				case RenderableTypes::FUR:
				case RenderableTypes::SKINNED: {
					const u32 mesh_idx = renderables[i] >> 40;
					const ModelInstance* LUMIX_RESTRICT mi = &model_instances[e.index];
					const Transform& tr = entity_data[e.index];
					const Vec3 rel_pos = Vec3(tr.pos - camera_pos);
					const Mesh& mesh = mi->meshes[mesh_idx];
					Shader* shader = mesh.material->getShader();
					u32 defines = skinned_define_mask | mesh.material->getDefineMask();
					if (type == RenderableTypes::FUR) defines |= fur_define_mask;
					const gpu::ProgramHandle prog = shader->getProgram(mesh.vertex_decl, defines);

					if (u32(cmd_page->data + sizeof(cmd_page->data) - out) < (u32)mi->pose->count * sizeof(Matrix) + 69) {
						new_page(bucket);
					}

					WRITE(type);
					WRITE(mesh.render_data);
					WRITE_FN(mesh.material->getRenderData());
					WRITE(prog);
					WRITE(mi->pose->count);

					const Quat* rotations = mi->pose->rotations;
					const Vec3* positions = mi->pose->positions;

					Model& model = *mi->model;

					struct UBPrefix {
						float fur_scale;
						float gravity;
						float layers;
						float padding;
						Matrix model_mtx;
					};

					Renderer::TransientSlice ub = renderer.allocUniform(sizeof(DualQuat) * mi->pose->count + sizeof(UBPrefix));
					UBPrefix* prefix = (UBPrefix*)ub.ptr;
					prefix->model_mtx = Matrix(rel_pos, tr.rot);
					prefix->model_mtx.multiply3x3(tr.scale);

					u32 layers = 1;
					if (type == RenderableTypes::FUR) {
						FurComponent& fur = m_scene->getFur(e);
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
					WRITE(layers);
					WRITE(ub.buffer);
					WRITE(ub.offset);
					WRITE(ub.size);
					break;
				}
				case RenderableTypes::DECAL: {
					const Material* material = scene->getDecal(e).material;

					int start_i = i;
					const u64 key = sort_keys[i];
					while (i < c && sort_keys[i] == key) {
						++i;
					}
					const u32 count = i - start_i;
					struct DecalData {
						Vec3 pos;
						Quat rot;
						Vec3 half_extents;
						Vec2 uv_scale;
					};
					const Renderer::TransientSlice slice = renderer.allocTransient(count * (sizeof(DecalData)));
					const gpu::ProgramHandle prog = material->getShader()->getProgram(m_decal_decl, define_mask | material->getDefineMask());

					if ((cmd_page->data + sizeof(cmd_page->data) - out) < 21) {
						new_page(bucket);
					}
					DecalData* beg = (DecalData*)slice.ptr;
					DecalData* end = (DecalData*)(slice.ptr + (count - 1) * sizeof(DecalData));
					for(u32 j = start_i; j < i; ++j) {
						const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
						const Transform& tr = entity_data[e.index];
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const Decal& decal = scene->getDecal(e);
						const float m = maximum(decal.half_extents.x, decal.half_extents.y, decal.half_extents.z);
						const bool intersecting = frustum.intersectNearPlane(tr.pos, m * SQRT3);
						
						DecalData* iter = intersecting ? end : beg;
						iter->pos = lpos;
						iter->rot = tr.rot;
						iter->half_extents = decal.half_extents;
						iter->uv_scale = decal.uv_scale;
						intersecting ? --end : ++beg;
					}

					WRITE(type);
					WRITE_FN(material->getRenderData());
					WRITE(prog);
					WRITE(slice.buffer);
					WRITE(slice.offset);
					WRITE(count);
					const u32 nonintersecting_count = u32(beg - (DecalData*)slice.ptr);
					WRITE(nonintersecting_count);
					--i;
					break;
				}
				case RenderableTypes::CURVE_DECAL: {
					const Material* material = scene->getCurveDecal(e).material;

					int start_i = i;
					const u64 key = sort_keys[i];
					while (i < c && sort_keys[i] == key) {
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
					const Renderer::TransientSlice slice = renderer.allocTransient(count * (sizeof(DecalData)));
					const gpu::ProgramHandle prog = material->getShader()->getProgram(m_curve_decal_decl, define_mask | material->getDefineMask());

					if ((cmd_page->data + sizeof(cmd_page->data) - out) < 21) {
						new_page(bucket);
					}
					DecalData* beg = (DecalData*)slice.ptr;
					DecalData* end = (DecalData*)(slice.ptr + (count - 1) * sizeof(DecalData));
					for(u32 j = start_i; j < i; ++j) {
						const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
						const Transform& tr = entity_data[e.index];
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const CurveDecal& decal = scene->getCurveDecal(e);
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

					WRITE(type);
					WRITE_FN(material->getRenderData());
					WRITE(prog);
					WRITE(slice.buffer);
					WRITE(slice.offset);
					WRITE(count);
					const u32 nonintersecting_count = u32(beg - (DecalData*)slice.ptr);
					WRITE(nonintersecting_count);
					--i;
					break;
				}
				case RenderableTypes::LOCAL_LIGHT: {
					const u64 type_bits = (u64)RenderableTypes::LOCAL_LIGHT << 32;
					const u64 type_mask = (u64)0xff << 32;
					int start_i = i;
					while (i < c && (renderables[i] & type_mask) == type_bits) {
						++i;
					}

					struct LightData {
						Quat rot;
						Vec3 pos;
						float range;
						float attenuation;
						Vec3 color;
						Vec3 dir;
						float fov;
					};
					const Renderer::TransientSlice slice = renderer.allocTransient((i - start_i) * sizeof(LightData));

					LightData* beg = (LightData*)slice.ptr;
					LightData* end = (LightData*)(slice.ptr + (i - start_i - 1) - sizeof(LightData));

					for (u32 j = start_i; j < i; ++j) {
						const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
						const Transform& tr = entity_data[e.index];
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const PointLight& pl = scene->getPointLight(e);
						const bool intersecting = frustum.intersectNearPlane(tr.pos, pl.range * SQRT3);
							
						LightData* iter = intersecting ? end : beg;
						iter->pos = lpos;
						iter->rot = tr.rot;
						iter->range = pl.range;
						iter->attenuation = pl.attenuation_param;
						iter->color = pl.color * pl.intensity;
						iter->dir = tr.rot * Vec3(0, 0, 1);
						iter->fov = pl.fov;
						intersecting ? --end : ++beg;
					}
					if ((cmd_page->data + sizeof(cmd_page->data) - out) < 9) {
						new_page(bucket);
					}
					WRITE(type);
					const u32 total_count = i - start_i;
					const u32 nonintersecting_count = u32(beg - (LightData*)slice.ptr);
					WRITE(total_count);
					WRITE(nonintersecting_count);
					WRITE(slice.buffer);
					WRITE(slice.offset);

					--i;
					break;
				}
				default: ASSERT(false); break;
			}
		}

		cmd_page->header.size = int(out - cmd_page->data);
		#undef WRITE
		#undef WRITE_FN
	}


	void createCommands(View& view)
	{
		PROFILE_FUNCTION();
		const u64* renderables = view.sorter.values.begin();
		const u64* sort_keys = view.sorter.keys.begin();
		const int size = view.sorter.keys.size();
		profiler::pushInt("Count", size);
		i32 STEP = 4096;
		i32 steps = (size + STEP - 1) / STEP;
		if (steps < jobs::getWorkersCount()) {
			STEP = (size + jobs::getWorkersCount() - 1) / jobs::getWorkersCount();
			steps = (size + STEP - 1) / STEP;
		}
		PageAllocator& page_allocator = m_renderer.getEngine().getPageAllocator();

		StackAllocator<sizeof(CmdPage*) * 64, alignof(CmdPage*)> pages_allocator(m_allocator);
		Array<CmdPage*> pages(pages_allocator);
		pages.resize(steps);

		jobs::forEach(size, STEP, [&](i32 from, i32 to){
			const u32 step = from / STEP;
			pages[step] = new (NewPlaceholder(), page_allocator.allocate(true))(CmdPage);
			createCommands(view, pages[step], renderables + from, sort_keys + from, to - from);
		});

		CmdPage* prev = nullptr;
		for (CmdPage* page : pages) {
			while (page) {
				if (prev) {
					prev->header.next = prev->header.bucket != page->header.bucket ? nullptr : page;
				}
				Bucket& bucket = view.buckets[page->header.bucket];
				if (!bucket.cmd_page) bucket.cmd_page = page;
				
				prev = page;
				page = page->header.next;
			}
		}
	}

	void renderBucket(u32 viewbucket_id, RenderState state) {
		PROFILE_FUNCTION();
		RenderBucketJob& job = m_renderer.createJob<RenderBucketJob>();
		job.m_render_state = state.value;
		job.m_pipeline = this;
		job.m_bucket_id = viewbucket_id & 0xffFF;
		job.m_view = m_views[viewbucket_id >> 16].get();
		job.m_layer = job.m_view->buckets[job.m_bucket_id].layer;
		job.m_instanced_meshes = job.m_view->instanced_meshes;
		m_renderer.queue(job, m_profiler_link);
	}

	void fillClusters(LuaWrapper::Optional<CameraParams> cp) {
		PROFILE_FUNCTION();
		LinearAllocator& current_frame_allocator = m_renderer.getCurrentFrameAllocator();
		FillClustersJob& job = m_renderer.createJob<FillClustersJob>(current_frame_allocator, m_allocator);
		
		job.m_pipeline = this;
		if (cp.valid) {
			job.m_camera_params = cp.value;
		}
		else {
			job.m_is_clear = true;
		}

		CullResult* lights = m_scene->getRenderables(cp.value.frustum, RenderableTypes::LOCAL_LIGHT);
		const Universe& universe = m_scene->getUniverse();
		const DVec3 cam_pos = m_viewport.pos;

		AtlasSorter atlas_sorter;
		if (lights) {
			lights->forEach([&](EntityRef e){
				PointLight& pl = m_scene->getPointLight(e);
				FillClustersJob::ClusterPointLight& light = job.m_point_lights.emplace();
				light.radius = pl.range;
				const DVec3 light_pos = universe.getPosition(e);
				light.pos = Vec3(light_pos - cam_pos);
				light.rot = universe.getRotation(e);
				light.fov = pl.fov;
				light.color = pl.color * pl.intensity;
				light.attenuation_param = pl.attenuation_param;

				auto iter = m_shadow_atlas.map.find(e);
				if (pl.flags.isSet(PointLight::CAST_SHADOWS)) {
					light.atlas_idx = iter.isValid() ? iter.value() : -1;
					atlas_sorter.push(job.m_point_lights.size() - 1, computePriority(light, light_pos, cam_pos), e);
				}
				else {
					light.atlas_idx = -1;
					if(iter.isValid()) {
						m_shadow_atlas.remove(e);
					}
				}
			});
		}

		for (u32 i = 0; i < atlas_sorter.count; ++i) {
			FillClustersJob::ClusterPointLight& light = job.m_point_lights[atlas_sorter.lights[i].idx];
			if (light.atlas_idx != -1 && ShadowAtlas::getGroup(i) != ShadowAtlas::getGroup(light.atlas_idx)) {
				m_shadow_atlas.remove(atlas_sorter.lights[i].entity);
				light.atlas_idx = -1;
			}
		}
		
		if (!m_shadow_atlas.texture) {
			m_shadow_atlas.texture = m_renderer.createTexture(ShadowAtlas::SIZE, ShadowAtlas::SIZE, 1, gpu::TextureFormat::D32, gpu::TextureFlags::NO_MIPS, Renderer::MemRef(), "shadow_atlas");
		}

		for (u32 i = 0; i < atlas_sorter.count; ++i) {
			FillClustersJob::ClusterPointLight& light = job.m_point_lights[atlas_sorter.lights[i].idx];
			EntityRef e = atlas_sorter.lights[i].entity;
			PointLight& pl = m_scene->getPointLight(e);
			if (light.atlas_idx == -1) {
				light.atlas_idx = m_shadow_atlas.add(ShadowAtlas::getGroup(i), e);
				bakeShadow(pl, light.atlas_idx);
			}
			else if (pl.flags.isSet(PointLight::DYNAMIC)) {
				bakeShadow(pl, light.atlas_idx);
			}
			const Matrix mtx = getShadowMatrix(pl, light.atlas_idx);
			job.m_shadow_atlas_matrices[light.atlas_idx] = mtx;
		}

		if (lights) lights->free(m_renderer.getEngine().getPageAllocator());

		m_renderer.queue(job, m_profiler_link);
	}

	CameraParams getShadowCameraParams(i32 slice)
	{
		const Viewport& vp = m_shadow_camera_viewports[slice];
		CameraParams cp;
		cp.pos = vp.pos;
		cp.frustum = vp.getFrustum();
		cp.lod_multiplier = m_scene->getCameraLODMultiplier(vp.fov, vp.is_ortho);
		cp.is_shadow = true;
		cp.view = vp.getView(cp.pos);
		cp.projection = vp.getProjection();
		return cp;
	}
	
	void setRenderTargets(Span<gpu::TextureHandle> renderbuffers, gpu::TextureHandle ds, bool readonly_ds, bool srgb) {
		struct Cmd : Renderer::RenderJob
		{
			void setup() override { }

			void execute() override
			{
				PROFILE_FUNCTION();
			
				gpu::setFramebuffer(rbs, count, ds, flags);
				gpu::viewport(0, 0, w, h);
			}

			PipelineImpl* pipeline;
			gpu::TextureHandle rbs[8];
			gpu::TextureHandle ds;
			gpu::FramebufferFlags flags;
			u32 count;
			u32 w;
			u32 h;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		ASSERT(renderbuffers.length() < lengthOf(cmd.rbs));

		for (u32 i = 0; i < renderbuffers.length(); ++i) {
			cmd.rbs[i] = renderbuffers[i];
		}

		cmd.pipeline = this;
		cmd.ds = ds;
		cmd.count = renderbuffers.length();
		cmd.flags = srgb ? gpu::FramebufferFlags::SRGB : gpu::FramebufferFlags::NONE;
		if (readonly_ds) {
			cmd.flags = cmd.flags | gpu::FramebufferFlags::READONLY_DEPTH_STENCIL;
		}
		cmd.w = m_viewport.w;
		cmd.h = m_viewport.h;
		m_renderer.queue(cmd, m_profiler_link);
	}

	static int setRenderTargets(lua_State* L, bool has_ds, bool readonly_ds) {
		PROFILE_FUNCTION();
		PipelineImpl* pipeline = getClosureThis(L);

		const u32 rb_count = lua_gettop(L) - (has_ds ? 1 : 0);
		gpu::TextureHandle rbs[16];
		if(rb_count > lengthOf(rbs)) {
			luaL_error(L, "%s", "Too many render buffers");	
			return 0;
		}

		for(u32 i = 0; i < rb_count; ++i) {
			const i32 rb_idx = pipeline->toRenderbufferIdx(L, i + 1);
			rbs[i] = pipeline->m_renderbuffers[rb_idx].handle;
		}

		gpu::TextureHandle ds = gpu::INVALID_TEXTURE;
		if (has_ds) {
			const int ds_idx = pipeline->toRenderbufferIdx(L, rb_count + 1);
			ds = pipeline->m_renderbuffers[ds_idx].handle;
		}

		pipeline->setRenderTargets(Span(rbs, rb_count), ds, readonly_ds, true);
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

	struct RenderGrassJob : Renderer::RenderJob
	{
		RenderGrassJob(IAllocator& allocator)
			: m_allocator(allocator)
			, m_grass_allocator(allocator)
			, m_grass(m_grass_allocator)
		{
		}

		struct UBValues {
			Vec4 pos;
			Vec4 lod_ref_point;
			IVec2 from;
			IVec2 to;
			Vec2 terrain_size;
			float terrain_y_scale;
			float distance;
			u32 step;
			float grass_height;
			u32 indices_count;
			u32 type_mask;
			float radius;
			u32 rotation_mode;
			Vec2 terrain_xz_scale;
			u32 indirect_offset;
			Vec3 padding;
		};

		struct Grass {
			Mesh::RenderData* mesh;
			Material::RenderData* material;
			float distance;
			u32 step;
			Matrix mtx;
			Vec3 lod_ref_point;
			gpu::TextureHandle heightmap;
			gpu::TextureHandle splatmap;
			gpu::ProgramHandle program;
			Vec2 terrain_size;
			float terrain_y_scale;
			Vec2 terrain_xz_scale;
			float grass_height;
			IVec2 from;
			IVec2 to;
			u32 type;
			float radius;
			u32 rotation_mode;
			u32 indirect_offset;
			Renderer::TransientSlice drawcall_ub;
		};

		void setup() override {
			PROFILE_FUNCTION();
			const HashMap<EntityRef, Terrain*>& terrains = m_pipeline->m_scene->getTerrains();
			const Universe& universe = m_pipeline->m_scene->getUniverse();

			float fov_multiplier = 1;
			if (!m_pipeline->m_viewport.is_ortho) {
				fov_multiplier = clamp(degreesToRadians(60) / m_pipeline->m_viewport.fov, 1.f, 3.f);
			}

			for (Terrain* terrain : terrains) {
				const Transform tr = universe.getTransform(terrain->m_entity);
				Transform rel_tr = tr;
				rel_tr.pos = tr.pos - m_camera_params.pos;
				
				for (Terrain::GrassType& type : terrain->m_grass_types) {
					if (!type.m_grass_model || !type.m_grass_model->isReady()) continue;

					Vec3 points[4];
					AABB frustum_aabb(Vec3(0), Vec3(0));
					for (Vec3 v : m_camera_params.frustum.points) {
						frustum_aabb.addPoint(v);
					}
					frustum_aabb.translate(Vec3(m_camera_params.frustum.origin - m_pipeline->m_viewport.pos));
					const AABB grass_aabb(Vec3(-type.m_distance), Vec3(type.m_distance));
					AABB aabb = frustum_aabb.intersection(grass_aabb);
					aabb.translate(Vec3(m_pipeline->m_viewport.pos - tr.pos));
					if (aabb.min.x > aabb.max.x || aabb.min.z > aabb.max.z) continue;

					const i32 mesh_count = type.m_grass_model->getLODIndices()[0].to;
					for (i32 i = 0; i <= mesh_count; ++i) {
						const Mesh& mesh = type.m_grass_model->getMesh(i);
						Grass& grass = m_grass.emplace();
						grass.mesh = mesh.render_data;
						grass.material = mesh.material->getRenderData();
						grass.distance = type.m_distance * fov_multiplier;
						grass.program = mesh.material->getShader()->getProgram(mesh.vertex_decl, m_define_mask | grass.material->define_mask);
						grass.mtx = Matrix(Vec3(rel_tr.pos), rel_tr.rot);
						const i32 step_len = maximum(i32(type.m_spacing * 100), 1);
						const i32 steps = i32(grass.distance * 100) / step_len;
						grass.lod_ref_point = Vec3(tr.pos - m_pipeline->m_viewport.pos);
						grass.from = IVec2(Vec3((aabb.min) * 100.f).xz());
						grass.to = IVec2(Vec3((aabb.max) * 100.f).xz());
						grass.from = (grass.from / step_len) * step_len + IVec2(step_len);
						grass.to = (grass.to / step_len) * step_len + IVec2(step_len);
						grass.step = step_len;
						grass.heightmap = terrain->m_heightmap ? terrain->m_heightmap->handle : gpu::INVALID_TEXTURE;
						grass.splatmap = terrain->m_splatmap ? terrain->m_splatmap->handle : gpu::INVALID_TEXTURE;
						grass.terrain_size = terrain->getSize();
						grass.terrain_y_scale = terrain->getYScale();
						grass.terrain_xz_scale = Vec2(terrain->getXZScale());
						grass.grass_height = type.m_grass_model->getAABB().max.y;
						grass.type = u32(&type - terrain->m_grass_types.begin());
						grass.radius = type.m_grass_model->getOriginBoundingRadius();
						grass.rotation_mode = (u32)type.m_rotation_mode;

						grass.drawcall_ub = m_pipeline->m_renderer.allocUniform(sizeof(UBValues));
						UBValues* dc = new (NewPlaceholder(), grass.drawcall_ub.ptr) UBValues;
						dc->pos = Vec4(grass.mtx.getTranslation(), 1);
						dc->lod_ref_point = Vec4(grass.lod_ref_point, 1);
						dc->from = grass.from;
						dc->to = grass.to;
						dc->terrain_size = grass.terrain_size;
						dc->terrain_y_scale = grass.terrain_y_scale;
						dc->distance = grass.distance;
						dc->step = grass.step;
						dc->grass_height = grass.grass_height;
						dc->indices_count = grass.mesh->indices_count;
						dc->type_mask = 1 << grass.type;
						dc->radius = grass.radius;
						dc->rotation_mode = grass.rotation_mode;
						dc->terrain_xz_scale = grass.terrain_xz_scale;
					}
				}
			}
			
			if (m_grass.empty()) return;

			m_staging = m_pipeline->m_renderer.allocTransient(sizeof(Indirect) * m_grass.size());
			Indirect* indirect_dc = (Indirect*)m_staging.ptr;
			// we use 1 indirect structure as a sentinel in shader, so we need m_grass.size() + 1
			const i32 base_indirect_offset = atomicAdd(&m_pipeline->m_indirect_buffer_offset, m_grass.size() + 1);
			for (u32 i = 0; i < (u32)m_grass.size(); ++i) {
				Grass& grass = m_grass[i];
				indirect_dc[i].base_instance = 0;
				indirect_dc[i].base_vertex = 0;
				indirect_dc[i].first_index = 0;
				indirect_dc[i].vertex_count = grass.mesh->indices_count;
				indirect_dc[i].instance_count = 0;
				grass.indirect_offset = base_indirect_offset + i;
				UBValues* dc = (UBValues*)grass.drawcall_ub.ptr;
				dc->indirect_offset = grass.indirect_offset;
			}
		}

		void execute() override {
			PROFILE_FUNCTION();
			if (m_grass.empty()) return;
			if (!m_compute_shader) return;

			Renderer& renderer = m_pipeline->m_renderer;
			const gpu::BufferHandle material_ub = m_pipeline->m_renderer.getMaterialUniformBuffer();
			u32 material_ub_idx = 0xffFFffFF;
			renderer.beginProfileBlock("grass", 0);
			gpu::BufferHandle data = m_pipeline->m_renderer.getScratchBuffer();

			gpu::copy(m_pipeline->m_indirect_buffer
				, m_staging.buffer
				, m_grass[0].indirect_offset * sizeof(Indirect)
				, m_staging.offset
				, sizeof(Indirect) * m_grass.size()
			);

			gpu::bindShaderBuffer(data, 0, gpu::BindShaderBufferFlags::OUTPUT);
			gpu::bindShaderBuffer(m_pipeline->m_indirect_buffer, 1, gpu::BindShaderBufferFlags::OUTPUT);
			gpu::useProgram(m_compute_shader);
			for (const Grass& grass : m_grass) {
				gpu::bindTextures(&grass.heightmap, 2, 1);
				gpu::bindTextures(&grass.splatmap, 3, 1);
				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, grass.drawcall_ub.buffer, grass.drawcall_ub.offset, sizeof(UBValues));
				const IVec2 size =  (grass.to - grass.from) / grass.step;
				gpu::dispatch((size.x + 15) / 16, (size.y + 15) / 16, 1);
				gpu::memoryBarrier(gpu::MemoryBarrierType::SSBO, m_pipeline->m_indirect_buffer);
			}

			gpu::bindShaderBuffer(gpu::INVALID_BUFFER, 0, gpu::BindShaderBufferFlags::NONE);
			gpu::bindShaderBuffer(gpu::INVALID_BUFFER, 1, gpu::BindShaderBufferFlags::NONE);

			gpu::bindVertexBuffer(1, data, 0, 32);
			gpu::bindIndirectBuffer(m_pipeline->m_indirect_buffer);
			for (const Grass& grass : m_grass) {
				gpu::useProgram(grass.program);
				gpu::bindTextures(grass.material->textures, 0, grass.material->textures_count);
				gpu::bindIndexBuffer(grass.mesh->index_buffer_handle);
				gpu::bindVertexBuffer(0, grass.mesh->vertex_buffer_handle, 0, grass.mesh->vb_stride);
				if (material_ub_idx != grass.material->material_constants) {
					gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, grass.material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
					material_ub_idx = grass.material->material_constants;
				}

				gpu::setState(gpu::StateFlags::DEPTH_TEST | gpu::StateFlags::DEPTH_WRITE | m_render_state | grass.material->render_states);
				gpu::drawIndirect(grass.mesh->index_type, grass.indirect_offset * sizeof(Indirect));

			}
			gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			renderer.endProfileBlock();
		}

		IAllocator& m_allocator;
		gpu::ProgramHandle m_compute_shader;
		StackAllocator<sizeof(Grass) * 32, alignof(Grass)> m_grass_allocator;
		Array<Grass> m_grass;
		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		gpu::StateFlags m_render_state;
		u32 m_define_mask = 0;
		Renderer::TransientSlice m_staging;
	};

	struct RenderTerrainsJob : Renderer::RenderJob
	{
		RenderTerrainsJob(LinearAllocator& current_frame_allocator, IAllocator& allocator)
			: m_allocator(allocator)
			, m_instances(current_frame_allocator)
		{
		}

		void setup() override
		{
			PROFILE_FUNCTION();
			const HashMap<EntityRef, Terrain*>& terrains = m_pipeline->m_scene->getTerrains();
			if(terrains.empty()) return;

			Universe& universe = m_pipeline->m_scene->getUniverse();
			m_instances.reserve(terrains.size());
			for (const Terrain* terrain : terrains) {
				if (!terrain->m_heightmap) continue;
				if (!terrain->m_heightmap->isReady()) continue;
				if (!terrain->m_material || !terrain->m_material->isReady()) continue;

				const Transform& tr = universe.getTransform(terrain->m_entity);
				Instance& inst = m_instances.emplace(m_allocator);
				inst.pos = Vec3(tr.pos- m_camera_params.pos);
				inst.ref_pos = Vec3(tr.pos - m_pipeline->m_viewport.pos);
				inst.rot = tr.rot;
				inst.scale = terrain->getScale();
				inst.hm_size = terrain->getSize();
				Shader* shader = terrain->m_material->getShader();
				inst.program = shader->getProgram(gpu::VertexDecl(), m_define_mask);
				inst.material = terrain->m_material->getRenderData();
				if (isinf(inst.pos.x) || isinf(inst.pos.y) || isinf(inst.pos.z)) {
					m_instances.pop();
					continue;
				}

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
				quad.pos = Vec4(inst.pos, 0);
				quad.lpos = Vec4(inst.rot.conjugated().rotate(-inst.pos), 0);
				quad.hm_size = inst.hm_size;

				const Vec3 ref_pos = inst.rot.conjugated().rotate(-inst.ref_pos);
				IVec4 prev_from_to;
				float s = inst.scale.x;
				bool first = true;
				for (;;) {
					// round 
					IVec2 from = IVec2((ref_pos.xz() + Vec2(0.5f * s)) / float(s)) - IVec2(32);
					from.x = from.x & ~1;
					from.y = from.y & ~1;
					IVec2 to = from + IVec2(64);
					// clamp
					quad.from_to_sup = IVec4(from, to);
					
					from.x = clamp(from.x, 0, (int)ceil(inst.hm_size.x / s));
					from.y = clamp(from.y, 0, (int)ceil(inst.hm_size.y / s));
					to.x = clamp(to.x, 0, (int)ceil(inst.hm_size.x / s));
					to.y = clamp(to.y, 0, (int)ceil(inst.hm_size.y / s));

					auto draw_rect = [&](const IVec2& subfrom, const IVec2& subto){
						if (subfrom.x >= subto.x || subfrom.y >= subto.y) return;
						quad.from_to = IVec4(subfrom, subto);
						quad.terrain_scale = Vec4(inst.scale, 0);
						quad.cell_size = s;
						Instance::Quad& tmp = inst.quads.emplace();
						tmp.from = subfrom;
						tmp.to = subto;
						tmp.buf = m_pipeline->m_renderer.allocUniform(sizeof(quad));
						memcpy(tmp.buf.ptr, &quad, sizeof(quad));
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
					
					if (from.x <= 0 && from.y <= 0 && to.x * s >= inst.hm_size.x && to.y * s >= inst.hm_size.y) break;

					s *= 2;
					prev_from_to = IVec4(from / 2, to / 2);
				}
			}
		}

		void execute() override {
			PROFILE_FUNCTION();
			
			if (m_instances.empty()) return;
			const gpu::BufferHandle material_ub = m_pipeline->m_renderer.getMaterialUniformBuffer();

			gpu::StateFlags state = m_render_state;
			Renderer& renderer = m_pipeline->m_renderer;
			renderer.beginProfileBlock("terrain", 0);
			for (const Instance& inst : m_instances) {
				gpu::useProgram(inst.program);
				gpu::bindUniformBuffer(UniformBuffer::MATERIAL, material_ub, inst.material->material_constants * sizeof(MaterialConsts), sizeof(MaterialConsts));
				
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

				gpu::bindTextures(inst.material->textures, 0, inst.material->textures_count);

				gpu::setState(state);

				for (const Instance::Quad& q : inst.quads) {
					gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, q.buf.buffer, q.buf.offset, q.buf.size);
					gpu::drawArraysInstanced(gpu::PrimitiveType::TRIANGLE_STRIP, (q.to.x - q.from.x) * 2 + 2, q.to.y - q.from.y);
				}
			}
			renderer.endProfileBlock();
		}

		struct Instance {
			Instance(IAllocator& allocator) 
				: quads_allocator(allocator)
				, quads(quads_allocator)
			{}

			struct Quad {
				Renderer::TransientSlice buf;
				IVec2 from;
				IVec2 to;
			};
			Vec2 hm_size;
			Vec3 pos;
			Vec3 ref_pos;
			Quat rot;
			Vec3 scale;
			gpu::ProgramHandle program;
			Material::RenderData* material;
			StackAllocator<sizeof(Quad) * 32, alignof(Quad)> quads_allocator;
			Array<Quad> quads;
		};

		IAllocator& m_allocator;
		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		gpu::StateFlags m_render_state;
		Array<Instance> m_instances;
		u32 m_define_mask = 0;
	};

	void createSortKeys(PipelineImpl::View& view) {
		if (view.renderables->header.count == 0 && !view.renderables->header.next) return;
		PagedListIterator<const CullResult> iterator(view.renderables);

		view.instancers.reserve(jobs::getWorkersCount());
		LinearAllocator& allocator = m_renderer.getCurrentFrameAllocator();
		for (u8 i = 0; i < jobs::getWorkersCount(); ++i) {
			view.instancers.emplace(allocator, m_renderer.getEngine().getPageAllocator());
		}

		const float time_delta = m_renderer.getEngine().getLastTimeDelta();
		volatile i32 worker_idx = 0;

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
			RenderScene* scene = m_scene;
			ModelInstance* LUMIX_RESTRICT model_instances = scene->getModelInstances().begin();
			const Transform* LUMIX_RESTRICT entity_data = scene->getUniverse().getTransforms();
			const DVec3 camera_pos = view.cp.pos;
			const DVec3 lod_ref_point = m_viewport.pos;
			Sorter::Inserter inserter(view.sorter);

			const i32 instancer_idx = atomicIncrement(&worker_idx) - 1;
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
					case RenderableTypes::LOCAL_LIGHT: {
						// TODO use this for fillClusters
						break;
					}
					case RenderableTypes::DECAL: {
						for (int i = 0, c = page->header.count; i < c; ++i) {
							const EntityRef e = renderables[i];
							const Material* material = scene->getDecal(e).material;
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
							const Material* material = scene->getCurveDecal(e).material;
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
							const DVec3 pos = entity_data[e.index].pos;
							ModelInstance& mi = model_instances[e.index];
							const float squared_length = float(squaredLength(pos - lod_ref_point));
								
							const u32 lod_idx = mi.model->getLODMeshIndices(squared_length);

							auto create_key = [&](const LODMeshIndices& lod){
								for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
									const Mesh& mesh = mi.meshes[mesh_idx];
									const u8 layer = mi.custom_material ? mi.custom_material->getLayer() : mesh.layer;
									const u32 bucket = bucket_map[layer];
									const u32 mesh_sort_key = mi.custom_material ? 0x00FFffFF : mesh.sort_key;
									ASSERT(!mi.custom_material || mesh_idx == 0);
									const u64 subrenderable = e.index | type_mask | ((u64)mesh_idx << 40);
									if (bucket < 0xff) {
										const u64 key = mesh_sort_key | ((u64)bucket << SORT_KEY_BUCKET_SHIFT);
										inserter.push(key, subrenderable);
									} else if (bucket < 0xffFF) {
										const DVec3 pos = entity_data[e.index].pos;
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
							const DVec3 pos = entity_data[e.index].pos;
							ModelInstance& mi = model_instances[e.index];
							const float squared_length = float(squaredLength(pos - lod_ref_point));
								
							const u32 lod_idx = mi.model->getLODMeshIndices(squared_length);

							auto create_key = [&](const LODMeshIndices& lod){
								for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
									const Mesh& mesh = mi.meshes[mesh_idx];
									const u32 bucket = bucket_map[mesh.layer];
									ASSERT(!mi.custom_material);
									const u64 subrenderable = e.index | type_mask | ((u64)mesh_idx << 40);
									if (bucket < 0xff) {
										instancer.add(mesh.sort_key, subrenderable);
									} else if (bucket < 0xffFF) {
										const DVec3 pos = entity_data[e.index].pos;
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
					default: ASSERT(false); break;
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
				instances.slice = m_renderer.allocTransient(count * (2 * sizeof(Vec4)));
				u8* instance_data = instances.slice.ptr;
				const u32 sort_key = u32(&instances - instancer.instances.begin());
				const Mesh* mesh = sort_key_to_mesh[sort_key];

				#define WRITE(x) do { \
					memcpy(out, &(x), sizeof(x)); \
					out += sizeof(x); \
				} while(false)

				const float mesh_lod = mesh->lod;

				while (group) {
					for (u32 i = 0; i < group->count; ++i) {
						const EntityRef e = { (i32)group->renderables[i] };
						const Transform& tr = entity_data[e.index];
						const Vec3 lpos = Vec3(tr.pos - camera_pos);
						const float lod_d = model_instances[e.index].lod - mesh_lod;
						const Vec4 r = packRotationLOD(tr.rot, lod_d);
						memcpy(instance_data, &r, sizeof(r));
						instance_data += sizeof(r);
						memcpy(instance_data, &lpos, sizeof(lpos));
						instance_data += sizeof(lpos);
						memcpy(instance_data, &tr.scale, sizeof(tr.scale));
						instance_data += sizeof(tr.scale);
					}
					group = group->next;
				}
				#undef WRITE
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

			volatile i32 counter = 0;
			auto work = [&](){
				PROFILE_BLOCK("compute histogram");
				u32 histogram[SIZE];
				bool sorted = true;
				memset(histogram, 0, sizeof(histogram));

				i32 begin = atomicAdd(&counter, STEP);

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
					begin = atomicAdd(&counter, STEP);
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
		PROFILE_FUNCTION();
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::clear(flags, &color.x, depth);
			}
			Vec4 color;
			float depth;
			gpu::ClearFlags flags;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.color = Vec4(r, g, b, a);
		cmd.flags = (gpu::ClearFlags)flags;
		cmd.depth = depth;
		m_renderer.queue(cmd, m_profiler_link);
	}

	void viewport(int x, int y, int w, int h) {
		PROFILE_FUNCTION();

		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::viewport(x, y, w, h);
			}
			int x, y, w, h;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.x = x;
		cmd.y = y;
		cmd.w = w;
		cmd.h = h;

		m_renderer.queue(cmd, m_profiler_link);
	}

	void beginBlock(const char* name) {
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				renderer->beginProfileBlock(name, link);
			}
			StaticString<32> name;
			Renderer* renderer;
			i64 link;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.name = name;
		cmd.renderer = &m_renderer;
		m_profiler_link = profiler::createNewLinkID();
		cmd.link = m_profiler_link;
		m_renderer.queue(cmd, m_profiler_link);
	}

	void endBlock() {
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				renderer->endProfileBlock();
			}
			Renderer* renderer;
		};

		Cmd& cmd = m_renderer.createJob<Cmd>();
		cmd.renderer = &m_renderer;
		m_renderer.queue(cmd, m_profiler_link);
		m_profiler_link = 0;
	}
	
	void setOutput(lua_State* L, PipelineTexture tex) {
		if (tex.type != PipelineTexture::RENDERBUFFER) LuaWrapper::argError(L, 1, "renderbuffer");
		
		m_output = tex.renderbuffer;
	}

	bool environmentCastShadows() {
		if (!m_scene) return false;
		const EntityPtr env = m_scene->getActiveEnvironment();
		if (!env.isValid()) return false;
		return m_scene->getEnvironmentCastShadows((EntityRef)env);
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

	void saveRenderbuffer(lua_State* L)
	{
		struct Cmd : Renderer::RenderJob {
			Cmd(IAllocator& allocator) : allocator(allocator) {}
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				Array<u32> pixels(allocator);
				pixels.resize(w * h);
				gpu::TextureHandle staging = gpu::allocTextureHandle();
				const gpu::TextureFlags flags = gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::READBACK;
				gpu::createTexture(staging, w, h, 1, gpu::TextureFormat::RGBA8, flags, "staging_buffer");
				gpu::copy(staging, handle, 0, 0);
				gpu::readTexture(staging, 0, Span((u8*)pixels.begin(), pixels.byte_size()));
				gpu::destroy(staging);

				os::OutputFile file;
				if (fs->open(path, file)) {
					Texture::saveTGA(&file, w, h, gpu::TextureFormat::RGBA8, (u8*)pixels.begin(), false, Path(path), allocator);
					file.close();
				}
				else {
					logError("Failed to save ", path);
				}
			}

			IAllocator& allocator;
			u32 w, h;
			gpu::TextureHandle handle;
			FileSystem* fs;
			StaticString<LUMIX_MAX_PATH> path;
		};

		const i32 render_buffer = toRenderbufferIdx(L, 1);
		const char* out_path = LuaWrapper::checkArg<const char*>(L, 2);

		Cmd& cmd = m_renderer.createJob<Cmd>(m_renderer.getAllocator());
		cmd.handle = m_renderbuffers[render_buffer].handle;
		cmd.w = m_viewport.w;
		cmd.h = m_viewport.h;
		cmd.path = out_path;
		cmd.fs = &m_renderer.getEngine().getFileSystem();
		m_renderer.queue(cmd, m_profiler_link);
	}

	void registerLuaAPI(lua_State* L)
	{
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);

		auto registerCFunction = [L, this](const char* name, lua_CFunction function) {
			lua_pushlightuserdata(L, this);
			lua_pushcclosure(L, function, 1);
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
		REGISTER_FUNCTION(bindUniformBuffer);
		REGISTER_FUNCTION(clear);
		REGISTER_FUNCTION(createBuffer);
		REGISTER_FUNCTION(createRenderbuffer);
		REGISTER_FUNCTION(createTextureArray);
		REGISTER_FUNCTION(createTexture2D);
		REGISTER_FUNCTION(createTexture3D);
		REGISTER_FUNCTION(dispatch);
		REGISTER_FUNCTION(drawArray);
		REGISTER_FUNCTION(endBlock);
		REGISTER_FUNCTION(environmentCastShadows);
		REGISTER_FUNCTION(executeCustomCommand);
		REGISTER_FUNCTION(fillClusters);
		REGISTER_FUNCTION(getCameraParams);
		REGISTER_FUNCTION(getShadowCameraParams);
		REGISTER_FUNCTION(keepRenderbufferAlive);
		REGISTER_FUNCTION(pass);
		REGISTER_FUNCTION(preloadShader);
		REGISTER_FUNCTION(releaseRenderbuffer);
		REGISTER_FUNCTION(render2D);
		REGISTER_FUNCTION(renderBucket);
		REGISTER_FUNCTION(renderDebugShapes);
		REGISTER_FUNCTION(renderGrass);
		REGISTER_FUNCTION(renderLocalLights);
		REGISTER_FUNCTION(renderParticles);
		REGISTER_FUNCTION(renderTerrains);
		REGISTER_FUNCTION(renderOpaque);
		REGISTER_FUNCTION(renderTransparent);
		REGISTER_FUNCTION(renderUI);
		REGISTER_FUNCTION(saveRenderbuffer);
		REGISTER_FUNCTION(setOutput);
		REGISTER_FUNCTION(viewport);

		lua_pushinteger(L, -2); lua_setfield(L, -2, "SHADOW_ATLAS");
		lua_pushinteger(L, -3); lua_setfield(L, -2, "REFLECTION_PROBES");

		registerConst("CLEAR_DEPTH", (u32)gpu::ClearFlags::DEPTH);
		registerConst("CLEAR_COLOR", (u32)gpu::ClearFlags::COLOR);
		registerConst("CLEAR_ALL", (u32)gpu::ClearFlags::COLOR | (u32)gpu::ClearFlags::DEPTH | (u32)gpu::ClearFlags::STENCIL);

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
				lua_pushcclosure(L, f, 1);			 \
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

	IAllocator& m_allocator;
	Renderer& m_renderer;
	i64 m_profiler_link;
	PipelineResource* m_resource;
	lua_State* m_lua_state;
	int m_lua_thread_ref;
	int m_lua_env;
	StaticString<32> m_define;
	RenderScene* m_scene;
	Draw2D m_draw2d;
	Shader* m_draw2d_shader;
	Array<UniquePtr<View>> m_views;
	jobs::Signal m_buckets_ready;
	Viewport m_viewport;
	Viewport m_prev_viewport;
	bool m_first_set_viewport = true;
	int m_output;
	Shader* m_debug_shape_shader;
	Shader* m_place_grass_shader;
	Shader* m_instancing_shader;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<Renderbuffer> m_renderbuffers;
	Array<ShaderRef> m_shaders;
	Array<gpu::TextureHandle> m_textures;
	Array<gpu::BufferHandle> m_buffers;
	os::Timer m_timer;
	volatile i32 m_indirect_buffer_offset;
	gpu::BufferHandle m_instanced_meshes_buffer;
	gpu::BufferHandle m_indirect_buffer;
	gpu::VertexDecl m_base_vertex_decl;
	gpu::VertexDecl m_2D_decl;
	gpu::VertexDecl m_simple_cube_decl;
	gpu::VertexDecl m_decal_decl;
	gpu::VertexDecl m_curve_decal_decl;
	gpu::VertexDecl m_3D_pos_decl;
	gpu::VertexDecl m_point_light_decl;
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


UniquePtr<Pipeline> Pipeline::create(Renderer& renderer, PipelineResource* resource, const char* define, IAllocator& allocator)
{
	return UniquePtr<PipelineImpl>::create(allocator, renderer, resource, define, allocator);
}


} // namespace Lumix