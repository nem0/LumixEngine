#pragma once
#include <Windows.h>
#include <gl/GL.h>


namespace Lux
{


class Shader;


class Geometry
{
	public:
		Geometry();
		~Geometry();

		void copy(const void* data, int size);
		void draw(int start, int count, Shader& shader);

	private:
		GLuint m_id;
};


} // ~namespace Lux

