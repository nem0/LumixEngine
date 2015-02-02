#include "graphics/gl_ext.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/texture.h"
#include "graphics/texture_manager.h"

namespace Lumix
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

		static LUMIX_FORCE_INLINE void swapMemory(void* mem1, void* mem2, size_t size, IAllocator& allocator)
		{
			if (size <= 32768)
			{
				uint8_t tmp[32768];
				memcpy(tmp, mem1, size);
				memcpy(mem1, mem2, size);
				memcpy(mem2, tmp, size);
			}
			else
			{
				uint8_t* tmp = (uint8_t*)allocator.allocate(sizeof(uint8_t) * size);
				memcpy(tmp, mem1, size);
				memcpy(mem1, mem2, size);
				memcpy(mem2, tmp, size);
				allocator.deallocate(tmp);
			}
		}

		static LUMIX_FORCE_INLINE void swapBytes(uint8_t* LUMIX_RESTRICT mem1, uint8_t* LUMIX_RESTRICT mem2)
		{
			uint8_t tmp = *mem1;
			*mem1 = *mem2;
			*mem2 = tmp;
		}

		static LUMIX_FORCE_INLINE void swapBytes(uint16_t* LUMIX_RESTRICT mem1, uint16_t* LUMIX_RESTRICT mem2)
		{
			uint16_t tmp = *mem1;
			*mem1 = *mem2;
			*mem2 = tmp;
		}

		static void flipBlockDXTC1(DXTColBlock *line, int numBlocks)
		{
			DXTColBlock *curblock = line;

			for (int i = 0; i < numBlocks; i++)
			{
				swapBytes(&curblock->row[0], &curblock->row[3]);
				swapBytes(&curblock->row[1], &curblock->row[2]);
				++curblock;
			}
		}

		static void flipBlockDXTC3(DXTColBlock *line, int numBlocks)
		{
			DXTColBlock *curblock = line;
			DXT3AlphaBlock *alphablock;

			for (int i = 0; i < numBlocks; i++)
			{
				alphablock = reinterpret_cast<DXT3AlphaBlock*>(curblock);

				swapBytes((uint16_t*)&alphablock->row[0], (uint16_t*)&alphablock->row[3]);
				swapBytes((uint16_t*)&alphablock->row[1], (uint16_t*)&alphablock->row[2]);
				++curblock;

				swapBytes(&curblock->row[0], &curblock->row[3]);
				swapBytes(&curblock->row[1], &curblock->row[2]);
				++curblock;
			}
		}

		static void flipDXT5Alpha(DXT5AlphaBlock *block)
		{
			uint8_t tmp_bits[4][4];

			const uint32_t mask = 0x00000007;
			uint32_t bits = 0;
			memcpy(&bits, &block->row[0], sizeof(uint8_t)* 3);

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
			memcpy(&bits, &block->row[3], sizeof(uint8_t)* 3);

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
			DXTColBlock* curblock = line;

			for (int i = 0; i < numBlocks; i++)
			{
				DXT5AlphaBlock* alphablock = reinterpret_cast<DXT5AlphaBlock*>(curblock);

				flipDXT5Alpha(alphablock);

				++curblock;

				swapBytes(&curblock->row[0], &curblock->row[3]);
				swapBytes(&curblock->row[1], &curblock->row[2]);

				++curblock;
			}
		}

		/// from gpu gems
		static void flipCompressedTexture(int w, int h, int format, void* surface, IAllocator& allocator)
		{
			PROFILE_FUNCTION();
			void(*flipBlocksFunction)(DXTColBlock*, int);
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

			DXTColBlock * LUMIX_RESTRICT top = static_cast<DXTColBlock*>(surface);
			DXTColBlock * LUMIX_RESTRICT bottom = reinterpret_cast<DXTColBlock*>((uint8_t*)surface + ((yblocks - 1) * linesize));

			while (top < bottom)
			{
				(*flipBlocksFunction)(top, xblocks);
				(*flipBlocksFunction)(bottom, xblocks);
				swapMemory(bottom, top, linesize, allocator);

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


	Texture::Texture(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, m_data_reference(0)
		, m_is_cubemap(false)
		, m_allocator(allocator)
		, m_data(m_allocator)
		, m_BPP(-1)
	{
		glGenTextures(1, &m_id);
	}

	Texture::~Texture()
	{
		ASSERT(isEmpty());
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
		glBindTexture(m_is_cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, m_id);
	}

	
	uint32_t Texture::getPixel(float x, float y) const
	{
		if(m_data.empty() || x >= m_width || y >= m_height || x < 0 || y < 0)
		{
			return 0;
		}

		// http://fastcpp.blogspot.sk/2011/06/bilinear-pixel-interpolation-using-sse.html
		int px = (int)x;
		int py = (int)y;
		const uint32_t* p0 = (uint32_t*)&m_data[(px + py * m_width) * 4];
 
		const uint8_t* p1 = (uint8_t*)p0;
		const uint8_t* p2 = (uint8_t*)(p0 + 1);
		const uint8_t* p3 = (uint8_t*)(p0 + m_width);
		const uint8_t* p4 = (uint8_t*)(p0 + 1 + m_width);
 
		float fx = x - px;
		float fy = y - py;
		float fx1 = 1.0f - fx;
		float fy1 = 1.0f - fy;
  
		int w1 = (int)(fx1 * fy1 * 256.0f);
		int w2 = (int)(fx  * fy1 * 256.0f);
		int w3 = (int)(fx1 * fy  * 256.0f);
		int w4 = (int)(fx  * fy  * 256.0f);
 
		uint8_t res[4];
		res[0] = (uint8_t)((p1[0] * w1 + p2[0] * w2 + p3[0] * w3 + p4[0] * w4) >> 8);
		res[1] = (uint8_t)((p1[1] * w1 + p2[1] * w2 + p3[1] * w3 + p4[1] * w4) >> 8);
		res[2] = (uint8_t)((p1[2] * w1 + p2[2] * w2 + p3[2] * w3 + p4[2] * w4) >> 8);
		res[3] = (uint8_t)((p1[3] * w1 + p2[3] * w2 + p3[3] * w3 + p4[3] * w4) >> 8);
 
		return *(uint32_t*)res;
	}


	unsigned int Texture::compareTGA(IAllocator& allocator, FS::IFile* file1, FS::IFile* file2, int difference)
	{
		TGAHeader header1, header2;
		file1->read(&header1, sizeof(header1));
		file2->read(&header2, sizeof(header2));

		if (header1.bitsPerPixel != header2.bitsPerPixel
			|| header1.width != header2.width
			|| header1.height != header2.height
			|| header1.dataType != header2.dataType
			|| header1.imageDescriptor != header2.imageDescriptor
			)
		{
			g_log_error.log("renderer") << "Trying to compare textures with different formats";
			return 0;
		}

		int color_mode = header1.bitsPerPixel / 8;
		if (header1.dataType != 2)
		{
			g_log_error.log("renderer") << "Unsupported texture format";
			return 0;
		}

		// Targa is BGR, swap to RGB, add alpha and flip Y axis
		int different_pixel_count = 0;
		size_t pixel_count = header1.width * header1.height;
		uint8_t* img1 = (uint8_t*)allocator.allocate(pixel_count * color_mode);
		uint8_t* img2 = (uint8_t*)allocator.allocate(pixel_count * color_mode);

		file1->read(img1, pixel_count * color_mode);
		file2->read(img2, pixel_count * color_mode);

		for (size_t i = 0; i < pixel_count * color_mode; i += color_mode)
		{
			for (int j = 0; j < color_mode; ++j)
			{
				if (Math::abs(img1[i + j] - img2[i + j]) > difference)
				{
					++different_pixel_count;
					break;
				}
			}
		}

		allocator.deallocate(img1);
		allocator.deallocate(img2);

		return different_pixel_count;
	}


	bool Texture::saveTGA(IAllocator& allocator, FS::IFile* file, int width, int height, int bytes_per_pixel, const uint8_t* image_dest, const Path& path)
	{
		if (bytes_per_pixel != 4)
		{
			g_log_error.log("renderer") << "Texture " << path.c_str() << " could not be saved, unsupported TGA format";
			return false;
		}

		uint8_t* data = (uint8_t*)allocator.allocate(width * height * 4);

		TGAHeader header;
		memset(&header, 0, sizeof(header));
		header.bitsPerPixel = (char)(bytes_per_pixel * 8);
		header.height = (short)height;
		header.width = (short)width;
		header.dataType = 2;

		file->write(&header, sizeof(header));

		for (long y = 0; y < header.height; y++)
		{
			long read_index = y * header.width * 4;
			long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : y * header.width * 4;
			for (long x = 0; x < header.width; x++)
			{
				data[write_index + 0] = image_dest[write_index + 2];
				data[write_index + 1] = image_dest[write_index + 1];
				data[write_index + 2] = image_dest[write_index + 0];
				data[write_index + 3] = image_dest[write_index + 3];
				write_index += 4;
			}
		}

		file->write(data, width * height * 4);

		allocator.deallocate(data);

		return true;
	}


	void Texture::saveTGA()
	{
		if (m_data.empty())
		{
			g_log_error.log("renderer") << "Texture " << getPath().c_str() << " could not be saved, no data was loaded";
			return;
		}

		FS::FileSystem& fs = m_resource_manager.getFileSystem();
		FS::IFile* file = fs.open("disk", getPath().c_str(), FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);

		saveTGA(m_allocator, file, m_width, m_height, m_BPP, &m_data[0], getPath());

		fs.close(file);
	}


void Texture::save()
{
	char ext[5];
	ext[0] = 0;
	PathUtils::getExtension(ext, 5, getPath().c_str());
	if (strcmp(ext, "raw") == 0 && m_BPP == 2)
	{
		FS::FileSystem& fs = m_resource_manager.getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(), getPath().c_str(), FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);
		file->write(&m_data[0], m_data.size() * sizeof(m_data[0]));
		fs.close(file);
	}
	else if (strcmp(ext, "tga") == 0 && m_BPP == 4)
	{
		saveTGA();
	}
	else
	{
		g_log_error.log("renderer") << "Texture " << getPath() << " can not be saved - unsupported format";
	}
}


void Texture::onDataUpdated()
{
	glBindTexture(GL_TEXTURE_2D, m_id);
	if (m_BPP == 4)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &m_data[0]);
	}
	else if (m_BPP == 2)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, m_width, m_height, 0, GL_RED, GL_UNSIGNED_SHORT, &m_data[0]);
	}
	else
	{
		ASSERT(false);
	}
}


