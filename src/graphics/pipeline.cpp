#include "graphics/pipeline.h"
#include "graphics/gl_ext.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/string.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/bitmap_font.h"
#include "graphics/frame_buffer.h"
#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lumix
{

struct PipelineImpl;
struct PipelineInstanceImpl;


static const uint32_t LIGHT_DIR_HASH = crc32("light_dir");
static const uint32_t TERRAIN_SCALE_HASH = crc32("terrain_scale");
static const uint32_t BONE_MATRICES_HASH = crc32("bone_matrices");
static const uint32_t CAMERA_POS_HASH = crc32("camera_pos");
static const uint32_t POINT_LIGHT_HASH = crc32("point_light");
static float split_distances[] = { 0.01f, 5, 20, 100, 300 };
static const float SHADOW_CAM_NEAR = 0.1f;
static const float SHADOW_CAM_FAR = 10000.0f;


struct RenderModelsMeshContext
{
	RenderModelsMeshContext()
		: m_mesh(NULL)
		, m_shader(NULL)
		, m_world_matrix_uniform_location(-1)
		, m_indices_offset(0)
		, m_vertex_count(0)
	{
	}

	const Mesh* m_mesh;
	Shader* m_shader;
	int m_world_matrix_uniform_location;
	int m_indices_offset;
	int m_vertex_count;
};


struct Command
{
	virtual ~Command() {}
	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) = 0;
	virtual void execute(PipelineInstanceImpl& pipeline) = 0;
};


struct CustomCommand : public Command
{
	CustomCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	uint32_t m_name;
};


struct PolygonModeCommand : public Command
{
	PolygonModeCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	bool m_fill;
};


struct SetPassCommand : public Command
{
	SetPassCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	uint32_t m_pass_hash;
};


struct ClearCommand : public Command
{
	ClearCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	uint32_t m_buffers;
};


struct RenderModelsCommand : public Command
{
	RenderModelsCommand(IAllocator&) : m_point_light_influenced_geometry(false) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	int64_t m_layer_mask;
	bool m_point_light_influenced_geometry;
};


struct ApplyCameraCommand : public Command
{
	ApplyCameraCommand(IAllocator& allocator)
		: m_camera_slot(allocator)
	{ }
	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	string m_camera_slot;
};


struct BindFramebufferCommand : public Command
{
	BindFramebufferCommand(IAllocator& allocator)
		: m_buffer_name(allocator)
	{ }

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	string m_buffer_name;
};


struct UnbindFramebufferCommand : public Command
{
	UnbindFramebufferCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl&, JsonSerializer&) override {}
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct DrawScreenQuadCommand : public Command
{
	DrawScreenQuadCommand(IAllocator& allocator) : m_allocator(allocator) {}
	~DrawScreenQuadCommand();

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	IAllocator& m_allocator;
	Mesh* m_mesh;
	Geometry* m_geometry;
};


