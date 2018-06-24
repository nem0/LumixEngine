#ifndef GL_FRAGMENT_SHADER
	#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
	#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
	#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_INFO_LOG_LENGTH
	#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_LINK_STATUS
	#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
	#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_ARRAY_BUFFER
	#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
	#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_TEXTURE0
	#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE_BUFFER
	#define GL_TEXTURE_BUFFER 0x8C2A
#endif
#ifndef GL_R32F
	#define GL_R32F 0x822E
#endif
#ifndef GL_RGBA32F
	#define GL_RGBA32F 0x8814
#endif
#ifndef GL_FRAMEBUFFER
	#define GL_FRAMEBUFFER 0x8D40
#endif

#ifdef FFR_GL_IMPORT_TYPEDEFS
typedef void (APIENTRY  *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam);

typedef void (APIENTRY* PFNGLACTIVETEXTUREPROC) (GLenum texture);
typedef void (APIENTRY* PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRY* PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRY* PFNGLBINDFRAMEBUFFERPROC) (GLenum target, GLuint framebuffer);
typedef void (APIENTRY* PFNGLBINDVERTEXARRAYPROC) (GLuint array);
typedef void (APIENTRY* PFNGLBUFFERDATAPROC) (GLenum target, ptrdiff_t size, const void *data, GLenum usage);
typedef void (APIENTRY* PFNGLBUFFERSUBDATAPROC) (GLenum target, ptrdiff_t offset, ptrdiff_t size, const void *data);
typedef void (APIENTRY* PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint (APIENTRY* PFNGLCREATEPROGRAMPROC) (void);
typedef GLuint (APIENTRY* PFNGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRY* PFNGLDEBUGMESSAGECALLBACKPROC) (GLDEBUGPROC callback, const void *userParam);
typedef void (APIENTRY* PFNGLDEBUGMESSAGECONTROLPROC) (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled);
typedef void (APIENTRY* PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);
typedef void (APIENTRY* PFNGLDELETEPROGRAMPROC) (GLuint program);
typedef void (APIENTRY* PFNGLDELETESHADERPROC) (GLuint shader);
typedef void (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRY* PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void (APIENTRY* PFNGLGENERATEMIPMAPPROC) (GLenum target);
typedef void (APIENTRY* PFNGLGENVERTEXARRAYSPROC) (GLsizei n, GLuint *arrays);
typedef void (APIENTRY* PFNGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei *length, char *infoLog);
typedef void (APIENTRY* PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog);
typedef void (APIENTRY* PFNGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint *params);
typedef GLint (APIENTRY* PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const char *name);
typedef void (APIENTRY* PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRY* PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const char ** tring, const GLint * length);
typedef void (APIENTRY* PFNGLTEXBUFFERPROC) (GLenum target, GLenum internalformat, GLuint buffer);
typedef void (APIENTRY* PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (APIENTRY* PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

#endif

FFR_GL_IMPORT(PFNGLACTIVETEXTUREPROC, glActiveTexture);
FFR_GL_IMPORT(PFNGLATTACHSHADERPROC, glAttachShader);
FFR_GL_IMPORT(PFNGLBINDBUFFERPROC, glBindBuffer);
FFR_GL_IMPORT(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
FFR_GL_IMPORT(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);
FFR_GL_IMPORT(PFNGLBUFFERDATAPROC, glBufferData);
FFR_GL_IMPORT(PFNGLBUFFERSUBDATAPROC, glBufferSubData);
FFR_GL_IMPORT(PFNGLCOMPILESHADERPROC, glCompileShader);
FFR_GL_IMPORT(PFNGLCREATEPROGRAMPROC, glCreateProgram);
FFR_GL_IMPORT(PFNGLCREATESHADERPROC, glCreateShader);
FFR_GL_IMPORT(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback);
FFR_GL_IMPORT(PFNGLDEBUGMESSAGECONTROLPROC, glDebugMessageControl);
FFR_GL_IMPORT(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);
FFR_GL_IMPORT(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
FFR_GL_IMPORT(PFNGLDELETESHADERPROC, glDeleteShader);
FFR_GL_IMPORT(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
FFR_GL_IMPORT(PFNGLGENBUFFERSPROC, glGenBuffers);
FFR_GL_IMPORT(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap);
FFR_GL_IMPORT(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays);
FFR_GL_IMPORT(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
FFR_GL_IMPORT(PFNGLGETPROGRAMIVPROC, glGetProgramiv);
FFR_GL_IMPORT(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
FFR_GL_IMPORT(PFNGLGETSHADERIVPROC, glGetShaderiv);
FFR_GL_IMPORT(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
FFR_GL_IMPORT(PFNGLLINKPROGRAMPROC, glLinkProgram);
FFR_GL_IMPORT(PFNGLSHADERSOURCEPROC, glShaderSource);
FFR_GL_IMPORT(PFNGLTEXBUFFERPROC, glTexBuffer);
FFR_GL_IMPORT(PFNGLUNIFORM1IPROC, glUniform1i);
FFR_GL_IMPORT(PFNGLUSEPROGRAMPROC, glUseProgram);
FFR_GL_IMPORT(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
