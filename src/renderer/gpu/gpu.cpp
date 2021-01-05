#include "dds.h"
#include "gpu.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/sync.h"
#include "engine/os.h"
#include "engine/stream.h"
#include "engine/string.h"
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

struct Buffer {
	~Buffer() {
		if (gl_handle) glDeleteBuffers(1, &gl_handle);
	}

	GLuint gl_handle;
	BufferFlags flags;
};

struct Texture {
	~Texture() {
		if (gl_handle) glDeleteTextures(1, &gl_handle);
	}

	GLuint gl_handle;
	GLenum target;
	GLenum format;
	u32 width;
	u32 height;
	u32 depth;
	TextureFlags flags;
	#ifdef LUMIX_DEBUG
		StaticString<64> name;
	#endif
};

struct Program {
	~Program() {
		if(gl_handle) glDeleteProgram(gl_handle);
	}

	GLuint gl_handle = 0;
	VertexDecl decl;
};

struct WindowContext {
	u32 last_frame;
	void* window_handle = nullptr;
	GLuint vao;
	#ifdef _WIN32
		HDC device_context;
		HGLRC hglrc;
	#endif
};

struct GL {
	GL(IAllocator& allocator) : allocator(allocator) {}

	IAllocator& allocator;
	u32 frame = 0;
	RENDERDOC_API_1_0_2* rdoc_api;
	WindowContext contexts[64];
	Lumix::os::ThreadID thread;
	int instance_attributes = 0;
	int max_vertex_attributes = 16;
	ProgramHandle last_program = INVALID_PROGRAM;
	StateFlags last_state = StateFlags::NONE;
	GLuint framebuffer = 0;
	ProgramHandle default_program = INVALID_PROGRAM;
	bool has_gpu_mem_info_ext = false;
};

Local<GL> gl;

