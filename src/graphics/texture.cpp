#include "graphics/gl_ext.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/texture.h"
#include "graphics/texture_manager.h"

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
		true, false, false, 4, 8, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
	};
	static LoadInfo loadInfoDXT3 = {
		true, false, false, 4, 16, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
	};
	static LoadInfo loadInfoDXT5 = {
		true, false, false, 4, 16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
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

	struct DXTColBlock
	{
		uint16_t col0;
		uint16_t col1;
		uint8_t row[4];
	};

	struct DXT3AlphaBlock
	{
		uint16_t row[4];
	};

	struct DXT5AlphaBlock
	{
		uint8_t alpha0;
		uint8_t alpha1;
		uint8_t row[6];
	};

	static LUX_FORCE_INLINE void swapMemory(void* mem1, void* mem2, size_t size)
	{
		if(size < 2048)
		{
			uint8_t tmp[2048];
			memcpy(tmp, mem1, size);
			memcpy(mem1, mem2, size);
			memcpy(mem2, tmp, size);
		}
		else
		{
			uint8_t* tmp = LUX_NEW_ARRAY(uint8_t, size);
			memcpy(tmp, mem1, size);
			memcpy(mem1, mem2, size);
			memcpy(mem2, tmp, size);
			LUX_DELETE_ARRAY(tmp);
		}
	}

	static void flipBlockDXTC1(DXTColBlock *line, int numBlocks)
	{
		DXTColBlock *curblock = line;

		for (int i = 0; i < numBlocks; i++)
		{
			swapMemory(&curblock->row[0], &curblock->row[3], sizeof(uint8_t));
			swapMemory(&curblock->row[1], &curblock->row[2], sizeof(uint8_t));
			++curblock;
		}
	}

	static void flipBlockDXTC3(DXTColBlock *line, int numBlocks)
	{
		DXTColBlock *curblock = line;
		DXT3AlphaBlock *alphablock;

		for (int i = 0; i < numBlocks; i++)
		{
			alphablock = (DXT3AlphaBlock*)curblock;

			swapMemory(&alphablock->row[0], &alphablock->row[3], sizeof(uint16_t));
			swapMemory(&alphablock->row[1], &alphablock->row[2], sizeof(uint16_t));
			++curblock;

			swapMemory(&curblock->row[0], &curblock->row[3], sizeof(uint8_t));
			swapMemory(&curblock->row[1], &curblock->row[2], sizeof(uint8_t));
			++curblock;
		}
	}

	static void flipDXT5Alpha(DXT5AlphaBlock *block)
	{
		uint8_t tmp_bits[4][4];

		const uint32_t mask = 0x00000007;
		uint32_t bits = 0;
		memcpy(&bits, &block->row[0], sizeof(uint8_t) * 3);

		tmp_bits[0][0] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[0][1] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[0][2] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[0][3] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[1][0] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[1][1] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[1][2] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[1][3] = (uint8_t)(bits & mask);

		bits = 0;
		memcpy(&bits, &block->row[3], sizeof(uint8_t) * 3);

		tmp_bits[2][0] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[2][1] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[2][2] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[2][3] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[3][0] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[3][1] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[3][2] = (uint8_t)(bits & mask);
		bits >>= 3;
		tmp_bits[3][3] = (uint8_t)(bits & mask);

		uint32_t *out_bits = (uint32_t*)&block->row[0];

		*out_bits = *out_bits | (tmp_bits[3][0] << 0);
		*out_bits = *out_bits | (tmp_bits[3][1] << 3);
		*out_bits = *out_bits | (tmp_bits[3][2] << 6);
		*out_bits = *out_bits | (tmp_bits[3][3] << 9);

		*out_bits = *out_bits | (tmp_bits[2][0] << 12);
		*out_bits = *out_bits | (tmp_bits[2][1] << 15);
		*out_bits = *out_bits | (tmp_bits[2][2] << 18);
		*out_bits = *out_bits | (tmp_bits[2][3] << 21);

		out_bits = (uint32_t*)&block->row[3];

		*out_bits &= 0xff000000;

		*out_bits = *out_bits | (tmp_bits[1][0] << 0);
		*out_bits = *out_bits | (tmp_bits[1][1] << 3);
		*out_bits = *out_bits | (tmp_bits[1][2] << 6);
		*out_bits = *out_bits | (tmp_bits[1][3] << 9);

		*out_bits = *out_bits | (tmp_bits[0][0] << 12);
		*out_bits = *out_bits | (tmp_bits[0][1] << 15);
		*out_bits = *out_bits | (tmp_bits[0][2] << 18);
		*out_bits = *out_bits | (tmp_bits[0][3] << 21);
	}

	static void flipBlockDXTC5(DXTColBlock *line, int numBlocks)
	{
		DXTColBlock *curblock = line;
		DXT5AlphaBlock *alphablock;

		for (int i = 0; i < numBlocks; i++)
		{
			alphablock = (DXT5AlphaBlock*)curblock;

			flipDXT5Alpha(alphablock);

			++curblock;

			swapMemory(&curblock->row[0], &curblock->row[3], sizeof(uint8_t));
			swapMemory(&curblock->row[1], &curblock->row[2], sizeof(uint8_t));

			++curblock;
		}
	}

	/// from gpu gems
	static void flipCompressedTexture(int w, int h, int format, void* surface)
	{
		void (*flipBlocksFunction)(DXTColBlock*, int);
		int xblocks = w >> 2;
		int yblocks = h >> 2;
		int blocksize;

		switch (format)
		{
			case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
				blocksize = 8;
				flipBlocksFunction = &flipBlockDXTC1;
				break;
			case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
				blocksize = 16;
				flipBlocksFunction = &flipBlockDXTC3;
				break;
			case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
				blocksize = 16;
				flipBlocksFunction = &flipBlockDXTC5;
				break;
			default:
				ASSERT(false);
				return;
		}

		int linesize = xblocks * blocksize;

		DXTColBlock *top = (DXTColBlock*)surface;
		DXTColBlock *bottom = (DXTColBlock*)((uint8_t*)surface + ((yblocks - 1) * linesize));

		while (top < bottom)
		{
			(*flipBlocksFunction)(top, xblocks);
			(*flipBlocksFunction)(bottom, xblocks);
			swapMemory(bottom, top, linesize);

			top = (DXTColBlock*)((uint8_t*)top + linesize);
			bottom = (DXTColBlock*)((uint8_t*)bottom - linesize);
		}
	}
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
	TGAHeader header;
	file->read(&header, sizeof(header));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;

	if (header.dataType != 2)
	{
		g_log_error.log("renderer", "Unsupported texture format %s", m_path);
		return false;
	}

	if (color_mode < 3)
	{
		g_log_error.log("renderer", "Unsupported color mode %s", m_path);
		return false;
	}

	TextureManager* manager = static_cast<TextureManager*>(getResourceManager().get(ResourceManager::TEXTURE));
	uint8_t* image_dest = (uint8_t*)manager->getBuffer(image_size);

	// Targa is BGR, swap to RGB, add alpha and flip Y axis
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : y * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			file->read(&image_dest[write_index + 2], sizeof(uint8_t));
			file->read(&image_dest[write_index + 1], sizeof(uint8_t));
			file->read(&image_dest[write_index + 0], sizeof(uint8_t));
			if (color_mode == 4)
				file->read(&image_dest[write_index + 3], sizeof(uint8_t));
			else
				image_dest[write_index + 3] = 255;
			write_index += 4;
		}
	}

	glGenTextures(1, &m_id);
	if (m_id == 0)
	{
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, m_id);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, header.width, header.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_dest);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	return true;
}


