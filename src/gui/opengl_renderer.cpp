#include <cstdio>
#include <Windows.h>
#include <gl/GL.h>
#include "gui/opengl_renderer.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/map.h"
#include "core/math_utils.h"
#include "core/array.h"
#include "core/string.h"
#include "core/vec3.h"
#include "graphics/shader.h"
#include "gui/block.h"
#include "gui/texture_base.h"


namespace Lumix
{
namespace UI
{

	class OpenGLTexture : public TextureBase
	{
		public:
			OpenGLTexture(const char* name, float width, float height)
				: TextureBase(name, width, height)
			{
			}

			GLuint getId() const { return m_gl_id; }
			void setId(GLuint id) { m_gl_id = id; }
			void setFileSystem(FS::FileSystem& fs) { m_fs = &fs; }

			void imageLoaded(FS::IFile* file, bool success, FS::FileSystem& fs);

		private:
			GLuint m_gl_id;
			FS::FileSystem* m_fs;

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

		TextureBase* getImage(const char* name);
		void fontLoaded(FS::IFile* file, bool success, FS::FileSystem& fs);
		void fontImageLoaded(TextureBase& img);

		Map<char, Character> m_characters;
		Array<TextureBase*> m_images;
		OpenGLTexture* m_font_image;
		int m_window_height;
		Array<Block::Area> m_scissors_areas;
		FS::FileSystem* m_file_system;
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
		m_impl = LUMIX_NEW(OpenGLRendererImpl)();
		return true;
	}


	void OpenGLRenderer::destroy()
	{
		LUX_DELETE(m_impl);
		m_impl = NULL;
	}

	TextureBase* OpenGLRendererImpl::getImage(const char* name)
	{
		for(int i = 0; i < m_images.size(); ++i)
		{
			if(m_images[i]->getName() == name)
			{
				return m_images[i];
			}
		}
		return NULL;
	}


	TextureBase* OpenGLRenderer::loadImage(const char* name, FS::FileSystem& file_system)
	{
		TextureBase* img = m_impl->getImage(name);
		if(img)
		{
			return img;
		}
		img = LUMIX_NEW(OpenGLTexture)(name, (float)0, (float)0);
		static_cast<OpenGLTexture*>(img)->setId(0);
		static_cast<OpenGLTexture*>(img)->setFileSystem(file_system);

		FS::ReadCallback image_loaded_cb;
		image_loaded_cb.bind<OpenGLTexture, &OpenGLTexture::imageLoaded>(static_cast<OpenGLTexture*>(img));
		file_system.openAsync(file_system.getDefaultDevice(), name, FS::Mode::OPEN | FS::Mode::READ, image_loaded_cb);
		return img;
	}

	void OpenGLTexture::imageLoaded(FS::IFile* file, bool success, FS::FileSystem& fs)
	{
		if(success)
		{
			size_t buffer_size = file->size();
			char* buffer = LUMIX_NEW_ARRAY(char, buffer_size);
			file->read(buffer, buffer_size);			
			m_fs->close(file);

			TGAHeader header;
			memcpy(&header, buffer, sizeof(TGAHeader));
	
			int color_mode = header.bitsPerPixel / 8;
			int image_size = header.width * header.height * 4; 
	
			if (header.dataType != 2)
			{
				LUX_DELETE_ARRAY(buffer);
				return;
			}
	
			if (color_mode < 3)
			{
				LUX_DELETE_ARRAY(buffer);
				return;
			}
	
			const char* image_src = buffer + sizeof(TGAHeader);
			unsigned char* image_dest = LUMIX_NEW_ARRAY(unsigned char, image_size);
	

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
				LUX_DELETE_ARRAY(buffer);
				return;
			}

			glBindTexture(GL_TEXTURE_2D, texture_id);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, header.width, header.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_dest);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			/*glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);*/

			LUX_DELETE_ARRAY(image_dest);
			LUX_DELETE_ARRAY(buffer);
	
