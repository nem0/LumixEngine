#pragma once

#include "engine/lumix.h"


namespace Lumix {

struct IAllocator;

namespace gpu {

using BufferHandle = struct Buffer*;
using ProgramHandle = struct Program*;
using TextureHandle = struct Texture*;
using QueryHandle = struct Query*;
const BufferHandle INVALID_BUFFER = nullptr;
const ProgramHandle INVALID_PROGRAM = nullptr;
const TextureHandle INVALID_TEXTURE = nullptr;
const QueryHandle INVALID_QUERY = nullptr;

enum class InitFlags : u32 {
	NONE = 0,
	DEBUG_OUTPUT = 1 << 0,
	VSYNC = 1 << 1
};

enum class FramebufferFlags : u32 {
	NONE = 0,
	SRGB = 1 << 0,
	READONLY_DEPTH_STENCIL = 1 << 1
};

enum class StateFlags : u64 {
	NONE = 0,
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
	GEOMETRY,
	COMPUTE
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
	SRC1_COLOR,
	ONE_MINUS_SRC1_COLOR,
	SRC1_ALPHA,
	ONE_MINUS_SRC1_ALPHA,
};


enum class AttributeType : u8 {
	U8,
	FLOAT,
	I16,
	I8
};


// keep order, this is serialized
enum class TextureFormat : u32 {
	R8,
	RG8,
	D32,
	D24S8,
	RGBA8,
	RGBA16,
	RGBA16F,
	RGBA32F,
	BGRA8,
	R16F,
	R16,
	R32F,
	RG32F,
	SRGB,
	SRGBA,
	BC1,
	BC2,
	BC3,
	BC4,
	BC5,
	R11G11B10F
};

enum class BindShaderBufferFlags : u32 {
	NONE = 0,
	OUTPUT = 1 << 0,
};

enum class TextureFlags : u32 {
	NONE = 0,
	POINT_FILTER = 1 << 0,
	CLAMP_U = 1 << 1,
	CLAMP_V = 1 << 2,
	CLAMP_W = 1 << 3,
	ANISOTROPIC_FILTER = 1 << 4,
	NO_MIPS = 1 << 5,
	SRGB = 1 << 6,
	READBACK = 1 << 7,
	IS_3D = 1 << 8,
	IS_CUBE = 1 << 9,
	COMPUTE_WRITE = 1 << 10,
	RENDER_TARGET = 1 << 11,
};

enum class BufferFlags : u32 {
	NONE = 0,
	IMMUTABLE = 1 << 0,
	UNIFORM_BUFFER = 1 << 1,
	SHADER_BUFFER = 1 << 2,
	COMPUTE_WRITE = 1 << 3,
	MAPPABLE = 1 << 4,
};

enum class DataType {
	U16,
	U32
};

#pragma pack(1)
struct Attribute {
	enum Flags {
		NORMALIZED = 1 << 0,
		AS_INT = 1 << 1,
		INSTANCED = 1 << 2
	};
	u8 idx;
	u8 components_count;
	u8 byte_offset;
	AttributeType type;
	u8 flags;
};
#pragma pack()

struct VertexDecl {
	enum { MAX_ATTRIBUTES = 16 };

	void addAttribute(u8 idx, u8 byte_offset, u8 components_num, AttributeType type, u8 flags);

