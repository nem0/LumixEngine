#include "lua_script_manager.h"

#include "engine/log.h"
#include "engine/fs/file_system.h"


namespace Lumix
{


const ResourceType LuaScript::TYPE("lua_script");


LuaScript::LuaScript(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_source_code(allocator)
{
}


LuaScript::~LuaScript() = default;


void LuaScript::unload()
{
	m_source_code = "";
}


bool LuaScript::load(FS::IFile& file)
{
	m_source_code.set((const char*)file.getBuffer(), (int)file.size());
	m_size = file.size();
	return true;
}


LuaScriptManager::LuaScriptManager(IAllocator& allocator)
	: ResourceManager(allocator)
	, m_allocator(allocator)
{
}


LuaScriptManager::~LuaScriptManager() = default;


Resource* LuaScriptManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, LuaScript)(path, *this, m_allocator);
}


void LuaScriptManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<LuaScript*>(&resource));
}


} // namespace Lumix