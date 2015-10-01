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


void LuaScript::doUnload()
{
	m_properties.clear();
	m_source_code = "";
	m_size = 0;
	onEmpty();
}


const char* LuaScript::getPropertyName(uint32_t hash) const
{
	for (const char* name : m_properties)
	{
		if (crc32(name) == hash)
		{
			return name;
		}
	}
	return nullptr;
}


static bool isWhitespace(char c)
{
	return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}


static void getToken(const char* src, char* dest, int size)
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
}


void LuaScript::parseProperties()
{
	static const char* PROPERTY_MARK = "-- LUMIX PROPERTY";
	const int PROPERTY_MARK_LENGTH = (int)strlen(PROPERTY_MARK);
	const char* str = m_source_code.c_str();
	const char* prop = strstr(str, PROPERTY_MARK);
	while (prop)
	{
		const char* prop_name = prop + PROPERTY_MARK_LENGTH + 1;

		PropertyName& token = m_properties.pushEmpty();
		getToken(prop_name, token, sizeof(token));

		prop = strstr(prop + 1, PROPERTY_MARK);
	}
}


void LuaScript::loaded(FS::IFile& file, bool success, FS::FileSystem& fs)
{
	if (success)
	{
		m_properties.clear();
		m_source_code.set((const char*)file.getBuffer(), (int)file.size());
		parseProperties();
		m_size = file.size();
		decrementDepCount();
	}
	else
	{
		g_log_error.log("lua_script") << "Could not load script "
									  << m_path.c_str();
		onFailure();
	}
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
	return m_allocator.newObject<LuaScript>(path, getOwner(), m_allocator);
}


void LuaScriptManager::destroyResource(Resource& resource)
{
	m_allocator.deleteObject(static_cast<LuaScript*>(&resource));
}


} // namespace Lumix