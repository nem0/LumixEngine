#include "gpu.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/sync.h"
#include "engine/os.h"
#include "engine/stream.h"
#ifdef _WIN32
	#include <Windows.h>
#endif
#ifdef __linux__
	#include <X11/Xlib.h>
	#define GLX_GLXEXT_LEGACY
	#include <GL/glx.h>
	#include <GL/glxext.h>
#endif
#include <GL/gl.h>
#include "renderdoc_app.h"

namespace Lumix {

namespace gpu {

#define GPU_GL_IMPORT(prototype, name) static prototype name;
#define GPU_GL_IMPORT_TYPEDEFS

#include "gl_ext.h"

#undef GPU_GL_IMPORT_TYPEDEFS
#undef GPU_GL_IMPORT

struct Buffer
{
	enum { MAX_COUNT = 8192 };
	
	GLuint handle;
	u32 flags;
};

struct BufferGroup
{
	enum { MAX_COUNT = 64 };
	
	GLuint handle;
	u32 flags;
	size_t element_size;
	size_t elements_count;
};


struct Texture
{
	enum { MAX_COUNT = 8192 };

	GLuint handle;
	GLenum target;
	GLenum format;
	u32 width;
	u32 height;
};


struct Program
{
	enum { MAX_COUNT = 2048 };
	GLuint handle;
	VertexDecl decl;
};


template <typename T, u32 MAX_COUNT>
struct Pool
{
	void init()
	{
		values = (T*)mem;
		for (int i = 0; i < MAX_COUNT; ++i) {
			new (NewPlaceholder(), &values[i]) int(i + 1);
		}
		new (NewPlaceholder(), &values[MAX_COUNT - 1]) int(-1);
		first_free = 0;
	}

	int alloc()
	{
		if(first_free == -1) return -1;

		const int id = first_free;
		first_free = *((int*)&values[id]);
		new (NewPlaceholder(), &values[id]) T;
		return id;
	}

	void dealloc(u32 idx)
	{
		values[idx].~T();
		new (NewPlaceholder(), &values[idx]) int(first_free);
		first_free = idx;
	}

	alignas(T) u8 mem[sizeof(T) * MAX_COUNT];
	T* values;
	int first_free;

	T& operator[](int idx) { return values[idx]; }
	bool isFull() const { return first_free == -1; }
};

struct WindowContext {
	void* window_handle = nullptr;
	GLuint vao;
#ifdef _WIN32
	HDC device_context;
	HGLRC hglrc;
#endif
};

static struct {
	RENDERDOC_API_1_0_2* rdoc_api;
	IAllocator* allocator;
	WindowContext contexts[64];
	Pool<Buffer, Buffer::MAX_COUNT> buffers;
	Pool<BufferGroup, BufferGroup::MAX_COUNT> buffer_groups;
	Pool<Texture, Texture::MAX_COUNT> textures;
	Pool<Program, Program::MAX_COUNT> programs;
	Mutex handle_mutex;
	Lumix::OS::ThreadID thread;
	int instance_attributes = 0;
	int max_vertex_attributes = 16;
	ProgramHandle last_program = INVALID_PROGRAM;
	u64 last_state = 0;
	GLuint framebuffer = 0;
	ProgramHandle default_program;
	bool has_gpu_mem_info_ext = false;
} g_gpu;


namespace DDS
{

static const u32 DDS_MAGIC = 0x20534444; //  little-endian
static const u32 DDSD_CAPS = 0x00000001;
static const u32 DDSD_HEIGHT = 0x00000002;
static const u32 DDSD_WIDTH = 0x00000004;
static const u32 DDSD_PITCH = 0x00000008;
static const u32 DDSD_PIXELFORMAT = 0x00001000;
static const u32 DDSD_MIPMAPCOUNT = 0x00020000;
static const u32 DDSD_LINEARSIZE = 0x00080000;
static const u32 DDSD_DEPTH = 0x00800000;
static const u32 DDPF_ALPHAPIXELS = 0x00000001;
static const u32 DDPF_FOURCC = 0x00000004;
static const u32 DDPF_INDEXED = 0x00000020;
static const u32 DDPF_RGB = 0x00000040;
static const u32 DDSCAPS_COMPLEX = 0x00000008;
static const u32 DDSCAPS_TEXTURE = 0x00001000;
static const u32 DDSCAPS_MIPMAP = 0x00400000;
static const u32 DDSCAPS2_CUBEMAP = 0x00000200;
static const u32 DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400;
static const u32 DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800;
static const u32 DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000;
static const u32 DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000;
static const u32 DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000;
static const u32 DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000;
static const u32 DDSCAPS2_VOLUME = 0x00200000;
static const u32 D3DFMT_ATI1 = '1ITA';
static const u32 D3DFMT_ATI2 = '2ITA';
static const u32 D3DFMT_DXT1 = '1TXD';
static const u32 D3DFMT_DXT2 = '2TXD';
static const u32 D3DFMT_DXT3 = '3TXD';
static const u32 D3DFMT_DXT4 = '4TXD';
static const u32 D3DFMT_DXT5 = '5TXD';
static const u32 D3DFMT_DX10 = '01XD';

enum class DxgiFormat : u32 {
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
	u32 dwSize;
	u32 dwFlags;
	u32 dwFourCC;
	u32 dwRGBBitCount;
	u32 dwRBitMask;
	u32 dwGBitMask;
	u32 dwBBitMask;
	u32 dwAlphaBitMask;
};

struct Caps2 {
	u32 dwCaps1;
	u32 dwCaps2;
	u32 dwDDSX;
	u32 dwReserved;
};

struct Header {
	u32 dwMagic;
	u32 dwSize;
	u32 dwFlags;
	u32 dwHeight;
	u32 dwWidth;
	u32 dwPitchOrLinearSize;
	u32 dwDepth;
	u32 dwMipMapCount;
	u32 dwReserved1[11];

	PixelFormat pixelFormat;
	Caps2 caps2;

