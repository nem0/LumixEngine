#include "graphics/shader.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/matrix.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"

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
	glProgramUniform1i(m_program_id, loc, value);
}


void Shader::setUniform(const char* name, const Vec3& value)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	glProgramUniform3f(m_program_id, loc, value.x, value.y, value.z);
}


void Shader::setUniform(const char* name, GLfloat value)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	glProgramUniform1f(m_program_id, loc, value);
}


void Shader::setUniform(const char* name, const Matrix& mtx)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	glProgramUniformMatrix4fv(m_program_id, loc, 1, false, &mtx.m11);
}

void Shader::setUniform(const char* name, const Matrix* matrices, int count)
{
	GLint loc = glGetUniformLocation(m_program_id, name);
	if(loc != -1)
		glProgramUniformMatrix4fv(m_program_id, loc, count, false, &matrices[0].m11);
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
		TODO("Use here some shared buffer")
		char* buf = LUX_NEW_ARRAY(char, size);
		file->read(buf, size);
		
		char* end = strstr(buf, "//~VS");		
		ASSERT(end);
		int32_t vs_len = (int32_t)(end - buf);
		buf[vs_len-1] = 0;
		m_vertex_id = attach(GL_VERTEX_SHADER, buf, vs_len);
		m_fragment_id = attach(GL_FRAGMENT_SHADER, buf + vs_len, size - vs_len);
		glLinkProgram(m_program_id);
		m_vertex_attributes_ids[0] = glGetAttribLocation(m_program_id, "bone_weights");
		m_vertex_attributes_ids[1] = glGetAttribLocation(m_program_id, "bone_indices");

		LUX_DELETE_ARRAY(buf);

		onReady();
	}

	fs.close(file);
}

void Shader::doUnload(void)
{
	TODO("Implement Shader Unload");
}

void Shader::doReload(void)
{
	TODO("Implement Shader Reload");
}

FS::ReadCallback Shader::getReadCallback()
{
	FS::ReadCallback cb;
	cb.bind<Shader, &Shader::loaded>(this);
	return cb;
}


} // ~namespace Lux