namespace DDS
{

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

void checkThread()
{
	ASSERT(gl->thread == os::getCurrentThreadID());
}

void launchRenderDoc() {
	if (gl->rdoc_api) {
		gl->rdoc_api->LaunchReplayUI(1, "");
	}
}

static void try_load_renderdoc()
{
	#ifdef _WIN32
		void* lib = os::loadLibrary("renderdoc.dll");
		if (!lib) lib = os::loadLibrary("C:\\Program Files\\RenderDoc\\renderdoc.dll");
		if (!lib) return;
		pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)os::getLibrarySymbol(lib, "RENDERDOC_GetAPI");
		if (RENDERDOC_GetAPI) {
			RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_0_2, (void **)&gl->rdoc_api);
			gl->rdoc_api->MaskOverlayBits(~RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled, 0);
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
		logInfo("OpenGL version: ", version);
		logInfo("OpenGL vendor: ", vendor);
		logInfo("OpenGL renderer: ", renderer);
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
				logError("Failed to load GL function " #name "."); \
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

static bool load_gl(void* platform_handle, InitFlags init_flags)
{
	#ifdef _WIN32
		const bool vsync = u32(init_flags & InitFlags::VSYNC);
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
			logError("wglCreateContextAttribsARB failed, GetLastError() = ", (u32) err); 
			logError("OpenGL 4.5+ required");
			logVersion();
			return false;
		}
		logVersion();
		gl->contexts[0].hglrc = hglrc;
		wglSwapIntervalEXT(vsync ? 1 : 0);
		void* gl_dll = os::loadLibrary("opengl32.dll");

		#define GPU_GL_IMPORT(prototype, name) \
			do { \
				name = (prototype)getGLFunc(#name); \
				if (!name && gl_dll) { \
					name = (prototype)os::getLibrarySymbol(gl_dll, #name); \
					if (!name) { \
						logError("Failed to load GL function " #name "."); \
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
	++attributes_count;
	hash = crc32(attributes, sizeof(Attribute) * attributes_count);
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
			glVertexAttribIFormat(attr.idx, attr.components_count, gl_attr_type, attr.byte_offset);
		}
		else {
			glVertexAttribFormat(attr.idx, attr.components_count, gl_attr_type, normalized, attr.byte_offset);
		}
		glEnableVertexAttribArray(attr.idx);
		mask |= 1 << attr.idx;
		glVertexAttribBinding(attr.idx, instanced ? 1 : 0);
	}

	for (u32 i = 0; i < 16; ++i) {
		if (!(mask & (1 << i))) {
			glDisableVertexAttribArray(i);
		}
	}
}

void dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z)
{
	glDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
}

void useProgram(ProgramHandle program)
{
	const Program* prev = gl->last_program;
	if (prev != program) {
		gl->last_program = program;
		if (program) {
			glUseProgram(program->gl_handle);

			if (!prev || program->decl.hash != prev->decl.hash) {
				setVAO(program->decl);
			}
		}
		else {
			glUseProgram(0);
		}
	}
}

void bindImageTexture(TextureHandle texture, u32 unit) {
	if (texture) {
		glBindImageTexture(unit, texture->gl_handle, 0, GL_TRUE, 0, GL_READ_WRITE, texture->format);
	}
	else {
		glBindImageTexture(unit, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	}
}

void bindTextures(const TextureHandle* handles, u32 offset, u32 count)
{
	GLuint gl_handles[64];
	ASSERT(count <= lengthOf(gl_handles));
	ASSERT(handles);
	
	for(u32 i = 0; i < count; ++i) {
		if (handles[i]) {
			gl_handles[i] = handles[i]->gl_handle;
		}
		else {
			gl_handles[i] = 0;
		}
	}

	glBindTextures(offset, count, gl_handles);
}

void bindShaderBuffer(BufferHandle buffer, u32 binding_idx, BindShaderBufferFlags flags)
{
	checkThread();
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_idx, buffer ? buffer->gl_handle : 0);
}

void bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride_offset) {
	checkThread();
	ASSERT(binding_idx < 2);
	glBindVertexBuffer(binding_idx, buffer ? buffer->gl_handle : 0, buffer_offset, stride_offset);
}


void setState(StateFlags state)
{
	checkThread();
	
	if(state == gl->last_state) return;
	gl->last_state = state;

	if (u64(state & StateFlags::DEPTH_TEST)) glEnable(GL_DEPTH_TEST);
	else glDisable(GL_DEPTH_TEST);
	
	glDepthMask(u64(state & StateFlags::DEPTH_WRITE) != 0);
	
	if (u64(state & StateFlags::SCISSOR_TEST)) glEnable(GL_SCISSOR_TEST);
	else glDisable(GL_SCISSOR_TEST);
	
	if (u64(state & StateFlags::CULL_BACK)) {
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
	else if(u64(state & StateFlags::CULL_FRONT)) {
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
	}
	else {
		glDisable(GL_CULL_FACE);
	}

	glPolygonMode(GL_FRONT_AND_BACK, u64(state & StateFlags::WIREFRAME) ? GL_LINE : GL_FILL);

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
			GL_ONE_MINUS_DST_ALPHA,
			GL_SRC1_COLOR,
			GL_ONE_MINUS_SRC1_COLOR,
			GL_SRC1_ALPHA,
			GL_ONE_MINUS_SRC1_ALPHA
		};
		return table[(int)factor];
	};

	u16 blend_bits = u16(u64(state) >> 6);

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
	
	glStencilMask(u8(u64(state) >> 22));
	const StencilFuncs func = (StencilFuncs)((u64(state) >> 30) & 0xf);
	if (func == StencilFuncs::DISABLE) {
		glDisable(GL_STENCIL_TEST);
	}
	else {
		const u8 ref = u8(u64(state) >> 34);
		const u8 mask = u8(u64(state) >> 42);
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
		const StencilOps sfail = StencilOps((u64(state) >> 50) & 0xf);
		const StencilOps zfail = StencilOps((u64(state) >> 54) & 0xf);
		const StencilOps zpass = StencilOps((u64(state) >> 58) & 0xf);
		glStencilOp(toGLOp(sfail), toGLOp(zfail), toGLOp(zpass));
	}
}


void bindIndexBuffer(BufferHandle buffer)
{
	checkThread();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer ? buffer->gl_handle : 0);
}


void bindIndirectBuffer(BufferHandle buffer)
{
	checkThread();
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer ? buffer->gl_handle : 0);
}


void drawElements(PrimitiveType primitive_type, u32 offset, u32 count, DataType type)
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

	glDrawElements(pt, count, t, (void*)(intptr_t)offset);
}

void drawIndirect(DataType index_type)
{
	const GLenum type = index_type == DataType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	glMultiDrawElementsIndirect(GL_TRIANGLES, type, nullptr, 1, 0);
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
		glMultiDrawElementsIndirect(GL_TRIANGLES, type, &mdi, 1, 0);
	}
	else*/ {
		glDrawElementsInstanced(GL_TRIANGLES, indices_count, type, 0, instances_count);
	}
}


void drawTriangles(u32 indices_byte_offset, u32 indices_count, DataType index_type)
{
	checkThread();

	const GLenum type = index_type == DataType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	glDrawElements(GL_TRIANGLES, indices_count, type, (const GLvoid*)(uintptr_t)indices_byte_offset);
}


void drawTriangleStripArraysInstanced(u32 indices_count, u32 instances_count)
{
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, indices_count, instances_count);
}


void drawArrays(PrimitiveType type, u32 offset, u32 count)
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

	glDrawArrays(pt, offset, count);
}

void bindUniformBuffer(u32 index, BufferHandle buffer, size_t offset, size_t size) {
	checkThread();
	glBindBufferRange(GL_UNIFORM_BUFFER, index, buffer ? buffer->gl_handle : 0, offset, size);
}


void* map(BufferHandle buffer, size_t size)
{
	checkThread();
	ASSERT(buffer);
	ASSERT(u32(buffer->flags & BufferFlags::IMMUTABLE) == 0);
	const GLbitfield gl_flags = GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT;
	return glMapNamedBufferRange(buffer->gl_handle, 0, size, gl_flags);
}


