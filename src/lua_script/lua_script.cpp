#include "lua_script.h"

#include "engine/log.h"
#include "engine/file_system.h"


namespace Lumix
{


const ResourceType LuaScript::TYPE("lua_script");


LuaScript::LuaScript(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator, m_path.c_str())
	, m_source_code(m_allocator)
{
}


LuaScript::~LuaScript() = default;


void LuaScript::unload()
{
	m_source_code = "";
}


bool LuaScript::load(u64 size, const u8* mem)
{
	m_source_code = Span((const char*)mem, (u32)size);
	return true;
}


} // namespace Lumix