#include "graphics/pipeline.h"
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include "core/array.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/input_system.h"
#include "core/iserializer.h"
#include "core/json_serializer.h"
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

	
struct Command
{
	enum Type
	{
		CLEAR,
		RENDER_MODELS,
		APPLY_CAMERA,
		BIND_FRAMEBUFFER,
		UNBIND_FRAMEBUFFER,
		DRAW_FULLSCREEN_QUAD,
		BIND_FRAMEBUFFER_TEXTURE,
		RENDER_SHADOWMAP,
		BIND_SHADOWMAP
	};

	uint32_t m_type;
	uint32_t m_uint;
	uint32_t m_uint2;
	uint32_t m_uint3;
	Material* m_material;
};


struct PipelineImpl : public Pipeline
{
	PipelineImpl(Renderer& renderer)
		: m_renderer(renderer)
	{
	}


	~PipelineImpl()
	{
		/// TODO
	}


	virtual int getCameraCount() const LUX_OVERRIDE
	{
		return m_cameras.size();
	}


	virtual void setCamera(int index, const Component& camera) LUX_OVERRIDE
	{
		if(m_cameras.size() <= index)
		{
			m_cameras.resize(index + 1);
		}
		m_cameras[index] = camera;
	}


	virtual const Component& getCamera(int index) LUX_OVERRIDE
	{
		return m_cameras[index];
	}


	virtual void render() LUX_OVERRIDE
	{
		for(int i = 0; i < m_commands.size(); ++i)
		{
			executeCommand(m_commands[i]);
		}
	}


	virtual bool deserialize(ISerializer& serializer) LUX_OVERRIDE
	{
		int32_t count;
		serializer.deserialize("frame_buffer_count", count);
		serializer.deserializeArrayBegin("frame_buffers");
		m_framebuffers.resize(count);
		for(int i = 0; i < count; ++i)
		{
			/// TODO framebuffer resolution
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
			/// TODO framebuffer resolution
			m_framebuffers[i] = LUX_NEW(FrameBuffer)(800, 600, mask);
		}
		m_shadowmap_framebuffer = LUX_NEW(FrameBuffer)(800, 600, FrameBuffer::DEPTH_BIT);
		serializer.deserialize("command_count", count);
		serializer.deserializeArrayBegin("commands");
		for(int i = 0; i < count; ++i)
		{
			Command& cmd = m_commands.pushEmpty();
			char tmp[255];
			serializer.deserializeArrayItem(tmp, 255);
			/// TODO do this in a normal way
			if(strcmp(tmp, "clear") == 0)
			{
				cmd.m_type = Command::CLEAR;
				cmd.m_uint = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
			}
			else if(strcmp(tmp, "render_models") == 0)
			{
				cmd.m_type = Command::RENDER_MODELS;
				cmd.m_uint = 0;
			}
			else if(strcmp(tmp, "apply_camera") == 0)
			{
				cmd.m_type = Command::APPLY_CAMERA;
				serializer.deserializeArrayItem(cmd.m_uint);
			}
			else if(strcmp(tmp, "bind_framebuffer") == 0)
			{
				cmd.m_type = Command::BIND_FRAMEBUFFER;
				serializer.deserializeArrayItem(cmd.m_uint);
				ASSERT(cmd.m_uint < (uint32_t)m_framebuffers.size());
			}
			else if(strcmp(tmp, "unbind_framebuffer") == 0)
			{
				cmd.m_type = Command::UNBIND_FRAMEBUFFER;
			}
			else if(strcmp(tmp, "draw_fullscreen_quad") == 0)
			{
				cmd.m_type = Command::DRAW_FULLSCREEN_QUAD;
				char material[100];
				serializer.deserializeArrayItem(material, 100);
				char material_path[MAX_PATH];
				strcpy(material_path, "materials/");
				strcat(material_path, material);
				strcat(material_path, ".mat");
				cmd.m_material = m_renderer.loadMaterial(material_path);
			}
			else if(strcmp(tmp, "bind_framebuffer_texture") == 0)
			{
				cmd.m_type = Command::BIND_FRAMEBUFFER_TEXTURE;
				/// TODO map names to indices
				serializer.deserializeArrayItem(cmd.m_uint); // framebuffer index
				serializer.deserializeArrayItem(cmd.m_uint2); // renderbuffer index
				serializer.deserializeArrayItem(cmd.m_uint3); // texture unit
			}
			else if(strcmp(tmp, "render_shadowmap") == 0)
			{
				cmd.m_type = Command::RENDER_SHADOWMAP;
			}
			else if(strcmp(tmp, "bind_shadowmap") == 0)
			{
				cmd.m_type = Command::BIND_SHADOWMAP;
			}
		}
		serializer.deserializeArrayEnd();
		return true;
	}


