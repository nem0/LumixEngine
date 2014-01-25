#pragma once
#include <Windows.h>
#include <gl/GL.h>


namespace Lux
{


class Geometry
{
	public:
		Geometry();
		~Geometry();

		void copy(const void* data, int size);
		void draw(int start, int count);

	private:
		GLuint m_id;
};


} // ~namespace Lux

