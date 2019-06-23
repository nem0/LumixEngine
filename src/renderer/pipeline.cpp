#include "ffr/ffr.h"
#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"
#include "engine/os.h"
#include "engine/page_allocator.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "culling_system.h"
#include "font_manager.h"
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
#include <algorithm>
#include <cmath>
#include <malloc.h> // TODO
#include <Windows.h> // TODO

namespace Lumix
{


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
	Vec4 light_direction;
	Vec3 light_color;
	float light_intensity;
	float light_indirect_intensity;
	float time;
	IVec2 framebuffer_size;
};


struct PassState
{
	Matrix projection;
	Matrix inv_projection;
	Matrix view;
	Matrix inv_view;
	Matrix view_projection;
	Matrix inv_view_projection;
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

	MTBucketArray(IAllocator& allocator) 
		: m_counts(allocator) 
	{
		m_keys_mem = (u8*)VirtualAlloc(nullptr, 1024 * 1024 * 8, MEM_RESERVE, PAGE_READWRITE);
		m_values_mem = (u8*)VirtualAlloc(nullptr, 1024 * 1024 * 8, MEM_RESERVE, PAGE_READWRITE);
		m_keys_end = m_keys_mem;
		m_values_end = m_values_mem;
		m_counts.reserve(1024 * 1024 * 8 / BUCKET_SIZE);
	}

	~MTBucketArray()
	{
		VirtualFree(m_values_mem, 0, MEM_RELEASE);
		VirtualFree(m_keys_mem, 0, MEM_RELEASE);
	}

	Bucket begin()
	{
		PROFILE_FUNCTION();
		Bucket b;
		b.array = this;
		
		// TODO get rid of mutex, atomics should be enough
		m_mutex.enter();
		m_counts.emplace();
		b.values = (T*)m_values_end;
		b.keys = (T*)m_keys_end;
		m_keys_end += BUCKET_SIZE;
		m_values_end += BUCKET_SIZE;
		m_mutex.exit();
		// TODO make sure BUCKET_SIZE is multiple of page size
		VirtualAlloc(b.values, BUCKET_SIZE, MEM_COMMIT, PAGE_READWRITE);
		VirtualAlloc(b.keys, BUCKET_SIZE, MEM_COMMIT, PAGE_READWRITE);
		return b;
	}

	void end(const Bucket& bucket)
	{
		const int bucket_idx = int(((u8*)bucket.values - m_values_mem) / BUCKET_SIZE);
		MT::CriticalSectionLock lock(m_mutex);
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

	MT::CriticalSection m_mutex;
	u8* m_keys_mem;
	u8* m_values_mem;
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
		, m_is_first_render(true)
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
		TextureManager& texture_manager = renderer.getTextureManager();
		m_default_cubemap = rm.load<Texture>(Path("textures/common/default_probe.dds"));

		FontAtlas& font_atlas = m_renderer.getFontManager().getFontAtlas();
		m_draw2d.FontTexUvWhitePixel = font_atlas.TexUvWhitePixel;
		m_draw2d.Clear();
		m_draw2d.PushClipRectFullScreen();
		m_draw2d.PushTextureID(font_atlas.TexID);

		m_position_uniform = ffr::allocUniform("u_position", ffr::UniformType::VEC3, 1);
		m_lod_uniform = ffr::allocUniform("u_lod", ffr::UniformType::INT, 1);
		m_position_radius_uniform = ffr::allocUniform("u_pos_radius", ffr::UniformType::VEC4, 1);
		m_terrain_params_uniform = ffr::allocUniform("u_terrain_params", ffr::UniformType::VEC4, 1);
		m_rel_camera_pos_uniform = ffr::allocUniform("u_rel_camera_pos", ffr::UniformType::VEC3, 1);
		m_terrain_scale_uniform = ffr::allocUniform("u_terrain_scale", ffr::UniformType::VEC3, 1);
		m_terrain_matrix_uniform = ffr::allocUniform("u_terrain_matrix", ffr::UniformType::MAT4, 1);
		m_model_uniform = ffr::allocUniform("u_model", ffr::UniformType::MAT4, 1);
		m_bones_uniform = ffr::allocUniform("u_bones", ffr::UniformType::MAT4, 196);
		m_canvas_size_uniform = ffr::allocUniform("u_canvas_size", ffr::UniformType::VEC2, 1);
		m_texture_uniform = ffr::allocUniform("u_texture", ffr::UniformType::INT, 1);
		m_irradiance_map_uniform = ffr::allocUniform("u_irradiancemap", ffr::UniformType::INT, 1);
		m_radiance_map_uniform = ffr::allocUniform("u_radiancemap", ffr::UniformType::INT, 1);
		m_material_params_uniform = ffr::allocUniform("u_material_params", ffr::UniformType::VEC4, 1);
		m_material_color_uniform = ffr::allocUniform("u_material_color", ffr::UniformType::VEC4, 1);

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
		m_cube_vb = m_renderer.createBuffer(vb_mem);

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
		m_cube_ib = m_renderer.createBuffer(ib_mem);

		m_resource->onLoaded<PipelineImpl, &PipelineImpl::onStateChanged>(this);

		GlobalState global_state;
		const Renderer::MemRef global_state_mem = m_renderer.copy(&global_state, sizeof(global_state));
		m_global_state_buffer = m_renderer.createBuffer(global_state_mem);
		
		PassState pass_state;
		const Renderer::MemRef pass_state_mem = m_renderer.copy(&pass_state, sizeof(pass_state));
		m_pass_state_buffer = m_renderer.createBuffer(pass_state_mem);
	}



