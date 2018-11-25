#include "ffr.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/math_utils.h"
#include "engine/mt/sync.h"
#include <Windows.h>
#include <gl/GL.h>
#include "renderdoc_app.h"

#define FFR_GL_IMPORT(prototype, name) static prototype name;
#define FFR_GL_IMPORT_TYPEDEFS

#include "gl_ext.h"

#undef FFR_GL_IMPORT_TYPEDEFS
#undef FFR_GL_IMPORT

namespace Lumix
{


namespace ffr {

struct Buffer
{
	enum { MAX_COUNT = 8192 };
	
	GLuint handle;
};


struct Uniform
{
	enum { MAX_COUNT = 512 };

	UniformType type;
	uint count;
	void* data; 
	#ifdef _DEBUG
		StaticString<32> debug_name;
	#endif
};


struct Texture
{
	enum { MAX_COUNT = 8192 };

	GLuint handle;
	GLenum target;
};


struct Program
{
	enum { MAX_COUNT = 2048 };
	GLuint handle;

	struct {
		int loc;
		UniformHandle uniform;
	} uniforms[32];
	int uniforms_count;
};


template <typename T, int MAX_COUNT>
struct Pool
{
	void create(IAllocator& allocator)
	{
		values = (T*)allocator.allocate(sizeof(T) * MAX_COUNT);
		for(int i = 0; i < MAX_COUNT; ++i) {
			*((int*)&values[i]) = i + 1;
		}
		*((int*)&values[MAX_COUNT - 1]) = -1;	
		first_free = 0;
	}

	void destroy(IAllocator& allocator)
	{
		allocator.deallocate(values);
	}

	int alloc()
	{
		if(first_free == -1) return -1;

		const int id = first_free;
		first_free = *((int*)&values[id]);
		return id;
	}

	void dealloc(uint idx)
	{
		*((int*)&values[idx]) = first_free;
		first_free = idx;
	}

	T* values;
	int first_free;

	T& operator[](int idx) { return values[idx]; }
	bool isFull() const { return first_free == -1; }
};

static struct {
	RENDERDOC_API_1_0_2* rdoc_api;
	GLuint vao;
	IAllocator* allocator;
	void* device_context;
	Pool<Buffer, Buffer::MAX_COUNT> buffers;
	Pool<Texture, Texture::MAX_COUNT> textures;
	Pool<Uniform, Uniform::MAX_COUNT> uniforms;
	Pool<Program, Program::MAX_COUNT> programs;
	HashMap<u32, uint>* uniforms_hash_map;
	MT::SpinMutex handle_mutex;
	DWORD thread;
	int vertex_attributes = 0;
	int instance_attributes = 0;
	int max_vertex_attributes = 16;
	ProgramHandle last_program = INVALID_PROGRAM;
	u64 last_state = 0;
} g_ffr;


namespace DDS
{

static const uint DDS_MAGIC = 0x20534444; //  little-endian
static const uint DDSD_CAPS = 0x00000001;
static const uint DDSD_HEIGHT = 0x00000002;
static const uint DDSD_WIDTH = 0x00000004;
static const uint DDSD_PITCH = 0x00000008;
static const uint DDSD_PIXELFORMAT = 0x00001000;
static const uint DDSD_MIPMAPCOUNT = 0x00020000;
static const uint DDSD_LINEARSIZE = 0x00080000;
static const uint DDSD_DEPTH = 0x00800000;
static const uint DDPF_ALPHAPIXELS = 0x00000001;
static const uint DDPF_FOURCC = 0x00000004;
static const uint DDPF_INDEXED = 0x00000020;
static const uint DDPF_RGB = 0x00000040;
static const uint DDSCAPS_COMPLEX = 0x00000008;
static const uint DDSCAPS_TEXTURE = 0x00001000;
static const uint DDSCAPS_MIPMAP = 0x00400000;
static const uint DDSCAPS2_CUBEMAP = 0x00000200;
static const uint DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400;
static const uint DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800;
static const uint DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000;
static const uint DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000;
static const uint DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000;
static const uint DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000;
static const uint DDSCAPS2_VOLUME = 0x00200000;
static const uint D3DFMT_ATI1 = '1ITA';
static const uint D3DFMT_ATI2 = '2ITA';
static const uint D3DFMT_DXT1 = '1TXD';
static const uint D3DFMT_DXT2 = '2TXD';
static const uint D3DFMT_DXT3 = '3TXD';
static const uint D3DFMT_DXT4 = '4TXD';
static const uint D3DFMT_DXT5 = '5TXD';
static const uint D3DFMT_DX10 = '01XD';

enum class DxgiFormat : uint {
  UNKNOWN                     ,
  R32G32B32A32_TYPELESS       ,
  R32G32B32A32_FLOAT          ,
  R32G32B32A32_UINT           ,
  R32G32B32A32_SINT           ,
  R32G32B32_TYPELESS          ,
  R32G32B32_FLOAT             ,
  R32G32B32_UINT              ,
  R32G32B32_SINT              ,
  R16G16B16A16_TYPELESS       ,
  R16G16B16A16_FLOAT          ,
  R16G16B16A16_UNORM          ,
  R16G16B16A16_UINT           ,
  R16G16B16A16_SNORM          ,
  R16G16B16A16_SINT           ,
  R32G32_TYPELESS             ,
  R32G32_FLOAT                ,
  R32G32_UINT                 ,
  R32G32_SINT                 ,
  R32G8X24_TYPELESS           ,
  D32_FLOAT_S8X24_UINT        ,
  R32_FLOAT_X8X24_TYPELESS    ,
  X32_TYPELESS_G8X24_UINT     ,
  R10G10B10A2_TYPELESS        ,
  R10G10B10A2_UNORM           ,
  R10G10B10A2_UINT            ,
  R11G11B10_FLOAT             ,
  R8G8B8A8_TYPELESS           ,
  R8G8B8A8_UNORM              ,
  R8G8B8A8_UNORM_SRGB         ,
  R8G8B8A8_UINT               ,
  R8G8B8A8_SNORM              ,
  R8G8B8A8_SINT               ,
  R16G16_TYPELESS             ,
  R16G16_FLOAT                ,
  R16G16_UNORM                ,
  R16G16_UINT                 ,
  R16G16_SNORM                ,
  R16G16_SINT                 ,
  R32_TYPELESS                ,
  D32_FLOAT                   ,
  R32_FLOAT                   ,
  R32_UINT                    ,
  R32_SINT                    ,
  R24G8_TYPELESS              ,
  D24_UNORM_S8_UINT           ,
  R24_UNORM_X8_TYPELESS       ,
  X24_TYPELESS_G8_UINT        ,
  R8G8_TYPELESS               ,
  R8G8_UNORM                  ,
  R8G8_UINT                   ,
  R8G8_SNORM                  ,
  R8G8_SINT                   ,
  R16_TYPELESS                ,
  R16_FLOAT                   ,
  D16_UNORM                   ,
  R16_UNORM                   ,
  R16_UINT                    ,
  R16_SNORM                   ,
  R16_SINT                    ,
  R8_TYPELESS                 ,
  R8_UNORM                    ,
  R8_UINT                     ,
  R8_SNORM                    ,
  R8_SINT                     ,
  A8_UNORM                    ,
  R1_UNORM                    ,
  R9G9B9E5_SHAREDEXP          ,
  R8G8_B8G8_UNORM             ,
  G8R8_G8B8_UNORM             ,
  BC1_TYPELESS                ,
  BC1_UNORM                   ,
  BC1_UNORM_SRGB              ,
  BC2_TYPELESS                ,
  BC2_UNORM                   ,
  BC2_UNORM_SRGB              ,
  BC3_TYPELESS                ,
  BC3_UNORM                   ,
  BC3_UNORM_SRGB              ,
  BC4_TYPELESS                ,
  BC4_UNORM                   ,
  BC4_SNORM                   ,
  BC5_TYPELESS                ,
  BC5_UNORM                   ,
  BC5_SNORM                   ,
  B5G6R5_UNORM                ,
  B5G5R5A1_UNORM              ,
  B8G8R8A8_UNORM              ,
  B8G8R8X8_UNORM              ,
  R10G10B10_XR_BIAS_A2_UNORM  ,
  B8G8R8A8_TYPELESS           ,
  B8G8R8A8_UNORM_SRGB         ,
  B8G8R8X8_TYPELESS           ,
  B8G8R8X8_UNORM_SRGB         ,
  BC6H_TYPELESS               ,
  BC6H_UF16                   ,
  BC6H_SF16                   ,
  BC7_TYPELESS                ,
  BC7_UNORM                   ,
  BC7_UNORM_SRGB              ,
  AYUV                        ,
  Y410                        ,
  Y416                        ,
  NV12                        ,
  P010                        ,
  P016                        ,
  OPAQUE_420                  ,
  YUY2                        ,
  Y210                        ,
  Y216                        ,
  NV11                        ,
  AI44                        ,
  IA44                        ,
  P8                          ,
  A8P8                        ,
  B4G4R4A4_UNORM              ,
  P208                        ,
  V208                        ,
  V408                        ,
  FORCE_UINT
} ;

struct PixelFormat {
	uint dwSize;
	uint dwFlags;
	uint dwFourCC;
	uint dwRGBBitCount;
	uint dwRBitMask;
	uint dwGBitMask;
	uint dwBBitMask;
	uint dwAlphaBitMask;
};

struct Caps2 {
	uint dwCaps1;
	uint dwCaps2;
	uint dwDDSX;
	uint dwReserved;
};

struct Header {
	uint dwMagic;
	uint dwSize;
	uint dwFlags;
	uint dwHeight;
	uint dwWidth;
	uint dwPitchOrLinearSize;
	uint dwDepth;
	uint dwMipMapCount;
	uint dwReserved1[11];

