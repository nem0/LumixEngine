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
#include "font_manager.h"
#include "global_state_uniforms.h"
#include "material.h"
#include "model.h"
#include "pipeline.h"
#include "renderer.h"
#include "render_scene.h"
#include "shader.h"
#include "shader_manager.h"
#include "texture.h"


namespace Lumix
{


struct PipelineImpl LUMIX_FINAL : Pipeline
{
	PipelineImpl(Renderer& renderer, const Path& path, const char* define, IAllocator& allocator)
		: m_allocator(allocator)
		, m_renderer(renderer)
		, m_path(path)
		, m_lua_state(nullptr)
		, m_is_ready(false)
		, m_custom_commands_handlers(allocator)
		, m_define(define, allocator)
		, m_scene(nullptr)
		, m_width(-1)
		, m_height(-1)
		, m_is_first_render(true)
		, m_draw2d(allocator)
		, m_output(ffr::INVALID_TEXTURE)
		, m_renderbuffers(allocator)
		, m_framebuffers(allocator)
	{
		ShaderManager& shader_manager = renderer.getShaderManager();
		m_draw2d_shader = (Shader*)shader_manager.load(Path("pipelines/draw2d.shd"));
		m_debug_shape_shader = (Shader*)shader_manager.load(Path("pipelines/debug_shape.shd"));
		

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
		if (m_define.length() == 0) return;
		StaticString<256> tmp(m_define.c_str(), " = true");

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
		catString(tmp, "(pipeline) executeCustomCommand(pipeline, \"");
		catString(tmp, handler.name);
		catString(tmp, "\") end");

		bool errors = luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp), nullptr) != 0;
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

		Pipeline::registerLuaAPI(m_lua_state);
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

		m_width = m_height = -1;
		if (m_scene) callInitScene();

