#pragma once


#include "core/string.h"
#include "engine/iplugin.h"


namespace Lumix
{


class LuaScriptScene : public IScene
{
public:
	struct Property
	{
		Property(Lumix::IAllocator& allocator)
			: m_value(allocator)
		{
		}

		Lumix::string m_value;
		uint32_t m_name_hash;
	};

public:
	virtual int getPropertyCount(Lumix::ComponentIndex cmp) const = 0;
	virtual const char* getPropertyName(Lumix::ComponentIndex cmp, int index) const = 0;
	virtual const char* getPropertyValue(Lumix::ComponentIndex cmp, int index) const = 0;
	virtual void setPropertyValue(Lumix::ComponentIndex cmp,
		const char* name,
		const char* value) = 0;
};


} // namespace Lumix