	PixelFormat pixelFormat;
	Caps2 caps2;

	uint dwReserved2;
};

struct DXT10Header
{
	DxgiFormat dxgi_format;
	uint resource_dimension;
	uint misc_flag;
	uint array_size;
	uint misc_flags2;
};

struct LoadInfo {
	bool compressed;
	bool swap;
	bool palette;
	uint blockBytes;
	GLenum internalFormat;
	GLenum internalSRGBFormat;
	GLenum externalFormat;
	GLenum type;
};

static uint sizeDXTC(uint w, uint h, GLuint format) {
    const bool is_dxt1 = format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || format == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
	const bool is_ati = format == GL_COMPRESSED_RED_RGTC1;
	return ((w + 3) / 4) * ((h + 3) / 4) * (is_dxt1 || is_ati ? 8 : 16);
}

static bool isDXT1(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT1));
}

static bool isDXT10(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DX10));
}

static bool isATI1(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_ATI1));
}

static bool isATI2(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_ATI2));
}

static bool isDXT3(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT3));

}

static bool isDXT5(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT5));
}

static bool isBGRA8(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& (pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 32)
		&& (pf.dwRBitMask == 0xff0000)
		&& (pf.dwGBitMask == 0xff00)
		&& (pf.dwBBitMask == 0xff)
		&& (pf.dwAlphaBitMask == 0xff000000U));
}

static bool isBGR8(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_ALPHAPIXELS)
		&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 24)
		&& (pf.dwRBitMask == 0xff0000)
		&& (pf.dwGBitMask == 0xff00)
		&& (pf.dwBBitMask == 0xff));
}

static bool isBGR5A1(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& (pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 16)
		&& (pf.dwRBitMask == 0x00007c00)
		&& (pf.dwGBitMask == 0x000003e0)
		&& (pf.dwBBitMask == 0x0000001f)
		&& (pf.dwAlphaBitMask == 0x00008000));
}

static bool isBGR565(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 16)
		&& (pf.dwRBitMask == 0x0000f800)
		&& (pf.dwGBitMask == 0x000007e0)
		&& (pf.dwBBitMask == 0x0000001f));
}

static bool isINDEX8(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_INDEXED) && (pf.dwRGBBitCount == 8));
}

static LoadInfo loadInfoDXT1 = {
	true, false, false, 8, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
};
static LoadInfo loadInfoDXT3 = {
	true, false, false, 16, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
};
static LoadInfo loadInfoDXT5 = {
	true, false, false, 16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
};
static LoadInfo loadInfoATI1 = {
	true, false, false, 8, GL_COMPRESSED_RED_RGTC1, GL_ZERO
};
static LoadInfo loadInfoATI2 = {
	true, false, false, 16, GL_COMPRESSED_RG_RGTC2, GL_ZERO
};
static LoadInfo loadInfoBGRA8 = {
	false, false, false, 4, GL_RGBA8, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE
};
static LoadInfo loadInfoRGBA8 = {
	false, false, false, 4, GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE
};
static LoadInfo loadInfoBGR8 = {
	false, false, false, 3, GL_RGB8, GL_SRGB8, GL_BGR, GL_UNSIGNED_BYTE
};
static LoadInfo loadInfoBGR5A1 = {
	false, true, false, 2, GL_RGB5_A1, GL_ZERO, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV
};
static LoadInfo loadInfoBGR565 = {
	false, true, false, 2, GL_RGB5, GL_ZERO, GL_RGB, GL_UNSIGNED_SHORT_5_6_5
};
static LoadInfo loadInfoIndex8 = {
	false, false, true, 1, GL_RGB8, GL_SRGB8, GL_BGRA, GL_UNSIGNED_BYTE
};

static LoadInfo* getDXT10LoadInfo(const Header& hdr, const DXT10Header& dxt10_hdr)
{
	switch(dxt10_hdr.dxgi_format) {
		case DxgiFormat::B8G8R8A8_UNORM_SRGB:
			return &loadInfoBGRA8;
			break;
		case DxgiFormat::B8G8R8A8_UNORM:
			return &loadInfoBGRA8;
			break;
		case DxgiFormat::R8G8B8A8_UNORM:
			return &loadInfoRGBA8;
			break;
		case DxgiFormat::BC1_UNORM:
			return &loadInfoDXT1;
			break;
		case DxgiFormat::BC2_UNORM:
			return &loadInfoDXT3;
			break;
		case DxgiFormat::BC3_UNORM:
			return &loadInfoDXT5;
			break;
		default:
			ASSERT(false);
			return nullptr;
			break;
	}
}

struct DXTColBlock
{
	uint16_t col0;
	uint16_t col1;
	u8 row[4];
};

struct DXT3AlphaBlock
{
	uint16_t row[4];
};

struct DXT5AlphaBlock
{
	u8 alpha0;
	u8 alpha1;
	u8 row[6];
};

static LUMIX_FORCE_INLINE void swapMemory(void* mem1, void* mem2, int size)
{
	if(size < 2048)
	{
		u8 tmp[2048];
		memcpy(tmp, mem1, size);
		memcpy(mem1, mem2, size);
		memcpy(mem2, tmp, size);
	}
	else
	{
		Array<u8> tmp(*g_ffr.allocator);
		tmp.resize(size);
		memcpy(&tmp[0], mem1, size);
		memcpy(mem1, mem2, size);
		memcpy(mem2, &tmp[0], size);
	}
}

static void flipBlockDXTC1(DXTColBlock *line, int numBlocks)
{
	DXTColBlock *curblock = line;

	for (int i = 0; i < numBlocks; i++)
	{
		swapMemory(&curblock->row[0], &curblock->row[3], sizeof(u8));
		swapMemory(&curblock->row[1], &curblock->row[2], sizeof(u8));
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

		swapMemory(&curblock->row[0], &curblock->row[3], sizeof(u8));
		swapMemory(&curblock->row[1], &curblock->row[2], sizeof(u8));
		++curblock;
	}
}