		m_is_ready = true;
	}


	void clearBuffers()
	{
		for(const Framebuffer& fb : m_framebuffers) {
			ffr::destroy(fb.handle);
		}
		m_framebuffers.clear();

		for (Renderbuffer& rb : m_renderbuffers) {
			++rb.frame_counter;
		}

		for(int i = m_renderbuffers.size() - 1; i >= 0; --i) {
			if(m_renderbuffers[i].frame_counter > 1) {
				ffr::destroy(m_renderbuffers[i].handle);
				m_renderbuffers.eraseFast(i);
			}
		}
	}


	bool render() override 
	{ 
		PROFILE_FUNCTION();

		if (!isReady() || !m_scene || m_width < 0 || m_height < 0) {
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
/*		m_applied_camera = INVALID_ENTITY;
		m_global_light_shadowmap = nullptr;
		m_layer_mask = 0;
		m_current_framebuffer = m_default_framebuffer;
		m_instance_data_idx = 0;
		m_point_light_shadowmaps.clear();
		*/
		clearBuffers();

		const Entity global_light = m_scene->getActiveGlobalLight();
		if(global_light.isValid()) {
			GlobalStateUniforms& uniforms = m_renderer.getGlobalStateUniforms();
			uniforms.state.light_direction = Vec4(m_scene->getUniverse().getRotation(global_light).rotate(Vec3(0, 0, -1)), 0); 
			uniforms.state.light_color = Vec4(m_scene->getGlobalLightColor(global_light), 1);
			uniforms.state.light_intensity = m_scene->getGlobalLightIntensity(global_light);
			uniforms.state.light_indirect_intensity = m_scene->getGlobalLightIndirectIntensity(global_light);
			uniforms.update();
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		bool success = true;
		lua_getfield(m_lua_state, -1, "main");
		if (lua_type(m_lua_state, -1) == LUA_TFUNCTION) {
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != 0) {
				success = false;
				g_log_warning.log("Renderer") << lua_tostring(m_lua_state, -1);
				lua_pop(m_lua_state, 1);
			}
		}
		else {
			lua_pop(m_lua_state, 1);
		}
		ffr::setFramebuffer(ffr::INVALID_FRAMEBUFFER);
		return success;
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

		const ffr::BufferHandle vb = ffr::createBuffer(vertices.size() * sizeof(vertices[0]), &vertices[0]);
		ffr::DrawCall dc;
		dc.attribute_map = nullptr;
		dc.index_buffer = ffr::INVALID_BUFFER;
		dc.primitive_type = ffr::PrimitiveType::LINES;
		dc.shader = shader.handle;
		dc.state = 0;
		dc.textures_count = 0;
		dc.tex_buffers_count = 0;
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
		auto resetDraw2D =  [this](){
			m_draw2d.Clear();
			m_draw2d.PushClipRectFullScreen();
			FontAtlas& atlas = m_renderer.getFontManager().getFontAtlas();
			m_draw2d.FontTexUvWhitePixel = atlas.TexUvWhitePixel;
			m_draw2d.PushTextureID(atlas.TexID);
		};

		Vec2 size((float)getWidth(), (float)getHeight());
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

		resetDraw2D();
	}


	void resize(int w, int h) override
	{
		m_width = w;
		m_height = h;	
	}
	

	void setScene(RenderScene* scene) override
	{
		m_scene = scene;
		if (m_lua_state && m_scene) callInitScene();
	}

	RenderScene* getScene() const override { return m_scene; }
	int getWidth() const override { return m_width; }
	int getHeight() const override { return m_height; }

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


	void setCamera(const char* slot)
	{
		if (!m_scene) return;

		const Entity camera = m_scene->getCameraInSlot(slot);
		if(!camera.isValid()) return;

		GlobalStateUniforms& uniforms = m_renderer.getGlobalStateUniforms();
		const int w = uniforms.state.framebuffer_size.x;
		const int h = uniforms.state.framebuffer_size.y;
		m_scene->setCameraScreenSize(camera, w, h);

		uniforms.state.camera_projection = m_scene->getCameraProjection(camera);
		uniforms.state.camera_view = m_scene->getUniverse().getMatrix(camera);
		uniforms.state.camera_pos = Vec4(uniforms.state.camera_view.getTranslation(), 1);
		uniforms.state.camera_view.fastInverse();
		
		uniforms.update();
	}


	void setFramebuffer(int framebuffer)
	{
		const Framebuffer& fb = m_framebuffers[framebuffer];
		ffr::setFramebuffer(fb.handle);
		ffr::viewport(0, 0, fb.width, fb.height);
		
		GlobalStateUniforms& uniforms = m_renderer.getGlobalStateUniforms();
		uniforms.state.framebuffer_size.x = fb.width;
		uniforms.state.framebuffer_size.y = fb.height;
	}


	int createRenderbuffer(float w, float h, bool relative, const char* format_str)
	{
		const uint rb_w = uint(relative ? w * m_width : w);
		const uint rb_h = uint(relative ? h * m_height : h);
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
		rb.frame_counter = 0;
		rb.width = rb_w;
		rb.height = rb_h;
		rb.format = format;
		rb.handle = createTexture(rb_w, rb_h, format, nullptr);

		return m_renderbuffers.size() - 1;
	}


	static int createFramebuffer(lua_State* L)
	{ 
		PipelineImpl* pipeline = LuaWrapper::checkArg<PipelineImpl*>(L, 1);
		ffr::TextureHandle rbs[16];

		const int rb_count = lua_gettop(L) - 1;
		if(rb_count > lengthOf(rbs)) {
			g_log_error.log("Renderer") << "Too many render buffers.";	
			return 0;
		}

		Framebuffer fb;
		for(int i = 0; i < rb_count; ++i) {
			const int rb_idx = LuaWrapper::checkArg<int>(L, i + 2);
			const Renderbuffer& rb = pipeline->m_renderbuffers[rb_idx];
			rbs[i] = rb.handle;	
			fb.width = rb.width;
			fb.height = rb.height;
		}

		fb.handle = ffr::createFramebuffer(rb_count, rbs);
		pipeline->m_framebuffers.push(fb);
		
		LuaWrapper::push(L, pipeline->m_framebuffers.size() - 1);
		return 1;
	}



	void renderModel(Model& model, const Matrix& mtx) override
	{
		for(int i = 0; i < model.getMeshCount(); ++i) {

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
		}
	}

	
	void renderMeshes(const char* camera_slot, u64 layer_mask, const char* shader_define)
	{
		const Entity camera = m_scene->getCameraInSlot(camera_slot);
		const Frustum frustum = m_scene->getCameraFrustum(camera);
		const Universe& universe = m_scene->getUniverse();
		const Vec3 lod_ref_point = universe.getPosition(camera);
		const auto& meshes = m_scene->getModelInstanceInfos(frustum, lod_ref_point, camera, layer_mask);

		ffr::pushDebugGroup("meshes");

		const u32 define_mask = shader_define && shader_define[0] 
			? 1 << m_renderer.getShaderDefineIdx(shader_define) 
			: 0;

		for(auto& submeshes : meshes) {
			for(auto& mesh : submeshes) {
				const Material* material = mesh.mesh->material;

				const u32 final_define_mask = material->getDefineMask() | define_mask;
				const Shader::Program prog = material->getShader()->getProgram(final_define_mask);

				if(!prog.handle.isValid()) continue;

				const int textures_count = material->getTextureCount();
				ffr::TextureHandle textures[16];
				for(int i = 0; i < textures_count; ++i) {
					textures[i] = material->getTexture(i)->handle; 
					ffr::setUniform1i(prog.handle, material->getTextureUniform(i), i);
				}

				const Matrix& mtx = universe.getMatrix(mesh.owner);
				ffr::setUniformMatrix4f(prog.handle, "u_model", 1, &mtx.m11);

				
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
				dc.textures_count = textures_count;
				dc.tex_buffers_count = 0;
				dc.vertex_buffer = mesh.mesh->vertex_buffer_handle;
				dc.vertex_buffer_offset = 0;
				dc.vertex_decl = &mesh.mesh->vertex_decl;
				ffr::draw(dc);
			}
		}

		ffr::popDebugGroup();
	}


	void blending(const char* mode)
	{
		if(mode[0]) {
			
		}
		else {
			ffr::blending(0);
		}
	}


	void clearFramebuffer(u32 flags, float r, float g, float b, float a, float depth)
	{
		const float c[] = { r, g, b, a };
		ffr::clear(flags, c, depth);
	}


	void setOutput(int rb_index) 
	{
		m_output = rb_index >= 0 ? m_renderbuffers[rb_index].handle : ffr::INVALID_TEXTURE;
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
	

	bool isReady() const override { return m_is_ready; }
	const Stats& getStats() const override { return m_stats; }
	Path& getPath() override { return m_path; }

	Draw2D& getDraw2D() override { return m_draw2d; }
	ffr::TextureHandle getOutput() override { return m_output; }

	struct Renderbuffer {
		uint width;
		uint height;
		ffr::TextureFormat format;
		ffr::TextureHandle handle;
		int frame_counter;
	};

	struct Framebuffer {
		ffr::FramebufferHandle handle;
		int width;
		int height;
	};

	IAllocator& m_allocator;
	Renderer& m_renderer;
	Path m_path;
	lua_State* m_lua_state;
	int m_lua_thread_ref;
	int m_lua_env;
	bool m_is_ready;
	bool m_is_first_render;
	string m_define;
	RenderScene* m_scene;
	Draw2D m_draw2d;
	Shader* m_draw2d_shader;
	int m_width;
	int m_height;
	Stats m_stats;
	ffr::TextureHandle m_output;
	Shader* m_debug_shape_shader;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<Renderbuffer> m_renderbuffers;
	Array<Framebuffer> m_framebuffers;
};


Pipeline* Pipeline::create(Renderer& renderer, const Path& path, const char* define, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, PipelineImpl)(renderer, path, define, allocator);
}


