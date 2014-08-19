#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"


namespace Lumix
{


class LUMIX_PHYSICS_API PhysicsSystem : public IPlugin
{
	friend class PhysicsScene;
	friend struct PhysicsSceneImpl;
	public:
		PhysicsSystem() { m_impl = 0; }
		
		virtual bool create(Engine& engine) override;
		virtual void destroy() override;
		virtual void onCreateUniverse(Universe& universe) override;
		virtual void onDestroyUniverse(Universe& universe) override;
		virtual void serialize(ISerializer& serializer) override;
		virtual void deserialize(ISerializer& serializer) override;
		virtual void update(float dt) override;
		virtual Component createComponent(uint32_t component_type, const Entity& entity) override;
		virtual void destroyComponent(const Component& component) override;
		virtual const char* getName() const override { return "physics"; }
		virtual void sendMessage(const char* message) override;
		class PhysicsScene* getScene() const;

	private:
		struct PhysicsSystemImpl* m_impl;
};


extern "C"
{
	LUMIX_PHYSICS_API IPlugin* createPlugin();
}


} // !namespace Lumix