static void flipDXT5Alpha(DXT5AlphaBlock *block)
{
	u8 tmp_bits[4][4];

	const uint mask = 0x00000007;
	uint bits = 0;
	memcpy(&bits, &block->row[0], sizeof(u8) * 3);

	tmp_bits[0][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[0][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[0][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[0][3] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][3] = (u8)(bits & mask);

	bits = 0;
	memcpy(&bits, &block->row[3], sizeof(u8) * 3);

	tmp_bits[2][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[2][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[2][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[2][3] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][3] = (u8)(bits & mask);

	uint *out_bits = (uint*)&block->row[0];

	*out_bits = *out_bits | (tmp_bits[3][0] << 0);
	*out_bits = *out_bits | (tmp_bits[3][1] << 3);
	*out_bits = *out_bits | (tmp_bits[3][2] << 6);
	*out_bits = *out_bits | (tmp_bits[3][3] << 9);

	*out_bits = *out_bits | (tmp_bits[2][0] << 12);
	*out_bits = *out_bits | (tmp_bits[2][1] << 15);
	*out_bits = *out_bits | (tmp_bits[2][2] << 18);
	*out_bits = *out_bits | (tmp_bits[2][3] << 21);

	out_bits = (uint*)&block->row[3];

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

		swapMemory(&curblock->row[0], &curblock->row[3], sizeof(u8));
		swapMemory(&curblock->row[1], &curblock->row[2], sizeof(u8));

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
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			blocksize = 8;
			flipBlocksFunction = &flipBlockDXTC1;
			break;
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			blocksize = 16;
			flipBlocksFunction = &flipBlockDXTC3;
			break;
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
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
	DXTColBlock *bottom = (DXTColBlock*)((u8*)surface + ((yblocks - 1) * linesize));

	while (top < bottom)
	{
		(*flipBlocksFunction)(top, xblocks);
		(*flipBlocksFunction)(bottom, xblocks);
		swapMemory(bottom, top, linesize);

		top = (DXTColBlock*)((u8*)top + linesize);
		bottom = (DXTColBlock*)((u8*)bottom - linesize);
	}
}


} // namespace DDS

#ifdef _DEBUG
	#define CHECK_GL(gl) \
		do { \
			gl; \
			GLenum err = glGetError(); \
			if (err != GL_NO_ERROR) { \
				g_log_error.log("Renderer") << "OpenGL error " << err; \
			/*	ASSERT(false);/**/ \
			} \
		} while(false)
#else
	#define CHECK_GL(gl) do { gl; } while(false)
#endif

void checkThread()
{
	ASSERT(g_ffr.thread == GetCurrentThreadId());
}


static void try_load_renderdoc()
{
	HMODULE lib = LoadLibrary("renderdoc.dll");
	if (!lib) return;
	pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(lib, "RENDERDOC_GetAPI");
	if (RENDERDOC_GetAPI) {
		RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_2, (void **)&g_ffr.rdoc_api);
		g_ffr.rdoc_api->MaskOverlayBits(~RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled, 0);
	}
	/**/
	//FreeLibrary(lib);
}


static int load_gl(void* device_contex)
{
	HDC hdc = (HDC)device_contex;
	const HGLRC dummy_context = wglCreateContext(hdc);
	wglMakeCurrent(hdc, dummy_context);

	typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC) (int interval);
	typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int *attribList);
	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
	PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
	
	#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
	#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
	#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
	#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
	#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
	#define WGL_CONTEXT_FLAGS_ARB 0x2094
	#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
	#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
	
	const int32_t contextAttrs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 5,
		#ifdef _DEBUG
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
		#endif
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0
	};
	HGLRC hglrc = wglCreateContextAttribsARB(hdc, 0, contextAttrs);
	wglMakeCurrent(hdc, hglrc);
	wglDeleteContext(dummy_context);
	// wglSwapIntervalEXT(0); // no vsync

	#define FFR_GL_IMPORT(prototype, name) \
		do { \
			name = (prototype)wglGetProcAddress(#name); \
			if (!name) { \
				g_log_error.log("Renderer") << "Failed to load GL function " #name "."; \
				return 0; \
			} \
		} while(0)

	#include "gl_ext.h"

	#undef FFR_GL_IMPORT

	return 1;
}


static int getSize(AttributeType type)
{
	switch(type) {
		case AttributeType::FLOAT: return 4;
		case AttributeType::U8: return 1;
		case AttributeType::I16: return 2;
		default: ASSERT(false); return 0;
	}
}


void VertexDecl::addAttribute(u8 components_num, AttributeType type, bool normalized, bool as_int)
{
	if((int)attributes_count >= lengthOf(attributes)) {
		ASSERT(false);
		return;
	}

	Attribute& attr = attributes[attributes_count];
	attr.components_num = components_num;
	attr.flags = as_int ? Attribute::AS_INT : 0;
	attr.flags |= normalized ? Attribute::NORMALIZED : 0;
	attr.type = type;
	attr.offset = 0;
	if(attributes_count > 0) {
		const Attribute& prev = attributes[attributes_count - 1];
		attr.offset = prev.offset + prev.components_num * getSize(prev.type);
	}
	size = attr.offset + attr.components_num * getSize(attr.type);
	hash = crc32(attributes, sizeof(Attribute) * attributes_count);
	++attributes_count;
}


void viewport(uint x,uint y,uint w,uint h)
{
	checkThread();
	glViewport(x, y, w, h);
}


void scissor(uint x,uint y,uint w,uint h)
{
	checkThread();
	glScissor(x, y, w, h);
}

int getUniformLocation(ProgramHandle program_handle, UniformHandle uniform)
{
	const Program& prg = g_ffr.programs.values[program_handle.value];
	const Uniform& u = g_ffr.uniforms[uniform.value];
	for(int i = 0; i < prg.uniforms_count; ++i) {
		const auto& pu = prg.uniforms[i];
		if (pu.uniform.value == uniform.value) {
			return pu.loc;
		}
	}
	return -1;
}

void applyUniformMatrix4f(int location, const float* value)
{
	glUniformMatrix4fv(location, 1, false, value);
}

void applyUniformMatrix4fv(int location, uint count, const float* value)
{
	glUniformMatrix4fv(location, count, false, value);
}

void applyUniformMatrix4x3f(int location, const float* value)
{
	glUniformMatrix4x3fv(location, 1, false, value);
}

void applyUniform1i(int location, int value)
{
	glUniform1i(location, value);
}

void applyUniform4i(int location, const int* value)
{
	glUniform4iv(location, 1, value);
}

void applyUniform4f(int location, const float* value)
{
	glUniform4fv(location, 1, value);
}

void applyUniformMatrix3x4f(int location, const float* value)
{
	glUniformMatrix3x4fv(location, 1, false, value);
}

void useProgram(ProgramHandle handle)
{
	if (!handle.isValid()) return;

	const Program& prg = g_ffr.programs.values[handle.value];
	if(g_ffr.last_program.value != handle.value) {
		g_ffr.last_program = handle;
		CHECK_GL(glUseProgram(prg.handle));
	}
	
	for(int i = 0; i < prg.uniforms_count; ++i) {
		const auto& pu = prg.uniforms[i];
		const Uniform& u = g_ffr.uniforms[pu.uniform.value];
		switch(u.type) {
			case UniformType::MAT4:
				glUniformMatrix4fv(pu.loc, u.count, false, (float*)u.data);
				break;
			case UniformType::MAT4X3:
				glUniformMatrix4x3fv(pu.loc, u.count, false, (float*)u.data);
				break;
			case UniformType::MAT3X4:
				glUniformMatrix3x4fv(pu.loc, u.count, false, (float*)u.data);
				break;
			case UniformType::VEC4:
				glUniform4fv(pu.loc, u.count, (float*)u.data);
				break;
			case UniformType::VEC3:
				glUniform3fv(pu.loc, u.count, (float*)u.data);
				break;
			case UniformType::VEC2:
				glUniform2fv(pu.loc, u.count, (float*)u.data);
				break;
			case UniformType::FLOAT:
				glUniform1fv(pu.loc, u.count, (float*)u.data);
				break;
			case UniformType::INT:
				glUniform1i(pu.loc, *(int*)u.data);
				break;
			case UniformType::IVEC2:
				glUniform2iv(pu.loc, u.count, (int*)u.data);
				break;
			case UniformType::IVEC4:
				glUniform4iv(pu.loc, u.count, (int*)u.data);
				break;
			default: ASSERT(false); break;
		}
	}
}


void bindTextures(const TextureHandle* handles, int count)
{
	GLuint gl_handles[64];
	ASSERT(count <= lengthOf(gl_handles));
	
	if (!handles) {
	//	CHECK_GL(glBindTextures(0, count, nullptr));
	}
	else {
		for(int i = 0; i < count; ++i) {
			if (handles[i].isValid()) {
				gl_handles[i] = g_ffr.textures[handles[i].value].handle;
			}
			else {
				gl_handles[i] = 0;
			}
		}

		CHECK_GL(glBindTextures(0, count, gl_handles));
	}
}


void setInstanceBuffer(const VertexDecl& decl, BufferHandle instance_buffer, int byte_offset, int location_offset, int* attributes_map)
{
	checkThread();
	const GLuint ib = g_ffr.buffers[instance_buffer.value].handle;
	const GLsizei stride = decl.size;

	g_ffr.instance_attributes = decl.attributes_count;
	for (uint i = 0; i < decl.attributes_count; ++i) {
		const Attribute* attr = &decl.attributes[i];
		const void* offset = (void*)(intptr_t)(attr->offset + byte_offset);
		GLenum gl_attr_type;
		switch (attr->type) {
			case AttributeType::I16: gl_attr_type = GL_SHORT; break;
			case AttributeType::FLOAT: gl_attr_type = GL_FLOAT; break;
			case AttributeType::U8: gl_attr_type = GL_UNSIGNED_BYTE; break;
		}

		const int index = attributes_map ? attributes_map[i] : location_offset + i;
		if(index >= 0) {
			CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, ib));
			CHECK_GL(glVertexAttribPointer(index, attr->components_num, gl_attr_type, attr->flags & Attribute::NORMALIZED, stride, offset));
			CHECK_GL(glVertexAttribDivisor(index, 1));  
			CHECK_GL(glEnableVertexAttribArray(index));
		}
	}
}

