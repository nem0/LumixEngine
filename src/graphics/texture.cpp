#include "graphics/gl_ext.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/log.h"
#include "graphics/texture.h"

namespace Lux
{


namespace DDS
{
	static const uint32_t DDS_MAGIC = 0x20534444; //  little-endian
	static const uint32_t DDSD_CAPS = 0x00000001;
	static const uint32_t DDSD_HEIGHT = 0x00000002;
	static const uint32_t DDSD_WIDTH = 0x00000004;
	static const uint32_t DDSD_PITCH = 0x00000008;
	static const uint32_t DDSD_PIXELFORMAT = 0x00001000;
	static const uint32_t DDSD_MIPMAPCOUNT = 0x00020000;
	static const uint32_t DDSD_LINEARSIZE = 0x00080000;
	static const uint32_t DDSD_DEPTH = 0x00800000;
	static const uint32_t DDPF_ALPHAPIXELS = 0x00000001;
	static const uint32_t DDPF_FOURCC = 0x00000004;
	static const uint32_t DDPF_INDEXED = 0x00000020;
	static const uint32_t DDPF_RGB = 0x00000040;
	static const uint32_t DDSCAPS_COMPLEX = 0x00000008;
	static const uint32_t DDSCAPS_TEXTURE = 0x00001000;
	static const uint32_t DDSCAPS_MIPMAP = 0x00400000;
	static const uint32_t DDSCAPS2_CUBEMAP = 0x00000200;
	static const uint32_t DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400;
	static const uint32_t DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800;
	static const uint32_t DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000;
	static const uint32_t DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000;
	static const uint32_t DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000;
	static const uint32_t DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000;
	static const uint32_t DDSCAPS2_VOLUME = 0x00200000;
	static const uint32_t D3DFMT_DXT1 = '1TXD';
	static const uint32_t D3DFMT_DXT2 = '2TXD';
	static const uint32_t D3DFMT_DXT3 = '3TXD';
	static const uint32_t D3DFMT_DXT4 = '4TXD';
	static const uint32_t D3DFMT_DXT5 = '5TXD';

	struct PixelFormat {
		uint32_t dwSize;
		uint32_t dwFlags;
		uint32_t dwFourCC;
		uint32_t dwRGBBitCount;
		uint32_t dwRBitMask;
		uint32_t dwGBitMask;
		uint32_t dwBBitMask;
		uint32_t dwAlphaBitMask;
	};

	struct Caps2 {
		uint32_t dwCaps1;
		uint32_t dwCaps2;
		uint32_t dwDDSX;
		uint32_t dwReserved;
	};

	struct Header {
		uint32_t dwMagic;
		uint32_t dwSize;
		uint32_t dwFlags;
		uint32_t dwHeight;
		uint32_t dwWidth;
		uint32_t dwPitchOrLinearSize;
		uint32_t dwDepth;
		uint32_t dwMipMapCount;
		uint32_t dwReserved1[11];

		PixelFormat pixelFormat;
		Caps2 caps2;

		uint32_t dwReserved2;
	};

	struct LoadInfo {
		bool compressed;
		bool swap;
		bool palette;
		uint32_t divSize;
		uint32_t blockBytes;
		GLenum internalFormat;
		GLenum externalFormat;
		GLenum type;
	};

	static bool isDXT1(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT1));
	}

	static bool isDXT3(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT3));

	}

	static bool isDXT5(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT5));
	}

	static bool isBGRA8(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_RGB)
			&& (pf.dwFlags & DDPF_ALPHAPIXELS)
			&& (pf.dwRGBBitCount == 32)
			&& (pf.dwRBitMask == 0xff0000)
			&& (pf.dwGBitMask == 0xff00)
			&& (pf.dwBBitMask == 0xff)
			&& (pf.dwAlphaBitMask == 0xff000000U));
	}

	static bool isBGR8(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_ALPHAPIXELS)
			&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
			&& (pf.dwRGBBitCount == 24)
			&& (pf.dwRBitMask == 0xff0000)
			&& (pf.dwGBitMask == 0xff00)
			&& (pf.dwBBitMask == 0xff));
	}

	static bool isBGR5A1(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_RGB)
			&& (pf.dwFlags & DDPF_ALPHAPIXELS)
			&& (pf.dwRGBBitCount == 16)
			&& (pf.dwRBitMask == 0x00007c00)
			&& (pf.dwGBitMask == 0x000003e0)
			&& (pf.dwBBitMask == 0x0000001f)
			&& (pf.dwAlphaBitMask == 0x00008000));
	}

	static bool isBGR565(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_RGB)
			&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
			&& (pf.dwRGBBitCount == 16)
			&& (pf.dwRBitMask == 0x0000f800)
			&& (pf.dwGBitMask == 0x000007e0)
			&& (pf.dwBBitMask == 0x0000001f));
	}

	static bool isINDEX8(PixelFormat& pf)
	{
		return ((pf.dwFlags & DDPF_INDEXED) && (pf.dwRGBBitCount == 8));
	}

	static LoadInfo loadInfoDXT1 = {
		true, false, false, 4, 8, GL_COMPRESSED_RGBA_S3TC_DXT1
	};
	static LoadInfo loadInfoDXT3 = {
		true, false, false, 4, 16, GL_COMPRESSED_RGBA_S3TC_DXT3
	};
	static LoadInfo loadInfoDXT5 = {
		true, false, false, 4, 16, GL_COMPRESSED_RGBA_S3TC_DXT5
	};
	static LoadInfo loadInfoBGRA8 = {
		false, false, false, 1, 4, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE
	};
	static LoadInfo loadInfoBGR8 = {
		false, false, false, 1, 3, GL_RGB8, GL_BGR, GL_UNSIGNED_BYTE
	};
	static LoadInfo loadInfoBGR5A1 = {
		false, true, false, 1, 2, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV
	};
	static LoadInfo loadInfoBGR565 = {
		false, true, false, 1, 2, GL_RGB5, GL_RGB, GL_UNSIGNED_SHORT_5_6_5
	};
	static LoadInfo loadInfoIndex8 = {
		false, false, true, 1, 1, GL_RGB8, GL_BGRA, GL_UNSIGNED_BYTE
	};
}


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


