#include "gpu/gpu.h"
#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/crt.h"
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


namespace LuaWrapper {
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
}



struct GlobalState
{
	Matrix shadow_view_projection;
	Matrix shadowmap_matrices[4];
	Matrix camera_projection;
	Matrix camera_inv_projection;
	Matrix camera_view;
	Matrix camera_inv_view;
	Matrix camera_view_projection;
	Matrix camera_inv_view_projection;
	Vec4 fog_params;
	Vec4 fog_color;
	Vec4 cam_world_pos;
	Vec4 light_direction;
	Vec4 light_color;
	IVec2 framebuffer_size;
	float light_intensity;
	float light_indirect_intensity;
	float time;
	float shadow_cam_near_plane;
	float shadow_cam_far_plane;
};


template <typename T>
struct MTBucketArray
{
	enum {
		BUCKET_SIZE = 32768,
		MAX_COUNT = BUCKET_SIZE / sizeof(T)
	}; 

	struct Bucket {
		T* keys;
		T* values;

		int count = 0;
		MTBucketArray* array;

		void end() { array->end(*this); }

		void push(const T& key, const T& value)
		{
			keys[count] = key;
			values[count] = value;
			++count;
			if (count == MAX_COUNT) {
				array->end(*this); 
				*this = array->begin();
			}
		}
	};

	static inline MTBucketArray* s_free_arrays[64] = {};
	static inline u32 s_num_free_arrays = 0;
	static inline Mutex s_cs;

	static MTBucketArray* allocArray(IAllocator& allocator) {
		MutexGuard lock(s_cs);
		if (s_num_free_arrays == 0) {
			return LUMIX_NEW(allocator, MTBucketArray<T>)(allocator);
		}

		for (u32 i = 0; i < s_num_free_arrays; ++i) {
			if (&s_free_arrays[i]->m_allocator == &allocator) {
				MTBucketArray* a = s_free_arrays[i];
				--s_num_free_arrays;
				s_free_arrays[i] = s_free_arrays[s_num_free_arrays];
				return a;
			}
		}

		return LUMIX_NEW(allocator, MTBucketArray<T>)(allocator);
	}

	static void freeArray(MTBucketArray* a) {
		MutexGuard lock(s_cs);
		a->clear();
		ASSERT(s_num_free_arrays < lengthOf(s_free_arrays) - 1);
		s_free_arrays[s_num_free_arrays] = a;
		++s_num_free_arrays;
	}

	static void cleanupArrays() {
		for (u32 i = 0; i < s_num_free_arrays; ++i) {
			LUMIX_DELETE(s_free_arrays[i]->m_allocator, s_free_arrays[i]);
		}
		s_num_free_arrays = 0;
	}

	MTBucketArray(IAllocator& allocator) 
		: m_counts(allocator)
		, m_allocator(allocator)
		, m_keys_mem((u8*)OS::memReserve(1024 * 1024 * 16))
		, m_values_mem((u8*)OS::memReserve(1024 * 1024 * 16))
	{
		m_keys_end = m_keys_mem;
		m_values_end = m_values_mem;
		m_counts.reserve(1024 * 1024 * 16 / BUCKET_SIZE);
	}

	~MTBucketArray()
	{
		PROFILE_FUNCTION();
		OS::memRelease(m_values_mem);
		OS::memRelease(m_keys_mem);
	}

	void clear() {
		m_keys_end = m_keys_mem;
		m_values_end = m_values_mem;
		m_counts.clear();
		m_total_count = 0;
	}

	Bucket begin()
	{
		PROFILE_FUNCTION();
		Bucket b;
		b.array = this;
		
		m_mutex.enter();
		m_counts.emplace();
		b.values = (T*)m_values_end;
		b.keys = (T*)m_keys_end;
		m_keys_end += BUCKET_SIZE;
		m_values_end += BUCKET_SIZE;
		m_mutex.exit();
		ASSERT(BUCKET_SIZE % OS::getMemPageSize() == 0);
		OS::memCommit(b.values, BUCKET_SIZE);
		OS::memCommit(b.keys, BUCKET_SIZE);
		return b;
	}

	void end(const Bucket& bucket)
	{
		const int bucket_idx = int(((u8*)bucket.values - m_values_mem) / BUCKET_SIZE);
		MutexGuard lock(m_mutex);
		m_counts[bucket_idx] = bucket.count;
	}

	void merge()
	{
		if (m_keys_end == m_keys_mem) return;
		int b = 0, e = int((m_keys_end - m_keys_mem) / BUCKET_SIZE) - 1;

		for(;;) {
			while (b != e && m_counts[b] == MAX_COUNT) { 
				++b;  
			}
			if (b == e) {
				for(int i = 0; i <= e; ++i) m_total_count += m_counts[i];
				break;
			}
			
			const int s = minimum(m_counts[e], MAX_COUNT - m_counts[b]);
			memcpy(&m_keys_mem[b * BUCKET_SIZE + m_counts[b] * sizeof(T)], &m_keys_mem[e * BUCKET_SIZE + (m_counts[e] - s) * sizeof(T)], s * sizeof(T));
			memcpy(&m_values_mem[b * BUCKET_SIZE + m_counts[b] * sizeof(T)], &m_values_mem[e * BUCKET_SIZE + (m_counts[e] - s) * sizeof(T)], s * sizeof(T));
			m_counts[b] += s;
			m_counts[e] -= s;
			if (m_counts[e] == 0) --e;
		}
	}

	int size() const { return m_total_count; }
	T* key_ptr() const { return (T*)m_keys_mem; }
	T* value_ptr() const { return (T*)m_values_mem; }

	IAllocator& m_allocator;
	Mutex m_mutex;
	u8* const m_keys_mem;
	u8* const m_values_mem;
	u8* m_keys_end;
	u8* m_values_end;
	Array<int> m_counts;
	int m_total_count = 0;
};


static const float SHADOW_CAM_NEAR = 50.0f;
static const float SHADOW_CAM_FAR = 5000.0f;


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
		CmdPage* next_init = nullptr;
		int size = 0;
		u8 bucket = 0;
	} header;
	u8 data[PageAllocator::PAGE_SIZE - sizeof(header)];
};