void setVertexBuffer(const VertexDecl* decl, BufferHandle vertex_buffer, uint buffer_offset_bytes, const int* attribute_map)
{
	for (int i = 0; i < g_ffr.max_vertex_attributes; ++i) {
		glDisableVertexAttribArray(i);
	}
	if (decl) {
		const GLsizei stride = decl->size;
		const GLuint vb = g_ffr.buffers[vertex_buffer.value].handle;
		const uint vb_offset = buffer_offset_bytes;
		g_ffr.vertex_attributes = decl->attributes_count;
		for (uint i = 0; i < decl->attributes_count; ++i) {
			const Attribute* attr = &decl->attributes[i];
			const void* offset = (void*)(intptr_t)(attr->offset + vb_offset);
			GLenum gl_attr_type;
			switch (attr->type) {
				case AttributeType::I16: gl_attr_type = GL_SHORT; break;
				case AttributeType::FLOAT: gl_attr_type = GL_FLOAT; break;
				case AttributeType::U8: gl_attr_type = GL_UNSIGNED_BYTE; break;
			}
			const int index = attribute_map ? attribute_map[i] : i;

			if(index >= 0) {
				CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, vb));
				CHECK_GL(glVertexAttribPointer(index, attr->components_num, gl_attr_type, attr->flags & Attribute::NORMALIZED, stride, offset));
				CHECK_GL(glVertexAttribDivisor(index, 0));  
				CHECK_GL(glEnableVertexAttribArray(index));
			}
		}
	}
}


void setState(u64 state)
{
	checkThread();
	
	if(state == g_ffr.last_state) return;
	g_ffr.last_state = state;

	if (state & u64(StateFlags::DEPTH_TEST)) CHECK_GL(glEnable(GL_DEPTH_TEST));
	else CHECK_GL(glDisable(GL_DEPTH_TEST));
	
	CHECK_GL(glDepthMask((state & u64(StateFlags::DEPTH_WRITE)) != 0));
	
	if (state & u64(StateFlags::SCISSOR_TEST)) CHECK_GL(glEnable(GL_SCISSOR_TEST));
	else CHECK_GL(glDisable(GL_SCISSOR_TEST));
	
	// TODO
	if (state & u64(StateFlags::CULL_BACK)) {
		CHECK_GL(glEnable(GL_CULL_FACE));
		CHECK_GL(glCullFace(GL_BACK));
	}
	else if(state & u64(StateFlags::CULL_FRONT)) {
		CHECK_GL(glEnable(GL_CULL_FACE));
		CHECK_GL(glCullFace(GL_FRONT));
	}
	else {
		CHECK_GL(glDisable(GL_CULL_FACE));
	}

	CHECK_GL(glPolygonMode(GL_FRONT_AND_BACK, state & u64(StateFlags::WIREFRAME) ? GL_LINE : GL_FILL));

	auto to_gl = [&](BlendFactors factor) -> GLenum{
		static const GLenum table[] = {
			GL_ZERO,
			GL_ONE,
			GL_SRC_COLOR,
			GL_ONE_MINUS_SRC_COLOR,
			GL_SRC_ALPHA,
			GL_ONE_MINUS_SRC_ALPHA,
			GL_DST_COLOR,
			GL_ONE_MINUS_DST_COLOR,
			GL_DST_ALPHA,
			GL_ONE_MINUS_DST_ALPHA
		};
		return table[(int)factor];
	};

	u16 blend_bits = u16(state >> 6);

	if (blend_bits) {
		const BlendFactors src_rgb = (BlendFactors)(blend_bits & 0xf);
		const BlendFactors dst_rgb = (BlendFactors)((blend_bits >> 4) & 0xf);
		const BlendFactors src_a = (BlendFactors)((blend_bits >> 8) & 0xf);
		const BlendFactors dst_a = (BlendFactors)((blend_bits >> 12) & 0xf);
		glEnable(GL_BLEND);
		glBlendFuncSeparate(to_gl(src_rgb), to_gl(dst_rgb), to_gl(src_a), to_gl(dst_a));
	}
	else {
		glDisable(GL_BLEND);
	}
	
	glStencilMask(u8(state >> 22));
	const StencilFuncs func = (StencilFuncs)((state >> 30) & 0xf);
	if (func == StencilFuncs::DISABLE) {
		glDisable(GL_STENCIL_TEST);
	}
	else {
		const u8 ref = u8(state >> 34);
		const u8 mask = u8(state >> 42);
		glEnable(GL_STENCIL_TEST);
		GLenum gl_func;
		switch(func) {
			case StencilFuncs::ALWAYS: gl_func = GL_ALWAYS; break;
			case StencilFuncs::EQUAL: gl_func = GL_EQUAL; break;
			case StencilFuncs::NOT_EQUAL: gl_func = GL_NOTEQUAL; break;
			default: ASSERT(false); break;
		}
		glStencilFunc(gl_func, ref, mask);
		auto toGLOp = [](StencilOps op) {
			const GLenum table[] = {
				GL_KEEP,
				GL_ZERO,
				GL_REPLACE,
				GL_INCR,
				GL_INCR_WRAP,
				GL_DECR,
				GL_DECR_WRAP,
				GL_INVERT
			};
			return table[(int)op];
		};
		const StencilOps sfail = StencilOps((state >> 50) & 0xf);
		const StencilOps zfail = StencilOps((state >> 54) & 0xf);
		const StencilOps zpass = StencilOps((state >> 58) & 0xf);
		glStencilOp(toGLOp(sfail), toGLOp(zfail), toGLOp(zpass));
	}
}


void setIndexBuffer(BufferHandle handle)
{
	checkThread();
	if(handle.isValid()) {	
		const GLuint ib = g_ffr.buffers[handle.value].handle;
		CHECK_GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib));
		return;
	}

	CHECK_GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}

void resetInstanceBuffer()
{
	if (g_ffr.instance_attributes == 0) return;

	for (int i = g_ffr.vertex_attributes; i < g_ffr.max_vertex_attributes; ++i) {
		glDisableVertexAttribArray(i);
	}
	g_ffr.instance_attributes = 0;
}


void drawElements(uint offset, uint count, PrimitiveType primitive_type, DataType type)
{
	checkThread();
	
	GLuint pt;
	switch (primitive_type) {
		case PrimitiveType::TRIANGLES: pt = GL_TRIANGLES; break;
		case PrimitiveType::TRIANGLE_STRIP: pt = GL_TRIANGLE_STRIP; break;
		case PrimitiveType::LINES: pt = GL_LINES; break;
		case PrimitiveType::POINTS: pt = GL_POINTS; break;
		default: ASSERT(0); break;
	} 

	GLenum t;
	int ts;
	switch(type) {
		case DataType::UINT16: t = GL_UNSIGNED_SHORT; ts = sizeof(u16); break;
		case DataType::UINT32: t = GL_UNSIGNED_INT; ts = sizeof(u32); break;
		default: ASSERT(0); break;
	}

	resetInstanceBuffer();
	CHECK_GL(glDrawElements(pt, count, t, (void*)(intptr_t)(offset * ts)));
}

void drawTrianglesInstanced(uint indices_offset, uint indices_count, uint instances_count)
{
	checkThread();
	CHECK_GL(glDrawElementsInstanced(GL_TRIANGLES, indices_count, GL_UNSIGNED_SHORT, (const void*)(intptr_t)indices_offset, instances_count));
}


void drawTriangles(uint indices_count)
{
	checkThread();

	resetInstanceBuffer();
	CHECK_GL(glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_SHORT, 0));
}


void drawTriangleStripArraysInstanced(uint offset, uint indices_count, uint instances_count)
{
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, offset, indices_count, instances_count);
}


void drawArrays(uint offset, uint count, PrimitiveType type)
{
	checkThread();
	
	GLuint pt;
	switch (type) {
		case PrimitiveType::TRIANGLES: pt = GL_TRIANGLES; break;
		case PrimitiveType::TRIANGLE_STRIP: pt = GL_TRIANGLE_STRIP; break;
		case PrimitiveType::LINES: pt = GL_LINES; break;
		case PrimitiveType::POINTS: pt = GL_POINTS; break;
		default: ASSERT(0); break;
	}

	resetInstanceBuffer();
	CHECK_GL(glDrawArrays(pt, offset, count));
}


void uniformBlockBinding(ProgramHandle program, const char* block_name, uint binding)
{
	checkThread();
	const GLuint handle = g_ffr.programs.values[program.value].handle;
	const GLint index = glGetUniformBlockIndex(handle, block_name);
	CHECK_GL(glUniformBlockBinding(handle, index, binding));
}


void bindUniformBuffer(uint index, BufferHandle buffer, size_t offset, size_t size)
{
	checkThread();
	const GLuint buf = g_ffr.buffers[buffer.value].handle;
	glBindBufferRange(GL_UNIFORM_BUFFER, index, buf, offset, size);
}


