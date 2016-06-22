#pragma once


#include "engine/lumix.h"


namespace Lumix
{


class IScene;


struct LUMIX_ENGINE_API ComponentUID final
{

	ComponentUID()
	{
		handle = INVALID_COMPONENT;
		scene = nullptr;
		entity = INVALID_ENTITY;
		type = {-1};
	}

	ComponentUID(Entity _entity, ComponentType _type, IScene* _scene, ComponentHandle _handle)
		: entity(_entity)
		, type(_type)
		, scene(_scene)
		, handle(_handle)
	{
	}

	Entity entity; 
	ComponentType type;
	IScene* scene;
	ComponentHandle handle;

	static const ComponentUID INVALID;

	bool operator==(const ComponentUID& rhs) const
	{
		return type == rhs.type && scene == rhs.scene && handle == rhs.handle;
	}
	bool isValid() const { return Lumix::isValid(handle); }
};


} // ~namespace Lumix