bool Texture::loadRaw(FS::IFile& file)
{
	PROFILE_FUNCTION();
	size_t size = file.size();
	m_BPP = 2;
	m_width = (int)sqrt(size / m_BPP);
	m_height = m_width;

	if (m_data_reference)
	{
		m_data.resize(size);
		file.read(&m_data[0], size);
	}

	glGenTextures(1, &m_id);
	if (m_id == 0)
	{
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, m_width, m_height, 0, GL_RED, GL_UNSIGNED_SHORT, file.getBuffer());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return true;
}


bool Texture::loadTGA(FS::IFile& file)
{
	PROFILE_FUNCTION();
	TGAHeader header;
	file.read(&header, sizeof(header));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;
	if (header.dataType != 2)
	{
		g_log_error.log("renderer") << "Unsupported texture format " << m_path.c_str();
		return false;
	}

	if (color_mode < 3)
	{
		g_log_error.log("renderer") << "Unsupported color mode " << m_path.c_str();
		return false;
	}

	m_width = header.width;
	m_height = header.height;
	TextureManager* manager = static_cast<TextureManager*>(getResourceManager().get(ResourceManager::TEXTURE));
	if (m_data_reference)
	{
		m_data.resize(image_size);
	}
	uint8_t* image_dest = m_data_reference ? &m_data[0] : (uint8_t*)manager->getBuffer(image_size);

	// Targa is BGR, swap to RGB, add alpha and flip Y axis
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : y * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			file.read(&image_dest[write_index + 2], sizeof(uint8_t));
			file.read(&image_dest[write_index + 1], sizeof(uint8_t));
			file.read(&image_dest[write_index + 0], sizeof(uint8_t));
			if (color_mode == 4)
				file.read(&image_dest[write_index + 3], sizeof(uint8_t));
			else
				image_dest[write_index + 3] = 255;
			write_index += 4;
		}
	}
	m_BPP = 4;

	glGenTextures(1, &m_id);
	if (m_id == 0)
	{
		return false;
	}

	glBindTexture(GL_TEXTURE_2D, m_id);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, header.width, header.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_dest);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return true;
}