	~PipelineImpl()
	{
		m_draw2d_shader->getResourceManager().unload(*m_draw2d_shader);
		m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
		m_text_mesh_shader->getResourceManager().unload(*m_text_mesh_shader);
		m_default_cubemap->getResourceManager().unload(*m_default_cubemap);

		for(ShaderRef& shader : m_shaders) {
			shader.res->getResourceManager().unload(*shader.res);
		}
		if (m_resource) m_resource->getResourceManager().unload(*m_resource);
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


	void clearBuffers()
	{
		PROFILE_FUNCTION();
		for (Renderbuffer& rb : m_renderbuffers) {
			++rb.frame_counter;
		}

		for(int i = m_renderbuffers.size() - 1; i >= 0; --i) {
			if (m_renderbuffers[i].frame_counter > 1) {
				m_renderer.destroy(m_renderbuffers[i].handle);
				m_renderbuffers.eraseFast(i);
			}
		}
	}


	virtual void setViewport(const Viewport& viewport) override 
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
			projection_matrix.setOrtho(-bb_size, bb_size, -bb_size, bb_size, SHADOW_CAM_NEAR, SHADOW_CAM_FAR, ffr::isHomogenousDepth(), true);
			shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
			Matrix view_matrix;
			view_matrix.lookAt(shadow_cam_pos, shadow_cam_pos + light_forward, light_mtx.getYVector());

			const float ymul = ffr::isOriginBottomLeft() ? 0.5f : -0.5f;
			const Matrix bias_matrix(
				0.5, 0.0, 0.0, 0.0,
				0.0, ymul, 0.0, 0.0,
				0.0, 0.0, 1.0, 0.0,
				0.5, 0.5, 0.0, 1.0);

			global_state.shadowmap_matrices[slice] = bias_matrix * projection_matrix * view_matrix;

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

			// TODO
			//findExtraShadowcasterPlanes(light_forward, camera_frustum, &cp.frustum);
		}
	}


	bool render(bool only_2d) override 
	{ 
		PROFILE_FUNCTION();

		if (!isReady() || !m_scene || m_viewport.w <= 0 || m_viewport.h <= 0) {
			m_is_first_render = true;
			return false;
		}

		if (m_is_first_render) {
			// m_draw2d might accumulate too much data to render while pipeline was not ready
			// so we clear it on the first frame
			m_is_first_render = false;
			m_draw2d.Clear();
		}

		m_stats = {};
		clearBuffers();

		{
			PROFILE_BLOCK("destroy renderbuffers");
			for (int i = m_renderbuffers.size() - 1; i >= 0; --i) {
				Renderbuffer& rb = m_renderbuffers[i];
				if (!rb.use_realtive_size) continue;
				const uint w = uint(rb.relative_size.x * m_viewport.w + 0.5f);
				const uint h = uint(rb.relative_size.y * m_viewport.h + 0.5f);
				if (rb.width != w || rb.height != h) {
					m_renderer.destroy(rb.handle);
					m_renderbuffers.eraseFast(i);
				}
			}
		}

		const Matrix view = m_viewport.getViewRotation();
		const Matrix projection = m_viewport.getProjection(ffr::isHomogenousDepth());
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

		const EntityPtr global_light = m_scene->getActiveEnvironment();
		if(global_light.isValid()) {
			EntityRef gl = (EntityRef)global_light;
			global_state.light_direction = Vec4(m_scene->getUniverse().getRotation(gl).rotate(Vec3(0, 0, -1)), 456); 
			global_state.light_color = m_scene->getEnvironment(gl).m_diffuse_color;
			global_state.light_intensity = m_scene->getEnvironment(gl).m_diffuse_intensity;
			global_state.light_indirect_intensity = m_scene->getEnvironment(gl).m_indirect_intensity;
		}

		if (!only_2d) {
			prepareShadowCameras(global_state);
		}

		struct StartFrameCmd : Renderer::RenderJob {
			void execute() override {
				PROFILE_FUNCTION();
				ffr::update(global_state_buffer, &global_state, 0, sizeof(global_state));
				ffr::bindUniformBuffer(0, global_state_buffer, 0, sizeof(GlobalState));
				ffr::bindUniformBuffer(1, pass_state_buffer, 0, sizeof(PassState));
			}
			void setup() override {}

			ffr::BufferHandle global_state_buffer;
			ffr::BufferHandle pass_state_buffer;
			GlobalState global_state;
			PassState pass_state;
		};

		StartFrameCmd* start_frame_cmd = LUMIX_NEW(m_renderer.getAllocator(), StartFrameCmd);
		start_frame_cmd->global_state = global_state;
		start_frame_cmd->global_state_buffer = m_global_state_buffer;
		start_frame_cmd->pass_state_buffer = m_pass_state_buffer;
		m_renderer.push(start_frame_cmd, 0);
		
		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "main");
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			return false;
		}
		{
			PROFILE_BLOCK("lua pipeline main");
			LuaWrapper::pcall(m_lua_state, 0);
		}
		lua_pop(m_lua_state, 1);
		return true;
	}


	void renderDebugLines()
	{
		struct Cmd : Renderer::RenderJob
		{
			Cmd(IAllocator& allocator) : lines(allocator) {}

			void setup() override
			{
				PROFILE_FUNCTION();
				const Array<DebugLine>& src_lines = pipeline->m_scene->getDebugLines();
				lines.resize(src_lines.size());
				copyMemory(&lines[0], &src_lines[0], lines.size() * sizeof(lines[0]));
				render_data = pipeline->m_debug_shape_shader->m_render_data;
			}


			void execute() override {
				PROFILE_FUNCTION();
				const Shader::Program& shader = Shader::getProgram(render_data, 0);
				if(!shader.handle.isValid()) return;

				ffr::pushDebugGroup("debug lines");
				struct BaseVertex {
					Vec3 pos;
					u32 color;
				};

				Array<BaseVertex> vertices(pipeline->m_allocator);
				vertices.resize(lines.size() * 2);
				for (int j = 0, n = lines.size(); j < n; ++j) {
					const DebugLine& line = lines[j];

					vertices[j * 2].color = line.color;
					vertices[j * 2].pos = (line.from - viewport_pos).toFloat();

					vertices[j * 2 + 1].color = line.color;
					vertices[j * 2 + 1].pos = (line.to - viewport_pos).toFloat();
				}

				ffr::VertexDecl vertex_decl;
				vertex_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
				vertex_decl.addAttribute(4, ffr::AttributeType::U8, true, false);

				ffr::setUniformMatrix4f(pipeline->m_model_uniform, &Matrix::IDENTITY.m11);

				ffr::setState(0);
				ffr::useProgram(shader.handle);

				const Renderer::TransientSlice vb = pipeline->m_renderer.allocTransient(vertices.byte_size());
				
				memcpy(vb.ptr, vertices.begin(), vb.size);
				ffr::flushBuffer(vb.buffer, vb.offset, vb.size);
				
				ffr::setVertexBuffer(&vertex_decl, vb.buffer, vb.offset, nullptr);
				ffr::setIndexBuffer(ffr::INVALID_BUFFER);
				ffr::drawArrays(0, lines.size() * 2, ffr::PrimitiveType::LINES);
				ffr::popDebugGroup();
			}

			Array<DebugLine> lines;
			PipelineImpl* pipeline;
			DVec3 viewport_pos;
			ShaderRenderData* render_data;
		};


		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_shape_shader->isReady()) return;

		IAllocator& allocator = m_renderer.getAllocator();
		Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
		cmd->pipeline = this;
		cmd->viewport_pos = m_viewport.pos;
		m_renderer.push(cmd, m_profiler_link);
	}


	void renderDebugShapes()
	{
		renderDebugLines();
		/*renderDebugTriangles();
		renderDebugPoints();*/
	}


	void render2D()
	{
		auto resetDraw2D =  [this](){
			m_draw2d.Clear();
			m_draw2d.PushClipRectFullScreen();
			FontAtlas& atlas = m_renderer.getFontManager().getFontAtlas();
			m_draw2d.FontTexUvWhitePixel = atlas.TexUvWhitePixel;
			m_draw2d.PushTextureID(atlas.TexID);
		};

		if (!m_draw2d_shader->isReady()) {
			resetDraw2D();
			return;
		}

		if (m_draw2d.IdxBuffer.size() == 0) {
			resetDraw2D();
			return;
		}

		struct Cmd : Renderer::RenderJob
		{
			Renderer::MemRef idx_buffer_mem;
			Renderer::MemRef vtx_buffer_mem;
			int num_indices;
			int num_vertices;
			Array<Draw2D::DrawCmd> cmd_buffer;

			Cmd(IAllocator& allocator) : cmd_buffer(allocator) {}

			void setup()
			{
				PROFILE_FUNCTION();
				size.set((float)pipeline->m_viewport.w, (float)pipeline->m_viewport.h);

				Draw2D& draw2d = pipeline->m_draw2d;

				num_indices = draw2d.IdxBuffer.size();
				num_vertices = draw2d.VtxBuffer.size();

				idx_buffer_mem = pipeline->m_renderer.copy(&draw2d.IdxBuffer[0], num_indices * sizeof(ImDrawIdx));
				vtx_buffer_mem = pipeline->m_renderer.copy(&draw2d.VtxBuffer[0], num_vertices * sizeof(ImDrawVert));
				cmd_buffer.resize(draw2d.CmdBuffer.size());
				copyMemory(&cmd_buffer[0], draw2d.CmdBuffer.begin(), sizeof(cmd_buffer[0]) * cmd_buffer.size());

				draw2d.Clear();
				draw2d.PushClipRectFullScreen();
				FontAtlas& atlas = pipeline->m_renderer.getFontManager().getFontAtlas();
				draw2d.FontTexUvWhitePixel = atlas.TexUvWhitePixel;
				draw2d.PushTextureID(atlas.TexID);

				shader = pipeline->m_draw2d_shader->m_render_data;
			}

			void execute()
			{
				PROFILE_FUNCTION();
				ffr::VertexDecl vertex_decl;
				vertex_decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
				vertex_decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
				vertex_decl.addAttribute(4, ffr::AttributeType::U8, true, false);
				
				ffr::BufferHandle vb = ffr::allocBufferHandle();
				ffr::BufferHandle ib = ffr::allocBufferHandle();
				ffr::createBuffer(vb, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, vtx_buffer_mem.size, vtx_buffer_mem.data);
				ffr::createBuffer(ib, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, idx_buffer_mem.size, idx_buffer_mem.data);
				pipeline->m_renderer.free(idx_buffer_mem);
				pipeline->m_renderer.free(vtx_buffer_mem);

				ffr::pushDebugGroup("draw2d");
				ffr::ProgramHandle prg = Shader::getProgram(shader, 0).handle;
				if(prg.isValid()) {

					ffr::setUniform2f(pipeline->m_canvas_size_uniform, &size.x);
					ffr::setVertexBuffer(&vertex_decl, vb, 0, nullptr);
					ffr::setIndexBuffer(ib);

					u32 elem_offset = 0;
					const Draw2D::DrawCmd* pcmd_begin = cmd_buffer.begin();
					const Draw2D::DrawCmd* pcmd_end = cmd_buffer.end();
					const u64 blend_state = ffr::getBlendStateBits(ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA, ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA);
					ffr::setState(blend_state);
					ffr::setUniform1i(pipeline->m_texture_uniform, 0);
					ffr::useProgram(prg);

					ASSERT(pcmd_begin <= pcmd_end - 1); // TODO compute correct offsets
					for (const Draw2D::DrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++) {
						if (0 == pcmd->ElemCount) continue;
			
						ffr::scissor(uint(maximum(pcmd->ClipRect.x, 0.0f)),
							uint(maximum(pcmd->ClipRect.y, 0.0f)),
							uint(minimum(pcmd->ClipRect.z, 65535.0f) - maximum(pcmd->ClipRect.x, 0.0f)),
							uint(minimum(pcmd->ClipRect.w, 65535.0f) - maximum(pcmd->ClipRect.y, 0.0f)));
			
						const Texture* atlas_texture = pipeline->m_renderer.getFontManager().getAtlasTexture();
						ffr::TextureHandle texture_id = atlas_texture->handle;
						if (pcmd->TextureId) texture_id = *(ffr::TextureHandle*)pcmd->TextureId;
						if(!texture_id.isValid()) texture_id = atlas_texture->handle;

						ffr::bindTextures(&texture_id, 0, 1);

						ffr::drawTriangles(num_indices);

						elem_offset += pcmd->ElemCount;
					}
				}

				ffr::popDebugGroup();
				ffr::destroy(vb);
				ffr::destroy(ib);
			}

			Vec2 size;
			PipelineImpl* pipeline;
			ShaderRenderData* shader;
		};

		IAllocator& allocator = m_renderer.getAllocator();
		Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
		cmd->pipeline = this;
		m_renderer.push(cmd, m_profiler_link);
	}

	void setScene(RenderScene* scene) override
	{
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

	
	static ffr::TextureFormat getFormat(const char* name)
	{
		static const struct
		{
			const char* name;
			ffr::TextureFormat value;
		} FORMATS[] = {
			{"depth32", ffr::TextureFormat::D32},
			{"depth24", ffr::TextureFormat::D24},
			{"depth24stencil8", ffr::TextureFormat::D24S8},
			{"rgba8", ffr::TextureFormat::RGBA8},
			{"srgba", ffr::TextureFormat::SRGBA},
			{"srgb", ffr::TextureFormat::SRGB},
			{"rgba16", ffr::TextureFormat::RGBA16},
			{"rgba16f", ffr::TextureFormat::RGBA16F},
			{"r16f", ffr::TextureFormat::R16F},
			{"r16", ffr::TextureFormat::R16},
			{"r32f", ffr::TextureFormat::R32F},
		};

		for (auto& i : FORMATS)
		{
			if (equalStrings(i.name, name)) return i.value;
		}
		logError("Renderer") << "Uknown texture format " << name;
		return ffr::TextureFormat::RGBA8;
	}


	int createRenderbuffer(float w, float h, bool relative, const char* format_str, const char* debug_name)
	{
		PROFILE_FUNCTION();
		const uint rb_w = uint(relative ? w * m_viewport.w + 0.5f : w);
		const uint rb_h = uint(relative ? h * m_viewport.h + 0.5f : h);
		const ffr::TextureFormat format = getFormat(format_str);

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
		rb.handle = m_renderer.createTexture(rb_w, rb_h, 1, format, 0, {0, 0}, debug_name);

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
		
		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		RenderTerrainsCommand* cmd = LUMIX_NEW(allocator, RenderTerrainsCommand)(allocator);

		if (lua_gettop(L) > 1 && lua_istable(L, 2)) {
			lua_pushnil(L);
			while (lua_next(L, 2) != 0) {
				if(lua_type(L, -1) != LUA_TNUMBER) {
					logError("Renderer") << "Incorrect global textures arguments of renderTerrains";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}

				if(lua_type(L, -2) != LUA_TSTRING) {
					logError("Renderer") << "Incorrect global textures arguments of renderTerrains";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}
			
				if (cmd->m_global_textures_count > lengthOf(cmd->m_global_textures)) {
					logError("Renderer") << "Too many textures in renderTerrains call";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}

				const char* uniform = lua_tostring(L, -2);
				const int rb_idx = (int)lua_tointeger(L, -1);
				auto& t = cmd->m_global_textures[cmd->m_global_textures_count]; 
				t.texture = pipeline->m_renderbuffers[rb_idx].handle;
				t.uniform = ffr::allocUniform(uniform, ffr::UniformType::INT, 1);
				++cmd->m_global_textures_count;

				lua_pop(L, 1);
			}
		}

		if (lua_gettop(L) > 2 && lua_istable(L, 3)) {
			cmd->m_render_state = getState(L, 3);
		}

		cmd->m_pipeline = pipeline;
		cmd->m_camera_params = cp;

		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);
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
			Cmd(IAllocator& allocator) 
				: m_data(allocator) 
			{}

			void setup() override
			{
				PROFILE_FUNCTION();
				const auto& emitters = m_pipeline->m_scene->getParticleEmitters();

				int byte_size = 0;
				for (ParticleEmitter* emitter : emitters) {
					byte_size += emitter->getInstanceDataSizeBytes();
				}

				m_data.reserve(sizeof(int) * emitters.size() + byte_size);

				for (ParticleEmitter* emitter : emitters) {
					if (!emitter->getResource() || !emitter->getResource()->isReady()) continue;
					
					const int size = emitter->getInstanceDataSizeBytes();
					if (size == 0) continue;

					const Material* material = emitter->getResource()->getMaterial();
					m_data.write(material->getShader()->m_render_data);
					m_data.write(size);
					m_data.write(emitter->getInstancesCount());
					float* instance_data = (float*)m_data.skip(size);
					emitter->fillInstanceData(m_camera_params.pos, instance_data);
				}
			}

			void execute() override
			{
				PROFILE_FUNCTION();
				ffr::pushDebugGroup("particles");
				InputMemoryStream blob(m_data);
				ffr::VertexDecl instance_decl;
				instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
				const u64 blend_state = ffr::getBlendStateBits(ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA, ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA);
				ffr::setState(blend_state);
				while(blob.getPosition() < blob.size()) {
					ShaderRenderData* shader_data = blob.read<ShaderRenderData*>();
					const int byte_size = blob.read<int>();
					const int instances_count = blob.read<int>();
					const Renderer::TransientSlice transient = m_pipeline->m_renderer.allocTransient(byte_size);
					if ((int)transient.size < byte_size) {
						logWarning("Renderer") << "Not enough memory reserved to render all particles.";
						break;
					}

					const void* mem = blob.skip(byte_size);
					memcpy(transient.ptr, mem, byte_size);
					ffr::flushBuffer(transient.buffer, transient.offset, transient.size);

					const Shader::Program& prog = Shader::getProgram(shader_data, 0);
					if (prog.handle.isValid()) {
						ffr::useProgram(prog.handle);
						ffr::setInstanceBuffer(instance_decl, transient.buffer, transient.offset, 0, nullptr);
						ffr::drawTriangleStripArraysInstanced(0, 4, instances_count);
					}
				}
				ffr::popDebugGroup();
			}

			OutputMemoryStream m_data;
			PipelineImpl* m_pipeline;
			CameraParams m_camera_params;
		};

		Cmd* cmd = LUMIX_NEW(pipeline->m_allocator, Cmd)(pipeline->m_allocator);
		cmd->m_pipeline = pipeline;
		cmd->m_camera_params = cp;

		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);

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
		float* points = cp.frustum.xs;
		if(!LuaWrapper::checkField(L, -1, "origin", &cp.frustum.origin)) {
				lua_pop(L, 1);
				luaL_error(L, "Frustum without origin");
		}
		for (int i = 0; i < 32 + 24; ++i) {
			lua_rawgeti(L, -1, i + 1);
			if(!LuaWrapper::isType<float>(L, -1)) {
				lua_pop(L, 2);
				luaL_error(L, "Frustum must contain exactly 24 floats");
			}
			points[i] = LuaWrapper::toType<float>(L, -1);
			lua_pop(L, 1);
		}
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
				ffr::bindTextures(m_textures_handles, m_offset, m_textures_count);
				for(int i = 0; i < m_textures_count; ++i) {
					ffr::setUniform1i(m_textures_uniforms[i], i + m_offset);
				}
			}

			ffr::TextureHandle m_textures_handles[16];
			ffr::UniformHandle m_textures_uniforms[16];
			int m_offset = 0;
			int m_textures_count = 0;
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
		
		
		const int len = (int)lua_objlen(L, 4);
		for(int i = 0; i < len; ++i) {
			lua_rawgeti(L, 4, i + 1);
			if(lua_type(L, -1) != LUA_TNUMBER) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Incorrect texture arguments of bindTextures");
			}

			if (cmd->m_textures_count > lengthOf(cmd->m_textures_handles)) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Too many texture in bindTextures call");
			}

			const int rb_idx = (int)lua_tointeger(L, -1);
			cmd->m_textures_handles[cmd->m_textures_count] = pipeline->m_renderbuffers[rb_idx].handle;
			++cmd->m_textures_count;

			lua_pop(L, 1);

		}

		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);

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
		const Shader* shader = [&] {
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
		
		struct Cmd : public Renderer::RenderJob
		{
			struct Probe
			{
				Vec3 pos;
				ffr::TextureHandle texture;
			};

			Cmd(IAllocator& allocator) : m_probes(allocator) {}

			void setup() override
			{
				m_pipeline->getScene()->getEnvironmentProbes(m_probes);
			}


			void execute() override
			{
				PROFILE_FUNCTION();
				if(m_probes.empty()) return;

				ffr::pushDebugGroup("environment");
				const Shader::Program& prog = Shader::getProgram(m_shader, 0);
				if(!prog.handle.isValid()) return;

				const int pos_radius_uniform_loc = ffr::getUniformLocation(prog.handle, m_pos_radius_uniform);

				ffr::VertexDecl decl;
				decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);

				ffr::setVertexBuffer(&decl, m_vb, 0, nullptr);
				ffr::setIndexBuffer(m_ib);
				ffr::useProgram(prog.handle);
				const u64 blend_state = ffr::getBlendStateBits(ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA, ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA);
				const DVec3 cam_pos = m_camera_params.pos;
				for (const EnvProbeInfo& probe : m_probes) {
					const Vec4 pos_radius((probe.position - cam_pos).toFloat(), probe.radius);
					ffr::TextureHandle handles[2] = { probe.radiance, probe.irradiance };
					ffr::bindTextures(handles, 14, 2);
					ffr::applyUniform4f(pos_radius_uniform_loc, &pos_radius.x);
					
					const bool intersecting = m_camera_params.frustum.intersectNearPlane(probe.position, probe.radius);
					const u64 state = intersecting
						? (u64)ffr::StateFlags::CULL_FRONT
						: (u64)ffr::StateFlags::DEPTH_TEST | (u64)ffr::StateFlags::CULL_BACK;
					ffr::setState(state | blend_state);
					ffr::drawTriangles(36);
				}
				ffr::popDebugGroup();
			}

			ffr::BufferHandle m_ib;
			ffr::BufferHandle m_vb;
			ffr::UniformHandle m_pos_radius_uniform;
			ffr::UniformHandle m_irradiance_map_uniform;
			ffr::UniformHandle m_radiance_map_uniform;
			CameraParams m_camera_params;
			PipelineImpl* m_pipeline;
			Array<EnvProbeInfo> m_probes;
			ShaderRenderData* m_shader;
		};

		if(shader->isReady()) {
			IAllocator& allocator = pipeline->m_renderer.getAllocator();
			Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
			cmd->m_pipeline = pipeline;
			cmd->m_shader = shader->m_render_data;
			cmd->m_ib = pipeline->m_cube_ib;
			cmd->m_vb = pipeline->m_cube_vb;
			cmd->m_camera_params = cp;
			cmd->m_irradiance_map_uniform = pipeline->m_irradiance_map_uniform;
			cmd->m_radiance_map_uniform = pipeline->m_radiance_map_uniform;
			cmd->m_pos_radius_uniform = pipeline->m_position_radius_uniform;
			pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);
		}
		return 0;
	}


	static int pass(lua_State* L)
	{
		PROFILE_FUNCTION();
		struct PushPassStateCmd : Renderer::RenderJob {
			void execute() override {
				PROFILE_FUNCTION();
				ffr::update(pass_state_buffer, &pass_state, 0, sizeof(pass_state));
				ffr::bindUniformBuffer(1, pass_state_buffer, 0, sizeof(PassState));
			}
			void setup() override {}

			ffr::BufferHandle pass_state_buffer;
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
		
		cmd->pass_state_buffer = pipeline->m_pass_state_buffer;
		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);
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
			if (LuaWrapper::getOptionalStringField(L, -1, "sort", tmp, lengthOf(tmp))) {
				order = equalIStrings(tmp, "depth") 
					? PrepareCommandsRenderJob::SortOrder::DEPTH
					: PrepareCommandsRenderJob::SortOrder::DEFAULT;
			}

			cmd->m_bucket_sort_order[i] = order;

			lua_getfield(L, -1, "layers");
			const bool ok = LuaWrapper::forEachArrayItem<const char*>(L, -1, nullptr, [&](const char* layer_name){
				const int layer = pipeline->m_renderer.getLayerIdx(layer_name);
				cmd->m_bucket_map[layer] = i | (order == PrepareCommandsRenderJob::SortOrder::DEPTH ? 256 : 0);
			});
			lua_pop(L, 1);

			if(!ok) {
				LUMIX_DELETE(allocator, cmd);
				return luaL_argerror(L, 2, "'layers' must be array of strings");
			}
			
			lua_pop(L, 1);
		}

		for(int i = 0; i < cmd->m_bucket_count; ++i) {
			CmdPage* page = new (NewPlaceholder(), page_allocator.allocate(true)) CmdPage;
			cmd->m_command_sets[i] = page;
			LuaWrapper::push(L, page);
		}
		cmd->m_camera_params = cp;
		cmd->m_pipeline = pipeline;
		const int num_cmd_sets = cmd->m_bucket_count;
		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);

		return num_cmd_sets;
	}


	static int drawArray(lua_State* L)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override { m_render_data = m_shader->isReady() ? m_shader->m_render_data : nullptr; }
			void execute() override 
			{
				PROFILE_FUNCTION();
				if (!m_render_data) return;

				ffr::ProgramHandle prg = Shader::getProgram(m_render_data, m_define_mask).handle;
				if(!prg.isValid()) return;

				ffr::setState(m_render_state);

				ffr::bindTextures(m_textures_handles, 0, m_textures_count);

				for(int i = 0; i < m_uniforms_count; ++i) {
					ffr::setUniform4f(m_uniforms[i].handle, &m_uniforms[i].value.x);
				}

				ffr::setVertexBuffer(nullptr, ffr::INVALID_BUFFER, 0, nullptr);
				ffr::useProgram(prg);
				ffr::setIndexBuffer(ffr::INVALID_BUFFER);
				ffr::drawArrays(m_indices_offset, m_indices_count, ffr::PrimitiveType::TRIANGLE_STRIP);
			}

			ffr::TextureHandle m_textures_handles[16];
			int m_textures_count = 0;
			struct {
				Vec4 value;
				ffr::UniformHandle handle;
			} m_uniforms[16];
			int m_uniforms_count = 0;
			Shader* m_shader;
			int m_indices_count;
			int m_indices_offset;
			u32 m_define_mask = 0;
			u64 m_render_state;
			ShaderRenderData* m_render_data = nullptr;

		};

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
			return (u64)ffr::StateFlags::DEPTH_WRITE | (u64)ffr::StateFlags::DEPTH_TEST;
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
			const int len = (int)lua_objlen(L, 4);
			for(int i = 0; i < len; ++i) {
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
				lua_pushnil(L);
				while (lua_next(L, 5) != 0) {
					if(lua_type(L, -1) != LUA_TTABLE) {
						LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
						return luaL_error(L, "%s", "Incorrect uniform arguments of drawArrays");
					}

					if(lua_type(L, -2) != LUA_TSTRING) {
						LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
						return luaL_error(L, "%s", "Incorrect uniform arguments of drawArrays");
					}

					const char* uniform_name = lua_tostring(L, -2);
					cmd->m_uniforms[cmd->m_uniforms_count].handle = ffr::allocUniform(uniform_name, ffr::UniformType::VEC4, 1);
					float* value = &cmd->m_uniforms[cmd->m_uniforms_count].value.x;
					for(int i = 0; i < 4; ++i) {
						lua_rawgeti(L, -1, 1 + i);
						if (lua_type(L, -1) != LUA_TNUMBER) {
							LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
							return luaL_error(L, "%s", "Incorrect uniform arguments of drawArrays. Uniforms can only be Vec4.");
						}
						value[i] = (float)lua_tonumber(L, -1);
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
	
		cmd->m_render_state = rs;
		cmd->m_shader = shader;
		cmd->m_indices_count = indices_count;
		cmd->m_indices_offset = indices_offset;
		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);

		return 0;
	}
	

	static void pushCameraParams(lua_State* L, const CameraParams& params)
	{
		lua_createtable(L, 0, 4);

		lua_createtable(L, 32+24, 0);
		const float* frustum = params.frustum.xs; 
		for(int i = 0; i < 32+24; ++i) {
			LuaWrapper::push(L, frustum[i]);
			lua_rawseti(L, -2, i + 1);
		}
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
			pipeline->m_renderer.push(cmd);
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
		cp.projection = pipeline->m_viewport.getProjection(ffr::isHomogenousDepth());
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
			RenderJob(IAllocator& allocator) : m_vertices(allocator) {}


			void setup() override
			{
				PROFILE_FUNCTION();
				const Quat& rot = m_pipeline->m_viewport.rot;
				const DVec3& pos = m_pipeline->m_viewport.pos;
				m_pipeline->m_scene->getTextMeshesVertices(m_vertices, pos, rot);
				Renderer& renderer = m_pipeline->m_renderer;
				Texture* atlas = renderer.getFontManager().getAtlasTexture();
				m_atlas = atlas ? atlas->handle : ffr::INVALID_TEXTURE;
			}


			void execute() override
			{
				PROFILE_FUNCTION();
				const Shader::Program& p = Shader::getProgram(m_shader, 0);
				if(!p.handle.isValid()) return;
				if (m_vertices.empty()) return;

				Renderer& renderer = m_pipeline->m_renderer;
				const Renderer::TransientSlice transient = renderer.allocTransient(m_vertices.byte_size());
				memcpy(transient.ptr, m_vertices.begin(), transient.size);
				ffr::flushBuffer(transient.buffer, transient.offset, transient.size);
				ffr::useProgram(p.handle);
				ffr::VertexDecl decl;
				const u64 blend_state = ffr::getBlendStateBits(ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA, ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA);
				ffr::setState((u64)ffr::StateFlags::DEPTH_WRITE | (u64)ffr::StateFlags::DEPTH_TEST | blend_state);
				ffr::bindTextures(&m_atlas, 0, 1);
				decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
				decl.addAttribute(4, ffr::AttributeType::U8, true, false);
				decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
				ffr::setVertexBuffer(&decl, transient.buffer, transient.offset, nullptr);
				ffr::drawArrays(0, m_vertices.size(), ffr::PrimitiveType::TRIANGLES);
			}


			ffr::TextureHandle m_atlas;
			ShaderRenderData* m_shader;
			PipelineImpl* m_pipeline;
			Array<TextMeshVertex> m_vertices;
		};


		IAllocator& allocator = m_renderer.getAllocator();
		RenderJob* job = LUMIX_NEW(allocator, RenderJob)(allocator);
		job->m_pipeline = this;
		job->m_shader = m_text_mesh_shader->m_render_data;
		m_renderer.push(job, m_profiler_link);
	}


	void renderLocalLights(const char* define, int shader_idx, CmdPage* cmds)
	{
		/*struct RenderJob : Renderer::RenderJob
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
				if(m_cmd_set->cmds.empty()) return;
				
				ffr::VertexDecl instance_decl;
				instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false); // rot
				instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false); // pos
				instance_decl.addAttribute(1, ffr::AttributeType::FLOAT, false, false); // radius
				instance_decl.addAttribute(1, ffr::AttributeType::FLOAT, false, false); // attn
				instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false); // color
				instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false); // dir
				instance_decl.addAttribute(1, ffr::AttributeType::FLOAT, false, false); // fov

				ffr::VertexDecl decl;
				decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);

				const u64 blend_state = ffr::getBlendStateBits(ffr::BlendFactors::ONE, ffr::BlendFactors::ONE, ffr::BlendFactors::ONE, ffr::BlendFactors::ONE);
				const u32 define_mask = m_define_mask;
				for (const Array<u8>& cmds : m_cmd_set->cmds) {
					const u8* cmd = cmds.begin();
					const u8* cmd_end = cmds.end();
					while (cmd != cmd_end) {
						const RenderableTypes type = *(RenderableTypes*)cmd;
						cmd += sizeof(type);
						switch(type) {
							case RenderableTypes::LOCAL_LIGHT: {
								READ(u16, instances_count);
								READ(bool, intersecting);

								const u8* instance_data = cmd;
								cmd += instance_decl.size * instances_count;

								const Shader::Program& prog = Shader::getProgram(m_shader, define_mask);
								if (prog.handle.isValid()) {
									ffr::useProgram(prog.handle);
									const u64 state = intersecting
										? (u64)ffr::StateFlags::CULL_FRONT
										: (u64)ffr::StateFlags::DEPTH_TEST | (u64)ffr::StateFlags::CULL_BACK;
									ffr::setState(blend_state | state);
									ffr::setIndexBuffer(m_pipeline->m_cube_ib);
									ffr::setVertexBuffer(&decl, m_pipeline->m_cube_vb, 0, nullptr);

									const Renderer::TransientSlice instance_buffer = m_pipeline->m_renderer.allocTransient(instances_count * instance_decl.size);
									memcpy(instance_buffer.ptr, instance_data, instance_buffer.size);
									ffr::flushBuffer(instance_buffer.buffer, instance_buffer.offset, instance_buffer.size);

									ffr::setInstanceBuffer(instance_decl, instance_buffer.buffer, instance_buffer.offset, 1, nullptr);
									ffr::drawTrianglesInstanced(0, 36, instances_count);
								}
								break;
							}
							default: ASSERT(false); return;
						}
					}
				}
				#undef READ
			}

			ShaderRenderData* m_shader;
			PipelineImpl* m_pipeline;
			CommandSet* m_cmd_set;
			u32 m_define_mask;
		};

		ShaderRenderData* shader = [&]() -> ShaderRenderData* {
			for (const ShaderRef& s : m_shaders) {
				if(s.id == shader_idx) {
					return ((Shader*)s.res)->m_render_data;
				}
			}
			return nullptr;
		}();

		if(!shader) return;

		RenderJob* job = LUMIX_NEW(m_renderer.getAllocator(), RenderJob);
		job->m_define_mask = define[0] ? 1 << m_renderer.getShaderDefineIdx(define) : 0;
		job->m_pipeline = this;
		job->m_cmd_set = cmd_set;
		job->m_shader = shader;
		m_renderer.push(job);*/
		//ASSERT(false);
		// TODO
	}


	static u64 getState(lua_State* L, int idx)
	{
		ffr::StencilFuncs stencil_func = ffr::StencilFuncs::DISABLE;
		u8 stencil_write_mask = 0xff;
		u8 stencil_ref = 0;
		u8 stencil_mask = 0;
		ffr::StencilOps stencil_sfail = ffr::StencilOps::KEEP;
		ffr::StencilOps stencil_zfail = ffr::StencilOps::KEEP;
		ffr::StencilOps stencil_zpass = ffr::StencilOps::KEEP;

		u64 rs = (u64)ffr::StateFlags::DEPTH_TEST | (u64)ffr::StateFlags::DEPTH_WRITE;
		char tmp[64];
		if (LuaWrapper::getOptionalStringField(L, idx, "blending", tmp, lengthOf(tmp))) {
			rs = tmp[0] ? ffr::getBlendStateBits(ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA, ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA) : 0;
		}

		LuaWrapper::getOptionalFlagField(L, idx, "depth_test", &rs, (u64)ffr::StateFlags::DEPTH_TEST, true);
		LuaWrapper::getOptionalFlagField(L, idx, "depth_write", &rs, (u64)ffr::StateFlags::DEPTH_WRITE, true);
		LuaWrapper::getOptionalField(L, idx, "stencil_func", reinterpret_cast<u8*>(&stencil_func));
		LuaWrapper::getOptionalField(L, idx, "stencil_write_mask", &stencil_write_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_ref", &stencil_ref);
		LuaWrapper::getOptionalField(L, idx, "stencil_mask", &stencil_mask);
		LuaWrapper::getOptionalField(L, idx, "stencil_sfail", reinterpret_cast<u8*>(&stencil_sfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zfail", reinterpret_cast<u8*>(&stencil_zfail));
		LuaWrapper::getOptionalField(L, idx, "stencil_zpass", reinterpret_cast<u8*>(&stencil_zpass));

		rs |= ffr::getStencilStateBits(stencil_write_mask, stencil_func, stencil_ref, stencil_mask, stencil_sfail, stencil_zfail, stencil_zpass);

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
					T N = *(T*)cmd; \
					cmd += sizeof(T); \
					do {} while(false)
				PROFILE_FUNCTION();
				if(m_cmds->header.size == 0 && !m_cmds->header.next) return;

				ffr::VertexDecl instance_decl;
				instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
				instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);

				ffr::VertexDecl decal_decl;
				decal_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);

				ffr::VertexDecl decal_instance_decl;
				decal_instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
				decal_instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
				decal_instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);

				Renderer& renderer = m_pipeline->m_renderer;

				Stats stats = {};

				const u32 instanced_mask = m_define_mask | (1 << renderer.getShaderDefineIdx("INSTANCED"));
				const u32 skinned_mask = m_define_mask | (1 << renderer.getShaderDefineIdx("SKINNED"));
				const u64 render_states = m_render_state;

				CmdPage* page = m_cmds;
				while (page) {
					const u8* cmd = page->data;
					const u8* cmd_end = page->data + page->header.size;
					while (cmd != cmd_end) {
						READ(const RenderableTypes, type);
						switch(type) {
							case RenderableTypes::MESH:
							case RenderableTypes::MESH_GROUP: {
								READ(const Mesh::RenderData*, mesh);
								READ(const Material::RenderData*, material);
								READ(const u16, instances_count);
								READ(const ffr::BufferHandle, buffer);
								READ(const uint, offset);
								const uint size = instances_count * instance_decl.size;

								ShaderRenderData* shader = material->shader;
								ffr::bindTextures(nullptr, 0, 16);
								ffr::bindTextures(material->textures, 0, material->textures_count);

								const Shader::Program& prog = Shader::getProgram(shader, instanced_mask);
								if(prog.handle.isValid()) {
									ffr::setState(material->render_states | render_states);
									const Vec4 params(material->roughness, material->metallic, material->emission, 0);
									ffr::setUniform4f(m_pipeline->m_material_params_uniform, &params.x);
									ffr::setUniform4f(m_pipeline->m_material_color_uniform, &material->color.x);

									ffr::useProgram(prog.handle);

									int attribute_map[16];
									for (uint i = 0; i < mesh->vertex_decl.attributes_count; ++i) {
										attribute_map[i] = prog.attribute_by_semantics[(int)mesh->attributes_semantic[i]];
									}
									ffr::setVertexBuffer(&mesh->vertex_decl, mesh->vertex_buffer_handle, 0, prog.use_semantics ? attribute_map : nullptr);
									ffr::setIndexBuffer(mesh->index_buffer_handle);

									ffr::flushBuffer(buffer, offset, size);
									int instance_map[16];
									for (uint i = 0; i < instance_decl.attributes_count; ++i) {
										instance_map[i] = prog.attribute_by_semantics[(int)Mesh::AttributeSemantic::INSTANCE0 + i];
									}

									ffr::setInstanceBuffer(instance_decl, buffer, offset, mesh->vertex_decl.attributes_count, instance_map);
									ffr::drawTrianglesInstanced(0, mesh->indices_count, instances_count);
									++stats.draw_call_count;
									stats.triangle_count += instances_count * mesh->indices_count / 3;
									stats.instance_count += instances_count;
								}
								break;
							}
							case RenderableTypes::SKINNED: {
								READ(const Mesh::RenderData*, mesh);
								READ(const Material::RenderData*, material);
								READ(const Vec3, pos);
								READ(const Quat, rot);
								READ(const float, scale);
								READ(const i32, bones_count);

								READ(const ffr::BufferHandle, buffer);
								READ(const uint, offset);

								Matrix model_mtx(pos, rot);
								model_mtx.multiply3x3(scale);

								ShaderRenderData* shader = material->shader;
								ffr::bindTextures(material->textures, 0, material->textures_count);

								const Shader::Program& prog = Shader::getProgram(shader, skinned_mask);
								if(prog.handle.isValid()) {
									ffr::setState(material->render_states | render_states);
									const Vec4 params(material->roughness, material->metallic, material->emission, 0);
									ffr::setUniform4f(m_pipeline->m_material_params_uniform, &params.x);
									ffr::setUniform4f(m_pipeline->m_material_color_uniform, &material->color.x);
									ffr::setUniformMatrix4f(m_pipeline->m_model_uniform, &model_mtx.m11);
								
									ffr::useProgram(prog.handle);

									const int bones_size = bones_count * sizeof(Matrix);
									ffr::bindUniformBuffer(2, buffer, offset, bones_size);

									ffr::flushBuffer(buffer, offset, bones_size);
									int attribute_map[16];
									for (uint i = 0; i < mesh->vertex_decl.attributes_count; ++i) {
										attribute_map[i] = prog.attribute_by_semantics[(int)mesh->attributes_semantic[i]];
									}
									ffr::setVertexBuffer(&mesh->vertex_decl, mesh->vertex_buffer_handle, 0, prog.use_semantics ? attribute_map : nullptr);
									ffr::setIndexBuffer(mesh->index_buffer_handle);
									ffr::drawTriangles(mesh->indices_count);
									++stats.draw_call_count;
									stats.triangle_count += mesh->indices_count / 3;
									++stats.instance_count;
								}
								break;
							}
							case RenderableTypes::DECAL: {
								READ(Material::RenderData*, material);
								READ(u16, instances_count);

								ShaderRenderData* shader = material->shader;
								ffr::bindTextures(material->textures, 0, material->textures_count);

								const u8* instance_data = cmd;
								cmd += decal_instance_decl.size * instances_count;

								const Shader::Program& prog = Shader::getProgram(shader, m_define_mask);
								if (prog.handle.isValid()) {
									ffr::useProgram(prog.handle);
									ffr::setState(material->render_states | render_states);
									ffr::setIndexBuffer(m_pipeline->m_cube_ib);
									ffr::setVertexBuffer(&decal_decl, m_pipeline->m_cube_vb, 0, nullptr);

									const Renderer::TransientSlice instance_buffer = m_pipeline->m_renderer.allocTransient(instances_count * decal_instance_decl.size);
									memcpy(instance_buffer.ptr, instance_data, instance_buffer.size);
									ffr::flushBuffer(instance_buffer.buffer, instance_buffer.offset, instance_buffer.size);
									
									ffr::setInstanceBuffer(decal_instance_decl, instance_buffer.buffer, instance_buffer.offset, 1, nullptr);
									ffr::drawTrianglesInstanced(0, 36, instances_count);
									++stats.draw_call_count;
									stats.instance_count += instances_count;
								}

								break;
							}
							case RenderableTypes::GRASS: {
								READ(const Quat, rot);
								READ(const Vec3, pos);
								READ(const Mesh::RenderData*, mesh);
								READ(const Material::RenderData*, material);
								READ(const int, instances_count);
								const u8* instance_data = cmd;
								const int byte_size = sizeof(Terrain::GrassPatch::InstanceData) * instances_count;
								cmd += byte_size;

								const Renderer::TransientSlice transient = m_pipeline->m_renderer.allocTransient(byte_size);
								if ((int)transient.size < byte_size) {
									logWarning("Renderer") << "Not enough memory reserved to render grass.";
									break;
								}

								memcpy(transient.ptr, instance_data, transient.size);
								ffr::flushBuffer(transient.buffer, transient.offset, transient.size);
								ffr::VertexDecl instance_decl;
								instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
								instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
								instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
								const u32 deferred_define_mask = 1 << m_pipeline->m_renderer.getShaderDefineIdx("DEFERRED");
								const u32 define_mask = (1 << m_pipeline->m_renderer.getShaderDefineIdx("GRASS")) | deferred_define_mask;

								int attribute_map[16];
								const Shader::Program& prg = Shader::getProgram(material->shader, define_mask);
								if (prg.handle.isValid()) {
									for (uint i = 0; i < mesh->vertex_decl.attributes_count; ++i) {
										attribute_map[i] = prg.attribute_by_semantics[(int)mesh->attributes_semantic[i]];
									}
									ffr::bindTextures(material->textures, 0, material->textures_count);
									ffr::setVertexBuffer(&mesh->vertex_decl
										, mesh->vertex_buffer_handle
										, 0
										, attribute_map);
									ffr::setIndexBuffer(mesh->index_buffer_handle);
									const Vec4 params(material->roughness, material->metallic, material->emission, 0);
									ffr::setUniform4f(m_pipeline->m_material_params_uniform, &params.x);
									ffr::setUniform4f(m_pipeline->m_material_color_uniform, &material->color.x);
									const Matrix mtx(pos, rot);
									ffr::setUniformMatrix4f(m_pipeline->m_model_uniform, &mtx.m11);
									ffr::useProgram(prg.handle);
									int instance_map[16];
									for (uint i = 0; i < instance_decl.attributes_count; ++i) {
										instance_map[i] = prg.attribute_by_semantics[(int)Mesh::AttributeSemantic::INSTANCE0 + i];
									}
									ffr::setInstanceBuffer(instance_decl, transient.buffer, transient.offset, 0, instance_map);

									ffr::setState(u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::DEPTH_WRITE));
									ffr::drawTrianglesInstanced(0, mesh->indices_count, instances_count);
									
									++stats.draw_call_count;
									stats.instance_count += instances_count;
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
				#undef READ
				Profiler::pushInt("drawcalls", stats.draw_call_count);
				Profiler::pushInt("instances", stats.instance_count);
				Profiler::pushInt("triangles", stats.triangle_count);
			}

			u32 m_define_mask = 0;
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

		RenderJob* job = LUMIX_NEW(pipeline->m_renderer.getAllocator(), RenderJob);

		char tmp[64];
		if (LuaWrapper::getOptionalStringField(L, 2, "define", tmp, lengthOf(tmp))) {
			job->m_define_mask = tmp[0] ? 1 << pipeline->m_renderer.getShaderDefineIdx(tmp) : 0;
		}

		job->m_render_state = getState(L, 2);
		job->m_pipeline = pipeline;
		job->m_cmds = cmd_page;
		pipeline->m_renderer.push(job, pipeline->m_profiler_link);
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


	static int setRenderTargets(lua_State* L)
	{ 
		PROFILE_FUNCTION();
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx );
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int rb_count = lua_gettop(L);
		int rbs[16];
		if(rb_count > lengthOf(rbs)) {
			logError("Renderer") << "Too many render buffers in " << pipeline->getPath();	
			return 0;
		}
		if(rb_count <= 0) {
			logError("Renderer") << "createFramebuffer without arguments in " << pipeline->getPath();
			return 0;
		}

		struct Cmd : Renderer::RenderJob
		{
			void setup() override { }

			void execute() override
			{
				PROFILE_FUNCTION();
				const ffr::FramebufferHandle fb = pipeline->m_renderer.getFramebuffer();
			
				ffr::update(fb, count, rbs);
				ffr::setFramebuffer(fb, true);
				ffr::viewport(0, 0, w, h);
			}

			PipelineImpl* pipeline;
			ffr::TextureHandle rbs[16];
			uint count;
			uint w;
			uint h;
		};

		Cmd* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), Cmd);
		for(int i = 0; i < rb_count; ++i) {
			const int rb_idx = LuaWrapper::checkArg<int>(L, i + 1);
			cmd->rbs[i] = pipeline->m_renderbuffers[rb_idx].handle;
		}

		cmd->pipeline = pipeline;
		cmd->count = rb_count;
		cmd->w = pipeline->m_viewport.w;
		cmd->h = pipeline->m_viewport.h;
		pipeline->m_renderer.push(cmd, pipeline->m_profiler_link);

		return 0;
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
			m_pipeline->m_scene->getTerrainInfos(m_camera_params.frustum, m_camera_params.pos, infos);
			if(infos.empty()) return;

			m_instances.reserve(infos.size());
			for (TerrainInfo& info : infos) {
				if (!info.terrain->m_heightmap) continue;
				if (!info.terrain->m_heightmap->isReady()) continue;
				
				Instance& inst = m_instances.emplace();
				inst.pos = (info.position - m_camera_params.pos).toFloat();
				inst.rot = info.rot;
				inst.scale = info.terrain->getScale();
				inst.shader = info.shader->m_render_data;
				inst.material = info.terrain->m_material->getRenderData();
			}
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			const u32 deferred_define_mask = 1 << m_pipeline->m_renderer.getShaderDefineIdx("DEFERRED");
			const u8 edge_define_idx = m_pipeline->m_renderer.getShaderDefineIdx("EDGE");
			
			u64 state = m_render_state;
			static bool b = false; // TODO
			if (b) state |= (u64)ffr::StateFlags::WIREFRAME;

			static int stat;
			stat = 0;

			for (Instance& inst : m_instances) {
				auto& p = Shader::getProgram(inst.shader, deferred_define_mask);
				if (!p.handle.isValid()) continue;

				const Vec3 pos = inst.pos;
				const Vec3 lpos = inst.rot.conjugated().rotate(-inst.pos);

				ffr::setUniform3f(m_pipeline->m_position_uniform, &pos.x);
				ffr::setUniform3f(m_pipeline->m_rel_camera_pos_uniform, &lpos.x);

				ffr::bindTextures(inst.material->textures, 0, inst.material->textures_count);

				ffr::setState(state);
				const int loc = ffr::getUniformLocation(p.handle, m_pipeline->m_lod_uniform);
				const int loc2 = ffr::getUniformLocation(p.handle, ffr::allocUniform("u_from_to", ffr::UniformType::IVEC4, 1));
				const int loc3 = ffr::getUniformLocation(p.handle, m_pipeline->m_terrain_scale_uniform);
				const int loc4 = ffr::getUniformLocation(p.handle, ffr::allocUniform("u_from_to_sup", ffr::UniformType::IVEC4, 1));
				IVec4 prev_from_to;
				for (int i = 0; ; ++i) {
					const int s = 1 << i;
					// round 
					IVec2 from = IVec2((lpos.xz() + Vec2(0.5f * s)) / float(s)) - IVec2(64);
					from.x = from.x & ~1;
					from.y = from.y & ~1;
					IVec2 to = from + IVec2(128);
					// clamp
					const IVec2 from_unclamped = from;
					const IVec2 to_unclamped = to;
					const IVec4 from_to_sup(from_unclamped, to_unclamped);
					
					from.x = clamp(from.x, 0, 1024 / s);
					from.y = clamp(from.y, 0, 1024 / s);
					to.x = clamp(to.x, 0, 1024 / s);
					to.y = clamp(to.y, 0, 1024 / s);

					auto draw_rect = [&](const IVec2& subfrom, const IVec2& subto){
						if (subfrom.x >= subto.x || subfrom.y >= subto.y) return;
						const IVec4 from_to(subfrom, subto);
						ffr::useProgram(p.handle);
						ffr::applyUniform4i(loc4, &from_to_sup.x);
						ffr::applyUniform4i(loc2, &from_to.x);
						ffr::applyUniform3f(loc3, &inst.scale.x);
						ffr::applyUniform1i(loc, i);
						ffr::drawArrays(0, (subto.x - subfrom.x) * (subto.y - subfrom.y), ffr::PrimitiveType::POINTS);
						stat += (subto.x - subfrom.x) * (subto.y - subfrom.y);
					};

					if (i > 0) {
						draw_rect(from, IVec2(to.x, prev_from_to.y / 2));
						draw_rect(IVec2(from.x, prev_from_to.w / 2), to);
						
						draw_rect(IVec2(prev_from_to.z / 2, prev_from_to.y / 2), IVec2(to.x, prev_from_to.w / 2));
						draw_rect(IVec2(from.x, prev_from_to.y / 2), IVec2(prev_from_to.x / 2, prev_from_to.w / 2));
					}
					else {
						draw_rect(from, to);
					}
					
					if (from.x <= 0 && from.y <= 0 && to.x * s >= 1024 && to.y * s >= 1024) break;

					prev_from_to = IVec4(from, to);
				}
			}
		}

		struct Instance
		{
			Vec3 pos;
			Quat rot;
			Vec3 scale;
			ShaderRenderData* shader;
			Material::RenderData* material;
		};

		IAllocator& m_allocator;
		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		u64 m_render_state;
		Array<Instance> m_instances;
		struct {
			ffr::TextureHandle texture;
			ffr::UniformHandle uniform;
		} m_global_textures[16];
		int m_global_textures_count = 0;

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


		void radixSort(u64* _keys, u64* _values, int size)
		{
			PROFILE_FUNCTION();
			Profiler::pushInt("count", size);
			if(size == 0) return;
			// from https://github.com/bkaradzic/bx
			enum {
				RADIXSORT_BITS = 11,
				RADIXSORT_HISTOGRAM_SIZE = 1 << RADIXSORT_BITS,
				RADIXSORT_BIT_MASK = RADIXSORT_HISTOGRAM_SIZE - 1
			};

			Array<u64> tmp_keys(m_allocator); // TODO more suitable allocator
			Array<u64> tmp_values(m_allocator);
			tmp_keys.resize(size);
			tmp_values.resize(size);

			u64* keys = _keys;
			u64* tempKeys = tmp_keys.begin();
			u64* values = _values;
			u64* tempValues = tmp_values.begin();

			u32 histogram[RADIXSORT_HISTOGRAM_SIZE];
			u16 shift = 0;
			for (int pass = 0; pass < 6; ++pass) {
				memset(histogram, 0, sizeof(u32) * RADIXSORT_HISTOGRAM_SIZE);

				bool sorted = true;
				u64 key = keys[0];
				u64 prevKey = key;
				for (int i = 0; i < size; ++i, prevKey = key) {
					key = keys[i];
					const u16 index = (key >> shift) & RADIXSORT_BIT_MASK;
					++histogram[index];
					sorted &= prevKey <= key;
				}

				if (sorted) {
					if (pass & 1) {
						// Odd number of passes needs to do copy to the destination.
						memcpy(_keys, tmp_keys.begin(), tmp_keys.byte_size());
						memcpy(_values, tmp_values.begin(), tmp_values.byte_size());
					}
					return;
				}

				u32 offset = 0;
				for (int i = 0; i < RADIXSORT_HISTOGRAM_SIZE; ++i) {
					const u32 count = histogram[i];
					histogram[i] = offset;
					offset += count;
				}

				for (int i = 0; i < size; ++i) {
					const u64 key = keys[i];
					const u16 index = (key >> shift) & RADIXSORT_BIT_MASK;
					const u32 dest = histogram[index]++;
					tempKeys[dest] = key;
					tempValues[dest] = values[i];
				}

				u64* const swapKeys = tempKeys;
				tempKeys = keys;
				keys = swapKeys;

				u64* const swapValues = tempValues;
				tempValues = values;
				values = swapValues;

				shift += RADIXSORT_BITS;
			}
		}


		static_assert(sizeof(CmdPage) == PageAllocator::PAGE_SIZE, "Wrong page size");


		struct CreateCommands 
		{
			static void execute(void* data)
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
				CreateCommands* ctx = (CreateCommands*)data;
				Profiler::pushInt("num", ctx->count);
				Renderer& renderer = ctx->cmd->m_pipeline->m_renderer;
				const Universe& universe = ctx->cmd->m_pipeline->m_scene->getUniverse();
				
				const u64* LUMIX_RESTRICT renderables = ctx->renderables;
				const u64* LUMIX_RESTRICT sort_keys = ctx->sort_keys;
				CmdPage* cmd_page = new (NewPlaceholder(), ctx->cmd->m_page_allocator.allocate(true)) CmdPage;
				ctx->first_page = ctx->last_page = cmd_page;
				cmd_page->header.bucket = sort_keys[0] >> 56;
				u8* out = cmd_page->data;

				auto new_page = [&](u8 bucket){
					cmd_page->header.size = int(out - cmd_page->data);
					CmdPage* new_page = new (NewPlaceholder(), ctx->cmd->m_page_allocator.allocate(true)) CmdPage;
					cmd_page->header.next = new_page;
					cmd_page = new_page;
					new_page->header.bucket = bucket;
					ctx->last_page = cmd_page;
					out = cmd_page->data;
				};

				RenderScene* scene = ctx->cmd->m_pipeline->m_scene;
				const ShiftedFrustum frustum = ctx->cmd->m_camera_params.frustum;
				const ModelInstance* LUMIX_RESTRICT model_instances = scene->getModelInstances();
				const Transform* LUMIX_RESTRICT entity_data = universe.getTransforms(); 
				const DVec3 camera_pos = ctx->camera_pos;
				for (int i = 0, c = ctx->count; i < c; ++i) {
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
							const bool sort_depth = ctx->cmd->m_bucket_map[bucket] > 0xff;
							const u64 instance_key_mask = sort_depth ? 0xff00'0000'00ff'ffff : 0xffff'ffff'0000'0000;
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
							if ((cmd_page->data + sizeof(cmd_page->data) - out) < 30) {
								new_page(bucket);
							}

							WRITE(type);
							WRITE(mi->meshes[mesh_idx].render_data);
							WRITE_FN(mi->meshes[mesh_idx].material->getRenderData());
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

							WRITE(type);
							WRITE(mi->meshes[mesh_idx].render_data);
							WRITE_FN(mi->meshes[mesh_idx].material->getRenderData());
							WRITE(rel_pos);
							WRITE(tr.rot);
							WRITE(tr.scale);
							WRITE(mi->pose->count);

							const Renderer::TransientSlice slice = renderer.allocTransient(mi->pose->count * sizeof(Matrix) + 256);
							WRITE(slice.buffer);
							uint align = 256 - slice.offset % 256; 
							uint offset = slice.offset + align;
							WRITE(offset);
							const Quat* rotations = mi->pose->rotations;
							const Vec3* positions = mi->pose->positions;

							Model& model = *mi->model;
							for (int j = 0, c = mi->pose->count; j < c; ++j) {
								const Model::Bone& bone = model.getBone(j);
								const LocalRigidTransform tmp = {positions[j], rotations[j]};
								Matrix m = (tmp * bone.inv_bind_transform).toMatrix();
								memcpy(slice.ptr + align + sizeof(Matrix) * j, &m, sizeof(Matrix));
							}
							break;
						}
						case RenderableTypes::DECAL: {
							/*const Material* material = scene->getDecalMaterial(e);
							const Vec3 half_extents = scene->getDecalHalfExtents(e);

							const uint out_offset = uint(out - ctx->output->begin());
							ctx->output->resize(ctx->output->size() +  sizeof(Vec3) + sizeof(void*) + sizeof(Vec3) + sizeof(Quat));
							WRITE_FN(material->getRenderData());

							u16* instance_count = (u16*)out;
							int start_i = i;
							out += sizeof(*instance_count);
							const u64 key = sort_keys[i];
							while (i < c && sort_keys[i] == key) {
								const EntityRef e = {int(renderables[i] & 0x00ffFFff)};
								const Transform& tr = entity_data[e.index];
								const Vec3 lpos = (tr.pos - camera_pos).toFloat();
								WRITE(lpos);
								WRITE(tr.rot);
								const Vec3 half_extents = scene->getDecalHalfExtents(e);
								WRITE(half_extents);
								++i;
							}
							*instance_count = u16(i - start_i);
							--i;*/
							ASSERT(false);
							// TODO
							break;
						}
						case RenderableTypes::LOCAL_LIGHT: {
							/*u16* instance_count = (u16*)out;
							int start_i = i;
							out += sizeof(*instance_count);
							bool* intersecting = (bool*)out;
							out += sizeof(*intersecting);
							const u64 key = sort_keys[i];

							const EntityRef e = {int(renderables[i] & 0x00ffFFff)};
							const Transform& tr = entity_data[e.index];
							const Vec3 lpos = (tr.pos - camera_pos).toFloat();
							const PointLight& pl = scene->getPointLight(e);
							*intersecting = frustum.intersectNearPlane(tr.pos, pl.range * SQRT3);
							if (*intersecting) {
								WRITE(tr.rot);
								WRITE(lpos);
								WRITE(pl.range);
								WRITE(pl.attenuation_param);
								const Vec3 color = pl.color * pl.intensity;
								WRITE(color);
								const Vec3 dir = tr.rot * Vec3(0, 0, 1);
								WRITE(dir);
								WRITE(pl.fov);
								++i;
							}
							else {
								while (i < c && sort_keys[i] == key) {
									const EntityRef e = {int(renderables[i] & 0x00ffFFff)};
									const Transform& tr = entity_data[e.index];
									const Vec3 lpos = (tr.pos - camera_pos).toFloat();
									const PointLight& pl = scene->getPointLight(e);

									WRITE(tr.rot);
									WRITE(lpos);
									WRITE(pl.range);
									WRITE(pl.attenuation_param);
									const Vec3 color = pl.color * pl.intensity;
									WRITE(color);
									const Vec3 dir = tr.rot * Vec3(0, 0, 1);
									WRITE(dir);
									WRITE(pl.fov);
									++i;
								}
							}
							*instance_count = u16(i - start_i);
							--i;*/
							ASSERT(false);
							// TODO
							break;
						}
						case RenderableTypes::GRASS: {
							/*const u16 quad_idx = u16(renderables[i] >> 32);
							const u8 patch_idx = u8(renderables[i] >> 48);
							const u8 mesh_idx = u8(renderables[i] >> 56);
							const Terrain* t = scene->getTerrain(e);
							// TODO this crashes if the shader is reloaded
							// TODO 0 const in following:
							const Terrain::GrassPatch& p = t->m_grass_quads[0][quad_idx]->m_patches[patch_idx];
							const Mesh& mesh = p.m_type->m_grass_model->getMesh(mesh_idx);
							const Transform& tr = entity_data[e.index];
							const Vec3 lpos = (tr.pos - camera_pos).toFloat();
							WRITE(tr.rot);
							WRITE(lpos);
							WRITE(mesh.render_data);
							WRITE_FN(mesh.material->getRenderData());
							const int instances_count = p.instance_data.size();
							WRITE(instances_count);
							const uint out_offset = uint(out - ctx->output->begin());
							ctx->output->resize(ctx->output->size() + p.instance_data.byte_size());
							out = ctx->output->begin() + out_offset;
							memcpy(out, p.instance_data.begin(), p.instance_data.byte_size());
							out += p.instance_data.byte_size();*/
							ASSERT(false);
							// TODO

							break;
						}
						default: ASSERT(false); break;
					}
				}
				cmd_page->header.size = int(out - cmd_page->data);
				#undef WRITE
				#undef WRITE_FN
			}

			u64* renderables;
			u64* sort_keys;
			int idx;
			DVec3 camera_pos;
			int count;
			PrepareCommandsRenderJob* cmd;
			CmdPage* first_page = nullptr;
			CmdPage* last_page = nullptr;
		};


		void createSortKeys(const CullResult* renderables, RenderableTypes type, MTBucketArray<u64>& sort_keys)
		{
			ASSERT(renderables);
			if (renderables->header.count == 0 && !renderables->header.next) return;
			PagedListIterator<const CullResult> iterator(renderables);

			JobSystem::runOnWorkers([&](){
				PROFILE_BLOCK("sort_keys");
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
									const u64 subrenderable = e.index | type_mask | ((u64)mesh_idx << 40);
									if (bucket < 0xff) {
										const u64 key = ((u64)mi.meshes[mesh_idx].sort_key << 32) | ((u64)bucket << 56);
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

			Renderer& renderer = m_pipeline->m_renderer;
			const RenderScene* scene = m_pipeline->getScene();

			MTBucketArray<u64> sort_keys(m_allocator);

			const RenderableTypes types[] = {
				RenderableTypes::MESH,
				RenderableTypes::MESH_GROUP,
				RenderableTypes::SKINNED,
				RenderableTypes::DECAL,
				RenderableTypes::GRASS,
				RenderableTypes::LOCAL_LIGHT
			};
			JobSystem::forEach(lengthOf(types), [&](int idx){
				CullResult* renderables = scene->getRenderables(m_camera_params.frustum, types[idx]);
				if (renderables) {
					createSortKeys(renderables, types[idx], sort_keys);
					renderables->free(m_pipeline->m_renderer.getEngine().getPageAllocator());
				}
			});
			sort_keys.merge();

			if (sort_keys.size() > 0) {
				radixSort(sort_keys.key_ptr(), sort_keys.value_ptr(), sort_keys.size());
				createCommands(sort_keys.value_ptr(), sort_keys.key_ptr(), sort_keys.size());
			}
		}


		void createCommands(u64* renderables, u64* sort_keys, int size)
		{
			const int jobs_count = JobSystem::getWorkersCount() * 4;
			CreateCommands* create_commands = (CreateCommands*)alloca(sizeof(CreateCommands) * jobs_count);
			new (NewPlaceholder(), create_commands) CreateCommands[jobs_count];
			
			JobSystem::SignalHandle counter = JobSystem::INVALID_HANDLE;

			const int step = (size + jobs_count - 1) / jobs_count;
			int offset = 0;
			for(int i = 0; i < jobs_count; ++i) {
				const int count = minimum(step, size - offset);
				if (count <= 0) break;
				CreateCommands& ctx = create_commands[i];
				ctx.renderables = renderables + offset;
				ctx.sort_keys = sort_keys + offset;
				ctx.count = count;
				ctx.cmd = this;
				ctx.camera_pos = m_camera_params.pos;
				JobSystem::run(&ctx, &CreateCommands::execute, &counter);
				offset += count;
			}
			JobSystem::wait(counter);
			CreateCommands& ctx = create_commands[0];
			for(int i = 0; i < jobs_count; ++i) {
				CreateCommands& cur = create_commands[i];
				CmdPage* page = cur.first_page;
				while(page) {
					m_command_sets[page->header.bucket]->header.next = page;
					m_command_sets[page->header.bucket] = page;
					CmdPage* next = page->header.next;
					page->header.next = nullptr;
					page = next;
				}
			}

		}


		void execute() override {}

		IAllocator& m_allocator;
		PageAllocator& m_page_allocator;
		CameraParams m_camera_params;
		PipelineImpl* m_pipeline;
		struct {
			ffr::TextureHandle texture;
			ffr::UniformHandle uniform;
		} m_global_textures[16];
		int m_global_textures_count = 0;
		CmdPage* m_command_sets[255];
		u32 m_bucket_map[255];
		SortOrder m_bucket_sort_order[255] = {};
		u8 m_bucket_count;
	};


	void clear(u32 flags, float r, float g, float b, float a, float depth)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				ffr::clear(flags, &color.x, depth);
			}
			Vec4 color;
			float depth;
			u32 flags;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->color.set(r, g, b, a);
		cmd->flags = flags;
		cmd->depth = depth;
		m_renderer.push(cmd, m_profiler_link);
	}


	void viewport(int x, int y, int w, int h)
	{
		struct Cmd : Renderer::RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				ffr::viewport(x, y, w, h); 
			}
			int x, y, w, h;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->x = x;
		cmd->y = y;
		cmd->w = w;
		cmd->h = h;

		m_renderer.push(cmd, m_profiler_link);
	}


	void beginBlock(const char* name)
	{
		struct Cmd : Renderer::RenderJob
		{
			void setup() override {}
			void execute() override 
			{
				PROFILE_FUNCTION();
				ffr::pushDebugGroup(name);
				renderer->beginProfileBlock(name, profiler_link);
			}
			StaticString<32> name;
			Renderer* renderer;
			i64 profiler_link;
		};
		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->name = name;
		cmd->renderer = &m_renderer;
		m_profiler_link = Profiler::createNewLinkID();
		cmd->profiler_link = m_profiler_link;
		m_renderer.push(cmd, m_profiler_link);
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
				ffr::popDebugGroup();
			}
			Renderer* renderer;
		};
		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->renderer = &m_renderer;
		m_renderer.push(cmd, m_profiler_link);
		m_profiler_link = 0;
	}

	
	void setOutput(int rb_index) 
	{
		m_output = rb_index;
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


	void setWindowHandle(void* data) override { ASSERT(false); } // TODO


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
				auto f = &LuaWrapper::wrapMethodClosure<PipelineImpl, decltype(&PipelineImpl::name), &PipelineImpl::name>; \
				registerCFunction(#name, f); \
			} while(false) \

		REGISTER_FUNCTION(beginBlock);
		REGISTER_FUNCTION(clear);
		REGISTER_FUNCTION(createRenderbuffer);
		REGISTER_FUNCTION(endBlock);
		REGISTER_FUNCTION(executeCustomCommand);
		REGISTER_FUNCTION(preloadShader);
		REGISTER_FUNCTION(render2D);
		REGISTER_FUNCTION(renderDebugShapes);
		REGISTER_FUNCTION(renderLocalLights);
		REGISTER_FUNCTION(renderTextMeshes);
		REGISTER_FUNCTION(setOutput);
		REGISTER_FUNCTION(viewport);

		registerConst("CLEAR_DEPTH", (uint)ffr::ClearFlags::DEPTH);
		registerConst("CLEAR_COLOR", (uint)ffr::ClearFlags::COLOR);
		registerConst("CLEAR_ALL", (uint)ffr::ClearFlags::COLOR | (uint)ffr::ClearFlags::DEPTH | (uint)ffr::ClearFlags::STENCIL);

		registerConst("STENCIL_ALWAYS", (uint)ffr::StencilFuncs::ALWAYS);
		registerConst("STENCIL_EQUAL", (uint)ffr::StencilFuncs::EQUAL);
		registerConst("STENCIL_NOT_EQUAL", (uint)ffr::StencilFuncs::NOT_EQUAL);
		registerConst("STENCIL_DISABLE", (uint)ffr::StencilFuncs::DISABLE);
		registerConst("STENCIL_KEEP", (uint)ffr::StencilOps::KEEP);
		registerConst("STENCIL_REPLACE", (uint)ffr::StencilOps::REPLACE);

		registerCFunction("bindTextures", PipelineImpl::bindTextures);
		registerCFunction("drawArray", PipelineImpl::drawArray);
		registerCFunction("prepareShadowcastingLocalLights", PipelineImpl::prepareShadowcastingLocalLights);
		registerCFunction("getCameraParams", PipelineImpl::getCameraParams);
		registerCFunction("getShadowCameraParams", PipelineImpl::getShadowCameraParams);
		registerCFunction("pass", PipelineImpl::pass);
		registerCFunction("prepareCommands", PipelineImpl::prepareCommands);
		registerCFunction("renderEnvProbeVolumes", PipelineImpl::renderEnvProbeVolumes);
		registerCFunction("renderBucket", PipelineImpl::renderBucket);
		registerCFunction("renderParticles", PipelineImpl::renderParticles);
		registerCFunction("renderTerrains", PipelineImpl::renderTerrains);
		registerCFunction("setRenderTargets", PipelineImpl::setRenderTargets);

		lua_pop(L, 1); // pop env

		#undef REGISTER_FUNCTION
	}


	bool isReady() const override { return m_resource->isReady(); }
	const Stats& getStats() const override { return m_stats; }
	const Path& getPath() override { return m_resource->getPath(); }

	Draw2D& getDraw2D() override { return m_draw2d; }
	ffr::TextureHandle getOutput() override { 
		if (m_output < 0 || m_output >= m_renderbuffers.size()) return ffr::INVALID_TEXTURE;
		return m_renderbuffers[m_output].handle;
	}

	struct Renderbuffer {
		uint width;
		uint height;
		bool use_realtive_size;
		Vec2 relative_size; 
		ffr::TextureFormat format;
		ffr::TextureHandle handle;
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
	bool m_is_first_render;
	StaticString<32> m_define;
	RenderScene* m_scene;
	Draw2D m_draw2d;
	Shader* m_draw2d_shader;
	Stats m_stats;
	Viewport m_viewport;
	int m_output;
	Shader* m_debug_shape_shader;
	Shader* m_text_mesh_shader;
	Texture* m_default_cubemap;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<Renderbuffer> m_renderbuffers;
	Array<ShaderRef> m_shaders;
	OS::Timer m_timer;
	ffr::BufferHandle m_global_state_buffer;
	ffr::BufferHandle m_pass_state_buffer;
	CameraParams m_shadow_camera_params[4];

	ffr::UniformHandle m_position_radius_uniform;
	ffr::UniformHandle m_position_uniform;
	ffr::UniformHandle m_lod_uniform;
	ffr::UniformHandle m_terrain_params_uniform;
	ffr::UniformHandle m_rel_camera_pos_uniform;
	ffr::UniformHandle m_terrain_scale_uniform;
	ffr::UniformHandle m_terrain_matrix_uniform;
	ffr::UniformHandle m_model_uniform;
	ffr::UniformHandle m_bones_uniform;
	ffr::UniformHandle m_canvas_size_uniform;
	ffr::UniformHandle m_texture_uniform;
	ffr::UniformHandle m_irradiance_map_uniform;
	ffr::UniformHandle m_radiance_map_uniform;
	ffr::UniformHandle m_material_params_uniform;
	ffr::UniformHandle m_material_color_uniform;
	ffr::BufferHandle m_cube_vb;
	ffr::BufferHandle m_cube_ib;
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


void Pipeline::renderModel(Model& model, const Matrix& mtx, ffr::UniformHandle mtx_uniform)
{
	for(int i = 0; i < model.getMeshCount(); ++i) {

		const Mesh& mesh = model.getMesh(i);
		const Material* material = mesh.material;
		ShaderRenderData* shader_rd = material->getShader()->m_render_data;
		const Shader::Program& prog = Shader::getProgram(shader_rd, 0); // TODO define

		if(!prog.handle.isValid()) continue;

		const Material::RenderData* mat_rd = material->getRenderData();
		ffr::bindTextures(mat_rd->textures, 0, mat_rd->textures_count);

		int attribute_map[16];
		const Mesh::RenderData* rd = mesh.render_data;
		for(uint i = 0; i < rd->vertex_decl.attributes_count; ++i) {
			attribute_map[i] = prog.attribute_by_semantics[(int)rd->attributes_semantic[i]];
		}

		ffr::setUniformMatrix4f(mtx_uniform, &mtx.m11);
		ffr::useProgram(prog.handle);
		ffr::setVertexBuffer(&rd->vertex_decl, rd->vertex_buffer_handle, 0, prog.use_semantics ? attribute_map : nullptr);
		ffr::setIndexBuffer(rd->index_buffer_handle);
		ffr::setState(u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::DEPTH_WRITE) | material->getRenderStates());
		ffr::drawTriangles(rd->indices_count);
	}
}



} // namespace Lumix