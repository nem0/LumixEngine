#pragma once


#include "core/lumix.h"
#include "universe/entity.h"


namespace Lumix
{
	struct Entity;
	class IScene;


	struct LUMIX_ENGINE_API Component final
	{

		typedef uint32_t Type;

		Component() { index = -1; }
		Component(const Entity& _entity, uint32_t _type, IScene* _scene, int _index)
			: entity(_entity)
			, type(_type)
			, scene(_scene)
			, index(_index)
		{
		}

		Entity entity;
		Type type;
		IScene* scene;
		int index;

		static const Component INVALID;

		bool operator ==(const Component& rhs) const { return type == rhs.type && scene == rhs.scene && index == rhs.index; }
		bool operator !=(const Component& rhs) const { return type != rhs.type || scene != rhs.scene || index != rhs.index; }
		bool operator <(const Component& rhs) const { ASSERT(type == rhs.type); ASSERT(scene == rhs.scene); return index < rhs.index; }
		bool isValid() const  { return index >= 0; }
	}; 


} // ~namespace Lumix
