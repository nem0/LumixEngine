#include "graphics/shader.h"
#include "core/matrix.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include <cstdio>

namespace Lux
{


Shader::Shader(const char* vertex, const char* fragment)
{
	m_program_id = glCreateProgram();
	m_vertex_id = attach(GL_VERTEX_SHADER, vertex);
	m_fragment_id = attach(GL_FRAGMENT_SHADER, fragment);
	glLinkProgram(m_program_id);
	m_vertex_attributes_ids[0] = glGetAttribLocation(m_program_id, "bone_weights");
	m_vertex_attributes_ids[1] = glGetAttribLocation(m_program_id, "bone_indices");
}


Shader::~Shader()
{
	glDeleteProgram(m_program_id);
	glDeleteShader(m_vertex_id);
	glDeleteShader(m_fragment_id);
}


GLuint Shader::attach(GLenum type, const char* shader)
{
	GLuint id = glCreateShader(type);
	FILE* fp = 0;
	fopen_s(&fp, shader, "rb");
	fseek(fp, 0, SEEK_END);
	GLint length = ftell(fp);
	GLchar* src = LUX_NEW_ARRAY(GLchar, length+1);
	fseek(fp, 0, SEEK_SET);
	fread(src, 1, length, fp);
	src[length] = 0;
	glShaderSource(id, 1, (const GLchar**)&src, &length);
	glCompileShader(id);
	glAttachShader(m_program_id, id);
	LUX_DELETE_ARRAY(src);
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
