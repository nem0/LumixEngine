#pragma once

struct lua_State;

#include "core/hash.hpp"
#include "core/hash_map.hpp"
#include "engine/resource.hpp"

namespace Lumix {

struct StudioApp;


struct LUMIX_EDITOR_API AssetCompiler {
	struct LUMIX_EDITOR_API IPlugin {
		virtual ~IPlugin() {}
		virtual bool compile(const Path& src) = 0;
		virtual void addSubresources(AssetCompiler& compiler, const Path& path);
		virtual void listLoaded() {}
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
	virtual void addPlugin(IPlugin& plugin, Span<const char*> extensions) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual bool compile(const Path& path) = 0;
	// load meta for `res` and returns it as lua_State*. Must call lua_close on the state after you are done using it.
	virtual lua_State* getMeta(const Path& res) = 0;
	virtual void updateMeta(const Path& resource, Span<const u8> data) const = 0;
	virtual const HashMap<FilePathHash, ResourceItem>& lockResources() = 0;
	virtual void unlockResources() = 0;
	// register non-`Resource` dependency, so we reload dependants in case something changes
	// `Resource` dependencies are automatically handled elsewhere
	virtual void registerDependency(const Path& included_from, const Path& dependency) = 0;
	virtual void addResource(ResourceType type, const Path& path) = 0;
	virtual bool writeCompiledResource(const Path& path, Span<const u8> data) = 0;
	virtual bool copyCompile(const Path& src) = 0;
	virtual DelegateList<void(const Path&)>& listChanged() = 0;
	virtual DelegateList<void(Resource&, bool)>& resourceCompiled() = 0;
	virtual void onBasePathChanged() = 0;
	virtual ResourceType getResourceType(StringView path) const = 0;
	virtual void registerExtension(const char* extension, ResourceType type) = 0;
	virtual bool acceptExtension(StringView ext, ResourceType type) const = 0;
};


} // namespace Lumix

