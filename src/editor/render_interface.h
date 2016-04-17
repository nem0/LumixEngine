#pragma once


#include "lumix.h"
#include "core/geometry.h"
#include "core/matrix.h"
#include "core/path.h"
#include "core/vec.h"


namespace Lumix
{


class RenderInterface
{
public:
	typedef int ModelHandle;

	struct Vertex
	{
		Lumix::Vec3 position;
		uint32 color;
		float u, v;
	};


public:
	virtual ~RenderInterface() {}

	virtual AABB getEntityAABB(Universe& universe, Entity entity) = 0;
	virtual float getCameraFOV(ComponentIndex cmp) = 0;
	virtual bool isCameraOrtho(ComponentIndex cmp) = 0;
	virtual float getCameraOrthoSize(ComponentIndex cmp) = 0;
	virtual float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Matrix& mtx) = 0;
	virtual void renderModel(ModelHandle model, const Matrix& mtx) = 0;
	virtual ModelHandle loadModel(Lumix::Path& path) = 0;
	virtual void unloadModel(ModelHandle handle) = 0;
	virtual Vec3 getModelCenter(Entity entity) = 0;
	virtual void render(const Matrix& mtx,
		uint16* indices,
		int indices_count,
		Vertex* vertices,
		int vertices_count,
		bool lines) = 0;
};


}