#include "pipeline.h"

#include "engine/core/crc32.h"
#include "engine/core/fs/disk_file_device.h"
#include "engine/core/fs/file_system.h"
#include "engine/core/geometry.h"
#include "engine/core/lifo_allocator.h"
#include "engine/core/log.h"
#include "engine/core/lua_wrapper.h"
#include "engine/core/profiler.h"
#include "engine/engine.h"
#include "lua_script/lua_script_system.h"
#include "renderer/frame_buffer.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "engine/universe/universe.h"
#include <bgfx/bgfx.h>
#include <cmath>


namespace Lumix
{


static const float SHADOW_CAM_NEAR = 50.0f;
static const float SHADOW_CAM_FAR = 5000.0f;


struct InstanceData
{
	static const int MAX_INSTANCE_COUNT = 128;

	const bgfx::InstanceDataBuffer* buffer;
	int instance_count;
	Mesh* mesh;
	Model* model;
};


struct View
{
	uint8 bgfx_id;
	uint64 render_state;
	uint32 stencil;
	int pass_idx;
	CommandBufferGenerator command_buffer;
};


enum class BufferCommands : uint8
{
	END,
	SET_TEXTURE,
	SET_UNIFORM_VEC4,
	SET_UNIFORM_TIME,
	SET_UNIFORM_ARRAY,
	SET_GLOBAL_SHADOWMAP,
	SET_LOCAL_SHADOWMAP,

	COUNT
};


#pragma pack(1)
struct SetTextureCommand
{
	SetTextureCommand() : type(BufferCommands::SET_TEXTURE) {}
	BufferCommands type;
	uint8 stage;
	bgfx::UniformHandle uniform;
	bgfx::TextureHandle texture;
};


struct SetUniformVec4Command
{
	SetUniformVec4Command() : type(BufferCommands::SET_UNIFORM_VEC4) {}
	BufferCommands type;
	bgfx::UniformHandle uniform;
	Vec4 value;
};


struct SetUniformTimeCommand
{
	SetUniformTimeCommand() : type(BufferCommands::SET_UNIFORM_TIME) {}
	BufferCommands type;
	bgfx::UniformHandle uniform;
};


struct SetLocalShadowmapCommand
{
	SetLocalShadowmapCommand() : type(BufferCommands::SET_LOCAL_SHADOWMAP) {}
	BufferCommands type;
	bgfx::TextureHandle texture;
};


struct SetUniformArrayCommand
{
	SetUniformArrayCommand() : type(BufferCommands::SET_UNIFORM_ARRAY) {}
	BufferCommands type;
	bgfx::UniformHandle uniform;
	uint16 size;
	uint16 count;
};


#pragma pack()


CommandBufferGenerator::CommandBufferGenerator()
{
	pointer = buffer;
}


void CommandBufferGenerator::setTexture(uint8 stage,
	const bgfx::UniformHandle& uniform,
	const bgfx::TextureHandle& texture)
{
	SetTextureCommand cmd;
	cmd.stage = stage;
	cmd.uniform = uniform;
	cmd.texture = texture;
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
}


void CommandBufferGenerator::setUniform(const bgfx::UniformHandle& uniform, const Vec4& value)
{
	SetUniformVec4Command cmd;
	cmd.uniform = uniform;
	cmd.value = value;
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
}


void CommandBufferGenerator::setUniform(const bgfx::UniformHandle& uniform, const Vec4* values, int count)
{
	SetUniformArrayCommand cmd;
	cmd.uniform = uniform;
	cmd.count = count;
	cmd.size = uint16(count * sizeof(Vec4));
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
	ASSERT(pointer + cmd.size - buffer <= sizeof(buffer));
	copyMemory(pointer, values, cmd.size);
	pointer += cmd.size;
}


void CommandBufferGenerator::setUniform(const bgfx::UniformHandle& uniform, const Matrix* values, int count)
{
	SetUniformArrayCommand cmd;
	cmd.uniform = uniform;
	cmd.count = count;
	cmd.size = uint16(count * sizeof(Matrix));
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
	ASSERT(pointer + cmd.size - buffer <= sizeof(buffer));
	copyMemory(pointer, values, cmd.size);
	pointer += cmd.size;
}


void CommandBufferGenerator::setGlobalShadowmap()
{
	ASSERT(pointer + 1 - buffer <= sizeof(buffer));
	*pointer = (uint8)BufferCommands::SET_GLOBAL_SHADOWMAP;
	pointer += 1;
}


void CommandBufferGenerator::setLocalShadowmap(const bgfx::TextureHandle& shadowmap)
{
	SetLocalShadowmapCommand cmd;
	cmd.texture = shadowmap;
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
}


void CommandBufferGenerator::setTimeUniform(const bgfx::UniformHandle& uniform)
{
	SetUniformTimeCommand cmd;
	cmd.uniform = uniform;
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
}


void CommandBufferGenerator::getData(uint8* data)
{
	copyMemory(data, buffer, pointer - buffer);
}


void CommandBufferGenerator::clear()
{
	buffer[0] = (uint8)BufferCommands::END;
	pointer = buffer;
}


void CommandBufferGenerator::beginAppend()
{
	if (pointer != buffer) --pointer;
}


void CommandBufferGenerator::end()
{
	ASSERT(pointer + 1 - buffer <= sizeof(buffer));
	*pointer = (uint8)BufferCommands::END;
	++pointer;
}



struct PipelineImpl : public Pipeline
{
	PipelineImpl(Renderer& renderer, const Path& path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_path(path)
		, m_framebuffers(allocator)
		, m_lua_state(nullptr)
		, m_custom_commands_handlers(allocator)
		, m_tmp_terrains(allocator)
		, m_tmp_grasses(allocator)
		, m_tmp_meshes(allocator)
		, m_tmp_local_lights(allocator)
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
		m_first_postprocess_framebuffer = 0;
		m_deferred_point_light_vertex_decl.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.end();

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
			renderer.getMaterialManager().load(Lumix::Path("shaders/debug_line.mat")));

		m_scene = nullptr;
		m_width = m_height = -1;

		createParticleBuffers();
		createCubeBuffers();
		m_stats = {};
	}


	const Stats& getStats() override
	{
		return m_stats;
	}


	static void parseRenderbuffers(lua_State* L, FrameBuffer::Declaration& decl)
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


