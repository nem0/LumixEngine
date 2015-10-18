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
	struct Property
	{
		char name[50];
		enum Type
		{
			ENTITY,
			FLOAT,
			ANY
		};
		Type type;
	};

public:
	LuaScript(const Path& path,
			  ResourceManager& resource_manager,
			  IAllocator& allocator);
	virtual ~LuaScript();
	
	virtual void unload() override;
	virtual bool load(FS::IFile& file) override;
	const char* getSourceCode() const { return m_source_code.c_str(); }
	const char* getPropertyName(uint32_t hash) const;
	const Array<Property>& getProperties() const
	{
		return m_properties;
	}

private:
	void parseProperties();

private:
	string m_source_code;
	Array<Property> m_properties;
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