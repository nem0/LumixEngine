#pragma once


#include "core/lumix.h"
#include "universe/entity.h"


namespace Lumix
{
	struct Entity;


	struct LUMIX_ENGINE_API Component final
	{

		typedef uint32_t Type;

		Component() { index = -1; }
		Component(const Entity& _entity, uint32_t _type, void* _system, int _index)
			: entity(_entity)
			, type(_type)
			, system(_system)
			, index(_index)
		{
		}

		Entity entity;
		Type type;
		void* system;
		int index;

		static const Component INVALID;

		bool operator ==(const Component& rhs) const { return type == rhs.type && system == rhs.system && index == rhs.index; }
		bool operator !=(const Component& rhs) const { return type != rhs.type || system != rhs.system || index != rhs.index; }
		bool isValid() const  { return index >= 0; }
	}; 


} // ~namespace Lumix