Texture::Texture(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
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

void Texture::apply(int unit)
{
	glActiveTexture(GL_TEXTURE0 + unit); 
	glBindTexture(GL_TEXTURE_2D, m_id);
	glEnable(GL_TEXTURE_2D);
}


bool Texture::loadTGA(FS::IFile* file)
{
	int buffer_size = file->size();
	char* buffer = LUX_NEW_ARRAY(char, buffer_size);
	file->read(buffer, buffer_size);

	TGAHeader header;
	memcpy(&header, buffer, sizeof(TGAHeader));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;

	if (header.dataType != 2)
	{
		LUX_DELETE_ARRAY(buffer);
		g_log_error.log("renderer", "Unsupported texture format %s", m_path);
		return false;
	}

	if (color_mode < 3)
	{
		LUX_DELETE_ARRAY(buffer);
		g_log_error.log("renderer", "Unsupported color mode %s", m_path);
		return false;
	}

	const char* image_src = buffer + sizeof(TGAHeader);
	unsigned char* image_dest = LUX_NEW_ARRAY(unsigned char, image_size);

	// Targa is BGR, swap to RGB, add alpha and flip Y axis
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : y * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			image_dest[write_index] = image_src[read_index + 2];
			image_dest[write_index + 1] = image_src[read_index + 1];
			image_dest[write_index + 2] = image_src[read_index];
			if (color_mode == 4)
				image_dest[write_index + 3] = image_src[read_index + 3];
			else
				image_dest[write_index + 3] = 255;

			write_index += 4;
			read_index += color_mode;
		}
	}

	glGenTextures(1, &m_id);
	if (m_id == 0)
	{
		LUX_DELETE_ARRAY(buffer);
		LUX_DELETE_ARRAY(image_dest);
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, m_id);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, header.width, header.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_dest);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	/*glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);*/

	LUX_DELETE_ARRAY(image_dest);
	LUX_DELETE_ARRAY(buffer);
	return true;
}