	u32 dwReserved2;
};

struct DXT10Header
{
	DxgiFormat dxgi_format;
	u32 resource_dimension;
	u32 misc_flag;
	u32 array_size;
	u32 misc_flags2;
};

struct LoadInfo {
	bool compressed;
	bool swap;
	bool palette;
	u32 blockBytes;
	GLenum internalFormat;
	GLenum internalSRGBFormat;
	GLenum externalFormat;
	GLenum type;
};

static u32 sizeDXTC(u32 w, u32 h, GLuint format) {
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
	return ((pf.dwFlags & DDPF_RGB)
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
		case DxgiFormat::B8G8R8A8_UNORM:
			return &loadInfoBGRA8;
			break;
		case DxgiFormat::R8G8B8A8_UNORM:
			return &loadInfoRGBA8;
			break;
		case DxgiFormat::BC1_UNORM_SRGB:
		case DxgiFormat::BC1_UNORM:
			return &loadInfoDXT1;
			break;
		case DxgiFormat::BC2_UNORM_SRGB:
		case DxgiFormat::BC2_UNORM:
			return &loadInfoDXT3;
			break;
		case DxgiFormat::BC3_UNORM_SRGB:
		case DxgiFormat::BC3_UNORM:
			return &loadInfoDXT5;
			break;
		default:
			ASSERT(false);
			return nullptr;
			break;
	}
}

} // namespace DDS

#ifdef LUMIX_DEBUG
	#define CHECK_GL(gl) \
		do { \
			gl; \
			GLenum err = glGetError(); \
			if (err != GL_NO_ERROR) { \
				logError("Renderer") << "OpenGL error " << err; \
			/*	ASSERT(false);/**/ \
			} \
		} while(false)
#else
	#define CHECK_GL(gl) do { gl; } while(false)
#endif

void checkThread()
{
	ASSERT(g_gpu.thread == OS::getCurrentThreadID());
}

static void try_load_renderdoc()
{
	#ifdef _WIN32
		void* lib = OS::loadLibrary("renderdoc.dll");
		if (!lib) return;
		pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)OS::getLibrarySymbol(lib, "RENDERDOC_GetAPI");
		if (RENDERDOC_GetAPI) {
			RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_2, (void **)&g_gpu.rdoc_api);
			g_gpu.rdoc_api->MaskOverlayBits(~RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled, 0);
		}
		/**/
		//FreeLibrary(lib);
	#endif
}

static void logVersion() {
	const char* version = (const char*)glGetString(GL_VERSION);
	const char* vendor = (const char*)glGetString(GL_VENDOR);
	const char* renderer = (const char*)glGetString(GL_RENDERER);
	if (version) {
		logInfo("Renderer") << "OpenGL version: " << version;
		logInfo("Renderer") << "OpenGL vendor: " << vendor;
		logInfo("Renderer") << "OpenGL renderer: " << renderer;
	}
}

static void* getGLFunc(const char* name) {
	#ifdef _WIN32
		return wglGetProcAddress(name);
	#else
		return (void*)glXGetProcAddress((const GLubyte*)name);
	#endif
}

