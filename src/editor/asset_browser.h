#pragma once

#include "engine/lumix.h"

namespace Lumix {

template <typename T> struct Span;
template <typename T> struct UniquePtr;

struct AssetBrowser {
	static constexpr int TILE_SIZE = 96;

	struct LUMIX_EDITOR_API IPlugin {
		virtual ~IPlugin() {}

		virtual bool canCreateResource() const { return false; }
		virtual bool createResource(const char* path) { return false; }

		virtual const char* getFileDialogFilter() const { return ""; }
		virtual const char* getFileDialogExtensions() const { return ""; }
		virtual const char* getDefaultExtension() const { return ""; }

		virtual void onGUI(Span<struct Resource*> resource) = 0;
		virtual void onResourceUnloaded(Resource* resource) = 0;
		virtual const char* getName() const = 0;
		virtual struct ResourceType getResourceType() const = 0;
		virtual bool createTile(const char* in_path, const char* out_path, ResourceType type);
		virtual void update() {}
	};

	static UniquePtr<AssetBrowser> create(struct StudioApp& app);

	virtual ~AssetBrowser() {}
	virtual void onInitFinished() = 0;
	virtual void onGUI() = 0;
	virtual void update() = 0;
	virtual bool onDropFile(const char* path) = 0;
	virtual void selectResource(const struct Path& resource, bool record_history, bool additive) = 0;
	virtual bool resourceInput(const char* str_id, Span<char> buf, ResourceType type) = 0;
	virtual void addPlugin(IPlugin& plugin) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual void openInExternalEditor(Resource* resource) const = 0;
	virtual void openInExternalEditor(const char* path) const = 0;
	virtual bool resourceList(Span<char> buf, u32& selected_idx, ResourceType type, float height, bool can_create_new) const = 0;
	virtual void tile(const Path& path, bool selected) = 0;
	virtual struct OutputMemoryStream* beginSaveResource(Resource& resource) = 0;
	virtual void endSaveResource(Resource& resource, OutputMemoryStream& file, bool success) = 0;
	virtual void releaseResources() = 0;
	virtual void reloadTile(u32 hash) = 0;
	virtual bool isOpen() const = 0;
	virtual void setOpen(bool open) = 0;
};


} // namespace Lumix