#include "graphics/pipeline.h"
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include "core/array.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/iserializer.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/string.h"
#include "engine/engine.h"
#include "graphics/frame_buffer.h"
#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"


namespace Lux
{

struct PipelineImpl;
struct PipelineInstanceImpl;

struct Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) = 0;
	virtual void execute(PipelineInstanceImpl& pipeline) = 0;
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
	uint32_t m_camera_idx;
};


struct BindFramebufferCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	uint32_t m_buffer_index;
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


struct BindFramebufferTextureCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;
	uint32_t m_framebuffer_index;
	uint32_t m_renderbuffer_index;
	uint32_t m_texture_uint;
};


struct RenderShadowmapCommand : public Command
{
	virtual void deserialize(PipelineImpl& pipeline, ISerializer& serializer) override;
	virtual void execute(PipelineInstanceImpl& pipeline) override;

	int64_t m_layer_mask;
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
		return LUX_NEW(T);
	}


	PipelineImpl(const Path& path, ResourceManager& resource_manager)
		: Pipeline(path, resource_manager)
	{
		addCommandCreator("clear").bind<&CreateCommand<ClearCommand> >();
		addCommandCreator("render_models").bind<&CreateCommand<RenderModelsCommand> >();
		addCommandCreator("apply_camera").bind<&CreateCommand<ApplyCameraCommand> >();
		addCommandCreator("bind_framebuffer").bind<&CreateCommand<BindFramebufferCommand> >();
		addCommandCreator("unbind_framebuffer").bind<&CreateCommand<UnbindFramebufferCommand> >();
		addCommandCreator("draw_fullscreen_quad").bind<&CreateCommand<DrawFullscreenQuadCommand> >();
		addCommandCreator("bind_framebuffer_texture").bind<&CreateCommand<BindFramebufferTextureCommand> >();
		addCommandCreator("render_shadowmap").bind<&CreateCommand<RenderShadowmapCommand> >();
		addCommandCreator("bind_shadowmap").bind<&CreateCommand<BindShadowmapCommand> >();
	}


	~PipelineImpl()
	{
		for (int i = 0; i < m_commands.size(); ++i)
		{
			LUX_DELETE(m_commands[i]);
		}
	}


	virtual void doUnload(void) override
	{
		for (int i = 0; i < m_commands.size(); ++i)
		{
			LUX_DELETE(m_commands[i]);
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
		serializer.deserialize("frame_buffer_count", count);
		serializer.deserializeArrayBegin("frame_buffers");
		m_framebuffers.resize(count);
		for(int i = 0; i < count; ++i)
		{
			int32_t render_buffer_count;
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
		
		serializer.deserialize("command_count", count);
		serializer.deserializeArrayBegin("commands");
		for(int i = 0; i < count; ++i)
		{
			char tmp[255];
			serializer.deserializeArrayItem(tmp, 255);
			uint32_t command_type_hash = crc32(tmp);
			Command* cmd = createCommand(command_type_hash);
			cmd->deserialize(*this, serializer);
			m_commands.push(cmd);
		}
		serializer.deserializeArrayEnd();
		return true;
	}


	void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) 
	{
		if(success)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ);
			deserialize(serializer);
			onReady();
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
	{
		m_width = m_height = -1;
		m_shadowmap_framebuffer = NULL;
		if(pipeline.isReady())
		{
			sourceLoaded(Resource::State::READY);
		}
		else
		{
			pipeline.getObserverCb().bind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
		}
	}

	~PipelineInstanceImpl()
	{
		for (int i = 0; i < m_framebuffers.size(); ++i)
		{
			LUX_DELETE(m_framebuffers[i]);
		}
		if (m_shadowmap_framebuffer)
		{
			LUX_DELETE(m_shadowmap_framebuffer);
		}
	}


	void setRenderer(Renderer& renderer)
	{
		m_renderer = &renderer;
	}


	void sourceLoaded(Resource::State status)
	{
		if (status == Resource::State::READY)
		{
			PipelineImpl::FrameBufferDeclaration fb = m_source.m_shadowmap_framebuffer;
			m_shadowmap_framebuffer = LUX_NEW(FrameBuffer)(fb.m_width, fb.m_height, fb.m_mask);
			m_framebuffers.reserve(m_source.m_framebuffers.size());
			for (int i = 0; i < m_source.m_framebuffers.size(); ++i)
			{
				fb = m_source.m_framebuffers[i];
				m_framebuffers.push(LUX_NEW(FrameBuffer)(fb.m_width, fb.m_height, fb.m_mask));
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

	void renderShadowmap(int64_t layer_mask)
	{
		ASSERT(m_renderer != NULL);
		glViewport(0, 0, m_shadowmap_framebuffer->getWidth(), m_shadowmap_framebuffer->getHeight());
		Component light_cmp = m_renderer->getLight(0);
		if (!light_cmp.isValid())
		{
			return;
		}
		glEnable(GL_CULL_FACE);
		m_shadowmap_framebuffer->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		Matrix projection_matrix;
		getOrthoMatrix(-10, 10, -10, 10, 0, 100, &projection_matrix);
		glMultMatrixf(&projection_matrix.m11);

		Matrix mtx = light_cmp.entity.getMatrix();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		Matrix modelview_matrix;
		getLookAtMatrix(mtx.getTranslation(), mtx.getTranslation() + mtx.getZVector(), mtx.getYVector(), &modelview_matrix);
		glMultMatrixf(&modelview_matrix.m11);
		static const Matrix biasMatrix(
			0.5, 0.0, 0.0, 0.0,
			0.0, 0.5, 0.0, 0.0,
			0.0, 0.0, 0.5, 0.0,
			0.5, 0.5, 0.5, 1.0
			);
		m_shadow_modelviewprojection = biasMatrix * (projection_matrix * modelview_matrix);

		renderModels(layer_mask);

		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		FrameBuffer::unbind();
		glDisable(GL_CULL_FACE);
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
		material->apply(*m_renderer);
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_CULL_FACE);
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


	void renderModels(int64_t layer_mask)
	{
		ASSERT(m_renderer != NULL);
		static Array<RenderableInfo> infos;
		infos.clear();
		m_renderer->getRenderableInfos(infos, layer_mask);
		int count = infos.size();
		for (int i = 0; i < count; ++i)
		{
			glPushMatrix();
			Model& model = infos[i].m_model_instance->getModel();
			Matrix mtx = infos[i].m_model_instance->getMatrix();
			mtx.multiply3x3(infos[i].m_scale);
			glMultMatrixf(&mtx.m11);
			Geometry* geom = model.getGeometry();
			for (int j = 0; j < model.getMeshCount(); ++j)
			{
				Mesh& mesh = model.getMesh(j);
				if (mesh.getMaterial()->isReady())
				{
					mesh.getMaterial()->apply(*m_renderer);

					static Matrix bone_mtx[64];
					Pose& pose = infos[i].m_model_instance->getPose();
					Vec3* poss = pose.getPositions();
					Quat* rots = pose.getRotations();
					for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
					{
						rots[bone_index].toMatrix(bone_mtx[bone_index]);
						bone_mtx[bone_index].translate(poss[bone_index]);
						bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
					}
					mesh.getMaterial()->getShader()->setUniform("bone_matrices", bone_mtx, pose.getCount());
					mesh.getMaterial()->getShader()->setUniform("shadowmap", 1);
					mesh.getMaterial()->getShader()->setUniform("shadowmap_matrix", m_shadow_modelviewprojection);
					mesh.getMaterial()->getShader()->setUniform("world_matrix", infos[i].m_model_instance->getMatrix());
					geom->draw(mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
				}
			}
			glPopMatrix();
		}
	}


	virtual void clearCameras() override
	{
		m_cameras.clear();
	}


	virtual int getCameraCount() const override
	{
		return m_cameras.size();
	}


	virtual void resize(int w, int h) override
	{
		m_width = w;
		m_height = h;
		ASSERT(m_renderer != NULL);
		for (int i = 0; i < m_cameras.size(); ++i)
		{
			m_renderer->setCameraSize(m_cameras[i], w, h);
		}
	}

	virtual void addCamera(const Component& camera) override
	{
		int priority;
		m_renderer->setCameraSize(camera, m_width, m_height);
		m_renderer->getCameraPriority(camera, priority);
		if (m_cameras.empty())
		{
			m_cameras.push(camera);
			return;
		}
		m_cameras.pushEmpty();
		for (int i = m_cameras.size() - 2; i >= 0; --i)
		{
			int existing_priority;
			m_renderer->getCameraPriority(m_cameras[i], existing_priority);
			if (existing_priority < priority)
			{
				m_cameras[i + 1] = m_cameras[i];
				m_cameras[i] = camera;
			}
			else
			{
				m_cameras[i + 1] = camera;
				break;
			}
		}
	}


	virtual void removeCamera(const Component& camera) override
	{
		for (int i = 0, c = m_cameras.size(); i < c; ++i)
		{
			if (m_cameras[i].index == camera.index)
			{
				m_cameras.erase(i);
				return;
			}
		}
	}


	virtual const Component& getCamera(int index) override
	{
		return m_cameras[index];
	}


	virtual void render() override
	{
		for (int i = 0; i < m_source.m_commands.size(); ++i)
		{
			m_source.m_commands[i]->execute(*this);
		}
	}

	PipelineImpl& m_source;
	Array<FrameBuffer*> m_framebuffers;
	FrameBuffer* m_shadowmap_framebuffer;
	Matrix m_shadow_modelviewprojection;
	Array<Component> m_cameras;
	Renderer* m_renderer;
	int m_width;
	int m_height;

	private:
		void operator=(const PipelineInstanceImpl&);
};


Pipeline::Pipeline(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
{
}


PipelineInstance* PipelineInstance::create(Pipeline& pipeline)
{
	return LUX_NEW(PipelineInstanceImpl)(pipeline);
}


void PipelineInstance::destroy(PipelineInstance* pipeline)
{
	LUX_DELETE(pipeline);
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
	serializer.deserializeArrayItem(m_camera_idx);
}


void ApplyCameraCommand::execute(PipelineInstanceImpl& pipeline)
{
	ASSERT(pipeline.m_renderer != NULL);
	pipeline.m_renderer->applyCamera(pipeline.m_cameras[m_camera_idx]);
}


void BindFramebufferCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_buffer_index);
}


void BindFramebufferCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.m_framebuffers[m_buffer_index]->bind();
}

	
void UnbindFramebufferCommand::execute(PipelineInstanceImpl&)
{
	FrameBuffer::unbind();
}


void DrawFullscreenQuadCommand::deserialize(PipelineImpl& pipeline, ISerializer& serializer)
{
	const int MATERIAL_NAME_MAX_LENGTH = 100;
	char material[MATERIAL_NAME_MAX_LENGTH];
	serializer.deserializeArrayItem(material, MATERIAL_NAME_MAX_LENGTH);
	char material_path[MAX_PATH];
	strcpy(material_path, "materials/");
	strcat(material_path, material);
	strcat(material_path, ".mat");
	m_material = static_cast<Material*>(pipeline.getResourceManager().get(ResourceManager::MATERIAL)->load(material_path));
}


void DrawFullscreenQuadCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.drawFullscreenQuad(m_material);
}


void BindFramebufferTextureCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	/// TODO map names to indices
	serializer.deserializeArrayItem(m_framebuffer_index);
	serializer.deserializeArrayItem(m_renderbuffer_index);
	serializer.deserializeArrayItem(m_texture_uint);
}


void BindFramebufferTextureCommand::execute(PipelineInstanceImpl& pipeline)
{
	glActiveTexture(GL_TEXTURE0 + m_texture_uint);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, pipeline.m_framebuffers[m_framebuffer_index]->getTexture((FrameBuffer::RenderBuffers)m_renderbuffer_index));
}


void RenderShadowmapCommand::deserialize(PipelineImpl&, ISerializer& serializer)
{
	serializer.deserializeArrayItem(m_layer_mask);
}


void RenderShadowmapCommand::execute(PipelineInstanceImpl& pipeline)
{
	pipeline.renderShadowmap(m_layer_mask);
}


void BindShadowmapCommand::execute(PipelineInstanceImpl& pipeline)
{
	glActiveTexture(GL_TEXTURE0 + 1);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, pipeline.m_shadowmap_framebuffer->getDepthTexture());
}


Resource* PipelineManager::createResource(const Path& path)
{
	return LUX_NEW(PipelineImpl)(path, getOwner());
}


void PipelineManager::destroyResource(Resource& resource)
{
	LUX_DELETE(static_cast<PipelineImpl*>(&resource));
}


} // ~namespace Lux