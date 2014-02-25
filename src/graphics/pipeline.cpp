#include "graphics/pipeline.h"
#include <Windows.h>
#include "core/array.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/input_system.h"
#include "core/iserializer.h"
#include "core/json_serializer.h"
#include "core/string.h"
#include "gl/GL.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
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
	};

	uint32_t m_type;
	uint32_t m_uint;
};


struct PipelineImpl : public Pipeline
{
	PipelineImpl(Renderer& renderer)
		: m_renderer(renderer)
	{
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
		serializer.deserialize("command_count", count);
		serializer.deserializeArrayBegin("commands");
		for(int i = 0; i < count; ++i)
		{
			Command& cmd = m_commands.pushEmpty();
			char tmp[255];
			serializer.deserializeArrayItem(tmp, 255);
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


	void loaded(FS::IFile* file, bool success) 
	{
		if(success)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ);
			deserialize(serializer);
		}
		// TODO close file somehow
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
			default:
				ASSERT(false);
				break;
		}
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
					/// TODO do not hardcode "bone_matrices" 
					mesh.getMaterial()->getShader()->setUniform("bone_matrices", bone_mtx, pose.getCount());
					geom->draw(mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
				}
			}
			glPopMatrix();
		}
	}

	Array<Command> m_commands;
	string m_path;
	Renderer& m_renderer;
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