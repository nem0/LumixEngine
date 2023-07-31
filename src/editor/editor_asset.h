#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"

namespace Lumix {

// use this if you want an editor-only asset to be visible in asset browser
// editor only assets do not inherit from Resource, e.g. particle system function
struct EditorAssetPlugin : AssetBrowser::Plugin, AssetCompiler::IPlugin {
	EditorAssetPlugin(const char* name, const char* ext, ResourceType type, StudioApp& app, IAllocator& allocator);
	~EditorAssetPlugin();

	bool compile(const Path& src) override { return true; }
	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return m_extension; }

	bool onGUI(Span<AssetBrowser::ResourceView*> resource) override { return false; }
	const char* getName() const override { return m_name; }
	ResourceType getResourceType() const override { return m_resource_type; }
	AssetBrowser::ResourceView& createView(const Path& path, StudioApp& app) override;

	void deserialize(InputMemoryStream& blob) override { ASSERT(false); }
	void serialize(OutputMemoryStream& blob) override {}

protected:
	StudioApp& m_app;
	const char* m_extension;
	const char* m_name;
	ResourceType m_resource_type;
};

}