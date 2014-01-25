#pragma once


namespace Lux
{


#include <Windows.h>
#include <gl/GL.h>


class Texture
{
	public:
		Texture();
		~Texture();

		bool create(int w, int h);
		bool load(const char* path);
		void apply(int unit = 0);

	private:
		GLuint m_id;
};


} // ~namespace Lux
