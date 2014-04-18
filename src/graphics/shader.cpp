#include "graphics/shader.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
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
	, m_is_shadowmap_required(true)
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
		JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str());
		serializer.deserializeObjectBegin();
		char attributes[MAX_ATTRIBUTE_COUNT][31];
		int attribute_count = 0;
		while (!serializer.isObjectEnd())
		{
			char label[256];
			serializer.deserializeLabel(label, 255);
			if (strcmp(label, "attributes") == 0)
			{
				serializer.deserializeArrayBegin();
				while (!serializer.isArrayEnd())
				{
					serializer.deserializeArrayItem(attributes[attribute_count], 30);
					++attribute_count;
					if (attribute_count == MAX_ATTRIBUTE_COUNT)
					{
						g_log_error.log("renderer", "Too many vertex attributes in shader %s", m_path.c_str());
						onFailure();
						fs.close(file);
						return;
					}
				}
				serializer.deserializeArrayEnd();
			}
			else if (strcmp(label, "shadowmap_required") == 0)
			{
				serializer.deserialize(m_is_shadowmap_required);
			}
		}
		serializer.deserializeObjectEnd();
		
		int32_t size = (int32_t)file->size() - file->pos() + 1; /// TODO + 1 is from JsonSerializer::m_buffer, hide this implementation detail
		ShaderManager* manager = static_cast<ShaderManager*>(getResourceManager().get(ResourceManager::SHADER));
		char* buf = manager->getBuffer(size + 1);
		serializer.deserializeRawString(buf, size);
		buf[size] = '\0';

		char* end = strstr(buf, "//~VS");		
		if (!end)
		{
			g_log_error.log("renderer", "Could not process shader file %s", m_path.c_str());
			onFailure();
			fs.close(file);
			return;
		}
		int32_t vs_len = (int32_t)(end - buf);
		buf[vs_len-1] = 0;
		m_vertex_id = attach(GL_VERTEX_SHADER, buf, vs_len);
		m_fragment_id = attach(GL_FRAGMENT_SHADER, buf + vs_len, size - vs_len); /// TODO size
		glLinkProgram(m_program_id);
		GLint link_status;
		glGetProgramiv(m_program_id, GL_LINK_STATUS, &link_status);
		if (link_status != GL_TRUE)
		{
			g_log_error.log("renderer", "Could not link shader %s", m_path.c_str());
			onFailure();
			fs.close(file);
			return;
		}

		for (int i = 0; i < attribute_count; ++i)
		{
			m_vertex_attributes_ids[i] = glGetAttribLocation(m_program_id, attributes[i]);
		}


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