void unmap(BufferHandle buffer)
{
	checkThread();
	ASSERT(buffer);
	glUnmapNamedBuffer(buffer->gl_handle);
}


void update(BufferHandle buffer, const void* data, size_t size)
{
	checkThread();
	ASSERT(buffer);
	ASSERT(u32(buffer->flags & BufferFlags::IMMUTABLE) == 0);
	const GLuint buf = buffer->gl_handle;
	glNamedBufferSubData(buf, 0, size, data);
}

void copy(BufferHandle dst, BufferHandle src, u32 dst_offset, u32 size)
{
	checkThread();
	ASSERT(src);
	ASSERT(dst);
	ASSERT(u32(dst->flags & BufferFlags::IMMUTABLE) == 0);

	glCopyNamedBufferSubData(src->gl_handle, dst->gl_handle, 0, dst_offset, size);
}

void startCapture()
{
	if (gl->rdoc_api) {
		gl->rdoc_api->StartFrameCapture(nullptr, nullptr);
	}
}


void stopCapture()
{
	if (gl->rdoc_api) {
		gl->rdoc_api->EndFrameCapture(nullptr, nullptr);
	}
}

static void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam)
{
	if(GL_DEBUG_TYPE_PUSH_GROUP == type || type == GL_DEBUG_TYPE_POP_GROUP) return;
	if (type == GL_DEBUG_TYPE_ERROR) {
		logError(message);
		//ASSERT(false);
	}
	else if (type == GL_DEBUG_TYPE_PERFORMANCE) {
		logInfo(message);
	}
	else {
		//logInfo("GL") << message;
	}
}

void setCurrentWindow(void* window_handle) {
	checkThread();

	#ifdef _WIN32
		WindowContext& ctx = [window_handle]() -> WindowContext& {
			if (!window_handle) return gl->contexts[0];

			for (WindowContext& i : gl->contexts) {
				if (i.window_handle == window_handle) return i;
			}

			for (WindowContext& i : gl->contexts) {
				if (!i.window_handle) {
					i.window_handle = window_handle;
					i.device_context = GetDC((HWND)window_handle);
					i.hglrc = 0;
					return i;
				}
			}
			LUMIX_FATAL(false);
			return gl->contexts[0];
		}();

		ctx.last_frame = gl->frame;

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

			wglMakeCurrent(hdc, gl->contexts[0].hglrc);

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
			HGLRC hglrc = wglCreateContextAttribsARB(hdc, gl->contexts[0].hglrc, contextAttrs);
			ctx.hglrc = hglrc;
			wglMakeCurrent(ctx.device_context, hglrc);
			glGenVertexArrays(1, &ctx.vao);
			glBindVertexArray(ctx.vao);
			glVertexBindingDivisor(0, 0);
			glVertexBindingDivisor(1, 1);

			#ifdef LUMIX_DEBUG
				glEnable(GL_DEBUG_OUTPUT);
				glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
				glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
				glDebugMessageCallback(gl_debug_callback, 0);
			#endif

		}

		wglMakeCurrent(ctx.device_context, ctx.hglrc);
	#endif
	useProgram(INVALID_PROGRAM);
}


u32 swapBuffers()
{
	checkThread();
	glFinish();
	#ifdef _WIN32
		for (WindowContext& ctx : gl->contexts) {
			if (!ctx.window_handle) continue;
			if (gl->frame == ctx.last_frame || &ctx == gl->contexts) {
				SwapBuffers(ctx.device_context);
			}
			else {
				BOOL res = wglMakeCurrent(ctx.device_context, ctx.hglrc);
				ASSERT(res);
				glDeleteVertexArrays(1, &ctx.vao);
				SwapBuffers(ctx.device_context);
				
				ASSERT(res);
				res = wglDeleteContext(ctx.hglrc);
				ctx.window_handle = nullptr;
				ASSERT(res);
			}
		}
		BOOL res = wglMakeCurrent(gl->contexts[0].device_context, gl->contexts[0].hglrc);
		ASSERT(res);
	#else
		glXSwapBuffers(gdisplay, (Window)gl->contexts[0].window_handle);
	#endif
	++gl->frame;
	return 0;
}

bool frameFinished(u32 frame) { return true; }
void waitFrame(u32 frame) {}

void createBuffer(BufferHandle buffer, BufferFlags flags, size_t size, const void* data)
{
	checkThread();
	ASSERT(buffer);
	GLuint buf;
	glCreateBuffers(1, &buf);
	
	GLbitfield gl_flags = 0;
	if (u64(flags & BufferFlags::IMMUTABLE) == 0) gl_flags |= GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_READ_BIT;
	glNamedBufferStorage(buf, size, data, gl_flags);

	buffer->gl_handle = buf;
	buffer->flags = flags;
}