	virtual void load(const char* path, FS::FileSystem& file_system) 
	{
		m_path = path;
		FS::ReadCallback cb;
		cb.bind<PipelineImpl, &PipelineImpl::loaded>(this);
		file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
	}


	virtual const char* getPath() 
	{
		return m_path.c_str();
	}


	void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) 
	{
		if(success)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ);
			deserialize(serializer);
		}

		fs.close(file);
	}

	void executeCommand(const Command& command)
	{
		switch(command.m_type)
		{
			case Command::CLEAR:
				glClear(command.m_uint);
				break;
			case Command::RENDER_MODELS:
				renderModels();
				break;
			case Command::APPLY_CAMERA:
				m_renderer.applyCamera(m_cameras[command.m_uint]);
				break;
			case Command::BIND_FRAMEBUFFER:
				m_framebuffers[command.m_uint]->bind();
				break;
			case Command::UNBIND_FRAMEBUFFER:
				FrameBuffer::unbind();
				break;
			case Command::DRAW_FULLSCREEN_QUAD:
				drawFullscreenQuad(command.m_material);
				break;
			case Command::BIND_SHADOWMAP:
				glActiveTexture(GL_TEXTURE0 + 1);
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, m_shadowmap_framebuffer->getDepthTexture());
				break;
			case Command::BIND_FRAMEBUFFER_TEXTURE:
				glActiveTexture(GL_TEXTURE0 + command.m_uint3);
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, m_framebuffers[command.m_uint]->getTexture((FrameBuffer::RenderBuffers)command.m_uint2));
				break;
			case Command::RENDER_SHADOWMAP:
				renderShadowmap();
				break;
			default:
				ASSERT(false);
				break;
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
		mtx->setTranslation(Vec3(dotProduct(r, pos), dotProduct(-u, pos), dotProduct(f, pos)));
/*		gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
		glGetFloatv(GL_MODELVIEW_MATRIX, &mtx->m11);
*/		
	}

	void renderShadowmap()
	{
		glEnable(GL_CULL_FACE);
		m_shadowmap_framebuffer->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		Component light_cmp = m_renderer.getLight(0);
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

		renderModels();

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

		material->apply();
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


	void renderModels()
	{
		/// TODO clean this and optimize
		static PODArray<RenderableInfo> infos;
		infos.clear();
		m_renderer.getRenderableInfos(infos);
		int count = infos.size();
		for(int i = 0; i < count; ++i)
		{
			glPushMatrix();
			Model& model = infos[i].m_model_instance->getModel();
			glMultMatrixf(&infos[i].m_model_instance->getMatrix().m11);
			Geometry* geom = model.getGeometry();
			for(int j = 0; j < model.getMeshCount(); ++j)
			{
				Mesh& mesh = model.getMesh(j);
				if(mesh.getMaterial()->isReady())
				{
					mesh.getMaterial()->apply();

					static Matrix bone_mtx[64];
					Pose& pose = infos[i].m_model_instance->getPose();
					Vec3* poss = pose.getPositions();
					Quat* rots = pose.getRotations();
					for(int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
					{
						rots[bone_index].toMatrix(bone_mtx[bone_index]);
						bone_mtx[bone_index].translate(poss[bone_index]);
						bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
					}
					/// TODO do not hardcode following
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

	Array<Command> m_commands;
	string m_path;
	Renderer& m_renderer;
	PODArray<Component> m_cameras;
	PODArray<FrameBuffer*> m_framebuffers;
	FrameBuffer* m_shadowmap_framebuffer;
	Matrix m_shadow_modelviewprojection;
};


Pipeline* Pipeline::create(Renderer& renderer)
{
	return LUX_NEW(PipelineImpl)(renderer);
}


void Pipeline::destroy(Pipeline* pipeline)
{
	LUX_DELETE(pipeline);
}


} // ~namespace Lux