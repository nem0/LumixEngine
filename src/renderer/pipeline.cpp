#include "draw2d.h"
#include "ffr/ffr.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/ifile_device.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/universe/universe.h"
#include "engine/viewport.h"
#include "font_manager.h"
#include "material.h"
#include "model.h"
#include "pipeline.h"
#include "renderer.h"
#include "render_scene.h"
#include "shader.h"
#include "shader_manager.h"
#include "texture.h"
#include "texture_manager.h"
#include <cmath>


namespace Lumix
{


static const float SHADOW_CAM_NEAR = 50.0f;
static const float SHADOW_CAM_FAR = 5000.0f;


struct PipelineImpl LUMIX_FINAL : Pipeline
{
	PipelineImpl(Renderer& renderer, const Path& path, const char* define, IAllocator& allocator)
		: m_allocator(allocator)
		, m_renderer(renderer)
		, m_path(path)
		, m_lua_state(nullptr)
		, m_is_ready(false)
		, m_custom_commands_handlers(allocator)
		, m_define(define)
		, m_scene(nullptr)
		, m_is_first_render(true)
		, m_draw2d(allocator)
		, m_output(-1)
		, m_renderbuffers(allocator)
		, m_shaders(allocator)
		, m_global_textures(allocator)
		, m_commands(allocator)
	{
		m_viewport.w = m_viewport.h = 800;
		ShaderManager& shader_manager = renderer.getShaderManager();
		m_draw2d_shader = (Shader*)shader_manager.load(Path("pipelines/draw2d.shd"));
		m_debug_shape_shader = (Shader*)shader_manager.load(Path("pipelines/debug_shape.shd"));
		TextureManager& texture_manager = renderer.getTextureManager();
		m_default_cubemap = (Texture*)texture_manager.load(Path("models/common/default_probe.dds"));

		FontAtlas& font_atlas = m_renderer.getFontManager().getFontAtlas();
		m_draw2d.FontTexUvWhitePixel = font_atlas.TexUvWhitePixel;
		m_draw2d.Clear();
		m_draw2d.PushClipRectFullScreen();
		m_draw2d.PushTextureID(font_atlas.TexID);
	}


