#pragma once

struct lua_State;

#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/resource.h"

namespace Lumix {

struct StudioApp;


struct LUMIX_EDITOR_API AssetCompiler {
	struct LUMIX_EDITOR_API IPlugin {
		virtual ~IPlugin() {}
		virtual bool compile(const Path& src) = 0;
		virtual void addSubresources(AssetCompiler& compiler, const char* path);
	};

	struct ResourceItem {
		Path path;
		ResourceType type;
		RuntimeHash dir_hash;
	};

	static UniquePtr<AssetCompiler> create(StudioApp& app);

	virtual ~AssetCompiler() {}

	virtual void onInitFinished() = 0;
	virtual void onGUI() = 0;
	virtual void update() = 0;
	virtual void addPlugin(IPlugin& plugin, const char** extensions) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual bool compile(const Path& path) = 0;
	virtual bool getMeta(const Path& res, void* user_ptr, void (*callback)(void*, lua_State*)) const = 0;
	virtual void updateMeta(const Path& res, const char* src) const = 0;
	virtual const HashMap<FilePathHash, ResourceItem>& lockResources() = 0;
	virtual void unlockResources() = 0;
	virtual void registerDependency(const Path& included_from, const Path& dependency) = 0;
	virtual void addResource(ResourceType type, const char* path) = 0;
	virtual bool writeCompiledResource(const char* locator, Span<const u8> data) = 0;
	virtual bool copyCompile(const Path& src) = 0;
	virtual DelegateList<void(const Path&)>& listChanged() = 0;
	virtual void onBasePathChanged() = 0;
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

