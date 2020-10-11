#pragma once


#include "engine/math.h"


namespace Lumix
{

template <typename T> struct Array;
template <typename T> struct UniquePtr;

struct EditorIcons
{
	struct Hit
	{
		EntityPtr entity;
		float t;
	};

	struct RenderData {
		Matrix mtx;
		struct Model* model;
	};

	static UniquePtr<EditorIcons> create(struct WorldEditor& editor, struct RenderScene& scene);
	static void destroy(EditorIcons& icons);

	virtual ~EditorIcons() {}

	virtual void getRenderData(Array<RenderData>* data) = 0;
	virtual Hit raycast(const DVec3& origin, const Vec3& dir) = 0;
};


}