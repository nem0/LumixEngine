#include "gui/opengl_renderer.h"
#include <cstdio>
#include <Windows.h>
#include <gl/GL.h>
#include "core/map.h"
#include "core/string.h"
#include "core/vec3.h"
#include "core/vector.h"


namespace Lux
{
namespace UI
{

	struct Image
	{
		string m_name;
		GLuint m_gl_image;
		float m_width;
		float m_height;
	};

	struct OpenGLRendererImpl
	{
		struct Character
		{
			float left;
			float top;
			float right;
			float bottom;
			float pixel_w;
			float pixel_h;
			float x_offset;
			float y_offset;
			float x_advance;
		};

		Image* getImage(const char* name);

		map<char, Character> m_characters;
		vector<Image*> m_images;
		int m_font_image;
		int m_font_image_width;
		int m_font_image_height;
		int m_window_height;
	};

	#pragma pack(1) 
	struct TGAHeader 
	{
		char  idLength;
		char  colourMapType;
		char  dataType;
		short int colourMapOrigin;
		short int colourMapLength;
		char  colourMapDepth;
		short int xOrigin;
		short int yOrigin;
		short int width;
		short int height;
		char  bitsPerPixel;
		char  imageDescriptor;
	};
	#pragma pack()


	bool OpenGLRenderer::create()
	{
		m_impl = new OpenGLRendererImpl();
		return true;
	}


	void OpenGLRenderer::destroy()
	{
		delete m_impl;
		m_impl = 0;
	}

	Image* OpenGLRendererImpl::getImage(const char* name)
	{
		for(int i = 0; i < m_images.size(); ++i)
		{
			if(m_images[i]->m_name == name)
			{
				return m_images[i];
			}
		}
		return NULL;
	}


	int OpenGLRenderer::loadImage(const char* name)
	{
		Image* img = m_impl->getImage(name);
		if(img)
		{
			return img->m_gl_image;
		}
		img = new Image();
		FILE* fp;
		fopen_s(&fp, name, "rb");

		fseek(fp, 0, SEEK_END);
		long buffer_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
	
		char* buffer = new char[buffer_size];
		fread(buffer, buffer_size, 1, fp);
		fclose(fp);

		TGAHeader header;
		memcpy(&header, buffer, sizeof(TGAHeader));
	
		int color_mode = header.bitsPerPixel / 8;
		int image_size = header.width * header.height * 4; 
	
		if (header.dataType != 2)
		{
			return -1;
		}
	
		if (color_mode < 3)
		{
			return -1;
		}
	
		const char* image_src = buffer + sizeof(TGAHeader);
		unsigned char* image_dest = new unsigned char[image_size];
	
		img->m_width = (float)header.width;
		img->m_height = (float)header.height;
		img->m_name = name;

		// Targa is BGR, swap to RGB and flip Y axis
		for (long y = 0; y < header.height; y++)
		{
			long read_index = y * header.width * color_mode;
			long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : (header.height - y - 1) * header.width * color_mode;
			for (long x = 0; x < header.width; x++)
			{
				image_dest[write_index] = image_src[read_index+2];
				image_dest[write_index+1] = image_src[read_index+1];
				image_dest[write_index+2] = image_src[read_index];
				if (color_mode == 4)
					image_dest[write_index+3] = image_src[read_index+3];
				else
					image_dest[write_index+3] = 255;
			
				write_index += 4;
				read_index += color_mode;
			}
		}

		GLuint texture_id = 0;
		glGenTextures(1, &texture_id);
		if (texture_id == 0)
		{
			//printf("Failed to generate textures\n");
			return -1;
		}

		glBindTexture(GL_TEXTURE_2D, texture_id);

		uint32_t color = 0xffffFFFF;
		//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, header.width, header.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_dest);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		/*glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
*/
		delete [] image_dest;
		delete [] buffer;
	
		img->m_gl_image = texture_id;

		return texture_id;
	}


	void OpenGLRenderer::setWindowHeight(int height)
	{
		m_impl->m_window_height = height;
	}


