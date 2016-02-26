#pragma once

#include "lumix.h"
#include "core/delegate.h"


struct lua_State;


namespace bgfx
{
	struct TextureHandle;
	struct UniformHandle;
	struct ProgramHandle;
}


namespace Lumix
{


class FrameBuffer;
class IAllocator;
struct Matrix;
class Model;
class Path;
class Renderer;
class RenderScene;
class TransientGeometry;
struct Vec4;


class CommandBufferGenerator
{
public:
	CommandBufferGenerator();

	void setTexture(uint8 stage,
		const bgfx::UniformHandle& uniform,
		const bgfx::TextureHandle& texture);
	void setUniform(const bgfx::UniformHandle& uniform, const Vec4& value);
	void setUniform(const bgfx::UniformHandle& uniform, const Vec4* values, int count);
	void setUniform(const bgfx::UniformHandle& uniform, const Matrix* values, int count);
	void setTimeUniform(const bgfx::UniformHandle& uniform);
	void setLocalShadowmap(const bgfx::TextureHandle& shadowmap);
	void setGlobalShadowmap();
	int getSize() const { return int(pointer - buffer); }
	void getData(uint8* data);
	void clear();
	void beginAppend();
	void end();

	uint8 buffer[1024];
	uint8* pointer;
};


class LUMIX_RENDERER_API Pipeline
{
	public:
		struct Stats
		{
			int m_draw_call_count;
			int m_instance_count;
			int m_triangle_count;
		};

		struct CustomCommandHandler
		{
			Delegate<void> callback;
			char name[30];
			uint32 hash;
		};

	public:
		static void registerLuaAPI(lua_State* state);

		virtual ~Pipeline() {}

		virtual void load() = 0;
		virtual void render() = 0;
		virtual void setViewport(int x, int y, int width, int height) = 0;

		static Pipeline* create(Renderer& renderer, const Path& path, IAllocator& allocator);
		static void destroy(Pipeline* pipeline);

		virtual FrameBuffer* getFramebuffer(const char* framebuffer_name) = 0;
		virtual void setScene(RenderScene* scene) = 0;
		virtual int getWidth() = 0;
		virtual int getHeight() = 0;
		virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;
		virtual void setViewProjection(const Matrix& mtx, int width, int height) = 0;
		virtual void setScissor(int x, int y, int width, int height) = 0;
		virtual void setTexture(int slot,
			bgfx::TextureHandle texture,
			bgfx::UniformHandle uniform) = 0;
		virtual void render(TransientGeometry& geom,
			const Matrix& mtx,
			int first_index,
			int num_indices,
			uint64 render_states,
			bgfx::ProgramHandle program_handle) = 0;
		virtual void setWireframe(bool wireframe) = 0;
		virtual void renderModel(Model& model, const Matrix& mtx) = 0;
		virtual void toggleStats() = 0;
		virtual void setWindowHandle(void* data) = 0;
		virtual int getPassIdx() const = 0;
		virtual bool isReady() const = 0;
		virtual const Stats& getStats() = 0;
		virtual Path& getPath() = 0;
};


} // namespace Lumix