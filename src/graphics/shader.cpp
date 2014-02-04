#include "graphics/shader.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/matrix.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"

namespace Lux
{


Shader::Shader()
{
	m_program_id = glCreateProgram();
}


void Shader::load(const char* path, FS::FileSystem& file_system)
{
	FS::ReadCallback cb;
	cb.bind<Shader, &Shader::loaded>(this);
	file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
}


void Shader::loaded(FS::IFile* file, bool success)
{
	if(success)
	{
		size_t size = file->size();
		char* buf = LUX_NEW_ARRAY(char, size);
		file->read(buf, size);
		
		char* end = strstr(buf, "//~VS");		
		ASSERT(end);
		int vs_len = end - buf;
		buf[vs_len-1] = 0;
		m_vertex_id = attach(GL_VERTEX_SHADER, buf, vs_len);
		m_fragment_id = attach(GL_FRAGMENT_SHADER, buf + vs_len, size - vs_len);
		glLinkProgram(m_program_id);
		m_vertex_attributes_ids[0] = glGetAttribLocation(m_program_id, "bone_weights");
		m_vertex_attributes_ids[1] = glGetAttribLocation(m_program_id, "bone_indices");

		LUX_DELETE_ARRAY(buf);
	}
	/// TODO close file somehow
}


Shader::~Shader()
{
	glDeleteProgram(m_program_id);
	glDeleteShader(m_vertex_id);
	glDeleteShader(m_fragment_id);
}


GLuint Shader::attach(GLenum type, const char* src, int length)
{
	GLuint id = glCreateShader(type);
	glShaderSource(id, 1, (const GLchar**)&src, &length);
	glCompileShader(id);
	glAttachShader(m_program_id, id);
	return id;
}


void Shader::apply()
{
	glUseProgram(m_program_id);
}


void Shader::applyVertexAttributes()
{
	glEnableVertexAttribArray(m_vertex_attributes_ids[0]);
	glEnableVertexAttribArray(m_vertex_attributes_ids[1]);
	glVertexAttribPointer(m_vertex_attributes_ids[0], 4, GL_FLOAT, GL_FALSE, 16 * sizeof(GLfloat), 0);
	glVertexAttribPointer(m_vertex_attributes_ids[1], 4, GL_INT, GL_FALSE, 16 * sizeof(GLint), (GLvoid*)(4 * sizeof(GLfloat)));
}


void Shader::disableVertexAttributes()
{
	glDisableVertexAttribArray(m_vertex_attributes_ids[0]);
	glDisableVertexAttribArray(m_vertex_attributes_ids[1]);
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
	glProgramUniformMatrix4fv(m_program_id, loc, count, false, &matrices[0].m11);
}


} // ~namespace Lux
