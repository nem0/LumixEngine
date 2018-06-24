#include "ffr.h"
#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <Windows.h>
#include <gl/GL.h>
#include "renderdoc_app.h"

#define FFR_GL_IMPORT(prototype, name) static prototype name;
#define FFR_GL_IMPORT_TYPEDEFS

#include "gl_ext.h"

#undef FFR_GL_IMPORT_TYPEDEFS
#undef FFR_GL_IMPORT

#define LOG(level, msg) do { (*s_ffr.init.log)(s_ffr.init.user_ptr, (FFR_LOG_##level), (msg)); } while(0)
#define CHECK_GL(gl) \
	do { \
		gl; \
		GLenum err = glGetError(); \
		if (err != GL_NO_ERROR) { \
			char buf[1024]; \
			sprintf_s(buf, sizeof(buf), "%s %d", "OpenGL error ", err); \
			LOG(ERROR, buf); \
		} \
	} while(0)

static struct {
	ffr_init_params init;
	RENDERDOC_API_1_1_2* rdoc_api;
	GLuint vao;
	GLuint tex_buffers[32];
} s_ffr;


static void default_log(void* user_ptr, ffr_log_level level, const char* msg)
{
	printf("%s", msg);
	if (IsDebuggerPresent()) {
		OutputDebugString("ffr: ");
		OutputDebugString(msg);
		OutputDebugString("\n");
	}
}


static void try_load_renderdoc()
{
	HMODULE lib = LoadLibrary("renderdoc.dll");
	if (!lib) return;
	pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(lib, "RENDERDOC_GetAPI");
	if (RENDERDOC_GetAPI) {
		RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&s_ffr.rdoc_api);
	}
	/**/
	//FreeLibrary(lib);
}


static int load_gl()
{
	#define FFR_GL_IMPORT(prototype, name) \
		do { \
			name = (prototype)wglGetProcAddress(#name); \
			if (!name) { \
				(*s_ffr.init.log)(s_ffr.init.user_ptr, (FFR_LOG_FATAL), ("Failed to load GL function " #name ".")); \
				return 0; \
			} \
		} while(0)

	#include "gl_ext.h"

	#undef FFR_GL_IMPORT

	return 1;
}


static void* default_alloc(void* user_ptr, size_t size, size_t align)
{
	return _aligned_malloc(size, align);
}


static void default_free(void* user_ptr, void* mem)
{
	_aligned_free(mem);
}


void ffr_viewport(ffr_uint x, ffr_uint y, ffr_uint w, ffr_uint h)
{
	glViewport(x, y, w, h);
}


void ffr_blend()
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


void ffr_scissor(ffr_uint x, ffr_uint y, ffr_uint w, ffr_uint h)
{
	glScissor(x, y, w, h);
}


void ffr_draw(const ffr_draw_call* dc)
{
	const GLuint prg = dc->shader.value;
	CHECK_GL(glUseProgram(prg));

	GLuint pt;
	switch (dc->primitive_type) {
		case FFR_PRIMITIVE_TYPE_TRIANGLES: pt = GL_TRIANGLES; break;
		case FFR_PRIMITIVE_TYPE_TRIANGLE_STRIP: pt = GL_TRIANGLE_STRIP; break;
		default: assert(0); break;
	}

	for (ffr_uint i = 0; i < dc->tex_buffers_count; ++i) {
		const GLuint buf = dc->tex_buffers[i].value;
		CHECK_GL(glActiveTexture(GL_TEXTURE0 + i));
		CHECK_GL(glBindTexture(GL_TEXTURE_BUFFER, s_ffr.tex_buffers[i]));
		CHECK_GL(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, buf));
		const GLint uniform_loc = glGetUniformLocation(prg, "test");
		CHECK_GL(glUniform1i(uniform_loc, i));
	}

	for (ffr_uint i = 0; i < dc->textures_count; ++i) {
		const GLuint t = dc->textures[i].value;
		glBindTexture(GL_TEXTURE_2D, t);
		const GLint uniform_loc = glGetUniformLocation(prg, "test");
		CHECK_GL(glUniform1i(uniform_loc, i));
	}

	if (dc->vertex_decl) {
		const ffr_vertex_decl* decl = dc->vertex_decl;
		const GLsizei stride = decl->size;
		const GLuint vb = dc->vertex_buffer.value;
		const ffr_uint vb_offset = dc->vertex_buffer_offset;
		glBindBuffer(GL_ARRAY_BUFFER, vb);

		for (ffr_uint i = 0; i < decl->attributes_count; ++i) {
			const ffr_attribute* attr = &decl->attributes[i];
			const void* offset = (void*)(intptr_t)(attr->offset + vb_offset);
			GLenum gl_attr_type;
			switch (attr->type) {
				case FFR_ATTRIBUTE_TYPE_FLOAT: gl_attr_type = GL_FLOAT; break;
				case FFR_ATTRIBUTE_TYPE_UBYTE: gl_attr_type = GL_UNSIGNED_BYTE; break;
			}
			glVertexAttribPointer(i, attr->size, gl_attr_type, attr->normalized, stride, offset);
			glEnableVertexAttribArray(i);
		}
	}

	if (dc->index_buffer.value != 0xffFFffFF) {
		const GLuint ib = dc->index_buffer.value;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);
		glDrawElements(pt, dc->indices_count, GL_UNSIGNED_SHORT, (void*)(intptr_t)(dc->indices_offset * sizeof(short)));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	else {
		CHECK_GL(glDrawArrays(pt, dc->indices_offset, dc->indices_count));
	}
}


void ffr_update_buffer(ffr_buffer_handle buffer, const void* data, size_t offset, size_t size)
{
	const GLuint buf = buffer.value;
	CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, buf));
	CHECK_GL(glBufferSubData(GL_ARRAY_BUFFER, offset, size, data));
	CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, 0));
}


