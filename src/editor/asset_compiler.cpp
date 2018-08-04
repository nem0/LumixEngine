#include "asset_compiler.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace Lumix
{


struct AssetCompilerImpl : AssetCompiler
{
	struct LoadHook : ResourceManager::LoadHook
	{
		LoadHook(AssetCompilerImpl& compiler) : compiler(compiler) {}

		bool onBeforeLoad(Resource& res) override
		{
			return compiler.onBeforeLoad(res);
		}

		AssetCompilerImpl& compiler;
	};

	AssetCompilerImpl(StudioApp& app) 
		: m_app(app)
		, m_load_hook(*this)
		, m_plugins(app.getWorldEditor().getAllocator())
	{
		const char* base_path = m_app.getWorldEditor().getEngine().getDiskFileDevice()->getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, ".lumix/assets");
		PlatformInterface::makePath(path);
		ResourceManager& rm = app.getWorldEditor().getEngine().getResourceManager();
		rm.setLoadHook(&m_load_hook);
	}

	~AssetCompilerImpl()
	{
		ResourceManager& rm = m_app.getWorldEditor().getEngine().getResourceManager();
		rm.setLoadHook(nullptr);
	}

	bool onBeforeLoad(Resource& res)
	{
		if (!PlatformInterface::fileExists(res.getPath().c_str())) return false;
		const u32 hash = res.getPath().getHash();
		const StaticString<MAX_PATH_LENGTH> dst_path(".lumix/assets/", hash, ".res");
		if (!PlatformInterface::fileExists(dst_path)
			|| PlatformInterface::getLastModified(dst_path) < PlatformInterface::getLastModified(res.getPath().c_str()))
		{
			char ext[16];
			PathUtils::getExtension(ext, lengthOf(ext), res.getPath().c_str());
			const u32 hash = crc32(ext);
			auto iter = m_plugins.find(hash);
			if (!iter.isValid()) {
				g_log_error.log("Editor") << "Asset compiler does not know how to compile " <<  res.getPath();
				return false;
			}
			return iter.value()->compile(res.getPath());
		}
		return false;
	}

	void removePlugin(IPlugin& plugin) override
	{
		// TODO
		ASSERT(false);
	}

	const char* getCompiledDir() const override { return ".lumix/assets/"; }


	void addPlugin(IPlugin& plugin, const char** extensions) override
	{
		const char** i = extensions;
		while(*i) {
			const u32 hash = crc32(*i);
			m_plugins.insert(hash, &plugin);
			++i;
		}
	}

	StudioApp& m_app;
	LoadHook m_load_hook;
	HashMap<u32, IPlugin*> m_plugins;
};


AssetCompiler* AssetCompiler::create(StudioApp& app)
{
	return LUMIX_NEW(app.getWorldEditor().getAllocator(), AssetCompilerImpl)(app);
}


void AssetCompiler::destroy(AssetCompiler& compiler)
{
	AssetCompilerImpl& impl = (AssetCompilerImpl&)compiler;
	IAllocator& allocator = impl.m_app.getWorldEditor().getAllocator();
	LUMIX_DELETE(allocator, &compiler);
}


} // namespace Lumix

