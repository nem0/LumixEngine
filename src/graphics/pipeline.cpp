#include "graphics/pipeline.h"
#include "graphics/gl_ext.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/fs/file_system.h"
#include "core/iserializer.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/map.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/string.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/frame_buffer.h"
#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"


namespace Lumix
{

struct PipelineImpl;
struct PipelineInstanceImpl;


static const uint32_t SHADOW_MATRIX0_HASH = crc32("shadowmap_matrix0");
static const uint32_t SHADOW_MATRIX1_HASH = crc32("shadowmap_matrix1");
static const uint32_t SHADOW_MATRIX2_HASH = crc32("shadowmap_matrix2");
static const uint32_t SHADOW_MATRIX3_HASH = crc32("shadowmap_matrix3");
static const uint32_t LIGHT_DIR_HASH = crc32("light_dir");
static const uint32_t TERRAIN_SCALE_HASH = crc32("terrain_scale");
static const uint32_t BONE_MATRICES_HASH = crc32("bone_matrices");
static const uint32_t CAMERA_POS_HASH = crc32("camera_pos");
static float split_distances[] = { 0, 5, 20, 100, 300 };
static const float SHADOW_CAM_NEAR = 0.1f;
static const float SHADOW_CAM_FAR = 10000.0f;


struct Command
{
	virtual ~Command() {}
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) = 0;
	virtual void execute(PipelineInstanceImpl& pipeline) = 0;
};


struct CustomCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	uint32_t m_name;
};


struct PolygonModeCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	bool m_fill;
};


struct SetPassCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	uint32_t m_pass_hash;
};


struct ClearCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	uint32_t m_buffers;
};


struct RenderModelsCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	
	int64_t m_layer_mask;
};


struct ApplyCameraCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	string m_camera_slot;
};


struct BindFramebufferCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	string m_buffer_name;
};


struct UnbindFramebufferCommand : public Command
{
	virtual void deserialize(PipelineImpl&, ISerializer&) override {}
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct DrawScreenQuadCommand : public Command
{
	~DrawScreenQuadCommand();

	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	Material* m_material;
	Geometry* m_geometry;
};


struct RenderDebugLinesCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct BindFramebufferTextureCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	string m_framebuffer_name;
	uint32_t m_renderbuffer_index;
	uint32_t m_texture_uint;
};


struct RenderShadowmapCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;

	int64_t m_layer_mask;
	string m_camera_slot;
};


struct BindShadowmapCommand : public Command
{
	virtual void deserialize(PipelineImpl&, ISerializer&) override {}
	virtual void execute(PipelineInstanceImpl& pipeline) override;
};


struct PipelineImpl : public Pipeline
{
	struct FrameBufferDeclaration
	{
		int32_t m_width;
		int32_t m_height;
		int32_t m_mask;
		string m_name;
	};
	
	struct CommandCreator
	{
		typedef Delegate<Command*> Creator;
		Creator m_creator;
		uint32_t m_type_hash;
	};

	template <typename T>
	static Command* CreateCommand()
	{
		return LUMIX_NEW(T);
	}


