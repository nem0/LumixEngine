#include "editor_asset.h"

namespace Lumix {

EditorAssetPlugin::EditorAssetPlugin(const char* name, const char* ext, ResourceType type, StudioApp& app, IAllocator& allocator)
	: AssetBrowser::Plugin(allocator)
	, m_app(app)
	, m_name(name)
	, m_extension(ext)
	, m_resource_type(type)
{
	AssetCompiler& compiler = app.getAssetCompiler();
	compiler.registerExtension(ext, type);
	const char* extensions[] = { ext, nullptr };
	compiler.addPlugin(*this, extensions);
	AssetBrowser& browser = app.getAssetBrowser();
	browser.addPlugin(*this);
}

EditorAssetPlugin::~EditorAssetPlugin() {
	m_app.getAssetBrowser().removePlugin(*this);
	m_app.getAssetCompiler().removePlugin(*this);
}

AssetBrowser::ResourceView& EditorAssetPlugin::createView(const Path& path, StudioApp& app) {
	struct View : AssetBrowser::ResourceView {
		View(const Path& path, ResourceType type, IAllocator& allocator) 
			: m_path(path)
			, m_allocator(allocator)
			, m_type(type)
		{}
			
		const struct Path& getPath() override { return m_path; }
		struct ResourceType getType() override { return m_type; }
		bool isEmpty() override { return false; }
		bool isReady() override { return true; }
		bool isFailure() override { return false; }
		u64 size() override { return 0; }
		void destroy() override { LUMIX_DELETE(m_allocator, this); }
		Resource* getResource() override { ASSERT(false); return nullptr; }

		IAllocator& m_allocator;
		Path m_path;
		ResourceType m_type;
	};

	IAllocator& allocator = app.getAllocator();
	View* view = LUMIX_NEW(allocator, View)(path, m_resource_type, allocator);
	return *view;
}

}