			setId(texture_id);
			setSize((float)header.width, (float)header.height);
			onLoaded().invoke(*this);
		}
		else
		{
			m_fs->close(file);
		}
	}


	void OpenGLRenderer::setWindowHeight(int height)
	{
		m_impl->m_window_height = height;
	}


	void OpenGLRenderer::beginRender(float w, float h)
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		m_impl->m_scissors_areas.clear();
		glColor3f(1, 1, 1);
		glDisable(GL_SCISSOR_TEST);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, w, h, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	Block::Area OpenGLRenderer::getCharArea(const char* text, int pos, float max_width)
	{
		Block::Area area;
		if(text)
		{
			float width = 0;
			float height = 0;
			float prev_h = 0;
			const char* c = text;
			bool is_multiline = false;
			OpenGLRendererImpl::Character character;
			bool found = false;
			bool is_some_char = false;
			while(*c)
			{
				if(m_impl->m_characters.find(*c, character))
				{
					is_some_char = true;
					if(c - text == pos)
					{
						found = true;
						area.left = width;
						area.top = prev_h + character.y_offset;
						area.right = width + character.x_advance;
						area.bottom = prev_h + character.pixel_h + character.y_offset;
						area.rel_bottom = area.rel_left = area.rel_right = area.rel_top = 0;
						break;
					}
					width += character.x_advance;
					height = Math::max(height, character.pixel_h);
					if(width > max_width || *c == '\n')
					{
						is_multiline = true;
						width = 0;
						prev_h += height;
					}
				}
				else if(*c == '\n')
				{
					is_multiline = true;
					width = 0;
					prev_h += height;
				}
				++c;
			}
			if(!found)
			{
				if(is_some_char)
				{
					area.left = width;
					area.top = prev_h + character.y_offset;
					area.right = width + character.x_advance;
					area.bottom = prev_h + character.pixel_h + character.y_offset;
				}
				else
				{
					area.left = 0;
					area.right = 3;
					area.top = 0;
					area.bottom = 20;
				}
				area.rel_bottom = area.rel_left = area.rel_right = area.rel_top = 0;
			}
		}
		return area;
	}

	void OpenGLRenderer::measureText(const char* text, float* w, float* h, float max_width)
	{
		if(!text)
		{
			*w = 0;
			*h = 0;
			return;
		}
		float width = 0;
		float height = 0;
		float prev_h = 0;
		const char* c = text;
		bool is_multiline = false;
		while(*c)
		{
			OpenGLRendererImpl::Character character;
			if(m_impl->m_characters.find(*c, character))
			{
				width += character.x_advance;
				height = Math::max(height, character.pixel_h);
				if(width > max_width || *c == '\n')
				{
					is_multiline = true;
					width = 0;
					prev_h += height;
				}
			}
			else if(*c == '\n')
			{
				is_multiline = true;
				width = 0;
				prev_h += height;
			}
			++c;
		}
		*w = is_multiline ? max_width : width;
		*h = height + prev_h;
	}

	void OpenGLRenderer::pushScissorArea(float left, float top, float right, float bottom)
	{
		Block::Area r;
		r.left = left;
		r.top = top;
		r.right = right;
		r.bottom = bottom;
		if(m_impl->m_scissors_areas.empty())
		{
			r.rel_left = left;
			r.rel_top = top;
			r.rel_right = right;
			r.rel_bottom = bottom;
			glEnable(GL_SCISSOR_TEST);
		}
		else
		{
			Block::Area& parent_area = m_impl->m_scissors_areas.back();
			r.rel_left = Math::max(left, parent_area.rel_left);
			r.rel_top = Math::max(top, parent_area.rel_top);
			r.rel_right = Math::min(right, parent_area.rel_right);
			r.rel_bottom = Math::min(bottom, parent_area.rel_bottom);
		}
		glScissor((int)r.rel_left, (int)(m_impl->m_window_height - r.rel_bottom), (int)(r.rel_right - r.rel_left), (int)(r.rel_bottom - r.rel_top));
		m_impl->m_scissors_areas.push(r);
	}

	void OpenGLRenderer::popScissorArea()
	{
		m_impl->m_scissors_areas.pop();
		if(m_impl->m_scissors_areas.empty())
		{
			glDisable(GL_SCISSOR_TEST);
		}
		else
		{
			Block::Area& r = m_impl->m_scissors_areas.back();
			glScissor((int)r.rel_left, (int)(m_impl->m_window_height - r.rel_bottom), (int)(r.rel_right - r.rel_left), (int)(r.rel_bottom - r.rel_top));
		}
	}

	void OpenGLRenderer::renderText(const char* text, float x, float y, float z, float max_width)
	{
		if(!text || text[0] == '\0')
		{
			return;
		}
		struct Vec2
		{
			Vec2() {}
			void set(float _x, float _y) { x = _x; y = _y; }
			float x, y;
		};
		static Array<Vec3> verts;
		static Array<Vec2> uvs;
		int len = (int)strlen(text);
		verts.resize(len * 6);
		uvs.resize(len * 6);
		const char* c = text;
		float cur_x = x;
		float line_h = 0;
		float line_base = y;
		int i = 0;
		while(*c)
		{
			OpenGLRendererImpl::Character character;
			if(m_impl->m_characters.find(*c, character))
			{
				float cur_y = line_base + character.y_offset;
				line_h = Math::max(line_h, character.pixel_h);
				verts[i*6].set(cur_x, cur_y, z);
				verts[i*6+1].set(cur_x, cur_y + character.pixel_h, z);
				verts[i*6+2].set(cur_x + character.pixel_w, cur_y + character.pixel_h, z);
			
				verts[i*6+3].set(cur_x, cur_y, z);
				verts[i*6+4].set(cur_x + character.pixel_w, cur_y + character.pixel_h, z);
				verts[i*6+5].set(cur_x + character.pixel_w, cur_y, z);
			
				cur_x += character.x_advance;

				if(cur_x - x > max_width)
				{
					cur_x = x;
					line_base += line_h;
				}

				uvs[i*6].set(character.left, character.top);
				uvs[i*6+1].set(character.left, character.bottom);
				uvs[i*6+2].set(character.right, character.bottom);
			
				uvs[i*6+3].set(character.left, character.top);
				uvs[i*6+4].set(character.right, character.bottom);
				uvs[i*6+5].set(character.right, character.top);
				++i;
			}
			else if(*c == '\n')
			{
				cur_x = x;
				line_base += line_h;
			}
			++c;
		}
		glEnable(GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		renderImage(m_impl->m_font_image, &verts[0].x, &uvs[0].x, verts.size());
	}

	bool readLine(FS::IFile* file, char buffer[], int max_size)
	{
		int i = 0;
		while(file->read(buffer + i, 1) && buffer[i] != '\n' && buffer[i] != '\0' && i < max_size - 1)
		{
			++i;
		}
		buffer[i+1] = 0;
		return buffer[i] == '\n' || buffer[i] == '\0' || i == max_size - 1;
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

	void OpenGLRendererImpl::fontImageLoaded(TextureBase& texture)
	{
		char tmp[255];
		strcpy_s(tmp, texture.getName().c_str());
		int len = (int)strlen(tmp);
		strcpy_s(tmp + len - 4, 255 - len + 4, ".fnt");

		FS::ReadCallback font_loaded_cb;
		font_loaded_cb.bind<OpenGLRendererImpl, &OpenGLRendererImpl::fontLoaded>(this);
		m_file_system->openAsync(m_file_system->getDefaultDevice(), tmp, FS::Mode::OPEN | FS::Mode::READ, font_loaded_cb);
	}

	void OpenGLRendererImpl::fontLoaded(FS::IFile* file, bool success, FS::FileSystem& fs)
	{
		if(success)
		{
			char line[255];
			while(readLine(file, line, 255) && strncmp(line, "chars count", 11) != 0);
			if(strncmp(line, "chars count", 11) == 0)
			{
				int count;
				sscanf_s(getFirstNumberPos(line), "%d", &count);
				for(int i = 0; i < count; ++i)
				{
					readLine(file, line, 255);
					const char* c = getFirstNumberPos(line);
					int id;
					sscanf_s(c, "%d", &id);
					OpenGLRendererImpl::Character character;
					int tmp;
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.left = (float)tmp / m_font_image->getWidth();
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.top = (float)tmp / m_font_image->getHeight();
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.pixel_w = (float)tmp;
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.pixel_h = (float)tmp;
					character.right = character.left + character.pixel_w / m_font_image->getWidth();
					character.bottom = character.top + character.pixel_h / m_font_image->getHeight();
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.x_offset = (float)tmp;
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.y_offset = (float)tmp;
					c = getNextNumberPos(c);
					sscanf_s(c, "%d", &tmp);
					character.x_advance = (float)tmp;
					m_characters.insert((char)id, character);
				}
			}
		}

		fs.close(file);
	}


	void OpenGLRenderer::loadFont(const char* path, FS::FileSystem& file_system)
	{
		m_impl->m_file_system = &file_system;
		m_impl->m_font_image = static_cast<OpenGLTexture*>(loadImage(path, file_system));
		m_impl->m_font_image->onLoaded().bind<OpenGLRendererImpl, &OpenGLRendererImpl::fontImageLoaded>(m_impl);
	}


	void OpenGLRenderer::renderImage(TextureBase* image, float* vertices, float* tex_coords, int vertex_count)
	{
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, vertices);
		
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, static_cast<OpenGLTexture*>(image)->getId());
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, tex_coords);

		glDrawArrays(GL_TRIANGLES, 0, vertex_count);

		glDisable(GL_TEXTURE_2D);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}


} // ~namespace UI
} // ~namespace Lumix