void flushBuffer(BufferHandle buffer, size_t offset, size_t len)
{
	checkThread();
	const GLuint buf = g_ffr.buffers[buffer.value].handle;
	glFlushMappedNamedBufferRange(buf, offset, len);
}


void* map(BufferHandle buffer, size_t offset, size_t size, uint flags)
{
	checkThread();
	const GLuint buf = g_ffr.buffers[buffer.value].handle;
	GLbitfield gl_flags = 0;
	if (flags & (uint)BufferFlags::MAP_READ) gl_flags |= GL_MAP_READ_BIT;
	if (flags & (uint)BufferFlags::MAP_WRITE) gl_flags |= GL_MAP_WRITE_BIT;
	if (flags & (uint)BufferFlags::PERSISTENT) gl_flags |= GL_MAP_PERSISTENT_BIT;
	if (flags & (uint)BufferFlags::COHERENT) gl_flags |= GL_MAP_COHERENT_BIT;
	if (flags & (uint)BufferFlags::MAP_FLUSH_EXPLICIT) gl_flags |= GL_MAP_FLUSH_EXPLICIT_BIT;
	return glMapNamedBufferRange(buf, offset, size, gl_flags);
}


void unmap(BufferHandle buffer)
{
	checkThread();
	const GLuint buf = g_ffr.buffers[buffer.value].handle;
	glUnmapNamedBuffer(buf);
}


void update(BufferHandle buffer, const void* data, size_t offset, size_t size)
{
	checkThread();
	const GLuint buf = g_ffr.buffers[buffer.value].handle;
	CHECK_GL(glNamedBufferSubData(buf, offset, size, data));
}


void startCapture()
{
	if (g_ffr.rdoc_api) {
		g_ffr.rdoc_api->StartFrameCapture(nullptr, nullptr);
	}
}


void stopCapture()
{
	if (g_ffr.rdoc_api) {
		g_ffr.rdoc_api->EndFrameCapture(nullptr, nullptr);
	}
}


void swapBuffers()
{
	checkThread();
	HDC hdc = (HDC)g_ffr.device_context;
	SwapBuffers(hdc);
}


void createBuffer(BufferHandle buffer, uint flags, size_t size, const void* data)
{
	checkThread();
	GLuint buf;
	CHECK_GL(glCreateBuffers(1, &buf));
	
	GLbitfield gl_flags = 0;
	if (flags & (uint)BufferFlags::MAP_READ) gl_flags |= GL_MAP_READ_BIT;
	if (flags & (uint)BufferFlags::MAP_WRITE) gl_flags |= GL_MAP_WRITE_BIT;
	if (flags & (uint)BufferFlags::PERSISTENT) gl_flags |= GL_MAP_PERSISTENT_BIT;
	if (flags & (uint)BufferFlags::COHERENT) gl_flags |= GL_MAP_COHERENT_BIT;
	if (flags & (uint)BufferFlags::DYNAMIC_STORAGE) gl_flags |= GL_DYNAMIC_STORAGE_BIT;
	CHECK_GL(glNamedBufferStorage(buf, size, data, gl_flags));

	g_ffr.buffers[buffer.value].handle = buf;
}


void destroy(ProgramHandle program)
{
	checkThread();
	
	Program& p = g_ffr.programs[program.value];
	const GLuint handle = p.handle;
	CHECK_GL(glDeleteProgram(handle));

	MT::SpinLock lock(g_ffr.handle_mutex);
	g_ffr.programs.dealloc(program.value);
}

static struct {
	TextureFormat format;
	GLenum gl_internal;
	GLenum gl_format;
	GLenum type;
} s_texture_formats[] =
{ 
	{TextureFormat::D24, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
	{TextureFormat::D24S8, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
	{TextureFormat::D32, GL_DEPTH_COMPONENT32, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
	
	{TextureFormat::SRGB, GL_SRGB8, GL_RGBA, GL_UNSIGNED_BYTE},
	{TextureFormat::SRGBA, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE},
	{TextureFormat::RGBA8, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},
	{TextureFormat::RGBA16, GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT},
	{TextureFormat::RGBA16F, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT},
	{TextureFormat::R16F, GL_R16F, GL_RED, GL_HALF_FLOAT},
	{TextureFormat::R16, GL_R16, GL_RED, GL_UNSIGNED_SHORT},
	{TextureFormat::R32F, GL_R32F, GL_RED, GL_FLOAT}
};


TextureInfo getTextureInfo(const void* data)
{
	TextureInfo info;

	const DDS::Header* hdr = (const DDS::Header*)data;
	info.width = hdr->dwWidth;
	info.height = hdr->dwHeight;
	info.is_cubemap = (hdr->caps2.dwCaps2 & DDS::DDSCAPS2_CUBEMAP) != 0;
	info.mips = (hdr->dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr->dwMipMapCount : 1;
	info.depth = (hdr->dwFlags & DDS::DDSD_DEPTH) ? hdr->dwDepth : 1;
	
	if (isDXT10(hdr->pixelFormat)) {
		const DDS::DXT10Header* hdr_dxt10 = (const DDS::DXT10Header*)((const u8*)data + sizeof(DDS::Header));
		info.layers = hdr_dxt10->array_size;
	}
	else {
		info.layers = 1;
	}
	
	return info;
}


bool loadTexture(TextureHandle handle, const void* input, int input_size, uint flags, const char* debug_name)
{
	ASSERT(debug_name && debug_name[0]);
	checkThread();
	DDS::Header hdr;

	InputBlob blob(input, input_size);
	blob.read(&hdr, sizeof(hdr));

	if (hdr.dwMagic != DDS::DDS_MAGIC || hdr.dwSize != 124 ||
		!(hdr.dwFlags & DDS::DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDS::DDSD_CAPS))
	{
		g_log_error.log("renderer") << "Wrong dds format or corrupted dds.";
		return false;
	}

	DDS::LoadInfo* li;
	int layers = 1;

	if (isDXT1(hdr.pixelFormat)) {
		li = &DDS::loadInfoDXT1;
	}
	else if (isDXT3(hdr.pixelFormat)) {
		li = &DDS::loadInfoDXT3;
	}
	else if (isDXT5(hdr.pixelFormat)) {
		li = &DDS::loadInfoDXT5;
	}
	else if (isATI1(hdr.pixelFormat)) {
		li = &DDS::loadInfoATI1;
	}
	else if (isATI2(hdr.pixelFormat)) {
		li = &DDS::loadInfoATI2;
	}
	else if (isBGRA8(hdr.pixelFormat)) {
		li = &DDS::loadInfoBGRA8;
	}
	else if (isBGR8(hdr.pixelFormat)) {
		li = &DDS::loadInfoBGR8;
	}
	else if (isBGR5A1(hdr.pixelFormat)) {
		li = &DDS::loadInfoBGR5A1;
	}
	else if (isBGR565(hdr.pixelFormat)) {
		li = &DDS::loadInfoBGR565;
	}
	else if (isINDEX8(hdr.pixelFormat)) {
		li = &DDS::loadInfoIndex8;
	}
	else if (isDXT10(hdr.pixelFormat)) {
		DDS::DXT10Header dxt10_hdr;
		blob.read(dxt10_hdr);
		li = DDS::getDXT10LoadInfo(hdr, dxt10_hdr);
		layers = dxt10_hdr.array_size;
	}
	else {
		ASSERT(false);
		return false;
	}

	const bool is_cubemap = (hdr.caps2.dwCaps2 & DDS::DDSCAPS2_CUBEMAP) != 0;

	const GLenum texture_target = is_cubemap ? GL_TEXTURE_CUBE_MAP : layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	const bool is_srgb = flags & (u32)TextureFlags::SRGB;
	const GLenum internal_format = is_srgb ? li->internalSRGBFormat : li->internalFormat;
	const uint mipMapCount = (hdr.dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;

	GLuint texture;
	CHECK_GL(glCreateTextures(texture_target, 1, &texture));
	if (texture == 0) {
		return false;
	}
	if(layers > 1) {
		CHECK_GL(glTextureStorage3D(texture, mipMapCount, internal_format, hdr.dwWidth, hdr.dwHeight, layers));
	}
	else {
		CHECK_GL(glTextureStorage2D(texture, mipMapCount, internal_format, hdr.dwWidth, hdr.dwHeight));
	}
	if (debug_name && debug_name[0]) {
		CHECK_GL(glObjectLabel(GL_TEXTURE, texture, stringLength(debug_name), debug_name));
	}

	for (int layer = 0; layer < layers; ++layer) {
		for(int side = 0; side < (is_cubemap ? 6 : 1); ++side) {
			const GLenum tex_img_target =  is_cubemap ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + side : layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
			uint width = hdr.dwWidth;
			uint height = hdr.dwHeight;

			if (li->compressed) {
				uint size = DDS::sizeDXTC(width, height, internal_format);
				if (size != hdr.dwPitchOrLinearSize || (hdr.dwFlags & DDS::DDSD_LINEARSIZE) == 0) {
					CHECK_GL(glDeleteTextures(1, &texture));
					return false;
				}
				Array<u8> data(*g_ffr.allocator);
				data.resize(size);
				for (uint mip = 0; mip < mipMapCount; ++mip) {
					blob.read(&data[0], size);
					//DDS::flipCompressedTexture(width, height, internal_format, &data[0]);
					if(layers > 1) {
						CHECK_GL(glCompressedTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, internal_format, size, &data[0]));
					}
					else if (is_cubemap) {
						ASSERT(layer == 0);
						CHECK_GL(glCompressedTextureSubImage3D(texture, mip, 0, 0, side, width, height, 1, internal_format, size, &data[0]));
					}
					else {
						CHECK_GL(glCompressedTextureSubImage2D(texture, mip, 0, 0, width, height, internal_format, size, &data[0]));
					}
					CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
					CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
					width = Math::maximum(1, width >> 1);
					height = Math::maximum(1, height >> 1);
					size = DDS::sizeDXTC(width, height, internal_format);
				}
			}
			else if (li->palette) {
				if ((hdr.dwFlags & DDS::DDSD_PITCH) == 0 || hdr.pixelFormat.dwRGBBitCount != 8) {
					CHECK_GL(glDeleteTextures(1, &texture));
					return false;
				}
				uint size = hdr.dwPitchOrLinearSize * height;
				if (size != width * height * li->blockBytes) {
					CHECK_GL(glDeleteTextures(1, &texture));
					return false;
				}
				Array<u8> data(*g_ffr.allocator);
				data.resize(size);
				uint palette[256];
				Array<uint> unpacked(*g_ffr.allocator);
				unpacked.resize(size);
				blob.read(palette, 4 * 256);
				for (uint mip = 0; mip < mipMapCount; ++mip) {
					blob.read(&data[0], size);
					for (uint zz = 0; zz < size; ++zz) {
						unpacked[zz] = palette[data[zz]];
					}
					//glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
					if(layers > 1) {
						CHECK_GL(glTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, li->externalFormat, li->type, &unpacked[0]));
					}
					else {
						CHECK_GL(glTextureSubImage2D(texture, mip, 0, 0, width, height, li->externalFormat, li->type, &unpacked[0]));
					}
					width = Math::maximum(1, width >> 1);
					height = Math::maximum(1, height >> 1);
					size = width * height * li->blockBytes;
				}
			}
			else {
				if (li->swap) {
					CHECK_GL(glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE));
				}
				uint size = width * height * li->blockBytes;
				Array<u8> data(*g_ffr.allocator);
				data.resize(size);
				for (uint mip = 0; mip < mipMapCount; ++mip) {
					blob.read(&data[0], size);
					//glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
					if (layers > 1) {
						CHECK_GL(glTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, li->externalFormat, li->type, &data[0]));
					}
					else {
						CHECK_GL(glTextureSubImage2D(texture, mip, 0, 0, width, height, li->externalFormat, li->type, &data[0]));
					}
					width = Math::maximum(1, width >> 1);
					height = Math::maximum(1, height >> 1);
					size = width * height * li->blockBytes;
				}
				CHECK_GL(glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE));
			}
			CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1));
		}
	}

	const GLint wrap = (flags & (u32)TextureFlags::CLAMP) ? GL_CLAMP : GL_REPEAT;
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap));
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap));

	Texture& t = g_ffr.textures[handle.value];
	t.handle = texture;
	t.target = is_cubemap ? GL_TEXTURE_CUBE_MAP : layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	return true;
}

