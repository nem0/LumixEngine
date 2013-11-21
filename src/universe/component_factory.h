#pragma once


#include "core/lux.h"
#include "core/map.h"


namespace Lux
{


class Renderer;


class LUX_API ComponentFactory
{
	public:
		typedef EntityComponent* (*Creator)(Entity&, const ComponentFactory&);

	public:
		void registerCreator(EntityComponent::Type type, Creator creator);
		EntityComponent* create(EntityComponent::Type type, Entity& entity) const; 
		void registerCreators();

		Renderer* getRenderer() const { return m_renderer; }
		void setRenderer(Renderer* renderer) { m_renderer = renderer; }


	private:
		Renderer* m_renderer;
		map<EntityComponent::Type, Creator> m_creators;
};


} // !namespace Lux