void Pipeline::destroy(Pipeline* pipeline)
{
}


void Pipeline::registerLuaAPI(lua_State* L)
{
	auto registerCFunction = [L](const char* name, lua_CFunction function) {
		lua_pushcfunction(L, function);
		lua_setglobal(L, name);
	};

	auto registerConst = [L](const char* name, u32 value)
	{
		lua_pushinteger(L, value);
		lua_setglobal(L, name);
	};

	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<PipelineImpl, decltype(&PipelineImpl::name), &PipelineImpl::name>; \
			registerCFunction(#name, f); \
		} while(false) \

	REGISTER_FUNCTION(blending);
	REGISTER_FUNCTION(clearFramebuffer);
	REGISTER_FUNCTION(createRenderbuffer);
	REGISTER_FUNCTION(executeCustomCommand);
	REGISTER_FUNCTION(getLayerMask);
	REGISTER_FUNCTION(render2D);
	REGISTER_FUNCTION(renderDebugShapes);
	REGISTER_FUNCTION(renderMeshes);
	REGISTER_FUNCTION(setCamera);
	REGISTER_FUNCTION(setFramebuffer);
	REGISTER_FUNCTION(setOutput);

	registerConst("CLEAR_ALL", (uint)ffr::ClearFlags::COLOR | (uint)ffr::ClearFlags::DEPTH);

	registerCFunction("createFramebuffer", PipelineImpl::createFramebuffer);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix