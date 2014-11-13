#pragma once


#include "universe/universe.h"


namespace Lumix
{


struct Component;
class Engine;
struct Entity;
class IRenderDevice;
class Model;
class Renderer;
class RenderScene;


class EditorIcon
{
	friend class EditorApp;
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
		void create(Engine& engine, RenderScene& scene, const Entity& entity);
		void destroy();
		void render(Renderer* renderer, IRenderDevice& render_device);
		void show();
		void hide();
		float hit(const Vec3& origin, const Vec3& dir) const;
		Entity getEntity() const { return m_entity; }

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