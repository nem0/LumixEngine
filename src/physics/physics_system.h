#pragma once


#include "core/lux.h"
#include "editor/iplugin.h"


namespace Lux
{


class LUX_PHYSICS_API PhysicsSystem : public IPlugin
{
	friend class PhysicsScene;
	friend struct PhysicsSceneImpl;
	public:
		PhysicsSystem() { m_impl = 0; }
		
		virtual bool create(EditorPropertyMap& properties, ComponentCreatorList& creators) LUX_OVERRIDE;
		virtual void onCreateUniverse(Universe& universe) LUX_OVERRIDE;
		virtual void onDestroyUniverse(Universe& universe) LUX_OVERRIDE;
		virtual void serialize(ISerializer& serializer) LUX_OVERRIDE;
		virtual void deserialize(ISerializer& serializer) LUX_OVERRIDE;
		virtual void update(float dt) LUX_OVERRIDE;
		virtual Component createComponent(uint32_t component_type, const Entity& entity) LUX_OVERRIDE;

		virtual void destroy();


	private:
		struct PhysicsSystemImpl* m_impl;
};


extern "C"
{
	LUX_PHYSICS_API IPlugin* createPlugin();
}


} // !namespace Lux