#ifdef __linux__
Display* gdisplay;
static bool load_gl_linux(void* wnd){
	XInitThreads();
	Display* display = XOpenDisplay(nullptr);
	gdisplay = display;
	XLockDisplay(display);

	int major, minor;
	const bool version_res = glXQueryVersion(display, &major, &minor);
	LUMIX_FATAL(version_res);
	LUMIX_FATAL((major == 1 && minor >= 2) || major > 1);

	const i32 screen = DefaultScreen(display);
	const int attrs[] = {
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
		GLX_DOUBLEBUFFER, true,
		GLX_RED_SIZE, 8,
		GLX_BLUE_SIZE, 8,
		GLX_GREEN_SIZE, 8,
		GLX_DEPTH_SIZE, 24,
		GLX_STENCIL_SIZE, 8,
		0,
	};

	GLXFBConfig best_cfg = NULL;

	int cfgs_count;
	GLXFBConfig* cfgs = glXChooseFBConfig(display, screen, attrs, &cfgs_count);

	XVisualInfo* visual = nullptr;
	for (int i = 0; i < cfgs_count; ++i) {
		visual = glXGetVisualFromFBConfig(display, cfgs[i]);
		if (visual) {
			bool valid = true;
			for (uint32_t attr = 6; attr < lengthOf(attrs) - 1 && attrs[attr] != 0; attr += 2) {
				int value;
				glXGetFBConfigAttrib(display, cfgs[i], attrs[attr], &value);
				if (value < attrs[attr + 1]) {
					valid = false;
					break;
				}
			}

			if (valid) {
				best_cfg = cfgs[i];
				break;
			}
		}

		XFree(visual);
		visual = NULL;
	}

	LUMIX_FATAL(visual);

	GLXContext ctx = glXCreateContext(display, visual, 0, GL_TRUE);
	LUMIX_FATAL(ctx);

	PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB;
	glXCreateContextAttribsARB = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddress( (const GLubyte*)"glXCreateContextAttribsARB");

	if (glXCreateContextAttribsARB) {
		i32 flags = 0;
		#ifdef LUMIX_DEBUG
 			flags = GLX_CONTEXT_DEBUG_BIT_ARB;
		#endif
		const int ctx_attrs[] =
		{
			GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
			GLX_CONTEXT_MINOR_VERSION_ARB, 1,
			GLX_CONTEXT_FLAGS_ARB, flags,
			GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
			0,
		};

		const GLXContext ctx2 = glXCreateContextAttribsARB(display, best_cfg, 0, true, ctx_attrs);

		if (ctx2) {
			glXDestroyContext(display, ctx);
			ctx = ctx2;
		}
	}

	XFree(cfgs);
	XUnlockDisplay(display);	

	#define GPU_GL_IMPORT(prototype, name) \
		do { \
			name = (prototype)getGLFunc(#name); \
			if (!name) { \
				logError("Renderer") << "Failed to load GL function " #name "."; \
				return false; \
			} \
		} while(0)

	#include "gl_ext.h"

	#undef GPU_GL_IMPORT

	glXMakeCurrent(display, (::Window)wnd, ctx);
	logVersion();
	return true;
}
#endif

static bool load_gl(void* platform_handle, u32 init_flags)
{
	#ifdef _WIN32
		const bool vsync = init_flags & (u32)InitFlags::VSYNC;
		HDC hdc = (HDC)platform_handle;
		const PIXELFORMATDESCRIPTOR pfd =
		{
			sizeof(PIXELFORMATDESCRIPTOR),
			1,
			PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    // Flags
			PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
			32,                   // Colordepth of the framebuffer.
			0, 0, 0, 0, 0, 0,
			0,
			0,
			0,
			0, 0, 0, 0,
			24,                   // Number of bits for the depthbuffer
			8,                    // Number of bits for the stencilbuffer
			0,                    // Number of Aux buffers in the framebuffer.
			PFD_MAIN_PLANE,
			0,
			0, 0, 0
		};
		const int pf = ChoosePixelFormat(hdc, &pfd);
		BOOL pf_status = SetPixelFormat(hdc, pf, &pfd);
		ASSERT(pf_status == TRUE);

		const HGLRC dummy_context = wglCreateContext(hdc);
		ASSERT(dummy_context);
		wglMakeCurrent(hdc, dummy_context);

		typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC) (int interval);
		typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int *attribList);
		PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)getGLFunc("wglCreateContextAttribsARB");
		PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)getGLFunc("wglSwapIntervalEXT");
		
		#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
		#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
		#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
		#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
		#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
		#define WGL_CONTEXT_FLAGS_ARB 0x2094
		#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
		#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
		#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB  0x00000002
		
		const int32_t contextAttrs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
			WGL_CONTEXT_MINOR_VERSION_ARB, 5,
			#ifdef LUMIX_DEBUG
				WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
			#endif
	//		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB ,
			0
		};
		HGLRC hglrc = wglCreateContextAttribsARB(hdc, 0, contextAttrs);
		if (hglrc) {
			wglMakeCurrent(hdc, hglrc);
			wglDeleteContext(dummy_context);
		}
		else {
			DWORD err = GetLastError();
			logError("Renderer") << "wglCreateContextAttribsARB failed, GetLastError() = " << (u32) err; 
			logError("Renderer") << "OpenGL 4.5+ required";
			logVersion();
			return false;
		}
		logVersion();
		g_gpu.contexts[0].hglrc = hglrc;
		wglSwapIntervalEXT(vsync ? 1 : 0);
		void* gl_dll = OS::loadLibrary("opengl32.dll");

		#define GPU_GL_IMPORT(prototype, name) \
			do { \
				name = (prototype)getGLFunc(#name); \
				if (!name && gl_dll) { \
					name = (prototype)OS::getLibrarySymbol(gl_dll, #name); \
					if (!name) { \
						logError("Renderer") << "Failed to load GL function " #name "."; \
						return false; \
					} \
				} \
			} while(0)

		#include "gl_ext.h"

		#undef GPU_GL_IMPORT
		return true;
	#elif defined __linux__
		return load_gl_linux(platform_handle);
	#else
		#error platform not supported
		return false;
	#endif
}


int getSize(AttributeType type)
{
	switch(type) {
		case AttributeType::FLOAT: return 4;
		case AttributeType::I8: return 1;
		case AttributeType::U8: return 1;
		case AttributeType::I16: return 2;
		default: ASSERT(false); return 0;
	}
}


void VertexDecl::addAttribute(u8 idx, u8 byte_offset, u8 components_num, AttributeType type, u8 flags)
{
	if(attributes_count >= lengthOf(attributes)) {
		ASSERT(false);
		return;
	}

	Attribute& attr = attributes[attributes_count];
	attr.components_count = components_num;
	attr.idx = idx;
	attr.flags = flags;
	attr.type = type;
	attr.byte_offset = byte_offset;
	hash = crc32(attributes, sizeof(Attribute) * attributes_count);
	++attributes_count;
}


void viewport(u32 x,u32 y,u32 w,u32 h)
{
	checkThread();
	glViewport(x, y, w, h);
}


void scissor(u32 x,u32 y,u32 w,u32 h)
{
	checkThread();
	glScissor(x, y, w, h);
}

static void setVAO(const VertexDecl& decl) {
	checkThread();

	u32 mask = 0;
	
	for (u32 i = 0; i < decl.attributes_count; ++i) {
		const Attribute& attr = decl.attributes[i];
		GLenum gl_attr_type;
		switch (attr.type) {
			case AttributeType::I16: gl_attr_type = GL_SHORT; break;
			case AttributeType::FLOAT: gl_attr_type = GL_FLOAT; break;
			case AttributeType::I8: gl_attr_type = GL_BYTE; break;
			case AttributeType::U8: gl_attr_type = GL_UNSIGNED_BYTE; break;
			default: ASSERT(false); break;
		}

		const bool instanced = attr.flags & Attribute::INSTANCED;
		const bool normalized = attr.flags & Attribute::NORMALIZED;
		if (attr.flags & Attribute::AS_INT) {
			ASSERT(!normalized);
			CHECK_GL(glVertexAttribIFormat(attr.idx, attr.components_count, gl_attr_type, attr.byte_offset));
		}
		else {
			CHECK_GL(glVertexAttribFormat(attr.idx, attr.components_count, gl_attr_type, normalized, attr.byte_offset));
		}
		CHECK_GL(glEnableVertexAttribArray(attr.idx));
		mask |= 1 << attr.idx;
		CHECK_GL(glVertexAttribBinding(attr.idx, instanced ? 1 : 0));
	}

	for (u32 i = 0; i < 16; ++i) {
		if (!(mask & (1 << i))) {
			CHECK_GL(glDisableVertexAttribArray(i));
		}
	}
}

void useProgram(ProgramHandle handle)
{
	const Program& prg = g_gpu.programs.values[handle.value];
	const u32 prev = g_gpu.last_program.value;
	if (prev != handle.value) {
		g_gpu.last_program = handle;
		if (!handle.isValid()) {
			CHECK_GL(glUseProgram(0));
		}
		else {
			if (!prg.handle) {
				CHECK_GL(glUseProgram(g_gpu.programs[g_gpu.default_program.value].handle));
			}
			else {
				CHECK_GL(glUseProgram(prg.handle));
			}

			if (prev == 0xffFFffFF || g_gpu.programs.values[handle.value].decl.hash != g_gpu.programs.values[prev].decl.hash) {
				setVAO(prg.decl);
			}
		}
	}
}


void bindTextures(const TextureHandle* handles, u32 offset, u32 count)
{
	GLuint gl_handles[64];
	ASSERT(count <= lengthOf(gl_handles));
	ASSERT(handles);
	
	for(u32 i = 0; i < count; ++i) {
		if (handles[i].isValid()) {
			gl_handles[i] = g_gpu.textures[handles[i].value].handle;
		}
		else {
			gl_handles[i] = 0;
		}
	}

	CHECK_GL(glBindTextures(offset, count, gl_handles));
}


void bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride_offset) {
	checkThread();
	ASSERT(binding_idx < 2);
	if(buffer.isValid()) {
		const GLuint gl_handle = g_gpu.buffers[buffer.value].handle;
		CHECK_GL(glBindVertexBuffer(binding_idx, gl_handle, buffer_offset, stride_offset));
	}
	else {
		CHECK_GL(glBindVertexBuffer(binding_idx, 0, 0, 0));
	}
}


void setState(u64 state)
{
	checkThread();
	
	if(state == g_gpu.last_state) return;
	g_gpu.last_state = state;

	if (state & u64(StateFlags::DEPTH_TEST)) CHECK_GL(glEnable(GL_DEPTH_TEST));
	else CHECK_GL(glDisable(GL_DEPTH_TEST));
	
	CHECK_GL(glDepthMask((state & u64(StateFlags::DEPTH_WRITE)) != 0));
	
	if (state & u64(StateFlags::SCISSOR_TEST)) CHECK_GL(glEnable(GL_SCISSOR_TEST));
	else CHECK_GL(glDisable(GL_SCISSOR_TEST));
	
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


void bindIndexBuffer(BufferHandle handle)
{
	checkThread();
	if(handle.isValid()) {	
		const GLuint ib = g_gpu.buffers[handle.value].handle;
		CHECK_GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib));
		return;
	}

	CHECK_GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}


void drawElements(u32 offset, u32 count, PrimitiveType primitive_type, DataType type)
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
	switch(type) {
		case DataType::U16: t = GL_UNSIGNED_SHORT; break;
		case DataType::U32: t = GL_UNSIGNED_INT; break;
		default: ASSERT(0); break;
	}

	CHECK_GL(glDrawElements(pt, count, t, (void*)(intptr_t)offset));
}

