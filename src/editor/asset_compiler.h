#pragma once

struct lua_State;

namespace Lumix
{


class OutputBlob;
class Path;
class StudioApp;


struct AssetCompiler
{
	struct IPlugin
	{
		virtual ~IPlugin() {}
		virtual bool compile(const Path& src) = 0;
	};

	static AssetCompiler* create(StudioApp& app);
	static void destroy(AssetCompiler& compiler);

	virtual ~AssetCompiler() {}
	virtual void update() = 0;
	virtual void addPlugin(IPlugin& plugin, const char** extensions) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual bool compile(const Path& path) = 0;
	virtual const char* getCompiledDir() const = 0;
	virtual bool getMeta(const Path& res, void* user_ptr, void (*callback)(void*, lua_State*)) const = 0;
	virtual void updateMeta(const Path& res, const char* src) const = 0;

	template <typename T>
	bool getMeta(const Path& path, T callback) {
		return getMeta(path, &callback, [](void* user_ptr, lua_State* L){
			return (*(T*)user_ptr)(L);
		});
	}
};


} // namespace Lumix

