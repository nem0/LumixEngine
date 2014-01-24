#include "graphics/pipeline.h"
#include <Windows.h>
#include "core/array.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/iserializer.h"
#include "core/json_serializer.h"
#include "core/string.h"
#include "gl/GL.h"


namespace Lux
{


struct Command
{
	enum Type
	{
		CLEAR,

	};

	uint32_t m_type;
	uint32_t m_uint;
};


struct PipelineImpl : public Pipeline
{
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
				glDisable(GL_CULL_FACE);
				glBegin(GL_TRIANGLES);
					glColor3f(1, 1, 1);
					glVertex3f(0, 0, 5);
					glVertex3f(1, 0, 5);
					glVertex3f(1, 1, 5);
				glEnd();
				break;
			default:
				ASSERT(false);
				break;
		}
	}

	Array<Command> m_commands;
	string m_path;
};


Pipeline* Pipeline::create()
{
	return LUX_NEW(PipelineImpl);
}


void Pipeline::destroy(Pipeline* pipeline)
{
	LUX_DELETE(pipeline);
}


} // ~namespace Lux