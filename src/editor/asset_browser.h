#pragma once

#include "core/hash.h"
#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct UniquePtr;
struct ResourceType;

struct LUMIX_EDITOR_API AssetBrowser : StudioApp::GUIPlugin {
	static constexpr int TILE_SIZE = 96;

	struct LUMIX_EDITOR_API IPlugin {
		virtual bool canCreateResource() const { return false; }
		virtual bool canMultiEdit() { return false; }
		virtual void createResource(struct OutputMemoryStream& content) {}
		virtual const char* getDefaultExtension() const { return ""; }

		virtual const char* getLabel() const = 0;
		virtual ResourceType getResourceType() const = 0;
		virtual bool createTile(const char* in_path, const char* out_path, struct ResourceType type);
		virtual void update() {}
		virtual void openEditor(const struct Path& path) {}
		virtual void openMultiEditor(Span<const Path> paths) {}
	};

	static UniquePtr<AssetBrowser> create(struct StudioApp& app);

	virtual ~AssetBrowser() {}
	virtual void onInitFinished() = 0;
	virtual void openEditor(const Path& resource) = 0;
	virtual bool resourceInput(const char* str_id, Path& buf, ResourceType type, float width = -1) = 0;
	virtual void addPlugin(IPlugin& plugin, Span<const char*> extensions) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual IPlugin* getPlugin(StringView extension) const = 0;
	virtual Span<IPlugin*> getPlugins() = 0;
	virtual void openInExternalEditor(struct Resource* resource) const = 0;
	virtual void openInExternalEditor(struct StringView path) const = 0;
	virtual void locate(const Resource& resource) = 0;
	virtual void locate(const Path& resource) = 0;
	virtual bool resourceList(Path& path, FilePathHash& selected_idx, ResourceType type, bool can_create_new, bool enter_submit = false, bool focus = false, float width = 200) = 0;
	virtual Span<const Path> getSelectedResources() = 0;
	virtual void tile(const Path& path, bool selected) = 0;
	virtual void saveResource(const Path& path, Span<const u8> data) = 0;
	virtual void saveResource(Resource& resource, Span<const u8> data) = 0;
	virtual void releaseResources() = 0;
	virtual void reloadTile(FilePathHash hash) = 0; 
	virtual void addWindow(UniquePtr<struct AssetEditorWindow>&& window) = 0;
	virtual AssetEditorWindow* getWindow(const Path& path) = 0;
	virtual void closeWindow(AssetEditorWindow& window) = 0;
};


} // namespace Lumix