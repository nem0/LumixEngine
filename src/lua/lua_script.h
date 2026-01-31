#pragma once


#include "engine/resource.h"
#include "core/string.h"
#include "core/tag_allocator.h"


namespace black
{


struct LuaScript final : Resource
{
public:
	LuaScript(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	virtual ~LuaScript();

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	StringView getSourceCode() const { return m_source_code; }

	static inline const ResourceType TYPE = ResourceType("lua_script");

private:
	TagAllocator m_allocator;
	Array<LuaScript*> m_dependencies;
	String m_source_code;
};


} // namespace black