	PipelineImpl(const Path& path, ResourceManager& resource_manager)
		: Pipeline(path, resource_manager)
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
		addCommandCreator("polygon_mode").bind<&CreateCommand<PolygonModeCommand> >();
		addCommandCreator("set_pass").bind<&CreateCommand<SetPassCommand> >();
	}


	virtual ~PipelineImpl() override
	{
		ASSERT(isEmpty());
	}


	virtual void doUnload(void) override
	{
		for (int i = 0; i < m_commands.size(); ++i)
		{
			LUMIX_DELETE(m_commands[i]);
		}
		m_commands.clear();
		onEmpty();
	}


	virtual FS::ReadCallback getReadCallback() override
	{
		FS::ReadCallback cb;
		cb.bind<PipelineImpl, &PipelineImpl::loaded>(this);
		return cb;
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
				return m_command_creators[i].m_creator.invoke();
			}
		}
		return NULL;
	}


	virtual bool deserialize(ISerializer& serializer) override
	{
		int32_t count;
		serializer.deserializeObjectBegin();
		serializer.deserialize("frame_buffer_count", count);
		serializer.deserializeArrayBegin("frame_buffers");
		m_framebuffers.resize(count);
		for(int i = 0; i < count; ++i)
		{
			int32_t render_buffer_count;
			char fb_name[31];
			serializer.deserializeArrayItem(fb_name, 30);
			m_framebuffers[i].m_name = fb_name;

			serializer.deserializeArrayItem(render_buffer_count);
			int mask = 0;
			char rb_name[30];
			for(int j = 0; j < render_buffer_count; ++j)
			{
				serializer.deserializeArrayItem(rb_name, 30);
				if(strcmp(rb_name, "depth") == 0)
				{
					mask |= FrameBuffer::DEPTH_BIT;
				}
				else
				{
					ASSERT(false);
				}
			}
			serializer.deserializeArrayItem(m_framebuffers[i].m_width);
			serializer.deserializeArrayItem(m_framebuffers[i].m_height);
			m_framebuffers[i].m_mask = mask;
		}
		serializer.deserializeArrayEnd();
		serializer.deserialize("shadowmap_width", m_shadowmap_framebuffer.m_width);
		serializer.deserialize("shadowmap_height", m_shadowmap_framebuffer.m_height);
		m_shadowmap_framebuffer.m_mask = FrameBuffer::DEPTH_BIT;
		
		serializer.deserializeArrayBegin("commands");
		while (!serializer.isArrayEnd())
		{
			char tmp[255];
			serializer.deserializeArrayItem(tmp, 255);
			uint32_t command_type_hash = crc32(tmp);
			Command* cmd = createCommand(command_type_hash);
			if(cmd)
			{
				cmd->deserialize(*this, serializer);
				m_commands.push(cmd);
			}
			else
			{
				g_log_error.log("renderer") << "Unknown pipeline command \"" << tmp << "\" in pipeline " << getPath().c_str();
			}
		}
		serializer.deserializeArrayEnd();
		serializer.deserializeObjectEnd();
		return true;
	}


	void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) 
	{
		if(success)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str());
			deserialize(serializer);
			decrementDepCount();
		}
		else
		{
			onFailure();
		}

		fs.close(file);
	}

	Array<Command*> m_commands;
	Array<CommandCreator> m_command_creators;
	Array<FrameBufferDeclaration> m_framebuffers;
	FrameBufferDeclaration m_shadowmap_framebuffer;
};


struct PipelineInstanceImpl : public PipelineInstance
{
	PipelineInstanceImpl(Pipeline& pipeline)
		: m_source(static_cast<PipelineImpl&>(pipeline))
		, m_active_camera(Component::INVALID)
	{
		m_scene = NULL;
		m_light_dir.set(0, -1, 0);
		m_width = m_height = -1;
		m_shadowmap_framebuffer = NULL;
		if(pipeline.isReady())
		{
			sourceLoaded(Resource::State::EMPTY, Resource::State::READY);
		}
		else
		{
			pipeline.getObserverCb().bind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
		}
	}

