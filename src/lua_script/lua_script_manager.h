#pragma once


#include "core/array.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"
#include "core/string.h"


namespace Lumix
{


class LuaScript : public Resource
{
public:
	typedef char PropertyName[50];

public:
	LuaScript(const Path& path,
			  ResourceManager& resource_manager,
			  IAllocator& allocator);
	virtual ~LuaScript();
	
	virtual void doUnload() override;
	virtual void
	loaded(FS::IFile& file, bool success, FS::FileSystem& fs) override;
	const char* getSourceCode() const { return m_source_code.c_str(); }
	const char* getPropertyName(uint32_t hash) const;
	const Array<PropertyName>& getPropertiesNames() const
	{
		return m_properties;
	}

private:
	void parseProperties();

private:
	string m_source_code;
	Array<PropertyName> m_properties;
};


class LuaScriptManager : public ResourceManagerBase
{
public:
	LuaScriptManager(IAllocator& allocator);
	~LuaScriptManager();

protected:
	virtual Resource* createResource(const Path& path) override;
	virtual void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


} // namespace Lumix