#pragma once
#include <Windows.h>
#include <gl/GL.h>


namespace Lux
{


struct Vec3;
struct Matrix;

class Shader
{
	public:
		Shader(const char* vertex, const char* fragment);
		~Shader();

		void apply();
		void setUniform(const char* name, GLint value);
		void setUniform(const char* name, const Vec3& value);
		void setUniform(const char* name, GLfloat value);
		void setUniform(const char* name, const Matrix& mtx);
		void setUniform(const char* name, const Matrix* matrices, int count);
		void applyVertexAttributes();
		void disableVertexAttributes();
	
	private:
		GLuint attach(GLenum type, const char* shader);

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
