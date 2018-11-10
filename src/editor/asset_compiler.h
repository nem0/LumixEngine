#pragma once

struct lua_State;

#include "engine/hash_map.h"
#include "engine/resource.h"

namespace Lumix
{


class OutputBlob;
class Path;
struct ResourceType;
class StudioApp;
template <typename T> class Array;
template <typename T> class DelegateList;


template<>
struct HashFunc<ResourceType>
{
	static u32 get(const ResourceType& key)
	{
		return HashFunc<u32>::get(key.type);
	}
};


struct AssetCompiler
{
	struct IPlugin
	{
		virtual ~IPlugin() {}
		virtual bool compile(const Path& src) = 0;
		virtual void addSubresources(AssetCompiler& compiler, const char* path, HashMap<ResourceType, Array<Path>, HashFunc<ResourceType>>& subresources);
	};

	static AssetCompiler* create(StudioApp& app);
	static void destroy(AssetCompiler& compiler);

	virtual void onInitFinished() = 0;

	virtual ~AssetCompiler() {}
	virtual void update() = 0;
	virtual void addPlugin(IPlugin& plugin, const char** extensions) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual bool compile(const Path& path) = 0;
	virtual const char* getCompiledDir() const = 0;
	virtual bool getMeta(const Path& res, void* user_ptr, void (*callback)(void*, lua_State*)) const = 0;
	virtual void updateMeta(const Path& res, const char* src) const = 0;
	virtual const Array<Path>& getResources(ResourceType) const = 0;

	virtual ResourceType getResourceType(const char* path) const = 0;
	virtual void registerExtension(const char* extension, ResourceType type) = 0;
	virtual bool acceptExtension(const char* ext, ResourceType type) const = 0;

	template <typename T>
	bool getMeta(const Path& path, T callback) {
		return getMeta(path, &callback, [](void* user_ptr, lua_State* L){
			return (*(T*)user_ptr)(L);
		});
	}
};


} // namespace Lumix

