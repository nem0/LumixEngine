#include "lua_script_manager.h"

#include "core/crc32.h"
#include "core/log.h"
#include "core/FS/file_system.h"


namespace Lumix
{


LuaScript::LuaScript(const Path& path,
					 ResourceManager& resource_manager,
					 IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_source_code(allocator)
{
}


LuaScript::~LuaScript()
{

}


void LuaScript::unload()
{
	m_source_code = "";
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


bool LuaScript::load(FS::IFile& file)
{
	m_source_code.set((const char*)file.getBuffer(), (int)file.size());
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