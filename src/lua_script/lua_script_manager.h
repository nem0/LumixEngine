#pragma once


#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/string.h"


namespace Lumix
{


class LuaScript final : public Resource
{
public:
	LuaScript(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	virtual ~LuaScript();

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(u64 size, void* mem) override;
	const char* getSourceCode() const { return m_source_code.c_str(); }

	static const ResourceType TYPE;

private:
	string m_source_code;
};


class LuaScriptManager final : public ResourceManager
{
public:
	explicit LuaScriptManager(IAllocator& allocator);
	~LuaScriptManager();

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


} // namespace Lumix