struct PipelineImpl final : Pipeline
{
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
	{
		m_viewport.w = m_viewport.h = 800;
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_draw2d_shader = rm.load<Shader>(Path("pipelines/draw2d.shd"));
		m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));
		m_text_mesh_shader = rm.load<Shader>(Path("pipelines/text_mesh.shd"));
		m_default_cubemap = rm.load<Texture>(Path("textures/common/default_probe.dds"));

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
		m_cube_vb = m_renderer.createBuffer(vb_mem, (u32)gpu::BufferFlags::IMMUTABLE);

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
		m_cube_ib = m_renderer.createBuffer(ib_mem, (u32)gpu::BufferFlags::IMMUTABLE);

		m_resource->onLoaded<&PipelineImpl::onStateChanged>(this);

		GlobalState global_state;
		const Renderer::MemRef global_state_mem = m_renderer.copy(&global_state, sizeof(global_state));
		m_global_state_buffer = m_renderer.createBuffer(global_state_mem, (u32)gpu::BufferFlags::UNIFORM_BUFFER);
		
		PassState pass_state;
		const Renderer::MemRef pass_state_mem = m_renderer.copy(&pass_state, sizeof(pass_state));
		m_pass_state_buffer = m_renderer.createBuffer(pass_state_mem, (u32)gpu::BufferFlags::UNIFORM_BUFFER);

		const Renderer::MemRef dc_mem = { 32*1024, nullptr, false };
		const u32 dc_ub_flags = (u32)gpu::BufferFlags::UNIFORM_BUFFER;
		m_drawcall_ub = m_renderer.createBuffer(dc_mem, dc_ub_flags);

		m_base_vertex_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
		m_base_vertex_decl.addAttribute(1, 12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		m_decal_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
		m_decal_decl.addAttribute(1, 0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(2, 12, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		m_decal_decl.addAttribute(3, 28, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

		m_2D_decl.addAttribute(0, 0, 2, gpu::AttributeType::FLOAT, 0);
		m_2D_decl.addAttribute(1, 8, 2, gpu::AttributeType::FLOAT, 0);
		m_2D_decl.addAttribute(2, 16, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		m_3D_pos_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);

		m_text_mesh_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
		m_text_mesh_decl.addAttribute(1, 12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
		m_text_mesh_decl.addAttribute(2, 16, 2, gpu::AttributeType::FLOAT, 0);

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
		m_renderer.frame();
		m_renderer.frame();
		m_renderer.frame();

		m_draw2d_shader->getResourceManager().unload(*m_draw2d_shader);
		m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
		m_text_mesh_shader->getResourceManager().unload(*m_text_mesh_shader);
		m_default_cubemap->getResourceManager().unload(*m_default_cubemap);

		for(ShaderRef& shader : m_shaders) {
			shader.res->getResourceManager().unload(*shader.res);
		}
		if (m_resource) m_resource->getResourceManager().unload(*m_resource);

		m_renderer.destroy(m_cube_ib);
		m_renderer.destroy(m_cube_vb);
		m_renderer.destroy(m_global_state_buffer);
		m_renderer.destroy(m_pass_state_buffer);
		m_renderer.destroy(m_drawcall_ub);

		clearBuffers();
		MTBucketArray<u64>::cleanupArrays();
	}


	void callInitScene()
	{
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "initScene");
		if (lua_type(m_lua_state, -1) == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != 0)
			{
				logError("lua") << lua_tostring(m_lua_state, -1);
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
			logError("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			logError("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
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
			logError("Renderer") << lua_tostring(m_lua_state, -1);
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
			logError("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			logError("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
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
		}

		for(int i = m_renderbuffers.size() - 1; i >= 0; --i) {
			if (m_renderbuffers[i].frame_counter > 1) {
				m_renderer.destroy(m_renderbuffers[i].handle);
				m_renderbuffers.swapAndPop(i);
			}
		}
	}

	void define(const char* define, bool enable) override {
		LuaWrapper::DebugGuard guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		LuaWrapper::setField(m_lua_state, -1, define, enable);
		lua_pop(m_lua_state, 1);
	}

	gpu::BufferHandle getDrawcallUniformBuffer() override { return m_drawcall_ub; }

	void setViewport(const Viewport& viewport) override 
	{
		m_viewport = viewport;
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
			camera_frustum.computePerspective(Vec3::ZERO,
				m_viewport.rot * Vec3(0, 0, -1),
				m_viewport.rot * Vec3(0, 1, 0),
				camera_fov,
				camera_ratio,
				split_distances[slice],
				split_distances[slice + 1]);

			const Sphere frustum_bounding_sphere = camera_frustum.computeBoundingSphere();
			const float bb_size = frustum_bounding_sphere.radius;
			const Vec3 light_forward = light_mtx.getZVector();

			Vec3 shadow_cam_pos = frustum_bounding_sphere.position;
			shadow_cam_pos = shadowmapTexelAlign(shadow_cam_pos, 0.5f * shadowmap_width - 2, bb_size, light_mtx);

			Matrix projection_matrix;
			projection_matrix.setOrtho(-bb_size, bb_size, -bb_size, bb_size, SHADOW_CAM_NEAR, SHADOW_CAM_FAR, gpu::isHomogenousDepth(), true);
			shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
			Matrix view_matrix;
			view_matrix.lookAt(shadow_cam_pos, shadow_cam_pos + light_forward, light_mtx.getYVector());

			const float ymul = gpu::isOriginBottomLeft() ? 0.5f : -0.5f;
			const Matrix bias_matrix(
				0.5, 0.0, 0.0, 0.0,
				0.0, ymul, 0.0, 0.0,
				0.0, 0.0, 1.0, 0.0,
				0.5, 0.5, 0.0, 1.0);

			global_state.shadowmap_matrices[slice] = bias_matrix * projection_matrix * view_matrix;
			global_state.shadow_cam_near_plane = SHADOW_CAM_NEAR;
			global_state.shadow_cam_far_plane = SHADOW_CAM_FAR;

			global_state.shadow_view_projection = projection_matrix * view_matrix;

			CameraParams& cp = m_shadow_camera_params[slice];
			cp.view = view_matrix;
			cp.projection = projection_matrix;
			cp.is_shadow = true;
			cp.lod_multiplier = 1;
			cp.pos = m_viewport.pos;
			cp.frustum.computeOrtho(m_viewport.pos + shadow_cam_pos
				, -light_forward
				, light_mtx.getYVector()
				, bb_size
				, bb_size
				, SHADOW_CAM_NEAR
				, SHADOW_CAM_FAR);

			findExtraShadowcasterPlanes(light_forward, camera_frustum, &cp.frustum);
		}
	}

	Vec2 getAtlasSize() const {
		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();
		if (!atlas_texture) return {1, 1};
		if (!atlas_texture->isReady()) return {1, 1};
		return {(float)atlas_texture->width, (float)atlas_texture->height};
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

		clearBuffers();

		{
			PROFILE_BLOCK("destroy renderbuffers");
			for (int i = m_renderbuffers.size() - 1; i >= 0; --i) {
				Renderbuffer& rb = m_renderbuffers[i];
				if (!rb.use_realtive_size) continue;
				const u32 w = u32(rb.relative_size.x * m_viewport.w + 0.5f);
				const u32 h = u32(rb.relative_size.y * m_viewport.h + 0.5f);
				if (rb.width != w || rb.height != h) {
					m_renderer.destroy(rb.handle);
					m_renderbuffers.swapAndPop(i);
				}
			}
		}

		const Matrix view = m_viewport.getViewRotation();
		const Matrix projection = m_viewport.getProjection(gpu::isHomogenousDepth());
		GlobalState global_state;
		global_state.camera_projection = projection;
		global_state.camera_inv_projection = projection.inverted();
		global_state.camera_view = view;
		global_state.camera_inv_view = view.fastInverted();
		global_state.camera_view_projection = projection * view;
		global_state.camera_inv_view_projection = global_state.camera_view_projection.inverted();
		global_state.time = m_timer.getTimeSinceStart();
		global_state.framebuffer_size.x = m_viewport.w;
		global_state.framebuffer_size.y = m_viewport.h;
		global_state.cam_world_pos = Vec4(m_viewport.pos.toFloat(), 1);

		if(m_scene) {
			const EntityPtr global_light = m_scene->getActiveEnvironment();
			if(global_light.isValid()) {
				EntityRef gl = (EntityRef)global_light;
				const Environment& env = m_scene->getEnvironment(gl);
				global_state.light_direction = Vec4(m_scene->getUniverse().getRotation(gl).rotate(Vec3(0, 0, -1)), 456); 
				global_state.light_color = Vec4(env.diffuse_color, 456);
				global_state.light_intensity = env.diffuse_intensity;
				global_state.light_indirect_intensity = env.indirect_intensity;
				global_state.fog_params = Vec4(env.fog_density, env.fog_bottom, env.fog_height, 456);
				global_state.fog_color = Vec4(env.fog_color, 1);
			}
		}

		if (!only_2d) {
			prepareShadowCameras(global_state);
		}

		struct StartPipelineJob : Renderer::RenderJob {
			void execute() override {
				PROFILE_FUNCTION();
				gpu::update(global_state_buffer, &global_state, sizeof(global_state));
				gpu::bindUniformBuffer(0, global_state_buffer, sizeof(GlobalState));
				gpu::bindUniformBuffer(1, pass_state_buffer, sizeof(PassState));
				gpu::bindUniformBuffer(4, pipeline->m_drawcall_ub, 32 * 1024);
				pipeline->m_stats = {};
			}
			void setup() override {}

			gpu::BufferHandle global_state_buffer;
			gpu::BufferHandle pass_state_buffer;
			PipelineImpl* pipeline;
			GlobalState global_state;
			PassState pass_state;
		};

		StartPipelineJob* start_job = LUMIX_NEW(m_renderer.getAllocator(), StartPipelineJob);
		start_job->pipeline = this;
		start_job->global_state = global_state;
		start_job->global_state_buffer = m_global_state_buffer;
		start_job->pass_state_buffer = m_pass_state_buffer;
		m_renderer.queue(start_job, 0);
		
		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "main");
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			if (m_scene) {
				m_scene->clearDebugLines();
				m_scene->clearDebugTriangles();
			}
			return false;
		}
		{
			PROFILE_BLOCK("lua pipeline main");
			LuaWrapper::pcall(m_lua_state, 0, 0);
		}
		lua_pop(m_lua_state, 1);


		struct EndPipelineJob : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				pipeline->m_last_frame_stats = pipeline->m_stats;
			}

			PipelineImpl* pipeline;
		};

		EndPipelineJob* end_job = LUMIX_NEW(m_renderer.getAllocator(), EndPipelineJob);
		end_job->pipeline = this;
		m_renderer.queue(end_job, 0);
		m_renderer.waitForCommandSetup();

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
				vb.size = 0;
				if (tris.size() == 0) return;

				program = pipeline->m_debug_shape_shader->getProgram(pipeline->m_base_vertex_decl, 0);
				vb = pipeline->m_renderer.allocTransient(sizeof(BaseVertex) * tris.size() * 3);
				BaseVertex* vertices = (BaseVertex*)vb.ptr;
				for (u32 i = 0, c = tris.size(); i < c; ++i) {
					vertices[3 * i + 0].color = tris[i].color;
					vertices[3 * i + 0].pos = (tris[i].p0 - viewport_pos).toFloat();
					vertices[3 * i + 1].color = tris[i].color;
					vertices[3 * i + 1].pos = (tris[i].p1 - viewport_pos).toFloat();
					vertices[3 * i + 2].color = tris[i].color;
					vertices[3 * i + 2].pos = (tris[i].p2 - viewport_pos).toFloat();
				}
				pipeline->m_scene->clearDebugTriangles();
			}


			void execute() override {
				PROFILE_FUNCTION();

				if (vb.size == 0) return;

				gpu::pushDebugGroup("debug triangles");

				gpu::update(pipeline->m_drawcall_ub, &Matrix::IDENTITY.m11, sizeof(Matrix));

				gpu::setState(u64(gpu::StateFlags::DEPTH_TEST) | u64(gpu::StateFlags::DEPTH_WRITE) | u64(gpu::StateFlags::CULL_BACK));
				gpu::useProgram(program);

				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(BaseVertex));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::drawArrays(0, vb.size / sizeof(BaseVertex), gpu::PrimitiveType::TRIANGLES);
				gpu::popDebugGroup();
			}

			PipelineImpl* pipeline;
			DVec3 viewport_pos;
			gpu::ProgramHandle program;
			Renderer::TransientSlice vb;
		};


		const Array<DebugTriangle>& tris = m_scene->getDebugTriangles();
		if (tris.empty() || !m_debug_shape_shader->isReady()) return;

		IAllocator& allocator = m_renderer.getAllocator();
		Cmd* cmd = LUMIX_NEW(allocator, Cmd);
		cmd->pipeline = this;
		cmd->viewport_pos = m_viewport.pos;
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
				vb.size = 0;
				if (lines.size() == 0) return;

				program = pipeline->m_debug_shape_shader->getProgram(pipeline->m_base_vertex_decl, 0);
				vb = pipeline->m_renderer.allocTransient(sizeof(BaseVertex) * lines.size() * 2);
				BaseVertex* vertices = (BaseVertex*)vb.ptr;
				for (u32 i = 0, c = lines.size(); i < c; ++i) {
					vertices[2 * i + 0].color = lines[i].color;
					vertices[2 * i + 0].pos = (lines[i].from - viewport_pos).toFloat();
					vertices[2 * i + 1].color = lines[i].color;
					vertices[2 * i + 1].pos = (lines[i].to - viewport_pos).toFloat();
				}
				pipeline->m_scene->clearDebugLines();
			}


			void execute() override {
				PROFILE_FUNCTION();
				if (vb.size == 0) return;

				gpu::pushDebugGroup("debug lines");

				gpu::update(pipeline->m_drawcall_ub, &Matrix::IDENTITY.m11, sizeof(Matrix));

				gpu::setState(u64(gpu::StateFlags::DEPTH_TEST) | u64(gpu::StateFlags::DEPTH_WRITE));
				gpu::useProgram(program);

				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(BaseVertex));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

				gpu::drawArrays(0, vb.size / sizeof(BaseVertex), gpu::PrimitiveType::LINES);
				gpu::popDebugGroup();
			}

			PipelineImpl* pipeline;
			DVec3 viewport_pos;
			gpu::ProgramHandle program;
			Renderer::TransientSlice vb;
		};


		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_shape_shader->isReady()) return;

		IAllocator& allocator = m_renderer.getAllocator();
		Cmd* cmd = LUMIX_NEW(allocator, Cmd);
		cmd->pipeline = this;
		cmd->viewport_pos = m_viewport.pos;
		m_renderer.queue(cmd, m_profiler_link);
	}


	void renderDebugShapes()
	{
		renderDebugTriangles();
		renderDebugLines();
		//renderDebugPoints();
	}


	void render2D()
	{
		if (!m_draw2d_shader->isReady()) {
			m_draw2d.clear(getAtlasSize());
			return;
		}

		if (m_draw2d.getIndices().empty()) {
			m_draw2d.clear(getAtlasSize());
			return;
		}

		struct Cmd : Renderer::RenderJob
		{
			Cmd(IAllocator& allocator) : cmd_buffer(allocator) {}

			void setup()
			{
				PROFILE_FUNCTION();
				size.set((float)pipeline->m_viewport.w, (float)pipeline->m_viewport.h);

				Draw2D& draw2d = pipeline->m_draw2d;

				num_indices = draw2d.getIndices().size();
				num_vertices = draw2d.getVertices().size();

				idx_buffer_mem = pipeline->m_renderer.allocTransient(draw2d.getIndices().byte_size());
				vtx_buffer_mem = pipeline->m_renderer.allocTransient(draw2d.getVertices().byte_size());
				memcpy(idx_buffer_mem.ptr, draw2d.getIndices().begin(), draw2d.getIndices().byte_size());
				memcpy(vtx_buffer_mem.ptr, draw2d.getVertices().begin(), draw2d.getVertices().byte_size());
				cmd_buffer.resize(draw2d.getCmds().size());
				memcpy(&cmd_buffer[0], draw2d.getCmds().begin(), sizeof(cmd_buffer[0]) * cmd_buffer.size());

				draw2d.clear(pipeline->getAtlasSize());

				program = pipeline->m_draw2d_shader->getProgram(pipeline->m_2D_decl, 0);
			}

			void execute()
			{
				PROFILE_FUNCTION();

				if (cmd_buffer.empty()) return;

				gpu::pushDebugGroup("draw2d");

				gpu::update(pipeline->m_drawcall_ub, &size.x, sizeof(size));
				u32 elem_offset = 0;
				const u64 blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
				gpu::setState(blend_state);
				gpu::useProgram(program);
				gpu::bindIndexBuffer(idx_buffer_mem.buffer);
				gpu::bindVertexBuffer(0, vtx_buffer_mem.buffer, vtx_buffer_mem.offset, 20);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

				for (Draw2D::Cmd& cmd : cmd_buffer) {
					if(cmd.clip_size.x < 0) {
						gpu::scissor(0, 0, pipeline->m_viewport.w, pipeline->m_viewport.h);
					}
					else {
						gpu::scissor(u32(maximum(cmd.clip_pos.x, 0.0f)),
							u32(maximum(cmd.clip_pos.x, 0.0f)),
							u32(minimum(cmd.clip_size.x, 65535.0f)),
							u32(minimum(cmd.clip_size.y, 65535.0f)));
					}
			
					gpu::TextureHandle texture_id = atlas_texture;
					if (cmd.texture) texture_id = *cmd.texture;
					if (!texture_id.isValid()) texture_id = atlas_texture;

					gpu::bindTextures(&texture_id, 0, 1);
					gpu::drawElements(idx_buffer_mem.offset + elem_offset * sizeof(u32), cmd.indices_count, gpu::PrimitiveType::TRIANGLES, gpu::DataType::U32);

					elem_offset += cmd.indices_count;
				}

				gpu::popDebugGroup();
			}

			gpu::TextureHandle atlas_texture;
			Renderer::TransientSlice idx_buffer_mem;
			Renderer::TransientSlice vtx_buffer_mem;
			int num_indices;
			int num_vertices;
			Array<Draw2D::Cmd> cmd_buffer;
			Vec2 size;
			PipelineImpl* pipeline;
			gpu::ProgramHandle program;
		};

		const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();
		IAllocator& allocator = m_renderer.getAllocator();
		Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
		cmd->pipeline = this;
		cmd->atlas_texture = atlas_texture->handle;
		m_renderer.queue(cmd, m_profiler_link);
	}

	void setUniverse(Universe* universe) override {
		RenderScene* scene = universe ? (RenderScene*)universe->getScene(crc32("renderer")) : nullptr;
		if (m_scene == scene) return;
		m_scene = scene;
		if (m_lua_state && m_scene) callInitScene();
	}

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
			{"depth24", gpu::TextureFormat::D24},
			{"depth24stencil8", gpu::TextureFormat::D24S8},
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
		};

		for (auto& i : FORMATS)
		{
			if (equalStrings(i.name, name)) return i.value;
		}
		logError("Renderer") << "Uknown texture format " << name;
		return gpu::TextureFormat::RGBA8;
	}


	int createRenderbuffer(float w, float h, bool relative, const char* format_str, const char* debug_name)
	{
		PROFILE_FUNCTION();
		const u32 rb_w = u32(relative ? w * m_viewport.w + 0.5f : w);
		const u32 rb_h = u32(relative ? h * m_viewport.h + 0.5f : h);
		const gpu::TextureFormat format = getFormat(format_str);

		for (int i = 0, n = m_renderbuffers.size(); i < n; ++i)
		{
			Renderbuffer& rb = m_renderbuffers[i];
			if (rb.frame_counter == 0) continue;
			if (rb.width != rb_w) continue;
			if (rb.height != rb_h) continue;
			if (rb.format != format) continue;

			rb.frame_counter = 0;
			return i;
		}

		Renderbuffer& rb = m_renderbuffers.emplace();
		rb.use_realtive_size = relative;
		rb.relative_size.set(w, h);
		rb.frame_counter = 0;
		rb.width = rb_w;
		rb.height = rb_h;
		rb.format = format;
		rb.handle = m_renderer.createTexture(rb_w, rb_h, 1, format, (u32)gpu::TextureFlags::NO_MIPS, {0, 0}, debug_name);

		return m_renderbuffers.size() - 1;
	}


	static int renderTerrains(lua_State* L)
	{
		PROFILE_FUNCTION();
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const CameraParams cp = checkCameraParams(L, 1);
		u64 state = 0;
		if (lua_gettop(L) > 1 && lua_istable(L, 2)) {
			state = getState(L, 2);
		}
		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		RenderTerrainsCommand* cmd = LUMIX_NEW(allocator, RenderTerrainsCommand)(allocator);

		char tmp[64];
		if (LuaWrapper::getOptionalStringField(L, 2, "define", Span(tmp))) {
			cmd->m_define_mask = tmp[0] ? 1 << pipeline->m_renderer.getShaderDefineIdx(tmp) : 0;
		}
		
		cmd->m_render_state = state;
		cmd->m_pipeline = pipeline;
		cmd->m_camera_params = cp;

		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);
		return 0;
	}
	

	static int renderParticles(lua_State* L)
	{
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		const CameraParams cp = checkCameraParams(L ,1);
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		PROFILE_FUNCTION();
		struct Cmd : Renderer::RenderJob
		{
			void setup() override
			{
				PROFILE_FUNCTION();
				const auto& emitters = m_pipeline->m_scene->getParticleEmitters();
				m_size = 0;
				if(emitters.size() == 0) return;
				
				Universe& universe = m_pipeline->m_scene->getUniverse();

				u32 byte_size = 0;
				for (ParticleEmitter* emitter : emitters) {
					byte_size += emitter->getInstanceDataSizeBytes();
				}

				byte_size += (sizeof(int) * 2 + sizeof(gpu::ProgramHandle) + sizeof(Vec3) + sizeof(Quat)) * emitters.size();
				m_vb = m_pipeline->m_renderer.allocTransient(byte_size);

				OutputMemoryStream str(m_vb.ptr, m_vb.size);
				gpu::VertexDecl decl;
				decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);

				for (ParticleEmitter* emitter : emitters) {
					if (!emitter->getResource() || !emitter->getResource()->isReady()) continue;
					
					const int size = emitter->getInstanceDataSizeBytes();
					if (size == 0) continue;

					const Transform tr = universe.getTransform((EntityRef)emitter->m_entity);
					const Vec3 lpos = (tr.pos - m_camera_params.pos).toFloat();

					const Material* material = emitter->getResource()->getMaterial();
					str.write(lpos);
					str.write(tr.rot);
					str.write(material->getShader()->getProgram(decl, 0));
					str.write(size);
					str.write(emitter->getInstancesCount());
					float* instance_data = (float*)str.skip(size);
					emitter->fillInstanceData(m_camera_params.pos, instance_data);
				}
				m_size = (u32)str.getPos();
			}

			void execute() override
			{
				PROFILE_FUNCTION();
				
				if (m_size == 0) return;
				
				gpu::pushDebugGroup("particles");
				InputMemoryStream blob(m_vb.ptr, m_size);
				
				const u64 blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
				gpu::setState(blend_state);
				while(blob.getPosition() < blob.size()) {
					const Vec3 lpos = blob.read<Vec3>();
					const Quat rot = blob.read<Quat>();
					const gpu::ProgramHandle program = blob.read<gpu::ProgramHandle>();
					const int byte_size = blob.read<int>();
					const int instances_count = blob.read<int>();

					const u32 offset = (u32)blob.getPosition();
					blob.skip(byte_size);

					Matrix mtx = rot.toMatrix();
					mtx.setTranslation(lpos);
					gpu::update(m_pipeline->m_drawcall_ub, &mtx.m11, sizeof(mtx));
					gpu::useProgram(program);
					gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
					gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
					gpu::bindVertexBuffer(1, m_vb.buffer, m_vb.offset + offset, 12);
					gpu::drawTriangleStripArraysInstanced(4, instances_count);
				}
				gpu::popDebugGroup();
			}

			PipelineImpl* m_pipeline;
			CameraParams m_camera_params;
			Renderer::TransientSlice m_vb;
			u32 m_size;
		};

		Cmd* cmd = LUMIX_NEW(pipeline->m_allocator, Cmd);
		cmd->m_pipeline = pipeline;
		cmd->m_camera_params = cp;

		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);

		return 0;
	}


	struct CameraParams
	{
		ShiftedFrustum frustum;
		DVec3 pos;
		float lod_multiplier;
		bool is_shadow;
		Matrix view;
		Matrix projection;
	};
	

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

	static int bindTextures(lua_State* L)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				gpu::bindTextures(m_textures_handles, m_offset, m_textures_count);
			}

			gpu::TextureHandle m_textures_handles[16];
			int m_offset = 0;
			u32 m_textures_count = 0;
		};

		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		LuaWrapper::checkTableArg(L, 1);

		const int offset = lua_gettop(L) > 1 ? LuaWrapper::checkArg<int>(L, 2) : 0;

		Cmd* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), Cmd);
		cmd->m_offset = offset;

		Engine& engine = pipeline->m_renderer.getEngine();
		const int len = (int)lua_objlen(L, 1);
		for(int i = 0; i < len; ++i) {
			lua_rawgeti(L, 1, i + 1);
			if(lua_type(L, -1) != LUA_TNUMBER) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Incorrect texture arguments of bindTextures");
			}

			if (cmd->m_textures_count > lengthOf(cmd->m_textures_handles)) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Too many texture in bindTextures");
			}

			const int res_idx = (int)lua_tointeger(L, -1);
			Resource* res = engine.getLuaResource(res_idx);
			if (!res || res->getType() != Texture::TYPE) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Unknown textures in bindTextures");
			}
			cmd->m_textures_handles[cmd->m_textures_count] = ((Texture*)res)->handle;
			++cmd->m_textures_count;

			lua_pop(L, 1);

		}

		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);

		return 0;
	};

	static int bindRenderbuffers(lua_State* L)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override 
			{
				PROFILE_FUNCTION();
				gpu::bindTextures(m_textures_handles, m_offset, m_textures_count);
			}

			gpu::TextureHandle m_textures_handles[16];
			int m_offset = 0;
			u32 m_textures_count = 0;
		};

		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		LuaWrapper::checkTableArg(L, 1);

		const int offset = lua_gettop(L) > 1 ? LuaWrapper::checkArg<int>(L, 2) : 0;

		Cmd* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), Cmd);
		cmd->m_offset = offset;
		
		const int len = (int)lua_objlen(L, 1);
		for(int i = 0; i < len; ++i) {
			lua_rawgeti(L, 1, i + 1);
			if(lua_type(L, -1) != LUA_TNUMBER) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Incorrect texture arguments of bindRenderbuffers");
			}

			if (cmd->m_textures_count > lengthOf(cmd->m_textures_handles)) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Too many texture in bindRenderbuffers");
			}

			const int rb_idx = (int)lua_tointeger(L, -1);
			cmd->m_textures_handles[cmd->m_textures_count] = pipeline->m_renderbuffers[rb_idx].handle;
			++cmd->m_textures_count;

			lua_pop(L, 1);

		}

		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);

		return 0;
	};

	static int renderEnvProbeVolumes(lua_State* L)
	{
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		const int shader_id = LuaWrapper::checkArg<int>(L, 1);
		const bool specular = LuaWrapper::checkArg<bool>(L, 3);
		const bool weights = LuaWrapper::checkArg<bool>(L, 4);
		Shader* shader = [&] {
			for (const ShaderRef& s : pipeline->m_shaders) {
				if(s.id == shader_id) {
					return s.res;
				}
			}
			return (Shader*)nullptr;
		}();
		if (!shader) {
			return luaL_error(L, "Unknown shader id %d in renderEnvProbeVolumes.", shader_id);
		}
		const CameraParams cp = checkCameraParams(L, 2);
		
		struct Cmd : Renderer::RenderJob
		{
			struct Probe
			{
				Vec3 pos;
				gpu::TextureHandle texture;
			};

			Cmd(IAllocator& allocator) : m_probes(allocator) {}

			void setup() override
			{
				RenderScene* scene = m_pipeline->getScene();
				scene->getEnvironmentProbes(m_probes);
				
				for (i32 i = m_probes.size() - 1; i >= 0; --i) {
					const EnvProbeInfo& probe = m_probes[i]; 
					if (!probe.radiance.isValid() && m_specular) m_probes.swapAndPop(i);
					else if (!probe.use_irradiance && !m_specular) m_probes.swapAndPop(i);
				}
			}


			void execute() override
			{
				PROFILE_FUNCTION();
				if(m_probes.empty()) return;

				gpu::useProgram(m_program);
				gpu::bindIndexBuffer(m_ib);
				gpu::bindVertexBuffer(0, m_vb, 0, 12);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				const DVec3 cam_pos = m_camera_params.pos;
				const u64 blend_state = gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
				for (const EnvProbeInfo& probe : m_probes) {
					Vec4* dc_mem = (Vec4*)gpu::map(m_pipeline->m_drawcall_ub, sizeof(Vec4) * 11);
					dc_mem[0] = Vec4((probe.position - cam_pos).toFloat(), 0);
					dc_mem[1] = Vec4(probe.half_extents, 0);
					if (m_specular) {
						gpu::bindTextures(&probe.radiance, 15, 1);
					}
					else {
						for (u32 i = 0; i < 9; ++i) {
							dc_mem[2 + i] = Vec4(probe.sh_coefs[i], 0);
						}
					}

					gpu::unmap(m_pipeline->m_drawcall_ub);
					
					const bool intersecting = m_camera_params.frustum.intersectNearPlane(probe.position, probe.half_extents.length());
					const u64 state = intersecting
						? (u64)gpu::StateFlags::CULL_FRONT
						: (u64)gpu::StateFlags::DEPTH_TEST | (u64)gpu::StateFlags::CULL_BACK;
					gpu::setState(state | blend_state);
					gpu::drawTriangles(36, gpu::DataType::U16);
				}
			}

			gpu::BufferHandle m_ib;
			gpu::BufferHandle m_vb;
			CameraParams m_camera_params;
			PipelineImpl* m_pipeline;
			bool m_specular;
			Array<EnvProbeInfo> m_probes;
			gpu::ProgramHandle m_program;
		};

		if(shader->isReady()) {
			IAllocator& allocator = pipeline->m_renderer.getAllocator();
			Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
			cmd->m_pipeline = pipeline;
			const u32 specular_mask = 1 << pipeline->m_renderer.getShaderDefineIdx("_LUMIX_SPECULAR");
			const u32 weights_mask = 1 << pipeline->m_renderer.getShaderDefineIdx("_LUMIX_WEIGHTS");
			u32 def_mask = specular ? specular_mask : 0;
			def_mask |= weights ? weights_mask : 0;
			cmd->m_program = shader->getProgram(pipeline->m_3D_pos_decl, def_mask);
			cmd->m_specular = specular;
			cmd->m_ib = pipeline->m_cube_ib;
			cmd->m_vb = pipeline->m_cube_vb;
			cmd->m_camera_params = cp;
			pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);
		}
		return 0;
	}


	static int pass(lua_State* L)
	{
		PROFILE_FUNCTION();
		struct PushPassStateCmd : Renderer::RenderJob {
			void execute() override {
				PROFILE_FUNCTION();
				gpu::update(pass_state_buffer, &pass_state, sizeof(pass_state));
				gpu::bindUniformBuffer(1, pass_state_buffer, sizeof(PassState));
			}
			void setup() override {}

			gpu::BufferHandle pass_state_buffer;
			PassState pass_state;
		};

		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		const CameraParams cp = checkCameraParams(L, 1);
		PushPassStateCmd* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), PushPassStateCmd);
		cmd->pass_state.view = cp.view;
		cmd->pass_state.projection = cp.projection;
		cmd->pass_state.inv_projection = cp.projection.inverted();
		cmd->pass_state.inv_view = cp.view.fastInverted();
		cmd->pass_state.view_projection = cp.projection * cp.view;
		cmd->pass_state.inv_view_projection = cmd->pass_state.view_projection.inverted();
		cmd->pass_state.view_dir = Vec4(cp.view.inverted().transformVector(Vec3(0, 0, -1)), 0);
		
		cmd->pass_state_buffer = pipeline->m_pass_state_buffer;
		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);
		return 0;
	}

	
	static int prepareCommands(lua_State* L)
	{
		PROFILE_FUNCTION();
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		LuaWrapper::checkTableArg(L, 1);
		const CameraParams cp = checkCameraParams(L, 1);

		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		PageAllocator& page_allocator = pipeline->m_renderer.getEngine().getPageAllocator();
		PrepareCommandsRenderJob* cmd = LUMIX_NEW(allocator, PrepareCommandsRenderJob)(allocator, page_allocator);

		LuaWrapper::checkTableArg(L, 2);

		const int table_len = (int)lua_objlen(L, 2);
		cmd->m_bucket_count = table_len;
		for(int i = 0; i < table_len; ++i) {
			lua_rawgeti(L, 2, i + 1);
			if(!lua_istable(L, -1)) {
				LUMIX_DELETE(allocator, cmd);
				return luaL_argerror(L, 2, "Incorrect bucket configuration");
			}

			PrepareCommandsRenderJob::SortOrder order = PrepareCommandsRenderJob::SortOrder::DEFAULT;
			char tmp[64];
			if (LuaWrapper::getOptionalStringField(L, -1, "sort", Span(tmp))) {
				order = equalIStrings(tmp, "depth") 
					? PrepareCommandsRenderJob::SortOrder::DEPTH
					: PrepareCommandsRenderJob::SortOrder::DEFAULT;
			}

			cmd->m_define_mask[i] = 0;
			cmd->m_bucket_sort_order[i] = order;

			lua_getfield(L, -1, "layers");
			const bool ok = LuaWrapper::forEachArrayItem<const char*>(L, -1, nullptr, [&](const char* layer_name){
				const int layer = pipeline->m_renderer.getLayerIdx(layer_name);
				cmd->m_bucket_map[layer] = i | (order == PrepareCommandsRenderJob::SortOrder::DEPTH ? 256 : 0);
			});
			lua_pop(L, 1);

			lua_getfield(L, -1, "defines");
			if (lua_istable(L, -1)) {
				LuaWrapper::forEachArrayItem<const char*>(L, -1, nullptr, [&](const char* define){
					cmd->m_define_mask[i] |= 1 << pipeline->m_renderer.getShaderDefineIdx(define);
				});
			}
			lua_pop(L, 1);

			if(!ok) {
				LUMIX_DELETE(allocator, cmd);
				return luaL_argerror(L, 2, "'layers' must be array of strings");
			}
			
			lua_pop(L, 1);
		}

		static_assert(sizeof(CmdPage) == PageAllocator::PAGE_SIZE, "Wrong page size");
		for(int i = 0; i < cmd->m_bucket_count; ++i) {
			CmdPage* page = new (NewPlaceholder(), page_allocator.allocate(true)) CmdPage;
			cmd->m_command_sets[i] = page;
			LuaWrapper::push(L, page);
		}
		cmd->m_camera_params = cp;
		cmd->m_pipeline = pipeline;
		const int num_cmd_sets = cmd->m_bucket_count;
		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);

		return num_cmd_sets;
	}


	static int drawArray(lua_State* L)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override { m_program = m_shader->getProgram(gpu::VertexDecl(), m_define_mask); }
			void execute() override 
			{
				PROFILE_FUNCTION();

				gpu::setState(m_render_state);

				gpu::bindTextures(m_textures_handles, 0, m_textures_count);

				if (m_uniforms_count > 0) {
					gpu::update(m_pipeline->m_drawcall_ub, m_uniforms, sizeof(m_uniforms[0]) * m_uniforms_count);
				}

				gpu::useProgram(m_program);
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::drawArrays(m_indices_offset, m_indices_count, gpu::PrimitiveType::TRIANGLE_STRIP);
			}

			PipelineImpl* m_pipeline;
			gpu::TextureHandle m_textures_handles[16];
			u32 m_textures_count = 0;
			float m_uniforms[16][4];
			int m_uniforms_count = 0;
			Shader* m_shader;
			int m_indices_count;
			int m_indices_offset;
			u32 m_define_mask = 0;
			u64 m_render_state;
			gpu::ProgramHandle m_program;

		};

		LuaWrapper::DebugGuard guard(L);

		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		const int indices_offset = LuaWrapper::checkArg<int>(L, 1);
		const int indices_count = LuaWrapper::checkArg<int>(L, 2);
		int shader_id = LuaWrapper::checkArg<int>(L, 3);
		if(lua_gettop(L) > 3) {
			LuaWrapper::checkTableArg(L, 4);
		}
		if(lua_gettop(L) > 4) {
			LuaWrapper::checkTableArg(L, 5);
		}
		const u64 rs = [&](){
			if(lua_gettop(L) > 6) {
				LuaWrapper::checkTableArg(L, 7);
				return getState(L, 7);
			}
			return (u64)gpu::StateFlags::DEPTH_WRITE | (u64)gpu::StateFlags::DEPTH_TEST;
		}();

		Shader* shader = nullptr;
		for (const ShaderRef& s : pipeline->m_shaders) {
			if(s.id == shader_id) {
				shader = s.res;
				break;
			}
		}
		if (!shader) {
			return luaL_error(L, "Unknown shader id %d in drawArrays.", shader_id);
		}

		if (shader->isFailure()) {
			return luaL_error(L, "Shader %s  failed to load. `drawArrays` has no effect.", shader->getPath().c_str());
		}
		if (!shader->isReady()) return 0;

		Cmd* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), Cmd);
		if(lua_gettop(L) > 3) {
			const u32 len = (u32)lua_objlen(L, 4);
			for(u32 i = 0; i < len; ++i) {
				lua_rawgeti(L, 4, i + 1);
				if(lua_type(L, -1) != LUA_TNUMBER) {
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					return luaL_error(L, "%s", "Incorrect texture arguments of drawArrays");
				}

				if (cmd->m_textures_count > lengthOf(cmd->m_textures_handles)) {
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					return luaL_error(L, "%s", "Too many texture in drawArray call");
				}

				const int rb_idx = (int)lua_tointeger(L, -1);
				cmd->m_textures_handles[cmd->m_textures_count] = pipeline->m_renderbuffers[rb_idx].handle;
				++cmd->m_textures_count;
				lua_pop(L, 1);
			}
		
			if (lua_istable(L, 5)) {
				for (u32 i = 0, c = (u32)lua_objlen(L, 5); i < c; ++i) {
					lua_rawgeti(L, 5, i + 1);
					if(lua_type(L, -1) != LUA_TTABLE) {
						LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
						return luaL_error(L, "%s", "Incorrect uniform arguments of drawArrays");
					}

					for(int i = 0; i < 4; ++i) {
						lua_rawgeti(L, -1, 1 + i);
						if (lua_type(L, -1) != LUA_TNUMBER) {
							LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
							return luaL_error(L, "%s", "Incorrect uniform arguments of drawArrays. Uniforms can only be Vec4.");
						}
						cmd->m_uniforms[cmd->m_uniforms_count][i] = (float)lua_tonumber(L, -1);
						lua_pop(L, 1);
					}

					++cmd->m_uniforms_count;
					lua_pop(L, 1);
				}
			}

			if (lua_isstring(L, 6)) {
				const char* define = lua_tostring(L, 6);
				cmd->m_define_mask = 1 << pipeline->m_renderer.getShaderDefineIdx(define);
			}
			else if (lua_istable(L, 6)) {
				lua_pushnil(L);
				while (lua_next(L, 6) != 0) {
					if(lua_type(L, -1) != LUA_TSTRING) {
						LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
						return luaL_error(L, "%s", "Incorrect uniform arguments of drawArrays");
					}
					const char* define = lua_tostring(L, -1);
					cmd->m_define_mask |= 1 << pipeline->m_renderer.getShaderDefineIdx(define);
					lua_pop(L, 1);
				}
			}

		}
	
		cmd->m_pipeline = pipeline;
		cmd->m_render_state = rs;
		cmd->m_shader = shader;
		cmd->m_indices_count = indices_count;
		cmd->m_indices_offset = indices_offset;
		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);

		return 0;
	}
	

	static void pushCameraParams(lua_State* L, const CameraParams& params)
	{
		lua_createtable(L, 0, 4);

		lua_createtable(L, 32+24, 0);
		const float* frustum = params.frustum.xs; 
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


	static int prepareShadowcastingLocalLights(lua_State* L)
	{
		/*const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		const Universe& universe = pipeline->m_scene->getUniverse();
		const CameraParams cp = checkCameraParams(L, 1);
		const int shadowmap_width = LuaWrapper::checkArg<int>(L, 2);
		const int shadowmap_height = LuaWrapper::checkArg<int>(L, 3);
		const int tile_width = LuaWrapper::checkArg<int>(L, 4);
		const int tile_height = LuaWrapper::checkArg<int>(L, 5);

		const int cols = shadowmap_width / tile_width;
		const int rows = shadowmap_height / tile_height;
		PointLight lights[16];
		const int max_count = maximum(lengthOf(lights), cols * rows);
		const int count = pipeline->m_scene->getClosestShadowcastingPointLights(cp.pos, max_count, lights);

		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		PageAllocator& page_allocator = pipeline->m_renderer.getEngine().getPageAllocator();

		lua_createtable(L, count, 0);
		for (int i = 0; i < count; ++i) {
			PrepareCommandsRenderJob* cmd = LUMIX_NEW(allocator, PrepareCommandsRenderJob)(allocator, page_allocator);
			cmd->m_bucket_count = 1;

			const bool ok = LuaWrapper::forEachArrayItem<const char*>(L, 6, nullptr, [&](const char* layer_name) {
				const int layer = pipeline->m_renderer.getLayerIdx(layer_name);
				cmd->m_bucket_map[layer] = 0;
				});
			if (!ok) {
				LUMIX_DELETE(allocator, cmd);
				return luaL_argerror(L, 2, "'layers' must be array of strings");
			}
			CommandSet* cmd_set = LUMIX_NEW(allocator, CommandSet)(allocator);
			pipeline->m_command_sets.push(cmd_set);
			const PointLight& pl = lights[i];
			cmd->m_command_sets[0] = cmd_set;

			lua_createtable(L, 0, 6);
			LuaWrapper::setField(L, -1, "entity", pl.entity.index);
			LuaWrapper::setField(L, -1, "viewport_x", (i % cols) * tile_width);
			LuaWrapper::setField(L, -1, "viewport_y", (i / cols) * tile_height);
			LuaWrapper::setField(L, -1, "viewport_w", tile_width);
			LuaWrapper::setField(L, -1, "viewport_h", tile_height);
			LuaWrapper::setField(L, -1, "bucket", cmd_set);
			lua_rawseti(L, -2, i + 1);
			
			// TODO light camera params
			CameraParams shadow_cam = cp;
			shadow_cam.pos = universe.getPosition(pl.entity);
			const Quat rot = universe.getRotation(pl.entity);
			const Vec3 dir = rot.rotate(Vec3(0, 0, 1));
			const Vec3 up = rot.rotate(Vec3(0, 1, 0));
			shadow_cam.frustum.computePerspective(shadow_cam.pos, dir, up, pl.fov, 1, 0.1f, pl.range);
			shadow_cam.is_shadow = true;
			cmd->m_camera_params = shadow_cam;
			cmd->m_pipeline = pipeline;
			const int num_cmd_sets = cmd->m_bucket_count;
			pipeline->m_renderer.queue(cmd);
		}

		return 1;*/
		ASSERT(false);
		// TODO
		return 0;
	}


	static int getCameraParams(lua_State* L)
	{
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		RenderScene* scene = pipeline->m_scene;

		CameraParams cp;

		cp.pos = pipeline->m_viewport.pos;
		cp.frustum = pipeline->m_viewport.getFrustum();
		cp.lod_multiplier = scene->getCameraLODMultiplier(pipeline->m_viewport.fov, pipeline->m_viewport.is_ortho);
		cp.is_shadow = false;
		cp.view = pipeline->m_viewport.getView(cp.pos);
		cp.projection = pipeline->m_viewport.getProjection(gpu::isHomogenousDepth());
		pushCameraParams(L, cp);

		return 1;
	}


	static void findExtraShadowcasterPlanes(const Vec3& light_forward
		, const Frustum& camera_frustum
		, ShiftedFrustum* shadow_camera_frustum)
	{
		/*static const Frustum::Planes planes[] = {
			Frustum::Planes::LEFT, Frustum::Planes::TOP, Frustum::Planes::RIGHT, Frustum::Planes::BOTTOM };
		bool prev_side = dotProduct(light_forward, camera_frustum.getNormal(planes[lengthOf(planes) - 1])) < 0;
		int out_plane = (int)Frustum::Planes::EXTRA0;
		Vec3 camera_frustum_center = camera_frustum.computeBoundingSphere().position;
		for (int i = 0; i < lengthOf(planes); ++i)
		{
			bool side = dotProduct(light_forward, camera_frustum.getNormal(planes[i])) < 0;
			if (prev_side != side)
			{
				Vec3 n0 = camera_frustum.getNormal(planes[i]);
				Vec3 n1 = camera_frustum.getNormal(planes[(i + lengthOf(planes) - 1) % lengthOf(planes)]);
				Vec3 line_dir = crossProduct(n1, n0);
				Vec3 n = crossProduct(light_forward, line_dir);
				float d = -dotProduct(camera_position, n);
				if (dotProduct(camera_frustum_center, n) + d < 0) {
					n = -n;
					d = -dotProduct(camera_position, n);
				}
				shadow_camera_frustum->setPlane((Frustum::Planes)out_plane, n, d);
				++out_plane;
				if (out_plane >(int)Frustum::Planes::EXTRA1) break;
			}
			prev_side = side;
		}*/
		// TODO
	}


	static Vec3 shadowmapTexelAlign(const Vec3& shadow_cam_pos,
		float shadowmap_width,
		float frustum_radius,
		const Matrix& light_mtx)
	{
		Matrix inv = light_mtx;
		inv.fastInverse();
		Vec3 out = inv.transformPoint(shadow_cam_pos);
		float align = 2 * frustum_radius / (shadowmap_width * 0.5f - 2);
		out.x -= fmodf(out.x, align);
		out.y -= fmodf(out.y, align);
		out = light_mtx.transformPoint(out);
		return out;
	}


	void renderTextMeshes()
	{
		if (!m_text_mesh_shader->isReady()) return;
		if (m_text_mesh_shader->m_texture_slot_count < 1) return;

		struct RenderJob : Renderer::RenderJob
		{
			void setup() override
			{
				PROFILE_FUNCTION();
				const Quat& rot = m_pipeline->m_viewport.rot;
				const DVec3& pos = m_pipeline->m_viewport.pos;
				const u32 count = m_pipeline->m_scene->getTextMeshesVerticesCount();
				vb = m_pipeline->m_renderer.allocTransient(count * sizeof(TextMeshVertex));
				m_pipeline->m_scene->getTextMeshesVertices((TextMeshVertex*)vb.ptr, pos, rot);
			}

			void execute() override
			{
				PROFILE_FUNCTION();
				if (vb.size == 0) return;

				gpu::useProgram(m_program);
				const u64 blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
				gpu::setState((u64)gpu::StateFlags::DEPTH_WRITE | (u64)gpu::StateFlags::DEPTH_TEST | blend_state);
				gpu::bindTextures(&m_atlas, 0, 1);
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset, 24);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::drawArrays(0, vb.size / sizeof(TextMeshVertex), gpu::PrimitiveType::TRIANGLES);
			}

			Renderer::TransientSlice vb;
			gpu::TextureHandle m_atlas;
			gpu::ProgramHandle m_program;
			PipelineImpl* m_pipeline;
		};


		Texture* atlas = m_renderer.getFontManager().getAtlasTexture();
		IAllocator& allocator = m_renderer.getAllocator();
		RenderJob* job = LUMIX_NEW(allocator, RenderJob);
		job->m_pipeline = this;
		job->m_atlas = atlas ? atlas->handle : gpu::INVALID_TEXTURE;
		job->m_program = m_text_mesh_shader->getProgram(m_text_mesh_decl, 0);
		m_renderer.queue(job, m_profiler_link);
	}

	static int renderLightProbeGrids(lua_State* L) {
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int shader_idx = LuaWrapper::checkArg<int>(L, 1);
		const CameraParams& cp = checkCameraParams(L, 2);

		Shader* shader = [&]() -> Shader* {
			for (const ShaderRef& s : pipeline->m_shaders) {
				if(s.id == shader_idx) {
					return ((Shader*)s.res);
				}
			}
			return nullptr;
		}();

		if (!shader || !shader->isReady()) return 0;

		struct RenderJob : Renderer::RenderJob {
			RenderJob(IAllocator& allocator)
				: m_grids(allocator)
			{}
			
			struct Grid {
				DVec3 position;
				IVec3 resolution;
				Vec3 half_extents;
				bool intersect_cam;
				gpu::TextureHandle spherical_harmonics[7];
			};

			void setup() override {
				RenderScene* scene = m_pipeline->getScene();
				Universe& universe = scene->getUniverse();
				Span<LightProbeGrid> grids = scene->getLightProbeGrids();
				m_grids.reserve(grids.length());
				for (const LightProbeGrid& grid : grids) {
					Grid& g = m_grids.emplace();
					g.half_extents = grid.half_extents;
					g.resolution = grid.resolution;
					g.position = universe.getPosition(grid.entity);
					g.intersect_cam = (g.position - m_cam_pos).squaredLength() < dotProduct(g.half_extents, g.half_extents);
					for (u32 i = 0; i < lengthOf(g.spherical_harmonics); ++i) {
						g.spherical_harmonics[i] = grid.data[i]->handle;
					}
				}
			}
			
			void execute() override {
				PROFILE_FUNCTION();
				gpu::useProgram(m_program);
				gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
				gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				for (const Grid& g : m_grids) {
					gpu::bindTextures(g.spherical_harmonics, 4, lengthOf(g.spherical_harmonics));
					if (g.intersect_cam) {
						gpu::setState((u64)gpu::StateFlags::CULL_FRONT);
					}
					else {
						gpu::setState((u64)gpu::StateFlags::CULL_BACK | (u64)gpu::StateFlags::DEPTH_TEST);
					}
					
					Vec4* dc_mem = (Vec4*)gpu::map(m_pipeline->m_drawcall_ub, 3 * sizeof(Vec4));
					memcpy(dc_mem, &g.resolution, sizeof(g.resolution));
					memcpy(dc_mem + 1, &g.half_extents, sizeof(g.half_extents));
					const Vec3 pos = (g.position - m_cam_pos).toFloat();
					memcpy(dc_mem + 2, &pos, sizeof(pos));
					gpu::unmap(m_pipeline->m_drawcall_ub);
					
					gpu::drawTriangles(36, gpu::DataType::U16);
				}
			}

			DVec3 m_cam_pos;
			Array<Grid> m_grids;
			gpu::ProgramHandle m_program;
			PipelineImpl* m_pipeline;
		};

		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		RenderJob* job = LUMIX_NEW(allocator, RenderJob)(allocator);
		job->m_pipeline = pipeline;
		job->m_cam_pos = cp.pos;
		job->m_program = shader->getProgram(pipeline->m_3D_pos_decl, 0);
		pipeline->m_renderer.queue(job, pipeline->m_profiler_link);
		return 0;
	}

	void renderLocalLights(const char* define, int shader_idx, CmdPage* cmds)
	{
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
				
				const u64 blend_state = gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
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
							gpu::setState(blend_state | (u64)gpu::StateFlags::CULL_FRONT);
							const u32 offs = offset + sizeof(float) * 16 * nonintersecting_count;
							gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
							gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);
							gpu::bindVertexBuffer(1, buffer, offs, 64);
							gpu::drawTrianglesInstanced(36, total_count - nonintersecting_count, gpu::DataType::U16);
						}

						if (nonintersecting_count) {
							gpu::setState(blend_state | (u64)gpu::StateFlags::DEPTH_TEST | (u64)gpu::StateFlags::CULL_BACK);
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

		RenderJob* job = LUMIX_NEW(m_renderer.getAllocator(), RenderJob);
		const u32 define_mask = define[0] ? 1 << m_renderer.getShaderDefineIdx(define) : 0;
		job->m_pipeline = this;
		job->m_cmds = cmds;
		job->m_program = shader->getProgram(m_point_light_decl, define_mask);
		m_renderer.queue(job, m_profiler_link);
	}


	static u64 getState(lua_State* L, int idx)
	{
		gpu::StencilFuncs stencil_func = gpu::StencilFuncs::DISABLE;
		u8 stencil_write_mask = 0xff;
		u8 stencil_ref = 0;
		u8 stencil_mask = 0;
		gpu::StencilOps stencil_sfail = gpu::StencilOps::KEEP;
		gpu::StencilOps stencil_zfail = gpu::StencilOps::KEEP;
		gpu::StencilOps stencil_zpass = gpu::StencilOps::KEEP;

		char tmp[64];
		u64 rs = (u64)gpu::StateFlags::DEPTH_TEST | (u64)gpu::StateFlags::DEPTH_WRITE;
		if (LuaWrapper::getOptionalStringField(L, idx, "blending", Span(tmp))) {
			if(equalIStrings(tmp, "add")) {
				rs |= gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
			}
			else if(equalIStrings(tmp, "alpha")) {
				rs |= gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
			}
			else if(equalIStrings(tmp, "multiply")) {
				rs |= gpu::getBlendStateBits(gpu::BlendFactors::DST_COLOR, gpu::BlendFactors::ZERO, gpu::BlendFactors::ONE, gpu::BlendFactors::ZERO);
			}
			else if(equalIStrings(tmp, "")) {
			}
			else {
				luaL_error(L, "Unknown blending mode");
			}
		}

		LuaWrapper::getOptionalFlagField(L, idx, "depth_test", &rs, (u64)gpu::StateFlags::DEPTH_TEST, true);
		LuaWrapper::getOptionalFlagField(L, idx, "wireframe", &rs, (u64)gpu::StateFlags::WIREFRAME, false);
		LuaWrapper::getOptionalFlagField(L, idx, "depth_write", &rs, (u64)gpu::StateFlags::DEPTH_WRITE, true);
		LuaWrapper::getOptionalField(L, idx, "stencil_func", reinterpret_cast<u8*>(&stencil_func));
		LuaWrapper::getOptionalField(L, idx, "stencil_write_mask", &stencil_write_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_ref", &stencil_ref);
		LuaWrapper::getOptionalField(L, idx, "stencil_mask", &stencil_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_sfail", reinterpret_cast<u8*>(&stencil_sfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zfail", reinterpret_cast<u8*>(&stencil_zfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zpass", reinterpret_cast<u8*>(&stencil_zpass));

		rs |= gpu::getStencilStateBits(stencil_write_mask, stencil_func, stencil_ref, stencil_mask, stencil_sfail, stencil_zfail, stencil_zpass);

		return rs;
	}


	static int renderBucket(lua_State* L)
	{
		PROFILE_FUNCTION();
		struct RenderJob : Renderer::RenderJob
		{
			void setup() override {}

			void execute() override
			{
				// inline in debug
				#define READ(T, N) \
					const T N = *(T*)cmd; \
					cmd += sizeof(T); \
					do {} while(false)
				PROFILE_FUNCTION();
				if(m_cmds->header.size == 0 && !m_cmds->header.next) {
					m_pipeline->m_renderer.getEngine().getPageAllocator().deallocate(m_cmds, true);
					return;
				}

				Renderer& renderer = m_pipeline->m_renderer;

				Stats stats = {};

				const u64 render_states = m_render_state;
				const gpu::BufferGroupHandle material_ub = renderer.getMaterialUniformBuffer();
				u32 material_ub_idx = 0xffFFffFF;
				CmdPage* page = m_cmds;
				while (page) {
					const u8* cmd = page->data;
					const u8* cmd_end = page->data + page->header.size;
					while (cmd != cmd_end) {
						READ(RenderableTypes, type);
						switch(type) {
							case RenderableTypes::MESH:
							case RenderableTypes::MESH_GROUP: {
								READ(Mesh::RenderData*, mesh);
								READ(Material::RenderData*, material);
								READ(gpu::ProgramHandle, program);
								READ(u16, instances_count);
								READ(gpu::BufferHandle, buffer);
								READ(u32, offset);

								gpu::bindTextures(material->textures, 0, material->textures_count);
								gpu::setState(material->render_states | render_states);
								if (material_ub_idx != material->material_constants) {
									gpu::bindUniformBuffer(2, material_ub, material->material_constants);
									material_ub_idx = material->material_constants;
								}

								gpu::useProgram(program);

								gpu::bindIndexBuffer(mesh->index_buffer_handle);
								gpu::bindVertexBuffer(0, mesh->vertex_buffer_handle, 0, mesh->vb_stride);
								gpu::bindVertexBuffer(1, buffer, offset, 32);

								gpu::drawTrianglesInstanced(mesh->indices_count, instances_count, mesh->index_type);
								++stats.draw_call_count;
								stats.triangle_count += instances_count * mesh->indices_count / 3;
								stats.instance_count += instances_count;
								break;
							}
							case RenderableTypes::SKINNED: {
								READ(Mesh::RenderData*, mesh);
								READ(Material::RenderData*, material);
								READ(gpu::ProgramHandle, program);
								READ(Vec3, pos);
								READ(Quat, rot);
								READ(float, scale);
								READ(i32, bones_count);

								Matrix* bones = (Matrix*)cmd;
								cmd += sizeof(bones[0]) * bones_count;

								Matrix model_mtx(pos, rot);
								model_mtx.multiply3x3(scale);

								gpu::bindTextures(material->textures, 0, material->textures_count);

								gpu::setState(material->render_states | render_states);
								if (material_ub_idx != material->material_constants) {
									gpu::bindUniformBuffer(2, material_ub, material->material_constants);
									material_ub_idx = material->material_constants;
								}

								u8* dc_mem = (u8*)gpu::map(m_pipeline->m_drawcall_ub, sizeof(Matrix) * (bones_count + 1));
								memcpy(dc_mem, &model_mtx, sizeof(Matrix));
								memcpy(dc_mem + sizeof(Matrix), bones, sizeof(Matrix) * bones_count);
								gpu::unmap(m_pipeline->m_drawcall_ub);

								gpu::useProgram(program);

								gpu::bindIndexBuffer(mesh->index_buffer_handle);
								gpu::bindVertexBuffer(0, mesh->vertex_buffer_handle, 0, mesh->vb_stride);
								gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
								gpu::drawTriangles(mesh->indices_count, mesh->index_type);
								++stats.draw_call_count;
								stats.triangle_count += mesh->indices_count / 3;
								++stats.instance_count;
								break;
							}
							case RenderableTypes::DECAL: {
								READ(Material::RenderData*, material);
								READ(gpu::ProgramHandle, program);
								READ(gpu::BufferHandle, buffer);
								READ(u32, offset);
								READ(u32, count);
								
								gpu::bindTextures(material->textures, 0, material->textures_count);
								
								gpu::useProgram(program);
								gpu::setState(material->render_states | render_states);
								gpu::bindIndexBuffer(m_pipeline->m_cube_ib);
								gpu::bindVertexBuffer(0, m_pipeline->m_cube_vb, 0, 12);
								gpu::bindVertexBuffer(1, buffer, offset, 44);

								gpu::drawTrianglesInstanced(36, count, gpu::DataType::U16);
								++stats.draw_call_count;
								stats.instance_count += count;
								break;
							}
							case RenderableTypes::GRASS: {
								READ(Quat, rot);
								READ(Vec3, pos);
								READ(float, distance);
								READ(Mesh::RenderData*, mesh);
								READ(Material::RenderData*, material);
								READ(gpu::ProgramHandle, program);
								READ(int, instances_count);
								READ(gpu::BufferHandle, buffer);
								READ(u32, offset);
								
								renderer.beginProfileBlock("grass", 0);
								gpu::useProgram(program);
								gpu::bindTextures(material->textures, 0, material->textures_count);
								gpu::bindIndexBuffer(mesh->index_buffer_handle);
								gpu::bindVertexBuffer(0, mesh->vertex_buffer_handle, 0, mesh->vb_stride);
								gpu::bindVertexBuffer(1, buffer, offset, 48);
								if (material_ub_idx != material->material_constants) {
									gpu::bindUniformBuffer(2, material_ub, material->material_constants);
									material_ub_idx = material->material_constants;
								}
								struct {
									Matrix mtx;
									float distance;
								} dc;
								dc.mtx = Matrix(pos, rot);
								dc.distance = distance;
								gpu::update(m_pipeline->m_drawcall_ub, &dc, sizeof(dc));

								gpu::setState(u64(gpu::StateFlags::DEPTH_TEST) | u64(gpu::StateFlags::DEPTH_WRITE) | render_states);
								gpu::drawTrianglesInstanced(mesh->indices_count, instances_count, mesh->index_type);
								renderer.endProfileBlock();
								++stats.draw_call_count;
								stats.triangle_count += mesh->indices_count / 3 * instances_count;
								stats.instance_count += instances_count;
								break;
							}
							default: ASSERT(false); break;
						}
					}
					CmdPage* next = page->header.next;
					m_pipeline->m_renderer.getEngine().getPageAllocator().deallocate(page, true);
					page = next;
				}
				#undef READ
				Profiler::pushInt("drawcalls", stats.draw_call_count);
				Profiler::pushInt("instances", stats.instance_count);
				Profiler::pushInt("triangles", stats.triangle_count);
				m_pipeline->m_stats.draw_call_count += stats.draw_call_count;
				m_pipeline->m_stats.instance_count += stats.instance_count;
				m_pipeline->m_stats.triangle_count += stats.triangle_count;
			}

			u64 m_render_state;
			PipelineImpl* m_pipeline;
			CmdPage* m_cmds;
		};

		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		CmdPage* cmd_page = LuaWrapper::checkArg<CmdPage*>(L, 1);
		LuaWrapper::checkTableArg(L, 2);
		const u64 state = getState(L, 2);

		RenderJob* job = LUMIX_NEW(pipeline->m_renderer.getAllocator(), RenderJob);

		job->m_render_state = state;
		job->m_pipeline = pipeline;
		job->m_cmds = cmd_page;
		pipeline->m_renderer.queue(job, pipeline->m_profiler_link);
		return 0;
	}


	static int getShadowCameraParams(lua_State* L)
	{
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int slice = LuaWrapper::checkArg<int>(L, 1);
		pushCameraParams(L, pipeline->m_shadow_camera_params[slice]);
		return 1;
	}

	static int setRenderTargets(lua_State* L, bool readonly_ds) {
		PROFILE_FUNCTION();
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx );
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const u32 rb_count = lua_gettop(L);
		int rbs[16];
		if(rb_count > lengthOf(rbs)) {
			logError("Renderer") << "Too many render buffers in " << pipeline->getPath();	
			return 0;
		}

		struct Cmd : Renderer::RenderJob
		{
			void setup() override { }

			void execute() override
			{
				PROFILE_FUNCTION();
			
				gpu::setFramebuffer(rbs, count, flags);
				gpu::viewport(0, 0, w, h);
			}

			PipelineImpl* pipeline;
			gpu::TextureHandle rbs[16];
			u32 flags;
			u32 count;
			u32 w;
			u32 h;
		};

		Cmd* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), Cmd);
		for(u32 i = 0; i < rb_count; ++i) {
			const int rb_idx = LuaWrapper::checkArg<int>(L, i + 1);
			cmd->rbs[i] = pipeline->m_renderbuffers[rb_idx].handle;
		}

		cmd->pipeline = pipeline;
		cmd->count = rb_count;
		cmd->flags = (u32)gpu::FramebufferFlags::SRGB;
		if (readonly_ds) {
			cmd->flags |= (u32)gpu::FramebufferFlags::READONLY_DEPTH_STENCIL;
		}
		cmd->w = pipeline->m_viewport.w;
		cmd->h = pipeline->m_viewport.h;
		pipeline->m_renderer.queue(cmd, pipeline->m_profiler_link);

		return 0;
	}

	static int setRenderTargets(lua_State* L) { 
		return setRenderTargets(L, false);
	}

	static int setRenderTargetsReadonlyDS(lua_State* L) { 
		return setRenderTargets(L, true);
	}

	struct RenderTerrainsCommand : Renderer::RenderJob
	{
		RenderTerrainsCommand(IAllocator& allocator)
			: m_allocator(allocator)
			, m_instances(allocator)
		{
		}

		void setup() override
		{
			PROFILE_FUNCTION();
			Array<TerrainInfo> infos(m_allocator);
			m_pipeline->m_scene->getTerrainInfos(infos);
			if(infos.empty()) return;

			m_instances.reserve(infos.size());
			for (TerrainInfo& info : infos) {
				if (!info.terrain->m_heightmap) continue;
				if (!info.terrain->m_heightmap->isReady()) continue;
				
				Instance& inst = m_instances.emplace();
				inst.pos = (info.position - m_camera_params.pos).toFloat();
				inst.rot = info.rot;
				inst.scale = info.terrain->getScale();
				inst.hm_size = info.terrain->getSize();
				inst.program = info.shader->getProgram(gpu::VertexDecl(), m_define_mask);
				inst.material = info.terrain->m_material->getRenderData();
			}
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			
			const gpu::BufferGroupHandle material_ub = m_pipeline->m_renderer.getMaterialUniformBuffer();

			u64 state = m_render_state;
			for (Instance& inst : m_instances) {
				Renderer& renderer = m_pipeline->m_renderer;
				renderer.beginProfileBlock("terrain", 0);
				gpu::useProgram(inst.program);
				gpu::bindUniformBuffer(2, material_ub, inst.material->material_constants);
				
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

				struct {
					IVec4 from_to;
					IVec4 from_to_sup;
					Vec4 pos;
					Vec4 lpos;
					Vec4 terrain_scale;
					Vec2 hm_size;
					float cell_size;
				} dc_data;
				dc_data.pos = Vec4(inst.pos, 0);
				dc_data.lpos = Vec4(inst.rot.conjugated().rotate(-inst.pos), 0);
				dc_data.hm_size = inst.hm_size;

				gpu::bindTextures(inst.material->textures, 0, inst.material->textures_count);

				gpu::setState(state);
				IVec4 prev_from_to;

				float s = 1 / 16.f;
				bool first = true;
				for (;;) {
					// round 
					IVec2 from = IVec2((dc_data.lpos.xz() + Vec2(0.5f * s)) / float(s)) - IVec2(first ? 128 : 64);
					from.x = from.x & ~1;
					from.y = from.y & ~1;
					IVec2 to = from + IVec2(first ? 256 : 128);
					// clamp
					dc_data.from_to_sup = IVec4(from, to);
					
					from.x = clamp(from.x, 0, (int)ceil(inst.hm_size.x / s));
					from.y = clamp(from.y, 0, (int)ceil(inst.hm_size.y / s));
					to.x = clamp(to.x, 0, (int)ceil(inst.hm_size.x / s));
					to.y = clamp(to.y, 0, (int)ceil(inst.hm_size.y / s));

					auto draw_rect = [&](const IVec2& subfrom, const IVec2& subto){
						if (subfrom.x >= subto.x || subfrom.y >= subto.y) return;
						dc_data.from_to = IVec4(subfrom, subto);
						dc_data.terrain_scale = Vec4(inst.scale, 0);
						dc_data.cell_size = s;
						gpu::update(m_pipeline->m_drawcall_ub, &dc_data, sizeof(dc_data));
						gpu::drawTriangleStripArraysInstanced((subto.x - subfrom.x) * 2 + 2, subto.y - subfrom.y);
						m_pipeline->m_stats.draw_call_count += 1;
						m_pipeline->m_stats.instance_count += 1;
						m_pipeline->m_stats.triangle_count += (subto.x - subfrom.x) * (subto.y - subfrom.y) * 2;
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



				renderer.endProfileBlock();
			}
		}

		struct Instance
		{
			Vec2 hm_size;
			Vec3 pos;
			Quat rot;
			Vec3 scale;
			gpu::ProgramHandle program;
			Material::RenderData* material;
		};

		IAllocator& m_allocator;
		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		u64 m_render_state;
		Array<Instance> m_instances;
		gpu::TextureHandle m_global_textures[16];
		int m_global_textures_count = 0;
		u32 m_define_mask = 0;

	};


	struct PrepareCommandsRenderJob : Renderer::RenderJob
	{
		enum class SortOrder : u8 {
			DEFAULT,
			DEPTH
		};


		PrepareCommandsRenderJob(IAllocator& allocator, PageAllocator& page_allocator) 
			: m_allocator(allocator)
			, m_page_allocator(page_allocator)
		{
			memset(m_bucket_map, 0xff, sizeof(m_bucket_map));
		}


		struct Histogram {
			static constexpr u32 BITS = 11;
			static constexpr u32 SIZE = 1 << BITS;
			static constexpr u32 BIT_MASK = SIZE - 1;
			static constexpr i32 STEP = 4096;

			u32 m_histogram[SIZE];
			bool m_sorted;
			Mutex m_cs;

			void compute(const u64* keys, const u64* values, int size, u16 shift) {
				memset(m_histogram, 0, sizeof(m_histogram));
				m_sorted = true;

				volatile i32 counter = 0;
				JobSystem::runOnWorkers([&](){
					PROFILE_FUNCTION();
					u32 histogram[SIZE];
					bool sorted = true;
					memset(histogram, 0, sizeof(histogram));

					i32 begin = atomicAdd(&counter, STEP);

					while(begin < size) {
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

					MutexGuard lock(m_cs);
					m_sorted &= sorted;
					for (u32 i = 0; i < lengthOf(m_histogram); ++i) {
						m_histogram[i] += histogram[i]; 
					}
				});
			}
		};


		void radixSort(u64* _keys, u64* _values, int size)
		{
			PROFILE_FUNCTION();
			Profiler::pushInt("count", size);
			if(size == 0) return;

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


		void createSortKeys(const CullResult* renderables, RenderableTypes type, MTBucketArray<u64>& sort_keys)
		{
			ASSERT(renderables);
			if (renderables->header.count == 0 && !renderables->header.next) return;
			PagedListIterator<const CullResult> iterator(renderables);
			
			const u8 local_light_layer = m_pipeline->m_renderer.getLayerIdx("local_light");
			const u8 local_light_bucket = m_bucket_map[local_light_layer];
			
			JobSystem::runOnWorkers([&](){
				PROFILE_BLOCK("create keys");
				int total = 0;
				const auto* bucket_map = m_bucket_map;
				RenderScene* scene = m_pipeline->m_scene;
				const ModelInstance* LUMIX_RESTRICT model_instances = scene->getModelInstances();
				const MeshSortData* LUMIX_RESTRICT mesh_data = scene->getMeshSortData();
				MTBucketArray<u64>::Bucket result = sort_keys.begin();
				const Transform* LUMIX_RESTRICT entity_data = scene->getUniverse().getTransforms();
				const DVec3 camera_pos = m_camera_params.pos;
				const u64 type_mask = (u64)type << 32;
				
				for(;;) {
					const CullResult* page = iterator.next();
					if(!page) break;
					total += page->header.count;
					const EntityRef* LUMIX_RESTRICT renderables = page->entities;
					switch(type) {
						case RenderableTypes::LOCAL_LIGHT: {
							if(local_light_bucket < 0xff) {
								for (int i = 0, c = page->header.count; i < c; ++i) {
									result.push((u64)local_light_bucket << 56, renderables[i].index | type_mask);
								}
							}
							break;
						}
						case RenderableTypes::GRASS: {
							for (int i = 0, c = page->header.count; i < c; ++i) {
								const EntityRef e = renderables[i];
								Terrain* terrain = scene->getTerrain(e);
								if (!terrain) continue;
								if (terrain->m_grass_quads.empty()) continue;
								const Transform& tr = scene->getUniverse().getTransform(e);
								
								ASSERT(terrain->m_grass_quads[0].size() < 0xffff);
								for (u16 q = 0; q < terrain->m_grass_quads[0].size(); ++q) {
									const Terrain::GrassQuad* quad = terrain->m_grass_quads[0][q];
									const DVec3 quad_pos = tr.transform(DVec3(quad->pos));
									if (!m_camera_params.frustum.intersectsAABB(quad_pos - DVec3(quad->radius), Vec3(2 * quad->radius))) continue;

									ASSERT(quad->m_patches.size() < 0xff);
									for (u8 p = 0; p < quad->m_patches.size(); ++p) {
										Model* model = quad->m_patches[p].m_type->m_grass_model;
										if (!model->isReady()) continue;
										ASSERT(model->getMeshCount() == 1);

										const Mesh& mesh = model->getMesh(0);
										const u8 bucket = bucket_map[mesh.material->getLayer()];
										if (bucket < 0xff) {
											const float squared_length = (float)quad_pos.squaredLength();
											const u32 depth_bits = floatFlip(*(u32*)&squared_length);
											const u64 subrenderable = e.index | type_mask | ((u64)p << 40) | ((u64)q << 48);
											result.push(mesh.material->getSortKey() | ((u64)bucket << 56) | ((u64)depth_bits << 24), subrenderable);
										}
									}
								}
							}
							break;
						}
						case RenderableTypes::DECAL: {
							for (int i = 0, c = page->header.count; i < c; ++i) {
								const EntityRef e = renderables[i];
								const Material* material = scene->getDecalMaterial(e);
								const int layer = material->getLayer();
								const u8 bucket = bucket_map[layer];
								if (bucket < 0xff) {
									// TODO material can have the same sort key as mesh
									const u64 subrenderable = e.index | type_mask;
									result.push(material->getSortKey() | ((u64)bucket << 56), subrenderable);
								}
							}
							break;
						}
						case RenderableTypes::MESH: {
							for (int i = 0, c = page->header.count; i < c; ++i) {
								const EntityRef e = renderables[i];
								const MeshSortData& mesh = mesh_data[e.index];
								const u32 bucket = bucket_map[mesh.layer];
								const u64 subrenderable = e.index | type_mask;
								if (bucket < 0xff) {
									const u64 key = ((u64)mesh.sort_key << 32) | ((u64)bucket << 56);
									result.push(key, subrenderable);
								} else if (bucket < 0xffFF) {
									const DVec3 pos = entity_data[e.index].pos;
									const DVec3 rel_pos = pos - camera_pos;
									const float squared_length = float(rel_pos.x * rel_pos.x + rel_pos.y * rel_pos.y + rel_pos.z * rel_pos.z);
									const u32 depth_bits = floatFlip(*(u32*)&squared_length);
									const u64 key = mesh.sort_key | ((u64)bucket << 56) | ((u64)depth_bits << 24);
									result.push(key, subrenderable);
								}
							}
							break;
						}
						case RenderableTypes::SKINNED:
						case RenderableTypes::MESH_GROUP: {
							for (int i = 0, c = page->header.count; i < c; ++i) {
								const EntityRef e = renderables[i];
								const DVec3 pos = entity_data[e.index].pos;
								const ModelInstance& mi = model_instances[e.index];
								const float squared_length = float((pos - camera_pos).squaredLength());
								const LODMeshIndices lod = mi.model->getLODMeshIndices(squared_length);
								for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
									const Mesh& mesh = mi.meshes[mesh_idx];
									const u32 bucket = bucket_map[mesh.layer];
									const RenderableTypes mesh_type = mesh.type == Mesh::RIGID ? RenderableTypes::MESH_GROUP : RenderableTypes::SKINNED;
									const u64 type_mask = (u64)mesh_type << 32;
									const u64 subrenderable = e.index | type_mask | ((u64)mesh_idx << 40);
									if (bucket < 0xff) {
										const u64 key = ((u64)mesh.sort_key << 32) | ((u64)bucket << 56);
										result.push(key, subrenderable);
									} else if (bucket < 0xffFF) {
										const DVec3 pos = entity_data[e.index].pos;
										const DVec3 rel_pos = pos - camera_pos;
										const float squared_length = float(rel_pos.x * rel_pos.x + rel_pos.y * rel_pos.y + rel_pos.z * rel_pos.z);
										const u32 depth_bits = floatFlip(*(u32*)&squared_length);
										const u64 key = mesh.sort_key | ((u64)bucket << 56) | ((u64)depth_bits << 24);
										result.push(key, subrenderable);
									}
								}
							}
							break;
						}
					}
				}
				result.end();
				Profiler::pushInt("count", total);
			});
		}


		void setup() override
		{
			PROFILE_FUNCTION();
			if(!m_pipeline->m_scene) return;

			const RenderScene* scene = m_pipeline->getScene();

			MTBucketArray<u64>* sort_keys = MTBucketArray<u64>::allocArray(m_allocator);

			const RenderableTypes types[] = {
				RenderableTypes::MESH,
				RenderableTypes::MESH_GROUP,
				RenderableTypes::SKINNED,
				RenderableTypes::DECAL,
				RenderableTypes::GRASS,
				RenderableTypes::LOCAL_LIGHT
			};
			JobSystem::forEach(lengthOf(types), [&](int idx){
				if (m_camera_params.is_shadow && types[idx] == RenderableTypes::GRASS) return;
				CullResult* renderables = scene->getRenderables(m_camera_params.frustum, types[idx]);
				if (renderables) {
					createSortKeys(renderables, types[idx], *sort_keys);
					renderables->free(m_pipeline->m_renderer.getEngine().getPageAllocator());
				}
			});
			sort_keys->merge();

			if (sort_keys->size() > 0) {
				radixSort(sort_keys->key_ptr(), sort_keys->value_ptr(), sort_keys->size());
				createCommands(sort_keys->value_ptr(), sort_keys->key_ptr(), sort_keys->size());
			}

			MTBucketArray<u64>::freeArray(sort_keys);
		}


		void createCommands(CmdPage* first_page
			, const u64* LUMIX_RESTRICT renderables
			, const u64* LUMIX_RESTRICT sort_keys
			, int count)
		{
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

			PROFILE_BLOCK("create cmds");
			const Universe& universe = m_pipeline->m_scene->getUniverse();
			Renderer& renderer = m_pipeline->m_renderer;
			RenderScene* scene = m_pipeline->m_scene;
			const ShiftedFrustum frustum = m_camera_params.frustum;
			const ModelInstance* LUMIX_RESTRICT model_instances = scene->getModelInstances();
			const Transform* LUMIX_RESTRICT entity_data = universe.getTransforms(); 
			const DVec3 camera_pos = m_camera_params.pos;
				
			CmdPage* cmd_page = first_page;
			cmd_page->header.bucket = sort_keys[0] >> 56;
			const bool sort_depth = m_bucket_map[cmd_page->header.bucket] > 0xff;
			u64 instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
			u8* out = cmd_page->data;
			u32 define_mask = m_define_mask[cmd_page->header.bucket];

			u32 instanced_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("INSTANCED"));
			u32 skinned_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("SKINNED"));
			u32 grass_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("GRASS"));

			auto new_page = [&](u8 bucket){
				cmd_page->header.size = int(out - cmd_page->data);
				CmdPage* new_page = new (NewPlaceholder(), m_page_allocator.allocate(true)) CmdPage;
				cmd_page->header.next = new_page;
				cmd_page = new_page;
				new_page->header.bucket = bucket;
				out = cmd_page->data;
				define_mask = m_define_mask[bucket];
				instanced_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("INSTANCED"));
				skinned_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("SKINNED"));
				grass_define_mask = define_mask | (1 << renderer.getShaderDefineIdx("GRASS"));
				const bool sort_depth = m_bucket_map[bucket] > 0xff;
				instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
			};

			for (u32 i = 0, c = count; i < c; ++i) {
				const EntityRef e = {int(renderables[i] & 0xFFffFFff)};
				const RenderableTypes type = RenderableTypes((renderables[i] >> 32) & 0xff);
				const u8 bucket = sort_keys[i] >> 56;
				if(bucket != cmd_page->header.bucket) {
					new_page(bucket);
				}

				switch(type) {
					case RenderableTypes::MESH_GROUP:
					case RenderableTypes::MESH: {
						const u32 mesh_idx = renderables[i] >> 40;
						const ModelInstance* LUMIX_RESTRICT mi = &model_instances[e.index];
						int start_i = i;
						const u64 key = sort_keys[i] & instance_key_mask;
						while (i < c && (sort_keys[i] & instance_key_mask) == key) {
							++i;
						}
						const u16 count = u16(i - start_i);
						const Renderer::TransientSlice slice = renderer.allocTransient(count * sizeof(Vec4) * 2);
						u8* instance_data = slice.ptr;
						for (int j = start_i; j < start_i + count; ++j) {
							const EntityRef e = { int(renderables[j] & 0xFFffFFff) };
							const Transform& tr = entity_data[e.index];
							const Vec3 lpos = (tr.pos - camera_pos).toFloat();
							memcpy(instance_data, &tr.rot, sizeof(tr.rot));
							instance_data += sizeof(tr.rot);
							memcpy(instance_data, &lpos, sizeof(lpos));
							instance_data += sizeof(lpos);
							memcpy(instance_data, &tr.scale, sizeof(tr.scale));
							instance_data += sizeof(tr.scale);
						}
						if ((cmd_page->data + sizeof(cmd_page->data) - out) < 34) {
							new_page(bucket);
						}

						const Mesh& mesh = mi->meshes[mesh_idx];
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
						break;
					}
					case RenderableTypes::SKINNED: {
						const u32 mesh_idx = renderables[i] >> 40;
						const ModelInstance* LUMIX_RESTRICT mi = &model_instances[e.index];
						const Transform& tr = entity_data[e.index];
						const Vec3 rel_pos = (tr.pos - camera_pos).toFloat();
						const Mesh& mesh = mi->meshes[mesh_idx];
						Shader* shader = mesh.material->getShader();
						const gpu::ProgramHandle prog = shader->getProgram(mesh.vertex_decl, skinned_define_mask | mesh.material->getDefineMask());

						if (u32(cmd_page->data + sizeof(cmd_page->data) - out) < (u32)mi->pose->count * sizeof(Matrix) + 57) {
							new_page(bucket);
						}

						WRITE(type);
						WRITE(mesh.render_data);
						WRITE_FN(mesh.material->getRenderData());
						WRITE(prog);
						WRITE(rel_pos);
						WRITE(tr.rot);
						WRITE(tr.scale);
						WRITE(mi->pose->count);

						const Quat* rotations = mi->pose->rotations;
						const Vec3* positions = mi->pose->positions;

						Model& model = *mi->model;
						for (int j = 0, c = mi->pose->count; j < c; ++j) {
							const Model::Bone& bone = model.getBone(j);
							const LocalRigidTransform tmp = {positions[j], rotations[j]};
							const Matrix m = (tmp * bone.inv_bind_transform).toMatrix();
							WRITE(m);
						}
						break;
					}
					case RenderableTypes::DECAL: {
						const Material* material = scene->getDecalMaterial(e);

						int start_i = i;
						const u64 key = sort_keys[i];
						while (i < c && sort_keys[i] == key) {
							++i;
						}
						const u32 count = i - start_i;
						const Renderer::TransientSlice slice = renderer.allocTransient(count * (sizeof(Vec3) * 2 + sizeof(Quat)));
						const gpu::ProgramHandle prog = material->getShader()->getProgram(m_pipeline->m_decal_decl, define_mask | material->getDefineMask());

						if ((cmd_page->data + sizeof(cmd_page->data) - out) < 21) {
							new_page(bucket);
						}
						WRITE(type);
						WRITE_FN(material->getRenderData());
						WRITE(prog);
						WRITE(slice.buffer);
						WRITE(slice.offset);
						WRITE(count);
						u8* mem = slice.ptr;
						for(u32 j = start_i; j < i; ++j) {
							const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
							const Transform& tr = entity_data[e.index];
							const Vec3 lpos = (tr.pos - camera_pos).toFloat();
							memcpy(mem, &lpos, sizeof(lpos));
							mem += sizeof(lpos);
							memcpy(mem, &tr.rot, sizeof(tr.rot));
							mem += sizeof(tr.rot);
							const Vec3 half_extents = scene->getDecalHalfExtents(e);
							memcpy(mem, &half_extents, sizeof(half_extents));
							mem += sizeof(half_extents);
						}
						break;
					}
					case RenderableTypes::LOCAL_LIGHT: {
						const u64 type_bits = (u64)RenderableTypes::LOCAL_LIGHT << 32;
						const u64 type_mask = (u64)0xff << 32;
						int start_i = i;
						while (i < c && (renderables[i] & type_mask) == type_bits) {
							++i;
						}

						const Renderer::TransientSlice slice = renderer.allocTransient((i - start_i) * sizeof(float) * 16);
						struct LightData {
							Quat rot;
							Vec3 pos;
							float range;
							float attenuation;
							Vec3 color;
							Vec3 dir;
							float fov;
						};

						LightData* beg = (LightData*)slice.ptr;
						LightData* end = (LightData*)(slice.ptr + slice.size - sizeof(LightData));

						for (u32 j = start_i; j < i; ++j) {
							const EntityRef e = {int(renderables[j] & 0x00ffFFff)};
							const Transform& tr = entity_data[e.index];
							const Vec3 lpos = (tr.pos - camera_pos).toFloat();
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
					case RenderableTypes::GRASS: {
						const Transform& tr = entity_data[e.index];
						const Vec3 lpos = (tr.pos - camera_pos).toFloat();

						u32 start_i = i;
						const u64 sort_key_mask = 0xffFFffFF;
						const u64 key = sort_keys[i] & sort_key_mask;
						const u64 entity_mask = 0xffFFffFF;
						u32 instance_data_size = 0;
						while (i < c && (sort_keys[i] & sort_key_mask) == key && (renderables[i] & entity_mask) == e.index) {
							const u16 quad_idx = u16(renderables[i] >> 48);
							const u8 patch_idx = u8(renderables[i] >> 40);
							const Terrain* t = scene->getTerrain(e);
							// TODO this crashes if the shader is reloaded
							// TODO 0 const in following:
							const Terrain::GrassPatch& p = t->m_grass_quads[0][quad_idx]->m_patches[patch_idx];
							instance_data_size += p.instance_data.byte_size();
							++i;
						}
						const Renderer::TransientSlice slice = renderer.allocTransient(instance_data_size);

						u32 mem_offset = 0;
						const Mesh* mesh = nullptr;
						float distance = 0;
						for (u32 j = start_i; j < i; ++j) {
							const u16 quad_idx = u16(renderables[j] >> 48);
							const u8 patch_idx = u8(renderables[j] >> 40);
							const Terrain* t = scene->getTerrain(e);
							const Terrain::GrassPatch& p = t->m_grass_quads[0][quad_idx]->m_patches[patch_idx];
							mesh = &p.m_type->m_grass_model->getMesh(0);
							distance = p.m_type->m_distance;
							memcpy(slice.ptr + mem_offset, p.instance_data.begin(), p.instance_data.byte_size());
							mem_offset += p.instance_data.byte_size();
						}
						if ((cmd_page->data + sizeof(cmd_page->data) - out) < 57) {
							new_page(bucket);
						}

						Shader* shader = mesh->material->getShader();
						const gpu::ProgramHandle prg = shader->getProgram(mesh->vertex_decl, grass_define_mask | mesh->material->getDefineMask());

						WRITE(type);
						WRITE(tr.rot);
						WRITE(lpos);
						WRITE(distance);
						WRITE(mesh->render_data);
						WRITE_FN(mesh->material->getRenderData());
						WRITE(prg);
						const u32 instances_count = instance_data_size / sizeof(Terrain::GrassPatch::InstanceData);
						WRITE(instances_count);
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


		void createCommands(const u64* renderables, const u64* sort_keys, int size)
		{
			constexpr i32 STEP = 4096;
			const i32 steps = (size + STEP - 1) / STEP;
			CmdPage* prev = nullptr;
			CmdPage* first = nullptr;
			m_page_allocator.lock();
			for (i32 i = 0; i < steps; ++i) {
				CmdPage* page = new (NewPlaceholder(), m_page_allocator.allocate(false)) CmdPage;
				if (i == 0) first = page;
				if (prev) prev->header.next_init = page;
				prev = page;
			}
			m_page_allocator.unlock();
	
			volatile i32 counter = 0;
			JobSystem::runOnWorkers([&](){
				i32 begin = atomicAdd(&counter, STEP);
				while(begin < size) {
					const i32 s = minimum(STEP, size - begin);
					i32 step_idx = begin / STEP;
					CmdPage* page = first;
					while (step_idx) {
						page = page->header.next_init;
						--step_idx;
					}

					createCommands(page, renderables + begin, sort_keys + begin, s);

					begin = atomicAdd(&counter, STEP);
				}
			});

			CmdPage* page = first;
			CmdPage* next_init = first->header.next_init;
			while(page) {
				m_command_sets[page->header.bucket]->header.next = page;
				m_command_sets[page->header.bucket] = page;
				CmdPage* next = page->header.next;
				page->header.next = nullptr;
				page = next;
				if (!page) {
					page = next_init;
					next_init = page ? page->header.next_init : nullptr;
				}
			}
		}


		void execute() override {}

		IAllocator& m_allocator;
		PageAllocator& m_page_allocator;
		CameraParams m_camera_params;
		PipelineImpl* m_pipeline;
		gpu::TextureHandle m_global_textures[16];
		int m_global_textures_count = 0;
		CmdPage* m_command_sets[255];
		u32 m_bucket_map[255];
		SortOrder m_bucket_sort_order[255] = {};
		u32 m_define_mask[255];
		u8 m_bucket_count;
	};


	void clear(u32 flags, float r, float g, float b, float a, float depth)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::clear(flags, &color.x, depth);
			}
			Vec4 color;
			float depth;
			u32 flags;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->color.set(r, g, b, a);
		cmd->flags = flags;
		cmd->depth = depth;
		m_renderer.queue(cmd, m_profiler_link);
	}


	void viewport(int x, int y, int w, int h)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::viewport(x, y, w, h); 
			}
			int x, y, w, h;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->x = x;
		cmd->y = y;
		cmd->w = w;
		cmd->h = h;

		m_renderer.queue(cmd, m_profiler_link);
	}


	void beginBlock(const char* name)
	{
		struct Cmd : Renderer::RenderJob
		{
			void setup() override {}
			void execute() override 
			{
				PROFILE_FUNCTION();
				gpu::pushDebugGroup(name);
				renderer->beginProfileBlock(name, link);
			}
			StaticString<32> name;
			Renderer* renderer;
			i64 link;
		};
		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->name = name;
		cmd->renderer = &m_renderer;
		m_profiler_link = Profiler::createNewLinkID();
		cmd->link = m_profiler_link;
		m_renderer.queue(cmd, m_profiler_link);
	}


	void endBlock()
	{
		struct Cmd : Renderer::RenderJob
		{
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				renderer->endProfileBlock();
				gpu::popDebugGroup();
			}
			Renderer* renderer;
		};
		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->renderer = &m_renderer;
		m_renderer.queue(cmd, m_profiler_link);
		m_profiler_link = 0;
	}

	
	void setOutput(int rb_index) 
	{
		m_output = rb_index;
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
			logWarning("Renderer") << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
		}
		lua_pop(m_lua_state, 1);
	}
	

	void saveRenderbuffer(int render_buffer, const char* out_path)
	{
		struct Cmd : Renderer::RenderJob {
			Cmd(IAllocator& allocator) : allocator(allocator) {}
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				Array<u32> pixels(allocator);
				pixels.resize(w * h);
				gpu::TextureHandle staging = gpu::allocTextureHandle();
				const u32 flags = u32(gpu::TextureFlags::NO_MIPS) | u32(gpu::TextureFlags::READBACK);
				gpu::createTexture(staging, w, h, 1, gpu::TextureFormat::RGBA8, flags, nullptr, "staging_buffer");
				gpu::copy(staging, handle);
				gpu::readTexture(staging, Span((u8*)pixels.begin(), pixels.byte_size()));
				gpu::destroy(staging);

				OS::OutputFile file;
				if (fs->open(path, Ref(file))) {
					Texture::saveTGA(&file, w, h, gpu::TextureFormat::RGBA8, (u8*)pixels.begin(), false, Path(path), allocator);
					file.close();
				}
				else {
					logError("Renderer") << "Failed to save " << path;
				}
			}

			IAllocator& allocator;
			u32 w, h;
			gpu::TextureHandle handle;
			FileSystem* fs;
			StaticString<MAX_PATH_LENGTH> path;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd)(m_renderer.getAllocator());
		cmd->handle = m_renderbuffers[render_buffer].handle;
		cmd->w = m_viewport.w;
		cmd->h = m_viewport.h;
		cmd->path = out_path;
		cmd->fs = &m_renderer.getEngine().getFileSystem();
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
		REGISTER_FUNCTION(clear);
		REGISTER_FUNCTION(createRenderbuffer);
		REGISTER_FUNCTION(endBlock);
		REGISTER_FUNCTION(environmentCastShadows);
		REGISTER_FUNCTION(executeCustomCommand);
		REGISTER_FUNCTION(preloadShader);
		REGISTER_FUNCTION(render2D);
		REGISTER_FUNCTION(renderDebugShapes);
		REGISTER_FUNCTION(renderLocalLights);
		REGISTER_FUNCTION(renderTextMeshes);
		REGISTER_FUNCTION(saveRenderbuffer);
		REGISTER_FUNCTION(setOutput);
		REGISTER_FUNCTION(viewport);

		registerConst("CLEAR_DEPTH", (u32)gpu::ClearFlags::DEPTH);
		registerConst("CLEAR_COLOR", (u32)gpu::ClearFlags::COLOR);
		registerConst("CLEAR_ALL", (u32)gpu::ClearFlags::COLOR | (u32)gpu::ClearFlags::DEPTH | (u32)gpu::ClearFlags::STENCIL);

		registerConst("STENCIL_ALWAYS", (u32)gpu::StencilFuncs::ALWAYS);
		registerConst("STENCIL_EQUAL", (u32)gpu::StencilFuncs::EQUAL);
		registerConst("STENCIL_NOT_EQUAL", (u32)gpu::StencilFuncs::NOT_EQUAL);
		registerConst("STENCIL_DISABLE", (u32)gpu::StencilFuncs::DISABLE);
		registerConst("STENCIL_KEEP", (u32)gpu::StencilOps::KEEP);
		registerConst("STENCIL_REPLACE", (u32)gpu::StencilOps::REPLACE);

		registerCFunction("bindRenderbuffers", PipelineImpl::bindRenderbuffers);
		registerCFunction("bindTextures", PipelineImpl::bindTextures);
		registerCFunction("drawArray", PipelineImpl::drawArray);
		registerCFunction("prepareShadowcastingLocalLights", PipelineImpl::prepareShadowcastingLocalLights);
		registerCFunction("getCameraParams", PipelineImpl::getCameraParams);
		registerCFunction("getShadowCameraParams", PipelineImpl::getShadowCameraParams);
		registerCFunction("pass", PipelineImpl::pass);
		registerCFunction("prepareCommands", PipelineImpl::prepareCommands);
		registerCFunction("renderEnvProbeVolumes", PipelineImpl::renderEnvProbeVolumes);
		registerCFunction("renderLightProbeGrids", PipelineImpl::renderLightProbeGrids);
		registerCFunction("renderBucket", PipelineImpl::renderBucket);
		registerCFunction("renderParticles", PipelineImpl::renderParticles);
		registerCFunction("renderTerrains", PipelineImpl::renderTerrains);
		registerCFunction("setRenderTargets", PipelineImpl::setRenderTargets);
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
	const Stats& getStats() const override { return m_last_frame_stats; }
	const Path& getPath() override { return m_resource->getPath(); }

	void clearDraw2D() override { return m_draw2d.clear(getAtlasSize()); }
	Draw2D& getDraw2D() override { return m_draw2d; }
	gpu::TextureHandle getOutput() override { 
		if (m_output < 0 || m_output >= m_renderbuffers.size()) return gpu::INVALID_TEXTURE;
		return m_renderbuffers[m_output].handle;
	}

	struct Renderbuffer {
		u32 width;
		u32 height;
		bool use_realtive_size;
		Vec2 relative_size; 
		gpu::TextureFormat format;
		gpu::TextureHandle handle;
		int frame_counter;
	};

	struct ShaderRef {
		Lumix::Shader* res;
		int id;
	};

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
	Stats m_last_frame_stats;
	Stats m_stats; // accessed from render thread
	Viewport m_viewport;
	int m_output;
	Shader* m_debug_shape_shader;
	Shader* m_text_mesh_shader;
	Texture* m_default_cubemap;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<Renderbuffer> m_renderbuffers;
	Array<ShaderRef> m_shaders;
	OS::Timer m_timer;
	gpu::BufferHandle m_global_state_buffer;
	gpu::BufferHandle m_pass_state_buffer;
	gpu::VertexDecl m_base_vertex_decl;
	gpu::VertexDecl m_2D_decl;
	gpu::VertexDecl m_decal_decl;
	gpu::VertexDecl m_3D_pos_decl;
	gpu::VertexDecl m_text_mesh_decl;
	gpu::VertexDecl m_point_light_decl;
	CameraParams m_shadow_camera_params[4];

	gpu::BufferHandle m_cube_vb;
	gpu::BufferHandle m_cube_ib;
	gpu::BufferHandle m_drawcall_ub = gpu::INVALID_BUFFER;
};


Pipeline* Pipeline::create(Renderer& renderer, PipelineResource* resource, const char* define, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, PipelineImpl)(renderer, resource, define, allocator);
}


void Pipeline::destroy(Pipeline* pipeline)
{
	PipelineImpl* p = (PipelineImpl*)pipeline;
	LUMIX_DELETE(p->m_allocator, p);
}


} // namespace Lumix