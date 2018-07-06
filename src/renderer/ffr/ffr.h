#pragma once

#include "engine/lumix.h"


namespace Lumix {

struct IAllocator;

namespace ffr {


struct BufferHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct ProgramHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct FramebufferHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct TextureHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };


const BufferHandle INVALID_BUFFER = { 0xffFFffFF };
const ProgramHandle INVALID_PROGRAM = { 0xffFFffFF };
const TextureHandle INVALID_TEXTURE = { 0xffFFffFF };
const FramebufferHandle INVALID_FRAMEBUFFER = { 0xffFFffFF };


enum class LogLevel : uint {
	INFO,
	WARNING,
	ERROR,
	FATAL
};


enum class StateFlags : u64 {
	WIREFRAME = 1 << 0,
	DEPTH_TEST = 1 << 1,
	CULL_FRONT = 1 << 2,
	CULL_BACK = 1 << 3
};


enum class PrimitiveType : uint {
	TRIANGLES,
	TRIANGLE_STRIP,
	LINES
};


enum class ShaderType : uint {
	VERTEX,
	FRAGMENT,
};


enum class ClearFlags : uint {
	COLOR = 1 << 0,
	DEPTH = 1 << 1
};


enum class AttributeType : uint {
	U8,
	FLOAT,
	I16
};


enum class TextureFormat : uint {
	D32,
	D24,
	D24S8,
	RGBA8,
	RGBA16F,
	R16F,
	R16,
	R32F,
	SRGB,
	SRGBA
};


enum class TextureFlags : uint {
	SRGB = 1 << 0
};


struct Attribute {
	uint components_num;
	uint offset;
	bool normalized;
	bool as_int;
	AttributeType type;
};


struct VertexDecl {
	enum { MAX_ATTRIBUTES = 16 };

	void addAttribute(uint components_num, AttributeType type, bool normalized, bool as_int);

	uint size = 0;
	uint attributes_count = 0;
	Attribute attributes[MAX_ATTRIBUTES];
};


struct DrawCall {
	ProgramHandle shader;
	PrimitiveType primitive_type;
	uint tex_buffers_count;
	const BufferHandle* tex_buffers;
	uint textures_count;
	const TextureHandle* textures;
	uint indices_offset;
	uint indices_count;
	BufferHandle index_buffer;
	BufferHandle vertex_buffer;
	uint vertex_buffer_offset;
	const VertexDecl* vertex_decl;
	const int* attribute_map;
	u64 state;
};

struct TextureInfo {
	int width;
	int height;
	int depth;
	int layers;
	int mips;
	bool is_cubemap;
};


void preinit();
bool init(IAllocator& allocator);
bool isHomogenousDepth();
bool isOriginBottomLeft();
void shutdown();

void clear(uint flags, const float* color, float depth);

void scissor(uint x, uint y, uint w, uint h);
void viewport(uint x, uint y, uint w, uint h);
void blending(int mode);

ProgramHandle createProgram(const char** srcs, const ShaderType* types, int num, const char** prefixes, int prefixes_count, const char* name);
BufferHandle createBuffer(size_t size, const void* data);
TextureHandle createTexture(uint w, uint h, TextureFormat format, uint flags, const void* data);
TextureHandle loadTexture(const void* data, int size, uint flags, TextureInfo* info);
FramebufferHandle createFramebuffer(uint renderbuffers_count, const TextureHandle* renderbuffers);

void setState(u32 flags);
void uniformBlockBinding(ProgramHandle program, const char* block_name, uint binding);
void update(FramebufferHandle fb, uint renderbuffers_count, const TextureHandle* renderbuffers);
void update(BufferHandle buffer, const void* data, size_t offset, size_t size);
void bindUniformBuffer(uint index, BufferHandle buffer, size_t offset, size_t size);
void getTextureImage(ffr::TextureHandle texture, uint size, void* buf);

void destroy(ProgramHandle program);
void destroy(BufferHandle buffer);
void destroy(TextureHandle texture);
void destroy(FramebufferHandle fb);

void draw(const DrawCall& draw_call);

void pushDebugGroup(const char* msg);
void popDebugGroup();
int getAttribLocation(ProgramHandle program, const char* uniform_name);
void setUniform1i(ProgramHandle program, const char* uniform_name, int value);
void setUniform2f(ProgramHandle program, const char* uniform_name, uint count, const float* value);
void setUniform4f(ProgramHandle program, const char* uniform_name, uint count, const float* value);
void setUniformMatrix4f(ProgramHandle program, const char* uniform_name, uint count, const float* value);

void setFramebuffer(FramebufferHandle fb, bool srgb);


} // namespace ffr

} // namespace Lumix