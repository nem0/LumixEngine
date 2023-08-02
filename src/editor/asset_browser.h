#pragma once

#include "engine/hash.h"
#include "engine/lumix.h"
#include "editor/utils.h"
#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct DelegateList;
template <typename T> struct Span;
template <typename T> struct UniquePtr;

struct LUMIX_EDITOR_API AssetBrowser : StudioApp::GUIPlugin {
	static constexpr int TILE_SIZE = 96;

	struct ResourceView {
		virtual ~ResourceView() {}
		virtual const struct Path& getPath() = 0;
		virtual struct ResourceType getType() = 0;
		virtual void destroy() = 0;
		virtual struct Resource* getResource() = 0;
	};

	struct LUMIX_EDITOR_API Plugin {
		virtual bool canCreateResource() const { return false; }
		virtual void createResource(OutputMemoryStream& content) {}
		virtual const char* getDefaultExtension() const { return ""; }

		virtual const char* getName() const = 0;
		virtual ResourceType getResourceType() const = 0;
		virtual bool createTile(const char* in_path, const char* out_path, ResourceType type);
		virtual void update() {}
		virtual ResourceView& createView(const Path& path, StudioApp& app);
		virtual void onResourceDoubleClicked(const Path& path) {}
	};

	static UniquePtr<AssetBrowser> create(struct StudioApp& app);

	virtual ~AssetBrowser() {}
	virtual void onInitFinished() = 0;
	virtual void selectResource(const Path& resource, bool additive) = 0;
	virtual bool resourceInput(const char* str_id, Span<char> buf, ResourceType type, float width = -1) = 0;
	virtual void addPlugin(Plugin& plugin) = 0;
	virtual void removePlugin(Plugin& plugin) = 0;
	virtual void openInExternalEditor(struct Resource* resource) const = 0;
	virtual void openInExternalEditor(const char* path) const = 0;
	virtual void locate(const Resource& resource) = 0;
	virtual bool resourceList(Span<char> buf, FilePathHash& selected_idx, ResourceType type, bool can_create_new, bool enter_submit = false) = 0;
	virtual void tile(const Path& path, bool selected) = 0;
	virtual void saveResource(Resource& resource, Span<const u8> data) = 0;
	virtual void releaseResources() = 0;
	virtual void reloadTile(FilePathHash hash) = 0; 
	virtual void addWindow(struct AssetEditorWindow* window) = 0;
	virtual AssetEditorWindow* getWindow(const Path& path) = 0;
	virtual void closeWindow(AssetEditorWindow& window) = 0;
};


} // namespace Lumix