#pragma once


#include "core/string.h"
#include "engine/iplugin.h"


namespace Lumix
{


class LuaScript;


class LuaScriptScene : public IScene
{
public:
	struct Property
	{
		Property(IAllocator& allocator)
			: m_value(allocator)
		{
		}

		string m_value;
		uint32_t m_name_hash;
	};

public:
	virtual int getPropertyCount(ComponentIndex cmp) const = 0;
	virtual const char* getPropertyName(ComponentIndex cmp, int index) const = 0;
	virtual const char* getPropertyValue(ComponentIndex cmp, int index) const = 0;
	virtual LuaScript* getScriptResource(ComponentIndex cmp) const = 0;
	virtual void setPropertyValue(ComponentIndex cmp,
		const char* name,
		const char* value) = 0;
};


} // namespace Lumix