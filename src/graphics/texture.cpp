#include "graphics/gl_ext.h"
#include "graphics/texture.h"


namespace Lux
{


Texture::Texture()
{
	glGenTextures(1, &m_id);
}


Texture::~Texture()
{
	glDeleteTextures(1, &m_id);
}


bool Texture::create(int w, int h)
{
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	return true;
}


bool Texture::load(const char* path)
{
/*	SDL_Surface* surface = IMG_Load(path);
	if(!surface)
	{
		return false;
	}
	if(surface->format->BytesPerPixel != 3)
	{
		SDL_FreeSurface(surface);
		return false;
	}
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, surface->w, surface->h, 0, GL_RGB, GL_UNSIGNED_BYTE, surface->pixels);
	SDL_FreeSurface(surface);*/
	return true;
}


void Texture::apply(int unit)
{
	glActiveTexture(GL_TEXTURE0 + unit); 
	glBindTexture(GL_TEXTURE_2D, m_id);
	glEnable(GL_TEXTURE_2D);
}


} // ~namespace Lux