void drawTrianglesInstanced(u32 indices_count, u32 instances_count, DataType index_type)
{
	checkThread();
	const GLenum type = index_type == DataType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	/*if (instances_count * indices_count > 4096) {
		struct {
			u32  indices_count;
			u32  instances_count;
			u32  indices_offset;
			u32  base_vertex;
			u32  base_instance;
		} mdi;
		mdi.indices_count = indices_count;
		mdi.instances_count = instances_count;
		mdi.indices_offset = 0;
		mdi.base_instance = 0;
		mdi.base_vertex = 0;
		// we use glMultiDrawElementsIndirect because of 
		// https://devtalk.nvidia.com/default/topic/1052728/opengl/extremely-slow-gldrawelementsinstanced-compared-to-gldrawarraysinstanced-/
		CHECK_GL(glMultiDrawElementsIndirect(GL_TRIANGLES, type, &mdi, 1, 0));
	}
	else*/ {
		CHECK_GL(glDrawElementsInstanced(GL_TRIANGLES, indices_count, type, 0, instances_count));
	}
}


void drawTriangles(u32 indices_count, DataType index_type)
{
	checkThread();

	const GLenum type = index_type == DataType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	CHECK_GL(glDrawElements(GL_TRIANGLES, indices_count, type, 0));
}


void drawTriangleStripArraysInstanced(u32 indices_count, u32 instances_count)
{
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, indices_count, instances_count);
}


void drawArrays(u32 offset, u32 count, PrimitiveType type)
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

	CHECK_GL(glDrawArrays(pt, offset, count));
}

void bindUniformBuffer(u32 index, BufferGroupHandle buffer, size_t element_index) {
	checkThread();
	if (buffer.isValid()) {
		const BufferGroup& g = g_gpu.buffer_groups[buffer.value];
		ASSERT(element_index < g.elements_count);
		CHECK_GL(glBindBufferRange(GL_UNIFORM_BUFFER, index, g.handle, g.element_size * element_index, g.element_size));
		return;
	}
	CHECK_GL(glBindBufferBase(GL_UNIFORM_BUFFER, index, 0));
}

void bindUniformBuffer(u32 index, BufferHandle buffer, size_t offset, size_t size) {
	checkThread();
	if (buffer.isValid()) {
		const GLuint buf = g_gpu.buffers[buffer.value].handle;
		CHECK_GL(glBindBufferRange(GL_UNIFORM_BUFFER, index, buf, offset, size));
		return;
	}
	CHECK_GL(glBindBufferRange(GL_UNIFORM_BUFFER, index, 0, offset, size));
}


void* map(BufferHandle buffer, size_t size)
{
	checkThread();
	const Buffer& b = g_gpu.buffers[buffer.value];
	ASSERT((b.flags & (u32)BufferFlags::IMMUTABLE) == 0);
	const GLbitfield gl_flags = GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT;
	return glMapNamedBufferRange(b.handle, 0, size, gl_flags);
}


void unmap(BufferHandle buffer)
{
	checkThread();
	const GLuint buf = g_gpu.buffers[buffer.value].handle;
	CHECK_GL(glUnmapNamedBuffer(buf));
}

void update(BufferHandle buffer, const void* data, size_t size)
{
	checkThread();
	const Buffer& b = g_gpu.buffers[buffer.value];
	ASSERT((b.flags & (u32)BufferFlags::IMMUTABLE) == 0);
	const GLuint buf = b.handle;
	CHECK_GL(glNamedBufferSubData(buf, 0, size, data));
}

void update(BufferGroupHandle buffer, const void* data, size_t element_index) {
	checkThread();
	const BufferGroup& b = g_gpu.buffer_groups[buffer.value];
	ASSERT((b.flags & (u32)BufferFlags::IMMUTABLE) == 0);
	const GLuint buf = b.handle;
	CHECK_GL(glNamedBufferSubData(buf, element_index * b.element_size, b.element_size, data));
}


void startCapture()
{
	if (g_gpu.rdoc_api) {
		g_gpu.rdoc_api->StartFrameCapture(nullptr, nullptr);
	}
}


void stopCapture()
{
	if (g_gpu.rdoc_api) {
		g_gpu.rdoc_api->EndFrameCapture(nullptr, nullptr);
	}
}

static void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam)
{
	if(GL_DEBUG_TYPE_PUSH_GROUP == type || type == GL_DEBUG_TYPE_POP_GROUP) return;
	if (type == GL_DEBUG_TYPE_ERROR) {
		logError("GL") << message;
		//ASSERT(false);
	}
	else if (type == GL_DEBUG_TYPE_PERFORMANCE) {
		logInfo("GL") << message;
	}
	else {
		//logInfo("GL") << message;
	}
}

void setCurrentWindow(void* window_handle) {
	checkThread();

	#ifdef _WIN32
		WindowContext& ctx = [window_handle]() -> WindowContext& {
			if (!window_handle) return g_gpu.contexts[0];

			for (WindowContext& i : g_gpu.contexts) {
				if (i.window_handle == window_handle) return i;
			}

			for (WindowContext& i : g_gpu.contexts) {
				if (!i.window_handle) {
					i.window_handle = window_handle;
					i.device_context = GetDC((HWND)window_handle);
					i.hglrc = 0;
					return i;
				}
			}
			LUMIX_FATAL(false);
			return g_gpu.contexts[0];
		}();

		if (!ctx.hglrc) {
			const HDC hdc = ctx.device_context;
			const PIXELFORMATDESCRIPTOR pfd =
			{
				sizeof(PIXELFORMATDESCRIPTOR),
				1,
				PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    // Flags
				PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
				32,                   // Colordepth of the framebuffer.
				0, 0, 0, 0, 0, 0,
				0,
				0,
				0,
				0, 0, 0, 0,
				24,                   // Number of bits for the depthbuffer
				8,                    // Number of bits for the stencilbuffer
				0,                    // Number of Aux buffers in the framebuffer.
				PFD_MAIN_PLANE,
				0,
				0, 0, 0
			};
			const int pf = ChoosePixelFormat(hdc, &pfd);
			BOOL pf_status = SetPixelFormat(hdc, pf, &pfd);
			ASSERT(pf_status == TRUE);

			wglMakeCurrent(hdc, g_gpu.contexts[0].hglrc);

			typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC) (int interval);
			typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int *attribList);
			PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)getGLFunc("wglCreateContextAttribsARB");
			PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)getGLFunc("wglSwapIntervalEXT");
		
			#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
			#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
			#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
			#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
			#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
			#define WGL_CONTEXT_FLAGS_ARB 0x2094
			#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
			#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
			#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB  0x00000002
		
			const int32_t contextAttrs[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
				WGL_CONTEXT_MINOR_VERSION_ARB, 5,
				#ifdef LUMIX_DEBUG
					WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
				#endif
		//		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB ,
				0
			};

			// TODO destroy context when window is destroyed
			HGLRC hglrc = wglCreateContextAttribsARB(hdc, g_gpu.contexts[0].hglrc, contextAttrs);
			ctx.hglrc = hglrc;
			wglMakeCurrent(ctx.device_context, hglrc);
			CHECK_GL(glGenVertexArrays(1, &ctx.vao));
			CHECK_GL(glBindVertexArray(ctx.vao));
			CHECK_GL(glVertexBindingDivisor(0, 0));
			CHECK_GL(glVertexBindingDivisor(1, 1));

			#ifdef LUMIX_DEBUG
				CHECK_GL(glEnable(GL_DEBUG_OUTPUT));
				CHECK_GL(glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS));
				CHECK_GL(glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE));
				CHECK_GL(glDebugMessageCallback(gl_debug_callback, 0));
			#endif

		}

		wglMakeCurrent(ctx.device_context, ctx.hglrc);
	#endif
	useProgram(INVALID_PROGRAM);
}


