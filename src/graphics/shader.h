#pragma once
#include <Windows.h>
#include <gl/GL.h>
#include "core/resource.h"


namespace Lux
{
namespace FS
{
	class FileSystem;
	class IFile;
}

struct Vec3;
struct Matrix;

class LUX_ENGINE_API Shader : public Resource
{
	public:
		Shader(const Path& path, ResourceManager& resource_manager);
		~Shader();

		void apply();
		void setUniform(const char* name, GLint value);
		void setUniform(const char* name, const Vec3& value);
		void setUniform(const char* name, GLfloat value);
		void setUniform(const char* name, const Matrix& mtx);
		void setUniform(const char* name, const Matrix* matrices, int count);
		GLint getAttribId(int index) { return m_vertex_attributes_ids[index]; }

	private:
		GLuint attach(GLenum type, const char* src, int32_t length);
		void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);

		virtual void doUnload(void) LUX_OVERRIDE;
		virtual void doReload(void) LUX_OVERRIDE;
		virtual FS::ReadCallback getReadCallback() LUX_OVERRIDE;

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