bool Texture::loadDDS(FS::IFile* file)
{
	DDS::Header hdr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipMapCount = 0;

	file->read(&hdr, sizeof(hdr));

	if (hdr.dwMagic != DDS::DDS_MAGIC || hdr.dwSize != 124 ||
		!(hdr.dwFlags & DDS::DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDS::DDSD_CAPS))
	{
		ASSERT(false);
		g_log_error.log("renderer", "Wrong dds format or corrupted dds %s", m_path.c_str());
		return false;
	}

	width = hdr.dwWidth;
	height = hdr.dwHeight;
	if ((width & (width - 1)) || (height & (height - 1)))
	{
		ASSERT(false);
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

	TextureManager* manager = static_cast<TextureManager*>(getResourceManager().get(ResourceManager::TEXTURE));
	glBindTexture(GL_TEXTURE_2D, m_id);

	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE);
	mipMapCount = (hdr.dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;
	if (li->compressed)
	{
		uint32_t size = Math::max(li->divSize, width) / li->divSize * Math::max(li->divSize, height) / li->divSize * li->blockBytes;
		if (size != hdr.dwPitchOrLinearSize || (hdr.dwFlags & DDS::DDSD_LINEARSIZE) == 0)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer", "Unsupported DDS format %s", m_path.c_str());
			onFailure();
			return false;
		}
		uint8_t* data = (uint8_t*)manager->getBuffer(size);
		ASSERT(data);
		for (uint32_t ix = 0; ix < mipMapCount; ++ix)
		{
			file->read(data, size);
			DDS::flipCompressedTexture(width, height, li->internalFormat, data);
			glCompressedTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, size, data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			width = (width + 1) >> 1;
			height = (height + 1) >> 1;
			size = Math::max(li->divSize, width) / li->divSize * Math::max(li->divSize, height) / li->divSize * li->blockBytes;
		}
	}
	else if (li->palette)
	{
		if ((hdr.dwFlags & DDS::DDSD_PITCH) == 0 || hdr.pixelFormat.dwRGBBitCount != 8)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer", "Unsupported DDS format %s", m_path.c_str());
			return false;
		}
		uint32_t size = hdr.dwPitchOrLinearSize * height;
		if (size != width * height * li->blockBytes)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer", "Unsupported DDS format or corrupted DDS %s", m_path.c_str());
			return false;
		}
		unsigned char * data = LUX_NEW_ARRAY(unsigned char, size);
		uint32_t palette[256];
		uint32_t* unpacked = (uint32_t*)manager->getBuffer(size * sizeof(uint32_t));
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
	}
	else
	{
		if (li->swap)
		{
			glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
		}
		uint32_t size = width * height * li->blockBytes;
		uint8_t* data = (uint8_t*)manager->getBuffer(size);
		for (uint32_t ix = 0; ix < mipMapCount; ++ix)
		{
			file->read(data, size);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
			glTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, li->externalFormat, li->type, data);
			width = (width+ 1) >> 1;
			height = (height + 1) >> 1;
			size = width * height * li->blockBytes;
		}
		glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);

	return true;
}

void Texture::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if (success)
	{
		const char* path = m_path.c_str();
		size_t len = m_path.length();
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