void destroy(ProgramHandle program)
{
	checkThread();
	LUMIX_DELETE(gl->allocator, program);
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
	{TextureFormat::R32F, GL_R32F, GL_RED, GL_FLOAT},
	{TextureFormat::RG32F, GL_RG32F, GL_RG, GL_FLOAT}
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


void update(TextureHandle texture, u32 level, u32 slice, u32 x, u32 y, u32 w, u32 h, TextureFormat format, void* buf)
{
	ASSERT(texture);
	checkThread();
	const GLuint handle = texture->gl_handle;
	for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
		if (s_texture_formats[i].format == format) {
			const auto& f = s_texture_formats[i];
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			if (u32(texture->flags & TextureFlags::IS_CUBE) || texture->depth > 1) {
				glTextureSubImage3D(handle, level, x, y, slice, w, h, 1, f.gl_format, f.type, buf);
			}
			else {
				ASSERT(slice == 0);
				glTextureSubImage2D(handle, level, x, y, w, h, f.gl_format, f.type, buf);
			}
			break;
		}
	}
}


bool loadTexture(TextureHandle handle, const void* input, int input_size, TextureFlags flags, const char* debug_name)
{
	ASSERT(debug_name && debug_name[0]);
	ASSERT(handle);
	checkThread();
	DDS::Header hdr;

	InputMemoryStream blob(input, input_size);
	blob.read(&hdr, sizeof(hdr));

	if (hdr.dwMagic != DDS::DDS_MAGIC || hdr.dwSize != 124 ||
		!(hdr.dwFlags & DDS::DDSD_PIXELFORMAT) || !(hdr.dwFlags & DDS::DDSD_CAPS))
	{
		logError("Wrong dds format or corrupted dds (", debug_name, ")");
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
	const bool is_srgb = u32(flags & TextureFlags::SRGB);
	const GLenum internal_format = is_srgb ? li->internalSRGBFormat : li->internalFormat;
	const u32 mipMapCount = (hdr.dwFlags & DDS::DDSD_MIPMAPCOUNT) ? hdr.dwMipMapCount : 1;

	GLuint texture;
	glCreateTextures(texture_target, 1, &texture);
	if (texture == 0) {
		return false;
	}
	if(layers > 1) {
		glTextureStorage3D(texture, mipMapCount, internal_format, hdr.dwWidth, hdr.dwHeight, layers);
	}
	else {
		glTextureStorage2D(texture, mipMapCount, internal_format, hdr.dwWidth, hdr.dwHeight);
	}
	if (debug_name && debug_name[0]) {
		glObjectLabel(GL_TEXTURE, texture, stringLength(debug_name), debug_name);
	}

	OutputMemoryStream unpacked(gl->allocator);

	for (int layer = 0; layer < layers; ++layer) {
		for(int side = 0; side < (is_cubemap ? 6 : 1); ++side) {
			u32 width = hdr.dwWidth;
			u32 height = hdr.dwHeight;

			if (li->compressed) {
				u32 size = DDS::sizeDXTC(width, height, internal_format);
				if (!is_dds10 && !is_cubemap && (size != hdr.dwPitchOrLinearSize || (hdr.dwFlags & DDS::DDSD_LINEARSIZE) == 0)) {
					logError("Unsupported format ", debug_name);
					glDeleteTextures(1, &texture);
					return false;
				}
				for (u32 mip = 0; mip < mipMapCount; ++mip) {
					const u8* data_ptr = (u8*)blob.skip(size);
					if(layers > 1) {
						glCompressedTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, internal_format, size, data_ptr);
					}
					else if (is_cubemap) {
						ASSERT(layer == 0);
						glCompressedTextureSubImage3D(texture, mip, 0, 0, side, width, height, 1, internal_format, size, data_ptr);
					}
					else {
						glCompressedTextureSubImage2D(texture, mip, 0, 0, width, height, internal_format, size, data_ptr);
					}
					glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					width = maximum(1, width >> 1);
					height = maximum(1, height >> 1);
					size = DDS::sizeDXTC(width, height, internal_format);
				}
			}
			else if (li->palette) {
				if ((hdr.dwFlags & DDS::DDSD_PITCH) == 0 || hdr.pixelFormat.dwRGBBitCount != 8) {
					glDeleteTextures(1, &texture);
					return false;
				}
				u32 size = hdr.dwPitchOrLinearSize * height;
				if (size != width * height * li->blockBytes) {
					glDeleteTextures(1, &texture);
					return false;
				}
				unpacked.resize(size);
				u32* unpacked_ptr = (u32*)unpacked.getMutableData();
				const u32* palette = (u32*)blob.skip(4 * 256);
				for (u32 mip = 0; mip < mipMapCount; ++mip) {
					const u8* data_ptr = (u8*)blob.skip(size);
					for (u32 zz = 0; zz < size; ++zz) {
						unpacked_ptr[zz] = palette[data_ptr[zz]];
					}
					//glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
					if(layers > 1) {
						glTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, li->externalFormat, li->type, unpacked_ptr);
					}
					else {
						glTextureSubImage2D(texture, mip, 0, 0, width, height, li->externalFormat, li->type, unpacked_ptr);
					}
					width = maximum(1, width >> 1);
					height = maximum(1, height >> 1);
					size = width * height * li->blockBytes;
				}
			}
			else {
				if (li->swap) {
					glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
				}
				u32 size = width * height * li->blockBytes;
				for (u32 mip = 0; mip < mipMapCount; ++mip) {
					const u8* data_ptr = (u8*)blob.skip(size);
					//glPixelStorei(GL_UNPACK_ROW_LENGTH, height);
					if (layers > 1) {
						glTextureSubImage3D(texture, mip, 0, 0, layer, width, height, 1, li->externalFormat, li->type, data_ptr);
					}
					else {
						glTextureSubImage2D(texture, mip, 0, 0, width, height, li->externalFormat, li->type, data_ptr);
					}
					width = maximum(1, width >> 1);
					height = maximum(1, height >> 1);
					size = width * height * li->blockBytes;
				}
				glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
			}
			glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
		}
	}

	const GLint wrap_u = u32(flags & TextureFlags::CLAMP_U) ? GL_CLAMP : GL_REPEAT;
	const GLint wrap_v = u32(flags & TextureFlags::CLAMP_V) ? GL_CLAMP : GL_REPEAT;
	const GLint wrap_w = u32(flags & TextureFlags::CLAMP_W) ? GL_CLAMP : GL_REPEAT;
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap_u);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap_v);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_R, wrap_w);

	Texture& t = *handle;
	t.format = internal_format;
	t.gl_handle = texture;
	t.target = is_cubemap ? GL_TEXTURE_CUBE_MAP : layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	t.width = hdr.dwWidth;
	t.height = hdr.dwHeight;
	t.depth = layers;
	t.flags = flags;
	return true;
}