	~PipelineInstanceImpl()
	{
		m_source.getObserverCb().unbind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
		m_source.getResourceManager().get(ResourceManager::PIPELINE)->unload(m_source);
		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			LUMIX_DELETE(m_framebuffers[i]);
		}
		if (m_shadowmap_framebuffer)
		{
			LUMIX_DELETE(m_shadowmap_framebuffer);
		}
	}

	void setActiveCamera(const Component& cmp)
	{
		m_active_camera = cmp;
	}

	CustomCommandHandler& addCustomCommandHandler(const char* name)
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


	void sourceLoaded(Resource::State old_state, Resource::State new_state)
	{
		if (old_state != Resource::State::READY && new_state == Resource::State::READY)
		{
			PipelineImpl::FrameBufferDeclaration fb = m_source.m_shadowmap_framebuffer;
			m_shadowmap_framebuffer = LUMIX_NEW(FrameBuffer)(fb.m_width, fb.m_height, fb.m_mask, fb.m_name.c_str());
			m_framebuffers.reserve(m_source.m_framebuffers.size());
			for (int i = 0; i < m_source.m_framebuffers.size(); ++i)
			{
				fb = m_source.m_framebuffers[i];
				m_framebuffers.push(LUMIX_NEW(FrameBuffer)(fb.m_width, fb.m_height, fb.m_mask, fb.m_name.c_str()));
			}
		}
	}


	void executeCustomCommand(uint32_t name)
	{
		Map<uint32_t, CustomCommandHandler>::iterator iter = m_custom_commands_handlers.find(name);
		if (iter != m_custom_commands_handlers.end())
		{
			iter.second().invoke();
		}
	}


	void renderShadowmap(Component camera, int64_t layer_mask)
	{
		PROFILE_FUNCTION();
		ASSERT(m_renderer != NULL);
		Component light_cmp = m_scene->getLight(0);
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
			frustum.compute(camera_matrix.getTranslation()
				, camera_matrix.getZVector()
				, camera_matrix.getYVector()
				, camera_fov
				, camera_ratio
				, split_distances[split_index]
				, split_distances[split_index + 1]
			);
			(&m_shadowmap_splits.x)[split_index] = split_distances[split_index + 1];

			Vec3 shadow_cam_pos = frustum.m_center;
			float bb_size = frustum.m_size * 0.5f;
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

			Frustum shadow_camera_frustum;
			shadow_camera_frustum.computeOrtho(shadow_cam_pos, -light_forward, light_mtx.getYVector(), bb_size * 2, bb_size * 2, SHADOW_CAM_NEAR, SHADOW_CAM_FAR);

			renderTerrains(layer_mask);
			renderModels(shadow_camera_frustum, layer_mask);
		}
		FrameBuffer::unbind();
		glCullFace(GL_BACK);
	}


	void renderScreenGeometry(Geometry* geometry, Material* material)
	{
		if (material->isReady())
		{
			ASSERT(m_renderer != NULL);
			Shader* shader = material->getShader();
			Matrix mtx;
			Renderer::getOrthoMatrix(-1, 1, -1, 1, 0, 30, &mtx);
			m_renderer->setProjectionMatrix(mtx);
			m_renderer->setViewMatrix(Matrix::IDENTITY);
			material->apply(*m_renderer, *this);
			m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX, Matrix::IDENTITY);
			m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::PROJECTION_MATRIX, mtx);
			m_renderer->renderGeometry(*geometry, 0, 6, *shader);
		}
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

	void renderGrass(int64_t layer_mask)
	{
		PROFILE_FUNCTION();
		if (m_active_camera.isValid())
		{
			const Material* last_material = NULL;
			m_grass_infos.clear();
			m_scene->getGrassInfos(m_grass_infos, layer_mask);
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
						m_renderer->setUniform(*shader, "shadowmap_matrix0", SHADOW_MATRIX0_HASH, m_shadow_modelviewprojection[0]);
						m_renderer->setUniform(*shader, "shadowmap_matrix1", SHADOW_MATRIX1_HASH, m_shadow_modelviewprojection[1]);
						m_renderer->setUniform(*shader, "shadowmap_matrix2", SHADOW_MATRIX2_HASH, m_shadow_modelviewprojection[2]);
						m_renderer->setUniform(*shader, "shadowmap_matrix3", SHADOW_MATRIX3_HASH, m_shadow_modelviewprojection[3]);
						m_renderer->setUniform(*shader, "light_dir", LIGHT_DIR_HASH, m_light_dir);
						m_renderer->setUniform(*shader, "camera_pos", CAMERA_POS_HASH, m_active_camera.entity.getPosition());
						Component light_cmp = m_scene->getLight(0);
						if(light_cmp.isValid())
						{
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::AMBIENT_COLOR, m_scene->getLightAmbientColor(light_cmp));
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::AMBIENT_INTENSITY, m_scene->getLightAmbientIntensity(light_cmp));
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getLightDiffuseColor(light_cmp));
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getLightDiffuseIntensity(light_cmp));
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
							m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
						}
						last_material = &material;
					}
					m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::GRASS_MATRICES, info.m_matrices, info.m_matrix_count);

					m_renderer->renderGeometry(*info.m_geometry, mesh.getStart(), mesh.getCount() / info.m_mesh_copy_count * info.m_matrix_count, *shader);
				}
			}
		}
	}

	void renderTerrains(int64_t layer_mask)
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
					m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX, world_matrix);
					m_renderer->setUniform(*shader, "shadowmap_matrix0", SHADOW_MATRIX0_HASH, m_shadow_modelviewprojection[0]);
					m_renderer->setUniform(*shader, "shadowmap_matrix1", SHADOW_MATRIX1_HASH, m_shadow_modelviewprojection[1]);
					m_renderer->setUniform(*shader, "shadowmap_matrix2", SHADOW_MATRIX2_HASH, m_shadow_modelviewprojection[2]);
					m_renderer->setUniform(*shader, "shadowmap_matrix3", SHADOW_MATRIX3_HASH, m_shadow_modelviewprojection[3]);
					Component light_cmp = m_scene->getLight(0);
					if(light_cmp.isValid())
					{
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::AMBIENT_COLOR, m_scene->getLightAmbientColor(light_cmp));
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::AMBIENT_INTENSITY, m_scene->getLightAmbientIntensity(light_cmp));
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getLightDiffuseColor(light_cmp));
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getLightDiffuseIntensity(light_cmp));
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
						m_renderer->setFixedCachedUniform(*shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
					}
					m_renderer->setUniform(*shader, "light_dir", LIGHT_DIR_HASH, m_light_dir);

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
				return (int)(info1->m_mesh - info2->m_mesh);
			});
		}
	}

	void renderModels(const Frustum& frustum, int64_t layer_mask)
	{
		PROFILE_FUNCTION();
		
		uint32_t pass_hash = getRenderer().getPass();
		m_renderable_infos.clear();
		m_scene->getRenderableInfos(frustum, m_renderable_infos, layer_mask);
		int count = m_renderable_infos.size();
		const Material* last_material = NULL;
		sortRenderables(m_renderable_infos);
		for (int i = 0; i < count; ++i)
		{
			const RenderableInfo& info = m_renderable_infos[i];
			const Matrix& world_matrix = info.m_model->getMatrix();
			const Mesh& mesh = *info.m_mesh;
			const Material& material = *mesh.getMaterial();
			Shader& shader = *material.getShader();
			
			if (last_material != &material)
			{
				if(!shader.hasPass(pass_hash))
				{
					continue;
				}
	
				material.apply(*m_renderer, *this);
				m_renderer->setUniform(shader, "shadowmap_matrix0", SHADOW_MATRIX0_HASH, m_shadow_modelviewprojection[0]);
				m_renderer->setUniform(shader, "shadowmap_matrix1", SHADOW_MATRIX1_HASH, m_shadow_modelviewprojection[1]);
				m_renderer->setUniform(shader, "shadowmap_matrix2", SHADOW_MATRIX2_HASH, m_shadow_modelviewprojection[2]);
				m_renderer->setUniform(shader, "shadowmap_matrix3", SHADOW_MATRIX3_HASH, m_shadow_modelviewprojection[3]);
				Component light_cmp = m_scene->getLight(0);
				if(light_cmp.isValid())
				{
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::AMBIENT_COLOR, m_scene->getLightAmbientColor(light_cmp));
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::AMBIENT_INTENSITY, m_scene->getLightAmbientIntensity(light_cmp));
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getLightDiffuseColor(light_cmp));
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getLightDiffuseIntensity(light_cmp));
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
					m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
				}
				m_renderer->setUniform(shader, "light_dir", LIGHT_DIR_HASH, m_light_dir);
				last_material = &material;
			}
			m_renderer->setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX, world_matrix);
			
			static Matrix bone_mtx[64];
			if (info.m_pose->getCount() > 0)
			{
				const Pose& pose = *info.m_pose;
				const Model& model = *info.m_model->getModel();
				Vec3* poss = pose.getPositions();
				Quat* rots = pose.getRotations();
				ASSERT(pose.getCount() <= 64);
				for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
				{
					rots[bone_index].toMatrix(bone_mtx[bone_index]);
					bone_mtx[bone_index].translate(poss[bone_index]);
					bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
				}
				m_renderer->setUniform(shader, "bone_matrices", BONE_MATRICES_HASH, bone_mtx, pose.getCount());
			}

			m_renderer->renderGeometry(*m_renderable_infos[i].m_geometry, mesh.getStart(), mesh.getCount(), *material.getShader());
		}
	}

	virtual void resize(int w, int h) override
	{
		m_width = w;
		m_height = h;
	}

	virtual void render() override
	{
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
	Map<uint32_t, CustomCommandHandler> m_custom_commands_handlers;
	Component m_active_camera;
	Array<TerrainInfo> m_terrain_infos;
	Array<GrassInfo> m_grass_infos;
	Array<RenderableInfo> m_renderable_infos;

	private:
		void operator=(const PipelineInstanceImpl&);
};