void Texture::addDataReference()
{
	ASSERT(!isReady() || m_data_reference > 0);
	++m_data_reference;
}


void Texture::removeDataReference()
{
	--m_data_reference;
	if (m_data_reference == 0)
	{
		m_data.clear();
	}
}


bool Texture::loadDDS(FS::IFile& file)
{
	PROFILE_FUNCTION();
	if (m_data_reference)
	{
		g_log_error.log("renderer") << "DDS texture " << m_path.c_str() << " can only be used as renderable texture";
		return false;
	}
	DDS::Header hdr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipMapCount = 0;

	file.read(&hdr, sizeof(hdr));

	m_is_cubemap = (hdr.caps2.dwCaps2 & DDS::DDSCAPS2_CUBEMAP) != 0;

	if (hdr.dwMagic != DDS::DDS_MAGIC || hdr.dwSize != 124 ||
		!(hdr.dwFlags & DDS::DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDS::DDSD_CAPS))
	{
		g_log_error.log("renderer") << "Wrong dds format or corrupted dds " << m_path.c_str();
		return false;
	}

	width = hdr.dwWidth;
	height = hdr.dwHeight;
	m_width = width;
	m_height = height;
	if ((width & (width - 1)) || (height & (height - 1)))
	{
		g_log_error.log("renderer") << "Wrong dds format " << m_path.c_str();
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
		g_log_error.log("renderer") << "Unsupported DDS format " << m_path.c_str();
		return false;
	}

	glGenTextures(1, &m_id);
	if (m_id == 0)
	{
		g_log_error.log("renderer") << "Error generating OpenGL texture " << m_path.c_str();
		return false;
	}

	TextureManager* manager = static_cast<TextureManager*>(getResourceManager().get(ResourceManager::TEXTURE));
	if (m_is_cubemap)
	{
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
	}
	else
	{
		glBindTexture(GL_TEXTURE_2D, m_id);
	}

	mipMapCount = (hdr.dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;
	if (li->compressed)
	{
		uint32_t size = Math::max(li->divSize, width) / li->divSize * Math::max(li->divSize, height) / li->divSize * li->blockBytes;
		uint8_t* data = (uint8_t*)manager->getBuffer(size);
		ASSERT(data);
		if (m_is_cubemap)
		{
			GLenum sides[] = { GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z };
			for (int i = 0; i < 6; ++i)
			{
				width = hdr.dwWidth;
				height = hdr.dwHeight;
				size = Math::max(li->divSize, width) / li->divSize * Math::max(li->divSize, height) / li->divSize * li->blockBytes;
				for (uint32_t ix = 0; ix < mipMapCount; ++ix)
				{
					file.read(data, size);
					DDS::flipCompressedTexture(width, height, li->internalFormat, data, m_allocator);
					
					glCompressedTexImage2D(sides[i], ix, li->internalFormat, width, height, 0, size, data);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					width = (width + 1) >> 1;
					height = (height + 1) >> 1;
					size = Math::max(li->divSize, width) / li->divSize * Math::max(li->divSize, height) / li->divSize * li->blockBytes;
				}
			}
		}
		else
		{
			for (uint32_t ix = 0; ix < mipMapCount; ++ix)
			{
				file.read(data, size);
				DDS::flipCompressedTexture(width, height, li->internalFormat, data, m_allocator);
				glCompressedTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, size, data);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				width = (width + 1) >> 1;
				height = (height + 1) >> 1;
				size = Math::max(li->divSize, width) / li->divSize * Math::max(li->divSize, height) / li->divSize * li->blockBytes;
			}
		}

	}
	else if (li->palette)
	{
		if ((hdr.dwFlags & DDS::DDSD_PITCH) == 0 || hdr.pixelFormat.dwRGBBitCount != 8)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer") << "Unsupported DDS format " << m_path.c_str();
			return false;
		}
		uint32_t size = hdr.dwPitchOrLinearSize * height;
		if (size != width * height * li->blockBytes)
		{
			glDeleteTextures(1, &m_id);
			g_log_error.log("renderer") << "Unsupported DDS format or corrupted DDS " << m_path.c_str();
			return false;
		}
		unsigned char * data = (unsigned char*)m_allocator.allocate(sizeof(unsigned char) * size);
		uint32_t palette[256];
		uint32_t* unpacked = (uint32_t*)manager->getBuffer(size * sizeof(uint32_t));
		file.read(palette, 4 * 256);
		for (uint32_t ix = 0; ix < mipMapCount; ++ix)
		{
			file.read(data, size);
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
		m_allocator.deallocate(data);
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
			file.read(data, size);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
			glTexImage2D(GL_TEXTURE_2D, ix, li->internalFormat, width, height, 0, li->externalFormat, li->type, data);
			width = (width+ 1) >> 1;
			height = (height + 1) >> 1;
			size = width * height * li->blockBytes;
		}
		glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
	}
	glTexParameteri(m_is_cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
	glGenerateMipmap(GL_TEXTURE_2D);

	return true;
}

void Texture::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	PROFILE_FUNCTION();
	ASSERT(file);
	if (success)
	{
		const char* path = m_path.c_str();
		size_t len = m_path.length();
		bool loaded = false;
		if (len > 3 && strcmp(path + len - 4, ".dds") == 0)
		{
			loaded = loadDDS(*file);
		}
		else if (len > 3 && strcmp(path + len - 4, ".raw") == 0)
		{
			loaded = loadRaw(*file);
		}
		else
		{
			loaded = loadTGA(*file);
		}
		if(!loaded)
		{
			g_log_warning.log("renderer") << "Error loading texture " << m_path.c_str();
			onFailure();
		}
		else
		{
			m_size = file->size();
			decrementDepCount();
		}
	}
	else
	{
		g_log_warning.log("renderer") << "Error loading texture " << m_path.c_str();
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


} // ~namespace Lumix