void swapBuffers()
{
	checkThread();
	glFinish();
	#ifdef _WIN32
		for (const WindowContext& ctx : g_gpu.contexts) {
			SwapBuffers(ctx.device_context);
		}
	#else
		glXSwapBuffers(gdisplay, (Window)g_gpu.contexts[0].window_handle);
	#endif
}

void createBuffer(BufferHandle buffer, u32 flags, size_t size, const void* data)
{
	checkThread();
	GLuint buf;
	CHECK_GL(glCreateBuffers(1, &buf));
	
	GLbitfield gl_flags = 0;
	if ((flags & (u32)BufferFlags::IMMUTABLE) == 0) gl_flags |= GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT;
	CHECK_GL(glNamedBufferStorage(buf, size, data, gl_flags));

	g_gpu.buffers[buffer.value].handle = buf;
	g_gpu.buffers[buffer.value].flags = flags;
}

void createBufferGroup(BufferGroupHandle buffer, u32 flags, size_t element_size, size_t element_count, const void* data) {
	checkThread();
	GLuint buf;
	CHECK_GL(glCreateBuffers(1, &buf));
	
	GLbitfield gl_flags = 0;
	if ((flags & (u32)BufferFlags::IMMUTABLE) == 0) gl_flags |= GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT;
	// there's no gpu::* api to use groups as anything else but uniform buffers
	ASSERT((flags & (u32)BufferFlags::UNIFORM_BUFFER) != 0); 
	CHECK_GL(glNamedBufferStorage(buf, element_size * element_count, data, gl_flags));

	g_gpu.buffer_groups[buffer.value].handle = buf;
	g_gpu.buffer_groups[buffer.value].flags = flags;
	g_gpu.buffer_groups[buffer.value].element_size = element_size;
	g_gpu.buffer_groups[buffer.value].elements_count = element_count;
}

void destroy(ProgramHandle program)
{
	checkThread();
	
	Program& p = g_gpu.programs[program.value];
	const GLuint handle = p.handle;
	CHECK_GL(glDeleteProgram(handle));

	MutexGuard lock(g_gpu.handle_mutex);
	g_gpu.programs.dealloc(program.value);
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
	{TextureFormat::RGBA32F, GL_RGBA32F, GL_RGBA, GL_FLOAT},
	{TextureFormat::R16F, GL_R16F, GL_RED, GL_HALF_FLOAT},
	{TextureFormat::R8, GL_R8, GL_RED, GL_UNSIGNED_BYTE},
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


void update(TextureHandle texture, u32 level, u32 x, u32 y, u32 w, u32 h, TextureFormat format, void* buf)
{
	checkThread();
	Texture& t = g_gpu.textures[texture.value];
	const GLuint handle = t.handle;
	for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
		if (s_texture_formats[i].format == format) {
			const auto& f = s_texture_formats[i];
			CHECK_GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
			CHECK_GL(glTextureSubImage2D(handle, level, x, y, w, h, f.gl_format, f.type, buf));
			break;
		}
	}

}


bool loadTexture(TextureHandle handle, const void* input, int input_size, u32 flags, const char* debug_name)
{
	ASSERT(debug_name && debug_name[0]);
	checkThread();
	DDS::Header hdr;

	InputMemoryStream blob(input, input_size);
	blob.read(&hdr, sizeof(hdr));

	if (hdr.dwMagic != DDS::DDS_MAGIC || hdr.dwSize != 124 ||
		!(hdr.dwFlags & DDS::DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDS::DDSD_CAPS))
	{
		logError("renderer") << "Wrong dds format or corrupted dds (" << debug_name << ")";
		return false;
	}

	DDS::LoadInfo* li;
	int layers = 1;
	bool is_dds10 = false;

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
		is_dds10 = true;
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
	const u32 mipMapCount = (hdr.dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;

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
			u32 width = hdr.dwWidth;
			u32 height = hdr.dwHeight;

			if (li->compressed) {
				u32 size = DDS::sizeDXTC(width, height, internal_format);
				if (!is_dds10 && (size != hdr.dwPitchOrLinearSize || (hdr.dwFlags & DDS::DDSD_LINEARSIZE) == 0)) {
					CHECK_GL(glDeleteTextures(1, &texture));
					return false;
				}
				Array<u8> data(*g_gpu.allocator);
				data.resize(size);
				for (u32 mip = 0; mip < mipMapCount; ++mip) {
					blob.read(&data[0], size);
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
					width = maximum(1, width >> 1);
					height = maximum(1, height >> 1);
					size = DDS::sizeDXTC(width, height, internal_format);
				}
			}
			else if (li->palette) {
				if ((hdr.dwFlags & DDS::DDSD_PITCH) == 0 || hdr.pixelFormat.dwRGBBitCount != 8) {
					CHECK_GL(glDeleteTextures(1, &texture));
					return false;
				}
				u32 size = hdr.dwPitchOrLinearSize * height;
				if (size != width * height * li->blockBytes) {
					CHECK_GL(glDeleteTextures(1, &texture));
					return false;
				}
				Array<u8> data(*g_gpu.allocator);
				data.resize(size);
				u32 palette[256];
				Array<u32> unpacked(*g_gpu.allocator);
				unpacked.resize(size);
				blob.read(palette, 4 * 256);
				for (u32 mip = 0; mip < mipMapCount; ++mip) {
					blob.read(&data[0], size);
					for (u32 zz = 0; zz < size; ++zz) {
						unpacked[zz] = palette[data[zz]];
					}
					//glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
					if(layers > 1) {
						CHECK_GL(glTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, li->externalFormat, li->type, &unpacked[0]));
					}
					else {
						CHECK_GL(glTextureSubImage2D(texture, mip, 0, 0, width, height, li->externalFormat, li->type, &unpacked[0]));
					}
					width = maximum(1, width >> 1);
					height = maximum(1, height >> 1);
					size = width * height * li->blockBytes;
				}
			}
			else {
				if (li->swap) {
					CHECK_GL(glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE));
				}
				u32 size = width * height * li->blockBytes;
				Array<u8> data(*g_gpu.allocator);
				data.resize(size);
				for (u32 mip = 0; mip < mipMapCount; ++mip) {
					blob.read(&data[0], size);
					//glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
					if (layers > 1) {
						CHECK_GL(glTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, li->externalFormat, li->type, &data[0]));
					}
					else {
						CHECK_GL(glTextureSubImage2D(texture, mip, 0, 0, width, height, li->externalFormat, li->type, &data[0]));
					}
					width = maximum(1, width >> 1);
					height = maximum(1, height >> 1);
					size = width * height * li->blockBytes;
				}
				CHECK_GL(glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE));
			}
			CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1));
		}
	}

	const GLint wrap_u = (flags & (u32)TextureFlags::CLAMP_U) ? GL_CLAMP : GL_REPEAT;
	const GLint wrap_v = (flags & (u32)TextureFlags::CLAMP_V) ? GL_CLAMP : GL_REPEAT;
	const GLint wrap_w = (flags & (u32)TextureFlags::CLAMP_W) ? GL_CLAMP : GL_REPEAT;
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap_u));
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap_v));
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_R, wrap_w));

	Texture& t = g_gpu.textures[handle.value];
	t.format = internal_format;
	t.handle = texture;
	t.target = is_cubemap ? GL_TEXTURE_CUBE_MAP : layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	t.width = hdr.dwWidth;
	t.height = hdr.dwHeight;
	return true;
}