	~PipelineImpl()
	{
		m_draw2d_shader->getResourceManager().unload(*m_draw2d_shader);
		m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
		m_default_cubemap->getResourceManager().unload(*m_default_cubemap);

		for(ShaderRef& shader : m_shaders) {
			shader.res->getResourceManager().unload(*shader.res);
		}
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


	void load() override 
	{
		auto& fs = m_renderer.getEngine().getFileSystem();
		Delegate<void(FS::IFile&, bool)> cb;
		cb.bind<PipelineImpl, &PipelineImpl::onFileLoaded>(this);
		fs.openAsync(fs.getDefaultDevice(), m_path, FS::Mode::OPEN_AND_READ, cb);
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

		bool errors = luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp.data), m_path.c_str()) != 0;
		if (errors)
		{
			g_log_error.log("Renderer") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			g_log_error.log("Renderer") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
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


	void onFileLoaded(FS::IFile& file, bool success)
	{
		if (!success)
		{
			g_log_error.log("Renderer") << "Failed to load " << m_path;
			return;
		}

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

		bool errors =
			luaL_loadbuffer(m_lua_state, (const char*)file.getBuffer(), file.size(), m_path.c_str()) != 0;
		if (errors)
		{
			g_log_error.log("Renderer") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setfenv(m_lua_state, -2);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != 0;
		if (errors)
		{
			g_log_error.log("Renderer") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		m_viewport.w = m_viewport.h = 800;
		if (m_scene) callInitScene();
		
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_getfield(m_lua_state, -1, "main");
		if (lua_type(m_lua_state, -1) == LUA_TFUNCTION) {
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != 0) {
				g_log_warning.log("Renderer") << lua_tostring(m_lua_state, -1);
				lua_pop(m_lua_state, 1);
				return;
			}
		}
		else {
			lua_pop(m_lua_state, 1);
		}

		m_is_ready = true;
	}


	void clearBuffers()
	{
		m_global_textures.clear();
		/*
		for (Renderbuffer& rb : m_renderbuffers) {
			++rb.frame_counter;
		}

		for(int i = m_renderbuffers.size() - 1; i >= 0; --i) {
			if(m_renderbuffers[i].frame_counter > 1) {
				ffr::destroy(m_renderbuffers[i].handle);
				m_renderbuffers.eraseFast(i);
			}
		}*/
	}


	virtual void setViewport(const Viewport& viewport) override { m_viewport = viewport; }


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
		state.camera_pos = Vec4(m_viewport.pos, 1);

		const Matrix view = m_viewport.getView();
		const Matrix projection = m_viewport.getProjection(ffr::isHomogenousDepth());
		state.camera_projection = projection;
		state.camera_view = view;
		state.camera_view_projection = projection * view;
		state.camera_inv_view_projection = state.camera_view_projection;
		state.camera_inv_view_projection.inverse();

		const Entity global_light = m_scene->getActiveGlobalLight();
		if(global_light.isValid()) {
			state.light_direction = Vec4(m_scene->getUniverse().getRotation(global_light).rotate(Vec3(0, 0, -1)), 456); 
			state.light_color = m_scene->getGlobalLightColor(global_light);
			state.light_intensity = m_scene->getGlobalLightIntensity(global_light);
			state.light_indirect_intensity = m_scene->getGlobalLightIndirectIntensity(global_light);
		}

		m_renderer.setGlobalState(state);
		for(auto* cmd : m_commands) {
			m_renderer.push(cmd);
		}

/*		
		m_global_light_shadowmap = nullptr;
		m_layer_mask = 0;
		m_current_framebuffer = m_default_framebuffer;
		m_instance_data_idx = 0;
		m_point_light_shadowmaps.clear();
		*/
		clearBuffers();
		return true;
	}


	void renderDebugLines()
	{
		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_shape_shader->isReady()) return;

		const Shader::Program& shader = m_debug_shape_shader->getProgram(0);
		struct BaseVertex {
			Vec3 pos;
			u32 color;
		};

		Array<BaseVertex> vertices(m_allocator);
		vertices.resize(lines.size() * 2);
		for (int j = 0, n = lines.size(); j < n; ++j) {
			const DebugLine& line = lines[j];

			vertices[j * 2].color = line.color;
			vertices[j * 2].pos = line.from;

			vertices[j * 2 + 1].color = line.color;
			vertices[j * 2 + 1].pos = line.to;
		}

		ffr::VertexDecl vertex_decl;
		vertex_decl.addAttribute(3, ffr::AttributeType::FLOAT, false, false);
		vertex_decl.addAttribute(4, ffr::AttributeType::U8, true, false);

		ffr::setUniformMatrix4f(shader.handle, "u_model", 1, &Matrix::IDENTITY.m11);

		// TODO do not create every frame
		const ffr::BufferHandle vb = ffr::allocBufferHandle();
		ffr::createBuffer(vb, vertices.size() * sizeof(vertices[0]), &vertices[0]);
		ffr::DrawCall dc;
		dc.attribute_map = nullptr;
		dc.index_buffer = ffr::INVALID_BUFFER;
		dc.primitive_type = ffr::PrimitiveType::LINES;
		dc.shader = shader.handle;
		dc.state = 0;
		dc.textures_count = 0;
		dc.vertex_buffer = vb;
		dc.vertex_buffer_offset = 0;
		dc.vertex_decl = &vertex_decl;
		dc.indices_offset = 0;
		dc.indices_count = lines.size() * 2;
		ffr::draw(dc);

		ffr::destroy(vb);
	}


	void renderDebugShapes()
	{
		renderDebugLines();
		/*renderDebugTriangles();
		renderDebugPoints();*/
	}