struct RenderDebugLinesCommand : public Command
{
	RenderDebugLinesCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct RenderDebugTextsCommand : public Command
{
	RenderDebugTextsCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct BindFramebufferTextureCommand : public Command
{
	BindFramebufferTextureCommand(IAllocator& allocator)
		: m_framebuffer_name(allocator)
	{ }

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	string m_framebuffer_name;
	uint32_t m_renderbuffer_index;
	uint32_t m_texture_uint;
};


struct RenderShadowmapCommand : public Command
{
	RenderShadowmapCommand(IAllocator& allocator)
		: m_camera_slot(allocator)
	{}

	virtual void deserialize(PipelineImpl& pipeline, JsonSerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;

	int64_t m_layer_mask;
	StackAllocator<128> m_string_allocator;
	string m_camera_slot;
};


struct BindShadowmapCommand : public Command
{
	BindShadowmapCommand(IAllocator&) {}

	virtual void deserialize(PipelineImpl&, JsonSerializer&) override {}
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct PipelineImpl : public Pipeline
{
	struct FrameBufferDeclaration
	{
		FrameBufferDeclaration()
			: m_name(m_name_allocator)
		{ }

		int32_t m_width;
		int32_t m_height;
		int32_t m_color_buffers_count;
		bool m_is_depth_buffer;
		StackAllocator<64> m_name_allocator;
		string m_name;
	};
	
	struct CommandCreator
	{
		typedef Delegate<Command* (IAllocator&)> Creator;
		Creator m_creator;
		uint32_t m_type_hash;
	};

	template <typename T>
	static Command* CreateCommand(IAllocator& allocator)
	{
		return allocator.newObject<T>(allocator);
	}


	PipelineImpl(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Pipeline(path, resource_manager, allocator)
		, m_allocator(allocator)
		, m_commands(allocator)
		, m_framebuffers(allocator)
		, m_command_creators(allocator)
	{
		addCommandCreator("clear").bind<&CreateCommand<ClearCommand> >();
		addCommandCreator("custom").bind<&CreateCommand<CustomCommand> >();
		addCommandCreator("render_models").bind<&CreateCommand<RenderModelsCommand> >();
		addCommandCreator("apply_camera").bind<&CreateCommand<ApplyCameraCommand> >();
		addCommandCreator("bind_framebuffer").bind<&CreateCommand<BindFramebufferCommand> >();
		addCommandCreator("unbind_framebuffer").bind<&CreateCommand<UnbindFramebufferCommand> >();
		addCommandCreator("draw_screen_quad").bind<&CreateCommand<DrawScreenQuadCommand> >();
		addCommandCreator("bind_framebuffer_texture").bind<&CreateCommand<BindFramebufferTextureCommand> >();
		addCommandCreator("render_shadowmap").bind<&CreateCommand<RenderShadowmapCommand> >();
		addCommandCreator("bind_shadowmap").bind<&CreateCommand<BindShadowmapCommand> >();
		addCommandCreator("render_debug_lines").bind<&CreateCommand<RenderDebugLinesCommand> >();
		addCommandCreator("render_debug_texts").bind<&CreateCommand<RenderDebugTextsCommand> >();
		addCommandCreator("polygon_mode").bind<&CreateCommand<PolygonModeCommand> >();
		addCommandCreator("set_pass").bind<&CreateCommand<SetPassCommand> >();
	}


	Renderer& getRenderer()
	{
		return static_cast<PipelineManager*>(m_resource_manager.get(ResourceManager::PIPELINE))->getRenderer();
	}


	virtual ~PipelineImpl() override
	{
		ASSERT(isEmpty());
	}


	virtual void doUnload(void) override
	{
		for (int i = 0; i < m_commands.size(); ++i)
		{
			m_allocator.deleteObject(m_commands[i]);
		}
		m_commands.clear();
		onEmpty();
	}


	CommandCreator::Creator& addCommandCreator(const char* type)
	{
		CommandCreator& creator = m_command_creators.pushEmpty();
		creator.m_type_hash = crc32(type);
		return creator.m_creator;
	}


	Command* createCommand(uint32_t type_hash)
	{
		for (int i = 0; i < m_command_creators.size(); ++i)
		{
			if (m_command_creators[i].m_type_hash == type_hash)
			{
				return m_command_creators[i].m_creator.invoke(m_allocator);
			}
		}
		return NULL;
	}


	bool deserialierFramebuffers(JsonSerializer& serializer)
	{
		while (!serializer.isArrayEnd())
		{
			serializer.nextArrayItem();
			serializer.deserializeObjectBegin();
			FrameBufferDeclaration& framebuffer = m_framebuffers.pushEmpty();

			framebuffer.m_color_buffers_count = 0;
			framebuffer.m_is_depth_buffer = false;
			while (!serializer.isObjectEnd())
			{
				char label[40];
				serializer.deserializeLabel(label, sizeof(label));
				if (strcmp(label, "name") == 0)
				{
					serializer.deserialize(label, sizeof(label), "");
					framebuffer.m_name = label;
				}
				else if (strcmp(label, "width") == 0)
				{
					serializer.deserialize(framebuffer.m_width, 0);
				}
				else if (strcmp(label, "height") == 0)
				{
					serializer.deserialize(framebuffer.m_height, 0);
				}
				else if (strcmp(label, "color_buffers_count") == 0)
				{
					serializer.deserialize(framebuffer.m_color_buffers_count, 0);
				}
				else if (strcmp(label, "is_depth_buffer") == 0)
				{
					serializer.deserialize(framebuffer.m_is_depth_buffer, false);
				}
			}
			serializer.deserializeObjectEnd();
		}
		serializer.deserializeArrayEnd();
		return true;
	}

	virtual bool deserialize(JsonSerializer& serializer) override
	{
		serializer.deserializeObjectBegin();
		serializer.deserializeArrayBegin("frame_buffers");
		m_framebuffers.clear();
		bool status = deserialierFramebuffers(serializer);
	
		serializer.deserializeArrayBegin("commands");
		while (!serializer.isArrayEnd())
		{
			char tmp[255];
			serializer.deserializeArrayItem(tmp, 255, "");
			uint32_t command_type_hash = crc32(tmp);
			Command* cmd = createCommand(command_type_hash);
			if(cmd)
			{
				cmd->deserialize(*this, serializer);
				m_commands.push(cmd);
			}
			else
			{
				status = false;
				g_log_error.log("renderer") << "Unknown pipeline command \"" << tmp << "\" in pipeline " << getPath().c_str();
			}
		}
		serializer.deserializeArrayEnd();
		serializer.deserializeObjectEnd();
		return status;
	}


	virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override
	{
		if(success)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str(), m_allocator);
			deserialize(serializer);
			decrementDepCount();
		}
		else
		{
			onFailure();
		}

		fs.close(file);
	}

	IAllocator& m_allocator;
	Array<Command*> m_commands;
	Array<CommandCreator> m_command_creators;
	Array<FrameBufferDeclaration> m_framebuffers;
};


struct PipelineInstanceImpl : public PipelineInstance
{
	PipelineInstanceImpl(Pipeline& pipeline, IAllocator& allocator)
		: m_source(static_cast<PipelineImpl&>(pipeline))
		, m_active_camera(Component::INVALID)
		, m_custom_commands_handlers(allocator)
		, m_allocator(allocator)
		, m_terrain_infos(allocator)
		, m_framebuffers(allocator)
		, m_grass_infos(allocator)
		, m_renderable_infos(allocator)
	{
		m_scene = NULL;
		m_light_dir.set(0, -1, 0);
		m_width = m_height = -1;
		m_shadowmap_framebuffer = NULL;
		pipeline.onLoaded<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
	}


	~PipelineInstanceImpl()
	{
		m_source.getObserverCb().unbind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
		m_source.getResourceManager().get(ResourceManager::PIPELINE)->unload(m_source);
		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			m_allocator.deleteObject(m_framebuffers[i]);
		}
	}


	void setActiveCamera(const Component& cmp)
	{
		m_active_camera = cmp;
	}


	CustomCommandHandler& addCustomCommandHandler(const char* name) override
	{
		return m_custom_commands_handlers[crc32(name)];
	}


	FrameBuffer* getFrameBuffer(const char* name)
	{
		for (int i = 0, c = m_framebuffers.size(); i < c;  ++i)
		{
			if (strcmp(m_framebuffers[i]->getName(), name) == 0)
			{
				return m_framebuffers[i];
			}
		}
		return NULL;
	}


	void setRenderer(Renderer& renderer) override
	{
		m_renderer = &renderer;
	}


	Renderer& getRenderer() override
	{
		ASSERT(m_renderer);
		return *m_renderer;
	}


	virtual int getWidth() override
	{
		return m_width;
	}


	virtual int getHeight() override
	{
		return m_height;
	}


	void sourceLoaded(Resource::State old_state, Resource::State new_state)
	{
		if (old_state != Resource::State::READY && new_state == Resource::State::READY)
		{
			m_framebuffers.reserve(m_source.m_framebuffers.size());
			for (int i = 0; i < m_source.m_framebuffers.size(); ++i)
			{
				PipelineImpl::FrameBufferDeclaration& fb = m_source.m_framebuffers[i];
				m_framebuffers.push(m_allocator.newObject<FrameBuffer>(fb.m_width, fb.m_height, fb.m_color_buffers_count, fb.m_is_depth_buffer, fb.m_name.c_str()));
				if (fb.m_name == "shadowmap")
				{
					m_shadowmap_framebuffer = m_framebuffers.back();
				}
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
	}


	void renderShadowmap(Component camera, int64_t layer_mask)
	{
		PROFILE_FUNCTION();
		ASSERT(m_renderer != NULL);
		Component light_cmp = m_scene->getActiveGlobalLight();
		if (!light_cmp.isValid() || !camera.isValid())
		{
			return;
		}
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		m_shadowmap_framebuffer->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		
		Matrix light_mtx = light_cmp.entity.getMatrix();
		m_light_dir = light_mtx.getZVector();

		float shadowmap_height = (float)m_shadowmap_framebuffer->getHeight();
		float shadowmap_width = (float)m_shadowmap_framebuffer->getWidth();
		float viewports[] = 
			{ 0, 0
			, 0.5f, 0
			, 0, 0.5f
			, 0.5f, 0.5f};
		float camera_fov = m_scene->getCameraFOV(camera);
		float camera_ratio = m_scene->getCameraWidth(camera) / m_scene->getCameraHeight(camera);
		for (int split_index = 0; split_index < 4; ++split_index)
		{
			float* viewport = viewports + split_index * 2;
			glViewport((int)(1 + shadowmap_width * viewport[0]), (int)(1 + shadowmap_height * viewport[1]),
				(int)(0.5f * shadowmap_width - 2), (int)(0.5f * shadowmap_height - 2));

			Frustum frustum;
			Matrix camera_matrix = camera.entity.getMatrix();
			frustum.computePerspective(camera_matrix.getTranslation()
				, camera_matrix.getZVector()
				, camera_matrix.getYVector()
				, camera_fov
				, camera_ratio
				, split_distances[split_index]
				, split_distances[split_index + 1]
			);
			(&m_shadowmap_splits.x)[split_index] = split_distances[split_index + 1];

			Vec3 shadow_cam_pos = frustum.getCenter();
			float bb_size = frustum.getRadius();
			Matrix projection_matrix;
			Renderer::getOrthoMatrix(-bb_size, bb_size, -bb_size, bb_size, SHADOW_CAM_NEAR, SHADOW_CAM_FAR, &projection_matrix);
			m_renderer->setProjectionMatrix(projection_matrix);

			Vec3 light_forward = light_mtx.getZVector();
			shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
			Matrix modelview_matrix;
			Renderer::getLookAtMatrix(shadow_cam_pos, shadow_cam_pos + light_forward, light_mtx.getYVector(), &modelview_matrix);
			m_renderer->setViewMatrix(modelview_matrix);
			static const Matrix biasMatrix(
				0.5, 0.0, 0.0, 0.0,
				0.0, 0.5, 0.0, 0.0,
				0.0, 0.0, 0.5, 0.0,
				0.5, 0.5, 0.5, 1.0
				);
			m_shadow_modelviewprojection[split_index] = biasMatrix * (projection_matrix * modelview_matrix);

			renderTerrains(light_cmp, layer_mask);

			Frustum shadow_camera_frustum;
			shadow_camera_frustum.computeOrtho(shadow_cam_pos, -light_forward, light_mtx.getYVector(), bb_size * 2, bb_size * 2, SHADOW_CAM_NEAR, SHADOW_CAM_FAR);
			renderModels(shadow_camera_frustum, layer_mask);
		}
		FrameBuffer::unbind();
		glCullFace(GL_BACK);
	}


	void renderScreenGeometry(Geometry* geometry, Mesh* mesh)
	{
		if (mesh->getMaterial()->isReady())
		{
			ASSERT(m_renderer != NULL);
			Shader* shader = mesh->getMaterial()->getShader();
			Matrix mtx;
			Renderer::getOrthoMatrix(-1, 1, -1, 1, 0, 30, &mtx);
			m_renderer->setProjectionMatrix(mtx);
			m_renderer->setViewMatrix(Matrix::IDENTITY);
			mesh->getMaterial()->apply(*m_renderer, *this);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX0, m_shadow_modelviewprojection[0]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX1, m_shadow_modelviewprojection[1]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX2, m_shadow_modelviewprojection[2]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX3, m_shadow_modelviewprojection[3]);
			m_renderer->setUniform(*shader, "light_dir", LIGHT_DIR_HASH, m_light_dir);
			m_renderer->setUniform(*shader, "camera_pos", CAMERA_POS_HASH, m_active_camera.entity.getPosition());
			Component light_cmp = m_scene->getActiveGlobalLight();
			if (light_cmp.isValid())
			{
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::AMBIENT_COLOR, m_scene->getLightAmbientColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::AMBIENT_INTENSITY, m_scene->getLightAmbientIntensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getGlobalLightColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getPointLightIntensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
			}
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX, Matrix::IDENTITY);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::PROJECTION_MATRIX, mtx);
			bindGeometry(*m_renderer, *geometry, *mesh);
			renderGeometry(0, 6);
		}
	}


	void renderDebugTexts()
	{
		BitmapFont* font = m_scene->getDebugTextFont();
		if (!font || !font->isReady())
		{
			return;
		}
		m_renderer->cleanup();
		Matrix projection_matrix;
		Renderer::getOrthoMatrix(0, (float)m_width, 0, (float)m_height, 0, 10, &projection_matrix);
		m_renderer->setProjectionMatrix(projection_matrix);
		m_renderer->setViewMatrix(Matrix::IDENTITY);

		Geometry& geometry = m_scene->getDebugTextGeometry();
		Mesh& mesh = m_scene->getDebugTextMesh();
	
		font->getMaterial()->apply(*m_renderer, *this);
		bindGeometry(*m_renderer, geometry, mesh);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		renderGeometry(0, mesh.getIndexCount());
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	}


	void renderDebugLines()
	{
		m_renderer->cleanup();

		const Array<DebugLine>& lines = m_scene->getDebugLines();
		Shader& shader = m_renderer->getDebugShader();
		m_renderer->applyShader(shader, 0);

		for(int j = 0; j <= lines.size() / 256; ++j)
		{
			Vec3 positions[512];
			Vec3 colors[512];
			int indices[512];
			int offset = j * 256;
			for (int i = 0, c = Math::minValue(lines.size() - offset, 256); i < c; ++i)
			{
				positions[i * 2] = lines[offset + i].m_from;
				positions[i * 2 + 1] = lines[offset + i].m_to;
				colors[i * 2] = lines[offset + i].m_color;
				colors[i * 2 + 1] = lines[offset + i].m_color;
				indices[i * 2] = i * 2;
				indices[i * 2 + 1] = i * 2 + 1;
			}

			glEnableVertexAttribArray(shader.getAttribId(0));
			glVertexAttribPointer(shader.getAttribId(0), 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), positions);
			glEnableVertexAttribArray(shader.getAttribId(1));
			glVertexAttribPointer(shader.getAttribId(1), 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), colors);
			glDrawElements(GL_LINES, Math::minValue(lines.size() - offset, 256) * 2, GL_UNSIGNED_INT, indices);
		}
	}

	void renderGrass(const Component& light_cmp, const Frustum& frustum, int64_t layer_mask)
	{
		PROFILE_FUNCTION();
		if (m_active_camera.isValid())
		{
			const Material* last_material = NULL;
			m_grass_infos.clear();
			m_scene->getGrassInfos(frustum, m_grass_infos, layer_mask);
			for (int i = 0; i < m_grass_infos.size(); ++i)
			{
				const GrassInfo& info = m_grass_infos[i];
				const Mesh& mesh = *info.m_mesh;
				const Material& material = *mesh.getMaterial();
				if (material.isReady())
				{
					Shader* shader = material.getShader();
					if (&material != last_material)
					{
						material.apply(*m_renderer, *this);
						m_renderer->setUniform(*shader, "camera_pos", CAMERA_POS_HASH, m_active_camera.entity.getPosition());

						setLightUniforms(light_cmp, shader);

						last_material = &material;
					}
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::GRASS_MATRICES, info.m_matrices, info.m_matrix_count);

					bindGeometry(*m_renderer, *info.m_geometry, mesh);
					renderGeometry(mesh.getIndicesOffset(), mesh.getIndexCount() / info.m_mesh_copy_count * info.m_matrix_count);
				}
			}
		}
	}

	void renderTerrains(const Component& light_cmp, int64_t layer_mask)
	{
		PROFILE_FUNCTION();
		if (m_active_camera.isValid())
		{
			m_terrain_infos.clear();
			m_scene->getTerrainInfos(m_terrain_infos, layer_mask);
			Vec3 camera_position = m_active_camera.entity.getPosition();
			for (int i = 0; i < m_terrain_infos.size(); ++i)
			{
				if (m_terrain_infos[i].m_material && m_terrain_infos[i].m_material->isReady())
				{
					Matrix world_matrix;
					m_terrain_infos[i].m_entity.getMatrix(world_matrix);
					Shader* shader = m_terrain_infos[i].m_material->getShader();
					m_terrain_infos[i].m_material->apply(*m_renderer, *this);
			
					setLightUniforms(light_cmp, shader);

					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX, world_matrix);
					Vec3 scale;
					scale.x = m_terrain_infos[i].m_xz_scale;
					scale.y = m_terrain_infos[i].m_y_scale;
					scale.z = scale.x;
					m_renderer->setUniform(*shader, "terrain_scale", TERRAIN_SCALE_HASH, scale);
					m_scene->renderTerrain(m_terrain_infos[i], *m_renderer, *this, camera_position);
				}
			}
		}
	}


	void sortRenderables(Array<RenderableInfo>& infos)
	{
		PROFILE_FUNCTION();
		if (!infos.empty())
		{
			qsort(&infos[0], infos.size(), sizeof(RenderableInfo), [](const void* a, const void* b) -> int
			{
				const RenderableInfo* info1 = static_cast<const RenderableInfo*>(a);
				const RenderableInfo* info2 = static_cast<const RenderableInfo*>(b);
				return (int)(info1->m_key - info2->m_key);
			});
		}
	}


	void setLightUniforms(const Component& light_cmp, Shader* shader)
	{
		if (light_cmp.isValid())
		{
			if (light_cmp.type == POINT_LIGHT_HASH)
			{
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getPointLightColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getPointLightIntensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::LIGHT_POSITION, light_cmp.entity.getPosition());
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::LIGHT_RANGE, m_scene->getLightRange(light_cmp));
			}
			else
			{
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX0, m_shadow_modelviewprojection[0]);
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX1, m_shadow_modelviewprojection[1]);
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX2, m_shadow_modelviewprojection[2]);
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX3, m_shadow_modelviewprojection[3]);
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::AMBIENT_COLOR, m_scene->getLightAmbientColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::AMBIENT_INTENSITY, m_scene->getLightAmbientIntensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getGlobalLightColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getGlobalLightIntensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
			}
			m_renderer->setUniform(*shader, "light_dir", LIGHT_DIR_HASH, m_light_dir);
		}
	}


	bool setMeshContext(RenderModelsMeshContext* context, const Component& light_cmp)
	{
		const Material& material = *context->m_mesh->getMaterial();
		Shader* shader = material.getShader();
		uint32_t pass_hash = getRenderer().getPass();
		if (!shader->hasPass(pass_hash) || !material.isReady())
		{
			return false;
		}

		material.apply(*m_renderer, *this);
		
		setLightUniforms(light_cmp, shader);
		
		context->m_world_matrix_uniform_location = getUniformLocation(*shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX);
		context->m_shader = shader;
		context->m_indices_offset = context->m_mesh->getIndicesOffset();
		context->m_vertex_count = context->m_mesh->getIndexCount();
		if (context->m_world_matrix_uniform_location < 0)
		{
			return false;
		}
		return true;
	}


	void setPoseUniform(const RenderableMesh* LUMIX_RESTRICT renderable_mesh, RenderModelsMeshContext& mesh_context)
	{
		Matrix bone_mtx[64];

		const Pose& pose = *renderable_mesh->m_pose;
		const Model& model = *renderable_mesh->m_model;
		Vec3* poss = pose.getPositions();
		Quat* rots = pose.getRotations();
		ASSERT(pose.getCount() <= 64);
		for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
		{
			rots[bone_index].toMatrix(bone_mtx[bone_index]);
			bone_mtx[bone_index].translate(poss[bone_index]);
			bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
		}
		m_renderer->setUniform(*mesh_context.m_shader, "bone_matrices", BONE_MATRICES_HASH, bone_mtx, pose.getCount());
	}


	void renderPointLightInfluencedTerrains(const Frustum& frustum, int64_t layer_mask)
	{
		PROFILE_FUNCTION();

		m_renderable_infos.clear();

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		Array<Component> lights(m_allocator);
		m_scene->getPointLights(frustum, lights);
		for (int i = 0; i < lights.size(); ++i)
		{
			Component light = lights[i];
			renderTerrains(light, layer_mask);
		}
		glDisable(GL_BLEND);
	}


	void renderPointLightInfluencedGeometry(const Frustum& frustum, int64_t layer_mask)
	{
		PROFILE_FUNCTION();

		m_renderable_infos.clear();

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		Array<Component> lights(m_allocator);
		m_scene->getPointLights(frustum, lights);
		for (int i = 0; i < lights.size(); ++i)
		{
			Component light = lights[i];
			m_renderable_infos.clear();
			m_scene->getPointLightInfluencedGeometry(light, frustum, m_renderable_infos, layer_mask);
			if (m_renderable_infos.empty())
			{
				continue;
			}

			sortRenderables(m_renderable_infos);

			render(&m_renderable_infos, light);
		}
		glDisable(GL_BLEND);
	}


	void renderModels(const Frustum& frustum, int64_t layer_mask)
	{
		PROFILE_FUNCTION();

		m_renderable_infos.clear();
		m_scene->getRenderableInfos(frustum, m_renderable_infos, layer_mask);
		if (m_renderable_infos.empty())
		{
			return;
		}

		sortRenderables(m_renderable_infos);

		render(&m_renderable_infos, m_scene->getActiveGlobalLight());
	}


	void render(Array<RenderableInfo>* renderable_infos, const Component& light)
	{
		PROFILE_FUNCTION();
		RenderableInfo& sentinel = renderable_infos->pushEmpty();
		sentinel.m_key = 0;
		const RenderableInfo* LUMIX_RESTRICT info = &(*renderable_infos)[0];
		const RenderableInfo* LUMIX_RESTRICT end = &(*renderable_infos)[0] + renderable_infos->size() - 1;
		RenderModelsMeshContext mesh_context;
		int64_t last_key = 0;
		Matrix matrices[64];
		while (info != end)
		{
			mesh_context.m_mesh = info->m_mesh->m_mesh;
			if (!setMeshContext(&mesh_context, light))
			{
				++info;
				continue;
			}
			bindGeometry(*m_renderer, info->m_mesh->m_model->getGeometry(), *mesh_context.m_mesh);
			last_key = info->m_key;
			if (info->m_mesh->m_pose->getCount() > 0)
			{
				while (last_key == info->m_key)
				{
					const RenderableMesh* LUMIX_RESTRICT renderable_mesh = info->m_mesh;
					const Matrix& world_matrix = *renderable_mesh->m_matrix;
					setUniform(mesh_context.m_world_matrix_uniform_location, world_matrix);
					setPoseUniform(renderable_mesh, mesh_context);
					renderGeometry(mesh_context.m_indices_offset, mesh_context.m_vertex_count);
					++info;
				}
			}
			else
			{
				while (last_key == info->m_key)
				{
					Matrix* LUMIX_RESTRICT instance_matrix = matrices;
					const Matrix* last_instance_matrix = matrices + (sizeof(matrices) / sizeof(matrices[0]));
					while (last_key == info->m_key && instance_matrix < last_instance_matrix)
					{
						const RenderableMesh* LUMIX_RESTRICT renderable_mesh = info->m_mesh;
						*instance_matrix = *renderable_mesh->m_matrix;
						++instance_matrix;
						++info;
					}
					int instance_count = instance_matrix - matrices;
					setUniform(mesh_context.m_world_matrix_uniform_location, matrices, instance_count);
					renderInstancedGeometry(mesh_context.m_indices_offset, mesh_context.m_vertex_count, instance_count, *mesh_context.m_shader);
				}
			}
		}
	}


	virtual void resize(int w, int h) override
	{
		m_width = w;
		m_height = h;
	}

	virtual void render() override
	{
		PROFILE_FUNCTION();
		if (m_scene)
		{
			for (int i = 0; i < m_source.m_commands.size(); ++i)
			{
				m_source.m_commands[i]->execute(*this);
			}
		}
	}

	virtual FrameBuffer* getShadowmapFramebuffer() override
	{
		return m_shadowmap_framebuffer;
	}

	virtual void setScene(RenderScene* scene) override
	{
		m_scene = scene;
		m_active_camera = Component::INVALID;
	}

	virtual RenderScene* getScene() override
	{
		return m_scene;
	}

	IAllocator& m_allocator;
	PipelineImpl& m_source;
	RenderScene* m_scene;
	Array<FrameBuffer*> m_framebuffers;
	FrameBuffer* m_shadowmap_framebuffer;
	Matrix m_shadow_modelviewprojection[4];
	Renderer* m_renderer;
	Vec3 m_light_dir;
	Vec4 m_shadowmap_splits;
	int m_width;
	int m_height;
	AssociativeArray<uint32_t, CustomCommandHandler> m_custom_commands_handlers;
	Component m_active_camera;
	Array<TerrainInfo> m_terrain_infos;
	Array<GrassInfo> m_grass_infos;
	Array<RenderableInfo> m_renderable_infos;

	private:
		void operator=(const PipelineInstanceImpl&);
		PipelineInstanceImpl(const PipelineInstanceImpl&);
};


Pipeline::Pipeline(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
{
}


PipelineInstance* PipelineInstance::create(Pipeline& pipeline, IAllocator& allocator)
{
	return allocator.newObject<PipelineInstanceImpl>(pipeline, allocator);
}


void PipelineInstance::destroy(PipelineInstance* pipeline)
{
	static_cast<PipelineInstanceImpl*>(pipeline)->m_allocator.deleteObject(pipeline);
}


void PolygonModeCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	serializer.deserializeArrayItem(m_fill, true);
}


void PolygonModeCommand::execute(PipelineInstanceImpl& pipeline)
{
	glPolygonMode(GL_FRONT_AND_BACK, m_fill && !pipeline.getRenderer().isEditorWireframe() ? GL_FILL : GL_LINE);
}


void SetPassCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	char pass_name[50];
	serializer.deserializeArrayItem(pass_name, sizeof(pass_name), "");
	m_pass_hash = crc32(pass_name);
}


void SetPassCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.getRenderer().setPass(m_pass_hash);
}


void ClearCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	char tmp[256];
	serializer.deserializeArrayItem(tmp, 255, "all");
	if (strcmp(tmp, "all") == 0)
	{
		m_buffers = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
	}
	else if (strcmp(tmp, "depth") == 0)
	{
		m_buffers = GL_DEPTH_BUFFER_BIT;
	}
}