	void OpenGLRenderer::beginRender()
	{
		glEnable(GL_SCISSOR_TEST);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, 800, 600, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	void OpenGLRenderer::measureText(const char* text, float* w, float* h)
	{
		if(!text)
		{
			*w = 0;
			*h = 0;
			return;
		}
		float width = 0;
		float height = 0;
		const char* c = text;
		while(*c)
		{
			OpenGLRendererImpl::Character character;
			if(m_impl->m_characters.find(*c, character))
			{
				width += character.x_advance;
				height = max(height, character.pixel_h);
			}
			++c;
		}
		*w = width;
		*h = height;
	}

	void OpenGLRenderer::setScissorArea(int left, int top, int right, int bottom)
	{
		glScissor(left, m_impl->m_window_height - bottom, right - left, bottom - top);
		//glScissor(left, top, right - left, bottom - top);
	}


	void OpenGLRenderer::renderText(const char* text, float x, float y)
	{
		if(!text)
		{
			return;
		}
		struct Vec2
		{
			Vec2() {}
			void set(float _x, float _y) { x = _x; y = _y; }
			float x, y;
		};
		static vector<Vec3> verts;
		static vector<Vec2> uvs;
		int len = strlen(text);
		verts.resize(len * 6);
		uvs.resize(len * 6);
		const char* c = text;
		float cur_x = x;
		int i = 0;
		while(*c)
		{
			OpenGLRendererImpl::Character character;
			m_impl->m_characters.find(*c, character);
			float cur_y = y + character.y_offset;
			verts[i*6].set(cur_x, cur_y, 0);
			verts[i*6+1].set(cur_x, cur_y + character.pixel_h, 0);
			verts[i*6+2].set(cur_x + character.pixel_w, cur_y + character.pixel_h, 0);
			
			verts[i*6+3].set(cur_x, cur_y, 0);
			verts[i*6+4].set(cur_x + character.pixel_w, cur_y + character.pixel_h, 0);
			verts[i*6+5].set(cur_x + character.pixel_w, cur_y, 0);
			
			cur_x += character.x_advance;

			uvs[i*6].set(character.left, character.top);
			uvs[i*6+1].set(character.left, character.bottom);
			uvs[i*6+2].set(character.right, character.bottom);
			
			uvs[i*6+3].set(character.left, character.top);
			uvs[i*6+4].set(character.right, character.bottom);
			uvs[i*6+5].set(character.right, character.top);

			++c;
			++i;
		}
		glEnable(GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		renderImage(m_impl->m_font_image, &verts[0].x, &uvs[0].x, verts.size());
		
	}

	const char* getFirstNumberPos(const char* str)
	{
		const char* c = str;
		while(*c != 0 && (*c < '0' || *c > '9'))
		{
			++c;
		}
		return c;
	}

	const char* getNextNumberPos(const char* str)
	{
		const char* c = str;
		while(*c != 0 && *c >= '0' && *c <= '9')
		{
			++c;
		}
		return getFirstNumberPos(c);
	}



	bool OpenGLRenderer::loadFont(const char* path)
	{
		m_impl->m_font_image_width = 256;
		m_impl->m_font_image_height = 256;
		m_impl->m_font_image = loadImage(path);
		char tmp[255];
		strcpy_s(tmp, path);
		int len = strlen(tmp);
		strcpy_s(tmp + len - 4, 255 - len + 4, ".fnt");
		FILE* fp;
		fopen_s(&fp, tmp, "r");
		char line[255];
		fgets(line, 255, fp);
		while(!feof(fp) && strncmp(line, "chars count", 11) != 0)
		{
			fgets(line, 255, fp);

		}
		if(strncmp(line, "chars count", 11) == 0)
		{
			int count;
			sscanf_s(getFirstNumberPos(line), "%d", &count);
			for(int i = 0; i < count; ++i)
			{
				fgets(line, 255, fp);
				const char* c = getFirstNumberPos(line);
				int id;
				sscanf_s(c, "%d", &id);
				OpenGLRendererImpl::Character character;
				int tmp;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.left = (float)tmp / m_impl->m_font_image_width;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.top = (float)tmp / m_impl->m_font_image_height;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.pixel_w = (float)tmp;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.pixel_h = (float)tmp;
				character.right = character.left + character.pixel_w / m_impl->m_font_image_width;
				character.bottom = character.top + character.pixel_h / m_impl->m_font_image_height;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.x_offset = (float)tmp;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.y_offset = (float)tmp;
				c = getNextNumberPos(c);
				sscanf_s(c, "%d", &tmp);
				character.x_advance = (float)tmp;
				m_impl->m_characters.insert((char)id, character);
			}
		}
		fclose(fp);
		return false;
	}


	void OpenGLRenderer::renderImage(int image, float* vertices, float* tex_coords, int vertex_count)
	{
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, vertices);
		
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)image);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, tex_coords);

		glDrawArrays(GL_TRIANGLES, 0, vertex_count);

		glDisableClientState(GL_VERTEX_ARRAY);
		glDisable(GL_TEXTURE_2D);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}


} // ~namespace UI
} // ~namespace Lux