	void render2D()
	{
		/*auto resetDraw2D =  [this](){
			m_draw2d.Clear();
			m_draw2d.PushClipRectFullScreen();
			FontAtlas& atlas = m_renderer.getFontManager().getFontAtlas();
			m_draw2d.FontTexUvWhitePixel = atlas.TexUvWhitePixel;
			m_draw2d.PushTextureID(atlas.TexID);
		};

		Vec2 size((float)m_viewport.w, (float)m_viewport.h);
		Matrix ortho;

		if (!m_draw2d_shader->isReady()) {
			resetDraw2D();
			return;
		}

		const int num_indices = m_draw2d.IdxBuffer.size();
		const int num_vertices = m_draw2d.VtxBuffer.size();
		
		if (num_indices == 0) {
			resetDraw2D();
			return;
		}

		ffr::VertexDecl vertex_decl;
		vertex_decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
		vertex_decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
		vertex_decl.addAttribute(4, ffr::AttributeType::U8, true, false);

		ffr::DrawCall dc;
		dc.attribute_map = nullptr;
		dc.vertex_buffer = ffr::createBuffer(num_vertices * vertex_decl.size, &m_draw2d.VtxBuffer[0]);
		dc.index_buffer = ffr::createBuffer(num_indices * sizeof(u16), &m_draw2d.IdxBuffer[0]);
		dc.vertex_decl = &vertex_decl;

		ffr::pushDebugGroup("draw2d");
		ffr::ProgramHandle prg = m_draw2d_shader->getProgram(0).handle;
		ffr::setUniform2f(prg, "u_canvas_size", 1, &size.x);

		u32 elem_offset = 0;
		const Draw2D::DrawCmd* pcmd_begin = m_draw2d.CmdBuffer.begin();
		const Draw2D::DrawCmd* pcmd_end = m_draw2d.CmdBuffer.end();
		
		ASSERT(pcmd_begin <= pcmd_end - 1); // TODO compute correct offsets
		for (const Draw2D::DrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++) {
			if (0 == pcmd->ElemCount) continue;
			
			ffr::scissor(uint(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				uint(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));
			
			const Texture* atlas_texture = m_renderer.getFontManager().getAtlasTexture();
			ffr::TextureHandle texture_id = atlas_texture->handle;
			if (pcmd->TextureId) texture_id = *(ffr::TextureHandle*)pcmd->TextureId;
			if(!texture_id.isValid()) texture_id = atlas_texture->handle;

			ffr::setUniform1i(prg, "u_texture", 0);
			
			dc.indices_offset = 0;
			dc.indices_count = num_indices;
			dc.primitive_type = ffr::PrimitiveType::TRIANGLES;
			dc.shader = prg;
			dc.state = 0;
			dc.textures = &texture_id;
			dc.textures_count = 1;
			dc.tex_buffers_count = 0;
			dc.vertex_buffer_offset = 0;

			ffr::blending(1);
			ffr::draw(dc);

			elem_offset += pcmd->ElemCount;
		}
		ffr::popDebugGroup();
		ffr::destroy(dc.vertex_buffer);
		ffr::destroy(dc.index_buffer);

		resetDraw2D();*/
		// TODO
		ASSERT(false);
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



	static int drawArray(lua_State* L)
	{
		/*const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, 1);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);
		const int indices_offset = LuaWrapper::checkArg<int>(L, 1);
		const int indices_count = LuaWrapper::checkArg<int>(L, 2);
		int shader_id = LuaWrapper::checkArg<int>(L, 3);
		LuaWrapper::checkTableArg(L, 4);

		Shader* shader = nullptr;
		for (const ShaderRef& s : pipeline->m_shaders) {
			if(s.id == shader_id) {
				shader = s.res;
				break;
			}
		}
		if (!shader) {
			g_log_error.log("Renderer") << "Unknown shader id " << shader_id << " in drawArrays.";
			return 0;
		}

		if (shader->isFailure()) {
			g_log_error.log("Renderer") << "Shader " << shader->getPath() << " failed to load. `drawArrays` has no effect.";
			return 0;
		}
		if (!shader->isReady()) return 0;


		ffr::ProgramHandle prg = shader->getProgram(0).handle;
		ffr::TextureHandle textures[16];
		int textures_count = 0;
		lua_pushnil(L);
		while (lua_next(L, 4) != 0) {
			if(lua_type(L, -1) != LUA_TNUMBER) {
				g_log_error.log("Renderer") << "Incorrect texture arguments of drawArrays";
				lua_pop(L, 2);
				return 0;
			}

			if(lua_type(L, -2) != LUA_TSTRING) {
				g_log_error.log("Renderer") << "Incorrect texture arguments of drawArrays";
				lua_pop(L, 2);
				return 0;
			}

			if (textures_count > lengthOf(textures)) {
				g_log_error.log("Renderer") << "Too many texture in drawArray call";
				lua_pop(L, 2);
				return 0;
			}

			const char* uniform = lua_tostring(L, -2);
			const int rb_idx = (int)lua_tointeger(L, -1);
			ffr::setUniform1i(prg, uniform, textures_count);
			textures[textures_count] = pipeline->m_renderbuffers[rb_idx].handle;
			++textures_count;

			lua_pop(L, 1);
		}

		if (lua_istable(L, 5)) {
			lua_pushnil(L);
			while (lua_next(L, 5) != 0) {
				if(lua_type(L, -1) != LUA_TTABLE) {
					g_log_error.log("Renderer") << "Incorrect uniform arguments of drawArrays";
					lua_pop(L, 2);
					return 0;
				}

				if(lua_type(L, -2) != LUA_TSTRING) {
					g_log_error.log("Renderer") << "Incorrect uniform arguments of drawArrays";
					lua_pop(L, 2);
					return 0;
				}

				const char* uniform = lua_tostring(L, -2);
				float value[4];
				for(int i = 0; i < 4; ++i) {
					lua_rawgeti(L, -1, 1 + i);
					if (lua_type(L, -1) != LUA_TNUMBER) {
						g_log_error.log("Renderer") << "Incorrect uniform arguments of drawArrays. Uniforms can only be Vec4.";
						lua_pop(L, 3);
						return 0;
					}
					value[i] = (float)lua_tonumber(L, -1);
					lua_pop(L, 1);
				}

				ffr::setUniform4f(prg, uniform, 1, value);

				lua_pop(L, 1);
			}
		}

		const Vec3 camera_pos = pipeline->m_viewport.pos;
		const Entity probe = pipeline->m_scene->getNearestEnvironmentProbe(camera_pos);
		
		if (probe.isValid()) {
			Texture* irradiance = pipeline->m_scene->getEnvironmentProbeIrradiance(probe);
			Texture* radiance = pipeline->m_scene->getEnvironmentProbeRadiance(probe);
			textures[textures_count + 0] = irradiance->handle;
			textures[textures_count + 1] = radiance->handle;
		}
		else {
			textures[textures_count + 0] = pipeline->m_default_cubemap->handle;
			textures[textures_count + 1] = pipeline->m_default_cubemap->handle;
		}
		ffr::setUniform1i(prg, "u_irradiancemap", textures_count + 0);
		ffr::setUniform1i(prg, "u_radiancemap", textures_count + 1);

		textures_count += 2;

		ffr::DrawCall dc;
		dc.attribute_map = nullptr;
		dc.index_buffer = ffr::INVALID_BUFFER;
		dc.indices_count = indices_count;
		dc.indices_offset = indices_offset;
		dc.primitive_type = ffr::PrimitiveType::TRIANGLE_STRIP;
		dc.shader = prg;
		dc.state = 0;
		dc.textures = textures;
		dc.textures_count = textures_count;
		dc.tex_buffers_count = 0;
		dc.vertex_buffer = ffr::INVALID_BUFFER;
		dc.vertex_decl = nullptr;

		ffr::draw(dc);*/
		// TODO
		ASSERT(false);
		return 0;
	}
	

	struct CameraParams
	{
		Frustum frustum;
		Vec3 pos;
		float lod_multiplier;
	};
	

	static void pushCameraParams(lua_State* L, const CameraParams& params)
	{
		lua_createtable(L, 0, 4);

		lua_createtable(L, 32+24, 0);
		const float* frustum = params.frustum.xs; 
		for(int i = 0; i < 32+24; ++i) {
			LuaWrapper::push(L, frustum[i]);
			lua_rawseti(L, -2, i + 1);
		}

		lua_setfield(L, -2, "frustum");

		LuaWrapper::setField(L, -2, "position", params.pos);
		LuaWrapper::setField(L, -2, "lod_multiplier", params.lod_multiplier);
	}


	static int getCameraParams(lua_State* L)
	{
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, 1);
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


	static void findExtraShadowcasterPlanes(const Vec3& light_forward, const Frustum& camera_frustum, const Vec3& camera_position, Frustum* shadow_camera_frustum)
	{
		static const Frustum::Planes planes[] = {
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
		}
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
	/*	const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, 1);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int slice = LuaWrapper::checkArg<int>(L, 1);
		const int shadowmap_width = LuaWrapper::checkArg<int>(L, 2);
		
		RenderScene* scene = pipeline->m_scene;
		
		const Universe& universe = scene->getUniverse();
		const Entity light = scene->getActiveGlobalLight();
		const Vec4 cascades = light.isValid() ? scene->getShadowmapCascades(light) : Vec4(3, 10, 60, 150);
		const Matrix light_mtx = light.isValid() ? universe.getMatrix(light) : Matrix::IDENTITY;

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

		GlobalStateUniforms& global_uniforms = pipeline->m_renderer.getGlobalStateUniforms();
		GlobalStateUniforms::State& global_state = global_uniforms.state;

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
		global_uniforms.update();

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
		*/
		// TODO
		ASSERT(false);
		return 1;
	}


	struct SetRenderTargetsCommand : Renderer::RenderCommandBase
	{
		struct Data
		{
			ffr::TextureHandle rbs[16];
			uint count;
			uint w;
			uint h;
			u32 clear_flags;
		};

		const char* getName() const override { return "set_render_targets"; }

		Renderer::MemRef setup() override
		{
			const Renderer::MemRef mem = m_pipeline->m_renderer.allocate(sizeof(Data));
			Data* data = (Data*)mem.data;

			for(int i = 0; i < m_rbs_count; ++i) {
				data->rbs[i] = m_pipeline->m_renderbuffers[m_rbs[i]].handle;
			}
			data->count = m_rbs_count;
			data->w = m_pipeline->m_viewport.w;
			data->h = m_pipeline->m_viewport.h;
			data->clear_flags = m_clear_flags;

			return mem;
		}


		void execute(const Renderer::MemRef& mem) override
		{
			PROFILE_FUNCTION();
			const Data* data = (Data*)mem.data;

			const ffr::FramebufferHandle fb = m_pipeline->m_renderer.getFramebuffer();
			
			ffr::TextureHandle rbs[16];
			for(uint i = 0; i < data->count; ++i) {
				rbs[i] = data->rbs[i];
			}

			ffr::update(fb, data->count, rbs);
			ffr::setFramebuffer(fb, true);
			ffr::viewport(0, 0, data->w, data->h);
		
			if(m_clear_flags) {
				const float c[] = { 0, 0, 0, 1 };
				ffr::clear(data->clear_flags, c, 0);
			}
		}


		PipelineImpl* m_pipeline;
		int m_rbs[16];
		int m_rbs_count;
		u32 m_clear_flags;
	};


	static int setRenderTargets(lua_State* L)
	{ 
		const int pipeline_idx = lua_upvalueindex(1);
		if (lua_type(L, pipeline_idx) != LUA_TLIGHTUSERDATA) {
			LuaWrapper::argError<PipelineImpl*>(L, 1);
		}
		PipelineImpl* pipeline = LuaWrapper::toType<PipelineImpl*>(L, pipeline_idx);

		const int rb_count = lua_gettop(L) - 1;
		int rbs[16];
		if(rb_count > lengthOf(rbs)) {
			g_log_error.log("Renderer") << "Too many render buffers in " << pipeline->getPath();	
			return 0;
		}
		if(rb_count <= 0) {
			g_log_error.log("Renderer") << "createFramebuffer without arguments in " << pipeline->getPath();
			return 0;
		}

		const u32 clear_flags = LuaWrapper::checkArg<u32>(L, 1);
		for(int i = 0; i < rb_count; ++i) {
			rbs[i] = LuaWrapper::checkArg<int>(L, i + 2);
		}

		SetRenderTargetsCommand* cmd = LUMIX_NEW(pipeline->m_renderer.getAllocator(), SetRenderTargetsCommand);
		cmd->m_pipeline = pipeline;
		cmd->m_clear_flags = clear_flags;
		cmd->m_rbs_count = rb_count;
		ASSERT(sizeof(cmd->m_rbs) == sizeof(rbs));
		copyMemory(cmd->m_rbs, rbs, sizeof(cmd->m_rbs));

		pipeline->m_commands.push(cmd);

		return 1;
	}


	void renderModel(Model& model, const Matrix& mtx) override
	{
		/*for(int i = 0; i < model.getMeshCount(); ++i) {

			const Mesh& mesh = model.getMesh(i);
			const Universe& universe = m_scene->getUniverse();
			const Material* material = mesh.material;
			const Shader::Program& prog = material->getShader()->getProgram(0); // TODO define
			const int textures_count = material->getTextureCount();

			if(!prog.handle.isValid()) continue;

			ffr::TextureHandle textures[16];
			for(int i = 0; i < textures_count; ++i) {
				textures[i] = material->getTexture(i)->handle; 
				ffr::setUniform1i(prog.handle, material->getTextureUniform(i), i);
			}

			int attribute_map[16];
			for(uint i = 0; i < mesh.vertex_decl.attributes_count; ++i) {
				attribute_map[i] = prog.attribute_by_semantics[(int)mesh.attributes_semantic[i]];
			}
			
			ffr::setUniformMatrix4f(prog.handle, "u_model", 1, &mtx.m11);
			ffr::DrawCall dc;
			dc.attribute_map = prog.use_semantics ? attribute_map : nullptr;
			dc.state = u64(ffr::StateFlags::DEPTH_TEST) | material->getRenderStates();
			dc.index_buffer = mesh.index_buffer_handle;
			dc.indices_count = mesh.indices_count;
			dc.indices_offset = 0;
			dc.primitive_type = ffr::PrimitiveType::TRIANGLES;
			dc.shader = prog.handle;
			dc.textures = textures;
			dc.textures_count = textures_count;
			dc.tex_buffers_count = 0;
			dc.vertex_buffer = mesh.vertex_buffer_handle;
			dc.vertex_buffer_offset = 0;
			dc.vertex_decl = &mesh.vertex_decl;
			ffr::draw(dc);
		}*/
		// TODO 
		ASSERT(false);
	}


	struct RenderMeshesCommand : Renderer::RenderCommandBase
	{
		struct Data
		{
			ffr::TextureHandle irradiance_map;
			ffr::TextureHandle radiance_map;
			int count;
			struct Mesh {
				Matrix mtx;
				Lumix::Mesh* mesh; // TODO
			};
			Mesh* meshes;
		};


		const char* getName() const override { return "render_meshes"; }


		Renderer::MemRef setup() override
		{
			if(!m_pipeline->m_scene) return {};

			Renderer& renderer = m_pipeline->m_renderer;
			m_define_mask = m_shader_define.empty() 
				? 0
				: 1 << renderer.getShaderDefineIdx(m_shader_define);
			const Universe& universe = m_pipeline->m_scene->getUniverse();
			
			const Frustum frustum = m_pipeline->m_viewport.getFrustum();
			const Vec3 pos = m_pipeline->m_viewport.pos;
			const float lod_multiplier = 1;
			RenderScene* scene = m_pipeline->getScene();
			auto& meshes = scene->getModelInstanceInfos(frustum, pos, lod_multiplier, m_layer_mask);
			int count = 0;
			for(const auto& submeshes : meshes) count += submeshes.size();
			Renderer::MemRef mem = renderer.allocate(sizeof(Data) + sizeof(Data::Mesh) * count);
			Data* out = (Data*)mem.data;
			out->meshes = (Data::Mesh*)((u8*)out + sizeof(Data));
			out->count = 0;
			for(const auto& submeshes : meshes) {
				for(const auto& mesh : submeshes) {
					auto& m = out->meshes[out->count];
					m.mtx = universe.getMatrix(mesh.owner);
					m.mesh = mesh.mesh;
					++out->count;
				}
			}

			const Entity probe = scene->getNearestEnvironmentProbe(m_pipeline->m_viewport.pos);
			if (probe.isValid()) {
				const Texture* irradiance = m_pipeline->m_scene->getEnvironmentProbeIrradiance(probe);
				const Texture* radiance = m_pipeline->m_scene->getEnvironmentProbeRadiance(probe);
				out->irradiance_map = irradiance->handle;
				out->radiance_map = radiance->handle;
			}
			else {
				out->irradiance_map = m_pipeline->m_default_cubemap->handle;
				out->radiance_map = m_pipeline->m_default_cubemap->handle;
			}
			return mem;
		}


		void execute(const Renderer::MemRef& user_mem) override
		{
			PROFILE_FUNCTION();

			if(user_mem.size == 0) return;

			Data* data = (Data*)user_mem.data;

			ffr::pushDebugGroup(m_shader_define.empty() ? "meshes" : m_shader_define);

			for(int i = 0, c = data->count; i < c; ++i) {
				const auto& mesh = data->meshes[i];
				const Material* material = mesh.mesh->material;
				if (!material->isReady()) continue;

				const u32 final_define_mask = material->getDefineMask() | m_define_mask;
				const Shader::Program prog = material->getShader()->getProgram(final_define_mask);

				if(!prog.handle.isValid()) continue;

				int textures_count = material->getTextureCount();
				ffr::TextureHandle textures[16];
				for(int i = 0; i < textures_count; ++i) {
					textures[i] = material->getTexture(i)->handle; 
					ffr::setUniform1i(prog.handle, material->getTextureUniform(i), i);
				}

				textures[textures_count + 0] = data->irradiance_map;
				textures[textures_count + 1] = data->radiance_map;
				ffr::setUniform1i(prog.handle, "u_irradiancemap", textures_count + 0);
				ffr::setUniform1i(prog.handle, "u_radiancemap", textures_count + 1);
				textures_count += 2;
				
				const Matrix& mtx = mesh.mtx;
				ffr::setUniformMatrix4f(prog.handle, "u_model", 1, &mtx.m11);
				
				for(const GlobalTexture& t : m_pipeline->m_global_textures) {
					textures[textures_count] = t.texture;
					ffr::setUniform1i(prog.handle, t.uniform, textures_count);
					++textures_count;
				}

				for(int i = textures_count; i < lengthOf(textures); ++i) {
					textures[i] = ffr::INVALID_TEXTURE;
				}

				int attribute_map[16];
				for(uint i = 0; i < mesh.mesh->vertex_decl.attributes_count; ++i) {
					attribute_map[i] = prog.attribute_by_semantics[(int)mesh.mesh->attributes_semantic[i]];
				}

				ffr::DrawCall dc;
				dc.attribute_map = prog.use_semantics ? attribute_map : nullptr;
				dc.state = u64(ffr::StateFlags::DEPTH_TEST) | material->getRenderStates();
				dc.index_buffer = mesh.mesh->index_buffer_handle;
				dc.indices_count = mesh.mesh->indices_count;
				dc.indices_offset = 0;
				dc.primitive_type = ffr::PrimitiveType::TRIANGLES;
				dc.shader = prog.handle;
				dc.textures = textures;
				dc.textures_count = lengthOf(textures);
				dc.vertex_buffer = mesh.mesh->vertex_buffer_handle;
				dc.vertex_buffer_offset = 0;
				dc.vertex_decl = &mesh.mesh->vertex_decl;
				ffr::draw(dc);
			}

			ffr::popDebugGroup();
		}


		PipelineImpl* m_pipeline;
		StaticString<32> m_shader_define;
		u32 m_define_mask;
		u64 m_layer_mask;
	};


	void renderMeshes(u64 layer_mask, const char* define)
	{
		RenderMeshesCommand* cmd = LUMIX_NEW(m_renderer.getAllocator(), RenderMeshesCommand);
		cmd->m_pipeline = this;
		cmd->m_layer_mask = layer_mask;
		cmd->m_shader_define = define;
		m_commands.push(cmd);
	}


	void blending(const char* mode)
	{
		if(mode[0]) {
			
		}
		else {
			ffr::blending(0);
		}
	}


	void clear(u32 flags, float r, float g, float b, float a, float depth)
	{
		const float c[] = { r, g, b, a };
		ffr::clear(flags, c, depth);
	}


	void viewport(int x, int y, int w, int h)
	{
		ffr::viewport(x, y, w, h);
	}


	void setGlobalTexture(const char* uniform, int rb_idx)
	{
		/*if (rb_idx < 0 || rb_idx >= m_renderbuffers.size()) {
			g_log_error.log("Renderer") << "Unknown renderbuffer";
			return;
		}
		GlobalTexture& t = m_global_textures.emplace();
		t.uniform = uniform;
		t.texture = m_renderbuffers[rb_idx].handle;*/
		// TODO 
		ASSERT(false);
	}


	void setOutput(int rb_index) 
	{
		m_output = rb_index;
	}


	int preloadShader(const char* path)
	{
		ShaderManager& sm = m_renderer.getShaderManager();
		ShaderRef s;
		s.res = (Shader*)sm.load(Path(path));
		s.id = 0;
		for(ShaderRef& i : m_shaders) {
			if(i.id >= s.id) {
				s.id = i.id + 1;
			}
		}
		m_shaders.push(s);
		return s.id;
	}


	u64 getLayerMask(const char* layer) { return u64(1) << m_renderer.getLayer(layer); }


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

		REGISTER_FUNCTION(blending);
		REGISTER_FUNCTION(clear);
		REGISTER_FUNCTION(createRenderbuffer);
		REGISTER_FUNCTION(executeCustomCommand);
		REGISTER_FUNCTION(getLayerMask);
		REGISTER_FUNCTION(preloadShader);
		REGISTER_FUNCTION(render2D);
		REGISTER_FUNCTION(renderDebugShapes);
		REGISTER_FUNCTION(renderMeshes);
		REGISTER_FUNCTION(setGlobalTexture);
		REGISTER_FUNCTION(setOutput);
		REGISTER_FUNCTION(viewport);

		registerConst("CLEAR_DEPTH", (uint)ffr::ClearFlags::DEPTH);
		registerConst("CLEAR_ALL", (uint)ffr::ClearFlags::COLOR | (uint)ffr::ClearFlags::DEPTH);

		registerCFunction("drawArray", PipelineImpl::drawArray);
		registerCFunction("getCameraParams", PipelineImpl::getCameraParams);
		registerCFunction("getShadowCameraParams", PipelineImpl::getShadowCameraParams);
		registerCFunction("setRenderTargets", PipelineImpl::setRenderTargets);

		lua_pop(L, 1); // pop env

		#undef REGISTER_FUNCTION
	}


	bool isReady() const override { return m_is_ready; }
	const Stats& getStats() const override { return m_stats; }
	Path& getPath() override { return m_path; }

	Draw2D& getDraw2D() override { return m_draw2d; }
	ffr::TextureHandle getOutput() override { 
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

	struct GlobalTexture
	{
		StaticString<32> uniform;
		ffr::TextureHandle texture;
	};

	IAllocator& m_allocator;
	Renderer& m_renderer;
	Path m_path;
	lua_State* m_lua_state;
	int m_lua_thread_ref;
	int m_lua_env;
	bool m_is_ready;
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
	Array<GlobalTexture> m_global_textures;
	Array<ShaderRef> m_shaders;
	Array<Renderer::RenderCommandBase*> m_commands; 
};


Pipeline* Pipeline::create(Renderer& renderer, const Path& path, const char* define, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, PipelineImpl)(renderer, path, define, allocator);
}


void Pipeline::destroy(Pipeline* pipeline)
{
}


} // namespace Lumix