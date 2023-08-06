#pragma once


#include "engine/allocators.h"
#include "engine/resource.h"
#include "engine/string.h"


namespace Lumix
{


struct LuaScript final : Resource
{
public:
	LuaScript(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	virtual ~LuaScript();

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	const char* getSourceCode() const { return m_source_code.c_str(); }

	static const ResourceType TYPE;

private:
	TagAllocator m_allocator;
	String m_source_code;
};


} // namespace Lumix