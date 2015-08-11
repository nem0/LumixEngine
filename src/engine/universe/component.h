#pragma once


#include "lumix.h"


namespace Lumix
{


class IScene;


struct LUMIX_ENGINE_API ComponentUID final
{

	typedef uint32_t Type;

	ComponentUID() { index = -1; scene = nullptr; entity = -1; type = 0; }
	ComponentUID(Entity _entity,
				 uint32_t _type,
				 IScene* _scene,
				 int _index)
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

	static const ComponentUID INVALID;

	bool operator==(const ComponentUID& rhs) const
	{
		return type == rhs.type && scene == rhs.scene && index == rhs.index;
	}
	bool operator!=(const ComponentUID& rhs) const
	{
		return type != rhs.type || scene != rhs.scene || index != rhs.index;
	}
	bool operator<(const ComponentUID& rhs) const
	{
		ASSERT(type == rhs.type);
		ASSERT(scene == rhs.scene);
		return index < rhs.index;
	}
	bool isValid() const { return index >= 0; }
};


} // ~namespace Lumix
