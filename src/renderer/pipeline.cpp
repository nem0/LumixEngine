#include "draw2d.h"
#include "ffr/ffr.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/ifile_device.h"
#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/mt/atomic.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/simd.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "engine/viewport.h"
#include "font_manager.h"
#include "material.h"
#include "model.h"
#include "particle_system.h"
#include "pipeline.h"
#include "pose.h"
#include "renderer.h"
#include "render_scene.h"
#include "shader.h"
#include "shader_manager.h"
#include "terrain.h"
#include "texture.h"
#include "texture_manager.h"
#include <algorithm>
#include <cmath>


namespace Lumix
{


static const float SHADOW_CAM_NEAR = 50.0f;
static const float SHADOW_CAM_FAR = 5000.0f;


ResourceType PipelineResource::TYPE("pipeline");


void PipelineResource::unload()
{
	content.clear();
}


bool PipelineResource::load(FS::IFile& file)
{
	content.resize((int)file.size());
	file.read(content.begin(), content.size());
	return true;
}


PipelineResourceManager::PipelineResourceManager(IAllocator& allocator)
	: ResourceManager(allocator)
	, m_allocator(allocator)
{}


Resource* PipelineResourceManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, PipelineResource)(path, *this, m_allocator);
}


void PipelineResourceManager::destroyResource(Resource& resource)
{
	return LUMIX_DELETE(m_allocator, static_cast<PipelineResource*>(&resource));
}