ProgramHandle allocProgramHandle()
{
	Program* p = LUMIX_NEW(gl->allocator, Program)();

	p->gl_handle = gl->default_program ? gl->default_program->gl_handle : 0;
	return p;
}


BufferHandle allocBufferHandle()
{
	Buffer* b = LUMIX_NEW(gl->allocator, Buffer);
	b->gl_handle = 0;
	return b;
}

TextureHandle allocTextureHandle()
{
	Texture* t = LUMIX_NEW(gl->allocator, Texture);
	t->gl_handle = 0;
	return t;
}


void createTextureView(TextureHandle view, TextureHandle texture)
{
	checkThread();
	
	ASSERT(texture);
	ASSERT(view);

	if (view->gl_handle != 0) {
		glDeleteTextures(1, &view->gl_handle);
	}

	view->target = GL_TEXTURE_2D;
	view->format = texture->format;

	glGenTextures(1, &view->gl_handle);
	glTextureView(view->gl_handle, GL_TEXTURE_2D, texture->gl_handle, texture->format, 0, 1, 0, 1);
	view->width = texture->width;
	view->height = texture->height;
}


bool createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, TextureFlags flags, const void* data, const char* debug_name)
{
	checkThread();
	ASSERT(handle);
	const bool is_srgb = u32(flags & TextureFlags::SRGB);
	const bool no_mips = u32(flags & TextureFlags::NO_MIPS);
	const bool is_3d = depth > 1 && u32(flags & TextureFlags::IS_3D);
	const bool is_cubemap = u32(flags & TextureFlags::IS_CUBE);

	ASSERT(!is_cubemap || depth == 1);
	ASSERT(!is_cubemap || !is_3d);
	ASSERT(!is_cubemap || no_mips || !data);
	ASSERT(!is_srgb); // use format argument to enable srgb
	ASSERT(debug_name && debug_name[0]);

	GLuint texture;
	int found_format = 0;
	GLenum internal_format = 0;
	const GLenum target = is_3d ? GL_TEXTURE_3D : (is_cubemap ? GL_TEXTURE_CUBE_MAP : (depth > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D));
	const u32 mip_count = no_mips ? 1 : 1 + log2(maximum(w, h, depth));

	glCreateTextures(target, 1, &texture);
	for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
		if(s_texture_formats[i].format == format) {
			internal_format = s_texture_formats[i].gl_internal;
			if(depth <= 1) {
				glTextureStorage2D(texture, mip_count, s_texture_formats[i].gl_internal, w, h);
				if (data) {
					if (is_cubemap) {
						for (u32 face = 0; face < 6; ++face) {
							ASSERT(format == TextureFormat::RGBA32F);
							glTextureSubImage3D(texture
								, 0
								, 0
								, 0
								, face
								, w
								, h
								, 1
								, s_texture_formats[i].gl_format
								, s_texture_formats[i].type
								, ((u8*)data) + face * w * h * sizeof(float) * 4);
						}
					}
					else {
						glTextureSubImage2D(texture
							, 0
							, 0
							, 0
							, w
							, h
							, s_texture_formats[i].gl_format
							, s_texture_formats[i].type
							, data);
					}
				}
			}
			else {
				glTextureStorage3D(texture, mip_count, s_texture_formats[i].gl_internal, w, h, depth);
				if (data) {
					glTextureSubImage3D(texture
						, 0
						, 0
						, 0
						, 0
						, w
						, h
						, depth
						, s_texture_formats[i].gl_format
						, s_texture_formats[i].type
						, data);
				}
			}
			found_format = 1;
			break;
		}
	}

	if(!found_format) {
		glDeleteTextures(1, &texture);
		ASSERT(false);
		return false;	
	}

	glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, mip_count - 1);

	if(debug_name && debug_name[0]) {
		glObjectLabel(GL_TEXTURE, texture, stringLength(debug_name), debug_name);
	}
	glGenerateTextureMipmap(texture);
	
	const GLint wrap_u = u32(flags & TextureFlags::CLAMP_U) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	const GLint wrap_v = u32(flags & TextureFlags::CLAMP_V) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	const GLint wrap_w = u32(flags & TextureFlags::CLAMP_W) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap_u);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap_v);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_R, wrap_w);
	if (u32(flags & TextureFlags::POINT_FILTER)) {
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else {
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, no_mips ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
	}

	handle->gl_handle = texture;
	handle->target = target;
	handle->format = internal_format;
	handle->width = w;
	handle->height = h;
	handle->depth = depth;
	handle->flags = flags;
	#ifdef LUMIX_DEBUG
		handle->name = debug_name;
	#endif
	return true;
}

