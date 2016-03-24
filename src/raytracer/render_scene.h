#pragma once

#include "lumix.h"
#include "iplugin.h"
#include "core/vec.h"
#include "core/array.h"
#include "core/matrix.h"


namespace Lumix
{

class Renderer;
class IAllocator;
struct RayCastModelHit;
class Frustum;
class Pose;
class Model;
template <typename T> class DelegateList;


struct Renderable
{
	Pose* pose;//pointer because of permanent valid position in memory
	Model* model;
	//Matrix matrix;
	Entity entity;
};


struct DebugTriangle
{
	Vec3 p0;
	Vec3 p1;
	Vec3 p2;
	uint32 color;
	float life;
};


class LUMIX_RENDERER_API RenderScene : public IScene
{
public:
	static RenderScene* createInstance(Renderer& renderer,
									   Engine& engine,
									   Universe& universe,
									   IAllocator& allocator);
	static void destroyInstance(RenderScene* scene);

	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Renderable* getRenderable(ComponentIndex cmp) = 0;
	virtual Renderable* getRenderables() = 0;
	
	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;

	virtual DelegateList<void(ComponentIndex)>& renderableCreated() = 0;
	virtual DelegateList<void(ComponentIndex)>& renderableDestroyed() = 0;
};


} // !namespace Lumix
