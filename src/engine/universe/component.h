#pragma once


#include "engine/lumix.h"


namespace Lumix
{


class IScene;


struct LUMIX_ENGINE_API ComponentUID final
{

	ComponentUID()
	{
		index = -1;
		scene = nullptr;
		entity = INVALID_ENTITY;
		type = {-1};
	}

	ComponentUID(Entity _entity, ComponentType _type, IScene* _scene, int _index)
		: entity(_entity)
		, type(_type)
		, scene(_scene)
		, index(_index)
	{
	}

	Entity entity; 
	ComponentType type;
	IScene* scene;
	ComponentIndex index;

	static const ComponentUID INVALID;

	bool operator==(const ComponentUID& rhs) const
	{
		return type == rhs.type && scene == rhs.scene && index == rhs.index;
	}
	bool isValid() const { return index >= 0; }
};


} // ~namespace Lumix
