#pragma once


#include "universe/universe.h"


namespace Lumix
{


struct ComponentOld;
class Engine;
class Model;
class PipelineInstance;
class RenderScene;


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
		EditorIcon(Engine& engine, RenderScene& scene, const Entity& entity);
		~EditorIcon();
		void render(PipelineInstance& pipeline);
		void show();
		void hide();
		float hit(const Vec3& origin, const Vec3& dir) const;
		Entity getEntity() const { return m_entity; }

		static bool loadIcons(Engine& engine);
		static void unloadIcons();

	private:
		RenderScene* m_scene;
		Entity m_entity;
		Model* m_model;
		Matrix m_matrix;
		float m_scale;
		bool m_is_visible;
		Type m_type;
};


}