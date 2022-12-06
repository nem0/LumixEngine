#pragma once

#include "engine/hash.h"
#include "engine/lumix.h"
#include "editor/utils.h"
#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct Span;
template <typename T> struct UniquePtr;

struct LUMIX_EDITOR_API AssetBrowser : StudioApp::GUIPlugin {
	static constexpr int TILE_SIZE = 96;

	struct LUMIX_EDITOR_API Plugin : SimpleUndoRedo {
		Plugin(IAllocator& allocator) : SimpleUndoRedo(allocator) {}

		virtual bool canCreateResource() const { return false; }
		virtual bool createResource(const char* path) { return false; }

		virtual const char* getFileDialogFilter() const { return ""; }
		virtual const char* getFileDialogExtensions() const { return ""; }
		virtual const char* getDefaultExtension() const { return ""; }

		virtual bool onGUI(Span<struct Resource*> resource) = 0;
		virtual void onResourceUnloaded(Resource* resource) = 0;
		virtual const char* getName() const = 0;
		virtual struct ResourceType getResourceType() const = 0;
		virtual bool createTile(const char* in_path, const char* out_path, ResourceType type);
		virtual void update() {}

		void gui(Span<Resource*> resources);

	private:
		RuntimeHash m_current_hash;
		// after an edit, a resource can reload itself (e.g. changing shader on material),
		// we need to save undo after the the resource is reloaded, therefore m_defer_push_undo
		bool m_defer_push_undo = false;

	};

	static UniquePtr<AssetBrowser> create(struct StudioApp& app);

	virtual ~AssetBrowser() {}
	virtual void onInitFinished() = 0;
	virtual void selectResource(const struct Path& resource, bool record_history, bool additive) = 0;
	virtual bool resourceInput(const char* str_id, Span<char> buf, ResourceType type, float width = -1) = 0;
	virtual void addPlugin(Plugin& plugin) = 0;
	virtual void removePlugin(Plugin& plugin) = 0;
	virtual void openInExternalEditor(Resource* resource) const = 0;
	virtual void openInExternalEditor(const char* path) const = 0;
	virtual bool resourceList(Span<char> buf, FilePathHash& selected_idx, ResourceType type, float height, bool can_create_new, bool enter_submit = false) const = 0;
	virtual void tile(const Path& path, bool selected) = 0;
	virtual struct OutputMemoryStream* beginSaveResource(Resource& resource) = 0;
	virtual void endSaveResource(Resource& resource, OutputMemoryStream& file, bool success) = 0;
	virtual void releaseResources() = 0;
	virtual void reloadTile(FilePathHash hash) = 0; 
	virtual bool copyTile(const char* from, const char* to) = 0;
};


} // namespace Lumix