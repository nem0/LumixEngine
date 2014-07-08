#include "graphics/pipeline.h"
#include "graphics/gl_ext.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/iserializer.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/map.h"
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


struct DrawFullscreenQuadCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	Material* m_material;
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
		addCommandCreator("draw_fullscreen_quad").bind<&CreateCommand<DrawFullscreenQuadCommand> >();
		addCommandCreator("bind_framebuffer_texture").bind<&CreateCommand<BindFramebufferTextureCommand> >();
		addCommandCreator("render_shadowmap").bind<&CreateCommand<RenderShadowmapCommand> >();
		addCommandCreator("bind_shadowmap").bind<&CreateCommand<BindShadowmapCommand> >();
		addCommandCreator("render_debug_lines").bind<&CreateCommand<RenderDebugLinesCommand> >();
		addCommandCreator("polygon_mode").bind<&CreateCommand<PolygonModeCommand> >();
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
				g_log_error.log("renderer") << "Unknown pipeline command " << tmp;
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


	void setRenderer(Renderer& renderer)
	{
		m_renderer = &renderer;
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


	void getOrthoMatrix(float left, float right, float bottom, float top, float z_near, float z_far, Matrix* mtx)
	{
		*mtx = Matrix::IDENTITY;
		mtx->m11 = 2 / (right - left);
		mtx->m22 = 2 / (top - bottom);
		mtx->m33 = -2 / (z_far - z_near);
		mtx->m41 = -(right + left) / (right - left);
		mtx->m42 = -(top + bottom) / (top - bottom);
		mtx->m43 = -(z_far + z_near) / (z_far - z_near);
		/*		glOrtho(left, right, bottom, top, z_near, z_far);
		glGetFloatv(GL_PROJECTION_MATRIX, &mtx->m11);
		*/
	}


	void getLookAtMatrix(const Vec3& pos, const Vec3& center, const Vec3& up, Matrix* mtx)
	{
		*mtx = Matrix::IDENTITY;
		Vec3 f = center - pos;
		f.normalize();
		Vec3 r = crossProduct(f, up);
		r.normalize();
		Vec3 u = crossProduct(r, f);
		mtx->setXVector(r);
		mtx->setYVector(u);
		mtx->setZVector(-f);
		mtx->transpose();
		mtx->setTranslation(Vec3(-dotProduct(r, pos), -dotProduct(u, pos), dotProduct(f, pos)));
		/*glPushMatrix();
		float m[16];
		gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
		glGetFloatv(GL_MODELVIEW_MATRIX, m);
		glPopMatrix();*/
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
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		
		for(int split_index = 0; split_index < 4; ++split_index)
		{
			glViewport(1, (GLint)(split_index * m_shadowmap_framebuffer->getHeight() * 0.25f), m_shadowmap_framebuffer->getWidth() - 2, (GLsizei)(m_shadowmap_framebuffer->getHeight() * 0.25f));
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			Matrix projection_matrix;
			float bb_size = (float)(2 * ((split_index + 1) * (split_index + 1) * (split_index + 1)));
			getOrthoMatrix(-bb_size, bb_size, -bb_size, bb_size, 0, 100, &projection_matrix);
			glMultMatrixf(&projection_matrix.m11);

			Matrix light_mtx = light_cmp.entity.getMatrix();
			m_light_dir = light_mtx.getZVector();
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			Matrix modelview_matrix;
			Vec3 pos = camera.entity.getPosition();
			Vec3 cam_forward = camera.entity.getRotation() * Vec3(0, 0, 1);
			pos -= light_mtx.getZVector() * 10;
			Vec3 up = camera.entity.getMatrix().getXVector();
			getLookAtMatrix(pos, pos + light_mtx.getZVector(), light_mtx.getYVector(), &modelview_matrix);
			glMultMatrixf(&modelview_matrix.m11);
			static const Matrix biasMatrix(
				0.5, 0.0, 0.0, 0.0,
				0.0, 0.5, 0.0, 0.0,
				0.0, 0.0, 0.5, 0.0,
				0.5, 0.5, 0.5, 1.0
				);
			m_shadow_modelviewprojection[split_index] = biasMatrix * (projection_matrix * modelview_matrix);

			renderModels(layer_mask);
		}
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		FrameBuffer::unbind();
		glCullFace(GL_BACK);
	}


	void drawFullscreenQuad(Material* material)
	{
		glDisable(GL_DEPTH_TEST);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(-1, 1, -1, 1, 0, 30);
		glMatrixMode(GL_MODELVIEW);

		ASSERT(m_renderer != NULL);
		material->apply(*m_renderer, *this);
		glPushMatrix();
		glLoadIdentity();
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0);
		glVertex3f(-1, -1, -1);
		glTexCoord2f(0, 1);
		glVertex3f(-1, 1, -1);
		glTexCoord2f(1, 1);
		glVertex3f(1, 1, -1);
		glTexCoord2f(1, 0);
		glVertex3f(1, -1, -1);
		glEnd();
		glPopMatrix();
		glEnable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
	}


	void renderDebugLines()
	{
		// cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
		for (int i = 0; i < 16; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glDisable(GL_TEXTURE_2D);
		}
		glActiveTexture(GL_TEXTURE0); 

		const Array<DebugLine>& lines = m_scene->getDebugLines();
		glBegin(GL_LINES);
		for (int i = 0, c = lines.size(); i < c; ++i)
		{
			glColor3fv(&lines[i].m_color.x);
			glVertex3fv(&lines[i].m_from.x);
			glVertex3fv(&lines[i].m_to.x);
		}
		glEnd();
	}

	void renderModels(int64_t layer_mask)
	{
		ASSERT(m_renderer != NULL);
		static Array<RenderableInfo> infos;
		infos.clear();
		m_scene->getRenderableInfos(infos, layer_mask);
		int count = infos.size();
		Material* last_material = NULL;
		for (int i = 0; i < count; ++i)
		{
			glPushMatrix();
			Matrix world_matrix = infos[i].m_model ? infos[i].m_model->getMatrix() : *infos[i].m_matrix;
			world_matrix.multiply3x3(infos[i].m_scale);
			glMultMatrixf(&world_matrix.m11);
			
			Mesh& mesh = *infos[i].m_mesh;
			Material& material = *mesh.getMaterial();
			if (last_material != &material)
			{
				material.apply(*m_renderer, *this);
				material.getShader()->setUniform("light_dir", m_light_dir);
				material.getShader()->setUniform("shadowmap_matrix0", m_shadow_modelviewprojection[0]);
				material.getShader()->setUniform("shadowmap_matrix1", m_shadow_modelviewprojection[1]);
				material.getShader()->setUniform("shadowmap_matrix2", m_shadow_modelviewprojection[2]);
				material.getShader()->setUniform("shadowmap_matrix3", m_shadow_modelviewprojection[3]);
				material.getShader()->setUniform("world_matrix", world_matrix);
				last_material = &material;
			}
			static Matrix bone_mtx[64];
			if (infos[i].m_pose)
			{
				const Pose& pose = *infos[i].m_pose;
				const Model& model = *infos[i].m_model->getModel();
				Vec3* poss = pose.getPositions();
				Quat* rots = pose.getRotations();
				for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
				{
					rots[bone_index].toMatrix(bone_mtx[bone_index]);
					bone_mtx[bone_index].translate(poss[bone_index]);
					bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
				}
				material.getShader()->setUniform("bone_matrices", bone_mtx, pose.getCount());
			}

			infos[i].m_geometry->draw(mesh.getStart(), mesh.getCount(), *material.getShader());
			glPopMatrix();
		}
		if (m_active_camera.isValid())
		{
			static Array<TerrainInfo> terrain_infos;
			terrain_infos.clear();
			m_scene->getTerrainInfos(terrain_infos, layer_mask);
			Vec3 camera_position = m_active_camera.entity.getPosition();
			for (int i = 0; i < terrain_infos.size(); ++i)
			{
				m_scene->renderTerrain(terrain_infos[i], *m_renderer, *this, camera_position);
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
	int m_width;
	int m_height;
	Map<uint32_t, CustomCommandHandler> m_custom_commands_handlers;
	Component m_active_camera;

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


void PolygonModeCommand::execute(PipelineInstanceImpl&)
{
	glPolygonMode(GL_FRONT_AND_BACK, m_fill ? GL_FILL : GL_LINE);
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
	pipeline.renderModels(m_layer_mask);
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


void DrawFullscreenQuadCommand::deserialize(PipelineImpl& pipeline, ISerializer& serializer)
{
	const int MATERIAL_NAME_MAX_LENGTH = 100;
	char material[MATERIAL_NAME_MAX_LENGTH];
	serializer.deserializeArrayItem(material, MATERIAL_NAME_MAX_LENGTH);
	base_string<char, StackAllocator<LUMIX_MAX_PATH> > material_path;
	material_path = "materials/";
	material_path += material;
	material_path += ".mat";
	m_material = static_cast<Material*>(pipeline.getResourceManager().get(ResourceManager::MATERIAL)->load(material_path.c_str()));
}


void DrawFullscreenQuadCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.drawFullscreenQuad(m_material);
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
		glEnable(GL_TEXTURE_2D);
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
	glActiveTexture(GL_TEXTURE0 + 1);
	glEnable(GL_TEXTURE_2D);
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
