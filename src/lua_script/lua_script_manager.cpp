#include "lua_script_manager.h"

#include "core/crc32.h"
#include "core/log.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"


namespace Lumix
{


LuaScript::LuaScript(const Path& path,
					 ResourceManager& resource_manager,
					 IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_source_code(allocator)
	, m_properties(allocator)
{
}


LuaScript::~LuaScript()
{

}


void LuaScript::unload()
{
	m_properties.clear();
	m_source_code = "";
}


const char* LuaScript::getPropertyName(uint32 hash) const
{
	for (auto& property : m_properties)
	{
		if (crc32(property.name) == hash)
		{
			return property.name;
		}
	}
	return nullptr;
}


static bool isWhitespace(char c)
{
	return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}


static const char* getToken(const char* src, char* dest, int size)
{
	const char* in = src;
	char* out = dest;
	--size;
	while (*in && isWhitespace(*in))
	{
		++in;
	}

	while (*in && !isWhitespace(*in) && size)
	{
		*out = *in;
		++out;
		++in;
		--size;
	}
	*out = '\0';
	return in;
}


void LuaScript::parseProperties()
{
	static const char* PROPERTY_MARK = "-- LUMIX PROPERTY";
	const int PROPERTY_MARK_LENGTH = stringLength(PROPERTY_MARK);
	const char* str = m_source_code.c_str();
	const char* prop = strstr(str, PROPERTY_MARK);
	while (prop)
	{
		const char* token = prop + PROPERTY_MARK_LENGTH + 1;

		Property& property = m_properties.pushEmpty();
		token = getToken(token, property.name, sizeof(property.name));
		char type[50];
		token = getToken(token, type, sizeof(type));
		if (compareString(type, "entity") == 0)
		{
			property.type = Property::ENTITY;
		}
		else if (compareString(type, "float") == 0)
		{
			property.type = Property::FLOAT;
		}
		else
		{
			property.type = Property::ANY;
		}

		prop = strstr(prop + 1, PROPERTY_MARK);
	}
}


bool LuaScript::load(FS::IFile& file)
{
	m_properties.clear();
	m_source_code.set((const char*)file.getBuffer(), (int)file.size());
	parseProperties();
	m_size = file.size();
	return true;
}


LuaScriptManager::LuaScriptManager(IAllocator& allocator)
	: ResourceManagerBase(allocator)
	, m_allocator(allocator)
{
}


LuaScriptManager::~LuaScriptManager()
{
}


Resource* LuaScriptManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, LuaScript)(path, getOwner(), m_allocator);
}


void LuaScriptManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<LuaScript*>(&resource));
}


} // namespace Lumix