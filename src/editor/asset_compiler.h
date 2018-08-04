#pragma once

namespace Lumix
{


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
	virtual void addPlugin(IPlugin& plugin, const char** extensions) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual const char* getCompiledDir() const = 0;
};


} // namespace Lumix