ProgramHandle allocProgramHandle()
{
	MT::SpinLock lock(g_ffr.handle_mutex);

	if(g_ffr.programs.isFull()) {
		g_log_error.log("Renderer") << "FFR is out of free program slots.";
		return INVALID_PROGRAM;
	}
	const int id = g_ffr.programs.alloc();
	Program& p = g_ffr.programs[id];
	p.handle = 0;
	return { (uint)id };
}


BufferHandle allocBufferHandle()
{
	MT::SpinLock lock(g_ffr.handle_mutex);

	if(g_ffr.buffers.isFull()) {
		g_log_error.log("Renderer") << "FFR is out of free buffer slots.";
		return INVALID_BUFFER;
	}
	const int id = g_ffr.buffers.alloc();
	Buffer& t = g_ffr.buffers[id];
	t.handle = 0;
	return { (uint)id };
}


TextureHandle allocTextureHandle()
{
	MT::SpinLock lock(g_ffr.handle_mutex);

	if(g_ffr.textures.isFull()) {
		g_log_error.log("Renderer") << "FFR is out of free texture slots.";
		return INVALID_TEXTURE;
	}
	const int id = g_ffr.textures.alloc();
	Texture& t = g_ffr.textures[id];
	t.handle = 0;
	return { (uint)id };
}


bool createTexture(TextureHandle handle, uint w, uint h, uint depth, TextureFormat format, uint flags, const void* data, const char* debug_name)
{
	checkThread();
	const bool is_srgb = flags & (u32)TextureFlags::SRGB;
	ASSERT(!is_srgb); // use format argument to enable srgb
	ASSERT(debug_name && debug_name[0]);

	GLuint texture;
	int found_format = 0;
	const GLenum target = depth <= 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY;
	if(depth <= 1) {
		CHECK_GL(glGenTextures(1, &texture));
		CHECK_GL(glBindTexture(target, texture));
		for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
			if(s_texture_formats[i].format == format) {
				CHECK_GL(glTexImage2D(target
					, 0
					, s_texture_formats[i].gl_internal
					, w
					, h
					, 0
					, s_texture_formats[i].gl_format
					, s_texture_formats[i].type
					, data));
				found_format = 1;
				break;
			}
		}
	}
	else {
		CHECK_GL(glGenTextures(1, &texture));
		CHECK_GL(glBindTexture(target, texture));
		for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
			if(s_texture_formats[i].format == format) {
				CHECK_GL(glTexImage3D(target
					, 0
					, s_texture_formats[i].gl_internal
					, w
					, h
					, depth
					, 0
					, s_texture_formats[i].gl_format
					, s_texture_formats[i].type
					, data));
				found_format = 1;
				break;
			}
		}
	}

	if(!found_format) {
		CHECK_GL(glBindTexture(target, 0));
		CHECK_GL(glDeleteTextures(1, &texture));
		return false;	
	}

	if(debug_name && debug_name[0]) {
		CHECK_GL(glObjectLabel(GL_TEXTURE, texture, stringLength(debug_name), debug_name));
	}
	CHECK_GL(glGenerateMipmap(target));
	
	const GLint wrap = (flags & (u32)TextureFlags::CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	CHECK_GL(glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap));
	CHECK_GL(glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap));
	CHECK_GL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	CHECK_GL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));

	CHECK_GL(glBindTexture(target, 0));

	Texture& t = g_ffr.textures[handle.value];
	t.handle = texture;
	t.target = target;

	return true;
}


void destroy(TextureHandle texture)
{
	checkThread();
	Texture& t = g_ffr.textures[texture.value];
	const GLuint handle = t.handle;
	CHECK_GL(glDeleteTextures(1, &handle));

	MT::SpinLock lock(g_ffr.handle_mutex);
	g_ffr.textures.dealloc(texture.value);
}


void destroy(BufferHandle buffer)
{
	checkThread();
	
	Buffer& t = g_ffr.buffers[buffer.value];
	const GLuint handle = t.handle;
	CHECK_GL(glDeleteBuffers(1, &handle));

	MT::SpinLock lock(g_ffr.handle_mutex);
	g_ffr.buffers.dealloc(buffer.value);
}


void clear(uint flags, const float* color, float depth)
{
	CHECK_GL(glDisable(GL_SCISSOR_TEST));
	CHECK_GL(glDisable(GL_BLEND));
	g_ffr.last_state &= ~u64(0xffFF << 6);
	checkThread();
	GLbitfield gl_flags = 0;
	if (flags & (uint)ClearFlags::COLOR) {
		CHECK_GL(glClearColor(color[0], color[1], color[2], color[3]));
		gl_flags |= GL_COLOR_BUFFER_BIT;
	}
	if (flags & (uint)ClearFlags::DEPTH) {
		CHECK_GL(glDepthMask(GL_TRUE));
		CHECK_GL(glClearDepth(depth));
		gl_flags |= GL_DEPTH_BUFFER_BIT;
	}
	if (flags & (uint)ClearFlags::STENCIL) {
		glStencilMask(0xff);
		g_ffr.last_state = g_ffr.last_state | (0xff << 22);
		glClearStencil(0);
		gl_flags |= GL_STENCIL_BUFFER_BIT;
	}
	CHECK_GL(glUseProgram(0));
	g_ffr.last_program = INVALID_PROGRAM;
	CHECK_GL(glClear(gl_flags));
}

