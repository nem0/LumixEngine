#pragma once

#include "engine/lumix.h"


namespace Lumix {

struct IAllocator;

namespace ffr {


struct BufferHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct ProgramHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct FramebufferHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct TextureHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct QueryHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };
struct UniformHandle { uint value; bool isValid() const { return value != 0xFFffFFff; } };


const BufferHandle INVALID_BUFFER = { 0xffFFffFF };
const ProgramHandle INVALID_PROGRAM = { 0xffFFffFF };
const TextureHandle INVALID_TEXTURE = { 0xffFFffFF };
const FramebufferHandle INVALID_FRAMEBUFFER = { 0xffFFffFF };
const QueryHandle INVALID_QUERY = { 0xffFFffFF };
const UniformHandle INVALID_UNIFORM = { 0xffFFffFF };


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
	CULL_BACK = 1 << 3,
	SCISSOR_TEST = 1 << 4
};


enum class PrimitiveType : uint {
	TRIANGLES,
	TRIANGLE_STRIP,
	LINES,
	POINTS
};


enum class ShaderType : uint {
	VERTEX,
	FRAGMENT,
};


enum class ClearFlags : uint {
	COLOR = 1 << 0,
	DEPTH = 1 << 1,
	STENCIL = 1 << 2
};


enum class StencilFuncs : uint {
	DISABLE = 1 << 0,
	ALWAYS = 1 << 1,
	EQUAL = 1 << 2,
	NOT_EQUAL = 1 << 3,
};

enum class StencilOps : uint {
	KEEP = 1 << 0,
	REPLACE = 1 << 1
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

enum class UniformType : uint {
	INT,
	FLOAT,
	VEC2,
	VEC3,
	VEC4,
	MAT4,
	MAT4X3,
	MAT3X4
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


struct TextureInfo {
	int width;
	int height;
	int depth;
	int layers;
	int mips;
	bool is_cubemap;
};


void preinit(IAllocator& allocator);
bool init(void* window_handle);
void swapBuffers();
bool isHomogenousDepth();
bool isOriginBottomLeft();
void checkThread();
void shutdown();

void clear(uint flags, const float* color, float depth);

void scissor(uint x, uint y, uint w, uint h);
void viewport(uint x, uint y, uint w, uint h);
void blending(int mode);
void setStencil(uint write_mask, StencilFuncs func, int ref, uint mask, StencilOps sfail, StencilOps dpfail, StencilOps dppass);

TextureHandle allocTextureHandle();
BufferHandle allocBufferHandle();
UniformHandle allocUniform(const char* name, UniformType type, int count);

void setState(u64 state);
ProgramHandle createProgram(const char** srcs, const ShaderType* types, int num, const char** prefixes, int prefixes_count, const char* name);
void useProgram(ProgramHandle prg);
void createBuffer(BufferHandle handle, size_t size, const void* data);
bool createTexture(TextureHandle handle, uint w, uint h, TextureFormat format, uint flags, const void* data);
bool loadTexture(TextureHandle handle, const void* data, int size, uint flags);
FramebufferHandle createFramebuffer();
QueryHandle createQuery();

void setVertexBuffer(const VertexDecl* decl, BufferHandle vertex_buffer, uint buffer_offset_bytes, const int* attribute_map);
void setInstanceBuffer(const VertexDecl& decl, BufferHandle instance_buffer, int byte_offset, int location_offset);
void bindTexture(uint unit, TextureHandle handle);
void uniformBlockBinding(ProgramHandle program, const char* block_name, uint binding);
void update(FramebufferHandle fb, uint renderbuffers_count, const TextureHandle* renderbuffers);
void update(BufferHandle buffer, const void* data, size_t offset, size_t size);
void* map(BufferHandle buffer);
void unmap(BufferHandle buffer);
void bindUniformBuffer(uint index, BufferHandle buffer, size_t offset, size_t size);
void getTextureImage(ffr::TextureHandle texture, uint size, void* buf);
TextureInfo getTextureInfo(const void* data);
void queryTimestamp(QueryHandle query);
u64 getQueryResult(QueryHandle query);

void destroy(ProgramHandle program);
void destroy(BufferHandle buffer);
void destroy(TextureHandle texture);
void destroy(FramebufferHandle fb);
void destroy(QueryHandle query);
void destroy(UniformHandle query);

void setIndexBuffer(BufferHandle handle);
void drawTriangles(uint indices_count);
void drawTrianglesInstanced(uint indices_offset_bytes, uint indices_count, uint instances_count);
void drawElements(uint offset, uint count, PrimitiveType type);
void drawArrays(uint offset, uint count, PrimitiveType type);
void drawTriangleStripArraysInstanced(uint offset, uint indices_count, uint instances_count);

void pushDebugGroup(const char* msg);
void popDebugGroup();
int getAttribLocation(ProgramHandle program, const char* uniform_name);
void setUniform1i(UniformHandle uniform, int value);
void setUniform2f(UniformHandle uniform, const float* value);
void setUniform4f(UniformHandle uniform, const float* value);
void setUniformMatrix3x4f(UniformHandle uniform, const float* value);
void setUniformMatrix4f(UniformHandle uniform, const float* value);
void setUniformMatrix4x3f(UniformHandle uniform, const float* value);
int getUniformLocation(ProgramHandle program_handle, UniformHandle uniform);
void applyUniformMatrix3x4f(int location, const float* value);
void applyUniformMatrix4f(int location, const float* value);
void applyUniformMatrix4fv(int location, uint count, const float* value);
void applyUniformMatrix4x3f(int location, const float* value);

void setFramebuffer(FramebufferHandle fb, bool srgb);


} // namespace ffr

} // namespace Lumix