ProgramHandle allocProgramHandle()
{
	MutexGuard lock(g_gpu.handle_mutex);

	if(g_gpu.programs.isFull()) {
		logError("Renderer") << "Not enough free program slots.";
		return INVALID_PROGRAM;
	}
	const int id = g_gpu.programs.alloc();
	if (id < 0) return INVALID_PROGRAM;

	Program& p = g_gpu.programs[id];
	p.handle = 0;
	return { (u32)id };
}


BufferHandle allocBufferHandle()
{
	MutexGuard lock(g_gpu.handle_mutex);

	if(g_gpu.buffers.isFull()) {
		logError("Renderer") << "Not enough free buffer slots.";
		return INVALID_BUFFER;
	}
	const int id = g_gpu.buffers.alloc();
	if (id < 0) return INVALID_BUFFER;

	Buffer& t = g_gpu.buffers[id];
	t.handle = 0;
	return { (u32)id };
}

BufferGroupHandle allocBufferGroupHandle() {
	MutexGuard lock(g_gpu.handle_mutex);

	if(g_gpu.buffer_groups.isFull()) {
		logError("Renderer") << "Not enough free buffer group slots.";
		return INVALID_BUFFER_GROUP;
	}
	const int id = g_gpu.buffer_groups.alloc();
	ASSERT(id >= 0);

	BufferGroup& t = g_gpu.buffer_groups[id];
	t.handle = 0;
	return { (u32)id };
}

TextureHandle allocTextureHandle()
{
	MutexGuard lock(g_gpu.handle_mutex);

	if(g_gpu.textures.isFull()) {
		logError("Renderer") << "Not enough free texture slots.";
		return INVALID_TEXTURE;
	}
	const int id = g_gpu.textures.alloc();
	ASSERT(id >= 0);

	Texture& t = g_gpu.textures[id];
	t.handle = 0;
	return { (u32)id };
}


void createTextureView(TextureHandle view_handle, TextureHandle orig_handle)
{
	checkThread();
	
	const Texture& orig = g_gpu.textures[orig_handle.value];
	Texture& view = g_gpu.textures[view_handle.value];

	if (view.handle != 0) {
		CHECK_GL(glDeleteTextures(1, &view.handle));
	}

	view.target = GL_TEXTURE_2D;
	view.format = orig.format;

	CHECK_GL(glGenTextures(1, &view.handle));
	CHECK_GL(glTextureView(view.handle, GL_TEXTURE_2D, orig.handle, orig.format, 0, 1, 0, 1));
	view.width = orig.width;
	view.height = orig.height;
}


bool createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, u32 flags, const void* data, const char* debug_name)
{
	checkThread();
	const bool is_srgb = flags & (u32)TextureFlags::SRGB;
	const bool no_mips = flags & (u32)TextureFlags::NO_MIPS;
	const bool is_3d = depth > 1 && (flags & (u32)TextureFlags::IS_3D);
	ASSERT(!is_srgb); // use format argument to enable srgb
	ASSERT(debug_name && debug_name[0]);

	GLuint texture;
	int found_format = 0;
	GLenum internal_format = 0;
	const GLenum target = depth <= 1 ? GL_TEXTURE_2D : (is_3d ? GL_TEXTURE_3D : GL_TEXTURE_2D_ARRAY);
	const u32 mip_count = no_mips ? 1 : 1 + log2(maximum(w, h, depth));

	CHECK_GL(glCreateTextures(target, 1, &texture));
	for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
		if(s_texture_formats[i].format == format) {
			internal_format = s_texture_formats[i].gl_internal;
			if(depth <= 1) {
				CHECK_GL(glTextureStorage2D(texture, mip_count, s_texture_formats[i].gl_internal, w, h));
				if (data) {
					CHECK_GL(glTextureSubImage2D(texture
						, 0
						, 0
						, 0
						, w
						, h
						, s_texture_formats[i].gl_format
						, s_texture_formats[i].type
						, data));
				}
			}
			else {
				CHECK_GL(glTextureStorage3D(texture, mip_count, s_texture_formats[i].gl_internal, w, h, depth));
				if (data) {
					CHECK_GL(glTextureSubImage3D(texture
						, 0
						, 0
						, 0
						, 0
						, w
						, h
						, depth
						, s_texture_formats[i].gl_format
						, s_texture_formats[i].type
						, data));
				}
			}
			found_format = 1;
			break;
		}
	}

	if(!found_format) {
		CHECK_GL(glDeleteTextures(1, &texture));
		ASSERT(false);
		return false;	
	}

	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, mip_count - 1));

	if(debug_name && debug_name[0]) {
		CHECK_GL(glObjectLabel(GL_TEXTURE, texture, stringLength(debug_name), debug_name));
	}
	CHECK_GL(glGenerateTextureMipmap(texture));
	
	const GLint wrap_u = (flags & (u32)TextureFlags::CLAMP_U) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	const GLint wrap_v = (flags & (u32)TextureFlags::CLAMP_V) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	const GLint wrap_w = (flags & (u32)TextureFlags::CLAMP_W) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap_u));
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap_v));
	CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_WRAP_R, wrap_w));
	if (flags & (u32)TextureFlags::POINT_FILTER) {
		CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
		CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	}
	else {
		CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		CHECK_GL(glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, no_mips ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR));
	}

	Texture& t = g_gpu.textures[handle.value];
	t.handle = texture;
	t.target = target;
	t.format = internal_format;
	t.width = w;
	t.height = h;

	return true;
}


