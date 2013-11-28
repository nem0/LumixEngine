#pragma once


#include "universe\universe.h"
#include "core/lux.h"
#include "editor/iplugin.h"


namespace Lux
{
	class LUX_NAVIGATION_API Navigation : public IPlugin
	{
		public:
			Navigation();
			virtual ~Navigation();

			virtual bool create(EditorPropertyMap&, ComponentCreatorList&) LUX_OVERRIDE { return true; }
			virtual Component createComponent(uint32_t, const Entity&) LUX_OVERRIDE { return Component::INVALID; }
			virtual void update(float dt) LUX_OVERRIDE;

			void navigate(Entity e, const Vec3& dest, float speed);
			bool load(const char path[]);
			void draw();

		private:
			struct NavigationImpl* m_impl;
	};


	extern "C"
	{
		LUX_NAVIGATION_API IPlugin* createPlugin();
	}


}