void generateMipmaps(TextureHandle texture)
{
	ASSERT(texture);
	glGenerateTextureMipmap(texture->gl_handle);
}

void destroy(TextureHandle texture)
{
	checkThread();
	LUMIX_DELETE(gl->allocator, texture);
}

void destroy(BufferHandle buffer) {
	checkThread();
	LUMIX_DELETE(gl->allocator, buffer);
}

void clear(ClearFlags flags, const float* color, float depth)
{
	glUseProgram(0);
	gl->last_program = INVALID_PROGRAM;
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	gl->last_state = gl->last_state & ~StateFlags(0xffFF << 6);
	checkThread();
	GLbitfield gl_flags = 0;
	if (u32(flags & ClearFlags::COLOR)) {
		glClearColor(color[0], color[1], color[2], color[3]);
		gl_flags |= GL_COLOR_BUFFER_BIT;
	}
	if (u32(flags & ClearFlags::DEPTH)) {
		glDepthMask(GL_TRUE);
		glClearDepth(depth);
		gl_flags |= GL_DEPTH_BUFFER_BIT;
	}
	if (u32(flags & ClearFlags::STENCIL)) {
		glStencilMask(0xff);
		gl->last_state = gl->last_state | StateFlags(0xff << 22);
		glClearStencil(0);
		gl_flags |= GL_STENCIL_BUFFER_BIT;
	}
	glClear(gl_flags);
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
		logError("Too many shaders per program in ", name);
		return false;
	}

	const GLuint prg = glCreateProgram();
	if (name && name[0]) {
		glObjectLabel(GL_PROGRAM, prg, stringLength(name), name);
	}

	for (u32 i = 0; i < num; ++i) {
		GLenum shader_type;
		u32 src_idx = 0;
		combined_srcs[0] = R"#(
			#version 430
			#extension GL_ARB_shader_storage_buffer_object : enable
			#extension GL_ARB_explicit_attrib_location : enable
			#extension GL_ARB_shading_language_420pack : enable
			#extension GL_ARB_separate_shader_objects : enable
			#define _ORIGIN_BOTTOM_LEFT
		)#";
		++src_idx;
		switch (types[i]) {
			case ShaderType::GEOMETRY: {
				combined_srcs[src_idx] = "#define LUMIX_GEOMETRY_SHADER\n"; 
				shader_type = GL_GEOMETRY_SHADER;
				break;
			}
			case ShaderType::COMPUTE: {
				combined_srcs[src_idx] = "#define LUMIX_COMPUTE_SHADER\n"; 
				shader_type = GL_COMPUTE_SHADER;
				break;
			}
			case ShaderType::FRAGMENT: {
				combined_srcs[src_idx] = "#define LUMIX_FRAGMENT_SHADER\n"; 
				shader_type = GL_FRAGMENT_SHADER;
				break;
			}
			case ShaderType::VERTEX: {
				combined_srcs[src_idx] = "#define LUMIX_VERTEX_SHADER\n"; 
				shader_type = GL_VERTEX_SHADER;
				break;
			}
			default: ASSERT(false); return false;
		}
		++src_idx;
		const GLuint shd = glCreateShader(shader_type);
		for (u32 j = 0; j < prefixes_count; ++j) {
			combined_srcs[src_idx] = prefixes[j];
			++src_idx;
		}
		for (u32 j = 0; j < decl.attributes_count; ++j) {
			combined_srcs[src_idx] = attr_defines[decl.attributes[j].idx];
			++src_idx;
		}
		combined_srcs[src_idx] = srcs[i];
		++src_idx;

		glShaderSource(shd, src_idx, combined_srcs, 0);
		glCompileShader(shd);

		GLint compile_status;
		glGetShaderiv(shd, GL_COMPILE_STATUS, &compile_status);
		if (compile_status == GL_FALSE) {
			GLint log_len = 0;
			glGetShaderiv(shd, GL_INFO_LOG_LENGTH, &log_len);
			if (log_len > 0) {
				Array<char> log_buf(gl->allocator);
				log_buf.resize(log_len);
				glGetShaderInfoLog(shd, log_len, &log_len, &log_buf[0]);
				logError(name, " - ", shaderTypeToString(types[i]), ": ", &log_buf[0]);
			}
			else {
				logError("Failed to compile shader ", name, " - ", shaderTypeToString(types[i]));
			}
			glDeleteShader(shd);
			return false;
		}

		glAttachShader(prg, shd);
		glDeleteShader(shd);
	}

	glLinkProgram(prg);
	GLint linked;
	glGetProgramiv(prg, GL_LINK_STATUS, &linked);

	if (linked == GL_FALSE) {
		GLint log_len = 0;
		glGetProgramiv(prg, GL_INFO_LOG_LENGTH, &log_len);
		if (log_len > 0) {
			Array<char> log_buf(gl->allocator);
			log_buf.resize(log_len);
			glGetProgramInfoLog(prg, log_len, &log_len, &log_buf[0]);
			logError(name, ": ", &log_buf[0]);
		}
		else {
			logError("Failed to link program ", name);
		}
		glDeleteProgram(prg);
		return false;
	}

	ASSERT(prog);
	prog->gl_handle = prg;
	prog->decl = decl;
	return true;
}