	Path& getPath() override
	{
		return m_path;
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

		ResourceManagerBase& material_manager = m_renderer.getMaterialManager();
		for (auto* material : m_materials)
		{
			material_manager.unload(*material);
		}
		m_materials.clear();

		for (int i = 0; i < m_uniforms.size(); ++i)
		{
			bgfx::destroyUniform(m_uniforms[i]);
		}
		m_uniforms.clear();

		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_framebuffers[i]);
			if (m_framebuffers[i] == m_default_framebuffer) m_default_framebuffer = nullptr;
		}
		LUMIX_DELETE(m_allocator, m_default_framebuffer);
		m_framebuffers.clear();
		bgfx::frame();
		bgfx::frame();
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
		lua_pushglobaltable(m_lua_state);
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

		bool errors =
			luaL_loadbuffer(m_lua_state, (const char*)file.getBuffer(), file.size(), m_path.c_str()) != LUA_OK;
		if (errors)
		{
			g_log_error.log("Renderer") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		lua_setupvalue(m_lua_state, -2, 1);
		errors = lua_pcall(m_lua_state, 0, 0, 0) != LUA_OK;
		if (errors)
		{
			g_log_error.log("Renderer") << m_path.c_str() << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
			return;
		}
		m_first_postprocess_framebuffer = m_framebuffers.size();;

		m_width = m_height = -1;
		if(m_scene) callInitScene();

		m_is_ready = true;
	}

	lua_State* m_lua_state;
	int m_lua_thread_ref;
	int m_lua_env;
	Stats m_stats;


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
		m_grass_max_dist_uniform = bgfx::createUniform("u_grassMaxDist", bgfx::UniformType::Vec4);
		m_texture_size_uniform = bgfx::createUniform("u_textureSize", bgfx::UniformType::Vec4);
		m_cam_params = bgfx::createUniform("u_camParams", bgfx::UniformType::Vec4);
		m_cam_proj_uniform = bgfx::createUniform("u_camProj", bgfx::UniformType::Mat4);
		m_cam_view_uniform = bgfx::createUniform("u_camView", bgfx::UniformType::Mat4);
		m_cam_inv_view_uniform = bgfx::createUniform("u_camInvView", bgfx::UniformType::Mat4);
		m_cam_inv_viewproj_uniform = bgfx::createUniform("u_camInvViewProj", bgfx::UniformType::Mat4);
		m_cam_inv_proj_uniform = bgfx::createUniform("u_camInvProj", bgfx::UniformType::Mat4);
		m_tex_shadowmap_uniform = bgfx::createUniform("u_texShadowmap", bgfx::UniformType::Int1);
		m_terrain_scale_uniform = bgfx::createUniform("u_terrainScale", bgfx::UniformType::Vec4);
		m_rel_camera_pos_uniform = bgfx::createUniform("u_relCamPos", bgfx::UniformType::Vec4);
		m_terrain_params_uniform = bgfx::createUniform("u_terrainParams", bgfx::UniformType::Vec4);
		m_fog_params_uniform = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
		m_fog_color_density_uniform =
			bgfx::createUniform("u_fogColorDensity", bgfx::UniformType::Vec4);
		m_light_pos_radius_uniform =
			bgfx::createUniform("u_lightPosRadius", bgfx::UniformType::Vec4);
		m_light_color_attenuation_uniform = bgfx::createUniform("u_lightRgbAttenuation", bgfx::UniformType::Vec4);
		m_light_dir_fov_uniform = bgfx::createUniform("u_lightDirFov", bgfx::UniformType::Vec4);
		m_light_specular_uniform =
			bgfx::createUniform("u_lightSpecular", bgfx::UniformType::Mat4, 64);
		m_ambient_color_uniform = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
		m_shadowmap_matrices_uniform =
			bgfx::createUniform("u_shadowmapMatrices", bgfx::UniformType::Mat4, 4);
		m_bone_matrices_uniform =
			bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, 64);
		m_layer_uniform = bgfx::createUniform("u_layer", bgfx::UniformType::Vec4);
		m_terrain_matrix_uniform = bgfx::createUniform("u_terrainMatrix", bgfx::UniformType::Mat4);
	}


	void destroyUniforms()
	{
		bgfx::destroyUniform(m_tex_shadowmap_uniform);
		bgfx::destroyUniform(m_terrain_matrix_uniform);
		bgfx::destroyUniform(m_bone_matrices_uniform);
		bgfx::destroyUniform(m_layer_uniform);
		bgfx::destroyUniform(m_terrain_scale_uniform);
		bgfx::destroyUniform(m_rel_camera_pos_uniform);
		bgfx::destroyUniform(m_terrain_params_uniform);
		bgfx::destroyUniform(m_fog_params_uniform);
		bgfx::destroyUniform(m_fog_color_density_uniform);
		bgfx::destroyUniform(m_light_pos_radius_uniform);
		bgfx::destroyUniform(m_light_color_attenuation_uniform);
		bgfx::destroyUniform(m_light_dir_fov_uniform);
		bgfx::destroyUniform(m_ambient_color_uniform);
		bgfx::destroyUniform(m_shadowmap_matrices_uniform);
		bgfx::destroyUniform(m_light_specular_uniform);
		bgfx::destroyUniform(m_cam_inv_proj_uniform);
		bgfx::destroyUniform(m_cam_inv_viewproj_uniform);
		bgfx::destroyUniform(m_cam_view_uniform);
		bgfx::destroyUniform(m_cam_proj_uniform);
		bgfx::destroyUniform(m_cam_params);
		bgfx::destroyUniform(m_grass_max_dist_uniform);
		bgfx::destroyUniform(m_cam_inv_view_uniform);
		bgfx::destroyUniform(m_texture_size_uniform);
	}


	~PipelineImpl()
	{
		if(m_lua_state)
		{
			luaL_unref(m_renderer.getEngine().getState(), LUA_REGISTRYINDEX, m_lua_thread_ref);
			luaL_unref(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
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

		bgfx::destroyVertexBuffer(m_cube_vb);
		bgfx::destroyIndexBuffer(m_cube_ib);
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

		auto& view = m_views[m_current_render_views[0]];
		for (int i = 0, c = emitter.m_life.size(); i < c; ++i)
		{
			if (i % PARTICLE_BATCH_SIZE == 0)
			{
				if (instance_buffer)
				{
					executeCommandBuffer(material->getCommandBuffer(), material);
					executeCommandBuffer(view.command_buffer.buffer, material);

					bgfx::setInstanceDataBuffer(instance_buffer, PARTICLE_BATCH_SIZE);
					bgfx::setVertexBuffer(m_particle_vertex_buffer);
					bgfx::setIndexBuffer(m_particle_index_buffer);
					bgfx::setStencil(view.stencil, BGFX_STENCIL_NONE);
					bgfx::setState(view.render_state | material->getRenderStates());
					++m_stats.draw_call_count;
					m_stats.instance_count += PARTICLE_BATCH_SIZE;
					m_stats.triangle_count += PARTICLE_BATCH_SIZE * 2;
					bgfx::submit(view.bgfx_id, material->getShaderInstance().m_program_handles[view.pass_idx]);
				}

				instance_buffer = bgfx::allocInstanceDataBuffer(PARTICLE_BATCH_SIZE, sizeof(Instance));
				instance = (Instance*)instance_buffer->data;
			}

			instance->pos = Vec4(emitter.m_position[i], emitter.m_size[i]);
			instance->alpha_and_rotation = Vec4(emitter.m_alpha[i], emitter.m_rotation[i], 0, 0);
			++instance;
		}

		executeCommandBuffer(material->getCommandBuffer(), material);
		executeCommandBuffer(view.command_buffer.buffer, material);

		int instance_count = emitter.m_life.size() % PARTICLE_BATCH_SIZE;
		bgfx::setInstanceDataBuffer(instance_buffer, instance_count);
		bgfx::setVertexBuffer(m_particle_vertex_buffer);
		bgfx::setIndexBuffer(m_particle_index_buffer);
		bgfx::setStencil(view.stencil, BGFX_STENCIL_NONE);
		bgfx::setState(view.render_state | material->getRenderStates());
		++m_stats.draw_call_count;
		m_stats.instance_count += instance_count;
		m_stats.triangle_count += instance_count * 2;
		bgfx::submit(view.bgfx_id, material->getShaderInstance().m_program_handles[view.pass_idx]);
	}


	void renderParticles()
	{
		PROFILE_FUNCTION();
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
		m_views[m_view_idx].command_buffer.beginAppend();
		if (m_global_textures_count == 0) m_views[m_view_idx].command_buffer.setUniform(m_texture_size_uniform, size);
		m_views[m_view_idx].command_buffer.setTexture(15 - m_global_textures_count,
			m_uniforms[uniform_idx],
			fb->getRenderbufferHandle(renderbuffer_idx));
		++m_global_textures_count;
		m_views[m_view_idx].command_buffer.end();
	}


	void setViewProjection(const Matrix& mtx, int width, int height) override
	{
		bgfx::setViewRect(m_bgfx_view, 0, 0, (uint16_t)width, (uint16_t)height);
		bgfx::setViewTransform(m_bgfx_view, nullptr, &mtx.m11);
	}


	void finishInstances(int idx)
	{
		InstanceData& data = m_instances_data[idx];
		if (!data.buffer) return;

		Mesh& mesh = *data.mesh;
		const Model& model = *data.model;
		Material* material = mesh.material;
		const uint16 stride = model.getVertexDecl().getStride();

		auto& view = m_views[m_current_render_views[0]];

		executeCommandBuffer(material->getCommandBuffer(), material);
		executeCommandBuffer(view.command_buffer.buffer, material);

		bgfx::setVertexBuffer(model.getVerticesHandle(),
							  mesh.attribute_array_offset / stride,
							  mesh.attribute_array_size / stride);
		bgfx::setIndexBuffer(model.getIndicesHandle(),
							 mesh.indices_offset,
							 mesh.indices_count);
		bgfx::setStencil(view.stencil, BGFX_STENCIL_NONE);
		bgfx::setState(view.render_state | material->getRenderStates());
		bgfx::setInstanceDataBuffer(data.buffer, data.instance_count);
		ShaderInstance& shader_instance = mesh.material->getShaderInstance();
		++m_stats.draw_call_count;
		m_stats.instance_count += data.instance_count;
		m_stats.triangle_count += data.instance_count * mesh.indices_count / 3;
		bgfx::submit(view.bgfx_id, shader_instance.m_program_handles[view.pass_idx]);

		data.buffer = nullptr;
		data.instance_count = 0;
		mesh.instance_idx = -1;
	}


	void applyCamera(const char* slot)
	{
		ComponentIndex cmp = m_scene->getCameraInSlot(slot);
		if (cmp < 0) return;

		m_scene->setCameraScreenSize(cmp, m_width, m_height);
		m_applied_camera = cmp;
		m_camera_frustum = m_scene->getCameraFrustum(cmp);

		Matrix projection_matrix;
		float fov = m_scene->getCameraFOV(cmp);
		float near_plane = m_scene->getCameraNearPlane(cmp);
		float far_plane = m_scene->getCameraFarPlane(cmp);
		float ratio = float(m_width) / m_height;
		projection_matrix.setPerspective(
			Math::degreesToRadians(fov), ratio, near_plane, far_plane);
		projection_matrix = m_scene->getCameraProjection(cmp);

		Universe& universe = m_scene->getUniverse();
		Matrix mtx = universe.getMatrix(m_scene->getCameraEntity(cmp));
		mtx.fastInverse();
		bgfx::setViewTransform(m_bgfx_view, &mtx.m11, &projection_matrix.m11);

		bgfx::setViewRect(
			m_bgfx_view, (uint16_t)m_view_x, (uint16_t)m_view_y, (uint16)m_width, (uint16)m_height);
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
		m_views[m_view_idx].pass_idx = m_pass_idx;
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
				bgfx::setViewFrameBuffer(m_bgfx_view,
										 m_current_framebuffer->getHandle());
			}
			else
			{
				bgfx::setViewFrameBuffer(m_bgfx_view, BGFX_INVALID_HANDLE);
			}
			return;
		}
		m_current_framebuffer = getFramebuffer(framebuffer_name);
		if (m_current_framebuffer)
		{
			bgfx::setViewFrameBuffer(m_bgfx_view,
									 m_current_framebuffer->getHandle());
		}
		else
		{
			g_log_warning.log("Renderer") << "Framebuffer " << framebuffer_name
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


	int newView(const char* debug_name)
	{
		++m_view_idx;
		ASSERT(m_view_idx < lengthOf(m_views));
		m_renderer.viewCounterAdd();
		m_bgfx_view = (uint8)m_renderer.getViewCounter();
		auto& view = m_views[m_view_idx];
		view.bgfx_id = m_bgfx_view;
		view.render_state = m_render_state;
		view.stencil = m_stencil;
		view.pass_idx = m_pass_idx;
		view.command_buffer.clear();
		m_global_textures_count = 0;
		if (m_current_framebuffer)
		{
			bgfx::setViewFrameBuffer(m_bgfx_view, m_current_framebuffer->getHandle());
		}
		else
		{
			bgfx::setViewFrameBuffer(m_bgfx_view, BGFX_INVALID_HANDLE);
		}
		bgfx::setViewClear(m_bgfx_view, 0);
		bgfx::setViewName(m_bgfx_view, debug_name);
		m_stencil = BGFX_STENCIL_NONE;
		m_render_state = BGFX_STATE_RGB_WRITE | BGFX_STATE_ALPHA_WRITE | BGFX_STATE_DEPTH_WRITE | BGFX_STATE_MSAA;
		return m_view_idx;
	}


	void copyRenderbuffer(const char* src_fb_name, int src_rb_idx, const char* dest_fb_name, int dest_rb_idx)
	{
		auto* src_fb = getFramebuffer(src_fb_name);
		auto* dest_fb = getFramebuffer(dest_fb_name);
		if (!src_fb || !dest_fb) return;

		auto src_rb = src_fb->getRenderbufferHandle(src_rb_idx);
		auto dest_rb = dest_fb->getRenderbufferHandle(dest_rb_idx);

		bgfx::blit(m_bgfx_view, dest_rb, 0, 0, src_rb);
	}


	void createCubeBuffers()
	{
		const Vec3 cube_vertices[] = {
			{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
			{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
			{1, -1, -1}, {1, -1, 1}, {1, 1, 1}, {1, 1, -1},
			{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1},
			{-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1},
			{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}
		};
		static const uint16 cube_indices[] = {
			0, 2, 1, 2, 0, 3,
			4, 5, 6, 6, 7, 4,
			8, 10, 9, 10, 8, 11,
			12, 13, 14, 14, 15, 12,
			16, 18, 17, 18, 16, 19,
			20, 21, 22, 22, 23, 20
		};
		auto* vertices_mem = bgfx::copy(cube_vertices, sizeof(cube_vertices));
		auto* indices_mem = bgfx::copy(cube_indices, sizeof(cube_indices));
		m_cube_vb = bgfx::createVertexBuffer(vertices_mem, m_deferred_point_light_vertex_decl);
		m_cube_ib = bgfx::createIndexBuffer(indices_mem);
	}


	void finishDeferredPointLightInstances(Material* material,
		const bgfx::InstanceDataBuffer* instance_buffer,
		int instance_count,
		bool is_intersecting)
	{
		bgfx::setInstanceDataBuffer(instance_buffer, instance_count);
		bgfx::setStencil(m_stencil, BGFX_STENCIL_NONE); 
		if (is_intersecting)
		{
			auto state = m_render_state | material->getRenderStates();
			bgfx::setState(((state & ~BGFX_STATE_CULL_MASK) & ~BGFX_STATE_DEPTH_TEST_MASK) | BGFX_STATE_CULL_CCW);
		}
		else
		{
			bgfx::setState(m_render_state | material->getRenderStates());
		}
		executeCommandBuffer(m_views[m_view_idx].command_buffer.buffer, material);
		bgfx::setVertexBuffer(m_cube_vb);
		bgfx::setIndexBuffer(m_cube_ib);
		++m_stats.draw_call_count;
		m_stats.instance_count += instance_count;
		m_stats.triangle_count +=	instance_count * 12;
		bgfx::submit(m_bgfx_view, material->getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void removeFramebuffer(const char* framebuffer_name)
	{
		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			auto* f = m_framebuffers[i];
			if (compareString(f->getName(), framebuffer_name) == 0)
			{
				LUMIX_DELETE(m_allocator, m_framebuffers[i]);
				if (m_first_postprocess_framebuffer > i)
				{
					--m_first_postprocess_framebuffer;
				}
				m_framebuffers.eraseFast(i);
				break;
			}
		}
	}


	void setMaterialDefine(int material_idx, const char* define, bool enabled)
	{
		auto define_idx = m_renderer.getShaderDefineIdx(define);
		m_materials[material_idx]->setDefine(define_idx, enabled);
	}


	bool postprocessCallback(const char* camera_slot)
	{
		auto scr_scene = static_cast<LuaScriptScene*>(m_scene->getUniverse().getScene(crc32("lua_script")));
		if (!scr_scene) return false;
		ComponentIndex camera = m_scene->getCameraInSlot(camera_slot);
		if (camera == INVALID_COMPONENT) return false;

		Entity camera_entity = m_scene->getCameraEntity(camera);
		ComponentIndex scr_cmp = scr_scene->getComponent(camera_entity);
		if (scr_cmp == INVALID_COMPONENT) return false;

		bool ret = false;
		for (int i = 0, c = scr_scene->getScriptCount(scr_cmp); i < c; ++i)
		{
			lua_State* L = scr_scene->getState(scr_cmp, i);
			if(!L) continue;

			int env = scr_scene->getEnvironment(scr_cmp, i);
			lua_rawgeti(L, LUA_REGISTRYINDEX, env);
			if(lua_getfield(L, -1, "_IS_POSTPROCESS_INITIALIZED") == LUA_TNIL)
			{
				if(auto* call = scr_scene->beginFunctionCall(scr_cmp, i, "initPostprocess"))
				{
					call->add(this);
					call->addEnvironment(m_lua_env);
					scr_scene->endFunctionCall(*call);
					lua_rawgeti(L, LUA_REGISTRYINDEX, env);
					lua_pushboolean(L, 1);
					lua_setfield(L, -2, "_IS_POSTPROCESS_INITIALIZED");
				}
			}
			lua_pop(L, 2);

			if (auto* call = scr_scene->beginFunctionCall(scr_cmp, i, "postprocess"))
			{
				ret = true;
				call->add(this);
				call->addEnvironment(m_lua_env);
				scr_scene->endFunctionCall(*call);
			}
		}
		return ret;
	}


	void renderLightVolumes(int material_index)
	{
		PROFILE_FUNCTION();
		if (m_applied_camera == INVALID_COMPONENT) return;
		auto* material = m_materials[material_index];
		if (!material->isReady()) return;

		m_tmp_local_lights.clear();
		m_scene->getPointLights(m_camera_frustum, m_tmp_local_lights);
		if (m_tmp_local_lights.empty()) return;

		PROFILE_INT("light count", m_tmp_local_lights.size());
		struct Data
		{
			Matrix mtx;
			Vec4 pos_radius;
			Vec4 color_attenuation;
			Vec4 dir_fov;
			Vec4 specular;
		};
		const bgfx::InstanceDataBuffer* instance_buffer[2] = {nullptr, nullptr};
		Data* instance_data[2] = { nullptr, nullptr };
		Universe& universe = m_scene->getUniverse();
		for(auto light_cmp : m_tmp_local_lights)
		{
			auto entity = m_scene->getPointLightEntity(light_cmp);
			float range = m_scene->getLightRange(light_cmp);
			Vec3 light_dir = universe.getRotation(entity) * Vec3(0, 0, -1);
			float attenuation = m_scene->getLightAttenuation(light_cmp);
			float fov = Math::degreesToRadians(m_scene->getLightFOV(light_cmp));
			float intensity = m_scene->getPointLightIntensity(light_cmp);
			intensity *= intensity;
			Vec3 color = m_scene->getPointLightColor(light_cmp) * intensity;

			Vec3 pos = universe.getPosition(entity);
			int buffer_idx = m_camera_frustum.intersectNearPlane(pos, range * Math::SQRT3) ? 0 : 1;
			if(!instance_buffer[buffer_idx])
			{
				instance_buffer[buffer_idx] = bgfx::allocInstanceDataBuffer(128, sizeof(Data));
				instance_data[buffer_idx] = (Data*)instance_buffer[buffer_idx]->data;
			}

			auto* id = instance_data[buffer_idx];
			id->mtx = universe.getPositionAndRotation(entity);
			id->mtx.multiply3x3(range);
			id->pos_radius.set(pos, range);
			id->color_attenuation.set(color, attenuation);
			id->dir_fov.set(light_dir, fov);
			float specular_intensity = m_scene->getPointLightSpecularIntensity(light_cmp);
			id->specular.set(m_scene->getPointLightSpecularColor(light_cmp) 
				* specular_intensity * specular_intensity, 1);
			++instance_data[buffer_idx];

			if(instance_data[buffer_idx] - (Data*)instance_buffer[buffer_idx]->data == 128)
			{
				finishDeferredPointLightInstances(material,
					instance_buffer[buffer_idx],
					128,
					buffer_idx == 0);
				instance_buffer[buffer_idx] = nullptr;
				instance_data[buffer_idx] = nullptr;
			}
		}

		for(int buffer_idx = 0; buffer_idx < 2; ++buffer_idx)
		{
			if(instance_data[buffer_idx])
			{
				finishDeferredPointLightInstances(material,
					instance_buffer[buffer_idx],
					int(instance_data[buffer_idx] - (Data*)instance_buffer[buffer_idx]->data),
					buffer_idx == 0);
			}
		}
	}


	void renderSpotLightShadowmap(ComponentIndex light)
	{
		newView("point_light");

		Entity light_entity = m_scene->getPointLightEntity(light);
		Matrix mtx = m_scene->getUniverse().getMatrix(light_entity);
		float fov = m_scene->getLightFOV(light);
		float range = m_scene->getLightRange(light);
		uint16 shadowmap_height = (uint16)m_current_framebuffer->getHeight();
		uint16 shadowmap_width = (uint16)m_current_framebuffer->getWidth();
		Vec3 pos = mtx.getTranslation();

		bgfx::setViewClear(m_bgfx_view, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
		bgfx::touch(m_bgfx_view);
		bgfx::setViewRect(m_bgfx_view, 0, 0, shadowmap_width, shadowmap_height);

		Matrix projection_matrix;
		projection_matrix.setPerspective(Math::degreesToRadians(fov), 1, 0.01f, range);
		Matrix view_matrix;
		view_matrix.lookAt(pos, pos - mtx.getZVector(), mtx.getYVector());
		bgfx::setViewTransform(m_bgfx_view, &view_matrix.m11, &projection_matrix.m11);

		PointLightShadowmap& s = m_point_light_shadowmaps.emplace();
		s.m_framebuffer = m_current_framebuffer;
		s.m_light = light;
		static const Matrix biasMatrix(
			0.5,  0.0, 0.0, 0.0,
			0.0, -0.5, 0.0, 0.0,
			0.0,  0.0, 0.5, 0.0,
			0.5,  0.5, 0.5, 1.0);
		s.m_matrices[0] = biasMatrix * (projection_matrix * view_matrix);

		renderPointLightInfluencedGeometry(light);
	}


	void renderOmniLightShadowmap(ComponentIndex light)
	{
		Entity light_entity = m_scene->getPointLightEntity(light);
		Vec3 light_pos = m_scene->getUniverse().getPosition(light_entity);
		float range = m_scene->getLightRange(light);
		uint16 shadowmap_height = (uint16)m_current_framebuffer->getHeight();
		uint16 shadowmap_width = (uint16)m_current_framebuffer->getWidth();

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

		PointLightShadowmap& shadowmap_info = m_point_light_shadowmaps.emplace();
		shadowmap_info.m_framebuffer = m_current_framebuffer;
		shadowmap_info.m_light = light;
		//setPointLightUniforms(light);

		for (int i = 0; i < 4; ++i)
		{
			newView("omnilight");

			bgfx::setViewClear(m_bgfx_view, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
			bgfx::touch(m_bgfx_view);
			uint16 view_x = uint16(shadowmap_width * viewports[i * 2]);
			uint16 view_y = uint16(shadowmap_height * viewports[i * 2 + 1]);
			bgfx::setViewRect(
				m_bgfx_view, view_x, view_y, shadowmap_width >> 1, shadowmap_height >> 1);

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

			bgfx::setViewTransform(m_bgfx_view, &view_matrix.m11, &projection_matrix.m11);

			static const Matrix biasMatrix(
				0.5, 0.0, 0.0, 0.0, 0.0, -0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, 1.0);
			shadowmap_info.m_matrices[i] = biasMatrix * (projection_matrix * view_matrix);

			m_tmp_meshes.clear();
			m_is_current_light_global = false;
			m_scene->getPointLightInfluencedGeometry(light, frustum, m_tmp_meshes);

			renderMeshes(m_tmp_meshes);
		}
	}


	void renderLocalLightShadowmaps(ComponentIndex camera,
		FrameBuffer** fbs,
		int framebuffers_count)
	{
		if (camera < 0) return;

		Universe& universe = m_scene->getUniverse();
		Entity camera_entity = m_scene->getCameraEntity(camera);
		Vec3 camera_pos = universe.getPosition(camera_entity);

		ComponentIndex lights[16];
		int light_count = m_scene->getClosestPointLights(camera_pos, lights, lengthOf(lights));

		int fb_index = 0;
		for (int i = 0; i < light_count; ++i)
		{
			if (!m_scene->getLightCastShadows(lights[i])) continue;
			if (fb_index == framebuffers_count) break;

			float fov = m_scene->getLightFOV(lights[i]);

			m_current_framebuffer = fbs[i];
			if (fov < 180)
			{
				renderSpotLightShadowmap(lights[i]);
			}
			else
			{
				renderOmniLightShadowmap(lights[i]);
			}
			++fb_index;
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


	void renderShadowmap(int split_index)
	{
		Universe& universe = m_scene->getUniverse();
		ComponentIndex light_cmp = m_scene->getActiveGlobalLight();
		if (light_cmp < 0 || m_applied_camera < 0) return;
		float camera_height = m_scene->getCameraScreenHeight(m_applied_camera);
		if (!camera_height) return;

		Matrix light_mtx = universe.getMatrix(m_scene->getGlobalLightEntity(light_cmp));
		m_global_light_shadowmap = m_current_framebuffer;
		float shadowmap_height = (float)m_current_framebuffer->getHeight();
		float shadowmap_width = (float)m_current_framebuffer->getWidth();
		float viewports[] = { 0, 0, 0.5f, 0, 0, 0.5f, 0.5f, 0.5f };
		float camera_fov = Math::degreesToRadians(m_scene->getCameraFOV(m_applied_camera));
		float camera_ratio = m_scene->getCameraScreenWidth(m_applied_camera) / camera_height;
		Vec4 cascades = m_scene->getShadowmapCascades(light_cmp);
		float split_distances[] = { 0.01f, cascades.x, cascades.y, cascades.z, cascades.w };
		m_is_rendering_in_shadowmap = true;
		bgfx::setViewClear(
			m_bgfx_view, BGFX_CLEAR_DEPTH | BGFX_CLEAR_COLOR, 0xffffffff, 1.0f, 0);
		bgfx::touch(m_bgfx_view);
		float* viewport = viewports + split_index * 2;
		bgfx::setViewRect(m_bgfx_view,
			(uint16)(1 + shadowmap_width * viewport[0]),
			(uint16)(1 + shadowmap_height * viewport[1]),
			(uint16)(0.5f * shadowmap_width - 2),
			(uint16)(0.5f * shadowmap_height - 2));

		Frustum frustum;
		Matrix camera_matrix = universe.getMatrix(m_scene->getCameraEntity(m_applied_camera));
		frustum.computePerspective(camera_matrix.getTranslation(),
			camera_matrix.getZVector(),
			camera_matrix.getYVector(),
			camera_fov,
			camera_ratio,
			split_distances[split_index],
			split_distances[split_index + 1]);

		Vec3 shadow_cam_pos = camera_matrix.getTranslation();
		float bb_size = frustum.radius;
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
		bgfx::setViewTransform(m_bgfx_view, &view_matrix.m11, &projection_matrix.m11);
		static const Matrix biasMatrix(
			0.5, 0.0, 0.0, 0.0, 0.0, -0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, 1.0);
		m_shadow_viewprojection[split_index] = biasMatrix * (projection_matrix * view_matrix);

		Frustum shadow_camera_frustum;
		shadow_camera_frustum.computeOrtho(shadow_cam_pos,
			-light_forward,
			light_mtx.getYVector(),
			bb_size,
			bb_size,
			SHADOW_CAM_NEAR,
			SHADOW_CAM_FAR);
		m_current_render_views = &m_view_idx;
		m_current_render_view_count = 1;
		renderAll(shadow_camera_frustum, false, camera_matrix.getTranslation());
		m_is_rendering_in_shadowmap = false;
	}


	void renderDebugShapes()
	{
		renderDebugTriangles();
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
				vertex[0].rgba = point.color;
				vertex[0].x = point.pos.x;
				vertex[0].y = point.pos.y;
				vertex[0].z = point.pos.z;
				vertex[0].u = vertex[0].v = 0;

				indices[0] = i;
				++vertex;
				++indices;
			}

			bgfx::setVertexBuffer(&tvb);
			bgfx::setIndexBuffer(&tib);
			bgfx::setStencil(m_stencil, BGFX_STENCIL_NONE);
			bgfx::setState(
				m_render_state | m_debug_line_material->getRenderStates() | BGFX_STATE_PT_POINTS);
			bgfx::submit(m_bgfx_view,
				m_debug_line_material->getShaderInstance().m_program_handles[m_pass_idx]);
		}
	}


	void renderDebugLines()
	{
		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_line_material->isReady()) return;

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;

		static const int BATCH_SIZE = 1024 * 16;

		for (int j = 0; j < lines.size(); j += BATCH_SIZE)
		{
			int count = Math::minimum(BATCH_SIZE, lines.size() - j);
			if (bgfx::allocTransientBuffers(&tvb, m_base_vertex_decl, count * 2, &tib, count * 2))
			{
				BaseVertex* vertex = (BaseVertex*)tvb.data;
				uint16* indices = (uint16*)tib.data;
				for (int i = 0; i < count; ++i)
				{
					const DebugLine& line = lines[j + i];
					vertex[0].rgba = line.color;
					vertex[0].x = line.from.x;
					vertex[0].y = line.from.y;
					vertex[0].z = line.from.z;
					vertex[0].u = vertex[0].v = 0;

					vertex[1].rgba = line.color;
					vertex[1].x = line.to.x;
					vertex[1].y = line.to.y;
					vertex[1].z = line.to.z;
					vertex[1].u = vertex[0].v = 0;

					indices[0] = i * 2;
					indices[1] = i * 2 + 1;
					vertex += 2;
					indices += 2;
				}

				bgfx::setVertexBuffer(&tvb);
				bgfx::setIndexBuffer(&tib);
				bgfx::setStencil(m_stencil, BGFX_STENCIL_NONE);
				bgfx::setState(m_render_state | m_debug_line_material->getRenderStates() |
							   BGFX_STATE_PT_LINES);
				bgfx::submit(m_bgfx_view,
					m_debug_line_material->getShaderInstance().m_program_handles[m_pass_idx]);
			}
		}
	}


	void renderDebugTriangles()
	{
		const auto& tris = m_scene->getDebugTriangles();
		if(tris.empty() || !m_debug_line_material->isReady()) return;

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;

		static const int BATCH_SIZE = 1024 * 16;

		for(int j = 0; j < tris.size(); j += BATCH_SIZE)
		{
			int count = Math::minimum(BATCH_SIZE, tris.size() - j);
			if(bgfx::allocTransientBuffers(&tvb, m_base_vertex_decl, count * 3, &tib, count * 3))
			{
				BaseVertex* vertex = (BaseVertex*)tvb.data;
				uint16* indices = (uint16*)tib.data;
				for(int i = 0; i < count; ++i)
				{
					const DebugTriangle& tri = tris[j + i];
					vertex[0].rgba = tri.color;
					vertex[0].x = tri.p0.x;
					vertex[0].y = tri.p0.y;
					vertex[0].z = tri.p0.z;
					vertex[0].u = vertex[0].v = 0;

					vertex[1].rgba = tri.color;
					vertex[1].x = tri.p1.x;
					vertex[1].y = tri.p1.y;
					vertex[1].z = tri.p1.z;
					vertex[1].u = vertex[0].v = 0;

					vertex[2].rgba = tri.color;
					vertex[2].x = tri.p2.x;
					vertex[2].y = tri.p2.y;
					vertex[2].z = tri.p2.z;
					vertex[2].u = vertex[0].v = 0;

					indices[0] = i * 3;
					indices[1] = i * 3 + 1;
					indices[2] = i * 3 + 2;
					vertex += 3;
					indices += 3;
				}

				bgfx::setVertexBuffer(&tvb);
				bgfx::setIndexBuffer(&tib);
				bgfx::setStencil(m_stencil, BGFX_STENCIL_NONE);
				bgfx::setState(m_render_state | m_debug_line_material->getRenderStates());
				bgfx::submit(m_bgfx_view,
					m_debug_line_material->getShaderInstance().m_program_handles[m_pass_idx]);
			}
		}
	}

	int getPassIdx() const override
	{
		return m_pass_idx;
	}


	void setPointLightUniforms(ComponentIndex light_cmp)
	{
		if (light_cmp < 0) return;

		Universe& universe = m_scene->getUniverse();
		Entity light_entity = m_scene->getPointLightEntity(light_cmp);
		Vec3 light_pos = universe.getPosition(light_entity);
		Vec3 light_dir = universe.getRotation(light_entity) * Vec3(0, 0, -1);
		float fov = Math::degreesToRadians(m_scene->getLightFOV(light_cmp));
		float intensity = m_scene->getPointLightIntensity(light_cmp);
		intensity *= intensity;
		Vec3 color = m_scene->getPointLightColor(light_cmp) * intensity;
		float range = m_scene->getLightRange(light_cmp);
		float attenuation = m_scene->getLightAttenuation(light_cmp);
		Vec4 light_pos_radius(light_pos, range);
		Vec4 light_color_attenuation(color, attenuation);
		Vec4 light_dir_fov(light_dir, fov);
		float specular_intensity = m_scene->getPointLightSpecularIntensity(light_cmp);
		Vec4 light_specular(m_scene->getPointLightSpecularColor(light_cmp) * specular_intensity *
								specular_intensity, 1);

		m_views[m_view_idx].command_buffer.setUniform(m_light_pos_radius_uniform, light_pos_radius);
		m_views[m_view_idx].command_buffer.setUniform(m_light_color_attenuation_uniform, light_color_attenuation);
		m_views[m_view_idx].command_buffer.setUniform(m_light_dir_fov_uniform, light_dir_fov);
		m_views[m_view_idx].command_buffer.setUniform(m_light_specular_uniform, light_specular);

		FrameBuffer* shadowmap = nullptr;
		if (m_scene->getLightCastShadows(light_cmp))
		{
			for (auto& info : m_point_light_shadowmaps)
			{
				if (info.m_light == light_cmp)
				{
					shadowmap = info.m_framebuffer;
					m_views[m_view_idx].command_buffer.setUniform(m_shadowmap_matrices_uniform,
						&info.m_matrices[0],
						m_scene->getLightFOV(light_cmp) > 180 ? 4 : 1);
					break;
				}
			}
		}
		if (shadowmap)
		{
			m_views[m_view_idx].command_buffer.setLocalShadowmap(shadowmap->getRenderbufferHandle(0));
		}
		else
		{
			m_views[m_view_idx].command_buffer.setLocalShadowmap(BGFX_INVALID_HANDLE);
		}
		m_views[m_view_idx].command_buffer.end();
	}


	void setStencilRef(uint32 ref)
	{
		m_stencil |= BGFX_STENCIL_FUNC_REF(ref);
		m_views[m_view_idx].stencil = m_stencil;
	}


	void setStencilRMask(uint32 rmask)
	{
		m_stencil |= BGFX_STENCIL_FUNC_RMASK(rmask);
		m_views[m_view_idx].stencil = m_stencil;
	}


	void setStencil(uint32 flags)
	{
		m_stencil |= flags;
		m_views[m_view_idx].stencil = m_stencil;
	}


	void setActiveGlobalLightUniforms()
	{
		auto current_light = m_scene->getActiveGlobalLight();
		if (current_light == INVALID_COMPONENT) return;

		Universe& universe = m_scene->getUniverse();
		Entity light_entity = m_scene->getGlobalLightEntity(current_light);
		Vec3 light_dir = universe.getRotation(light_entity) * Vec3(0, 0, 1);
		Vec3 diffuse_color = m_scene->getGlobalLightColor(current_light) *
							 m_scene->getGlobalLightIntensity(current_light);
		Vec3 ambient_color = m_scene->getLightAmbientColor(current_light) *
							 m_scene->getLightAmbientIntensity(current_light);
		Vec3 fog_color = m_scene->getFogColor(current_light);
		float fog_density = m_scene->getFogDensity(current_light);
		Vec3 specular = m_scene->getGlobalLightSpecular(current_light);
		float specular_intensity = m_scene->getGlobalLightSpecularIntensity(current_light);
		specular *= specular_intensity * specular_intensity;

		m_views[m_view_idx].command_buffer.setUniform(m_light_color_attenuation_uniform, Vec4(diffuse_color, 1));
		m_views[m_view_idx].command_buffer.setUniform(m_ambient_color_uniform, Vec4(ambient_color, 1));
		m_views[m_view_idx].command_buffer.setUniform(m_light_dir_fov_uniform, Vec4(light_dir, 0));

		fog_density *= fog_density * fog_density;
		m_views[m_view_idx].command_buffer.setUniform(m_fog_color_density_uniform, Vec4(fog_color, fog_density));
		m_views[m_view_idx].command_buffer.setUniform(m_light_specular_uniform, Vec4(specular, 0));
		m_views[m_view_idx].command_buffer.setUniform(m_fog_params_uniform,
			Vec4(m_scene->getFogBottom(current_light),
								 m_scene->getFogHeight(current_light),
								 0,
								 0));
		if (m_global_light_shadowmap && !m_is_rendering_in_shadowmap)
		{
			m_views[m_view_idx].command_buffer.setUniform(m_shadowmap_matrices_uniform, m_shadow_viewprojection, 4);
			m_views[m_view_idx].command_buffer.setGlobalShadowmap();
		}
		m_views[m_view_idx].command_buffer.end();
	}

	void disableBlending()
	{
		m_render_state &= ~BGFX_STATE_BLEND_MASK;
		m_views[m_view_idx].render_state = m_render_state;
	}

	void enableDepthWrite()
	{
		m_render_state |= BGFX_STATE_DEPTH_WRITE;
		m_views[m_view_idx].render_state = m_render_state;
	}
	void disableDepthWrite()
	{
		m_render_state &= ~BGFX_STATE_DEPTH_WRITE;
		m_views[m_view_idx].render_state = m_render_state;
	}

	void enableAlphaWrite()
	{
		m_render_state |= BGFX_STATE_ALPHA_WRITE;
		m_views[m_view_idx].render_state = m_render_state;
	}
	void disableAlphaWrite()
	{
		m_render_state &= ~BGFX_STATE_ALPHA_WRITE;
		m_views[m_view_idx].render_state = m_render_state;
	}

	void enableRGBWrite()
	{
		m_render_state |= BGFX_STATE_RGB_WRITE;
		m_views[m_view_idx].render_state = m_render_state;
	}
	void disableRGBWrite()
	{
		m_render_state &= ~BGFX_STATE_RGB_WRITE;
		m_views[m_view_idx].render_state = m_render_state;
	}


	void renderPointLightInfluencedGeometry(ComponentIndex light)
	{
		PROFILE_FUNCTION();

		m_tmp_meshes.clear();

		m_scene->getPointLightInfluencedGeometry(light, m_tmp_meshes);

		renderMeshes(m_tmp_meshes);
	}


	void renderPointLightInfluencedGeometry(const Frustum& frustum)
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
			m_is_current_light_global = false;
			setPointLightUniforms(light);
			m_scene->getPointLightInfluencedGeometry(light, frustum, m_tmp_meshes);

			m_scene->getTerrainInfos(m_tmp_terrains,
				m_scene->getUniverse().getPosition(m_scene->getCameraEntity(m_applied_camera)),
				m_renderer.getFrameAllocator());

			m_scene->getGrassInfos(frustum, m_tmp_grasses, m_applied_camera);
			renderMeshes(m_tmp_meshes);
			renderTerrains(m_tmp_terrains);
			renderGrasses(m_tmp_grasses);
		}
	}


	float getCPUTime() const override
	{
		auto* stats = bgfx::getStats();
		return float(double(stats->cpuTimeEnd - stats->cpuTimeBegin) / (double)stats->cpuTimerFreq);
	}


	float getGPUTime() const override
	{
		auto* stats = bgfx::getStats();
		return float(double(stats->gpuTimeEnd - stats->gpuTimeBegin) / (double)stats->gpuTimerFreq);
	}


	void drawQuad(float x, float y, float w, float h, int material_index)
	{
		Material* material = m_materials[material_index];
		if (!material->isReady() || !bgfx::checkAvailTransientVertexBuffer(3, m_base_vertex_decl))
		{
			bgfx::touch(m_bgfx_view);
			return;
		}

		Matrix projection_mtx;
		projection_mtx.setOrtho(-1, 1, 1, -1, 0, 30);

		bgfx::setViewTransform(m_bgfx_view, &Matrix::IDENTITY.m11, &projection_mtx.m11);
		if (m_current_framebuffer)
		{
			bgfx::setViewRect(m_bgfx_view,
				m_view_x,
				m_view_y,
				(uint16)m_current_framebuffer->getWidth(),
				(uint16)m_current_framebuffer->getHeight());
		}
		else
		{
			bgfx::setViewRect(m_bgfx_view, m_view_x, m_view_y, (uint16)m_width, (uint16)m_height);
		}

		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 6, m_base_vertex_decl);
		BaseVertex* vertex = (BaseVertex*)vb.data;
		float x2 = x + w;
		float y2 = y + h;

		vertex[0].x = x;
		vertex[0].y = y;
		vertex[0].z = 0;
		vertex[0].rgba = 0xffffffff;
		vertex[0].u = 0;
		vertex[0].v = 0;

		vertex[1].x = x2;
		vertex[1].y = y;
		vertex[1].z = 0;
		vertex[1].rgba = 0xffffffff;
		vertex[1].u = 1;
		vertex[1].v = 0;

		vertex[2].x = x2;
		vertex[2].y = y2;
		vertex[2].z = 0;
		vertex[2].rgba = 0xffffffff;
		vertex[2].u = 1;
		vertex[2].v = 1;

		vertex[3].x = x;
		vertex[3].y = y;
		vertex[3].z = 0;
		vertex[3].rgba = 0xffffffff;
		vertex[3].u = 0;
		vertex[3].v = 0;

		vertex[4].x = x2;
		vertex[4].y = y2;
		vertex[4].z = 0;
		vertex[4].rgba = 0xffffffff;
		vertex[4].u = 1;
		vertex[4].v = 1;

		vertex[5].x = x;
		vertex[5].y = y2;
		vertex[5].z = 0;
		vertex[5].rgba = 0xffffffff;
		vertex[5].u = 0;
		vertex[5].v = 1;

		executeCommandBuffer(material->getCommandBuffer(), material);
		executeCommandBuffer(m_views[m_view_idx].command_buffer.buffer, material);

		if (m_applied_camera >= 0)
		{
			Matrix projection_matrix;
			Universe& universe = m_scene->getUniverse();
			float fov = m_scene->getCameraFOV(m_applied_camera);
			float near_plane = m_scene->getCameraNearPlane(m_applied_camera);
			float far_plane = m_scene->getCameraFarPlane(m_applied_camera);
			float ratio = float(m_width) / m_height;
			Entity camera_entity = m_scene->getCameraEntity(m_applied_camera);
			Matrix camera_matrix = universe.getPositionAndRotation(camera_entity);
			Matrix view_matrix = camera_matrix;
			view_matrix.fastInverse();
			projection_matrix.setPerspective(
				Math::degreesToRadians(fov), ratio, near_plane, far_plane);
			Matrix inv_projection = projection_matrix;
			inv_projection.inverse();
			Matrix inv_view_proj = projection_matrix * view_matrix;
			inv_view_proj.inverse();

			bgfx::setUniform(m_cam_inv_proj_uniform, &inv_projection.m11);
			bgfx::setUniform(m_cam_inv_viewproj_uniform, &inv_view_proj.m11);
			bgfx::setUniform(m_cam_view_uniform, &view_matrix.m11);
			bgfx::setUniform(m_cam_proj_uniform, &projection_matrix.m11);
			bgfx::setUniform(m_cam_inv_view_uniform, &camera_matrix.m11);
			auto cam_params = Vec4(near_plane, far_plane, fov, ratio);
			bgfx::setUniform(m_cam_params, &cam_params);
		}

		bgfx::setStencil(m_stencil, BGFX_STENCIL_NONE);
		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::setVertexBuffer(&vb);
		++m_stats.draw_call_count;
		++m_stats.instance_count;
		m_stats.triangle_count += 2;
		bgfx::submit(m_bgfx_view, material->getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void renderAll(const Frustum& frustum, bool render_grass, const Vec3& lod_ref_point)
	{
		PROFILE_FUNCTION();

		if (m_applied_camera < 0) return;

		m_tmp_grasses.clear();
		m_tmp_terrains.clear();

		auto& meshes = m_scene->getRenderableInfos(frustum, lod_ref_point);
		Entity camera_entity = m_scene->getCameraEntity(m_applied_camera);
		Vec3 camera_pos = m_scene->getUniverse().getPosition(camera_entity);
		LIFOAllocator& frame_allocator = m_renderer.getFrameAllocator();
		m_scene->getTerrainInfos(m_tmp_terrains, camera_pos, frame_allocator);

		m_is_current_light_global = true;

		if (render_grass)
		{
			m_scene->getGrassInfos(frustum, m_tmp_grasses, m_applied_camera);
			renderGrasses(m_tmp_grasses);
		}
		renderTerrains(m_tmp_terrains);
		renderMeshes(meshes);
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
			int instance_idx = mesh.instance_idx;
			if (instance_idx == -1)
			{
				instance_idx = m_instance_data_idx;
				m_instance_data_idx = (m_instance_data_idx + 1) % lengthOf(m_instances_data);
				if (m_instances_data[instance_idx].buffer)
				{
					finishInstances(instance_idx);
				}
				mesh.instance_idx = instance_idx;
			}
			InstanceData& data = m_instances_data[instance_idx];
			if (!data.buffer)
			{
				if (!bgfx::checkAvailInstanceDataBuffer(InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix)))
				{
					return;
				}
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


	void renderSkinnedMesh(const Renderable& renderable, const RenderableMesh& info)
	{
		const Mesh& mesh = *info.mesh;
		Material* material = mesh.material;
		auto& shader_instance = mesh.material->getShaderInstance();

		Matrix bone_mtx[128];

		const Pose& pose = *renderable.pose;
		const Model& model = *renderable.model;
		Vec3* poss = pose.positions;
		Quat* rots = pose.rotations;

		ASSERT(pose.count <= lengthOf(bone_mtx));
		for (int bone_index = 0, bone_count = pose.count; bone_index < bone_count; ++bone_index)
		{
			auto& bone = model.getBone(bone_index);
			rots[bone_index].toMatrix(bone_mtx[bone_index]);
			bone_mtx[bone_index].translate(poss[bone_index]);
			bone_mtx[bone_index] = bone_mtx[bone_index] * bone.inv_bind_matrix;
		}

		int stride = model.getVertexDecl().getStride();
		for (int i = 0; i < m_current_render_view_count; ++i)
		{
			auto& view = m_views[m_current_render_views[i]];
			if (!bgfx::isValid(shader_instance.m_program_handles[view.pass_idx])) continue;

			for (int j = 0, c = material->getLayerCount(view.pass_idx); j < c; ++j)
			{
				auto layer = Vec4((j + 1) / (float)c, 0, 0, 0);
				bgfx::setUniform(m_layer_uniform, &layer);
				bgfx::setUniform(m_bone_matrices_uniform, bone_mtx, pose.count);
				executeCommandBuffer(material->getCommandBuffer(), material);
				executeCommandBuffer(view.command_buffer.buffer, material);

				bgfx::setTransform(&renderable.matrix);
				bgfx::setVertexBuffer(renderable.model->getVerticesHandle(),
					mesh.attribute_array_offset / stride,
					mesh.attribute_array_size / stride);
				bgfx::setIndexBuffer(renderable.model->getIndicesHandle(), mesh.indices_offset, mesh.indices_count);
				bgfx::setStencil(view.stencil, BGFX_STENCIL_NONE);
				bgfx::setState(view.render_state | material->getRenderStates());
				++m_stats.draw_call_count;
				++m_stats.instance_count;
				m_stats.triangle_count += mesh.indices_count / 3;
				bgfx::submit(view.bgfx_id, shader_instance.m_program_handles[view.pass_idx]);
			}
		}
	}


	void setScissor(int x, int y, int width, int height) override
	{
		bgfx::setScissor(x, y, width, height);
	}


	void setTexture(int slot, bgfx::TextureHandle texture, bgfx::UniformHandle uniform) override
	{
		bgfx::setTexture(slot, uniform, texture);
	}


	void render(const bgfx::TransientVertexBuffer& vertex_buffer,
		const bgfx::TransientIndexBuffer& index_buffer,
		const Matrix& mtx,
		int first_index,
		int num_indices,
		uint64 render_states,
		bgfx::ProgramHandle program_handle) override
	{
		bgfx::setStencil(m_stencil, BGFX_STENCIL_NONE);
		bgfx::setState(m_render_state | render_states);
		bgfx::setTransform(&mtx.m11);
		bgfx::setVertexBuffer(&vertex_buffer);
		bgfx::setIndexBuffer(&index_buffer, first_index, num_indices);
		++m_stats.draw_call_count;
		++m_stats.instance_count;
		m_stats.triangle_count += num_indices / 3;
		bgfx::submit(m_bgfx_view, program_handle);
	}


	void renderRigidMesh(const Renderable& renderable, const RenderableMesh& info)
	{
		int instance_idx = info.mesh->instance_idx;
		if (instance_idx == -1)
		{
			instance_idx = m_instance_data_idx;
			m_instance_data_idx = (m_instance_data_idx + 1) % lengthOf(m_instances_data);
			if (m_instances_data[instance_idx].buffer)
			{
				finishInstances(instance_idx);
			}
			InstanceData& data = m_instances_data[instance_idx];
			if (!bgfx::checkAvailInstanceDataBuffer(InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix)))
			{
				return;
			}
			data.buffer =
				bgfx::allocInstanceDataBuffer(InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix));
			data.instance_count = 0;
			data.mesh = info.mesh;
			data.model = renderable.model;
			info.mesh->instance_idx = instance_idx;
		}
		InstanceData& data = m_instances_data[instance_idx];
		Matrix* mtcs = (Matrix*)data.buffer->data;
		mtcs[data.instance_count] = renderable.matrix;
		++data.instance_count;

		if (data.instance_count == InstanceData::MAX_INSTANCE_COUNT)
		{
			finishInstances(instance_idx);
		}
	}


	void executeCommandBuffer(const uint8* data, Material* material) const
	{
		const uint8* ip = data;
		for (;;)
		{
			switch ((BufferCommands)*ip)
			{
				case BufferCommands::END:
					return;
				case BufferCommands::SET_TEXTURE:
				{
					auto cmd = (SetTextureCommand*)ip;
					ip += sizeof(*cmd);
					bgfx::setTexture(cmd->stage, cmd->uniform, cmd->texture);
					break;
				}
				case BufferCommands::SET_UNIFORM_TIME:
				{
					auto cmd = (SetUniformTimeCommand*)ip;
					ip += sizeof(*cmd);
					auto uniform_time = Vec4(m_scene->getTime(), 0, 0, 0);
					bgfx::setUniform(cmd->uniform, &uniform_time);
					break;
				}
				case BufferCommands::SET_UNIFORM_VEC4:
				{
					auto cmd = (SetUniformVec4Command*)ip;
					ip += sizeof(*cmd);
					bgfx::setUniform(cmd->uniform, &cmd->value);
					break;
				}
				case BufferCommands::SET_UNIFORM_ARRAY:
				{
					auto cmd = (SetUniformArrayCommand*)ip;
					ip += sizeof(*cmd);
					bgfx::setUniform(cmd->uniform, ip, cmd->count);
					ip += cmd->size;
					break;
				}
				case BufferCommands::SET_GLOBAL_SHADOWMAP:
				{
					ip += 1;
					auto handle = m_global_light_shadowmap->getRenderbufferHandle(0);
					bgfx::setTexture(15 - m_global_textures_count,
						m_tex_shadowmap_uniform,
						handle);
					break;
				}
				case BufferCommands::SET_LOCAL_SHADOWMAP:
				{
					auto cmd = (SetLocalShadowmapCommand*)ip;
					ip += sizeof(*cmd);
					material->setDefine(m_has_shadowmap_define_idx, bgfx::isValid(cmd->texture));
					bgfx::setTexture(15 - m_global_textures_count,
						m_tex_shadowmap_uniform,
						cmd->texture);
					break;
				}
				default:
					ASSERT(false);
					break;
			}
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

		auto& view = m_views[m_current_render_views[0]];
		executeCommandBuffer(material->getCommandBuffer(), material);
		executeCommandBuffer(view.command_buffer.buffer, material);

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
		int mesh_part_indices_count = mesh.indices_count / 4;
		bgfx::setIndexBuffer(info.m_terrain->getIndicesHandle(),
			info.m_index * mesh_part_indices_count,
			mesh_part_indices_count);
		bgfx::setStencil(view.stencil, BGFX_STENCIL_NONE);
		bgfx::setState(view.render_state | mesh.material->getRenderStates());
		bgfx::setInstanceDataBuffer(instance_buffer, m_terrain_instances[index].m_count);
		auto shader_instance = material->getShaderInstance().m_program_handles[view.pass_idx];
		++m_stats.draw_call_count;
		m_stats.instance_count += m_terrain_instances[index].m_count;
		m_stats.triangle_count += m_terrain_instances[index].m_count * mesh_part_indices_count;
		bgfx::submit(view.bgfx_id, shader_instance);

		m_terrain_instances[index].m_count = 0;
	}


	void renderGrass(const GrassInfo& grass)
	{
		if (!bgfx::checkAvailInstanceDataBuffer(grass.matrix_count, sizeof(Matrix))) return;

		const bgfx::InstanceDataBuffer* idb = bgfx::allocInstanceDataBuffer(grass.matrix_count, sizeof(Matrix));
		copyMemory(idb->data, &grass.matrices[0], grass.matrix_count * sizeof(Matrix));
		const Mesh& mesh = grass.model->getMesh(0);
		Material* material = mesh.material;
		int stride = grass.model->getVertexDecl().getStride();

		auto& view = m_views[m_current_render_views[0]];
		executeCommandBuffer(material->getCommandBuffer(), material);
		executeCommandBuffer(view.command_buffer.buffer, material);
		auto max_grass_distance = Vec4(grass.type_distance, 0, 0, 0);
		bgfx::setUniform(m_grass_max_dist_uniform, &max_grass_distance);

		bgfx::setVertexBuffer(
			grass.model->getVerticesHandle(), mesh.attribute_array_offset / stride, mesh.attribute_array_size / stride);
		bgfx::setIndexBuffer(grass.model->getIndicesHandle(), mesh.indices_offset, mesh.indices_count);
		bgfx::setStencil(view.stencil, BGFX_STENCIL_NONE);
		bgfx::setState(view.render_state | material->getRenderStates());
		bgfx::setInstanceDataBuffer(idb, grass.matrix_count);
		++m_stats.draw_call_count;
		m_stats.instance_count += grass.matrix_count;
		m_stats.triangle_count += grass.matrix_count * mesh.indices_count;
		bgfx::submit(view.bgfx_id, material->getShaderInstance().m_program_handles[view.pass_idx]);
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
		if(meshes.empty()) return;

		Renderable* renderables = m_scene->getRenderables();
		PROFILE_INT("mesh count", meshes.size());
		for(auto& mesh : meshes)
		{
			Renderable& renderable = renderables[mesh.renderable];
			if(renderable.pose && renderable.pose->count > 0)
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


	void renderMeshes(const Array<Array<RenderableMesh>>& meshes)
	{
		PROFILE_FUNCTION();
		int mesh_count = 0;
		for (auto& submeshes : meshes)
		{
			if(submeshes.empty()) continue;
			Renderable* renderables = m_scene->getRenderables();
			mesh_count += submeshes.size();
			for (auto& mesh : submeshes)
			{
				Renderable& renderable = renderables[mesh.renderable];
				if (renderable.pose && renderable.pose->count > 0)
				{
					renderSkinnedMesh(renderable, mesh);
				}
				else
				{
					renderRigidMesh(renderable, mesh);
				}
			}
		}
		finishInstances();
		PROFILE_INT("mesh count", mesh_count);
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
		for (auto& i : m_framebuffers)
		{
			auto size_ratio = i->getSizeRatio();
			if (size_ratio.x > 0 || size_ratio.y > 0)
			{
				i->resize(int(w * size_ratio.x), int(h * size_ratio.y));
			}
		}
		m_width = w;
		m_height = h;
	}


	void render() override
	{
		PROFILE_FUNCTION();

		if (!isReady()) return;
		if (!m_scene) return;

		m_stats = {};
		m_render_state = BGFX_STATE_RGB_WRITE | BGFX_STATE_ALPHA_WRITE | BGFX_STATE_DEPTH_WRITE | BGFX_STATE_MSAA;
		m_applied_camera = INVALID_COMPONENT;
		m_global_light_shadowmap = nullptr;
		m_stencil = BGFX_STENCIL_NONE;
		m_render_state |= m_is_wireframe ? BGFX_STATE_PT_LINESTRIP : 0;
		m_view_idx = -1;
		m_bgfx_view = m_renderer.getViewCounter();
		m_pass_idx = -1;
		m_current_framebuffer = m_default_framebuffer;
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

		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		if (lua_getfield(m_lua_state, -1, "render") == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_lua_state, this);
			if (lua_pcall(m_lua_state, 1, 0, 0) != LUA_OK)
			{
				g_log_warning.log("Renderer") << lua_tostring(m_lua_state, -1);
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
		errors = errors || lua_pcall(m_lua_state, 0, 0, 0) != LUA_OK;

		if (errors)
		{
			g_log_error.log("Renderer") << lua_tostring(m_lua_state, -1);
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


	int createVec4ArrayUniform(const char* name, int num)
	{
		bgfx::UniformHandle handle = bgfx::createUniform(name, bgfx::UniformType::Vec4, num);
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
		m_views[m_view_idx].render_state = m_render_state;
	}


	void clear(uint32 flags, uint32 color)
	{
		bgfx::setViewClear(m_bgfx_view, (uint16)flags, color, 1.0f, 0);
		bgfx::touch(m_bgfx_view);
	}


	void renderPointLightLitGeometry()
	{
		renderPointLightInfluencedGeometry(m_camera_frustum);
	}


	bool isReady() const override { return m_is_ready; }


	void setScene(RenderScene* scene) override
	{
		for (int i = m_first_postprocess_framebuffer; i < m_framebuffers.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_framebuffers[i]);
		}
		m_framebuffers.resize(m_first_postprocess_framebuffer);

		m_scene = scene;
		if (m_lua_state && m_scene) callInitScene();
	}


	void setWireframe(bool wireframe) override { m_is_wireframe = wireframe; }


	void callInitScene()
	{
		lua_rawgeti(m_lua_state, LUA_REGISTRYINDEX, m_lua_env);
		if(lua_getfield(m_lua_state, -1, "initScene") == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_lua_state, this);
			if(lua_pcall(m_lua_state, 1, 0, 0) != LUA_OK)
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
		float x, y, z;
		uint32 rgba;
		float u;
		float v;
	};


	bgfx::VertexDecl m_deferred_point_light_vertex_decl;
	bgfx::VertexDecl m_base_vertex_decl;
	TerrainInstance m_terrain_instances[4];
	uint32 m_debug_flags;
	uint8 m_bgfx_view;
	int m_view_idx;
	View m_views[32];
	int m_pass_idx;
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
	bgfx::VertexBufferHandle m_cube_vb;
	bgfx::IndexBufferHandle m_cube_ib;
	bool m_is_current_light_global;
	bool m_is_wireframe;
	bool m_is_rendering_in_shadowmap;
	bool m_is_ready;
	Frustum m_camera_frustum;

	int* m_current_render_views;
	int m_current_render_view_count;
	Matrix m_shadow_viewprojection[4];
	int m_view_x;
	int m_view_y;
	int m_width;
	int m_height;
	uint32 m_stencil;
	bgfx::VertexBufferHandle m_particle_vertex_buffer;
	bgfx::IndexBufferHandle m_particle_index_buffer;
	Array<CustomCommandHandler> m_custom_commands_handlers;
	Array<RenderableMesh> m_tmp_meshes;
	Array<const TerrainInfo*> m_tmp_terrains;
	Array<GrassInfo> m_tmp_grasses;
	Array<ComponentIndex> m_tmp_local_lights;

	bgfx::UniformHandle m_bone_matrices_uniform;
	bgfx::UniformHandle m_layer_uniform;
	bgfx::UniformHandle m_terrain_scale_uniform;
	bgfx::UniformHandle m_rel_camera_pos_uniform;
	bgfx::UniformHandle m_terrain_params_uniform;
	bgfx::UniformHandle m_fog_color_density_uniform;
	bgfx::UniformHandle m_fog_params_uniform;
	bgfx::UniformHandle m_light_pos_radius_uniform;
	bgfx::UniformHandle m_light_color_attenuation_uniform;
	bgfx::UniformHandle m_ambient_color_uniform;
	bgfx::UniformHandle m_light_dir_fov_uniform;
	bgfx::UniformHandle m_shadowmap_matrices_uniform;
	bgfx::UniformHandle m_light_specular_uniform;
	bgfx::UniformHandle m_terrain_matrix_uniform;
	bgfx::UniformHandle m_tex_shadowmap_uniform;
	bgfx::UniformHandle m_cam_view_uniform;
	bgfx::UniformHandle m_cam_proj_uniform;
	bgfx::UniformHandle m_cam_params;
	bgfx::UniformHandle m_cam_inv_view_uniform;
	bgfx::UniformHandle m_cam_inv_proj_uniform;
	bgfx::UniformHandle m_cam_inv_viewproj_uniform;
	bgfx::UniformHandle m_texture_size_uniform;
	bgfx::UniformHandle m_grass_max_dist_uniform;
	int m_global_textures_count;

	Material* m_debug_line_material;
	int m_has_shadowmap_define_idx;
	int m_first_postprocess_framebuffer;
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


int addFramebuffer(lua_State* L)
{
	auto* pipeline = LuaWrapper::checkArg<PipelineImpl*>(L, 1);
	const char* name = LuaWrapper::checkArg<const char*>(L, 2);
	LuaWrapper::checkTableArg(L, 3);
	FrameBuffer::Declaration decl;
	copyString(decl.m_name, name);

	if(lua_getfield(L, 3, "width") == LUA_TNUMBER)
	{
		decl.m_width = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	decl.m_size_ratio = Vec2(-1, -1);
	if(lua_getfield(L, 3, "size_ratio") == LUA_TTABLE)
	{
		decl.m_size_ratio = LuaWrapper::toType<Vec2>(L, -1);
	}
	lua_pop(L, 1);
	if(lua_getfield(L, 3, "screen_size") == LUA_TBOOLEAN)
	{
		bool is_screen_size = lua_toboolean(L, -1) != 0;
		decl.m_size_ratio = is_screen_size ? Vec2(1, 1) : Vec2(-1, -1);
	}
	lua_pop(L, 1);
	if(lua_getfield(L, 3, "height") == LUA_TNUMBER)
	{
		decl.m_height = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	if(lua_getfield(L, 3, "renderbuffers") == LUA_TTABLE)
	{
		PipelineImpl::parseRenderbuffers(L, decl);
	}
	lua_pop(L, 1);
	if ((decl.m_size_ratio.x > 0 || decl.m_size_ratio.y > 0) && pipeline->m_height > 0)
	{
		decl.m_width = int(pipeline->m_width * decl.m_size_ratio.x);
		decl.m_height = int(pipeline->m_height * decl.m_size_ratio.y);
	}
	auto* fb = LUMIX_NEW(pipeline->m_allocator, FrameBuffer)(decl);
	pipeline->m_framebuffers.push(fb);
	if(compareString(decl.m_name, "default") == 0) pipeline->m_default_framebuffer = fb;

	return 0;
}


int renderModels(lua_State* L)
{
	auto* pipeline = LuaWrapper::checkArg<PipelineImpl*>(L, 1);
	LuaWrapper::checkTableArg(L, 2);
	int len = (int)lua_rawlen(L, 2);
	int views[16] = {};
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, 2, 1 + i))
		{
			views[i] = (int)lua_tointeger(L, -1);
		}
		lua_pop(L, 1);
	}

	pipeline->m_current_render_views = views;
	pipeline->m_current_render_view_count = len;
	pipeline->renderAll(pipeline->m_camera_frustum, true, pipeline->m_camera_frustum.position);
	pipeline->m_current_render_views = &pipeline->m_view_idx;
	pipeline->m_current_render_view_count = 1;
	return 0;
}


void logError(const char* message)
{
	g_log_error.log("Renderer") << message;
}


int setUniform(lua_State* L)
{
	auto* pipeline = LuaWrapper::checkArg<PipelineImpl*>(L, 1);
	int uniform_idx = LuaWrapper::checkArg<int>(L, 2);
	LuaWrapper::checkTableArg(L, 3);

	Vec4 tmp[64];
	int len = Math::minimum((int)lua_rawlen(L, 3), lengthOf(tmp));
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, 3, 1 + i) == LUA_TTABLE)
		{
			if (lua_rawgeti(L, -1, 1) == LUA_TNUMBER) tmp[i].x = (float)lua_tonumber(L, -1);
			if (lua_rawgeti(L, -2, 2) == LUA_TNUMBER) tmp[i].y = (float)lua_tonumber(L, -1);
			if (lua_rawgeti(L, -3, 3) == LUA_TNUMBER) tmp[i].z = (float)lua_tonumber(L, -1);
			if (lua_rawgeti(L, -4, 4) == LUA_TNUMBER) tmp[i].w = (float)lua_tonumber(L, -1);
			lua_pop(L, 4);
		}
		lua_pop(L, 1);
	}

	if (uniform_idx >= pipeline->m_uniforms.size()) luaL_argerror(L, 2, "unknown uniform");

	pipeline->m_views[pipeline->m_view_idx].command_buffer.beginAppend();
	pipeline->m_views[pipeline->m_view_idx].command_buffer.setUniform(pipeline->m_uniforms[uniform_idx], tmp, len);
	pipeline->m_views[pipeline->m_view_idx].command_buffer.end();
	return 0;
}


int renderLocalLightsShadowmaps(lua_State* L)
{
	auto* pipeline = LuaWrapper::checkArg<PipelineImpl*>(L, 1);
	const char* camera_slot = LuaWrapper::checkArg<const char*>(L, 2);

	FrameBuffer* fbs[16];
	int len = Math::minimum((int)lua_rawlen(L, 3), lengthOf(fbs));
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
	ComponentIndex camera = scene->getCameraInSlot(camera_slot);
	pipeline->renderLocalLightShadowmaps(camera, fbs, len);

	return 0;
}


void print(int x, int y, const char* text)
{
	bgfx::dbgTextPrintf(x, y, 0x4f, text);
}


} // namespace LuaAPI


void Pipeline::registerLuaAPI(lua_State* L)
{
	auto registerCFunction = [L](const char* name, lua_CFunction function)
	{
		lua_pushcfunction(L, function);
		lua_setglobal(L, name);
	};

	auto registerConst = [L](const char* name, uint32 value)
	{
		lua_pushinteger(L, value);
		lua_setglobal(L, name);
	};

	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<PipelineImpl, decltype(&PipelineImpl::name), &PipelineImpl::name>; \
			registerCFunction(#name, f); \
		} while(false) \

	REGISTER_FUNCTION(drawQuad);
	REGISTER_FUNCTION(setPass);
	REGISTER_FUNCTION(newView);
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
	REGISTER_FUNCTION(createVec4ArrayUniform);
	REGISTER_FUNCTION(hasScene);
	REGISTER_FUNCTION(cameraExists);
	REGISTER_FUNCTION(enableBlending);
	REGISTER_FUNCTION(clear);
	REGISTER_FUNCTION(renderPointLightLitGeometry);
	REGISTER_FUNCTION(renderShadowmap);
	REGISTER_FUNCTION(copyRenderbuffer);
	REGISTER_FUNCTION(setActiveGlobalLightUniforms);
	REGISTER_FUNCTION(setStencil);
	REGISTER_FUNCTION(setStencilRMask);
	REGISTER_FUNCTION(setStencilRef);
	REGISTER_FUNCTION(renderLightVolumes);
	REGISTER_FUNCTION(postprocessCallback);
	REGISTER_FUNCTION(removeFramebuffer);
	REGISTER_FUNCTION(setMaterialDefine);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(name) \
		registerCFunction(#name, LuaWrapper::wrap<decltype(&LuaAPI::name), LuaAPI::name>)

	REGISTER_FUNCTION(print);
	REGISTER_FUNCTION(logError);
	REGISTER_FUNCTION(renderLocalLightsShadowmaps);
	REGISTER_FUNCTION(setUniform);
	REGISTER_FUNCTION(addFramebuffer);
	REGISTER_FUNCTION(renderModels);

	#undef REGISTER_FUNCTION

	#define REGISTER_STENCIL_CONST(a) \
		registerConst("STENCIL_" #a, BGFX_STENCIL_##a)

	REGISTER_STENCIL_CONST(TEST_LESS);
	REGISTER_STENCIL_CONST(TEST_LEQUAL);
	REGISTER_STENCIL_CONST(TEST_EQUAL);
	REGISTER_STENCIL_CONST(TEST_GEQUAL);
	REGISTER_STENCIL_CONST(TEST_GREATER);
	REGISTER_STENCIL_CONST(TEST_NOTEQUAL);
	REGISTER_STENCIL_CONST(TEST_NEVER);
	REGISTER_STENCIL_CONST(TEST_ALWAYS);
	REGISTER_STENCIL_CONST(TEST_SHIFT);
	REGISTER_STENCIL_CONST(TEST_MASK);

	REGISTER_STENCIL_CONST(OP_FAIL_S_ZERO);
	REGISTER_STENCIL_CONST(OP_FAIL_S_KEEP);
	REGISTER_STENCIL_CONST(OP_FAIL_S_REPLACE);
	REGISTER_STENCIL_CONST(OP_FAIL_S_INCR);
	REGISTER_STENCIL_CONST(OP_FAIL_S_INCRSAT);
	REGISTER_STENCIL_CONST(OP_FAIL_S_DECR);
	REGISTER_STENCIL_CONST(OP_FAIL_S_DECRSAT);
	REGISTER_STENCIL_CONST(OP_FAIL_S_INVERT);
	REGISTER_STENCIL_CONST(OP_FAIL_S_SHIFT);
	REGISTER_STENCIL_CONST(OP_FAIL_S_MASK);

	REGISTER_STENCIL_CONST(OP_FAIL_Z_ZERO);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_KEEP);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_REPLACE);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_INCR);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_INCRSAT);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_DECR);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_DECRSAT);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_INVERT);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_SHIFT);
	REGISTER_STENCIL_CONST(OP_FAIL_Z_MASK);

	REGISTER_STENCIL_CONST(OP_PASS_Z_ZERO);
	REGISTER_STENCIL_CONST(OP_PASS_Z_KEEP);
	REGISTER_STENCIL_CONST(OP_PASS_Z_REPLACE);
	REGISTER_STENCIL_CONST(OP_PASS_Z_INCR);
	REGISTER_STENCIL_CONST(OP_PASS_Z_INCRSAT);
	REGISTER_STENCIL_CONST(OP_PASS_Z_DECR);
	REGISTER_STENCIL_CONST(OP_PASS_Z_DECRSAT);
	REGISTER_STENCIL_CONST(OP_PASS_Z_INVERT);
	REGISTER_STENCIL_CONST(OP_PASS_Z_SHIFT);
	REGISTER_STENCIL_CONST(OP_PASS_Z_MASK);

	registerConst("CLEAR_DEPTH", BGFX_CLEAR_DEPTH);
	registerConst("CLEAR_COLOR", BGFX_CLEAR_COLOR);
	registerConst("CLEAR_STENCIL", BGFX_CLEAR_STENCIL);
	registerConst("CLEAR_ALL", BGFX_CLEAR_STENCIL | BGFX_CLEAR_DEPTH | BGFX_CLEAR_COLOR);

	#undef REGISTER_STENCIL_CONST
}


} // ~namespace Lumix