ffr_buffer_handle ffr_create_buffer(size_t size, const void* data)
{
	GLuint buf;
	CHECK_GL(glGenBuffers(1, &buf));
	CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, buf));
	CHECK_GL(glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW));
	CHECK_GL(glBindBuffer(GL_ARRAY_BUFFER, 0));

	return (ffr_buffer_handle) { buf };
}


void ffr_destroy_program(ffr_program_handle program)
{
	glDeleteProgram(program.value);
}


ffr_texture_handle ffr_create_texture(ffr_uint w, ffr_uint h, const void* data)
{
	GLuint t;
	CHECK_GL(glGenTextures(1, &t));
	CHECK_GL(glBindTexture(GL_TEXTURE_2D, t));
	CHECK_GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	return (ffr_texture_handle) { t };
}


void ffr_destroy_texture(ffr_texture_handle texture)
{
	glDeleteTextures(1, &texture.value);
}


void ffr_destroy_buffer(ffr_buffer_handle buffer)
{
	glDeleteBuffers(1, &buffer.value);
}


void ffr_clear(ffr_uint flags, const float* color, float depth)
{
	GLbitfield gl_flags = 0;
	if (flags & FFR_CLEAR_FLAG_COLOR) {
		glClearColor(color[0], color[1], color[2], color[3]);
		gl_flags |= GL_COLOR_BUFFER_BIT;
	}
	if (flags & FFR_CLEAR_FLAG_DEPTH) {
		glClearDepth(depth);
		gl_flags |= GL_DEPTH_BUFFER_BIT;
	}
	glClear(gl_flags);
}


ffr_program_handle ffr_create_program(const char** srcs, const ffr_shader_type* types, int num)
{
	enum { MAX_SHADERS_PER_PROGRAM = 16 };

	if (num > MAX_SHADERS_PER_PROGRAM) {
		LOG(ERROR, "Too many shaders per program.");
		return (ffr_program_handle)FFR_INVALID_HANDLE;
	}

	const GLuint prg = glCreateProgram();

	for (int i = 0; i < num; ++i) {
		GLenum shader_type;
		switch (types[i]) {
			case FFR_SHADER_TYPE_FRAGMENT: shader_type = GL_FRAGMENT_SHADER; break;
			case FFR_SHADER_TYPE_VERTEX: shader_type = GL_VERTEX_SHADER; break;
			default: assert(0); break;
		}
		const GLuint shd = glCreateShader(shader_type);
		CHECK_GL(glShaderSource(shd, 1, &srcs[i], 0));
		CHECK_GL(glCompileShader(shd));

		GLint compile_status;
		CHECK_GL(glGetShaderiv(shd, GL_COMPILE_STATUS, &compile_status));
		if (compile_status == GL_FALSE) {
			GLint log_len = 0;
			CHECK_GL(glGetShaderiv(shd, GL_INFO_LOG_LENGTH, &log_len));
			if (log_len > 0) {
				char* log_buf = s_ffr.init.alloc(s_ffr.init.user_ptr, log_len, 16);
				CHECK_GL(glGetShaderInfoLog(shd, log_len, &log_len, log_buf));
				LOG(ERROR, log_buf);
				s_ffr.init.free(s_ffr.init.user_ptr, log_buf);
			}
			else {
				LOG(ERROR, "Failed to compile shader.");
			}
			CHECK_GL(glDeleteShader(shd));
			return (ffr_program_handle)FFR_INVALID_HANDLE;
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
			char* log_buf = s_ffr.init.alloc(s_ffr.init.user_ptr, log_len, 16);
			CHECK_GL(glGetProgramInfoLog(prg, log_len, &log_len, log_buf));
			LOG(ERROR, log_buf);
			s_ffr.init.free(s_ffr.init.user_ptr, log_buf);
		}
		else {
			LOG(ERROR, "Failed to link program.");
		}
		CHECK_GL(glDeleteProgram(prg));
		return (ffr_program_handle)FFR_INVALID_HANDLE;
	}

	return (ffr_program_handle) { prg };
}


static void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam)
{
	OutputDebugString("GL: ");
	OutputDebugString(message);
	OutputDebugString("\n");
}


void ffr_preinit()
{
	try_load_renderdoc();
}


ffr_init_result ffr_init(const ffr_init_params* params)
{
	if (params) {
		s_ffr.init = *params;
	}
	else {
		s_ffr.init.user_ptr = 0;
		s_ffr.init.log = default_log;
		s_ffr.init.alloc = default_alloc;
		s_ffr.init.free = default_free;
	}
	
	if (!load_gl()) return FFR_INIT_FAIL;

	CHECK_GL(glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE));
	CHECK_GL(glDebugMessageCallback(gl_debug_callback, 0));

	CHECK_GL(glGenVertexArrays(1, &s_ffr.vao));
	CHECK_GL(glBindVertexArray(s_ffr.vao));
	CHECK_GL(glGenTextures(_countof(s_ffr.tex_buffers), s_ffr.tex_buffers));

	LOG(INFO, "init successful");

	return FFR_INIT_SUCCESS;
}


void ffr_set_framebuffer(ffr_framebuffer_handle fb)
{
	assert(fb.value = 0xffFFffFF);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void ffr_shutdown()
{
}