PipelineResource::PipelineResource(const Path& path, ResourceManager& owner, IAllocator& allocator)
	: Resource(path, owner, allocator)
	, content(allocator) 
{}
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
		m_timer = Timer::create(m_allocator);
		m_viewport.w = m_viewport.h = 800;
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_draw2d_shader = rm.load<Shader>(Path("pipelines/draw2d.shd"));
		m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));
		TextureManager& texture_manager = renderer.getTextureManager();
		m_default_cubemap = rm.load<Texture>(Path("models/common/default_probe.dds"));

		FontAtlas& font_atlas = m_renderer.getFontManager().getFontAtlas();
		m_draw2d.FontTexUvWhitePixel = font_atlas.TexUvWhitePixel;
		m_draw2d.Clear();
		m_draw2d.PushClipRectFullScreen();
		m_draw2d.PushTextureID(font_atlas.TexID);

		m_position_radius_uniform = ffr::allocUniform("u_pos_radius", ffr::UniformType::VEC4, 1);
		m_terrain_params_uniform = ffr::allocUniform("u_terrain_params", ffr::UniformType::VEC4, 1);
		m_rel_camera_pos_uniform = ffr::allocUniform("u_rel_camera_pos", ffr::UniformType::VEC4, 1);
		m_terrain_scale_uniform = ffr::allocUniform("u_terrain_scale", ffr::UniformType::VEC4, 1);
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
	}


	~PipelineImpl()
	{
		m_draw2d_shader->getResourceManager().unload(*m_draw2d_shader);
		m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
		m_default_cubemap->getResourceManager().unload(*m_default_cubemap);

		for(ShaderRef& shader : m_shaders) {
			shader.res->getResourceManager().unload(*shader.res);
		}
		Timer::destroy(m_timer);
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
				g_log_error.log("lua") << lua_tostring(m_lua_state, -1);
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
			g_log_error.log("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			g_log_error.log("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
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
			g_log_error.log("Renderer") << lua_tostring(m_lua_state, -1);
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

		if (m_renderer.getEngine().getDiskFileDevice())
		{
			lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
			lua_pushstring(m_lua_state, m_renderer.getEngine().getDiskFileDevice()->getBasePath());
			lua_setfield(m_lua_state, -2, "LUA_PATH");
		}

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
			g_log_error.log("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			g_log_error.log("Renderer") << m_resource->getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		m_viewport.w = m_viewport.h = 800;
		if (m_scene) callInitScene();
	}


	void clearBuffers()
	{
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

	
	void resetState()
	{
		struct Cmd : Renderer::RenderCommandBase {
			void setup() override {}
			void execute() override { 
				ffr::setStencil(0xff, ffr::StencilFuncs::DISABLE, 0, 0, ffr::StencilOps::KEEP, ffr::StencilOps::KEEP, ffr::StencilOps::KEEP);
			}
		};
		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		m_renderer.push(cmd);
	}


	bool render() override 
	{ 
		PROFILE_FUNCTION();

		if (!isReady() || !m_scene || m_viewport.w < 0 || m_viewport.h < 0) {
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

		for(Renderbuffer& rb : m_renderbuffers) {
			if(!rb.use_realtive_size) continue;
			const uint w = uint(rb.relative_size.x * m_viewport.w + 0.5f);
			const uint h = uint(rb.relative_size.y * m_viewport.h + 0.5f);
			if(rb.width != w || rb.height != h) {
				rb.width = w;
				rb.height = h;
				m_renderer.destroy(rb.handle);
				rb.handle = m_renderer.createTexture(w, h, rb.format, 0, {0, 0});
			}
		}

		Renderer::GlobalState state;

		const Matrix view = m_viewport.getViewRotation();
		const Matrix projection = m_viewport.getProjection(ffr::isHomogenousDepth());
		state.camera_projection = projection;
		state.camera_inv_projection = projection;
		state.camera_inv_projection.inverse();
		state.camera_view = view;
		state.camera_inv_view = view.fastInverted();
		state.camera_view_projection = projection * view;
		state.camera_inv_view_projection = state.camera_view_projection;
		state.camera_inv_view_projection.inverse();
		state.time = m_timer->getTimeSinceStart();
		state.framebuffer_size.x = m_viewport.w;
		state.framebuffer_size.y = m_viewport.h;

		const EntityPtr global_light = m_scene->getActiveGlobalLight();
		if(global_light.isValid()) {
			EntityRef gl = (EntityRef)global_light;
			state.light_direction = Vec4(m_scene->getUniverse().getRotation(gl).rotate(Vec3(0, 0, -1)), 456); 
			state.light_color = m_scene->getGlobalLightColor(gl);
			state.light_intensity = m_scene->getGlobalLightIntensity(gl);
			state.light_indirect_intensity = m_scene->getGlobalLightIndirectIntensity(gl);
		}

		resetState();
		m_renderer.setGlobalState(state);
		
		LuaWrapper::DebugGuard lua_debug_guard(m_lua_state);
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "main");
		if (lua_type(m_lua_state, -1) != LUA_TFUNCTION) {
			lua_pop(m_lua_state, 2);
			return false;
		}

		LuaWrapper::pcall(m_lua_state, 0);
		lua_pop(m_lua_state, 1);
		return true;
	}


	void renderDebugLines()
	{
		struct Cmd : Renderer::RenderCommandBase
		{
			Cmd(IAllocator& allocator) : lines(allocator) {}

			void setup() override
			{
				const Array<DebugLine>& src_lines = pipeline->m_scene->getDebugLines();
				lines.resize(src_lines.size());
				copyMemory(&lines[0], &src_lines[0], lines.size() * sizeof(lines[0]));
				render_data = pipeline->m_debug_shape_shader->m_render_data;
			}


			void execute() override {
				ffr::pushDebugGroup("debug lines");
				const Shader::Program& shader = Shader::getProgram(render_data, 0);
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
				ffr::update(vb.buffer, vertices.begin(), vb.offset, vb.size);
				
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
		m_renderer.push(cmd);
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

		struct Cmd : Renderer::RenderCommandBase
		{
			Renderer::MemRef idx_buffer_mem;
			Renderer::MemRef vtx_buffer_mem;
			int num_indices;
			int num_vertices;
			Array<Draw2D::DrawCmd> cmd_buffer;

			Cmd(IAllocator& allocator) : cmd_buffer(allocator) {}

			void setup()
			{
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
				ffr::VertexDecl vertex_decl;
				vertex_decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
				vertex_decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
				vertex_decl.addAttribute(4, ffr::AttributeType::U8, true, false);
				
				ffr::BufferHandle vb = ffr::allocBufferHandle();
				ffr::BufferHandle ib = ffr::allocBufferHandle();
				ffr::createBuffer(vb, vtx_buffer_mem.size, vtx_buffer_mem.data);
				ffr::createBuffer(ib, idx_buffer_mem.size, idx_buffer_mem.data);
				pipeline->m_renderer.free(idx_buffer_mem);
				pipeline->m_renderer.free(vtx_buffer_mem);

				ffr::pushDebugGroup("draw2d");
				ffr::ProgramHandle prg = Shader::getProgram(shader, 0).handle;
				ffr::setUniform2f(pipeline->m_canvas_size_uniform, &size.x);
				ffr::setVertexBuffer(&vertex_decl, vb, 0, nullptr);
				ffr::setIndexBuffer(ib);

				u32 elem_offset = 0;
				const Draw2D::DrawCmd* pcmd_begin = cmd_buffer.begin();
				const Draw2D::DrawCmd* pcmd_end = cmd_buffer.end();
				ffr::setState(0);
				ffr::setUniform1i(pipeline->m_texture_uniform, 0);
				ffr::useProgram(prg);

				ASSERT(pcmd_begin <= pcmd_end - 1); // TODO compute correct offsets
				for (const Draw2D::DrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++) {
					if (0 == pcmd->ElemCount) continue;
			
					ffr::scissor(uint(Math::maximum(pcmd->ClipRect.x, 0.0f)),
						uint(Math::maximum(pcmd->ClipRect.y, 0.0f)),
						uint(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
						uint(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));
			
					const Texture* atlas_texture = pipeline->m_renderer.getFontManager().getAtlasTexture();
					ffr::TextureHandle texture_id = atlas_texture->handle;
					if (pcmd->TextureId) texture_id = *(ffr::TextureHandle*)pcmd->TextureId;
					if(!texture_id.isValid()) texture_id = atlas_texture->handle;

					ffr::bindTexture(0, texture_id);

					ffr::blending(1);
					ffr::drawTriangles(num_indices);

					elem_offset += pcmd->ElemCount;
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
		m_renderer.push(cmd);
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
		g_log_error.log("Renderer") << "Uknown texture format " << name;
		return ffr::TextureFormat::RGBA8;
	}


	int createRenderbuffer(float w, float h, bool relative, const char* format_str)
	{
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
		rb.handle = m_renderer.createTexture(rb_w, rb_h, format, 0, {0, 0});

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
		const char* define = LuaWrapper::checkArg<const char*>(L, 1);

		const CameraParams cp = checkCameraParams(L, 2);
 		
		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		RenderTerrainsCommand* cmd = LUMIX_NEW(allocator, RenderTerrainsCommand)(allocator);

		if (lua_gettop(L) > 3 && lua_istable(L, 3)) {
			lua_pushnil(L);
			while (lua_next(L, 3) != 0) {
				if(lua_type(L, -1) != LUA_TNUMBER) {
					g_log_error.log("Renderer") << "Incorrect global textures arguments of renderTerrains";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}

				if(lua_type(L, -2) != LUA_TSTRING) {
					g_log_error.log("Renderer") << "Incorrect global textures arguments of renderTerrains";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}
			
				if (cmd->m_global_textures_count > lengthOf(cmd->m_global_textures)) {
					g_log_error.log("Renderer") << "Too many textures in renderTerrains call";
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

		cmd->m_pipeline = pipeline;
		cmd->m_camera_params = cp;
		cmd->m_shader_define = define;
		pipeline->m_renderer.push(cmd);
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
		struct Cmd : Renderer::RenderCommandBase
		{
			Cmd(IAllocator& allocator) 
				: m_data(allocator) 
			{}

			void setup() override
			{
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
				ffr::pushDebugGroup("particles");
				InputBlob blob(m_data);
				ffr::VertexDecl instance_decl;
				instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);

				while(blob.getPosition() < blob.getSize()) {
					ShaderRenderData* shader_data = blob.read<ShaderRenderData*>();
					const int byte_size = blob.read<int>();
					const int instances_count = blob.read<int>();
					const Renderer::TransientSlice transient = m_pipeline->m_renderer.allocTransient(byte_size);
					if ((int)transient.size < byte_size) {
						g_log_warning.log("Renderer") << "Not enough memory reserved to render all particles.";
						break;
					}

					const void* mem = blob.skip(byte_size);
					ffr::update(transient.buffer, mem, transient.offset, byte_size);

					const Shader::Program& prog = Shader::getProgram(shader_data, 0);
					ffr::blending(0);
					ffr::useProgram(prog.handle);
					ffr::setInstanceBuffer(instance_decl, transient.buffer, transient.offset, 0);
					ffr::drawTriangleStripArraysInstanced(0, 4, instances_count);
				}
				ffr::popDebugGroup();
			}

			OutputBlob m_data;
			PipelineImpl* m_pipeline;
			CameraParams m_camera_params;
		};

		Cmd* cmd = LUMIX_NEW(pipeline->m_allocator, Cmd)(pipeline->m_allocator);
		cmd->m_pipeline = pipeline;
		cmd->m_camera_params = cp;

		pipeline->m_renderer.push(cmd);

		return 0;
	}


	struct CameraParams
	{
		ShiftedFrustum frustum;
		DVec3 pos;
		float lod_multiplier;
	};
	

	static CameraParams checkCameraParams(lua_State* L, int idx)
	{
		CameraParams cp;

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
		cp.frustum.setPlanesFromPoints();
		
		if(!LuaWrapper::checkField(L, idx, "lod_multiplier", &cp.lod_multiplier)) {
			luaL_error(L, "Missing lod_multiplier in camera params");
		}

		if(!LuaWrapper::checkField(L, idx, "position", &cp.pos)) {
			luaL_error(L, "Missing position in camera params");
		}

		return cp;
	}


	static int bindTextures(lua_State* L)
	{
		struct Cmd : Renderer::RenderCommandBase {
			void setup() override {}
			void execute() override 
			{
				for(int i = 0; i < m_textures_count; ++i) {
					ffr::bindTexture(m_offset + i, m_textures[i].handle);
					ffr::setUniform1i(m_textures[i].uniform, i + m_offset);
				}
			}

			struct { 
				ffr::TextureHandle handle;
				ffr::UniformHandle uniform;
			} m_textures[16];
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
		lua_pushnil(L);
		while (lua_next(L, 1) != 0) {
			if(lua_type(L, -1) != LUA_TNUMBER) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Incorrect texture arguments of bindTextures");
			}

			if(lua_type(L, -2) != LUA_TSTRING) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Incorrect texture arguments of bindTextures");
			}

			if (cmd->m_textures_count > lengthOf(cmd->m_textures)) {
				LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
				return luaL_error(L, "%s", "Too many texture in bindTextures call");
			}

			const char* uniform_name = lua_tostring(L, -2);
			cmd->m_textures[cmd->m_textures_count].uniform = ffr::allocUniform(uniform_name, ffr::UniformType::INT, 1);

			const int rb_idx = (int)lua_tointeger(L, -1);
			cmd->m_textures[cmd->m_textures_count].handle = pipeline->m_renderbuffers[rb_idx].handle;
			++cmd->m_textures_count;

			lua_pop(L, 1);
		}
		pipeline->m_renderer.push(cmd);

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
		
		struct Cmd : public Renderer::RenderCommandBase
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
				ffr::setState(u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::CULL_BACK));
				ffr::blending(2);
				const int irradiance_map_loc = ffr::getUniformLocation(prog.handle, m_irradiance_map_uniform);
				const int radiance_map_loc = ffr::getUniformLocation(prog.handle, m_radiance_map_uniform);
				const DVec3 cam_pos = m_camera_params.pos;
				for (const EnvProbeInfo& probe : m_probes) {
					const Vec4 pos_radius((probe.position - cam_pos).toFloat(), probe.radius);
					ffr::bindTexture(0, probe.radiance);
					ffr::applyUniform1i(irradiance_map_loc, 0);
					ffr::bindTexture(1, probe.radiance);
					ffr::applyUniform1i(radiance_map_loc, 1);
					ffr::applyUniform4f(pos_radius_uniform_loc, &pos_radius.x);
					ffr::drawTriangles(36);
				}
				ffr::blending(0);
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
			pipeline->m_renderer.push(cmd);
		}
		return 0;
	}

	
	static int renderMeshes(lua_State* L)
	{
		PROFILE_FUNCTION();
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const char* define = LuaWrapper::checkArg<const char*>(L, 1);
		LuaWrapper::checkTableArg(L, 2);

		const CameraParams cp = checkCameraParams(L ,2);

		IAllocator& allocator = pipeline->m_renderer.getAllocator();
		RenderMeshesCommand* cmd = LUMIX_NEW(allocator, RenderMeshesCommand)(allocator);

		if (lua_gettop(L) > 4 && lua_istable(L, 4)) {
			lua_pushnil(L);
			while (lua_next(L, 4) != 0) {
				if(lua_type(L, -1) != LUA_TNUMBER) {
					g_log_error.log("Renderer") << "Incorrect global textures arguments of renderMeshes";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}

				if(lua_type(L, -2) != LUA_TSTRING) {
					g_log_error.log("Renderer") << "Incorrect global textures arguments of renderMeshes";
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					lua_pop(L, 2);
					return 0;
				}
			
				if (cmd->m_global_textures_count > lengthOf(cmd->m_global_textures)) {
					g_log_error.log("Renderer") << "Too many textures in renderMeshes call";
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

		cmd->m_camera_params = cp;
		cmd->m_pipeline = pipeline;
		cmd->m_shader_define = define;
		pipeline->m_renderer.push(cmd);
		return 0;
	}


	static int drawArray(lua_State* L)
	{
		struct Cmd : Renderer::RenderCommandBase {
			void setup() override { m_render_data = m_shader->isReady() ? m_shader->m_render_data : nullptr; }
			void execute() override 
			{
				if (!m_render_data) return;
				ffr::ProgramHandle prg = Shader::getProgram(m_render_data, m_define_mask).handle;
				
				for(int i = 0; i < m_textures_count; ++i) {
					ffr::bindTexture(i, m_textures[i].handle);
					ffr::setUniform1i(m_textures[i].uniform, i);
				}

				for(int i = 0; i < m_uniforms_count; ++i) {
					ffr::setUniform4f(m_uniforms[i].handle, &m_uniforms[i].value.x);
				}

				ffr::setVertexBuffer(nullptr, ffr::INVALID_BUFFER, 0, nullptr);
				ffr::useProgram(prg);
				ffr::setState(0);
				ffr::setIndexBuffer(ffr::INVALID_BUFFER);
				ffr::drawArrays(m_indices_offset, m_indices_count, ffr::PrimitiveType::TRIANGLE_STRIP);
			}

			struct { 
				ffr::TextureHandle handle;
				ffr::UniformHandle uniform;
			} m_textures[16];
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
			lua_pushnil(L);
			while (lua_next(L, 4) != 0) {
				if(lua_type(L, -1) != LUA_TNUMBER) {
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					return luaL_error(L, "%s", "Incorrect texture arguments of drawArrays");
				}

				if(lua_type(L, -2) != LUA_TSTRING) {
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					return luaL_error(L, "%s", "Incorrect texture arguments of drawArrays");
				}

				if (cmd->m_textures_count > lengthOf(cmd->m_textures)) {
					LUMIX_DELETE(pipeline->m_renderer.getAllocator(), cmd);
					return luaL_error(L, "%s", "Too many texture in drawArray call");
				}

				const char* uniform_name = lua_tostring(L, -2);
				cmd->m_textures[cmd->m_textures_count].uniform = ffr::allocUniform(uniform_name, ffr::UniformType::INT, 1);

				const int rb_idx = (int)lua_tointeger(L, -1);
				cmd->m_textures[cmd->m_textures_count].handle = pipeline->m_renderbuffers[rb_idx].handle;
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
	
		cmd->m_shader = shader;
		cmd->m_indices_count = indices_count;
		cmd->m_indices_offset = indices_offset;
		pipeline->m_renderer.push(cmd);

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

		LuaWrapper::setField(L, -2, "position", params.pos);
		LuaWrapper::setField(L, -2, "lod_multiplier", params.lod_multiplier);
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
		pushCameraParams(L, cp);

		return 1;
	}


	static void findExtraShadowcasterPlanes(const Vec3& light_forward, const Frustum& camera_frustum, const DVec3& camera_position, Frustum* shadow_camera_frustum)
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
				if (dotProduct(camera_frustum_center, n) + d < 0)
				{
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
		ASSERT(false);
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


	static int getShadowCameraParams(lua_State* L)
	{
		/*const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int slice = LuaWrapper::checkArg<int>(L, 1);
		const int shadowmap_width = LuaWrapper::checkArg<int>(L, 2);
		
		RenderScene* scene = pipeline->m_scene;
		
		const Universe& universe = scene->getUniverse();
		const EntityPtr light = scene->getActiveGlobalLight();
		const Vec4 cascades = light.isValid() ? scene->getShadowmapCascades((EntityRef)light) : Vec4(3, 10, 60, 150);
		const Matrix light_mtx = light.isValid() ? universe.getMatrix((EntityRef)light) : Matrix::IDENTITY;

		const float camera_height = (float)pipeline->m_viewport.h;
		const float camera_fov = pipeline->m_viewport.fov;
		const float camera_ratio = pipeline->m_viewport.w / camera_height;
		const float split_distances[] = {0.1f, cascades.x, cascades.y, cascades.z, cascades.w};
		
		Frustum camera_frustum;
		camera_frustum.computePerspective(pipeline->m_viewport.pos,
			pipeline->m_viewport.rot * Vec3(0, 0, -1),
			pipeline->m_viewport.rot * Vec3(0, 1, 0),
			camera_fov,
			camera_ratio,
			split_distances[slice],
			split_distances[slice + 1]);

		const Sphere frustum_bounding_sphere = camera_frustum.computeBoundingSphere();
		const float bb_size = frustum_bounding_sphere.radius;
		const Vec3 light_forward = light_mtx.getZVector();
		
		Vec3 shadow_cam_pos = frustum_bounding_sphere.position;
		shadow_cam_pos = shadowmapTexelAlign(shadow_cam_pos, 0.5f * shadowmap_width - 2, bb_size, light_mtx);

		Renderer::GlobalState global_state = pipeline->m_renderer.getGlobalState();

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
		pipeline->m_renderer.setGlobalState(global_state);

		CameraParams cp;
		cp.lod_multiplier = 1;
		cp.pos = pipeline->m_viewport.pos;
		cp.frustum.computeOrtho(shadow_cam_pos
			, -light_forward
			, light_mtx.getYVector()
			, bb_size
			, bb_size
			, SHADOW_CAM_NEAR
			, SHADOW_CAM_FAR);

		findExtraShadowcasterPlanes(light_forward, camera_frustum, pipeline->m_viewport.pos, &cp.frustum);

		pushCameraParams(L, cp);
		return 1;*/

		// TODO
		ASSERT(false);
		return 0;
	}


	


	static int setRenderTargets(lua_State* L)
	{ 
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, pipeline_idx );
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int rb_count = lua_gettop(L);
		int rbs[16];
		if(rb_count > lengthOf(rbs)) {
			g_log_error.log("Renderer") << "Too many render buffers in " << pipeline->getPath();	
			return 0;
		}
		if(rb_count <= 0) {
			g_log_error.log("Renderer") << "createFramebuffer without arguments in " << pipeline->getPath();
			return 0;
		}

		struct Cmd : Renderer::RenderCommandBase
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
		pipeline->m_renderer.push(cmd);

		return 0;
	}


	void renderModel(Model& model, const Matrix& mtx) override
	{
		for(int i = 0; i < model.getMeshCount(); ++i) {

			const Mesh& mesh = model.getMesh(i);
			const Material* material = mesh.material;
			ShaderRenderData* shader_rd = material->getShader()->m_render_data;
			const Shader::Program& prog = Shader::getProgram(shader_rd, 0); // TODO define
			const int textures_count = material->getTextureCount();

			if(!prog.handle.isValid()) continue;

			for(int i = 0; i < textures_count; ++i) {
				ffr::bindTexture(i, material->getTexture(i)->handle);
				ffr::setUniform1i(material->getTextureUniform(i), i);
			}

			int attribute_map[16];
			const Mesh::RenderData* rd = mesh.render_data;
			for(uint i = 0; i < rd->vertex_decl.attributes_count; ++i) {
				attribute_map[i] = prog.attribute_by_semantics[(int)rd->attributes_semantic[i]];
			}
			
			ffr::setUniformMatrix4f(m_model_uniform, &mtx.m11);
			ffr::useProgram(prog.handle);
			ffr::setVertexBuffer(&rd->vertex_decl, rd->vertex_buffer_handle, 0, prog.use_semantics ? attribute_map : nullptr);
			ffr::setIndexBuffer(rd->index_buffer_handle);
			ffr::setState(u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::DEPTH_WRITE) | material->getRenderStates());
			ffr::drawTriangles(rd->indices_count);
		}
	}

	struct RenderTerrainsCommand : Renderer::RenderCommandBase
	{
		RenderTerrainsCommand(IAllocator& allocator)
			: m_allocator(allocator)
			, m_instance_data(allocator)
			, m_batches(allocator)
		{
		}

		void setup() override
		{
			/*PROFILE_FUNCTION();
			Array<TerrainInfo> infos(m_allocator);
			m_pipeline->m_scene->getTerrainInfos(m_camera_params.frustum, m_camera_params.pos, infos);

			if (infos.empty()) return;

			m_define_mask = m_shader_define.empty() 
				? 0
				: 1 << m_pipeline->m_renderer.getShaderDefineIdx(m_shader_define);

			std::sort(infos.begin(), infos.end(), [](const TerrainInfo& a, const TerrainInfo& b) {
				if (a.terrain == b.terrain) return a.index < b.index;
				return a.terrain < b.terrain;
			});

			m_instance_data.resize(infos.size());
			Terrain* prev_terrain = nullptr;
			int prev_idx = -1;
			int prev_submesh = -1;
			for (int i = 0, c = infos.size(); i < c; ++i) {
				const TerrainInfo& info = infos[i];
				if (info.terrain != prev_terrain || prev_submesh != info.index) {
					if (prev_terrain) {
						Batch& b = m_batches.emplace();
						b.terrain = prev_terrain;
						b.shader = infos[prev_idx].shader->m_render_data;
						b.matrix = infos[prev_idx].rot.toMatrix();
						b.matrix.setTranslation((infos[prev_idx].position - m_camera_params.pos).toFloat());
						b.submesh = infos[prev_idx].index;
						b.from = prev_idx;
						b.to = i - 1;
					}
					prev_idx = i;
					prev_terrain = info.terrain;
					prev_submesh = info.index;
				}
				m_instance_data[i].size = info.size;
				m_instance_data[i].quad_min = info.min;
				m_instance_data[i].morph_consts = info.morph_const;
			}
			Batch& b = m_batches.emplace();
			b.terrain = prev_terrain;
			b.shader = infos[prev_idx].shader->m_render_data;
			b.matrix = infos[prev_idx].rot.toMatrix();
			b.matrix.setTranslation((infos[prev_idx].position - m_camera_params.pos).toFloat());
			b.submesh = infos[prev_idx].index;
			b.from = prev_idx;
			b.to = infos.size() - 1;*/
			// TODO
			//ASSERT(false);
		}

		void execute() override
		{
			/*if(m_instance_data.empty()) return;

			ffr::pushDebugGroup("terrains");
			Renderer::TransientSlice instance_buffer = m_pipeline->m_renderer.allocTransient(m_instance_data.byte_size());
			ffr::update(instance_buffer.buffer, m_instance_data.begin(), 0, m_instance_data.byte_size());

			ffr::VertexDecl decl;
			decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
			decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
			
			ffr::VertexDecl instance_decl;
			instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
			instance_decl.addAttribute(1, ffr::AttributeType::FLOAT, false, false);
			instance_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);

			const DVec3 camera_pos = m_camera_params.pos;

			for (const Batch& batch : m_batches) {
				Texture* detail_texture = batch.terrain->getDetailTexture();
				if (!detail_texture) continue;
				Texture* splat_texture = batch.terrain->getSplatmap();
				if (!splat_texture) continue;

				const Matrix inv_world_matrix = batch.matrix.fastInverted();
				const Vec4 rel_cam_pos(inv_world_matrix.transformPoint(camera_pos) / batch.terrain->getXZScale(), 1);
				const Vec4 terrain_scale(batch.terrain->getScale(), 0);
				const Vec4 terrain_params(batch.terrain->getRootSize()
					, (float)detail_texture->width
					, (float)splat_texture->width
					, 0);
				ffr::setUniform4f(m_pipeline->m_terrain_params_uniform, &terrain_params.x);
				ffr::setUniform4f(m_pipeline->m_rel_camera_pos_uniform, &rel_cam_pos.x);
				ffr::setUniform4f(m_pipeline->m_terrain_scale_uniform, &terrain_scale.x);
				ffr::setUniformMatrix4f(m_pipeline->m_terrain_matrix_uniform, &batch.matrix.m11);

				const ffr::ProgramHandle prg = Shader::getProgram(batch.shader, m_define_mask).handle;
				ffr::useProgram(prg);
				/*
				for (int i = 0; i < m_global_textures_count; ++i) {
					const auto& t = m_global_textures[i];
					ffr::bindTexture(i, t.texture);
					ffr::setUniform1i(t.uniform, i);
				}
				*//*
				const Material* material = batch.terrain->m_material;
				const int textures_count = material->getTextureCount();
				for (int i = 0; i < textures_count; ++i) {
					ffr::bindTexture(i + 0, material->getTexture(i)->handle);
					ffr::setUniform1i(material->getTextureUniform(i), i + 0);
				}

				const Mesh& mesh = *batch.terrain->getMesh();
				ffr::setVertexBuffer(&decl, mesh.vertex_buffer_handle, 0, nullptr);
				ffr::setInstanceBuffer(instance_decl, instance_buffer.buffer, instance_buffer.offset + batch.from * sizeof(m_instance_data[0]), 2);
				ffr::setIndexBuffer(mesh.index_buffer_handle);
				ffr::setState(u64(ffr::StateFlags::DEPTH_WRITE) | u64(ffr::StateFlags::DEPTH_TEST) | batch.terrain->m_material->getRenderStates());
				const int submesh_indices_count = mesh.indices_count / 4;
				ffr::drawTrianglesInstanced(batch.submesh * submesh_indices_count * sizeof(u16), submesh_indices_count , 1 + batch.to - batch.from);
			}
			ffr::popDebugGroup();*/
			// TODO
			//ASSERT(false);
		}

		struct InstanceData
		{
			Vec3 quad_min;
			float size;
			Vec3 morph_consts;
		};

		struct Batch
		{
			Terrain* terrain;
			ShaderRenderData* shader;
			Matrix matrix;
			uint submesh;
			uint from;
			uint to;
		};

		IAllocator& m_allocator;
		PipelineImpl* m_pipeline;
		CameraParams m_camera_params;
		StaticString<32> m_shader_define;
		u32 m_define_mask;
		Array<InstanceData> m_instance_data;
		Array<Batch> m_batches;
		struct {
			ffr::TextureHandle texture;
			ffr::UniformHandle uniform;
		} m_global_textures[16];
		int m_global_textures_count = 0;

	};


	struct RenderMeshesCommand : Renderer::RenderCommandBase
	{
		RenderMeshesCommand(IAllocator& allocator) 
			: m_allocator(allocator)
			, m_cmds(allocator)
		{}


		void radixSort(u64* _keys, u32* _values, int size)
		{
			PROFILE_FUNCTION();
			PROFILE_INT("count", size);
			if(size == 0) return;
			// from https://github.com/bkaradzic/bx
			enum {
				RADIXSORT_BITS = 11,
				RADIXSORT_HISTOGRAM_SIZE = 1 << RADIXSORT_BITS,
				RADIXSORT_BIT_MASK = RADIXSORT_HISTOGRAM_SIZE - 1
			};

			Array<u64> tmp_keys(m_allocator); // TODO more suitable allocator
			Array<u32> tmp_values(m_allocator);
			tmp_keys.resize(size);
			tmp_values.resize(size);

			u64* keys = _keys;
			u64* tempKeys = tmp_keys.begin();
			u32* values = _values;
			u32* tempValues = tmp_values.begin();

			u32 histogram[RADIXSORT_HISTOGRAM_SIZE];
			u16 shift = 0;
			for (int pass = 0; pass < 5; ++pass) {
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

				u32* const swapValues = tempValues;
				tempValues = values;
				values = swapValues;

				shift += RADIXSORT_BITS;
			}
		}


		void fillCommands(const Array<Array<u32>>& renderables, Array<Array<u8>>& cmds)
		{
			int renderables_count = 0;
			for (const auto& r : renderables) {
				renderables_count += r.size();
			}

			if(renderables_count == 0) return;

			struct CreateSortKeys {
				CreateSortKeys() {
					job.data = this;
					job.task = [](void* data){
						PROFILE_BLOCK("sort_keys");
						CreateSortKeys* ctx = (CreateSortKeys*)data;
						PROFILE_INT("num", ctx->count);

						const RenderScene* scene = ctx->cmd->m_pipeline->m_scene;
						const ModelInstance* model_instances = scene->getModelInstances();
						const u32* renderables = ctx->renderables;
						u64* sort_keys = ctx->sort_keys;
						for (int i = 0, c = ctx->count; i < c; ++i) {
							const EntityRef e = {int(renderables[i] & 0x00ffFFff)};
							const ModelInstance& mi = model_instances[e.index];
							sort_keys[i] = mi.meshes[0].sort_key;
						}
					};
				}
				u64* sort_keys;
				u32* renderables;
				int count;
				JobSystem::JobDecl job;
				RenderMeshesCommand* cmd;
			};

			Array<u64> sort_keys(m_allocator);
			sort_keys.resize(renderables_count);

			{
				Array<CreateSortKeys> create_sort_keys(m_allocator);
				create_sort_keys.reserve(renderables.size());
				volatile int sort_counter = 0;
				int sort_offset = 0;
				for(int i = 0; i < renderables.size(); ++i) {
					if (renderables.empty()) continue;
					CreateSortKeys& ctx = create_sort_keys.emplace();
					ctx.renderables = renderables[i].begin();
					ctx.sort_keys = sort_keys.begin() + sort_offset;
					ctx.count = renderables[i].size();
					ctx.cmd = this;
					sort_offset += renderables[i].size();
					JobSystem::runJobs(&ctx.job, 1, &sort_counter);
				}
				JobSystem::wait(&sort_counter);
			}

			{
				Array<u32> merged_renderables(m_allocator);
				merged_renderables.resize(renderables_count);

				{
					PROFILE_BLOCK("merge & sort")
					int sort_offset = 0;
					for(int i = 0; i < renderables.size(); ++i) {
						memcpy(merged_renderables.begin() + sort_offset, renderables[i].begin(), renderables[i].byte_size());
						sort_offset += renderables[i].size();
					}

					radixSort(sort_keys.begin(), merged_renderables.begin(), sort_keys.size());
				}

				struct CreateCommands {
					CreateCommands()
					{
						job.data = this;
						job.task = [](void* data){
							PROFILE_BLOCK("create cmds");
							CreateCommands* ctx = (CreateCommands*)data;
							PROFILE_INT("num", ctx->count);
							const Universe& universe = ctx->cmd->m_pipeline->m_scene->getUniverse();
							ctx->output->resize(ctx->count * (sizeof(Matrix) + sizeof(Mesh*) + sizeof(Material) + sizeof(u16)));
							u8* out = ctx->output->begin();
							const u32* LUMIX_RESTRICT renderables = ctx->renderables;
							const u64* LUMIX_RESTRICT sort_keys = ctx->sort_keys;
							const ModelInstance* LUMIX_RESTRICT model_instances = ctx->cmd->m_pipeline->m_scene->getModelInstances();
							const Universe::EntityData* LUMIX_RESTRICT entity_data = universe.getEntityData(); 
							const DVec3 camera_pos = ctx->camera_pos;
							for (int i = 0, c = ctx->count; i < c; ++i) {
								const EntityRef e = {int(renderables[i] & 0x00ffFFff)};
								const ModelInstance* LUMIX_RESTRICT mi = &model_instances[e.index];
								*(Mesh::RenderData**)out = mi->meshes[0].render_data;
								out += sizeof(Mesh::RenderData*);
								*(Material::RenderData**)out = mi->meshes[0].material->getRenderData();
								out += sizeof(Material::RenderData*);
								u16* instance_count = (u16*)out;
								int start_i = i;
								out += sizeof(*instance_count);
								const u64 key = sort_keys[i];
								while (i < c && sort_keys[i] == key) {
									const EntityRef e = {int(renderables[i] & 0x00ffFFff)};
									const Transform& tr = entity_data[e.index].transform;
									Matrix mtx = tr.rot.toMatrix();
									mtx.multiply3x3(tr.scale);
									mtx.setTranslation((tr.pos - camera_pos).toFloat());
									memcpy(out, &mtx, sizeof(mtx));
									out += sizeof(mtx);
									++i;
								}
								*instance_count = u16(i - start_i);
								--i;
							}
						};
					}

					u32* renderables;
					u64* sort_keys;
					Array<u8>* output;
					DVec3 camera_pos;
					int count;
					RenderMeshesCommand* cmd;
					JobSystem::JobDecl job;
				};
			
				{
					Array<CreateCommands> create_commands(m_allocator);
					const int job_count = Math::minimum(merged_renderables.size(), 16);
					create_commands.reserve(job_count);
					volatile int counter = 0;
					int offset = 0;
					const int step = (merged_renderables.size() + job_count - 1) / job_count;
					cmds.reserve(job_count);
					for(int i = 0; i < job_count; ++i) {
						CreateCommands& ctx = create_commands.emplace();
						ctx.renderables = merged_renderables.begin() + offset;
						ctx.sort_keys = sort_keys.begin() + offset;
						ctx.count = Math::minimum(step, merged_renderables.size() - offset);
						ctx.cmd = this;
						ctx.camera_pos = m_camera_params.pos;
						ctx.output = &cmds.emplace(m_allocator);
						offset += step;
						JobSystem::runJobs(&ctx.job, 1, &counter);
					}
					JobSystem::wait(&counter);
				}
			}
		}

		void setup() override
		{
			if(!m_pipeline->m_scene) return;

			Renderer& renderer = m_pipeline->m_renderer;
			const u32 define_mask = m_shader_define.empty() 
				? 0
				: 1 << renderer.getShaderDefineIdx(m_shader_define);
			const RenderScene* scene = m_pipeline->getScene();
			Array<Array<u32>> renderables(renderer.getAllocator());
			scene->getRenderables(m_camera_params.frustum, renderables);
			if (renderables.empty()) return;
			
			Array<u64> sort_keys(m_allocator);
			
			fillCommands(renderables, m_cmds);

//			radixSort(sort_keys.begin(), m_offsets.begin(), sort_keys.size());

			// TODO
			/*MT::atomicAdd(&m_pipeline->m_stats.draw_call_count, meshes.size());
			const EntityPtr probe = scene->getNearestEnvironmentProbe(m_pipeline->m_viewport.pos);
			if (probe.isValid()) {
				const Texture* irradiance = scene->getEnvironmentProbeIrradiance((EntityRef)probe);
				const Texture* radiance = scene->getEnvironmentProbeRadiance((EntityRef)probe);
				m_irradiance_map = irradiance->handle;
				m_radiance_map = radiance->handle;
			}
			else {
				m_irradiance_map = m_pipeline->m_default_cubemap->handle;
				m_radiance_map = m_pipeline->m_default_cubemap->handle;
			}*/
			m_irradiance_map = m_pipeline->m_default_cubemap->handle;
			m_radiance_map = m_pipeline->m_default_cubemap->handle;
		}


		void execute() override
		{
			PROFILE_FUNCTION();
			if(m_cmds.empty()) return;

			ffr::pushDebugGroup(m_shader_define.empty() ? "meshes" : m_shader_define);
			ffr::setUniform1i(m_pipeline->m_irradiance_map_uniform, 0);
			ffr::setUniform1i(m_pipeline->m_radiance_map_uniform, 1);

			ffr::bindTexture(0, m_irradiance_map);
			ffr::bindTexture(1, m_radiance_map);

			for (int i = 0; i < m_global_textures_count; ++i) {
				const auto& t = m_global_textures[i];
				ffr::bindTexture(2 + i, t.texture);
				ffr::setUniform1i(t.uniform, 2 + i);
			}
			
			ffr::VertexDecl instance_decl;
			instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
			instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
			instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);
			instance_decl.addAttribute(4, ffr::AttributeType::FLOAT, false, false);

			Renderer& renderer = m_pipeline->m_renderer;

			int drawcalls_count = 0;
			
			for (Array<u8>& cmds : m_cmds) {
				const u8* cmd = cmds.begin();
				Mesh::RenderData* mesh = *(Mesh::RenderData**)cmd;
				cmd += sizeof(mesh);
				Material::RenderData* material = *(Material::RenderData**)cmd;
				cmd += sizeof(mesh);
				u16 instances_count = *(u16*)cmd;
				cmd += sizeof(instances_count);
				float* matrices = (float*)cmd;
				cmd += sizeof(Matrix) * instances_count;

				ShaderRenderData* shader = material->shader;
				for (int i = 0; i < material->textures_count; ++i) {
					const ffr::TextureHandle handle = material->textures[i];
					const ffr::UniformHandle uniform = shader->texture_uniforms[i];
					ffr::bindTexture(i + 2 + m_global_textures_count, handle);
					ffr::setUniform1i(uniform, i + 2 + m_global_textures_count);
				}

				ffr::setState(material->render_states | u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::DEPTH_WRITE)); // TODO
				const u32 define_mask = 1 << m_pipeline->m_renderer.getShaderDefineIdx("INSTANCED")
					| 1 << m_pipeline->m_renderer.getShaderDefineIdx("DEFERRED"); // TODO
				const Shader::Program& prog = Shader::getProgram(shader, define_mask);
				if(prog.handle.isValid()) {
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

					const Renderer::TransientSlice instance_buffer = m_pipeline->m_renderer.allocTransient(instances_count * sizeof(Matrix));
	
					ffr::update(instance_buffer.buffer, matrices, instance_buffer.offset, instance_buffer.size);
					ffr::setInstanceBuffer(instance_decl, instance_buffer.buffer, instance_buffer.offset, mesh->vertex_decl.attributes_count);
					ffr::drawTrianglesInstanced(0, mesh->indices_count, instances_count);
				}
			}
//			PROFILE_INT("drawcalls", drawcalls_count);
			ffr::popDebugGroup();
		}


		IAllocator& m_allocator;
		CameraParams m_camera_params;
		PipelineImpl* m_pipeline;
		Array<Array<u8>> m_cmds;
		ffr::TextureHandle m_irradiance_map;
		ffr::TextureHandle m_radiance_map;
		StaticString<32> m_shader_define;
		struct {
			ffr::TextureHandle texture;
			ffr::UniformHandle uniform;
		} m_global_textures[16];
		int m_global_textures_count = 0;
	};


	void blending(const char* mode)
	{
		struct Cmd : Renderer::RenderCommandBase
		{
			void setup() override {}
			void execute() override
			{
				ffr::blending(mode);
			}
			int mode;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->mode = mode[0] ? 1 : 0;
		m_renderer.push(cmd);

	}


	void clear(u32 flags, float r, float g, float b, float a, float depth)
	{
		struct Cmd : Renderer::RenderCommandBase {
			void setup() override {}
			void execute() override {
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
		m_renderer.push(cmd);
	}


	void viewport(int x, int y, int w, int h)
	{
		struct Cmd : Renderer::RenderCommandBase {
			void setup() override {}
			void execute() override { ffr::viewport(x, y, w, h); }
			int x, y, w, h;
		};

		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->x = x;
		cmd->y = y;
		cmd->w = w;
		cmd->h = h;

		m_renderer.push(cmd);
	}


	void beginBlock(const char* name)
	{
		struct Cmd : Renderer::RenderCommandBase
		{
			void setup() override {}
			void execute() override 
			{
				ffr::pushDebugGroup(name);
				renderer->beginProfileBlock(name);
			}
			StaticString<32> name;
			Renderer* renderer;
		};
		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->name = name;
		cmd->renderer = &m_renderer;
		m_renderer.push(cmd);
	}


	void endBlock()
	{
		struct Cmd : Renderer::RenderCommandBase
		{
			void setup() override {}
			void execute() override
			{
				renderer->endProfileBlock();
				ffr::popDebugGroup();
			}
			Renderer* renderer;
		};
		Cmd* cmd = LUMIX_NEW(m_renderer.getAllocator(), Cmd);
		cmd->renderer = &m_renderer;
		m_renderer.push(cmd);
	}


	void setStencil(uint write_mask, uint func, int ref, uint mask, uint sfail, uint zfail, uint zpass) 
	{
		struct Cmd : Renderer::RenderCommandBase
		{
			void setup() {}
			void execute() override 
			{
				ffr::setStencil(write_mask, (ffr::StencilFuncs)func, ref, mask, (ffr::StencilOps)sfail, (ffr::StencilOps)zfail, (ffr::StencilOps)zpass);
			}

			uint write_mask;
			uint func;
			int ref;
			uint mask;
			uint sfail;
			uint zfail;
			uint zpass;
		};

		IAllocator& allocator = m_renderer.getAllocator();
		Cmd* cmd = LUMIX_NEW(allocator, Cmd);
		cmd->write_mask = write_mask;
		cmd->func = func;
		cmd->ref = ref;
		cmd->mask = mask;
		cmd->sfail = sfail;
		cmd->zfail = zfail;
		cmd->zpass = zpass;
		m_renderer.push(cmd);
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
			g_log_warning.log("Renderer") << lua_tostring(m_lua_state, -1);
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
		REGISTER_FUNCTION(blending);
		REGISTER_FUNCTION(clear);
		REGISTER_FUNCTION(createRenderbuffer);
		REGISTER_FUNCTION(endBlock);
		REGISTER_FUNCTION(executeCustomCommand);
		REGISTER_FUNCTION(preloadShader);
		REGISTER_FUNCTION(render2D);
		REGISTER_FUNCTION(renderDebugShapes);
		REGISTER_FUNCTION(setOutput);
		REGISTER_FUNCTION(setStencil);
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
		registerCFunction("getCameraParams", PipelineImpl::getCameraParams);
		registerCFunction("getShadowCameraParams", PipelineImpl::getShadowCameraParams);
		registerCFunction("renderEnvProbeVolumes", PipelineImpl::renderEnvProbeVolumes);
		registerCFunction("renderMeshes", PipelineImpl::renderMeshes);
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
	Texture* m_default_cubemap;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<Renderbuffer> m_renderbuffers;
	Array<ShaderRef> m_shaders;
	Timer* m_timer;

	ffr::UniformHandle m_position_radius_uniform;
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


} // namespace Lumix