	u8 attributes_count = 0;
	u32 hash = 0;
	Attribute attributes[MAX_ATTRIBUTES];
};


struct TextureDesc {
	TextureFormat format;
	u32 width;
	u32 height;
	u32 depth;
	u32 mips;
	bool is_cubemap;
};


struct MemoryStats {
	u64 total_available_mem;
	u64 current_available_mem;
	u64 dedicated_vidmem;
};


void preinit(IAllocator& allocator, bool load_renderdoc);
bool init(void* window_handle, InitFlags flags);
void launchRenderDoc();
void setCurrentWindow(void* window_handle);
bool getMemoryStats(MemoryStats& stats);
u32 swapBuffers();
void waitFrame(u32 frame);
bool frameFinished(u32 frame);
LUMIX_RENDERER_API bool isOriginBottomLeft();
void checkThread();
void shutdown();
void startCapture();
void stopCapture();
int getSize(AttributeType type);
u32 getSize(TextureFormat format, u32 w, u32 h);


void clear(ClearFlags flags, const float* color, float depth);

void scissor(u32 x, u32 y, u32 w, u32 h);
void viewport(u32 x, u32 y, u32 w, u32 h);

inline StateFlags getBlendStateBits(BlendFactors src_rgb, BlendFactors dst_rgb, BlendFactors src_a, BlendFactors dst_a)
{
	return StateFlags((((u64)src_rgb & 15) << 6) | (((u64)dst_rgb & 15) << 10) | (((u64)src_a & 15) << 14) | (((u64)dst_a & 15) << 18));
}

inline StateFlags getStencilStateBits(u8 write_mask, StencilFuncs func, u8 ref, u8 mask, StencilOps sfail, StencilOps dpfail, StencilOps dppass)
{
	return StateFlags(((u64)write_mask << 22) | ((u64)func << 30) | ((u64)ref << 34) | ((u64)mask << 42) | ((u64)sfail << 50) | ((u64)dpfail << 54) | ((u64)dppass << 58));
}

TextureHandle allocTextureHandle();
BufferHandle allocBufferHandle();
ProgramHandle allocProgramHandle();

void setState(StateFlags state);
bool createProgram(ProgramHandle program, const VertexDecl& decl, const char** srcs, const ShaderType* types, u32 num, const char** prefixes, u32 prefixes_count, const char* name);
void useProgram(ProgramHandle prg);
void dispatch(u32 num_groups_x, u32 num_groups_y, u32 num_groups_z);

void createBuffer(BufferHandle handle, BufferFlags flags, size_t size, const void* data);
bool createTexture(TextureHandle handle, u32 w, u32 h, u32 depth, TextureFormat format, TextureFlags flags, const char* debug_name);
void createTextureView(TextureHandle view, TextureHandle texture);
void generateMipmaps(TextureHandle handle);
void update(TextureHandle texture, u32 mip, u32 x, u32 y, u32 z, u32 w, u32 h, TextureFormat format, const void* buf, u32 size);
QueryHandle createQuery();

void bindVertexBuffer(u32 binding_idx, BufferHandle buffer, u32 buffer_offset, u32 stride);
void bindImageTexture(TextureHandle texture, u32 unit);
void bindTextures(const TextureHandle* handles, u32 offset, u32 count);
void bindShaderBuffer(BufferHandle buffer, u32 binding_point, BindShaderBufferFlags flags);
void update(BufferHandle buffer, const void* data, size_t size);
void* map(BufferHandle buffer, size_t size);
void unmap(BufferHandle buffer);
void bindUniformBuffer(u32 ub_index, BufferHandle buffer, size_t offset, size_t size);
void copy(TextureHandle dst, TextureHandle src, u32 dst_x, u32 dst_y);
void copy(BufferHandle dst, BufferHandle src, u32 dst_offset, u32 size);
void readTexture(TextureHandle texture, u32 mip, Span<u8> buf);
void queryTimestamp(QueryHandle query);
u64 getQueryResult(QueryHandle query);
u64 getQueryFrequency();
bool isQueryReady(QueryHandle query);

void destroy(ProgramHandle program);
void destroy(BufferHandle buffer);
void destroy(TextureHandle texture);
void destroy(QueryHandle query);

void bindIndexBuffer(BufferHandle handle);
void bindIndirectBuffer(BufferHandle handle);
void drawIndirect(DataType index_type);
void drawTriangles(u32 byte_offset, u32 indices_count, DataType index_type);
void drawTrianglesInstanced(u32 indices_count, u32 instances_count, DataType index_type);
void drawElements(PrimitiveType primitive_type, u32 byte_offset, u32 count, DataType index_type);
void drawArrays(PrimitiveType type, u32 offset, u32 count);
void drawArraysInstanced(PrimitiveType type, u32 indices_count, u32 instances_count);

void pushDebugGroup(const char* msg);
void popDebugGroup();

void setFramebufferCube(TextureHandle cube, u32 face, u32 mip);
void setFramebuffer(TextureHandle* attachments, u32 num, TextureHandle depth_stencil, FramebufferFlags flags);

inline u32 getBytesPerPixel(gpu::TextureFormat format) {
	switch (format) {
		case gpu::TextureFormat::R8:
			return 1;

		case gpu::TextureFormat::R16F:
		case gpu::TextureFormat::R16:
			return 2;
		case gpu::TextureFormat::SRGB:
			return 3;
		case gpu::TextureFormat::R11G11B10F:
		case gpu::TextureFormat::R32F:
		case gpu::TextureFormat::SRGBA:
		case gpu::TextureFormat::RGBA8:
			return 4;
		case gpu::TextureFormat::RGBA16:
		case gpu::TextureFormat::RGBA16F:
			return 8;
		case gpu::TextureFormat::RGBA32F:
			return 16;
		default:
			ASSERT(false);
			return 0;
	}
}

constexpr StateFlags operator ~(StateFlags a) { return StateFlags(~(u64)a); }
constexpr StateFlags operator | (StateFlags a, StateFlags b) { return StateFlags((u64)a | (u64)b); }
constexpr StateFlags operator & (StateFlags a, StateFlags b) { return StateFlags((u64)a & (u64)b); }

constexpr ClearFlags operator | (ClearFlags a, ClearFlags b) { return ClearFlags((u32)a | (u32)b); }
constexpr ClearFlags operator & (ClearFlags a, ClearFlags b) { return ClearFlags((u32)a & (u32)b); }

constexpr FramebufferFlags operator | (FramebufferFlags a, FramebufferFlags b) { return FramebufferFlags((u32)a | (u32)b); }
constexpr FramebufferFlags operator & (FramebufferFlags a, FramebufferFlags b) { return FramebufferFlags((u32)a & (u32)b); }

constexpr TextureFlags operator ~(TextureFlags a) { return TextureFlags(~(u32)a); }
constexpr TextureFlags operator | (TextureFlags a, TextureFlags b) { return TextureFlags((u32)a | (u32)b); }
constexpr TextureFlags operator & (TextureFlags a, TextureFlags b) { return TextureFlags((u32)a & (u32)b); }

constexpr BufferFlags operator | (BufferFlags a, BufferFlags b) { return BufferFlags((u32)a | (u32)b); }
constexpr BufferFlags operator & (BufferFlags a, BufferFlags b) { return BufferFlags((u32)a & (u32)b); }

constexpr BindShaderBufferFlags operator & (BindShaderBufferFlags a, BindShaderBufferFlags b) { return BindShaderBufferFlags((u32)a & (u32)b); }

constexpr InitFlags operator ~(InitFlags a) { return InitFlags(~(u32)a); }
constexpr InitFlags operator | (InitFlags a, InitFlags b) { return InitFlags((u32)a | (u32)b); }
constexpr InitFlags operator & (InitFlags a, InitFlags b) { return InitFlags((u32)a & (u32)b); }

} // namespace gpu

} // namespace Lumix