void destroy(TextureHandle texture)
{
	checkThread();
	Texture& t = g_gpu.textures[texture.value];
	const GLuint handle = t.handle;
	CHECK_GL(glDeleteTextures(1, &handle));

	MutexGuard lock(g_gpu.handle_mutex);
	g_gpu.textures.dealloc(texture.value);
}

void destroy(BufferHandle buffer) {
	checkThread();
	
	Buffer& t = g_gpu.buffers[buffer.value];
	const GLuint handle = t.handle;
	CHECK_GL(glDeleteBuffers(1, &handle));

	MutexGuard lock(g_gpu.handle_mutex);
	g_gpu.buffers.dealloc(buffer.value);
}

void destroy(BufferGroupHandle buffer) {
	checkThread();
	
	BufferGroup& t = g_gpu.buffer_groups[buffer.value];
	const GLuint handle = t.handle;
	CHECK_GL(glDeleteBuffers(1, &handle));

	MutexGuard lock(g_gpu.handle_mutex);
	g_gpu.buffer_groups.dealloc(buffer.value);
}

void clear(u32 flags, const float* color, float depth)
{
	CHECK_GL(glUseProgram(0));
	g_gpu.last_program = INVALID_PROGRAM;
	CHECK_GL(glDisable(GL_SCISSOR_TEST));
	CHECK_GL(glDisable(GL_BLEND));
	g_gpu.last_state &= ~u64(0xffFF << 6);
	checkThread();
	GLbitfield gl_flags = 0;
	if (flags & (u32)ClearFlags::COLOR) {
		CHECK_GL(glClearColor(color[0], color[1], color[2], color[3]));
		gl_flags |= GL_COLOR_BUFFER_BIT;
	}
	if (flags & (u32)ClearFlags::DEPTH) {
		CHECK_GL(glDepthMask(GL_TRUE));
		CHECK_GL(glClearDepth(depth));
		gl_flags |= GL_DEPTH_BUFFER_BIT;
	}
	if (flags & (u32)ClearFlags::STENCIL) {
		glStencilMask(0xff);
		g_gpu.last_state = g_gpu.last_state | (0xff << 22);
		glClearStencil(0);
		gl_flags |= GL_STENCIL_BUFFER_BIT;
	}
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


bool createProgram(ProgramHandle prog, const VertexDecl& decl, const char** srcs, const ShaderType* types, u32 num, const char** prefixes, u32 prefixes_count, const char* name)
{
	checkThread();

	static const char* attr_defines[] = {
		"#define _HAS_ATTR0\n",
		"#define _HAS_ATTR1\n",
		"#define _HAS_ATTR2\n",
		"#define _HAS_ATTR3\n",
		"#define _HAS_ATTR4\n",
		"#define _HAS_ATTR5\n",
		"#define _HAS_ATTR6\n",
		"#define _HAS_ATTR7\n",
		"#define _HAS_ATTR8\n",
		"#define _HAS_ATTR9\n",
		"#define _HAS_ATTR10\n",
		"#define _HAS_ATTR11\n",
		"#define _HAS_ATTR12\n"
	};

	const char* combined_srcs[32];
	ASSERT(prefixes_count < lengthOf(combined_srcs) - 1); 
	enum { MAX_SHADERS_PER_PROGRAM = 16 };

	if (num > MAX_SHADERS_PER_PROGRAM) {
		logError("Renderer") << "Too many shaders per program in " << name;
		return false;
	}

	const GLuint prg = glCreateProgram();
	if (name && name[0]) {
		CHECK_GL(glObjectLabel(GL_PROGRAM, prg, stringLength(name), name));
	}

	for (u32 i = 0; i < num; ++i) {
		GLenum shader_type;
		switch (types[i]) {
			case ShaderType::GEOMETRY: shader_type = GL_GEOMETRY_SHADER; break;
			case ShaderType::FRAGMENT: shader_type = GL_FRAGMENT_SHADER; break;
			case ShaderType::VERTEX: shader_type = GL_VERTEX_SHADER; break;
			default: ASSERT(false); return false;
		}
		const GLuint shd = glCreateShader(shader_type);
		combined_srcs[0] = R"#(
			#version 140
			#extension GL_ARB_explicit_attrib_location : enable
			#extension GL_ARB_shading_language_420pack : enable
			#extension GL_ARB_separate_shader_objects : enable
			#define _ORIGIN_BOTTOM_LEFT
		)#";
		combined_srcs[prefixes_count + decl.attributes_count + 1] = srcs[i];
		for (u32 j = 0; j < prefixes_count; ++j) {
			combined_srcs[j + 1] = prefixes[j];
		}
		for (u32 j = 0; j < decl.attributes_count; ++j) {
			combined_srcs[j + prefixes_count + 1] = attr_defines[decl.attributes[j].idx];
		}

		CHECK_GL(glShaderSource(shd, 2 + prefixes_count + decl.attributes_count, combined_srcs, 0));
		CHECK_GL(glCompileShader(shd));

		GLint compile_status;
		CHECK_GL(glGetShaderiv(shd, GL_COMPILE_STATUS, &compile_status));
		if (compile_status == GL_FALSE) {
			GLint log_len = 0;
			CHECK_GL(glGetShaderiv(shd, GL_INFO_LOG_LENGTH, &log_len));
			if (log_len > 0) {
				Array<char> log_buf(*g_gpu.allocator);
				log_buf.resize(log_len);
				CHECK_GL(glGetShaderInfoLog(shd, log_len, &log_len, &log_buf[0]));
				logError("Renderer") << name << " - " << shaderTypeToString(types[i]) << ": " << &log_buf[0];
			}
			else {
				logError("Renderer") << "Failed to compile shader " << name << " - " << shaderTypeToString(types[i]);
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
			Array<char> log_buf(*g_gpu.allocator);
			log_buf.resize(log_len);
			CHECK_GL(glGetProgramInfoLog(prg, log_len, &log_len, &log_buf[0]));
			logError("Renderer") << name << ": " << &log_buf[0];
		}
		else {
			logError("Renderer") << "Failed to link program " << name;
		}
		CHECK_GL(glDeleteProgram(prg));
		return false;
	}

	const int id = prog.value;
	g_gpu.programs[id].handle = prg;
	g_gpu.programs[id].decl = decl;
	return true;
}


void preinit(IAllocator& allocator)
{
	try_load_renderdoc();
	g_gpu.allocator = &allocator;
	g_gpu.textures.init();
	g_gpu.buffers.init();
	g_gpu.buffer_groups.init();
	g_gpu.programs.init();
}


bool getMemoryStats(Ref<MemoryStats> stats) {
	if (!g_gpu.has_gpu_mem_info_ext) return false;

	GLint tmp;
	CHECK_GL(glGetIntegerv(GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &tmp));
	stats->total_available_mem = (u64)tmp * 1024;

	CHECK_GL(glGetIntegerv(GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &tmp));
	stats->current_available_mem = (u64)tmp * 1024;

	CHECK_GL(glGetIntegerv(GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &tmp));
	stats->dedicated_vidmem = (u64)tmp * 1024;
	
	return true;
}


bool init(void* window_handle, u32 init_flags)
{
	#ifdef LUMIX_DEBUG
		const bool debug = true;
	#else 
		const bool debug = init_flags & (u32)InitFlags::DEBUG_OUTPUT;
	#endif
	
	g_gpu.thread = OS::getCurrentThreadID();
	g_gpu.contexts[0].window_handle = window_handle;
	#ifdef _WIN32
		g_gpu.contexts[0].device_context = GetDC((HWND)window_handle);
		if (!load_gl(g_gpu.contexts[0].device_context, init_flags)) return false;
	#else
		if (!load_gl(window_handle, init_flags)) return false;
	#endif

	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &g_gpu.max_vertex_attributes);

	int extensions_count;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensions_count);
	g_gpu.has_gpu_mem_info_ext = false; 
	for(int i = 0; i < extensions_count; ++i) {
		const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if (equalStrings(ext, "GL_NVX_gpu_memory_info")) {
			g_gpu.has_gpu_mem_info_ext = true; 
			break;
		}
		//OutputDebugString(ext);
		//OutputDebugString("\n");
	}
	//const unsigned char* version = glGetString(GL_VERSION);

	CHECK_GL(glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE));
	CHECK_GL(glDepthFunc(GL_GREATER));

	if (debug) {
		CHECK_GL(glEnable(GL_DEBUG_OUTPUT));
		CHECK_GL(glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS));
		CHECK_GL(glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE));
		CHECK_GL(glDebugMessageCallback(gl_debug_callback, 0));
	}

	CHECK_GL(glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS));
	CHECK_GL(glBindVertexArray(0));	
	CHECK_GL(glCreateFramebuffers(1, &g_gpu.framebuffer));

	
	g_gpu.default_program = allocProgramHandle();
	Program& p = g_gpu.programs[g_gpu.default_program.value];
	p.handle = glCreateProgram();
	CHECK_GL(glGenVertexArrays(1, &g_gpu.contexts[0].vao));
	CHECK_GL(glBindVertexArray(g_gpu.contexts[0].vao));
	CHECK_GL(glVertexBindingDivisor(0, 0));
	CHECK_GL(glVertexBindingDivisor(1, 1));

	const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	const char* vs_src = "void main() { gl_Position = vec4(0, 0, 0, 0); }";
	glShaderSource(vs, 1, &vs_src, nullptr);
	glCompileShader(vs);
	glAttachShader(p.handle, vs);
	CHECK_GL(glLinkProgram(p.handle));
	glDeleteShader(vs);

	g_gpu.last_state = 1;
	setState(0);

	return true;
}