bool Texture::loadDDS(FS::IFile* file)
{
	DDS::Header hdr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipMapCount = 0;

	file->read(&hdr, sizeof(hdr));
	ASSERT(hdr.dwMagic == DDS::DDS_MAGIC);
	ASSERT(hdr.dwSize == 124);

	if (hdr.dwMagic != DDS::DDS_MAGIC || hdr.dwSize != 124 ||
		!(hdr.dwFlags & DDS::DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDS::DDSD_CAPS))
	{
		g_log_error.log("renderer", "Wrong dds format or corrupted dds %s", m_path.c_str());
		return false;
	}

	width = hdr.dwWidth;
	height = hdr.dwHeight;
	ASSERT(!(width & (width - 1)));
	ASSERT(!(height & (height - 1)));
	if ((width & (width - 1)) || (height & (height - 1)))
	{
		g_log_error.log("renderer", "Wrong dds format %s", m_path.c_str());
		return false;
	}

	DDS::LoadInfo* li;

	if (isDXT1(hdr.pixelFormat))
	{
		li = &DDS::loadInfoDXT1;
	}
	else if (isDXT3(hdr.pixelFormat))
	{
		li = &DDS::loadInfoDXT3;
	}
	else if (isDXT5(hdr.pixelFormat))
	{
		li = &DDS::loadInfoDXT5;
	}
	else if (isBGRA8(hdr.pixelFormat))
	{
		li = &DDS::loadInfoBGRA8;
	}
	else if (isBGR8(hdr.pixelFormat))
	{
		li = &DDS::loadInfoBGR8;
	}
	else if (isBGR5A1(hdr.pixelFormat))
	{
		li = &DDS::loadInfoBGR5A1;
	}
	else if (isBGR565(hdr.pixelFormat))
	{
		li = &DDS::loadInfoBGR565;
	}
	else if (isINDEX8(hdr.pixelFormat))
	{
		li = &DDS::loadInfoIndex8;
	}
	else
	{
		g_log_error.log("renderer", "Unsupported DDS format %s", m_path.c_str());
		return false;
	}

	glGenTextures(1, &m_id);
	if (m_id == 0)
	{
		g_log_error.log("renderer", "Error generating OpenGL texture %s", m_path.c_str());
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, m_id);

	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE);
	mipMapCount = (hdr.dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;
	if (li->compressed)
	{
		uint32_t size = max(li->divSize, width) / li->divSize * max(li->divSize, height) / li->divSize * li->blockBytes;
		ASSERT(size == hdr.dwPitchOrLinearSize);
		ASSERT(hdr.dwFlags & DDS::DDSD_LINEARSIZE);
		if (size != hdr.dwPitchOrLinearSize || (hdr.dwFlags & DDS::DDSD_LINEARSIZE) == 0)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer", "Unsupported DDS format %s", m_path.c_str());
			onFailure();
			return false;
		}
		unsigned char * data = LUX_NEW_ARRAY(unsigned char, size);
		ASSERT(data);
		for (uint32_t ix = 0; ix < mipMapCount; ++ix)
		{
			file->read(data, size);
			glCompressedTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, size, data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			width = (width + 1) >> 1;
			height = (height + 1) >> 1;
			size = max(li->divSize, width) / li->divSize * max(li->divSize, height) / li->divSize * li->blockBytes;
		}
		LUX_DELETE_ARRAY(data);
	}
	else if (li->palette)
	{
		ASSERT(hdr.dwFlags & DDS::DDSD_PITCH);
		ASSERT(hdr.pixelFormat.dwRGBBitCount == 8);
		if ((hdr.dwFlags & DDS::DDSD_PITCH) == 0 || hdr.pixelFormat.dwRGBBitCount != 8)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer", "Unsupported DDS format %s", m_path.c_str());
			return false;
		}
		uint32_t size = hdr.dwPitchOrLinearSize * height;
		ASSERT(size == width * height * li->blockBytes);
		if (size != width * height * li->blockBytes)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer", "Unsupported DDS format or corrupted DDS %s", m_path.c_str());
			return false;
		}
		unsigned char * data = LUX_NEW_ARRAY(unsigned char, size);
		uint32_t palette[256];
		uint32_t * unpacked = LUX_NEW_ARRAY(uint32_t, size);
		file->read(palette, 4 * 256);
		for (uint32_t ix = 0; ix < mipMapCount; ++ix)
		{
			file->read(data, size);
			for (uint32_t zz = 0; zz < size; ++zz)
			{
				unpacked[zz] = palette[data[zz]];
			}
			glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
			glTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, li->externalFormat, li->type, unpacked);
			width = (width + 1) >> 1;
			height = (height + 1) >> 1;
			size = width * height * li->blockBytes;
		}
		LUX_DELETE_ARRAY(data);
		LUX_DELETE_ARRAY(unpacked);
	}
	else
	{
		if (li->swap)
		{
			glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
		}
		uint32_t size = width * height * li->blockBytes;
		unsigned char * data = LUX_NEW_ARRAY(unsigned char, size);
		for (uint32_t ix = 0; ix < mipMapCount; ++ix)
		{
			file->read(data, size);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
			glTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, li->externalFormat, li->type, data);
			width = (width+ 1) >> 1;
			height = (height + 1) >> 1;
			size = width * height * li->blockBytes;
		}
		LUX_DELETE_ARRAY(data);
		glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);

	return true;
}

void Texture::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	TODO("Optimize it! Buffer is not necesary at all and image_dest might be shared.");
	if (success)
	{
		const char* path = m_path.c_str();
		int len = m_path.length();
		if (len > 3 && strcmp(path + len - 4, ".dds") == 0)
		{
			bool loaded = loadDDS(file);
			ASSERT(loaded);
		}
		else
		{
			bool loaded = loadTGA(file);
			ASSERT(loaded);
		}

		m_size = file->size();
		decrementDepCount();
	}
	else
	{
		g_log_error.log("renderer", "Error loading texture %s", m_path.c_str());
		onFailure();
	}
	
	fs.close(file);
}


void Texture::doUnload(void)
{
	glDeleteTextures(1, &m_id);

	m_size = 0;
	onEmpty();
}

FS::ReadCallback Texture::getReadCallback()
{
	FS::ReadCallback cb;
	cb.bind<Texture, &Texture::loaded>(this);
	return cb;
}

} // ~namespace Lux