void preinit(IAllocator& allocator, bool load_renderdoc)
{
	gl.create(allocator);
	if (load_renderdoc) try_load_renderdoc();
}


bool getMemoryStats(Ref<MemoryStats> stats) {
	if (!gl->has_gpu_mem_info_ext) return false;

	GLint tmp;
	glGetIntegerv(GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &tmp);
	stats->total_available_mem = (u64)tmp * 1024;

	glGetIntegerv(GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &tmp);
	stats->current_available_mem = (u64)tmp * 1024;

	glGetIntegerv(GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &tmp);
	stats->dedicated_vidmem = (u64)tmp * 1024;
	
	return true;
}


bool init(void* window_handle, InitFlags init_flags)
{
	#ifdef LUMIX_DEBUG
		const bool debug = true;
	#else 
		const bool debug = u32(init_flags & InitFlags::DEBUG_OUTPUT);
	#endif
	
	gl->thread = os::getCurrentThreadID();
	gl->contexts[0].window_handle = window_handle;
	#ifdef _WIN32
		gl->contexts[0].device_context = GetDC((HWND)window_handle);
		if (!load_gl(gl->contexts[0].device_context, init_flags)) return false;
	#else
		if (!load_gl(window_handle, init_flags)) return false;
	#endif

	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &gl->max_vertex_attributes);

	int extensions_count;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensions_count);
	gl->has_gpu_mem_info_ext = false; 
	for(int i = 0; i < extensions_count; ++i) {
		const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if (equalStrings(ext, "GL_NVX_gpu_memory_info")) {
			gl->has_gpu_mem_info_ext = true; 
			break;
		}
		//OutputDebugString(ext);
		//OutputDebugString("\n");
	}
	//const unsigned char* version = glGetString(GL_VERSION);

	glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	glDepthFunc(GL_GREATER);

	if (debug) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
		glDebugMessageCallback(gl_debug_callback, 0);
	}

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	glBindVertexArray(0);	
	glCreateFramebuffers(1, &gl->framebuffer);

	
	gl->default_program = allocProgramHandle();
	ASSERT(gl->default_program);
	Program& p = *gl->default_program;
	p.gl_handle = glCreateProgram();
	glGenVertexArrays(1, &gl->contexts[0].vao);
	glBindVertexArray(gl->contexts[0].vao);
	glVertexBindingDivisor(0, 0);
	glVertexBindingDivisor(1, 1);

	const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	const char* vs_src = "void main() { gl_Position = vec4(0, 0, 0, 0); }";
	glShaderSource(vs, 1, &vs_src, nullptr);
	glCompileShader(vs);
	glAttachShader(p.gl_handle, vs);
	glLinkProgram(p.gl_handle);
	glDeleteShader(vs);

	gl->last_state = StateFlags(1);
	setState(StateFlags::NONE);

	return true;
}


bool isOriginBottomLeft() { return true; }


void copy(TextureHandle dst, TextureHandle src, u32 dst_x, u32 dst_y) {
	checkThread();
	ASSERT(dst);
	ASSERT(src);
	ASSERT(src->target == GL_TEXTURE_2D || src->target == GL_TEXTURE_CUBE_MAP);
	ASSERT(src->target == dst->target);

	u32 mip = 0;
	while ((src->width >> mip) != 0 || (src->height >> mip) != 0) {
		const u32 w = maximum(src->width >> mip, 1);
		const u32 h = maximum(src->height >> mip, 1);

		if (src->target == GL_TEXTURE_CUBE_MAP) {
			glCopyImageSubData(src->gl_handle, src->target, mip, 0, 0, 0, dst->gl_handle, dst->target, mip, dst_x, dst_y, 0, w, h, 6);
		}
		else {
			glCopyImageSubData(src->gl_handle, src->target, mip, 0, 0, 0, dst->gl_handle, dst->target, mip, dst_x, dst_y, 0, w, h, 1);
		}
		++mip;
		if (u32(src->flags & TextureFlags::NO_MIPS)) break;
		if (u32(dst->flags & TextureFlags::NO_MIPS)) break;
	}
}

