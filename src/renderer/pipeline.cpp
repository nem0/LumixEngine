#include "pipeline.h"

#include "renderer/pipeline.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/fs/ifile.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/static_array.h"
#include "core/string.h"
#include "engine.h"
#include "plugin_manager.h"
#include "renderer/frame_buffer.h"
#include "renderer/geometry.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/model_instance.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "renderer/transient_geometry.h"
#include "universe/universe.h"
#include <bgfx.h>


namespace Lumix
{


struct PipelineImpl;
struct PipelineInstanceImpl;


static const uint32_t LIGHT_DIR_HASH = crc32("light_dir");
static const uint32_t TERRAIN_SCALE_HASH = crc32("terrain_scale");
static const uint32_t BONE_MATRICES_HASH = crc32("bone_matrices");
static const uint32_t CAMERA_POS_HASH = crc32("camera_pos");
static const uint32_t MAP_SIZE_HASH = crc32("map_size");
static const uint32_t POINT_LIGHT_HASH = crc32("point_light");
static const uint32_t BRUSH_SIZE_HASH = crc32("brush_size");
static const uint32_t BRUSH_POSITION_HASH = crc32("brush_position");
static const char* TEX_COLOR_UNIFORM = "u_texColor";
static const float SHADOW_CAM_NEAR = 0.5f;
static const float SHADOW_CAM_FAR = 10000.0f;

class InstanceData
{
public:
	static const int MAX_INSTANCE_COUNT = 32;

public:
	const bgfx::InstanceDataBuffer* m_buffer;
	int m_instance_count;
	RenderableMesh m_mesh;
};

class BaseVertex
{
public:
	float m_x, m_y, m_z;
	uint32_t m_rgba;
	float m_u;
	float m_v;

	static bgfx::VertexDecl s_vertex_decl;
};


bgfx::VertexDecl BaseVertex::s_vertex_decl;


struct PipelineImpl : public Pipeline
{
	PipelineImpl(const Path& path,
				 ResourceManager& resource_manager,
				 IAllocator& allocator)
		: Pipeline(path, resource_manager, allocator)
		, m_allocator(allocator)
		, m_framebuffers(allocator)
		, m_lua_state(nullptr)
	{
	}


	Renderer& getRenderer()
	{
		return static_cast<PipelineManager*>(
				   m_resource_manager.get(ResourceManager::PIPELINE))
			->getRenderer();
	}


	virtual ~PipelineImpl() override
	{
		if (m_lua_state)
		{
			lua_close(m_lua_state);
		}
		ASSERT(isEmpty());
	}


