#pragma once

#ifdef __cplusplus
extern "C" {
#endif


typedef unsigned int ffr_uint;


typedef struct { ffr_uint value; } ffr_buffer_handle;
typedef struct { ffr_uint value; } ffr_program_handle;
typedef struct { ffr_uint value; } ffr_framebuffer_handle;
typedef struct { ffr_uint value; } ffr_texture_handle;


#define FFR_INVALID_HANDLE { 0xffFFffFF }


typedef enum {
	FFR_LOG_INFO,
	FFR_LOG_WARNING,
	FFR_LOG_ERROR,
	FFR_LOG_FATAL
} ffr_log_level;


typedef enum {
	FFR_INIT_SUCCESS,
	FFR_INIT_FAIL
} ffr_init_result;


typedef enum {
	FFR_PRIMITIVE_TYPE_TRIANGLES,
	FFR_PRIMITIVE_TYPE_TRIANGLE_STRIP
} ffr_primitive_type;


typedef enum {
	FFR_SHADER_TYPE_VERTEX,
	FFR_SHADER_TYPE_FRAGMENT,
} ffr_shader_type;


typedef enum {
	FFR_CLEAR_FLAG_COLOR = 1 << 0,
	FFR_CLEAR_FLAG_DEPTH = 1 << 1
} ffr_clear_flag;


typedef enum {
	FFR_ATTRIBUTE_TYPE_UBYTE,
	FFR_ATTRIBUTE_TYPE_FLOAT
} ffr_attribute_type;


typedef struct  {
	void* user_ptr;
	void (*log)(void* user_ptr, ffr_log_level level, const char* msg);
	void* (*alloc)(void* user_ptr, size_t size, size_t align);
	void (*free)(void* user_ptr, void* mem);
} ffr_init_params;


typedef struct {
	ffr_uint size;
	ffr_uint offset;
	ffr_uint normalized;
	ffr_attribute_type type;
} ffr_attribute;


typedef struct {
	ffr_uint size;
	ffr_uint attributes_count;
	ffr_attribute attributes[16];
} ffr_vertex_decl;


typedef struct {
	ffr_program_handle shader;
	ffr_primitive_type primitive_type;
	ffr_uint tex_buffers_count;
	const ffr_buffer_handle* tex_buffers;
	ffr_uint textures_count;
	const ffr_texture_handle* textures;
	ffr_uint indices_offset;
	ffr_uint indices_count;
	ffr_buffer_handle index_buffer;
	ffr_buffer_handle vertex_buffer;
	ffr_uint vertex_buffer_offset;
	const ffr_vertex_decl* vertex_decl;
} ffr_draw_call;


void ffr_preinit();
ffr_init_result ffr_init(const ffr_init_params* params);
void ffr_shutdown();

void ffr_clear(ffr_uint flags, const float* color, float depth);

void ffr_scissor(ffr_uint x, ffr_uint y, ffr_uint w, ffr_uint h);
void ffr_viewport(ffr_uint x, ffr_uint y, ffr_uint w, ffr_uint h);
void ffr_blend();

ffr_program_handle ffr_create_program(const char** srcs, const ffr_shader_type* types, int num);
ffr_buffer_handle ffr_create_buffer(size_t size, const void* data);
ffr_texture_handle ffr_create_texture(ffr_uint w, ffr_uint h, const void* data);

void ffr_update_buffer(ffr_buffer_handle buffer, const void* data, size_t offset, size_t size);

void ffr_destroy_program(ffr_program_handle program);
void ffr_destroy_buffer(ffr_buffer_handle buffer);
void ffr_destroy_texture(ffr_texture_handle texture);

void ffr_draw(const ffr_draw_call* draw_call);

void ffr_set_framebuffer(ffr_framebuffer_handle fb);

#ifdef __cplusplus
}
#endif