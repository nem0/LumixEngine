#include "graphics/gl_ext.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "graphics/texture.h"


namespace Lux
{


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


void Texture::loaded(FS::IFile* file, bool success)
{
	if(success)
	{
		size_t buffer_size = file->size();
		char* buffer = LUX_NEW_ARRAY(char, buffer_size);
		file->read(buffer, buffer_size);			

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
		unsigned char* image_dest = LUX_NEW_ARRAY(unsigned char, image_size);
	

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

		glGenTextures(1, &m_id);
		if (m_id == 0)
		{
			LUX_DELETE_ARRAY(buffer);
			return;
		}

		glBindTexture(GL_TEXTURE_2D, m_id);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, header.width, header.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_dest);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		/*glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);*/

		LUX_DELETE_ARRAY(image_dest);
		LUX_DELETE_ARRAY(buffer);
	}
/// TODO close file somehow
}


bool Texture::load(const char* path, FS::FileSystem& file_system)
{
	FS::ReadCallback cb;
	cb.bind<Texture, &Texture::loaded>(this);
	file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
	return true;
}


void Texture::apply(int unit)
{
	glActiveTexture(GL_TEXTURE0 + unit); 
	glBindTexture(GL_TEXTURE_2D, m_id);
	glEnable(GL_TEXTURE_2D);
}


} // ~namespace Lux
