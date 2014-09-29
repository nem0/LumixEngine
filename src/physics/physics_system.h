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
		virtual IScene* createScene(Universe& universe) override;
		virtual void destroy() override;
		virtual const char* getName() const override { return "physics"; }

	private:
		struct PhysicsSystemImpl* m_impl;
};


extern "C"
{
	LUMIX_PHYSICS_API IPlugin* createPlugin();
}


} // !namespace Lumix
