#pragma once


#include "engine/math.h"
#include "render_interface.h"


namespace Lumix
{


struct Engine;
struct Model;
struct Pipeline;
struct RenderScene;
struct WorldEditor;


struct EditorIcons
{
	struct Hit
	{
		EntityPtr entity;
		float t;
	};

	struct RenderData {
		Matrix mtx;
		RenderInterface::ModelHandle model;
	};

	static EditorIcons* create(WorldEditor& editor);
	static void destroy(EditorIcons& icons);

	virtual ~EditorIcons() {}

	virtual void setRenderInterface(struct RenderInterface* render_interface) = 0;
	virtual void clear() = 0;
	virtual void getRenderData(Array<RenderData>* data) = 0;
	virtual void refresh() = 0;
	virtual Hit raycast(const DVec3& origin, const Vec3& dir) = 0;
};


}