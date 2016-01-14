#pragma once


#include "core/matrix.h"


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

	public:
		static EditorIcons* create(WorldEditor& editor);
		static void destroy(EditorIcons& icons);

		virtual void clear() = 0;
		virtual void render() = 0;
		virtual Hit raycast(const Vec3& origin, const Vec3& dir) = 0;
		/*float hit(WorldEditor& editor, const Vec3& origin, const Vec3& dir) const;
		Entity getEntity() const { return m_entity; }

		static void loadIcons(WorldEditor& editor);
		static void unloadIcons(WorldEditor& editor);

	private:
		RenderScene* m_scene;
		Entity m_entity;
		int m_model;
		Matrix m_matrix;
		float m_scale;
		bool m_is_visible;
		Type m_type;*/
};


}