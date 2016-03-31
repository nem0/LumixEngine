#pragma once

#include "lumix.h"
#include "core/delegate.h"


struct lua_State;


namespace bgfx
{
	struct TextureHandle;
	struct UniformHandle;
	struct ProgramHandle;
	struct TransientVertexBuffer;
	struct TransientIndexBuffer;
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
struct Vec4;


class CommandBufferGenerator
{
public:
	CommandBufferGenerator();

	void setTexture(uint8 stage,
		const bgfx::UniformHandle& uniform,
		const bgfx::TextureHandle& texture);
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
		virtual ~Pipeline() {}

		virtual void load() = 0;
		virtual void render() = 0;
		virtual void setViewport(int x, int y, int width, int height) = 0;

		static Pipeline* create(Renderer& renderer, IAllocator& allocator);
		static void destroy(Pipeline* pipeline);

		virtual void setScene(RenderScene* scene) = 0;
		virtual int getWidth() = 0;
		virtual int getHeight() = 0;
		virtual void setViewProjection(const Matrix& mtx, int width, int height) = 0;

		virtual void renderModel(Model& model, const Matrix& mtx) = 0;
		virtual void toggleStats() = 0;

		virtual bool isReady() const = 0;
		virtual const Stats& getStats() = 0;
		virtual float getCPUTime() const = 0;
		virtual float getGPUTime() const = 0;
};


} // namespace Lumix