static const char* shaderTypeToString(ShaderType type)
{
	switch(type) {
		case ShaderType::GEOMETRY: return "geometry shader";		
		case ShaderType::FRAGMENT: return "fragment shader";
		case ShaderType::VERTEX: return "vertex shader";
		default: return "unknown shader type";
	}
}


static uint getSize(UniformType type)
{
	switch(type)
	{
	case UniformType::INT: return sizeof(int);
	case UniformType::FLOAT: return sizeof(float);
	case UniformType::IVEC2: return sizeof(int) * 2;
	case UniformType::IVEC4: return sizeof(int) * 4;
	case UniformType::VEC2: return sizeof(float) * 2;
	case UniformType::VEC3: return sizeof(float) * 3;
	case UniformType::VEC4: return sizeof(float) * 4;
	case UniformType::MAT4: return sizeof(float) * 16;
	case UniformType::MAT4X3: return sizeof(float) * 12;
	case UniformType::MAT3X4: return sizeof(float) * 12;
	default:
		ASSERT(false);
		return 4;
	}
}


UniformHandle allocUniform(const char* name, UniformType type, int count)
{
	const u32 name_hash = crc32(name);
	
	MT::SpinLock lock(g_ffr.handle_mutex);

	auto iter = g_ffr.uniforms_hash_map->find(name_hash);
	if(iter.isValid()) {
		return { iter.value() };
	}

	if(g_ffr.uniforms.isFull()) {
		g_log_error.log("Renderer") << "FFR is out of free uniform slots.";
		return INVALID_UNIFORM;
	}
	const int id = g_ffr.uniforms.alloc();
	Uniform& u = g_ffr.uniforms[id];
	u.count = count;
	u.type = type;
	#ifdef _DEBUG
		u.debug_name = name;
	#endif
	size_t byte_size = getSize(type) * count;
	u.data = g_ffr.allocator->allocate(byte_size);
	setMemory(u.data, 0, byte_size);
	g_ffr.uniforms_hash_map->insert(name_hash, id);
	return { (uint)id };
}


bool createProgram(ProgramHandle prog, const char** srcs, const ShaderType* types, int num, const char** prefixes, int prefixes_count, const char* name)
{
	checkThread();

	const char* combined_srcs[16];
	ASSERT(prefixes_count < lengthOf(combined_srcs) - 1); 
	enum { MAX_SHADERS_PER_PROGRAM = 16 };

	if (num > MAX_SHADERS_PER_PROGRAM) {
		g_log_error.log("Renderer") << "Too many shaders per program in " << name;
		return false;
	}

	const GLuint prg = glCreateProgram();

	for (int i = 0; i < num; ++i) {
		GLenum shader_type;
		switch (types[i]) {
			case ShaderType::GEOMETRY: shader_type = GL_GEOMETRY_SHADER; break;
			case ShaderType::FRAGMENT: shader_type = GL_FRAGMENT_SHADER; break;
			case ShaderType::VERTEX: shader_type = GL_VERTEX_SHADER; break;
			default: ASSERT(0); break;
		}
		const GLuint shd = glCreateShader(shader_type);
		combined_srcs[prefixes_count] = srcs[i];
		for (int j = 0; j < prefixes_count; ++j) {
			combined_srcs[j] = prefixes[j];
		}

		CHECK_GL(glShaderSource(shd, 1 + prefixes_count, combined_srcs, 0));
		CHECK_GL(glCompileShader(shd));

		GLint compile_status;
		CHECK_GL(glGetShaderiv(shd, GL_COMPILE_STATUS, &compile_status));
		if (compile_status == GL_FALSE) {
			GLint log_len = 0;
			CHECK_GL(glGetShaderiv(shd, GL_INFO_LOG_LENGTH, &log_len));
			if (log_len > 0) {
				Array<char> log_buf(*g_ffr.allocator);
				log_buf.resize(log_len);
				CHECK_GL(glGetShaderInfoLog(shd, log_len, &log_len, &log_buf[0]));
				g_log_error.log("Renderer") << name << " - " << shaderTypeToString(types[i]) << ": " << &log_buf[0];
			}
			else {
				g_log_error.log("Renderer") << "Failed to compile shader " << name << " - " << shaderTypeToString(types[i]);
			}
			CHECK_GL(glDeleteShader(shd));
			return false;
		}

		CHECK_GL(glAttachShader(prg, shd));
		CHECK_GL(glDeleteShader(shd));
	}

	CHECK_GL(glLinkProgram(prg));
	GLint linked;
	CHECK_GL(glGetProgramiv(prg, GL_LINK_STATUS, &linked));

	if (linked == GL_FALSE) {
		GLint log_len = 0;
		CHECK_GL(glGetProgramiv(prg, GL_INFO_LOG_LENGTH, &log_len));
		if (log_len > 0) {
			Array<char> log_buf(*g_ffr.allocator);
			log_buf.resize(log_len);
			CHECK_GL(glGetProgramInfoLog(prg, log_len, &log_len, &log_buf[0]));
			g_log_error.log("Renderer") << name << ": " << &log_buf[0];
		}
		else {
			g_log_error.log("Renderer") << "Failed to link program " << name;
		}
		CHECK_GL(glDeleteProgram(prg));
		return false;
	}

	const int id = prog.value;
	g_ffr.programs[id].handle = prg;
	GLint uniforms_count;
	CHECK_GL(glGetProgramiv(prg, GL_ACTIVE_UNIFORMS, &uniforms_count));
	if(uniforms_count > lengthOf(g_ffr.programs[id].uniforms)) {
		uniforms_count = lengthOf(g_ffr.programs[id].uniforms);
		g_log_error.log("Renderer") << "Too many uniforms per program, not all will be used.";
	}
	g_ffr.programs[id].uniforms_count = 0;
	for(int i = 0; i < uniforms_count; ++i) {
		char name[32];
		GLint size;
		GLenum type;
		UniformType ffr_type;
		glGetActiveUniform(prg, i, sizeof(name), nullptr, &size, &type, name);
		switch(type) {
			case GL_SAMPLER_CUBE:
			case GL_SAMPLER_2D_ARRAY:
			case GL_SAMPLER_2D:
			case GL_SAMPLER_3D: continue;
			case GL_INT: ffr_type = UniformType::INT; break;
			case GL_FLOAT: ffr_type = UniformType::FLOAT; break;
			case GL_FLOAT_VEC2: ffr_type = UniformType::VEC2; break;
			case GL_FLOAT_VEC3: ffr_type = UniformType::VEC3; break;
			case GL_FLOAT_VEC4: ffr_type = UniformType::VEC4; break;
			case GL_FLOAT_MAT4: ffr_type = UniformType::MAT4; break;
			case GL_FLOAT_MAT4x3: ffr_type = UniformType::MAT4X3; break;
			case GL_FLOAT_MAT3x4: ffr_type = UniformType::MAT3X4; break;
			case GL_INT_VEC2: ffr_type = UniformType::IVEC2; break;
			case GL_INT_VEC4: ffr_type = UniformType::IVEC4; break;
			default: ASSERT(false); ffr_type = UniformType::VEC4; break;
		}

		if (size > 1) {
			name[stringLength(name) - 3] = '\0';
		}
		const int loc = glGetUniformLocation(prg, name);

		if(loc >= 0) {
			auto& u = g_ffr.programs[id].uniforms[g_ffr.programs[id].uniforms_count];
			u.loc = loc;
			u.uniform = allocUniform(name, ffr_type, size);
			++g_ffr.programs[id].uniforms_count;
		}
	}

	return true;
}


static void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam)
{
	if(GL_DEBUG_TYPE_PUSH_GROUP == type || type == GL_DEBUG_TYPE_POP_GROUP) return;
	if (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_PERFORMANCE) {
		g_log_error.log("GL") << message;
	}
	else {
		g_log_info.log("GL") << message;
	}
}


void preinit(IAllocator& allocator)
{
	try_load_renderdoc();
	g_ffr.allocator = &allocator;
	g_ffr.textures.create(*g_ffr.allocator);
	g_ffr.buffers.create(*g_ffr.allocator);
	g_ffr.uniforms.create(*g_ffr.allocator);
	g_ffr.programs.create(*g_ffr.allocator);
	g_ffr.uniforms_hash_map = LUMIX_NEW(*g_ffr.allocator, HashMap<u32, uint>)(*g_ffr.allocator);
}