Pipeline::Pipeline(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
{
}


PipelineInstance* PipelineInstance::create(Pipeline& pipeline)
{
	return LUMIX_NEW(PipelineInstanceImpl)(pipeline);
}


void PipelineInstance::destroy(PipelineInstance* pipeline)
{
	LUMIX_DELETE(pipeline);
}


void PolygonModeCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_fill);
}


void PolygonModeCommand::execute(PipelineInstanceImpl& pipeline)
{
	glPolygonMode(GL_FRONT_AND_BACK, m_fill && !pipeline.getRenderer().isEditorWireframe() ? GL_FILL : GL_LINE);
}


void SetPassCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	char pass_name[50];
	serializer.deserializeArrayItem(pass_name, sizeof(pass_name));
	m_pass_hash = crc32(pass_name);
}


void SetPassCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.getRenderer().setPass(m_pass_hash);
}


void ClearCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	char tmp[256];
	serializer.deserializeArrayItem(tmp, 255);
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


void CustomCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	char tmp[256];
	serializer.deserializeArrayItem(tmp, 255);
	m_name = crc32(tmp);
}


void CustomCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.executeCustomCommand(m_name);
}



void RenderModelsCommand::deserialize(PipelineImpl&, ISerializer& serializer) 
{
	serializer.deserializeArrayItem(m_layer_mask);
}


void RenderModelsCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderTerrains(m_layer_mask);
	pipeline.renderModels(pipeline.getScene()->getFrustum(), m_layer_mask);
	pipeline.renderGrass(m_layer_mask);

}


void ApplyCameraCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_camera_slot);
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


void BindFramebufferCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_buffer_name);
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


void RenderDebugLinesCommand::deserialize(PipelineImpl&, ISerializer&)
{
}


void RenderDebugLinesCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderDebugLines();
}


DrawScreenQuadCommand::~DrawScreenQuadCommand()
{
	LUMIX_DELETE(m_geometry);
}


void DrawScreenQuadCommand::deserialize(PipelineImpl& pipeline, ISerializer& serializer)
{
	m_geometry = LUMIX_NEW(Geometry);
	VertexDef def;
	def.parse("pt", 2);
	Array<int> indices;
	const int GEOMETRY_VERTEX_ATTRIBUTE_COUNT = 20;
	float v[GEOMETRY_VERTEX_ATTRIBUTE_COUNT];
	indices.push(0);
	indices.push(1);
	indices.push(2);
	indices.push(0);
	indices.push(2);
	indices.push(3);
	
	for (int i = 0; i < GEOMETRY_VERTEX_ATTRIBUTE_COUNT; ++i)
	{
		serializer.deserializeArrayItem(v[i]);
	}

	uint8_t* data = (uint8_t*)v;
	m_geometry->copy(data, sizeof(v), indices, def);

	char material[LUMIX_MAX_PATH];
	serializer.deserializeArrayItem(material, LUMIX_MAX_PATH);
	m_material = static_cast<Material*>(pipeline.getResourceManager().get(ResourceManager::MATERIAL)->load(material));
}


void DrawScreenQuadCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderScreenGeometry(m_geometry, m_material);
}


void BindFramebufferTextureCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_framebuffer_name);
	char rb_name[31];
	serializer.deserializeArrayItem(rb_name, 30);
	if (stricmp(rb_name, "position") == 0)
	{
		m_renderbuffer_index = FrameBuffer::POSITION;
	}
	else if (stricmp(rb_name, "diffuse") == 0)
	{
		m_renderbuffer_index = FrameBuffer::DIFFUSE;
	}
	else if (stricmp(rb_name, "normal") == 0)
	{
		m_renderbuffer_index = FrameBuffer::NORMAL;
	}
	else if (stricmp(rb_name, "depth") == 0)
	{
		m_renderbuffer_index = FrameBuffer::DEPTH;
	}
	serializer.deserializeArrayItem(m_texture_uint);
}


void BindFramebufferTextureCommand::execute(PipelineInstanceImpl& pipeline)
{
	FrameBuffer* fb = pipeline.getFrameBuffer(m_framebuffer_name.c_str());
	if (fb)
	{
		glActiveTexture(GL_TEXTURE0 + m_texture_uint);
		glBindTexture(GL_TEXTURE_2D, fb->getTexture((FrameBuffer::RenderBuffers)m_renderbuffer_index));
	}
}


void RenderShadowmapCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_layer_mask);
	serializer.deserializeArrayItem(m_camera_slot);
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
	return LUMIX_NEW(PipelineImpl)(path, getOwner());
}


void PipelineManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(static_cast<PipelineImpl*>(&resource));
}


} // ~namespace Lumix
