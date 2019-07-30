#pragma once

#include "engine/lumix.h"


namespace Lumix {

struct IAllocator;

namespace ffr {


struct FenceHandle { void* value; bool isValid() const { return value; } };
struct BufferHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };
struct ProgramHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };
struct FramebufferHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };
struct TextureHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };
struct QueryHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };
struct UniformHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };
struct VAOHandle { u32 value; bool isValid() const { return value != 0xFFffFFff; } };

const BufferHandle INVALID_BUFFER = { 0xffFFffFF };
const ProgramHandle INVALID_PROGRAM = { 0xffFFffFF };
const TextureHandle INVALID_TEXTURE = { 0xffFFffFF };
const FramebufferHandle INVALID_FRAMEBUFFER = { 0xffFFffFF };
const QueryHandle INVALID_QUERY = { 0xffFFffFF };
const UniformHandle INVALID_UNIFORM = { 0xffFFffFF };
const FenceHandle INVALID_FENCE = { 0 };
const VAOHandle INVALID_VAO = { 0xffFFffFF };


enum class LogLevel : u32 {
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
	SCISSOR_TEST = 1 << 4,
	DEPTH_WRITE = 1 << 5,
	/* 16 bits reserved for blending*/
	/* 40 bits reserver for stencil*/ 
};


enum class PrimitiveType : u32 {
	TRIANGLES,
	TRIANGLE_STRIP,
	LINES,
	POINTS
};


enum class ShaderType : u32 {
	VERTEX,
	FRAGMENT,
	GEOMETRY
};


enum class ClearFlags : u32 {
	COLOR = 1 << 0,
	DEPTH = 1 << 1,
	STENCIL = 1 << 2
};


enum class StencilFuncs : u8 {
	DISABLE,
	ALWAYS,
	EQUAL,
	NOT_EQUAL,
};

enum class StencilOps : u8 {
	KEEP,
	ZERO,
	REPLACE,
	INCR,
	INCR_WRAP,
	DECR,
	DECR_WRAP,
	INVERT
};


enum class BlendFactors : u8 {
	ZERO,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
};


enum class AttributeType : u8 {
	U8,
	FLOAT,
	I16
};


enum class TextureFormat : u32 {
	R8,
	D32,
	D24,
	D24S8,
	RGBA8,
	RGBA16,
	RGBA16F,
	R16F,
	R16,
	R32F,
	SRGB,
	SRGBA
};

enum class UniformType : u32 {
	INT,
	FLOAT,
	VEC2,
	VEC3,
	VEC4,
	IVEC2,
	IVEC4,
	MAT4,
	MAT4X3,
	MAT3X4
};


enum class TextureFlags : u32 {
	SRGB = 1 << 0,
	CLAMP = 1 << 1,
	NO_MIPS = 1 << 2
};

enum class BufferFlags : u32 {
	PERSISTENT = 1 << 0,
	COHERENT = 1 << 1,
	MAP_READ = 1 << 2,
	MAP_WRITE = 1 << 3,
	MAP_FLUSH_EXPLICIT = 1 << 4,
	DYNAMIC_STORAGE = 1 << 5
};

enum class DataType {
	U16,
	U32
};

struct VertexAttrib {
	u8 idx;
	int size;
	AttributeType type;
	u32 offset;
	bool normalized;
	bool as_int;
	bool instanced;
};

struct Attribute {
	enum Flags {
		NORMALIZED = 1 << 0,
		AS_INT = 1 << 1
	};
	u8 components_num;
	u8 offset;
	AttributeType type;
	u8 flags;
};


struct VertexDecl {
	enum { MAX_ATTRIBUTES = 16 };

	void addAttribute(u8 components_num, AttributeType type, bool normalized, bool as_int);