void ClearCommand::execute(PipelineInstanceImpl&)
{
	glClear(m_buffers);
}


void CustomCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	char tmp[256];
	serializer.deserializeArrayItem(tmp, 255, "");
	m_name = crc32(tmp);
}


void CustomCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.executeCustomCommand(m_name);
}



void RenderModelsCommand::deserialize(PipelineImpl&, JsonSerializer& serializer) 
{
	serializer.deserializeArrayItem(m_layer_mask, 0);
	serializer.deserializeArrayItem(m_point_light_influenced_geometry, false);
}


void RenderModelsCommand::execute(PipelineInstanceImpl& pipeline)
{
	if (m_point_light_influenced_geometry)
	{
		pipeline.renderPointLightInfluencedTerrains(pipeline.getScene()->getFrustum(), m_layer_mask);
		pipeline.renderPointLightInfluencedGeometry(pipeline.getScene()->getFrustum(), m_layer_mask);
	}
	else
	{
		pipeline.renderTerrains(pipeline.getScene()->getActiveGlobalLight(), m_layer_mask);
		pipeline.renderModels(pipeline.getScene()->getFrustum(), m_layer_mask);
		pipeline.renderGrass(pipeline.getScene()->getActiveGlobalLight(), pipeline.getScene()->getFrustum(), m_layer_mask);
	}
}


void ApplyCameraCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	serializer.deserializeArrayItem(m_camera_slot, "main");
}


void ApplyCameraCommand::execute(PipelineInstanceImpl& pipeline)
{
	ASSERT(pipeline.m_renderer != NULL);
	Component cmp = pipeline.m_scene->getCameraInSlot(m_camera_slot.c_str()); 
	pipeline.setActiveCamera(cmp);
	if (cmp.isValid())
	{
		pipeline.m_scene->setCameraSize(cmp, pipeline.m_width, pipeline.m_height);
		pipeline.m_scene->applyCamera(cmp);
	}
}


void BindFramebufferCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	serializer.deserializeArrayItem(m_buffer_name, "");
}


void BindFramebufferCommand::execute(PipelineInstanceImpl& pipeline)
{
	FrameBuffer* fb = pipeline.getFrameBuffer(m_buffer_name.c_str());
	if (fb)
	{
		fb->bind();
	}
}

	
void UnbindFramebufferCommand::execute(PipelineInstanceImpl&)
{
	FrameBuffer::unbind();
}


void RenderDebugLinesCommand::deserialize(PipelineImpl&, JsonSerializer&)
{
}


void RenderDebugLinesCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderDebugLines();
}


void RenderDebugTextsCommand::deserialize(PipelineImpl&, JsonSerializer&)
{
}


void RenderDebugTextsCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderDebugTexts();
}


DrawScreenQuadCommand::~DrawScreenQuadCommand()
{
	m_allocator.deleteObject(m_geometry);
	m_allocator.deleteObject(m_mesh);
}


void DrawScreenQuadCommand::deserialize(PipelineImpl& pipeline, JsonSerializer& serializer)
{
	m_geometry = m_allocator.newObject<Geometry>();
	VertexDef def;
	Renderer& renderer = pipeline.getRenderer();
	def.addAttribute(renderer, "in_position", VertexAttributeDef::FLOAT2);
	def.addAttribute(renderer, "in_tex_coords", VertexAttributeDef::FLOAT2);
	int indices[6] = { 0, 1, 2, 0, 2, 3 };
	const int GEOMETRY_VERTEX_ATTRIBUTE_COUNT = 16;
	float v[GEOMETRY_VERTEX_ATTRIBUTE_COUNT];
	
	for (int i = 0; i < GEOMETRY_VERTEX_ATTRIBUTE_COUNT; ++i)
	{
		serializer.deserializeArrayItem(v[i], 0);
	}

	m_geometry->setAttributesData(v, sizeof(v));
	m_geometry->setIndicesData(indices, sizeof(indices));

	char material_path[LUMIX_MAX_PATH];
	serializer.deserializeArrayItem(material_path, LUMIX_MAX_PATH, "");
	Material* material = static_cast<Material*>(pipeline.getResourceManager().get(ResourceManager::MATERIAL)->load(Path(material_path)));
	m_mesh = m_allocator.newObject<Mesh>(def, material, 0, 0, sizeof(v), 6, "screen_quad", m_allocator);
}


void DrawScreenQuadCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderScreenGeometry(m_geometry, m_mesh);
}


void BindFramebufferTextureCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	serializer.deserializeArrayItem(m_framebuffer_name, "");
	serializer.deserializeArrayItem(m_renderbuffer_index, 0);
	serializer.deserializeArrayItem(m_texture_uint, 0);
}


void BindFramebufferTextureCommand::execute(PipelineInstanceImpl& pipeline)
{
	FrameBuffer* fb = pipeline.getFrameBuffer(m_framebuffer_name.c_str());
	if (fb)
	{
		glActiveTexture(GL_TEXTURE0 + m_texture_uint);
		glBindTexture(GL_TEXTURE_2D, fb->getTexture(m_renderbuffer_index));
	}
}


void RenderShadowmapCommand::deserialize(PipelineImpl&, JsonSerializer& serializer)
{
	serializer.deserializeArrayItem(m_layer_mask, 0);
	serializer.deserializeArrayItem(m_camera_slot, 0);
}


void RenderShadowmapCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderShadowmap(pipeline.getScene()->getCameraInSlot(m_camera_slot.c_str()), m_layer_mask);
}


void BindShadowmapCommand::execute(PipelineInstanceImpl& pipeline)
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, pipeline.m_shadowmap_framebuffer->getDepthTexture());
}


Resource* PipelineManager::createResource(const Path& path)
{
	return m_allocator.newObject<PipelineImpl>(path, getOwner(), m_allocator);
}


void PipelineManager::destroyResource(Resource& resource)
{
	m_allocator.deleteObject(static_cast<PipelineImpl*>(&resource));
}


} // ~namespace Lumix
