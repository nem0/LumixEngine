#pragma once


#include "core/lux.h"
#include "core/vector.h"
#include "core/map.h"
#include "universe/universe.h"


namespace Lux
{
	class Universe;
	class ISerializer;
	class PropertyDescriptor;
	class IPlugin;
	typedef map<uint32_t, vector<PropertyDescriptor> > EditorPropertyMap;
	typedef map<uint32_t, IPlugin*> ComponentCreatorList;

	class LUX_ENGINE_API IPlugin LUX_ABSTRACT
	{
		public:
			virtual ~IPlugin();

			virtual bool create(EditorPropertyMap&, ComponentCreatorList&) = 0;
			virtual void onCreateUniverse(Universe&) {}
			virtual void onDestroyUniverse(Universe&) {}
			virtual void serialize(ISerializer&) {}
			virtual void deserialize(ISerializer&) {}
			virtual void update(float) {}
			virtual Component createComponent(uint32_t, const Entity&) = 0;
	};


};