bool init(void* window_handle)
{
	g_ffr.device_context = GetDC((HWND)window_handle);
	g_ffr.thread = GetCurrentThreadId();

	if (!load_gl(g_ffr.device_context)) return false;

	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &g_ffr.max_vertex_attributes);

/*	int extensions_count;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensions_count);
	for(int i = 0; i < extensions_count; ++i) {
		const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
		OutputDebugString(ext);
		OutputDebugString("\n");
	}
	const unsigned char* extensions = glGetString(GL_EXTENSIONS);
	const unsigned char* version = glGetString(GL_VERSION);*/

	CHECK_GL(glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE));
	CHECK_GL(glDepthFunc(GL_GREATER));

	#ifdef _DEBUG
		CHECK_GL(glEnable(GL_DEBUG_OUTPUT));
		CHECK_GL(glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS));
		CHECK_GL(glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE));
		CHECK_GL(glDebugMessageCallback(gl_debug_callback, 0));
	#endif

	CHECK_GL(glGenVertexArrays(1, &g_ffr.vao));
	CHECK_GL(glBindVertexArray(g_ffr.vao));

	CHECK_GL(glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS));

	g_ffr.last_state = 1;
	setState(0);

	return true;
}


bool isHomogenousDepth() { return false; }


bool isOriginBottomLeft() { return true; }


void generateMipmaps(ffr::TextureHandle texture)
{
	checkThread();

	Texture& t = g_ffr.textures[texture.value];
	const GLuint handle = t.handle;

	glGenerateTextureMipmap(handle);
}


void getTextureImage(ffr::TextureHandle texture, uint size, void* buf)
{
	checkThread();

	Texture& t = g_ffr.textures[texture.value];
	const GLuint handle = t.handle;

	CHECK_GL(glGetTextureImage(handle, 0, GL_RGBA, GL_UNSIGNED_BYTE, size, buf));
}


void popDebugGroup()
{
	checkThread();
	CHECK_GL(glPopDebugGroup());
}


void pushDebugGroup(const char* msg)
{
	checkThread();
	CHECK_GL(glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, msg));
}


void destroy(FramebufferHandle fb)
{
	checkThread();
	CHECK_GL(glDeleteFramebuffers(1, &fb.value));
}


int getAttribLocation(ProgramHandle program, const char* uniform_name)
{
	checkThread();
	return glGetAttribLocation(g_ffr.programs.values[program.value].handle, uniform_name);
}


void setUniform1i(UniformHandle uniform, int value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::INT);
	memcpy(g_ffr.uniforms[uniform.value].data, &value, sizeof(value)); 
}


void setUniform2f(UniformHandle uniform, const float* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::VEC2);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 2); 
}


void setUniform4f(UniformHandle uniform, const float* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::VEC4);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 4); 
}


void setUniform4i(UniformHandle uniform, const int* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::IVEC4);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 4); 
}


void setUniform3f(UniformHandle uniform, const float* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::VEC3);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 3); 
}


void setUniformMatrix4f(UniformHandle uniform, const float* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::MAT4);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 16); 
}


void setUniformMatrix4x3f(UniformHandle uniform, const float* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::MAT4X3);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 12); 
}


void setUniformMatrix3x4f(UniformHandle uniform, const float* value)
{
	checkThread();
	ASSERT(g_ffr.uniforms[uniform.value].type == UniformType::MAT3X4);
	memcpy(g_ffr.uniforms[uniform.value].data, value, sizeof(value[0]) * 12); 
}

void bindLayer(FramebufferHandle fb, TextureHandle rb, uint layer)
{
	checkThread();

	int color_attachment_idx = 0;
	bool depth_bound = false;
	const GLuint t = g_ffr.textures[rb.value].handle;
	CHECK_GL(glBindTexture(GL_TEXTURE_2D_ARRAY, t));
	GLint internal_format;
	CHECK_GL(glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format));

	CHECK_GL(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
	switch(internal_format) {
		case GL_DEPTH24_STENCIL8:
			CHECK_GL(glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0));
			CHECK_GL(glNamedFramebufferTextureLayer(fb.value, GL_DEPTH_STENCIL_ATTACHMENT, t, 0, layer));
			depth_bound = true;
			break;
		case GL_DEPTH_COMPONENT24:
		case GL_DEPTH_COMPONENT32:
			CHECK_GL(glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0));
			CHECK_GL(glNamedFramebufferTextureLayer(fb.value, GL_DEPTH_ATTACHMENT, t, 0, layer));
			depth_bound = true;
			break;
		default:
			CHECK_GL(glNamedFramebufferTextureLayer(fb.value, GL_COLOR_ATTACHMENT0 + color_attachment_idx, t, 0, layer));
			++color_attachment_idx;
			break;
	}

	GLint max_attachments = 0;
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
	for(int i = color_attachment_idx; i < max_attachments; ++i) {
		glNamedFramebufferRenderbuffer(fb.value, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, 0);
	}
	
	if (!depth_bound) {
		glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
		glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fb.value);
	auto xx = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	ASSERT(xx == GL_FRAMEBUFFER_COMPLETE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void update(FramebufferHandle fb, uint renderbuffers_count, const TextureHandle* renderbuffers)
{
	checkThread();

	int color_attachment_idx = 0;
	bool depth_bound = false;
	for (uint i = 0; i < renderbuffers_count; ++i) {
		const GLuint t = g_ffr.textures[renderbuffers[i].value].handle;
		CHECK_GL(glBindTexture(GL_TEXTURE_2D, t));
		GLint internal_format;
		CHECK_GL(glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format));

		CHECK_GL(glBindTexture(GL_TEXTURE_2D, 0));
		switch(internal_format) {
			case GL_DEPTH24_STENCIL8:
				CHECK_GL(glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0));
				CHECK_GL(glNamedFramebufferTexture(fb.value, GL_DEPTH_STENCIL_ATTACHMENT, t, 0));
				depth_bound = true;
				break;
			case GL_DEPTH_COMPONENT24:
			case GL_DEPTH_COMPONENT32:
				CHECK_GL(glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0));
				CHECK_GL(glNamedFramebufferTexture(fb.value, GL_DEPTH_ATTACHMENT, t, 0));
				depth_bound = true;
				break;
			default:
				CHECK_GL(glNamedFramebufferTexture(fb.value, GL_COLOR_ATTACHMENT0 + color_attachment_idx, t, 0));
				++color_attachment_idx;
				break;
		}
	}

	GLint max_attachments = 0;
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
	for(int i = color_attachment_idx; i < max_attachments; ++i) {
		glNamedFramebufferRenderbuffer(fb.value, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, 0);
	}
	
	if (!depth_bound) {
		glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
		glNamedFramebufferRenderbuffer(fb.value, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fb.value);
	auto xx = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	ASSERT(xx == GL_FRAMEBUFFER_COMPLETE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


QueryHandle createQuery()
{
	GLuint q;
	CHECK_GL(glGenQueries(1, &q));
	return {q};
}


u64 getQueryResult(QueryHandle query)
{
	u64 time;
	glGetQueryObjectui64v(query.value, GL_QUERY_RESULT, &time);
	return time;
}


void destroy(QueryHandle query)
{
	glDeleteQueries(1, &query.value);
}


void queryTimestamp(QueryHandle query)
{
	glQueryCounter(query.value, GL_TIMESTAMP);
}


FramebufferHandle createFramebuffer()
{
	checkThread();
	GLuint fb;
	CHECK_GL(glCreateFramebuffers(1, &fb));
	return {fb};
}


void setFramebuffer(FramebufferHandle fb, bool srgb)
{
	checkThread();
	if(fb.value == 0xffFFffFF) {
		CHECK_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	}
	else {
		CHECK_GL(glBindFramebuffer(GL_FRAMEBUFFER, fb.value));
		const GLenum db[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
		CHECK_GL(glDrawBuffers(3, db));
	}
	if(srgb) {	
		CHECK_GL(glEnable(GL_FRAMEBUFFER_SRGB));
	}
	else {
		CHECK_GL(glDisable(GL_FRAMEBUFFER_SRGB));
	}
}


void shutdown()
{
	checkThread();
	g_ffr.textures.destroy(*g_ffr.allocator);
	g_ffr.buffers.destroy(*g_ffr.allocator);
	for (uint u : *g_ffr.uniforms_hash_map) {
		g_ffr.allocator->deallocate(g_ffr.uniforms[u].data);
	}
	g_ffr.uniforms.destroy(*g_ffr.allocator);
	g_ffr.programs.destroy(*g_ffr.allocator);
	LUMIX_DELETE(*g_ffr.allocator, g_ffr.uniforms_hash_map);
}

} // ns ffr 

} // ns Lumix