void readTexture(TextureHandle texture, u32 mip, Span<u8> buf)
{
	checkThread();
	ASSERT(texture);
	const GLuint handle = texture->gl_handle;

	for (int i = 0; i < sizeof(s_texture_formats) / sizeof(s_texture_formats[0]); ++i) {
		if (s_texture_formats[i].gl_internal == texture->format) {
			const auto& f = s_texture_formats[i];
			glGetTextureImage(handle, mip, f.gl_format, f.type, buf.length(), buf.begin());
			return;
		}
	}
	ASSERT(false);
}


void popDebugGroup()
{
	checkThread();
	glPopDebugGroup();
}


void pushDebugGroup(const char* msg)
{
	checkThread();
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, msg);
}


QueryHandle createQuery()
{
	GLuint q;
	glGenQueries(1, &q);
	ASSERT(q != 0);
	return (Query*)(uintptr_t)q;
}


bool isQueryReady(QueryHandle query)
{
	GLuint done;
	glGetQueryObjectuiv((GLuint)(uintptr_t)query, GL_QUERY_RESULT_AVAILABLE, &done);
	return done;
}

u64 getQueryFrequency() { return 1'000'000'000; }

u64 getQueryResult(QueryHandle query)
{
	u64 time;
	glGetQueryObjectui64v((GLuint)(uintptr_t)query, GL_QUERY_RESULT, &time);
	return time;
}


void destroy(QueryHandle query)
{
	GLuint q = (GLuint)(uintptr_t)query;
	glDeleteQueries(1, &q);
}


void queryTimestamp(QueryHandle query)
{
	glQueryCounter((GLuint)(uintptr_t)query, GL_TIMESTAMP);
}

void setFramebufferCube(TextureHandle cube, u32 face, u32 mip)
{
	ASSERT(cube);
	const GLuint t = cube->gl_handle;
	checkThread();
	glDisable(GL_FRAMEBUFFER_SRGB);
	glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, t, mip);

	GLint max_attachments = 0;
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
	for(int i = 1; i < max_attachments; ++i) {
		glNamedFramebufferRenderbuffer(gl->framebuffer, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, 0);
	}
	glNamedFramebufferRenderbuffer(gl->framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
	glNamedFramebufferRenderbuffer(gl->framebuffer, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
	auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	ASSERT(status == GL_FRAMEBUFFER_COMPLETE);

	GLenum db = GL_COLOR_ATTACHMENT0;
	
	glDrawBuffers(1, &db);
}

void setFramebuffer(TextureHandle* attachments, u32 num, TextureHandle ds, FramebufferFlags flags)
{
	checkThread();

	if (u32(flags & FramebufferFlags::SRGB)) {
		glEnable(GL_FRAMEBUFFER_SRGB);
	}
	else {
		glDisable(GL_FRAMEBUFFER_SRGB);
	}

	if((!attachments || num == 0) && !ds) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return;
	}

	for (u32 i = 0; i < num; ++i) {
		ASSERT(attachments[i]);
		const GLuint t = attachments[i]->gl_handle;
		glBindTexture(GL_TEXTURE_2D, t);
		glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, t, 0);
	}

	if (ds) {
		switch(ds->format) {
			case GL_DEPTH24_STENCIL8:
				glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, ds->gl_handle, 0);
				break;
			case GL_DEPTH_COMPONENT24:
			case GL_DEPTH_COMPONENT32:
				glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ds->gl_handle, 0);
				break;
			default: ASSERT(false);
		}
	}
	else {
		glNamedFramebufferRenderbuffer(gl->framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
		glNamedFramebufferRenderbuffer(gl->framebuffer, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	}

	GLint max_attachments = 0;
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
	for(int i = num; i < max_attachments; ++i) {
		glNamedFramebufferRenderbuffer(gl->framebuffer, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
	auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	ASSERT(status == GL_FRAMEBUFFER_COMPLETE);

	GLenum db[16];
	for (u32 i = 0; i < lengthOf(db); ++i) db[i] = GL_COLOR_ATTACHMENT0 + i;
	
	glDrawBuffers(num, db);
}


void shutdown()
{
	checkThread();
	destroy(gl->default_program);
	for (WindowContext& ctx : gl->contexts) {
		if (!ctx.window_handle) continue;
		#ifdef _WIN32
			wglMakeCurrent(ctx.device_context, 0);
			wglDeleteContext(ctx.hglrc);
		#endif
	}
	gl.destroy();
}

} // namespace gpu

} // namespace Lumix
