#pragma once


#include "universe/universe.h"


namespace Lux
{


struct Component;
struct Entity;
class IRenderDevice;
class Model;
class Renderer;


class EditorIcon
{
	friend class EditorApp;
	public:
		void create(Renderer& renderer, Entity& entity, const Component& cmp);
		void destroy();
		void render(Renderer* renderer, IRenderDevice& render_device);
		void show();
		void hide();
		float hit(Renderer& renderer, Component camera, const Vec3& origin, const Vec3& dir) const;
		Entity getEntity() const { return m_entity; }

		static void createResources(const char* base_path);

	private:
		Entity m_entity;
		Model* m_model;
		Matrix m_matrix;
		float m_scale;
};


}