	u16 size = 0;
	u16 attributes_count = 0;
	u32 hash = 0;
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
bool init(void* window_handle, bool debug);
void swapBuffers();
bool isHomogenousDepth();
bool isOriginBottomLeft();
void checkThread();
void shutdown();
void startCapture();
void stopCapture();

void clear(u32 flags, const float* color, float depth);

void scissor(u32 x, u32 y, u32 w, u32 h);
void viewport(u32 x, u32 y, u32 w, u32 h);

inline u64 getBlendStateBits(BlendFactors src_rgb, BlendFactors dst_rgb, BlendFactors src_a, BlendFactors dst_a)
{
	return (((u64)src_rgb & 15) << 6) | (((u64)dst_rgb & 15) << 10) | (((u64)src_a & 15) << 14) | (((u64)dst_a & 15) << 18);
}

inline u64 getStencilStateBits(u8 write_mask, StencilFuncs func, u8 ref, u8 mask, StencilOps sfail, StencilOps dpfail, StencilOps dppass)
{
	return ((u64)write_mask << 22) | ((u64)func << 30) | ((u64)ref << 34) | ((u64)mask << 42) | ((u64)sfail << 50) | ((u64)dpfail << 54) | ((u64)dppass << 58);
}

TextureHandle allocTextureHandle();
VAOHandle allocVAOHandle();
BufferHandle allocBufferHandle();
ProgramHandle allocProgramHandle();
UniformHandle allocUniform(const char* name, UniformType type, int count);

FenceHandle createFence();
void waitClient(FenceHandle fence);

void setState(u64 state);
bool createProgram(ProgramHandle program, const char** srcs, const ShaderType* types, int num, const char** prefixes, int prefixes_count, const char* name);
void useProgram(ProgramHandle prg);

void createVAO(VAOHandle handle, const VertexAttrib* attribs, u32 count);
void createBuffer(BufferHandle handle, u32 flags, size_t size, const void* data);
bool createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, u32 flags, const void* data, const char* debug_name);
void createTextureView(TextureHandle view, TextureHandle texture);
bool loadTexture(TextureHandle handle, const void* data, int size, u32 flags, const char* debug_name);
void update(TextureHandle texture, u32 level, u32 x, u32 y, u32 w, u32 h, TextureFormat format, void* buf);
FramebufferHandle createFramebuffer();
QueryHandle createQuery();

void bindVAO(VAOHandle handle);
void bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride_offset);
void bindTextures(const TextureHandle* handles, int offset, int count);
void uniformBlockBinding(ProgramHandle program, const char* block_name, u32 binding);
void update(FramebufferHandle fb, u32 renderbuffers_count, const TextureHandle* renderbuffers);
void bindLayer(FramebufferHandle fb, TextureHandle rb, u32 layer);
void update(BufferHandle buffer, const void* data, size_t offset, size_t size);
void* map(BufferHandle buffer, size_t offset, size_t size, u32 flags);
void unmap(BufferHandle buffer);
void flushBuffer(BufferHandle buffer, size_t offset, size_t len);
void bindUniformBuffer(u32 index, BufferHandle buffer, size_t offset, size_t size);
void getTextureImage(ffr::TextureHandle texture, u32 size, void* buf);
TextureInfo getTextureInfo(const void* data);
void queryTimestamp(QueryHandle query);
u64 getQueryResult(QueryHandle query);
bool isQueryReady(QueryHandle query);
void generateMipmaps(TextureHandle texture);

void destroy(FenceHandle fence);
void destroy(ProgramHandle program);
void destroy(BufferHandle buffer);
void destroy(TextureHandle texture);
void destroy(FramebufferHandle fb);
void destroy(QueryHandle query);
void destroy(UniformHandle query);
void destroy(VAOHandle query);

void bindIndexBuffer(BufferHandle handle);
void drawTriangles(u32 indices_count, DataType index_type);
void drawTrianglesInstanced(u32 indices_count, u32 instances_count, DataType index_type);
void drawElements(u32 offset, u32 count, PrimitiveType primitive_type, DataType index_type);
void drawArrays(u32 offset, u32 count, PrimitiveType type);
void drawTriangleStripArraysInstanced(u32 offset, u32 indices_count, u32 instances_count);

void pushDebugGroup(const char* msg);
void popDebugGroup();
int getAttribLocation(ProgramHandle program, const char* uniform_name);
void setUniform1i(UniformHandle uniform, int value);
void setUniform4i(UniformHandle uniform, const int* value);
void setUniform2f(UniformHandle uniform, const float* value);
void setUniform3f(UniformHandle uniform, const float* value);
void setUniform4f(UniformHandle uniform, const float* value);
void setUniformMatrix3x4f(UniformHandle uniform, const float* value);
void setUniformMatrix4f(UniformHandle uniform, const float* value);
void setUniformMatrix4x3f(UniformHandle uniform, const float* value);
int getUniformLocation(ProgramHandle program_handle, UniformHandle uniform);
void applyUniform1i(int location, int value);
void applyUniform4i(int location, const int* value);
void applyUniform4f(int location, const float* value);
void applyUniform3f(int location, const float* value);
void applyUniformMatrix3x4f(int location, const float* value);
void applyUniformMatrix4f(int location, const float* value);
void applyUniformMatrix4fv(int location, u32 count, const float* value);
void applyUniformMatrix4x3f(int location, const float* value);

void setFramebuffer(FramebufferHandle fb, bool srgb);


} // namespace ffr

} // namespace Lumix