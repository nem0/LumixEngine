#pragma once


#include "engine/delegate.h"
#include "engine/matrix.h"


struct lua_State;


namespace Lumix
{

namespace ffr { struct TextureHandle; }

struct Draw2D;
struct IAllocator;
class Path;
class Renderer;
class RenderScene;
template <typename T> class Delegate;


class LUMIX_RENDERER_API Pipeline
{
public:
	struct Stats
	{
		int draw_call_count;
		int instance_count;
		int triangle_count;
	};

	struct CustomCommandHandler
	{
		Delegate<void> callback;
		char name[30];
		u32 hash;
	};

public:
	static Pipeline* create(Renderer& renderer, const Path& path, const char* define, const char* camera_slot, IAllocator& allocator);
	static void destroy(Pipeline* pipeline);

	virtual ~Pipeline() {}

	virtual void load() = 0;
	virtual bool render() = 0;
	virtual void resize(int width, int height) = 0;
	virtual void setScene(RenderScene* scene) = 0;
	virtual RenderScene* getScene() const = 0;
	virtual int getWidth() const = 0;
	virtual int getHeight() const = 0;
	virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;
	virtual void setWindowHandle(void* data) = 0;
	virtual bool isReady() const = 0;
	virtual const Stats& getStats() const = 0;
	virtual Path& getPath() = 0;
	virtual void callLuaFunction(const char* func) = 0;
	virtual void renderModel(class Model& model, const Matrix& mtx) = 0;

	virtual Draw2D& getDraw2D() = 0;
	virtual ffr::TextureHandle getOutput() = 0;
};

} // namespace Lumix