bool isHomogenousDepth() { return false; }


bool isOriginBottomLeft() { return true; }


void copy(TextureHandle dst_handle, TextureHandle src_handle) {
	checkThread();
	Texture& dst = g_gpu.textures[dst_handle.value];
	Texture& src = g_gpu.textures[src_handle.value];
	ASSERT(src.target == GL_TEXTURE_2D);
	ASSERT(src.target == dst.target);
	ASSERT(dst.width == src.width);
	ASSERT(dst.height == src.height);

	CHECK_GL(glCopyImageSubData(src.handle, src.target, 0, 0, 0, 0, dst.handle, dst.target, 0, 0, 0, 0, src.width, src.height, 1));
}

void readTexture(TextureHandle texture, Span<u8> buf)
{
	checkThread();

	Texture& t = g_gpu.textures[texture.value];
	const GLuint handle = t.handle;

	for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
		if (s_texture_formats[i].gl_internal == t.format) {
			const auto& f = s_texture_formats[i];
			CHECK_GL(glGetTextureImage(handle, 0, f.gl_format, f.type, buf.length(), buf.begin()));
			return;
		}
	}
	ASSERT(false);
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


QueryHandle createQuery()
{
	GLuint q;
	CHECK_GL(glGenQueries(1, &q));
	return {q};
}


bool isQueryReady(QueryHandle query)
{
	GLuint done;
	glGetQueryObjectuiv(query.value, GL_QUERY_RESULT_AVAILABLE, &done);
	return done;
}

u64 getQueryFrequency() { return 1'000'000'000; }

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


void setFramebuffer(TextureHandle* attachments, u32 num, u32 flags)
{
	checkThread();

	if (flags & (u32)FramebufferFlags::SRGB) {
		CHECK_GL(glEnable(GL_FRAMEBUFFER_SRGB));
	}
	else {
		CHECK_GL(glDisable(GL_FRAMEBUFFER_SRGB));
	}

	if(!attachments || num == 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return;
	}

	bool depth_bound = false;
	u32 rb_count = 0;
	for (u32 i = 0; i < num; ++i) {
		const GLuint t = g_gpu.textures[attachments[i].value].handle;
		GLint internal_format;
		CHECK_GL(glBindTexture(GL_TEXTURE_2D, t));
		CHECK_GL(glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format));
		
		switch(internal_format) {
			case GL_DEPTH24_STENCIL8:
				CHECK_GL(glBindFramebuffer(GL_FRAMEBUFFER, g_gpu.framebuffer));
				CHECK_GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0));
				CHECK_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, t, 0));
				depth_bound = true;
				break;
			case GL_DEPTH_COMPONENT24:
			case GL_DEPTH_COMPONENT32:
				CHECK_GL(glBindFramebuffer(GL_FRAMEBUFFER, g_gpu.framebuffer));
				CHECK_GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0));
				CHECK_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t, 0));
				depth_bound = true;
				break;
			default:
				CHECK_GL(glBindFramebuffer(GL_FRAMEBUFFER, g_gpu.framebuffer));
				CHECK_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, t, 0));
				++rb_count;
				break;
		}
	}

	GLint max_attachments = 0;
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
	for(int i = rb_count; i < max_attachments; ++i) {
		glNamedFramebufferRenderbuffer(g_gpu.framebuffer, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, 0);
	}

	if(!depth_bound) {
		glNamedFramebufferRenderbuffer(g_gpu.framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
		glNamedFramebufferRenderbuffer(g_gpu.framebuffer, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, g_gpu.framebuffer);
	auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	ASSERT(status == GL_FRAMEBUFFER_COMPLETE);

	GLenum db[16];
	for (u32 i = 0; i < lengthOf(db); ++i) db[i] = GL_COLOR_ATTACHMENT0 + i;
	
	CHECK_GL(glDrawBuffers(rb_count, db));
}


void shutdown()
{
	checkThread();
}

} // ns gpu 

} // ns Lumix