	virtual void doUnload(void) override
	{
		if (m_lua_state)
		{
			lua_close(m_lua_state);
			m_lua_state = nullptr;
		}
		onEmpty();
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


	void parseFramebuffers(lua_State* L)
	{
		if (lua_getglobal(L, "framebuffers") == LUA_TTABLE)
		{
			int len = (int)lua_rawlen(L, -1);
			m_framebuffers.resize(len);
			for (int i = 0; i < len; ++i)
			{
				if (lua_rawgeti(L, -1, 1 + i) == LUA_TTABLE)
				{
					FrameBuffer::Declaration& decl = m_framebuffers[i];
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


	virtual void
	loaded(FS::IFile& file, bool success, FS::FileSystem& fs) override
	{
		if (m_lua_state)
		{
			lua_close(m_lua_state);
			m_lua_state = nullptr;
		}
		if (success)
		{
			m_lua_state = luaL_newstate();
			luaL_openlibs(m_lua_state);
			bool errors = luaL_loadbuffer(m_lua_state,
										  (const char*)file.getBuffer(),
										  file.size(),
										  "") != LUA_OK;
			errors =
				errors || lua_pcall(m_lua_state, 0, LUA_MULTRET, 0) != LUA_OK;
			if (errors)
			{
				g_log_error.log("lua") << getPath().c_str() << ": "
									   << lua_tostring(m_lua_state, -1);
				lua_pop(m_lua_state, 1);
				onFailure();
			}
			else
			{
				parseFramebuffers(m_lua_state);
				registerCFunctions();
				decrementDepCount();
			}
		}
		else
		{
			onFailure();
		}
	}

	lua_State* m_lua_state;
	IAllocator& m_allocator;
	Array<FrameBuffer::Declaration> m_framebuffers;
};


struct PipelineInstanceImpl : public PipelineInstance
{
	PipelineInstanceImpl(Pipeline& pipeline, IAllocator& allocator)
		: m_source(static_cast<PipelineImpl&>(pipeline))
		, m_custom_commands_handlers(allocator)
		, m_allocator(allocator)
		, m_tmp_terrains(allocator)
		, m_tmp_grasses(allocator)
		, m_tmp_meshes(allocator)
		, m_framebuffers(allocator)
		, m_uniforms(allocator)
		, m_global_textures(allocator)
		, m_renderer(static_cast<PipelineImpl&>(pipeline).getRenderer())
		, m_default_framebuffer(nullptr)
		, m_debug_line_material(nullptr)
		, m_debug_flags(BGFX_DEBUG_TEXT)
		, m_point_light_shadowmaps(allocator)
		, m_materials(allocator)
	{
		m_is_wireframe = false;
		m_view_x = m_view_y = 0;
		m_has_shadowmap_define_idx = m_renderer.getShaderDefineIdx("HAS_SHADOWMAP");

		m_cam_view_uniform =
			bgfx::createUniform("u_camView", bgfx::UniformType::Mat4);
		m_cam_inv_proj_uniform =
			bgfx::createUniform("u_camInvProj", bgfx::UniformType::Mat4);
		m_tex_shadowmap_uniform =
			bgfx::createUniform("u_texShadowmap", bgfx::UniformType::Int1);
		m_attenuation_params_uniform =
			bgfx::createUniform("u_attenuationParams", bgfx::UniformType::Vec4);
		m_terrain_scale_uniform =
			bgfx::createUniform("u_terrainScale", bgfx::UniformType::Vec4);
		m_rel_camera_pos_uniform =
			bgfx::createUniform("u_relCamPos", bgfx::UniformType::Vec4);
		m_terrain_params_uniform =
			bgfx::createUniform("u_terrainParams", bgfx::UniformType::Vec4);
		m_fog_color_density_uniform =
			bgfx::createUniform("u_fogColorDensity", bgfx::UniformType::Vec4);
		m_light_pos_radius_uniform =
			bgfx::createUniform("u_lightPosRadius", bgfx::UniformType::Vec4);
		m_light_color_uniform =
			bgfx::createUniform("u_lightRgbInnerR", bgfx::UniformType::Vec4);
		m_light_dir_fov_uniform =
			bgfx::createUniform("u_lightDirFov", bgfx::UniformType::Vec4);
		m_light_specular_uniform =
			bgfx::createUniform("u_lightSpecular", bgfx::UniformType::Mat4, 64);
		m_ambient_color_uniform =
			bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
		m_shadowmap_matrices_uniform = bgfx::createUniform(
			"u_shadowmapMatrices", bgfx::UniformType::Mat4, 4);
		m_bone_matrices_uniform =
			bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, 64);
		m_specular_shininess_uniform = bgfx::createUniform(
			"u_materialSpecularShininess", bgfx::UniformType::Vec4);
		m_terrain_matrix_uniform =
			bgfx::createUniform("u_terrainMatrix", bgfx::UniformType::Mat4);

		ResourceManagerBase* material_manager =
			pipeline.getResourceManager().get(ResourceManager::MATERIAL);
		m_debug_line_material = static_cast<Material*>(material_manager->load(
			Lumix::Path("models/editor/debug_line.mat")));

		m_scene = nullptr;
		m_width = m_height = -1;
		pipeline.onLoaded<PipelineInstanceImpl,
						  &PipelineInstanceImpl::sourceLoaded>(this);
	}


	~PipelineInstanceImpl()
	{
		ResourceManagerBase* material_manager =
			m_source.getResourceManager().get(ResourceManager::MATERIAL);
		for (auto* material : m_materials)
		{
			material_manager->unload(*material);
		}
		material_manager->unload(*m_debug_line_material);
		bgfx::destroyUniform(m_tex_shadowmap_uniform);
		bgfx::destroyUniform(m_attenuation_params_uniform);
		bgfx::destroyUniform(m_terrain_matrix_uniform);
		bgfx::destroyUniform(m_specular_shininess_uniform);
		bgfx::destroyUniform(m_bone_matrices_uniform);
		bgfx::destroyUniform(m_terrain_scale_uniform);
		bgfx::destroyUniform(m_rel_camera_pos_uniform);
		bgfx::destroyUniform(m_terrain_params_uniform);
		bgfx::destroyUniform(m_fog_color_density_uniform);
		bgfx::destroyUniform(m_light_pos_radius_uniform);
		bgfx::destroyUniform(m_light_color_uniform);
		bgfx::destroyUniform(m_light_dir_fov_uniform);
		bgfx::destroyUniform(m_ambient_color_uniform);
		bgfx::destroyUniform(m_shadowmap_matrices_uniform);
		bgfx::destroyUniform(m_light_specular_uniform);
		bgfx::destroyUniform(m_cam_inv_proj_uniform);
		bgfx::destroyUniform(m_cam_view_uniform);

		for (int i = 0; i < m_uniforms.size(); ++i)
		{
			bgfx::destroyUniform(m_uniforms[i]);
		}

		m_source.getObserverCb()
			.unbind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(
				this);
		m_source.getResourceManager()
			.get(ResourceManager::PIPELINE)
			->unload(m_source);
		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			m_allocator.deleteObject(m_framebuffers[i]);
			if (m_framebuffers[i] == m_default_framebuffer) m_default_framebuffer = nullptr;
		}
		m_allocator.deleteObject(m_default_framebuffer);
	}


	virtual void setViewProjection(const Matrix& mtx, int width, int height) override
	{
		bgfx::setViewRect(m_view_idx, 0, 0, width, height);
		bgfx::setViewTransform(m_view_idx, nullptr, &mtx.m11);
	}


	void finishInstances(int idx)
	{
		InstanceData& data = m_instances_data[idx];
		if (!data.m_buffer)	return;

		const RenderableMesh& info = data.m_mesh;
		const Mesh& mesh = *info.m_mesh;
		const Model& model = *info.m_model;
		const Geometry& geometry = model.getGeometry();
		Material* material = mesh.getMaterial();
		const uint16_t stride = mesh.getVertexDefinition().getStride();

		setMaterial(material);
		bgfx::setVertexBuffer(geometry.getAttributesArrayID(),
							  mesh.getAttributeArrayOffset() / stride,
							  mesh.getAttributeArraySize() / stride);
		bgfx::setIndexBuffer(geometry.getIndicesArrayID(),
							 mesh.getIndicesOffset(),
							 mesh.getIndexCount());
		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::setInstanceDataBuffer(data.m_buffer, data.m_instance_count);
		ShaderInstance& shader_instance =
			info.m_mesh->getMaterial()->getShaderInstance();
		bgfx::submit(m_view_idx, shader_instance.m_program_handles[m_pass_idx]);
		
		data.m_buffer = nullptr;
		data.m_instance_count = 0;
		data.m_mesh.m_mesh->setInstanceIdx(-1);
	}


	void applyCamera(const char* slot)
	{
		ComponentIndex cmp = getScene()->getCameraInSlot(slot);
		if (cmp < 0) return;

		getScene()->setCameraSize(cmp, m_width, m_height);
		m_applied_camera = cmp;
		m_camera_frustum = getScene()->getCameraFrustum(cmp);

		Matrix projection_matrix;
		float fov = getScene()->getCameraFOV(cmp);
		float near_plane = getScene()->getCameraNearPlane(cmp);
		float far_plane = getScene()->getCameraFarPlane(cmp);
		float ratio = float(m_width) / m_height;
		projection_matrix.setPerspective(
			Math::degreesToRadians(fov), ratio, near_plane, far_plane);

		Universe& universe = getScene()->getUniverse();
		Matrix mtx = universe.getMatrix(getScene()->getCameraEntity(cmp));
		mtx.fastInverse();
		bgfx::setViewTransform(m_view_idx, &mtx.m11, &projection_matrix.m11);
		
		bgfx::setViewRect(
			m_view_idx, m_view_x, m_view_y, (uint16_t)m_width, (uint16_t)m_height);
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
		m_global_textures.clear();

		m_pass_idx = m_renderer.getPassIdx(name);
		bool found = false;
		for (int i = 0; i < m_view2pass_map.size(); ++i)
		{
			if (m_view2pass_map[i] == m_pass_idx)
			{
				m_view_idx = i;
				found = true;
				break;
			}
		}

		if (!found)
		{
			beginNewView(m_current_framebuffer);
		}
	}


	CustomCommandHandler& addCustomCommandHandler(const char* name) override
	{
		return m_custom_commands_handlers[crc32(name)];
	}


	virtual FrameBuffer* getFramebuffer(const char* framebuffer_name) override
	{
		for (int i = 0, c = m_framebuffers.size(); i < c; ++i)
		{
			if (strcmp(m_framebuffers[i]->getName(), framebuffer_name) == 0)
			{
				return m_framebuffers[i];
			}
		}
		return nullptr;
	}


	void setCurrentFramebuffer(const char* framebuffer_name)
	{
		if (strcmp(framebuffer_name, "default") == 0)
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


	void bindFramebufferTexture(const char* framebuffer_name,
								int renderbuffer_idx,
								int uniform_idx)
	{
		FrameBuffer* fb = getFramebuffer(framebuffer_name);
		if (!fb) return;

		GlobalTexture& t = m_global_textures.pushEmpty();
		t.m_texture = fb->getRenderbufferHandle(renderbuffer_idx);
		t.m_uniform = m_uniforms[uniform_idx];
	}


	virtual int getWidth() override { return m_width; }


	virtual int getHeight() override { return m_height; }


	void sourceLoaded(Resource::State old_state, Resource::State new_state)
	{
		if (old_state != Resource::State::READY &&
			new_state == Resource::State::READY)
		{
			for (int i = 0; i < m_framebuffers.size(); ++i)
			{
				m_allocator.deleteObject(m_framebuffers[i]);
			}
			m_framebuffers.clear();

			m_framebuffers.reserve(m_source.m_framebuffers.size());
			for (int i = 0; i < m_source.m_framebuffers.size(); ++i)
			{
				FrameBuffer::Declaration& decl = m_source.m_framebuffers[i];
				auto* fb = m_allocator.newObject<FrameBuffer>(decl);
				m_framebuffers.push(fb);
				if (strcmp(decl.m_name, "default") == 0) m_default_framebuffer = fb;
			}

			if (lua_getglobal(m_source.m_lua_state, "init") == LUA_TFUNCTION)
			{
				lua_pushlightuserdata(m_source.m_lua_state, this);
				if (lua_pcall(m_source.m_lua_state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("lua")
						<< lua_tostring(m_source.m_lua_state, -1);
					lua_pop(m_source.m_lua_state, 1);
				}
			}
			else
			{
				lua_pop(m_source.m_lua_state, 1);
			}
		}
	}


	void executeCustomCommand(uint32_t name)
	{
		CustomCommandHandler handler;
		if (m_custom_commands_handlers.find(name, handler))
		{
			handler.invoke();
		}
		finishInstances();
	}


	void renderPointLightShadowmaps(ComponentIndex camera, ComponentIndex light)
	{
		Frustum light_frustum;
		int64_t mask = 0;
		mask = ~mask;
		renderPointLightInfluencedGeometry(light_frustum, mask);
	}


	void beginNewView(FrameBuffer* framebuffer)
	{
		m_renderer.viewCounterAdd();
		m_view_idx = m_renderer.getViewCounter();
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
	}

	void renderSpotLightShadowmap(FrameBuffer* fb,
								  ComponentIndex light,
								  int64_t layer_mask)
	{
		ASSERT(fb);
		beginNewView(fb);

		Entity light_entity = m_scene->getPointLightEntity(light);
		Matrix mtx = m_scene->getUniverse().getMatrix(light_entity);
		float fov = m_scene->getLightFOV(light);
		float range = m_scene->getLightRange(light);
		uint16_t shadowmap_height = (uint16_t)m_current_framebuffer->getHeight();
		uint16_t shadowmap_width = (uint16_t)m_current_framebuffer->getWidth();
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

		PointLightShadowmap& s = m_point_light_shadowmaps.pushEmpty();
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
								  int64_t layer_mask)
	{
		Entity light_entity = m_scene->getPointLightEntity(light);
		Vec3 light_pos = m_scene->getUniverse().getPosition(light_entity);
		float range = m_scene->getLightRange(light);
		uint16_t shadowmap_height = (uint16_t)fb->getHeight();
		uint16_t shadowmap_width = (uint16_t)fb->getWidth();

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
			m_point_light_shadowmaps.pushEmpty();
		shadowmap_info.m_framebuffer = fb;
		shadowmap_info.m_light = light;

		for (int i = 0; i < 4; ++i)
		{
			ASSERT(fb);
			beginNewView(fb);

			bgfx::setViewClear(m_view_idx, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
			bgfx::touch(m_view_idx);
			uint16_t view_x = uint16_t(shadowmap_width * viewports[i * 2]);
			uint16_t view_y = uint16_t(shadowmap_height * viewports[i * 2 + 1]);
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

			renderModels(light, frustum, layer_mask);
		}
	}


	void renderModels(ComponentIndex light,
					  const Frustum& frustum,
					  int64_t layer_mask)
	{
		PROFILE_FUNCTION();

		m_tmp_meshes.clear();
		m_current_light = light;
		m_is_current_light_global = false;
		m_scene->getPointLightInfluencedGeometry(
			light, frustum, m_tmp_meshes, layer_mask);

		int global_textures_count = m_global_textures.size();

		renderMeshes(m_tmp_meshes);
		m_global_textures.resize(global_textures_count);
		m_current_light = -1;
	}


	void renderLocalLightShadowmaps(ComponentIndex camera,
									FrameBuffer** fbs,
									int framebuffers_count,
									int64_t layer_mask)
	{
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


	void renderShadowmap(ComponentIndex camera, int64_t layer_mask)
	{
		Universe& universe = m_scene->getUniverse();
		ComponentIndex light_cmp = m_scene->getActiveGlobalLight();
		if (light_cmp < 0 || camera < 0)
		{
			return;
		}
		Matrix light_mtx =
			universe.getMatrix(m_scene->getGlobalLightEntity(light_cmp));

		float shadowmap_height = (float)m_current_framebuffer->getHeight();
		float shadowmap_width = (float)m_current_framebuffer->getWidth();
		float viewports[] = {0, 0, 0.5f, 0, 0, 0.5f, 0.5f, 0.5f};

		float camera_fov =
			Math::degreesToRadians(m_scene->getCameraFOV(camera));
		float camera_ratio = m_scene->getCameraWidth(camera) /
							 m_scene->getCameraHeight(camera);
		Vec4 cascades = m_scene->getShadowmapCascades(light_cmp);
		float split_distances[] = {
			0.01f, cascades.x, cascades.y, cascades.z, cascades.w};
		for (int split_index = 0; split_index < 4; ++split_index)
		{
			if (split_index > 0)
			{
				beginNewView(m_current_framebuffer);
			}

			bgfx::setViewClear(m_view_idx,
							   BGFX_CLEAR_DEPTH | BGFX_CLEAR_COLOR,
							   0xffffffff,
							   1.0f,
							   0);
			bgfx::touch(m_view_idx);
			float* viewport = viewports + split_index * 2;
			bgfx::setViewRect(m_view_idx,
							  (uint16_t)(1 + shadowmap_width * viewport[0]),
							  (uint16_t)(1 + shadowmap_height * viewport[1]),
							  (uint16_t)(0.5f * shadowmap_width - 2),
							  (uint16_t)(0.5f * shadowmap_height - 2));

			Frustum frustum;
			Matrix camera_matrix =
				universe.getMatrix(m_scene->getCameraEntity(camera));
			frustum.computePerspective(camera_matrix.getTranslation(),
									   camera_matrix.getZVector(),
									   camera_matrix.getYVector(),
									   camera_fov,
									   camera_ratio,
									   split_distances[split_index],
									   split_distances[split_index + 1]);
			(&m_shadowmap_splits.x)[split_index] =
				split_distances[split_index + 1];

			Vec3 shadow_cam_pos = camera_matrix.getTranslation();
			float bb_size = frustum.getRadius();
			Matrix projection_matrix;
			projection_matrix.setOrtho(bb_size,
									   -bb_size,
									   -bb_size,
									   bb_size,
									   SHADOW_CAM_NEAR,
									   SHADOW_CAM_FAR);
			Vec3 light_forward = light_mtx.getZVector();
			shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
			Matrix view_matrix;
			view_matrix.lookAt(shadow_cam_pos,
							   shadow_cam_pos + light_forward,
							   light_mtx.getYVector());
			bgfx::setViewTransform(
				m_view_idx, &view_matrix.m11, &projection_matrix.m11);
			static const Matrix biasMatrix(0.5, 0.0, 0.0, 0.0,
										   0.0, -0.5, 0.0, 0.0,
										   0.0, 0.0, 0.5, 0.0,
										   0.5, 0.5, 0.5, 1.0);
			m_shadow_viewprojection[split_index] =
				biasMatrix * (projection_matrix * view_matrix);

			Frustum shadow_camera_frustum;
			shadow_camera_frustum.computeOrtho(shadow_cam_pos,
											   -light_forward,
											   light_mtx.getYVector(),
											   bb_size * 2,
											   bb_size * 2,
											   SHADOW_CAM_NEAR,
											   SHADOW_CAM_FAR);
			renderAll(shadow_camera_frustum, layer_mask, true);
		}
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
		if (bgfx::allocTransientBuffers(&tvb,
			BaseVertex::s_vertex_decl,
			points.size(),
			&tib,
			points.size()))
		{
			BaseVertex* vertex = (BaseVertex*)tvb.data;
			uint16_t* indices = (uint16_t*)tib.data;
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
			bgfx::setState(m_render_state |
				m_debug_line_material->getRenderStates() |
				BGFX_STATE_PT_POINTS);
			bgfx::submit(m_view_idx,
				m_debug_line_material->getShaderInstance()
				.m_program_handles[m_pass_idx]);
		}
	}


	void renderDebugLines()
	{
		const Array<DebugLine>& lines = m_scene->getDebugLines();
		if (lines.empty() || !m_debug_line_material->isReady())
		{
			return;
		}
		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		if (bgfx::allocTransientBuffers(&tvb,
										BaseVertex::s_vertex_decl,
										lines.size() * 2,
										&tib,
										lines.size() * 2))
		{
			BaseVertex* vertex = (BaseVertex*)tvb.data;
			uint16_t* indices = (uint16_t*)tib.data;
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
			bgfx::setState(m_render_state |
						   m_debug_line_material->getRenderStates() |
						   BGFX_STATE_PT_LINES);
			bgfx::submit(m_view_idx,
						 m_debug_line_material->getShaderInstance()
							 .m_program_handles[m_pass_idx]);
		}
	}


	void setPointLightUniforms(Material* material, ComponentIndex light_cmp)
	{
		if (light_cmp < 0)
		{
			return;
		}

		Universe& universe = m_scene->getUniverse();
		Entity light_entity = m_scene->getPointLightEntity(light_cmp);
		Vec3 light_pos = universe.getPosition(light_entity);
		Vec3 light_dir = universe.getRotation(light_entity) * Vec3(0, 0, 1);
		float fov = Math::degreesToRadians(m_scene->getLightFOV(light_cmp));
		Vec3 color = m_scene->getPointLightColor(light_cmp) *
					 m_scene->getPointLightIntensity(light_cmp);
		Vec4 attenuation_params(m_scene->getLightRange(light_cmp),
								m_scene->getLightAttenuation(light_cmp),
								0,
								1);
		Vec4 light_pos_radius(light_pos, m_scene->getLightRange(light_cmp));
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


	void setPointLightShadowmapUniforms(Material* material,
										ComponentIndex light)
	{
		for (auto& info : m_point_light_shadowmaps)
		{
			if (info.m_light == light)
			{
				material->setUserDefine(m_has_shadowmap_define_idx);

				bgfx::setUniform(m_shadowmap_matrices_uniform,
								 &info.m_matrices[0].m11,
								 m_scene->getLightFOV(light) > 180 ? 4 : 1);

				GlobalTexture& t = m_global_textures.pushEmpty();
				t.m_texture = info.m_framebuffer->getRenderbufferHandle(0);
				t.m_uniform = m_tex_shadowmap_uniform;
				return;
			}
		}
		material->unsetUserDefine(m_has_shadowmap_define_idx);
	}


	void setDirectionalLightUniforms(ComponentIndex light_cmp) const
	{
		if (light_cmp < 0)
		{
			return;
		}

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

		bgfx::setUniform(m_light_color_uniform, &diffuse_light_color);
		bgfx::setUniform(m_ambient_color_uniform, &ambient_light_color);
		bgfx::setUniform(m_light_dir_fov_uniform, &light_dir_fov);
		bgfx::setUniform(m_fog_color_density_uniform, &fog_color_density);
		bgfx::setUniform(
			m_shadowmap_matrices_uniform, &m_shadow_viewprojection, 4);
	}


	void enableBlending() { m_render_state |= BGFX_STATE_BLEND_ADD; }
	void disableBlending() { m_render_state &= ~BGFX_STATE_BLEND_MASK; }

	void enableDepthWrite() { m_render_state |= BGFX_STATE_DEPTH_WRITE; }
	void disableDepthWrite() { m_render_state &= ~BGFX_STATE_DEPTH_WRITE; }

	void enableAlphaWrite() { m_render_state |= BGFX_STATE_ALPHA_WRITE; }
	void disableAlphaWrite() { m_render_state &= ~BGFX_STATE_ALPHA_WRITE; }

	void enableRGBWrite() { m_render_state |= BGFX_STATE_RGB_WRITE; }
	void disableRGBWrite() { m_render_state &= ~BGFX_STATE_RGB_WRITE; }

	void renderPointLightInfluencedGeometry(ComponentIndex light,
											int64_t layer_mask)
	{
		PROFILE_FUNCTION();

		m_tmp_meshes.clear();

		m_scene->getPointLightInfluencedGeometry(
			light, m_tmp_meshes, layer_mask);

		int global_textures_count = m_global_textures.size();
		renderMeshes(m_tmp_meshes);
		m_global_textures.resize(global_textures_count);
	}


	void renderPointLightInfluencedGeometry(const Frustum& frustum,
											int64_t layer_mask)
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
			m_scene->getPointLightInfluencedGeometry(
				light, frustum, m_tmp_meshes, layer_mask);

			m_scene->getTerrainInfos(
				m_tmp_terrains,
				layer_mask,
				m_scene->getUniverse().getPosition(
					m_scene->getCameraEntity(m_applied_camera)),
				m_renderer.getFrameAllocator());

			m_scene->getGrassInfos(
				frustum, m_tmp_grasses, layer_mask, m_applied_camera);
			int global_textures_count = m_global_textures.size();

			renderMeshes(m_tmp_meshes);
			renderTerrains(m_tmp_terrains);
			renderGrasses(m_tmp_grasses);
			m_global_textures.resize(global_textures_count);
		}
		m_current_light = -1;
	}


	void drawQuad(float x, float y, float w, float h, int material_index)
	{
		Material* material = m_materials[material_index];
		if (!material->isReady() ||
			!bgfx::checkAvailTransientVertexBuffer(3,
												   BaseVertex::s_vertex_decl))
			return;

		Matrix projection_mtx;
		projection_mtx.setOrtho(-1, 1, 1, -1, 0, 30);

		bgfx::setViewTransform(
			m_view_idx, &Matrix::IDENTITY.m11, &projection_mtx.m11);
		bgfx::setViewRect(
			m_view_idx, m_view_x, m_view_y, (uint16_t)m_width, (uint16_t)m_height);

		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 6, BaseVertex::s_vertex_decl);
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
			default:
				ASSERT(false);
				break;
			}
		}

		Shader* shader = material->getShader();
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			Texture* texture = material->getTexture(i);
			if (texture)
			{
				bgfx::setTexture(
					i,
					shader->getTextureSlot(i).m_uniform_handle,
					texture->getTextureHandle());
			}
		}/*

		for (int i = 0; i < m_global_textures.size(); ++i)
		{
			const GlobalTexture& t = m_global_textures[i];
			bgfx::setTexture(i, t.m_uniform, t.m_texture);
		}*/

		if (m_applied_camera >= 0)
		{
			Matrix projection_matrix;
			float fov = getScene()->getCameraFOV(m_applied_camera);
			float near_plane = getScene()->getCameraNearPlane(m_applied_camera);
			float far_plane = getScene()->getCameraFarPlane(m_applied_camera);
			float ratio = float(m_width) / m_height;
			projection_matrix.setPerspective(
				Math::degreesToRadians(fov), ratio, near_plane, far_plane);
			projection_matrix.inverse();

			bgfx::setUniform(m_cam_inv_proj_uniform, &projection_matrix.m11);

			Universe& universe = getScene()->getUniverse();
			Matrix mtx = universe.getMatrix(getScene()->getCameraEntity(m_applied_camera));

			bgfx::setUniform(m_cam_view_uniform, &mtx.m11);
		}

		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::setVertexBuffer(&vb);
		bgfx::submit(
			m_view_idx,
			material->getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void
	renderAll(const Frustum& frustum, int64_t layer_mask, bool is_shadowmap)
	{
		PROFILE_FUNCTION();

		if (m_applied_camera < 0) return;

		m_tmp_grasses.clear();
		m_tmp_meshes.clear();
		m_tmp_terrains.clear();

		m_scene->getRenderableInfos(frustum, m_tmp_meshes, layer_mask);
		m_scene->getTerrainInfos(
			m_tmp_terrains,
			layer_mask,
			m_scene->getUniverse().getPosition(
				m_scene->getCameraEntity(m_applied_camera)),
			m_renderer.getFrameAllocator());
		m_is_current_light_global = true;
		m_current_light = m_scene->getActiveGlobalLight();
		renderMeshes(m_tmp_meshes);
		renderTerrains(m_tmp_terrains);
		if (!is_shadowmap)
		{
			m_scene->getGrassInfos(
				frustum, m_tmp_grasses, layer_mask, m_applied_camera);
			renderGrasses(m_tmp_grasses);
		}
		m_current_light = -1;
	}


	virtual void toggleStats() override
	{
		m_debug_flags ^= BGFX_DEBUG_STATS;
		bgfx::setDebug(m_debug_flags);
	}


	virtual void setWindowHandle(void* data) override
	{
		m_default_framebuffer = m_allocator.newObject<FrameBuffer>(
			"default", m_width, m_height, data);
	}


	virtual void renderModel(Model& model, const Matrix& mtx) override
	{
		RenderableMesh mesh;
		mesh.m_matrix = &mtx;
		mesh.m_model = &model;
		mesh.m_pose = nullptr;
		for (int i = 0; i < model.getMeshCount(); ++i)
		{
			mesh.m_mesh = &model.getMesh(i);
			renderRigidMesh(mesh);
		}
	}


	void setPoseUniform(const RenderableMesh& renderable_mesh) const
	{
		Matrix bone_mtx[64];

		const Pose& pose = *renderable_mesh.m_pose;
		const Model& model = *renderable_mesh.m_model;
		Vec3* poss = pose.getPositions();
		Quat* rots = pose.getRotations();

		ASSERT(pose.getCount() <= lengthOf(bone_mtx));
		for (int bone_index = 0, bone_count = pose.getCount();
			 bone_index < bone_count;
			 ++bone_index)
		{
			rots[bone_index].toMatrix(bone_mtx[bone_index]);
			bone_mtx[bone_index].translate(poss[bone_index]);
			bone_mtx[bone_index] = bone_mtx[bone_index] *
								   model.getBone(bone_index).inv_bind_matrix;
		}
		bgfx::setUniform(m_bone_matrices_uniform, bone_mtx, pose.getCount());
	}


	void renderSkinnedMesh(const RenderableMesh& info)
	{
		if (!info.m_model->isReady())
		{
			return;
		}
		const Mesh& mesh = *info.m_mesh;
		const Geometry& geometry = info.m_model->getGeometry();
		Material* material = mesh.getMaterial();

		setPoseUniform(info);
		setMaterial(material);
		bgfx::setTransform(info.m_matrix);
		bgfx::setVertexBuffer(geometry.getAttributesArrayID(),
							  mesh.getAttributeArrayOffset() /
								  mesh.getVertexDefinition().getStride(),
							  mesh.getAttributeArraySize() /
								  mesh.getVertexDefinition().getStride());
		bgfx::setIndexBuffer(geometry.getIndicesArrayID(),
							 mesh.getIndicesOffset(),
							 mesh.getIndexCount());
		bgfx::setState(m_render_state | material->getRenderStates());
		bgfx::submit(m_view_idx,
					 info.m_mesh->getMaterial()
						 ->getShaderInstance()
						 .m_program_handles[m_pass_idx]);
	}


	virtual void setScissor(int x, int y, int width, int height) override
	{
		bgfx::setScissor(x, y, width, height);
	}


	virtual void render(TransientGeometry& geom,
						int first_index,
						int num_indices,
						Material& material,
						bgfx::TextureHandle* texture) override
	{
		bgfx::setState(m_render_state | material.getRenderStates());
		bgfx::setTransform(nullptr);
		setMaterial(&material);
		if (texture)
		{
			bgfx::setTexture(
				0,
				material.getShader()->getTextureSlot(0).m_uniform_handle,
				*texture);
		}
		bgfx::setVertexBuffer(&geom.getVertexBuffer(), 0, geom.getNumVertices());
		bgfx::setIndexBuffer(&geom.getIndexBuffer(), first_index, num_indices);
		bgfx::submit(
			m_view_idx,
			material.getShaderInstance().m_program_handles[m_pass_idx]);
	}


	void renderRigidMesh(const RenderableMesh& info)
	{
		if (!info.m_model->isReady())
		{
			return;
		}
		int instance_idx = info.m_mesh->getInstanceIdx();
		if (instance_idx == -1)
		{
			instance_idx = m_instance_data_idx;
			m_instance_data_idx =
				(m_instance_data_idx + 1) % lengthOf(m_instances_data);
			if (m_instances_data[instance_idx].m_buffer)
			{
				finishInstances(instance_idx);
			}
			info.m_mesh->setInstanceIdx(instance_idx);
		}
		InstanceData& data = m_instances_data[instance_idx];
		if (!data.m_buffer)
		{
			data.m_buffer = bgfx::allocInstanceDataBuffer(
				InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix));
			data.m_instance_count = 0;
			data.m_mesh = info;
		}
		Matrix* mtcs = (Matrix*)data.m_buffer->data;
		mtcs[data.m_instance_count] = *info.m_matrix;
		++data.m_instance_count;
		if (data.m_instance_count == InstanceData::MAX_INSTANCE_COUNT)
		{
			const Mesh& mesh = *info.m_mesh;
			const Geometry& geometry = info.m_model->getGeometry();
			Material* material = mesh.getMaterial();

			setMaterial(material);
			bgfx::setVertexBuffer(geometry.getAttributesArrayID(),
								  mesh.getAttributeArrayOffset() /
									  mesh.getVertexDefinition().getStride(),
								  mesh.getAttributeArraySize() /
									  mesh.getVertexDefinition().getStride());
			bgfx::setIndexBuffer(geometry.getIndicesArrayID(),
								 mesh.getIndicesOffset(),
								 mesh.getIndexCount());
			bgfx::setState(m_render_state | material->getRenderStates());
			bgfx::setInstanceDataBuffer(data.m_buffer, data.m_instance_count);
			bgfx::submit(m_view_idx,
						 info.m_mesh->getMaterial()
							 ->getShaderInstance()
							 .m_program_handles[m_pass_idx]);
			data.m_mesh.m_mesh->setInstanceIdx(-1);
			data.m_buffer = nullptr;
			data.m_instance_count = 0;
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
				default:
					ASSERT(false);
					break;
			}
		}

		Shader* shader = material->getShader();
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			Texture* texture = material->getTexture(i);
			if (texture)
			{
				bgfx::setTexture(
					i,
					shader->getTextureSlot(i).m_uniform_handle,
					texture->getTextureHandle());
			}
		}

		Vec4 specular_shininess(material->getSpecular(),
								material->getShininess());
		bgfx::setUniform(m_specular_shininess_uniform, &specular_shininess);

		int global_texture_offset = shader->getTextureSlotCount();
		for (int i = 0; i < m_global_textures.size(); ++i)
		{
			const GlobalTexture& t = m_global_textures[i];
			bgfx::setTexture(
				i + global_texture_offset, t.m_uniform, t.m_texture);
		}
	}


	void renderTerrain(const TerrainInfo& info)
	{
		if (!info.m_terrain->getMaterial()->isReady())
		{
			return;
		}
		auto& inst = m_terrain_instances[info.m_index];
		if ((inst.m_count > 0 &&
			 inst.m_infos[0]->m_terrain != info.m_terrain) ||
			inst.m_count == lengthOf(inst.m_infos))
		{
			finishTerrainInstances(info.m_index);
		}
		inst.m_infos[inst.m_count] = &info;
		++inst.m_count;
	}


	void finishTerrainInstances(int index)
	{
		if (m_terrain_instances[index].m_count == 0)
		{
			return;
		}
		const TerrainInfo& info = *m_terrain_instances[index].m_infos[0];
		Material* material = info.m_terrain->getMaterial();
		if (!material->isReady())
		{
			return;
		}
		Texture* detail_texture = info.m_terrain->getDetailTexture();
		if (!detail_texture)
		{
			return;
		}

		Matrix inv_world_matrix;
		inv_world_matrix = info.m_world_matrix;
		inv_world_matrix.fastInverse();
		Vec3 camera_pos = m_scene->getUniverse().getPosition(
			m_scene->getCameraEntity(m_applied_camera));

		Vec3 rel_cam_pos = inv_world_matrix.multiplyPosition(camera_pos) /
						   info.m_terrain->getXZScale();

		const Geometry& geometry = *info.m_terrain->getGeometry();
		const Mesh& mesh = *info.m_terrain->getMesh();


		Vec4 terrain_params(info.m_terrain->getRootSize(),
							(float)detail_texture->getWidth(),
							(float)detail_texture->getDepth(),
							0);
		bgfx::setUniform(m_terrain_params_uniform, &terrain_params);

		bgfx::setUniform(m_rel_camera_pos_uniform, &Vec4(rel_cam_pos, 0));
		bgfx::setUniform(m_terrain_scale_uniform,
						 &Vec4(info.m_terrain->getScale(), 0));
		bgfx::setUniform(m_terrain_matrix_uniform, &info.m_world_matrix.m11);

		setMaterial(material);

		struct TerrainInstanceData
		{
			Vec4 m_quad_min_and_size;
			Vec4 m_morph_const;
		};
		const bgfx::InstanceDataBuffer* instance_buffer =
			bgfx::allocInstanceDataBuffer(m_terrain_instances[index].m_count,
										  sizeof(TerrainInstanceData));
		TerrainInstanceData* instance_data =
			(TerrainInstanceData*)instance_buffer->data;
		for (int i = 0; i < m_terrain_instances[index].m_count; ++i)
		{
			const TerrainInfo& info = *m_terrain_instances[index].m_infos[i];
			instance_data[i].m_quad_min_and_size.set(
				info.m_min.x, info.m_min.y, info.m_min.z, info.m_size);
			instance_data[i].m_morph_const.set(info.m_morph_const.x,
											   info.m_morph_const.y,
											   info.m_morph_const.z,
											   0);
		}

		bgfx::setVertexBuffer(geometry.getAttributesArrayID(),
							  mesh.getAttributeArrayOffset() /
								  mesh.getVertexDefinition().getStride(),
							  mesh.getAttributeArraySize() /
								  mesh.getVertexDefinition().getStride());
		int mesh_part_indices_count = mesh.getIndexCount() / 4;
		bgfx::setIndexBuffer(geometry.getIndicesArrayID(),
							 info.m_index * mesh_part_indices_count,
							 mesh_part_indices_count);
		bgfx::setState(m_render_state | mesh.getMaterial()->getRenderStates());
		bgfx::setInstanceDataBuffer(instance_buffer,
									m_terrain_instances[index].m_count);
		bgfx::submit(
			m_view_idx,
			material->getShaderInstance().m_program_handles[m_pass_idx]);
		m_terrain_instances[index].m_count = 0;
	}


	void renderGrass(const GrassInfo& grass)
	{
		const bgfx::InstanceDataBuffer* idb =
			bgfx::allocInstanceDataBuffer(grass.m_matrix_count, sizeof(Matrix));
		memcpy(idb->data,
			   &grass.m_matrices[0],
			   grass.m_matrix_count * sizeof(Matrix));
		const Mesh& mesh = grass.m_model->getMesh(0);
		const Geometry& geometry = grass.m_model->getGeometry();
		Material* material = mesh.getMaterial();

		setMaterial(material);
		bgfx::setVertexBuffer(geometry.getAttributesArrayID(),
							  mesh.getAttributeArrayOffset() /
								  mesh.getVertexDefinition().getStride(),
							  mesh.getAttributeArraySize() /
								  mesh.getVertexDefinition().getStride());
		bgfx::setIndexBuffer(geometry.getIndicesArrayID(),
							 mesh.getIndicesOffset(),
							 mesh.getIndexCount());
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
		for (auto* info : terrains)
		{
			renderTerrain(*info);
		}
		for (int i = 0; i < lengthOf(m_terrain_instances); ++i)
		{
			finishTerrainInstances(i);
		}
	}


	void renderMeshes(const Array<const RenderableMesh*>& meshes)
	{
		PROFILE_FUNCTION();
		for (auto* mesh : meshes)
		{
			if (mesh->m_pose && mesh->m_pose->getCount() > 0)
			{
				renderSkinnedMesh(*mesh);
			}
			else
			{
				renderRigidMesh(*mesh);
			}
		}
		finishInstances();
	}


	virtual void setViewport(int x, int y, int w, int h) override
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


	virtual void render() override
	{
		PROFILE_FUNCTION();

		if (!m_source.isReady())
		{
			return;
		}

		m_render_state = BGFX_STATE_RGB_WRITE | BGFX_STATE_ALPHA_WRITE |
						 BGFX_STATE_DEPTH_WRITE | BGFX_STATE_MSAA;
		m_render_state |= m_is_wireframe ? BGFX_STATE_PT_LINESTRIP : 0;
		m_view_idx = m_renderer.getViewCounter();
		m_pass_idx = -1;
		m_current_framebuffer = m_default_framebuffer;
		m_current_light = -1;
		m_global_textures.clear();
		m_view2pass_map.assign(0xFF);
		m_instance_data_idx = 0;
		m_point_light_shadowmaps.clear();
		for (int i = 0; i < lengthOf(m_terrain_instances); ++i)
		{
			m_terrain_instances[i].m_count = 0;
		}
		for (int i = 0; i < lengthOf(m_instances_data); ++i)
		{
			m_instances_data[i].m_buffer = nullptr;
			m_instances_data[i].m_instance_count = 0;
		}

		if (lua_getglobal(m_source.m_lua_state, "render") == LUA_TFUNCTION)
		{
			lua_pushlightuserdata(m_source.m_lua_state, this);
			if (lua_pcall(m_source.m_lua_state, 1, 0, 0) != LUA_OK)
			{
				g_log_error.log("lua")
					<< lua_tostring(m_source.m_lua_state, -1);
				lua_pop(m_source.m_lua_state, 1);
			}
		}
		else
		{
			lua_pop(m_source.m_lua_state, 1);
		}
		finishInstances();

		m_renderer.getFrameAllocator().clear();
	}


	virtual void setScene(RenderScene* scene) override { m_scene = scene; }


	virtual RenderScene* getScene() override { return m_scene; }


	virtual void setWireframe(bool wireframe) override
	{
		m_is_wireframe = wireframe;
	}


	struct GlobalTexture
	{
		bgfx::TextureHandle m_texture;
		bgfx::UniformHandle m_uniform;
	};


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

	TerrainInstance m_terrain_instances[4];
	uint32_t m_debug_flags;
	uint8_t m_view_idx;
	int m_pass_idx;
	StaticArray<uint8_t, 256> m_view2pass_map;
	uint64_t m_render_state;
	IAllocator& m_allocator;
	Renderer& m_renderer;
	PipelineImpl& m_source;
	RenderScene* m_scene;
	FrameBuffer* m_current_framebuffer;
	FrameBuffer* m_default_framebuffer;
	Array<FrameBuffer*> m_framebuffers;
	Array<GlobalTexture> m_global_textures;
	Array<bgfx::UniformHandle> m_uniforms;
	Array<Material*> m_materials;
	Array<PointLightShadowmap> m_point_light_shadowmaps;
	InstanceData m_instances_data[128];
	int m_instance_data_idx;
	ComponentIndex m_applied_camera;
	ComponentIndex m_current_light;
	bool m_is_current_light_global;
	bool m_is_wireframe;
	Frustum m_camera_frustum;

	Matrix m_shadow_viewprojection[4];
	Vec4 m_shadowmap_splits;
	int m_view_x;
	int m_view_y;
	int m_width;
	int m_height;
	AssociativeArray<uint32_t, CustomCommandHandler> m_custom_commands_handlers;
	Array<const RenderableMesh*> m_tmp_meshes;
	Array<const TerrainInfo*> m_tmp_terrains;
	Array<GrassInfo> m_tmp_grasses;
	bgfx::UniformHandle m_specular_shininess_uniform;
	bgfx::UniformHandle m_bone_matrices_uniform;
	bgfx::UniformHandle m_terrain_scale_uniform;
	bgfx::UniformHandle m_rel_camera_pos_uniform;
	bgfx::UniformHandle m_terrain_params_uniform;
	bgfx::UniformHandle m_fog_color_density_uniform;
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
	Material* m_debug_line_material;
	int m_has_shadowmap_define_idx;

private:
	void operator=(const PipelineInstanceImpl&);
	PipelineInstanceImpl(const PipelineInstanceImpl&);
};


Pipeline::Pipeline(const Path& path,
				   ResourceManager& resource_manager,
				   IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
{
	if (BaseVertex::s_vertex_decl.getStride() == 0)
	{
		BaseVertex::s_vertex_decl.begin();
		BaseVertex::s_vertex_decl.add(
			bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
		BaseVertex::s_vertex_decl.add(
			bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8);
		BaseVertex::s_vertex_decl.add(
			bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
		BaseVertex::s_vertex_decl.end();
	}
}


PipelineInstance* PipelineInstance::create(Pipeline& pipeline,
										   IAllocator& allocator)
{
	return allocator.newObject<PipelineInstanceImpl>(pipeline, allocator);
}


void PipelineInstance::destroy(PipelineInstance* pipeline)
{
	static_cast<PipelineInstanceImpl*>(pipeline)
		->m_allocator.deleteObject(pipeline);
}


Resource* PipelineManager::createResource(const Path& path)
{
	return m_allocator.newObject<PipelineImpl>(path, getOwner(), m_allocator);
}


void PipelineManager::destroyResource(Resource& resource)
{
	m_allocator.deleteObject(static_cast<PipelineImpl*>(&resource));
}


namespace LuaAPI
{


void setPass(PipelineInstanceImpl* pipeline, const char* pass)
{
	pipeline->setPass(pass);
}


void setFramebuffer(PipelineInstanceImpl* pipeline,
					const char* framebuffer_name)
{
	pipeline->setCurrentFramebuffer(framebuffer_name);
}


void enableAlphaWrite(PipelineInstanceImpl* pipeline)
{
	pipeline->enableAlphaWrite();
}


void disableAlphaWrite(PipelineInstanceImpl* pipeline)
{
	pipeline->disableAlphaWrite();
}


void enableDepthWrite(PipelineInstanceImpl* pipeline)
{
	pipeline->enableDepthWrite();
}


void disableDepthWrite(PipelineInstanceImpl* pipeline)
{
	pipeline->disableDepthWrite();
}


void enableRGBWrite(PipelineInstanceImpl* pipeline)
{
	pipeline->enableRGBWrite();
}


void disableRGBWrite(PipelineInstanceImpl* pipeline)
{
	pipeline->disableRGBWrite();
}


void enableBlending(PipelineInstanceImpl* pipeline)
{
	pipeline->enableBlending();
}


void disableBlending(PipelineInstanceImpl* pipeline)
{
	pipeline->disableBlending();
}


void applyCamera(PipelineInstanceImpl* pipeline, const char* slot)
{
	pipeline->applyCamera(slot);
}


void clear(PipelineInstanceImpl* pipeline, const char* buffers)
{
	uint16_t flags = 0;
	if (strcmp(buffers, "all") == 0)
	{
		flags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;
	}
	else if (strcmp(buffers, "depth") == 0)
	{
		flags = BGFX_CLEAR_DEPTH;
	}
	bgfx::setViewClear(pipeline->m_view_idx, flags, 0x303030ff, 1.0f, 0);
	bgfx::touch(pipeline->m_view_idx);
}


void renderModels(PipelineInstanceImpl* pipeline,
				  int64_t layer_mask,
				  bool is_point_light_render)
{
	if (is_point_light_render)
	{
		pipeline->renderPointLightInfluencedGeometry(pipeline->m_camera_frustum,
													 layer_mask);
	}
	else
	{
		pipeline->renderAll(pipeline->m_camera_frustum, layer_mask, false);
	}
}


void bindFramebufferTexture(PipelineInstanceImpl* pipeline,
							const char* framebuffer_name,
							int renderbuffer_index,
							int uniform_idx)
{
	pipeline->bindFramebufferTexture(
		framebuffer_name, renderbuffer_index, uniform_idx);
}


void executeCustomCommand(PipelineInstanceImpl* pipeline, const char* command)
{
	pipeline->executeCustomCommand(crc32(command));
}


bool hasScene(PipelineInstanceImpl* pipeline)
{
	return pipeline->getScene() != nullptr;
}


bool cameraExists(PipelineInstanceImpl* pipeline, const char* slot_name)
{
	return pipeline->getScene()->getCameraInSlot(slot_name) != INVALID_ENTITY;
}


float getFPS(PipelineInstanceImpl* pipeline)
{
	return pipeline->m_renderer.getEngine().getFPS();
}


void renderDebugShapes(PipelineInstanceImpl* pipeline)
{
	pipeline->renderDebugLines();
	pipeline->renderDebugPoints();
}


int renderLocalLightsShadowmaps(lua_State* L)
{
	if (!LuaWrapper::isType<PipelineInstanceImpl*>(L, 1)
		|| !LuaWrapper::isType<int>(L, 2)
		|| !LuaWrapper::isType<const char*>(L, 4))
	{
		return 0;
	}

	FrameBuffer* fbs[16];
	auto* pipeline = (PipelineInstanceImpl*)lua_touserdata(L, 1);
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
	int64_t layer_mask = (int64_t)lua_tonumber(L, 2);
	ComponentIndex camera = scene->getCameraInSlot(lua_tostring(L, 4));
	pipeline->renderLocalLightShadowmaps(camera, fbs, len, layer_mask);

	return 0;
}


void renderShadowmap(PipelineInstanceImpl* pipeline,
					 int64_t layer_mask,
					 const char* slot)
{
	pipeline->renderShadowmap(pipeline->getScene()->getCameraInSlot(slot),
							  layer_mask);
}


int createUniform(PipelineInstanceImpl* pipeline, const char* name)
{
	bgfx::UniformHandle handle =
		bgfx::createUniform(name, bgfx::UniformType::Int1);
	pipeline->m_uniforms.push(handle);
	return pipeline->m_uniforms.size() - 1;
}


int loadMaterial(PipelineInstanceImpl* pipeline, const char* path)
{
	ResourceManagerBase* material_manager =
		pipeline->m_source.getResourceManager().get(ResourceManager::MATERIAL);
	auto* material =
		static_cast<Material*>(material_manager->load(Lumix::Path(path)));

	pipeline->m_materials.push(material);
	return pipeline->m_materials.size() - 1;
}



void drawQuad(
	PipelineInstanceImpl* pipeline, float x, float y, float w, float h, int material_index)
{
	pipeline->drawQuad(x, y, w, h, material_index);
}


void print(int x, int y, const char* text)
{
	bgfx::dbgTextPrintf(x, y, 0x4f, text);
}


} // namespace LuaAPI


void PipelineImpl::registerCFunctions()
{
	registerCFunction(
		"drawQuad",
		LuaWrapper::wrap<decltype(&LuaAPI::drawQuad), LuaAPI::drawQuad>);
	registerCFunction(
		"print", LuaWrapper::wrap<decltype(&LuaAPI::print), LuaAPI::print>);
	registerCFunction("loadMaterial",
					  LuaWrapper::wrap<decltype(&LuaAPI::loadMaterial),
									   LuaAPI::loadMaterial>);
	registerCFunction("createUniform",
					  LuaWrapper::wrap<decltype(&LuaAPI::createUniform),
									   LuaAPI::createUniform>);
	registerCFunction("setFramebuffer",
					  LuaWrapper::wrap<decltype(&LuaAPI::setFramebuffer),
									   LuaAPI::setFramebuffer>);
	registerCFunction("enableBlending",
					  LuaWrapper::wrap<decltype(&LuaAPI::enableBlending),
									   LuaAPI::enableBlending>);
	registerCFunction("disableBlending",
					  LuaWrapper::wrap<decltype(&LuaAPI::disableBlending),
									   LuaAPI::disableBlending>);
	registerCFunction("enableAlphaWrite",
					  LuaWrapper::wrap<decltype(&LuaAPI::enableAlphaWrite),
									   LuaAPI::enableAlphaWrite>);
	registerCFunction("disableAlphaWrite",
					  LuaWrapper::wrap<decltype(&LuaAPI::disableAlphaWrite),
									   LuaAPI::disableAlphaWrite>);
	registerCFunction("enableRGBWrite",
					  LuaWrapper::wrap<decltype(&LuaAPI::enableRGBWrite),
									   LuaAPI::enableRGBWrite>);
	registerCFunction("disableRGBWrite",
					  LuaWrapper::wrap<decltype(&LuaAPI::disableRGBWrite),
									   LuaAPI::disableRGBWrite>);
	registerCFunction("enableDepthWrite",
					  LuaWrapper::wrap<decltype(&LuaAPI::enableDepthWrite),
									   LuaAPI::enableDepthWrite>);
	registerCFunction("disableDepthWrite",
					  LuaWrapper::wrap<decltype(&LuaAPI::disableDepthWrite),
									   LuaAPI::disableDepthWrite>);
	registerCFunction(
		"setPass",
		LuaWrapper::wrap<decltype(&LuaAPI::setPass), LuaAPI::setPass>);
	registerCFunction(
		"applyCamera",
		LuaWrapper::wrap<decltype(&LuaAPI::applyCamera), LuaAPI::applyCamera>);
	registerCFunction(
		"clear", LuaWrapper::wrap<decltype(&LuaAPI::clear), LuaAPI::clear>);
	registerCFunction("renderModels",
					  LuaWrapper::wrap<decltype(&LuaAPI::renderModels),
									   LuaAPI::renderModels>);
	registerCFunction("renderShadowmap",
					  LuaWrapper::wrap<decltype(&LuaAPI::renderShadowmap),
									   LuaAPI::renderShadowmap>);
	registerCFunction("renderLocalLightsShadowmaps",
					  &LuaAPI::renderLocalLightsShadowmaps);
	registerCFunction(
		"bindFramebufferTexture",
		LuaWrapper::wrap<decltype(&LuaAPI::bindFramebufferTexture),
						 LuaAPI::bindFramebufferTexture>);
	registerCFunction("executeCustomCommand",
					  LuaWrapper::wrap<decltype(&LuaAPI::executeCustomCommand),
									   LuaAPI::executeCustomCommand>);
	registerCFunction("renderDebugShapes",
					  LuaWrapper::wrap<decltype(&LuaAPI::renderDebugShapes),
									   LuaAPI::renderDebugShapes>);
	registerCFunction(
		"getFPS", LuaWrapper::wrap<decltype(&LuaAPI::getFPS), LuaAPI::getFPS>);
	registerCFunction(
		"cameraExists", LuaWrapper::wrap<decltype(&LuaAPI::cameraExists), LuaAPI::cameraExists>);
	registerCFunction(
		"hasScene", LuaWrapper::wrap<decltype(&LuaAPI::hasScene), LuaAPI::hasScene>);
}


} // ~namespace Lumix
