#pragma once


#include "core/matrix.h"
#include "universe/universe.h"


namespace Lumix
{


class Engine;
class Model;
class Pipeline;
class RenderScene;
class WorldEditor;


class EditorIcon
{
	public:
		enum Type
		{
			PHYSICAL_CONTROLLER,
			PHYSICAL_BOX,
			CAMERA,
			LIGHT,
			TERRAIN,
			ENTITY,
			COUNT
		};

	public:
		EditorIcon(WorldEditor& editor, RenderScene& scene, Entity entity);
		~EditorIcon();
		void render(WorldEditor& editor);
		void show();
		void hide();
		float hit(WorldEditor& editor, const Vec3& origin, const Vec3& dir) const;
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
		Type m_type;
};


}