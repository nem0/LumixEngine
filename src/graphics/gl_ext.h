#pragma once
#include <Windows.h>
#include <gl/GL.h>

const GLuint GL_ARRAY_BUFFER = 0x8892;
const GLuint GL_FRAGMENT_SHADER = 0x8B30;
const GLuint GL_STATIC_DRAW = 0x88E4;
const GLuint GL_VERTEX_SHADER = 0x8B31;
const GLuint GL_CLAMP_TO_EDGE = 0x812F;
const GLuint GL_TEXTURE0 = 0x84C0;
const GLuint GL_TEXTURE1 = 0x84C1;
const GLuint GL_TEXTURE2 = 0x84C2;
const GLuint GL_TEXTURE3 = 0x84C3;
const GLuint GL_TEXTURE4 = 0x84C4;

typedef ptrdiff_t GLsizeiptr;
typedef char GLchar;

typedef void (WINAPI* PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (WINAPI* PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void (WINAPI* PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (WINAPI* PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
typedef GLuint (WINAPI* PFNGLCREATEPROGRAMPROC)();
typedef void (WINAPI* PFNGLLINGPROGRAMPROC)(GLuint program);
typedef GLint (WINAPI* PFNGLGETATTRIBLOCATION)(GLuint program, const GLchar *name);
typedef void (WINAPI* PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (WINAPI* PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (WINAPI* PFNGLPROGRAMUNIFORM1IPROC)(GLuint program, GLint location, GLint v0);
typedef void (WINAPI* PFNGLPROGRAMUNIFORM1FPROC)(GLuint program, GLint location, GLfloat v0);
typedef void (WINAPI* PFNGLPROGRAMUNIFORM3FPROC)(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (WINAPI* PFNGLPROGRAMUNIFORMMATRIX4FVPROC)(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (WINAPI* PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (WINAPI* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (WINAPI* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
typedef void (WINAPI* PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (WINAPI* PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint (WINAPI* PFNGLCREATESHADERPROC)(GLenum type);
typedef void (WINAPI* PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (WINAPI* PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* *string, const GLint *length);
typedef void (WINAPI* PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (WINAPI* PFNGLACTIVETEXTUREPROC)(GLenum texture);


extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLLINGPROGRAMPROC glLinkProgram;
extern PFNGLGETATTRIBLOCATION glGetAttribLocation;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLPROGRAMUNIFORM1IPROC glProgramUniform1i;
extern PFNGLPROGRAMUNIFORM1FPROC glProgramUniform1f;
extern PFNGLPROGRAMUNIFORM3FPROC glProgramUniform3f;
extern PFNGLPROGRAMUNIFORMMATRIX4FVPROC glProgramUniformMatrix4fv;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLACTIVETEXTUREPROC glActiveTexture;

void loadGLExtensions();