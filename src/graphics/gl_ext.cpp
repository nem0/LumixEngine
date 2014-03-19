#include "graphics/gl_ext.h"
#include <cstdio>

PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLDELETEBUFFERSPROC glDeleteBuffers;
PFNGLGENBUFFERSPROC glGenBuffers;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLLINGPROGRAMPROC glLinkProgram;
PFNGLGETATTRIBLOCATION glGetAttribLocation;
PFNGLUSEPROGRAMPROC glUseProgram;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
PFNGLPROGRAMUNIFORM1IPROC glProgramUniform1i;
PFNGLPROGRAMUNIFORM1FPROC glProgramUniform1f;
PFNGLPROGRAMUNIFORM3FPROC glProgramUniform3f;
PFNGLPROGRAMUNIFORMMATRIX4FVPROC glProgramUniformMatrix4fv;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
PFNGLDELETEPROGRAMPROC glDeleteProgram;
PFNGLDELETESHADERPROC glDeleteShader;
PFNGLCREATESHADERPROC glCreateShader;
PFNGLATTACHSHADERPROC glAttachShader;
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLACTIVETEXTUREPROC glActiveTexture;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;


PROC loadGLExtension(const char* name, float gl_version)
{
	if (gl_version >= 3.0f)
	{
		return wglGetProcAddress(name);
	}
	else
	{
		char tmp[200];
		sprintf_s(tmp, "%sEXT", name);
		return wglGetProcAddress(tmp);
	}
}

void loadGLExtensions()
{
	const char* gl_version_str = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	float gl_version = (float)atof(gl_version_str);
	glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
	glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
	glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
	glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
	glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
	glLinkProgram = (PFNGLLINGPROGRAMPROC)wglGetProcAddress("glLinkProgram");
	glGetAttribLocation = (PFNGLGETATTRIBLOCATION)wglGetProcAddress("glGetAttribLocation");
	glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
	glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
	glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
	glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glDisableVertexAttribArray");
	glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
	glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
	glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
	glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
	glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
	glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
	glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
	glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
	
	
	glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)loadGLExtension("glGenFramebuffers", gl_version); 
	glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)loadGLExtension("glGenRenderbuffers", gl_version);
	glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)loadGLExtension("glBindFramebuffer", gl_version);
	glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)loadGLExtension("glFramebufferRenderbuffer", gl_version);
	glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)loadGLExtension("glFramebufferTexture2D", gl_version);
	glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)loadGLExtension("glRenderbufferStorage", gl_version);
	glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)loadGLExtension("glBindRenderbuffer", gl_version);
	glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)loadGLExtension("glDeleteFramebuffers", gl_version);
	glProgramUniformMatrix4fv = (PFNGLPROGRAMUNIFORMMATRIX4FVPROC)loadGLExtension("glProgramUniformMatrix4fv", gl_version);
	glProgramUniform1i = (PFNGLPROGRAMUNIFORM1IPROC)loadGLExtension("glProgramUniform1i", gl_version);
	glProgramUniform1f = (PFNGLPROGRAMUNIFORM1FPROC)loadGLExtension("glProgramUniform1f", gl_version);
	glProgramUniform3f = (PFNGLPROGRAMUNIFORM3FPROC)loadGLExtension("glProgramUniform3f", gl_version);

}