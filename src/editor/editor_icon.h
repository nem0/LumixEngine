#pragma once


#include "engine/matrix.h"
#include "render_interface.h"


namespace Lumix
{


class Engine;
class Model;
class Pipeline;
class RenderScene;
class WorldEditor;


class EditorIcons
{
	public:
		struct Hit
		{
			Entity entity;
			float t;
		};

		struct RenderData
		{
			Matrix mtx;
			RenderInterface::ModelHandle model;
		};

	public:
		static EditorIcons* create(WorldEditor& editor);
		static void destroy(EditorIcons& icons);

		virtual ~EditorIcons() {}

		virtual void setRenderInterface(class RenderInterface* render_interface) = 0;
		virtual void clear() = 0;
		virtual void getRenderData(Array<RenderData>* data) = 0;
		virtual void refresh() = 0;
		virtual Hit raycast(const Vec3& origin, const Vec3& dir) = 0;
};


}