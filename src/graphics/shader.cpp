#include "graphics/shader.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include "graphics/shader_manager.h"


namespace Lux
{


Shader::Shader(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
	, m_vertex_id()
	, m_fragment_id()
{
	m_program_id = glCreateProgram();
}

Shader::~Shader()
{
	glDeleteProgram(m_program_id);
	glDeleteShader(m_vertex_id);
	glDeleteShader(m_fragment_id);
}

void Shader::apply()
{
	glUseProgram(m_program_id);
}	


void Shader::setUniform(const char* name, int value)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	if(loc >= 0)
	{
		glProgramUniform1i(m_program_id, loc, value);
	}
}


void Shader::setUniform(const char* name, const Vec3& value)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	if(loc >= 0)
	{
		glProgramUniform3f(m_program_id, loc, value.x, value.y, value.z);
	}
}


void Shader::setUniform(const char* name, GLfloat value)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	if(loc >= 0)
	{
		glProgramUniform1f(m_program_id, loc, value);
	}
}


void Shader::setUniform(const char* name, const Matrix& mtx)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	if(loc >= 0)
	{
		glProgramUniformMatrix4fv(m_program_id, loc, 1, false, &mtx.m11);
	}
}

void Shader::setUniform(const char* name, const Matrix* matrices, int count)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	if(loc >= 0) // this is here because of bug in some gl implementations
	{
		glProgramUniformMatrix4fv(m_program_id, loc, count, false, &matrices[0].m11);
	}
}

GLuint Shader::attach(GLenum type, const char* src, int32_t length)
{
	GLuint id = glCreateShader(type);
	glShaderSource(id, 1, (const GLchar**)&src, &length);
	glCompileShader(id);
	glAttachShader(m_program_id, id);
	return id;
}

void Shader::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		int32_t size = (int32_t)file->size();
		ShaderManager* manager = static_cast<ShaderManager*>(getResourceManager().get(ResourceManager::SHADER));
		char* buf = manager->getBuffer(size);
		file->read(buf, size);
		
		char* end = strstr(buf, "//~VS");		
		if (!end)
		{
			g_log_error.log("renderer", "Could not process shader file %s", m_path.c_str());
			onFailure();
			return;
		}
		int32_t vs_len = (int32_t)(end - buf);
		buf[vs_len-1] = 0;
		m_vertex_id = attach(GL_VERTEX_SHADER, buf, vs_len);
		m_fragment_id = attach(GL_FRAGMENT_SHADER, buf + vs_len, size - vs_len);
		glLinkProgram(m_program_id);
		m_vertex_attributes_ids[0] = glGetAttribLocation(m_program_id, "bone_weights");
		m_vertex_attributes_ids[1] = glGetAttribLocation(m_program_id, "bone_indices");


		m_size = file->size();
		decrementDepCount();
	}
	else
	{
		onFailure();
	}

	fs.close(file);
}

void Shader::doUnload(void)
{
	glDeleteProgram(m_program_id);
	glDeleteShader(m_vertex_id);
	glDeleteShader(m_fragment_id);
	m_program_id = glCreateProgram();
	m_vertex_id = 0;
	m_fragment_id = 0;

	m_size = 0;
	onEmpty();
}

FS::ReadCallback Shader::getReadCallback()
{
	FS::ReadCallback cb;
	cb.bind<Shader, &Shader::loaded>(this);
	return cb;
}


} // ~namespace Lux
