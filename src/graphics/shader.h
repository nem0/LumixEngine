#pragma once
#include <Windows.h>
#include <gl/GL.h>
#include "core/lux.h"


namespace Lux
{
namespace FS
{
	class FileSystem;
	class IFile;
}

struct Vec3;
struct Matrix;

class LUX_ENGINE_API Shader
{
	public:
		Shader();
		~Shader();

		void apply();
		void setUniform(const char* name, GLint value);
		void setUniform(const char* name, const Vec3& value);
		void setUniform(const char* name, GLfloat value);
		void setUniform(const char* name, const Matrix& mtx);
		void setUniform(const char* name, const Matrix* matrices, int count);
		void load(const char* path, FS::FileSystem& file_system);
		GLint getAttribId(int index) { return m_vertex_attributes_ids[index]; }

	private:
		GLuint attach(GLenum type, const char* src, int length);
		void loaded(FS::IFile* file, bool success);

	private:
		enum 
		{
			BONE_WEIGHT,
			BONE_INDEX,
			VERTEX_ATTRIBUTES_COUNT
		};

	private:
		GLuint	m_program_id;
		GLuint	m_vertex_id;
		GLuint	m_fragment_id;
		GLint	m_vertex_attributes_ids[VERTEX_ATTRIBUTES_COUNT];
};


} // ~namespace Lux
