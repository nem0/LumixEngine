#include "pipeline.h"

#include "renderer/pipeline.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/fs/ifile.h"
#include "core/fs/file_system.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/profiler.h"
#include "core/static_array.h"
#include "engine.h"
#include "plugin_manager.h"
#include "renderer/frame_buffer.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "renderer/transient_geometry.h"
#include "universe/universe.h"
#include <bgfx/bgfx.h>
#include <cmath>


namespace Lumix
{


static const float SHADOW_CAM_NEAR = 50.0f;
static const float SHADOW_CAM_FAR = 5000.0f;


struct InstanceData
{
	static const int MAX_INSTANCE_COUNT = 64;

	const bgfx::InstanceDataBuffer* buffer;
	int instance_count;
	Mesh* mesh;
	Model* model;
};



struct PipelineImpl : public Pipeline
{
	PipelineImpl(Renderer& renderer, const Path& path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_path(path)
		, m_framebuffers(allocator)
		, m_lua_state(nullptr)
		, m_parameters(allocator)
		, m_custom_commands_handlers(allocator)
		, m_tmp_terrains(allocator)
		, m_tmp_grasses(allocator)
		, m_tmp_meshes(allocator)
		, m_uniforms(allocator)
		, m_renderer(renderer)
		, m_default_framebuffer(nullptr)
		, m_debug_line_material(nullptr)
		, m_debug_flags(BGFX_DEBUG_TEXT)
		, m_point_light_shadowmaps(allocator)
		, m_materials(allocator)
		, m_is_rendering_in_shadowmap(false)
		, m_is_ready(false)
	{
		m_base_vertex_decl.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();

		m_is_wireframe = false;
		m_view_x = m_view_y = 0;
		m_has_shadowmap_define_idx = m_renderer.getShaderDefineIdx("HAS_SHADOWMAP");

		createUniforms();

		m_debug_line_material = static_cast<Material*>(
			renderer.getMaterialManager().load(Lumix::Path("models/editor/debug_line.mat")));

		m_scene = nullptr;
		m_width = m_height = -1;

		createParticleBuffers();
	}


	void parseRenderbuffers(lua_State* L, FrameBuffer::Declaration& decl)
	{
		decl.m_renderbuffers_count = 0;
		int len = (int)lua_rawlen(L, -1);
		for (int i = 0; i < len; ++i)
		{
			if (lua_rawgeti(L, -1, 1 + i) == LUA_TTABLE)
			{
				FrameBuffer::RenderBuffer& buf =
					decl.m_renderbuffers[decl.m_renderbuffers_count];
				buf.parse(L);
				++decl.m_renderbuffers_count;
			}
			lua_pop(L, 1);
		}
	}


	void parseParameters(lua_State* L)
	{
		m_parameters.clear();
		if (lua_getglobal(L, "parameters") == LUA_TTABLE)
		{
			lua_pushnil(L);
			while (lua_next(L, -2) != 0)
			{
				const char* parameter_name = luaL_checkstring(L, -2);
				m_parameters.push(Lumix::string(parameter_name, m_allocator));
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}


	void parseFramebuffers(lua_State* L)
	{
		if (lua_getglobal(L, "framebuffers") == LUA_TTABLE)
		{
			for(int i = 0; i < m_framebuffers.size(); ++i)
			{
				LUMIX_DELETE(m_allocator, m_framebuffers[i]);
			}
			m_framebuffers.clear();

			int len = (int)lua_rawlen(L, -1);
			ASSERT(m_framebuffers.empty());
			for (int i = 0; i < len; ++i)
			{
				if (lua_rawgeti(L, -1, 1 + i) == LUA_TTABLE)
				{
					FrameBuffer::Declaration decl;
					if (lua_getfield(L, -1, "name") == LUA_TSTRING)
					{
						copyString(decl.m_name,
								   sizeof(decl.m_name),
								   lua_tostring(L, -1));
					}
					lua_pop(L, 1);
					if (lua_getfield(L, -1, "width") == LUA_TNUMBER)
					{
						decl.m_width = (int)lua_tointeger(L, -1);
					}
					lua_pop(L, 1);
					if (lua_getfield(L, -1, "height") == LUA_TNUMBER)
					{
						decl.m_height = (int)lua_tointeger(L, -1);
					}
					lua_pop(L, 1);
					if (lua_getfield(L, -1, "renderbuffers") == LUA_TTABLE)
					{
						parseRenderbuffers(L, decl);
					}
					lua_pop(L, 1);
					auto* fb = LUMIX_NEW(m_allocator, FrameBuffer)(decl);
					m_framebuffers.push(fb);
					if (compareString(decl.m_name, "default") == 0) m_default_framebuffer = fb;
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}


	void registerCFunction(const char* name, lua_CFunction function)
	{
		lua_pushcfunction(m_lua_state, function);
		lua_setglobal(m_lua_state, name);
	}


	void registerCFunctions();


	void load() override
	{
		auto& fs = m_renderer.getEngine().getFileSystem();
		Delegate<void(FS::IFile&, bool)> cb;
		cb.bind<PipelineImpl, &PipelineImpl::onFileLoaded>(this);
		fs.openAsync(fs.getDefaultDevice(), m_path, FS::Mode::OPEN_AND_READ, cb);
	}


	void onFileLoaded(FS::IFile& file, bool success)
	{
		if(!success) return;

		if (m_lua_state)
		{
			lua_close(m_lua_state);
			m_lua_state = nullptr;
		}
		m_lua_state = luaL_newstate();
		luaL_openlibs(m_lua_state);
		bool errors =
			luaL_loadbuffer(
				m_lua_state, (const char*)file.getBuffer(), file.size(), m_path.c_str()) !=
			LUA_OK;
		errors = errors || lua_pcall(m_lua_state, 0, LUA_MULTRET, 0) != LUA_OK;
		if (errors)
		{
			g_log_error.log("lua") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		parseParameters(m_lua_state);
		parseFramebuffers(m_lua_state);
		registerCFunctions();

		m_width = m_height = -1;
		if (lua_getglobal(m_lua_state, "init") == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != LUA_OK)
			{
				g_log_error.log("lua") << lua_tostring(m_lua_state, -1);
				lua_pop(m_lua_state, 1);
			}
		}
		else
		{
			lua_pop(m_lua_state, 1);
		}
		m_is_ready = true;
	}

	lua_State* m_lua_state;
	Array<string> m_parameters;


	int getParameterCount() const override
	{
		return m_parameters.size();
	}


	const char* getParameterName(int index) const override
	{
		if (index >= m_parameters.size()) return false;
		return m_parameters[index].c_str();
	}


	bool getParameter(int index) override
	{
		if (!m_lua_state) return false;
		if (index >= m_parameters.size()) return false;

		bool ret = false;
		lua_State* L = m_lua_state;
		if (lua_getglobal(L, "parameters") == LUA_TTABLE)
		{
			lua_getfield(L, -1, m_parameters[index].c_str());
			ret = lua_toboolean(L, -1) != 0;
			lua_pop(L, -1);
		}
		lua_pop(L, -1);
		return ret;
	}


	void setParameter(int index, bool value) override
	{
		if (!m_lua_state) return;
		if (index >= m_parameters.size()) return;
		
		lua_State* L = m_lua_state;
		if (lua_getglobal(L, "parameters") == LUA_TTABLE)
		{
			lua_pushboolean(L, value);
			lua_setfield(L, -2, m_parameters[index].c_str());
		}
		lua_pop(L, -1);
	}


	void createParticleBuffers()
	{
		BaseVertex vertices[] = {
			{ -1, -1, 1, 0xffffffff, 0, 0},
			{ -1,  1, 1, 0xffffffff, 0, 1},
			{  1,  1, 1, 0xffffffff, 1, 1},
			{  1, -1, 1, 0xffffffff, 1, 0},
		};

		const bgfx::Memory* vertex_mem = bgfx::copy(vertices, sizeof(vertices));
		m_particle_vertex_buffer = bgfx::createVertexBuffer(vertex_mem, m_base_vertex_decl);

		uint16 indices[] = { 0, 1, 2, 0, 2, 3 };
		const bgfx::Memory* index_mem = bgfx::copy(indices, sizeof(indices));
		m_particle_index_buffer = bgfx::createIndexBuffer(index_mem);
	}


	void createUniforms()
	{
		m_texture_size_uniform = bgfx::createUniform("u_textureSize", bgfx::UniformType::Vec4);
		m_cam_view_uniform = bgfx::createUniform("u_camView", bgfx::UniformType::Mat4);
		m_cam_inv_proj_uniform = bgfx::createUniform("u_camInvProj", bgfx::UniformType::Mat4);
		m_tex_shadowmap_uniform = bgfx::createUniform("u_texShadowmap", bgfx::UniformType::Int1);
		m_attenuation_params_uniform =
			bgfx::createUniform("u_attenuationParams", bgfx::UniformType::Vec4);
		m_terrain_scale_uniform = bgfx::createUniform("u_terrainScale", bgfx::UniformType::Vec4);
		m_rel_camera_pos_uniform = bgfx::createUniform("u_relCamPos", bgfx::UniformType::Vec4);
		m_terrain_params_uniform = bgfx::createUniform("u_terrainParams", bgfx::UniformType::Vec4);
		m_fog_params_uniform = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
		m_fog_color_density_uniform =
			bgfx::createUniform("u_fogColorDensity", bgfx::UniformType::Vec4);
		m_light_pos_radius_uniform =
			bgfx::createUniform("u_lightPosRadius", bgfx::UniformType::Vec4);
		m_light_color_uniform = bgfx::createUniform("u_lightRgbInnerR", bgfx::UniformType::Vec4);
		m_light_dir_fov_uniform = bgfx::createUniform("u_lightDirFov", bgfx::UniformType::Vec4);
		m_light_specular_uniform =
			bgfx::createUniform("u_lightSpecular", bgfx::UniformType::Mat4, 64);
		m_ambient_color_uniform = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
		m_shadowmap_matrices_uniform =
			bgfx::createUniform("u_shadowmapMatrices", bgfx::UniformType::Mat4, 4);
		m_bone_matrices_uniform =
			bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, 64);
		m_specular_shininess_uniform =
			bgfx::createUniform("u_materialSpecularShininess", bgfx::UniformType::Vec4);
		m_terrain_matrix_uniform = bgfx::createUniform("u_terrainMatrix", bgfx::UniformType::Mat4);
	}


	void destroyUniforms()
	{
		bgfx::destroyUniform(m_tex_shadowmap_uniform);
		bgfx::destroyUniform(m_attenuation_params_uniform);
		bgfx::destroyUniform(m_terrain_matrix_uniform);
		bgfx::destroyUniform(m_specular_shininess_uniform);
		bgfx::destroyUniform(m_bone_matrices_uniform);
		bgfx::destroyUniform(m_terrain_scale_uniform);
		bgfx::destroyUniform(m_rel_camera_pos_uniform);
		bgfx::destroyUniform(m_terrain_params_uniform);
		bgfx::destroyUniform(m_fog_params_uniform);
		bgfx::destroyUniform(m_fog_color_density_uniform);
		bgfx::destroyUniform(m_light_pos_radius_uniform);
		bgfx::destroyUniform(m_light_color_uniform);
		bgfx::destroyUniform(m_light_dir_fov_uniform);
		bgfx::destroyUniform(m_ambient_color_uniform);
		bgfx::destroyUniform(m_shadowmap_matrices_uniform);
		bgfx::destroyUniform(m_light_specular_uniform);
		bgfx::destroyUniform(m_cam_inv_proj_uniform);
		bgfx::destroyUniform(m_cam_view_uniform);
		bgfx::destroyUniform(m_texture_size_uniform);
	}


	~PipelineImpl()
	{
		if(m_lua_state)
		{
			lua_close(m_lua_state);
		}

		ResourceManagerBase& material_manager = m_renderer.getMaterialManager();
		for (auto* material : m_materials)
		{
			material_manager.unload(*material);
		}
		material_manager.unload(*m_debug_line_material);

		destroyUniforms();

		for (int i = 0; i < m_uniforms.size(); ++i)
		{
			bgfx::destroyUniform(m_uniforms[i]);
		}

		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_framebuffers[i]);
			if (m_framebuffers[i] == m_default_framebuffer) m_default_framebuffer = nullptr;
		}
		LUMIX_DELETE(m_allocator, m_default_framebuffer);

		bgfx::destroyIndexBuffer(m_particle_index_buffer);
		bgfx::destroyVertexBuffer(m_particle_vertex_buffer);
	}


	void renderParticlesFromEmitter(const ParticleEmitter& emitter)
	{
		static const int PARTICLE_BATCH_SIZE = 256;

		if (emitter.m_life.empty()) return;
		if (!emitter.getMaterial()) return;
		if (!emitter.getMaterial()->isReady()) return;

		Material* material = emitter.getMaterial();

		const bgfx::InstanceDataBuffer* instance_buffer = nullptr;
		struct Instance
		{
			Vec4 pos;
			Vec4 alpha_and_rotation;
		};
		Instance* instance = nullptr;

		for (int i = 0, c = emitter.m_life.size(); i < c; ++i)
		{
			if (i % PARTICLE_BATCH_SIZE == 0)
			{
				if (instance_buffer)
				{
					setMaterial(material);
					bgfx::setInstanceDataBuffer(instance_buffer, PARTICLE_BATCH_SIZE);
					bgfx::setVertexBuffer(m_particle_vertex_buffer);
					bgfx::setIndexBuffer(m_particle_index_buffer);
					bgfx::submit(m_view_idx, material->getShaderInstance().m_program_handles[m_pass_idx]);
				}

				instance_buffer = bgfx::allocInstanceDataBuffer(PARTICLE_BATCH_SIZE, sizeof(Instance));
				instance = (Instance*)instance_buffer->data;
			}

			instance->pos = Vec4(emitter.m_position[i], emitter.m_size[i]);
			instance->alpha_and_rotation = Vec4(emitter.m_alpha[i], emitter.m_rotation[i], 0, 0);
			++instance;
		}

		if (emitter.m_life.size() % PARTICLE_BATCH_SIZE)
		{
			setMaterial(material);
			bgfx::setInstanceDataBuffer(instance_buffer, emitter.m_life.size() % PARTICLE_BATCH_SIZE);
			bgfx::setVertexBuffer(m_particle_vertex_buffer);
			bgfx::setIndexBuffer(m_particle_index_buffer);
			bgfx::setState(m_render_state | material->getRenderStates());
			bgfx::submit(m_view_idx, material->getShaderInstance().m_program_handles[m_pass_idx]);
		}
	}


	void renderParticles()
	{
		const auto& emitters = m_scene->getParticleEmitters();
		for (const auto* emitter : emitters)
		{
			if (!emitter) continue;

			renderParticlesFromEmitter(*emitter);
		}
	}


	void bindFramebufferTexture(const char* framebuffer_name,
		int renderbuffer_idx,
		int uniform_idx)
	{
		FrameBuffer* fb = getFramebuffer(framebuffer_name);
		if (!fb) return;

		Vec4 size;
		size.x = (float)fb->getWidth();
		size.y = (float)fb->getHeight();
		bgfx::setUniform(m_texture_size_uniform, &size);
		bgfx::setTexture(0, m_uniforms[uniform_idx], fb->getRenderbufferHandle(renderbuffer_idx));
	}


	void setViewProjection(const Matrix& mtx, int width, int height) override
	{
		bgfx::setViewRect(m_view_idx, 0, 0, (uint16_t)width, (uint16_t)height);
		bgfx::setViewTransform(m_view_idx, nullptr, &mtx.m11);
	}


	void finishInstances(int idx)
	{
		InstanceData& data = m_instances_data[idx];
		if (!data.buffer) return;

		Mesh& mesh = *data.mesh;
		const Model& model = *data.model;
		Material* material = mesh.getMaterial();
		const uint16 stride = mesh.getVertexDefinition().getStride();

		setMaterial(material);
		bgfx::setVertexBuffer(model.getVerticesHandle(),
							  mesh.getAttributeArrayOffset() / stride,
							  mesh.getAttributeArraySize() / stride);
		bgfx::setIndexBuffer(model.getIndicesHandle(),
							 mesh.getIndicesOffset(),
							 mesh.getIndexCount());
		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::setInstanceDataBuffer(data.buffer, data.instance_count);
		ShaderInstance& shader_instance =
			mesh.getMaterial()->getShaderInstance();
		bgfx::submit(m_view_idx, shader_instance.m_program_handles[m_pass_idx]);

		data.buffer = nullptr;
		data.instance_count = 0;
		mesh.setInstanceIdx(-1);
	}


	void applyCamera(const char* slot)
	{
		ComponentIndex cmp = m_scene->getCameraInSlot(slot);
		if (cmp < 0) return;

		m_scene->setCameraSize(cmp, m_width, m_height);
		m_applied_camera = cmp;
		m_camera_frustum = m_scene->getCameraFrustum(cmp);

		Matrix projection_matrix;
		float fov = m_scene->getCameraFOV(cmp);
		float near_plane = m_scene->getCameraNearPlane(cmp);
		float far_plane = m_scene->getCameraFarPlane(cmp);
		float ratio = float(m_width) / m_height;
		projection_matrix.setPerspective(
			Math::degreesToRadians(fov), ratio, near_plane, far_plane);

		Universe& universe = m_scene->getUniverse();
		Matrix mtx = universe.getMatrix(m_scene->getCameraEntity(cmp));
		mtx.fastInverse();
		bgfx::setViewTransform(m_view_idx, &mtx.m11, &projection_matrix.m11);

		bgfx::setViewRect(
			m_view_idx, (uint16_t)m_view_x, (uint16_t)m_view_y, (uint16)m_width, (uint16)m_height);
	}


	void finishInstances()
	{
		for (int i = 0; i < lengthOf(m_instances_data); ++i)
		{
			finishInstances(i);
		}
		m_instance_data_idx = 0;
	}


	void setPass(const char* name)
	{
		m_pass_idx = m_renderer.getPassIdx(name);
		for (int i = 0; i < m_view2pass_map.size(); ++i)
		{
			if (m_view2pass_map[i] == m_pass_idx)
			{
				m_view_idx = (uint8)i;
				return;
			}
		}

		beginNewView(m_current_framebuffer, name);
	}


	CustomCommandHandler& addCustomCommandHandler(const char* name) override
	{
		auto& handler = m_custom_commands_handlers.emplace();
		copyString(handler.name, name);
		handler.hash = crc32(name);
		exposeCustomCommandToLua(handler);
		return handler;
	}


	FrameBuffer* getFramebuffer(const char* framebuffer_name) override
	{
		for (int i = 0, c = m_framebuffers.size(); i < c; ++i)
		{
			if (compareString(m_framebuffers[i]->getName(), framebuffer_name) == 0)
			{
				return m_framebuffers[i];
			}
		}
		return nullptr;
	}


	void setFramebuffer(const char* framebuffer_name)
	{
		if (compareString(framebuffer_name, "default") == 0)
		{
			m_current_framebuffer = m_default_framebuffer;
			if (m_current_framebuffer)
			{
				bgfx::setViewFrameBuffer(m_view_idx,
										 m_current_framebuffer->getHandle());
			}
			else
			{
				bgfx::setViewFrameBuffer(m_view_idx, BGFX_INVALID_HANDLE);
			}
			return;
		}
		m_current_framebuffer = getFramebuffer(framebuffer_name);
		if (m_current_framebuffer)
		{
			bgfx::setViewFrameBuffer(m_view_idx,
									 m_current_framebuffer->getHandle());
		}
		else
		{
			g_log_warning.log("renderer") << "Framebuffer " << framebuffer_name
										  << " not found";
		}
	}


	int getWidth() override { return m_width; }


	int getHeight() override { return m_height; }


	float getFPS()
	{
		return m_renderer.getEngine().getFPS();
	}


	void executeCustomCommand(const char* name)
	{
		uint32 name_hash = crc32(name);
		CustomCommandHandler handler;
		for(auto& handler : m_custom_commands_handlers)
		{
			if(handler.hash == name_hash)
			{
				handler.callback.invoke();
				break;
			}
		}
		finishInstances();
	}


	void beginNewView(FrameBuffer* framebuffer, const char* debug_name)
	{
		m_renderer.viewCounterAdd();
		m_view_idx = (uint8)m_renderer.getViewCounter();
		m_view2pass_map[m_view_idx] = m_pass_idx;
		m_current_framebuffer = framebuffer;
		if (framebuffer)
		{
			bgfx::setViewFrameBuffer(m_view_idx,
									 m_current_framebuffer->getHandle());
		}
		else
		{
			bgfx::setViewFrameBuffer(m_view_idx, BGFX_INVALID_HANDLE);
		}
		bgfx::setViewClear(m_view_idx, 0);
		bgfx::setViewName(m_view_idx, debug_name);
	}


	void renderSpotLightShadowmap(FrameBuffer* fb,
								  ComponentIndex light,
								  int64 layer_mask)
	{
		ASSERT(fb);
		beginNewView(fb, "point_light");

		Entity light_entity = m_scene->getPointLightEntity(light);
		Matrix mtx = m_scene->getUniverse().getMatrix(light_entity);
		float fov = m_scene->getLightFOV(light);
		float range = m_scene->getLightRange(light);
		uint16 shadowmap_height = (uint16)m_current_framebuffer->getHeight();
		uint16 shadowmap_width = (uint16)m_current_framebuffer->getWidth();
		Vec3 pos = mtx.getTranslation();
		
		bgfx::setViewClear(m_view_idx, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
		bgfx::touch(m_view_idx);
		bgfx::setViewRect(m_view_idx, 0, 0, shadowmap_width, shadowmap_height);

		Matrix projection_matrix;
		projection_matrix.setPerspective(
			Math::degreesToRadians(fov), 1, 0.01f, range);
		Matrix view_matrix;
		view_matrix.lookAt(pos, pos + mtx.getZVector(), mtx.getYVector());
		bgfx::setViewTransform(
			m_view_idx, &view_matrix.m11, &projection_matrix.m11);

		PointLightShadowmap& s = m_point_light_shadowmaps.emplace();
		s.m_framebuffer = m_current_framebuffer;
		s.m_light = light;
		static const Matrix biasMatrix(
			0.5,  0.0, 0.0, 0.0,
			0.0, -0.5, 0.0, 0.0,
			0.0,  0.0, 0.5, 0.0,
			0.5,  0.5, 0.5, 1.0);
		s.m_matrices[0] = biasMatrix * (projection_matrix * view_matrix);

		renderPointLightInfluencedGeometry(light, layer_mask);
	}


	void renderOmniLightShadowmap(FrameBuffer* fb,
								  ComponentIndex light,
								  int64 layer_mask)
	{
		Entity light_entity = m_scene->getPointLightEntity(light);
		Vec3 light_pos = m_scene->getUniverse().getPosition(light_entity);
		float range = m_scene->getLightRange(light);
		uint16 shadowmap_height = (uint16)fb->getHeight();
		uint16 shadowmap_width = (uint16)fb->getWidth();

		float viewports[] = {0, 0, 0.5, 0, 0, 0.5, 0.5, 0.5};

		static const float YPR[4][3] = {
			{Math::degreesToRadians(0.0f),
			 Math::degreesToRadians(27.36780516f),
			 Math::degreesToRadians(0.0f)},
			{Math::degreesToRadians(180.0f),
			 Math::degreesToRadians(27.36780516f),
			 Math::degreesToRadians(0.0f)},
			{Math::degreesToRadians(-90.0f),
			 Math::degreesToRadians(-27.36780516f),
			 Math::degreesToRadians(0.0f)},
			{Math::degreesToRadians(90.0f),
			 Math::degreesToRadians(-27.36780516f),
			 Math::degreesToRadians(0.0f)},
		};

		PointLightShadowmap& shadowmap_info =
			m_point_light_shadowmaps.emplace();
		shadowmap_info.m_framebuffer = fb;
		shadowmap_info.m_light = light;

		for (int i = 0; i < 4; ++i)
		{
			ASSERT(fb);
			beginNewView(fb, "omnilight");

			bgfx::setViewClear(m_view_idx, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
			bgfx::touch(m_view_idx);
			uint16 view_x = uint16(shadowmap_width * viewports[i * 2]);
			uint16 view_y = uint16(shadowmap_height * viewports[i * 2 + 1]);
			bgfx::setViewRect(m_view_idx,
							  view_x,
							  view_y,
							  shadowmap_width >> 1,
							  shadowmap_height >> 1);

			float fovx = Math::degreesToRadians(143.98570868f + 3.51f);
			float fovy = Math::degreesToRadians(125.26438968f + 9.85f);
			float aspect = tanf(fovx * 0.5f) / tanf(fovy * 0.5f);

			Matrix projection_matrix;
			projection_matrix.setPerspective(fovx, aspect, 0.01f, range);
			
			Matrix view_matrix;
			view_matrix.fromEuler(YPR[i][0], YPR[i][1], YPR[i][2]);
			view_matrix.setTranslation(light_pos);
			Frustum frustum;
			frustum.computePerspective(light_pos,
				view_matrix.getZVector(),
				view_matrix.getYVector(),
				fovx,
				aspect,
				0.01f,
				range);
			
			view_matrix.fastInverse();

			bgfx::setViewTransform(
				m_view_idx, &view_matrix.m11, &projection_matrix.m11);

			static const Matrix biasMatrix(
			0.5, 0.0, 0.0, 0.0,
			0.0, -0.5, 0.0, 0.0,
			0.0, 0.0, 0.5, 0.0,
			0.5, 0.5, 0.5, 1.0);
			shadowmap_info.m_matrices[i] = biasMatrix * (projection_matrix * view_matrix);

			renderLitModels(light, frustum, layer_mask);
		}
	}


	void renderLitModels(ComponentIndex light, const Frustum& frustum, int64 layer_mask)
	{
		PROFILE_FUNCTION();

		m_tmp_meshes.clear();
		m_current_light = light;
		m_is_current_light_global = false;
		m_scene->getPointLightInfluencedGeometry(light, frustum, m_tmp_meshes, layer_mask);

		renderMeshes(m_tmp_meshes);
		m_current_light = -1;
	}


	void renderLocalLightShadowmaps(ComponentIndex camera,
									FrameBuffer** fbs,
									int framebuffers_count,
									int64 layer_mask)
	{
		if (camera < 0) return;

		Universe& universe = m_scene->getUniverse();
		Entity camera_entity = m_scene->getCameraEntity(camera);
		Vec3 camera_pos = universe.getPosition(camera_entity);

		ComponentIndex lights[16];
		int light_count = m_scene->getClosestPointLights(
			camera_pos, lights, lengthOf(lights));

		int fb_index = 0;
		for (int i = 0; i < light_count; ++i)
		{
			if (!m_scene->getLightCastShadows(lights[i])) continue;
			if (fb_index == framebuffers_count) break;

			float fov = m_scene->getLightFOV(lights[i]);
			if (fov < 180)
			{
				renderSpotLightShadowmap(fbs[fb_index], lights[i], layer_mask);
				++fb_index;
				continue;
			}
			else
			{
				renderOmniLightShadowmap(fbs[fb_index], lights[i], layer_mask);
				++fb_index;
				continue;
			}
		}
	}


	static Vec3 shadowmapTexelAlign(const Vec3& shadow_cam_pos,
		float shadowmap_width,
		float frustum_radius,
		const Matrix& light_mtx)
	{
		Matrix inv = light_mtx;
		inv.fastInverse();
		Vec3 out = inv.multiplyPosition(shadow_cam_pos);
		float align = 2 * frustum_radius / (shadowmap_width * 0.5f - 2);
		out.x -= fmodf(out.x, align);
		out.y -= fmodf(out.y, align);
		out = light_mtx.multiplyPosition(out);
		return out;
	}


	void renderShadowmap(int64 layer_mask, const char* slot)
	{
		auto camera = m_scene->getCameraInSlot(slot);
		Universe& universe = m_scene->getUniverse();
		ComponentIndex light_cmp = m_scene->getActiveGlobalLight();
		if (light_cmp < 0 || camera < 0) return;
		float camera_height = m_scene->getCameraHeight(camera);
		if (!camera_height) return;

		Matrix light_mtx = universe.getMatrix(m_scene->getGlobalLightEntity(light_cmp));
		m_global_light_shadowmap = m_current_framebuffer;
		float shadowmap_height = (float)m_current_framebuffer->getHeight();
		float shadowmap_width = (float)m_current_framebuffer->getWidth();
		float viewports[] = {0, 0, 0.5f, 0, 0, 0.5f, 0.5f, 0.5f};
		float camera_fov = Math::degreesToRadians(m_scene->getCameraFOV(camera));
		float camera_ratio = m_scene->getCameraWidth(camera) / camera_height;
		Vec4 cascades = m_scene->getShadowmapCascades(light_cmp);
		float split_distances[] = {0.01f, cascades.x, cascades.y, cascades.z, cascades.w};
		m_is_rendering_in_shadowmap = true;
		for (int split_index = 0; split_index < 4; ++split_index)
		{
			if (split_index > 0) beginNewView(m_current_framebuffer, "shadowmap");

			bgfx::setViewClear(
				m_view_idx, BGFX_CLEAR_DEPTH | BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
			bgfx::touch(m_view_idx);
			float* viewport = viewports + split_index * 2;
			bgfx::setViewRect(m_view_idx,
				(uint16)(1 + shadowmap_width * viewport[0]),
				(uint16)(1 + shadowmap_height * viewport[1]),
				(uint16)(0.5f * shadowmap_width - 2),
				(uint16)(0.5f * shadowmap_height - 2));

			Frustum frustum;
			Matrix camera_matrix = universe.getMatrix(m_scene->getCameraEntity(camera));
			frustum.computePerspective(camera_matrix.getTranslation(),
				camera_matrix.getZVector(),
				camera_matrix.getYVector(),
				camera_fov,
				camera_ratio,
				split_distances[split_index],
				split_distances[split_index + 1]);

			Vec3 shadow_cam_pos = camera_matrix.getTranslation();
			float bb_size = frustum.getRadius();
			shadow_cam_pos =
				shadowmapTexelAlign(shadow_cam_pos, 0.5f * shadowmap_width - 2, bb_size, light_mtx);

			Matrix projection_matrix;
			projection_matrix.setOrtho(
				bb_size, -bb_size, -bb_size, bb_size, SHADOW_CAM_NEAR, SHADOW_CAM_FAR);
			Vec3 light_forward = light_mtx.getZVector();
			shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
			Matrix view_matrix;
			view_matrix.lookAt(
				shadow_cam_pos, shadow_cam_pos + light_forward, light_mtx.getYVector());
			bgfx::setViewTransform(m_view_idx, &view_matrix.m11, &projection_matrix.m11);
			static const Matrix biasMatrix(
				0.5, 0.0, 0.0, 0.0, 0.0, -0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, 1.0);
			m_shadow_viewprojection[split_index] = biasMatrix * (projection_matrix * view_matrix);

			Frustum shadow_camera_frustum;
			shadow_camera_frustum.computeOrtho(shadow_cam_pos,
				-light_forward,
				light_mtx.getYVector(),
				bb_size * 2,
				bb_size * 2,
				SHADOW_CAM_NEAR,
				SHADOW_CAM_FAR);
			renderAll(shadow_camera_frustum, layer_mask, false);
		}
		m_is_rendering_in_shadowmap = false;
	}


	void renderDebugShapes()
	{
		renderDebugLines();
		renderDebugPoints();
	}


	void renderDebugPoints()
	{
		const Array<DebugPoint>& points = m_scene->getDebugPoints();
		if (points.empty() || !m_debug_line_material->isReady())
		{
			return;
		}
		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		if (bgfx::allocTransientBuffers(
				&tvb, m_base_vertex_decl, points.size(), &tib, points.size()))
		{
			BaseVertex* vertex = (BaseVertex*)tvb.data;
			uint16* indices = (uint16*)tib.data;
			for (int i = 0; i < points.size(); ++i)
			{
				const DebugPoint& point = points[i];
				vertex[0].m_rgba = point.m_color;
				vertex[0].m_x = point.m_pos.x;
				vertex[0].m_y = point.m_pos.y;
				vertex[0].m_z = point.m_pos.z;
				vertex[0].m_u = vertex[0].m_v = 0;

				indices[0] = i;
				++vertex;
				++indices;
			}

			bgfx::setVertexBuffer(&tvb);
			bgfx::setIndexBuffer(&tib);
			bgfx::setState(
				m_render_state | m_debug_line_material->getRenderStates() | BGFX_STATE_PT_POINTS);
			bgfx::submit(m_view_idx,
				m_debug_line_material->getShaderInstance().m_program_handles[m_pass_idx]);
		}
	}


	void renderDebugLines()
	{
		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_line_material->isReady()) return;

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		if (bgfx::allocTransientBuffers(
				&tvb, m_base_vertex_decl, lines.size() * 2, &tib, lines.size() * 2))
		{
			BaseVertex* vertex = (BaseVertex*)tvb.data;
			uint16* indices = (uint16*)tib.data;
			for (int i = 0; i < lines.size(); ++i)
			{
				const DebugLine& line = lines[i];
				vertex[0].m_rgba = line.m_color;
				vertex[0].m_x = line.m_from.x;
				vertex[0].m_y = line.m_from.y;
				vertex[0].m_z = line.m_from.z;
				vertex[0].m_u = vertex[0].m_v = 0;

				vertex[1].m_rgba = line.m_color;
				vertex[1].m_x = line.m_to.x;
				vertex[1].m_y = line.m_to.y;
				vertex[1].m_z = line.m_to.z;
				vertex[1].m_u = vertex[0].m_v = 0;

				indices[0] = i * 2;
				indices[1] = i * 2 + 1;
				vertex += 2;
				indices += 2;
			}

			bgfx::setVertexBuffer(&tvb);
			bgfx::setIndexBuffer(&tib);
			bgfx::setState(
				m_render_state | m_debug_line_material->getRenderStates() | BGFX_STATE_PT_LINES);
			bgfx::submit(m_view_idx,
				m_debug_line_material->getShaderInstance().m_program_handles[m_pass_idx]);
		}
	}


	int getPassIdx() const override
	{
		return m_pass_idx;
	}


	void setPointLightUniforms(Material* material, ComponentIndex light_cmp)
	{
		if (light_cmp < 0) return;

		Universe& universe = m_scene->getUniverse();
		Entity light_entity = m_scene->getPointLightEntity(light_cmp);
		Vec3 light_pos = universe.getPosition(light_entity);
		Vec3 light_dir = universe.getRotation(light_entity) * Vec3(0, 0, 1);
		float fov = Math::degreesToRadians(m_scene->getLightFOV(light_cmp));
		Vec3 color = m_scene->getPointLightColor(light_cmp) *
					 m_scene->getPointLightIntensity(light_cmp);
		float range = m_scene->getLightRange(light_cmp);
		float attenuation = m_scene->getLightAttenuation(light_cmp);
		Vec4 attenuation_params(range, attenuation, 0, 1);
		Vec4 light_pos_radius(light_pos, range);
		Vec4 light_color(color, 0);
		Vec4 light_dir_fov(light_dir, fov);
		Vec4 light_specular(m_scene->getPointLightSpecularColor(light_cmp), 1);

		bgfx::setUniform(m_attenuation_params_uniform, &attenuation_params);
		bgfx::setUniform(m_light_pos_radius_uniform, &light_pos_radius);
		bgfx::setUniform(m_light_color_uniform, &light_color);
		bgfx::setUniform(m_light_dir_fov_uniform, &light_dir_fov);
		bgfx::setUniform(m_light_specular_uniform, &light_specular);

		if (m_scene->getLightCastShadows(light_cmp))
		{
			setPointLightShadowmapUniforms(material, light_cmp);
		}
		else
		{
			material->unsetUserDefine(m_has_shadowmap_define_idx);
		}
	}


	void setPointLightShadowmapUniforms(Material* material, ComponentIndex light)
	{
		for (auto& info : m_point_light_shadowmaps)
		{
			if (info.m_light == light)
			{
				material->setUserDefine(m_has_shadowmap_define_idx);

				bgfx::setUniform(m_shadowmap_matrices_uniform,
					&info.m_matrices[0].m11,
					m_scene->getLightFOV(light) > 180 ? 4 : 1);

				int texture_offset = material->getShader()->getTextureSlotCount();
				bgfx::setTexture(texture_offset,
					m_tex_shadowmap_uniform,
					info.m_framebuffer->getRenderbufferHandle(0));
				return;
			}
		}
		material->unsetUserDefine(m_has_shadowmap_define_idx);
	}


	void setDirectionalLightUniforms(ComponentIndex light_cmp) const
	{
		if (light_cmp < 0) return;

		Universe& universe = m_scene->getUniverse();
		Entity light_entity = m_scene->getGlobalLightEntity(light_cmp);
		Vec3 light_dir = universe.getRotation(light_entity) * Vec3(0, 0, 1);
		Vec3 diffuse_color = m_scene->getGlobalLightColor(light_cmp) *
							 m_scene->getGlobalLightIntensity(light_cmp);
		Vec3 ambient_color = m_scene->getLightAmbientColor(light_cmp) *
							 m_scene->getLightAmbientIntensity(light_cmp);
		Vec4 diffuse_light_color(diffuse_color, 1);
		Vec3 fog_color = m_scene->getFogColor(light_cmp);
		float fog_density = m_scene->getFogDensity(light_cmp);
		Vec4 ambient_light_color(ambient_color, 1);
		Vec4 light_dir_fov(light_dir, 0);
		fog_density *= fog_density * fog_density;
		Vec4 fog_color_density(fog_color, fog_density);
		Vec4 fog_params(m_scene->getFogBottom(light_cmp), m_scene->getFogHeight(light_cmp), 0, 0);

		bgfx::setUniform(m_light_color_uniform, &diffuse_light_color);
		bgfx::setUniform(m_ambient_color_uniform, &ambient_light_color);
		bgfx::setUniform(m_light_dir_fov_uniform, &light_dir_fov);
		bgfx::setUniform(m_fog_color_density_uniform, &fog_color_density);
		bgfx::setUniform(m_fog_params_uniform, &fog_params);
		bgfx::setUniform(m_shadowmap_matrices_uniform, &m_shadow_viewprojection, 4);
	}


	void disableBlending() { m_render_state &= ~BGFX_STATE_BLEND_MASK; }

	void enableDepthWrite() { m_render_state |= BGFX_STATE_DEPTH_WRITE; }
	void disableDepthWrite() { m_render_state &= ~BGFX_STATE_DEPTH_WRITE; }

	void enableAlphaWrite() { m_render_state |= BGFX_STATE_ALPHA_WRITE; }
	void disableAlphaWrite() { m_render_state &= ~BGFX_STATE_ALPHA_WRITE; }

	void enableRGBWrite() { m_render_state |= BGFX_STATE_RGB_WRITE; }
	void disableRGBWrite() { m_render_state &= ~BGFX_STATE_RGB_WRITE; }


	void renderPointLightInfluencedGeometry(ComponentIndex light, int64 layer_mask)
	{
		PROFILE_FUNCTION();

		m_tmp_meshes.clear();

		m_scene->getPointLightInfluencedGeometry(light, m_tmp_meshes, layer_mask);

		renderMeshes(m_tmp_meshes);
	}


	void renderPointLightInfluencedGeometry(const Frustum& frustum,
											int64 layer_mask)
	{
		PROFILE_FUNCTION();

		Array<ComponentIndex> lights(m_allocator);
		m_scene->getPointLights(frustum, lights);
		for (int i = 0; i < lights.size(); ++i)
		{
			m_tmp_grasses.clear();
			m_tmp_meshes.clear();
			m_tmp_terrains.clear();

			ComponentIndex light = lights[i];
			m_current_light = light;
			m_is_current_light_global = false;
			m_scene->getPointLightInfluencedGeometry(light, frustum, m_tmp_meshes, layer_mask);

			m_scene->getTerrainInfos(m_tmp_terrains,
				layer_mask,
				m_scene->getUniverse().getPosition(m_scene->getCameraEntity(m_applied_camera)),
				m_renderer.getFrameAllocator());

			m_scene->getGrassInfos(frustum, m_tmp_grasses, layer_mask, m_applied_camera);
			renderMeshes(m_tmp_meshes);
			renderTerrains(m_tmp_terrains);
			renderGrasses(m_tmp_grasses);
		}
		m_current_light = -1;
	}


	void drawQuad(float x, float y, float w, float h, int material_index)
	{
		Material* material = m_materials[material_index];
		if (!material->isReady() || !bgfx::checkAvailTransientVertexBuffer(3, m_base_vertex_decl))
		{
			bgfx::touch(m_view_idx);
			return;
		}

		Matrix projection_mtx;
		projection_mtx.setOrtho(-1, 1, 1, -1, 0, 30);

		bgfx::setViewTransform(m_view_idx, &Matrix::IDENTITY.m11, &projection_mtx.m11);
		if (m_current_framebuffer)
		{
			bgfx::setViewRect(m_view_idx,
				m_view_x,
				m_view_y,
				(uint16)m_current_framebuffer->getWidth(),
				(uint16)m_current_framebuffer->getHeight());
		}
		else
		{
			bgfx::setViewRect(m_view_idx, m_view_x, m_view_y, (uint16)m_width, (uint16)m_height);
		}

		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 6, m_base_vertex_decl);
		BaseVertex* vertex = (BaseVertex*)vb.data;
		float x2 = x + w;
		float y2 = y + h;

		vertex[0].m_x = x;
		vertex[0].m_y = y;
		vertex[0].m_z = 0;
		vertex[0].m_rgba = 0xffffffff;
		vertex[0].m_u = 0;
		vertex[0].m_v = 0;

		vertex[1].m_x = x2;
		vertex[1].m_y = y;
		vertex[1].m_z = 0;
		vertex[1].m_rgba = 0xffffffff;
		vertex[1].m_u = 1;
		vertex[1].m_v = 0;

		vertex[2].m_x = x2;
		vertex[2].m_y = y2;
		vertex[2].m_z = 0;
		vertex[2].m_rgba = 0xffffffff;
		vertex[2].m_u = 1;
		vertex[2].m_v = 1;

		vertex[3].m_x = x;
		vertex[3].m_y = y;
		vertex[3].m_z = 0;
		vertex[3].m_rgba = 0xffffffff;
		vertex[3].m_u = 0;
		vertex[3].m_v = 0;

		vertex[4].m_x = x2;
		vertex[4].m_y = y2;
		vertex[4].m_z = 0;
		vertex[4].m_rgba = 0xffffffff;
		vertex[4].m_u = 1;
		vertex[4].m_v = 1;

		vertex[5].m_x = x;
		vertex[5].m_y = y2;
		vertex[5].m_z = 0;
		vertex[5].m_rgba = 0xffffffff;
		vertex[5].m_u = 0;
		vertex[5].m_v = 1;

		for (int i = 0; i < material->getUniformCount(); ++i)
		{
			const Material::Uniform& uniform = material->getUniform(i);

			switch (uniform.m_type)
			{
				case Material::Uniform::FLOAT:
				{
					Vec4 v(uniform.m_float, 0, 0, 0);
					bgfx::setUniform(uniform.m_handle, &v);
				}
				break;
				case Material::Uniform::TIME:
				{
					Vec4 v(m_scene->getTime(), 0, 0, 0);
					bgfx::setUniform(uniform.m_handle, &v);
				}
				break;
				default: ASSERT(false); break;
			}
		}

		Shader* shader = material->getShader();
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			Texture* texture = material->getTexture(i);
			if (texture)
			{
				bgfx::setTexture(
					i, shader->getTextureSlot(i).m_uniform_handle, texture->getTextureHandle());
			}
		}

		if (m_applied_camera >= 0)
		{
			Matrix projection_matrix;
			float fov = m_scene->getCameraFOV(m_applied_camera);
			float near_plane = m_scene->getCameraNearPlane(m_applied_camera);
			float far_plane = m_scene->getCameraFarPlane(m_applied_camera);
			float ratio = float(m_width) / m_height;
			projection_matrix.setPerspective(
				Math::degreesToRadians(fov), ratio, near_plane, far_plane);
			projection_matrix.inverse();

			bgfx::setUniform(m_cam_inv_proj_uniform, &projection_matrix.m11);

			Universe& universe = m_scene->getUniverse();
			Matrix mtx = universe.getMatrix(m_scene->getCameraEntity(m_applied_camera));

			bgfx::setUniform(m_cam_view_uniform, &mtx.m11);
		}

		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::setVertexBuffer(&vb);
		bgfx::submit(m_view_idx, material->getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void renderAll(const Frustum& frustum, int64 layer_mask, bool render_grass)
	{
		PROFILE_FUNCTION();

		if (m_applied_camera < 0) return;

		m_tmp_grasses.clear();
		m_tmp_meshes.clear();
		m_tmp_terrains.clear();

		m_scene->getRenderableInfos(frustum, m_tmp_meshes, layer_mask);
		Entity camera_entity = m_scene->getCameraEntity(m_applied_camera);
		Vec3 camera_pos = m_scene->getUniverse().getPosition(camera_entity);
		LIFOAllocator& frame_allocator = m_renderer.getFrameAllocator();
		m_scene->getTerrainInfos(m_tmp_terrains, layer_mask, camera_pos, frame_allocator);

		m_is_current_light_global = true;
		m_current_light = m_scene->getActiveGlobalLight();

		renderMeshes(m_tmp_meshes);
		renderTerrains(m_tmp_terrains);
		if (render_grass)
		{
			m_scene->getGrassInfos(frustum, m_tmp_grasses, layer_mask, m_applied_camera);
			renderGrasses(m_tmp_grasses);
		}

		m_current_light = -1;
	}


	void toggleStats() override
	{
		m_debug_flags ^= BGFX_DEBUG_STATS;
		bgfx::setDebug(m_debug_flags);
	}


	void setWindowHandle(void* data) override
	{
		m_default_framebuffer =
			LUMIX_NEW(m_allocator, FrameBuffer)("default", m_width, m_height, data);
	}


	void renderModel(Model& model, const Matrix& mtx) override
	{
		for (int i = 0; i < model.getMeshCount(); ++i)
		{
			auto& mesh = model.getMesh(i);
			int instance_idx = mesh.getInstanceIdx();
			if (instance_idx == -1)
			{
				instance_idx = m_instance_data_idx;
				m_instance_data_idx = (m_instance_data_idx + 1) % lengthOf(m_instances_data);
				if (m_instances_data[instance_idx].buffer)
				{
					finishInstances(instance_idx);
				}
				mesh.setInstanceIdx(instance_idx);
			}
			InstanceData& data = m_instances_data[instance_idx];
			if (!data.buffer)
			{
				data.buffer =
					bgfx::allocInstanceDataBuffer(InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix));
				data.instance_count = 0;
				data.mesh = &mesh;
				data.model = &model;
			}
			Matrix* mtcs = (Matrix*)data.buffer->data;
			mtcs[data.instance_count] = mtx;
			++data.instance_count;

			if (data.instance_count == InstanceData::MAX_INSTANCE_COUNT)
			{
				finishInstances(instance_idx);
			}
		}
	}


	void setPoseUniform(const RenderableMesh& renderable_mesh) const
	{
		Matrix bone_mtx[64];
		
		Renderable* renderable = m_scene->getRenderable(renderable_mesh.renderable);
		const Pose& pose = *renderable->pose;
		const Model& model = *renderable->model;
		Vec3* poss = pose.getPositions();
		Quat* rots = pose.getRotations();

		ASSERT(pose.getCount() <= lengthOf(bone_mtx));
		for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count;
			 ++bone_index)
		{
			rots[bone_index].toMatrix(bone_mtx[bone_index]);
			bone_mtx[bone_index].translate(poss[bone_index]);
			bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
		}
		bgfx::setUniform(m_bone_matrices_uniform, bone_mtx, pose.getCount());
	}


	void renderSkinnedMesh(const Renderable& renderable, const RenderableMesh& info)
	{
		const Mesh& mesh = *info.mesh;
		Material* material = mesh.getMaterial();

		setPoseUniform(info);
		setMaterial(material);
		bgfx::setTransform(&renderable.matrix);
		bgfx::setVertexBuffer(renderable.model->getVerticesHandle(),
			mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(),
			mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
		bgfx::setIndexBuffer(
			renderable.model->getIndicesHandle(), mesh.getIndicesOffset(), mesh.getIndexCount());
		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::submit(m_view_idx,
			mesh.getMaterial()->getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void setScissor(int x, int y, int width, int height) override
	{
		bgfx::setScissor(x, y, width, height);
	}


	void setTexture(int slot, bgfx::TextureHandle texture, bgfx::UniformHandle uniform) override
	{
		bgfx::setTexture(slot, uniform, texture);
	}


	void render(TransientGeometry& geom,
		const Matrix& mtx,
		int first_index,
		int num_indices,
		uint64 render_states,
		bgfx::ProgramHandle program_handle) override
	{
		bgfx::setState(m_render_state | render_states);
		bgfx::setTransform(&mtx.m11);
		bgfx::setVertexBuffer(&geom.getVertexBuffer());
		bgfx::setIndexBuffer(&geom.getIndexBuffer(), first_index, num_indices);
		bgfx::submit(m_view_idx, program_handle);
	}


	void renderRigidMesh(const Renderable& renderable, const RenderableMesh& info)
	{
		int instance_idx = info.mesh->getInstanceIdx();
		if (instance_idx == -1)
		{
			instance_idx = m_instance_data_idx;
			m_instance_data_idx = (m_instance_data_idx + 1) % lengthOf(m_instances_data);
			if (m_instances_data[instance_idx].buffer)
			{
				finishInstances(instance_idx);
			}
			info.mesh->setInstanceIdx(instance_idx);
		}
		InstanceData& data = m_instances_data[instance_idx];
		if (!data.buffer)
		{
			data.buffer =
				bgfx::allocInstanceDataBuffer(InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix));
			data.instance_count = 0;
			data.mesh = info.mesh;
			data.model = renderable.model;
		}
		Matrix* mtcs = (Matrix*)data.buffer->data;
		mtcs[data.instance_count] = renderable.matrix;
		++data.instance_count;

		if (data.instance_count == InstanceData::MAX_INSTANCE_COUNT)
		{
			finishInstances(instance_idx);
		}
	}


	void setMaterial(Material* material)
	{
		if (m_is_current_light_global)
		{
			setDirectionalLightUniforms(m_current_light);
		}
		else
		{
			setPointLightUniforms(material, m_current_light);
		}

		for (int i = 0; i < material->getUniformCount(); ++i)
		{
			const Material::Uniform& uniform = material->getUniform(i);

			switch (uniform.m_type)
			{
				case Material::Uniform::FLOAT:
				{
					Vec4 v(uniform.m_float, 0, 0, 0);
					bgfx::setUniform(uniform.m_handle, &v);
				}
				break;
				case Material::Uniform::TIME:
				{
					Vec4 v(m_scene->getTime(), 0, 0, 0);
					bgfx::setUniform(uniform.m_handle, &v);
				}
				break;
				default: ASSERT(false); break;
			}
		}

		Shader* shader = material->getShader();
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			Texture* texture = material->getTexture(i);
			if (!texture) continue;

			bgfx::setTexture(
				i, shader->getTextureSlot(i).m_uniform_handle, texture->getTextureHandle());
		}

		Vec4 specular_shininess(material->getSpecular(), material->getShininess());
		bgfx::setUniform(m_specular_shininess_uniform, &specular_shininess);

		if (m_is_current_light_global && !m_is_rendering_in_shadowmap && m_global_light_shadowmap)
		{
			auto handle = m_global_light_shadowmap->getRenderbufferHandle(0);
			bgfx::setTexture(shader->getTextureSlotCount(), m_tex_shadowmap_uniform, handle);
		}
	}


	void renderTerrain(const TerrainInfo& info)
	{
		auto& inst = m_terrain_instances[info.m_index];
		if ((inst.m_count > 0 && inst.m_infos[0]->m_terrain != info.m_terrain) ||
			inst.m_count == lengthOf(inst.m_infos))
		{
			finishTerrainInstances(info.m_index);
		}
		inst.m_infos[inst.m_count] = &info;
		++inst.m_count;
	}


	void finishTerrainInstances(int index)
	{
		if (m_terrain_instances[index].m_count == 0) return;

		const TerrainInfo& info = *m_terrain_instances[index].m_infos[0];
		Material* material = info.m_terrain->getMaterial();
		if (!material->isReady()) return;

		Texture* detail_texture = info.m_terrain->getDetailTexture();
		if (!detail_texture) return;
		Texture* splat_texture = info.m_terrain->getSplatmap();
		if (!splat_texture) return;

		Matrix inv_world_matrix;
		inv_world_matrix = info.m_world_matrix;
		inv_world_matrix.fastInverse();
		Vec3 camera_pos =
			m_scene->getUniverse().getPosition(m_scene->getCameraEntity(m_applied_camera));

		Vec4 rel_cam_pos(
			inv_world_matrix.multiplyPosition(camera_pos) / info.m_terrain->getXZScale(), 1);
		Vec4 terrain_scale(info.m_terrain->getScale(), 0);
		const Mesh& mesh = *info.m_terrain->getMesh();

		Vec4 terrain_params(info.m_terrain->getRootSize(),
			(float)detail_texture->getWidth(),
			(float)detail_texture->getAtlasSize(),
			(float)splat_texture->getWidth());
		bgfx::setUniform(m_terrain_params_uniform, &terrain_params);
		bgfx::setUniform(m_rel_camera_pos_uniform, &rel_cam_pos);
		bgfx::setUniform(m_terrain_scale_uniform, &terrain_scale);
		bgfx::setUniform(m_terrain_matrix_uniform, &info.m_world_matrix.m11);

		setMaterial(material);

		struct TerrainInstanceData
		{
			Vec4 m_quad_min_and_size;
			Vec4 m_morph_const;
		};
		const bgfx::InstanceDataBuffer* instance_buffer = bgfx::allocInstanceDataBuffer(
			m_terrain_instances[index].m_count, sizeof(TerrainInstanceData));
		TerrainInstanceData* instance_data = (TerrainInstanceData*)instance_buffer->data;

		for (int i = 0; i < m_terrain_instances[index].m_count; ++i)
		{
			const TerrainInfo& info = *m_terrain_instances[index].m_infos[i];
			instance_data[i].m_quad_min_and_size.set(
				info.m_min.x, info.m_min.y, info.m_min.z, info.m_size);
			instance_data[i].m_morph_const.set(
				info.m_morph_const.x, info.m_morph_const.y, info.m_morph_const.z, 0);
		}

		bgfx::setVertexBuffer(info.m_terrain->getVerticesHandle());
		int mesh_part_indices_count = mesh.getIndexCount() / 4;
		bgfx::setIndexBuffer(info.m_terrain->getIndicesHandle(),
			info.m_index * mesh_part_indices_count,
			mesh_part_indices_count);
		bgfx::setState(m_render_state | mesh.getMaterial()->getRenderStates());
		bgfx::setInstanceDataBuffer(instance_buffer, m_terrain_instances[index].m_count);
		auto shader_instance = material->getShaderInstance().m_program_handles[m_pass_idx];
		bgfx::submit(m_view_idx, shader_instance);

		m_terrain_instances[index].m_count = 0;
	}


	void renderGrass(const GrassInfo& grass)
	{
		const bgfx::InstanceDataBuffer* idb =
			bgfx::allocInstanceDataBuffer(grass.m_matrix_count, sizeof(Matrix));
		copyMemory(idb->data, &grass.m_matrices[0], grass.m_matrix_count * sizeof(Matrix));
		const Mesh& mesh = grass.m_model->getMesh(0);
		Material* material = mesh.getMaterial();

		setMaterial(material);
		bgfx::setVertexBuffer(grass.m_model->getVerticesHandle(),
			mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(),
			mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
		bgfx::setIndexBuffer(
			grass.m_model->getIndicesHandle(), mesh.getIndicesOffset(), mesh.getIndexCount());
		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::setInstanceDataBuffer(idb, grass.m_matrix_count);
		bgfx::submit(m_view_idx, material->getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void renderGrasses(const Array<GrassInfo>& grasses)
	{
		PROFILE_FUNCTION();
		for (const auto& grass : grasses)
		{
			renderGrass(grass);
		}
	}


	void renderTerrains(const Array<const TerrainInfo*>& terrains)
	{
		PROFILE_FUNCTION();
		PROFILE_INT("terrain patches", terrains.size());
		for (auto* info : terrains)
		{
			renderTerrain(*info);
		}
		for (int i = 0; i < lengthOf(m_terrain_instances); ++i)
		{
			finishTerrainInstances(i);
		}
	}


	void renderMeshes(const Array<RenderableMesh>& meshes)
	{
		PROFILE_FUNCTION();
		if (meshes.empty()) return;

		Renderable* renderables = m_scene->getRenderables();
		PROFILE_INT("mesh count", meshes.size());
		for (auto& mesh : meshes)
		{
			Renderable& renderable = renderables[mesh.renderable];
			if (renderable.pose && renderable.pose->getCount() > 0)
			{
				renderSkinnedMesh(renderable, mesh);
			}
			else
			{
				renderRigidMesh(renderable, mesh);
			}
		}
		finishInstances();
	}


	void setViewport(int x, int y, int w, int h) override
	{
		m_view_x = x;
		m_view_y = y;
		if (m_width == w && m_height == h) return;

		if (m_default_framebuffer)
		{
			m_default_framebuffer->resize(w, h);
		}
		m_width = w;
		m_height = h;
	}


	void render() override
	{
		PROFILE_FUNCTION();

		if (!isReady()) return;

		m_render_state = BGFX_STATE_RGB_WRITE | BGFX_STATE_ALPHA_WRITE | BGFX_STATE_DEPTH_WRITE |
						 BGFX_STATE_MSAA;
		m_applied_camera = INVALID_COMPONENT;
		m_global_light_shadowmap = nullptr;
		m_render_state |= m_is_wireframe ? BGFX_STATE_PT_LINESTRIP : 0;
		m_view_idx = m_renderer.getViewCounter();
		m_pass_idx = -1;
		m_current_framebuffer = m_default_framebuffer;
		m_current_light = -1;
		m_view2pass_map.assign(0xFF);
		m_instance_data_idx = 0;
		m_point_light_shadowmaps.clear();
		for (int i = 0; i < lengthOf(m_terrain_instances); ++i)
		{
			m_terrain_instances[i].m_count = 0;
		}
		for (int i = 0; i < lengthOf(m_instances_data); ++i)
		{
			m_instances_data[i].buffer = nullptr;
			m_instances_data[i].instance_count = 0;
		}

		if (lua_getglobal(m_lua_state, "render") == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != LUA_OK)
			{
				g_log_error.log("lua") << lua_tostring(m_lua_state, -1);
				lua_pop(m_lua_state, 1);
			}
		}
		else
		{
			lua_pop(m_lua_state, 1);
		}
		finishInstances();

		m_renderer.getFrameAllocator().clear();
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

		bool errors = luaL_loadbuffer(m_lua_state, tmp, stringLength(tmp), nullptr) != LUA_OK;
		errors = errors || lua_pcall(m_lua_state, 0, LUA_MULTRET, 0) != LUA_OK;

		if (errors)
		{
			g_log_error.log("pipeline") << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
		}
	}


	int loadMaterial(const char* path)
	{
		ResourceManagerBase& material_manager = m_renderer.getMaterialManager();
		auto* material = static_cast<Material*>(material_manager.load(Lumix::Path(path)));

		m_materials.push(material);
		return m_materials.size() - 1;
	}


	int createUniform(const char* name)
	{
		bgfx::UniformHandle handle = bgfx::createUniform(name, bgfx::UniformType::Int1);
		m_uniforms.push(handle);
		return m_uniforms.size() - 1;
	}


	bool hasScene()
	{
		return m_scene != nullptr;
	}


	bool cameraExists(const char* slot_name)
	{
		return m_scene->getCameraInSlot(slot_name) != INVALID_ENTITY;
	}


	void enableBlending(const char* mode)
	{
		uint64 mode_value = 0;
		if (compareString(mode, "alpha") == 0) mode_value = BGFX_STATE_BLEND_ALPHA;
		else if (compareString(mode, "add") == 0) mode_value = BGFX_STATE_BLEND_ADD;
		else if (compareString(mode, "multiply") == 0) mode_value = BGFX_STATE_BLEND_MULTIPLY;

		m_render_state |= mode_value;
	}


	void clear(const char* buffers, int color)
	{
		uint16 flags = 0;
		if (compareString(buffers, "all") == 0)
		{
			flags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;
		}
		else if (compareString(buffers, "depth") == 0)
		{
			flags = BGFX_CLEAR_DEPTH;
		}
		bgfx::setViewClear(m_view_idx, flags, color, 1.0f, 0);
		bgfx::touch(m_view_idx);
	}


	void renderModels(int64 layer_mask, bool is_point_light_render)
	{
		if (is_point_light_render)
		{
			renderPointLightInfluencedGeometry(m_camera_frustum, layer_mask);
		}
		else
		{
			renderAll(m_camera_frustum, layer_mask, true);
		}
	}


	bool isReady() const override { return m_is_ready; }
	void setScene(RenderScene* scene) override { m_scene = scene; }
	void setWireframe(bool wireframe) override { m_is_wireframe = wireframe; }


	struct TerrainInstance
	{
		int m_count;
		const TerrainInfo* m_infos[64];
	};


	struct PointLightShadowmap
	{
		ComponentIndex m_light;
		FrameBuffer* m_framebuffer;
		Matrix m_matrices[4];
	};


	struct BaseVertex
	{
		float m_x, m_y, m_z;
		uint32 m_rgba;
		float m_u;
		float m_v;
	};


	bgfx::VertexDecl m_base_vertex_decl;
	TerrainInstance m_terrain_instances[4];
	uint32 m_debug_flags;
	uint8 m_view_idx;
	int m_pass_idx;
	StaticArray<uint8, 256> m_view2pass_map;
	uint64 m_render_state;
	IAllocator& m_allocator;
	Lumix::Path m_path;
	Renderer& m_renderer;
	RenderScene* m_scene;
	FrameBuffer* m_current_framebuffer;
	FrameBuffer* m_default_framebuffer;
	Array<FrameBuffer*> m_framebuffers;
	Array<bgfx::UniformHandle> m_uniforms;
	Array<Material*> m_materials;
	Array<PointLightShadowmap> m_point_light_shadowmaps;
	FrameBuffer* m_global_light_shadowmap;
	InstanceData m_instances_data[128];
	int m_instance_data_idx;
	ComponentIndex m_applied_camera;
	ComponentIndex m_current_light;
	bool m_is_current_light_global;
	bool m_is_wireframe;
	bool m_is_rendering_in_shadowmap;
	bool m_is_ready;
	Frustum m_camera_frustum;

	Matrix m_shadow_viewprojection[4];
	int m_view_x;
	int m_view_y;
	int m_width;
	int m_height;
	bgfx::VertexBufferHandle m_particle_vertex_buffer;
	bgfx::IndexBufferHandle m_particle_index_buffer;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<RenderableMesh> m_tmp_meshes;
	Array<const TerrainInfo*> m_tmp_terrains;
	Array<GrassInfo> m_tmp_grasses;

	bgfx::UniformHandle m_specular_shininess_uniform;
	bgfx::UniformHandle m_bone_matrices_uniform;
	bgfx::UniformHandle m_terrain_scale_uniform;
	bgfx::UniformHandle m_rel_camera_pos_uniform;
	bgfx::UniformHandle m_terrain_params_uniform;
	bgfx::UniformHandle m_fog_color_density_uniform;
	bgfx::UniformHandle m_fog_params_uniform;
	bgfx::UniformHandle m_light_pos_radius_uniform;
	bgfx::UniformHandle m_light_color_uniform;
	bgfx::UniformHandle m_ambient_color_uniform;
	bgfx::UniformHandle m_light_dir_fov_uniform;
	bgfx::UniformHandle m_shadowmap_matrices_uniform;
	bgfx::UniformHandle m_light_specular_uniform;
	bgfx::UniformHandle m_terrain_matrix_uniform;
	bgfx::UniformHandle m_attenuation_params_uniform;
	bgfx::UniformHandle m_tex_shadowmap_uniform;
	bgfx::UniformHandle m_cam_view_uniform;
	bgfx::UniformHandle m_cam_inv_proj_uniform;
	bgfx::UniformHandle m_texture_size_uniform;

	Material* m_debug_line_material;
	int m_has_shadowmap_define_idx;
};


Pipeline* Pipeline::create(Renderer& renderer, const Path& path, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, PipelineImpl)(renderer, path, allocator);
}


void Pipeline::destroy(Pipeline* pipeline)
{
	LUMIX_DELETE(static_cast<PipelineImpl*>(pipeline)->m_allocator, pipeline);
}


namespace LuaAPI
{
	

void logError(const char* message)
{
	g_log_error.log("renderer") << message;
}


int renderLocalLightsShadowmaps(lua_State* L)
{
	if (!LuaWrapper::isType<PipelineImpl*>(L, 1)
		|| !LuaWrapper::isType<int>(L, 2)
		|| !LuaWrapper::isType<const char*>(L, 4))
	{
		return 0;
	}

	FrameBuffer* fbs[16];
	auto* pipeline = (PipelineImpl*)lua_touserdata(L, 1);
	int len = Math::minValue((int)lua_rawlen(L, 3), lengthOf(fbs));
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, 3, 1 + i) == LUA_TSTRING)
		{
			const char* fb_name = lua_tostring(L, -1);
			fbs[i] = pipeline->getFramebuffer(fb_name);
		}
		lua_pop(L, 1);
	}

	RenderScene* scene = pipeline->m_scene;
	int64 layer_mask = (int64)lua_tonumber(L, 2);
	ComponentIndex camera = scene->getCameraInSlot(lua_tostring(L, 4));
	pipeline->renderLocalLightShadowmaps(camera, fbs, len, layer_mask);

	return 0;
}


void print(int x, int y, const char* text)
{
	bgfx::dbgTextPrintf(x, y, 0x4f, text);
}


} // namespace LuaAPI


void PipelineImpl::registerCFunctions()
{
	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<PipelineImpl, decltype(&PipelineImpl::name), &PipelineImpl::name>; \
			registerCFunction(#name, f); \
		} while(false) \

	REGISTER_FUNCTION(drawQuad);
	REGISTER_FUNCTION(setPass);
	REGISTER_FUNCTION(beginNewView);
	REGISTER_FUNCTION(bindFramebufferTexture);
	REGISTER_FUNCTION(applyCamera);
	
	REGISTER_FUNCTION(disableBlending);
	REGISTER_FUNCTION(enableAlphaWrite);
	REGISTER_FUNCTION(disableAlphaWrite);
	REGISTER_FUNCTION(enableRGBWrite);
	REGISTER_FUNCTION(disableRGBWrite);
	REGISTER_FUNCTION(enableDepthWrite);
	REGISTER_FUNCTION(disableDepthWrite);
	REGISTER_FUNCTION(renderDebugShapes);
	REGISTER_FUNCTION(setFramebuffer);
	REGISTER_FUNCTION(renderParticles);
	REGISTER_FUNCTION(loadMaterial);
	REGISTER_FUNCTION(executeCustomCommand);
	REGISTER_FUNCTION(getFPS);
	REGISTER_FUNCTION(createUniform);
	REGISTER_FUNCTION(hasScene);
	REGISTER_FUNCTION(cameraExists);
	REGISTER_FUNCTION(enableBlending);
	REGISTER_FUNCTION(clear);
	REGISTER_FUNCTION(renderModels);
	REGISTER_FUNCTION(renderShadowmap);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(name) \
		registerCFunction(#name, LuaWrapper::wrap<decltype(&LuaAPI::name), LuaAPI::name>)

	REGISTER_FUNCTION(print);
	REGISTER_FUNCTION(logError);
	REGISTER_FUNCTION(renderLocalLightsShadowmaps);

	#undef REGISTER_FUNCTION
	
	for(auto& handler : m_custom_commands_handlers)
	{
		exposeCustomCommandToLua(handler);
	}
}


} // ~namespace Lumix
