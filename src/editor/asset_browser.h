#pragma once


#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/hash_map.h"
#include "engine/delegate_list.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/sync.h"


namespace Lumix
{

struct Action;
struct Material;
struct OutputMemoryStream;
struct StudioApp;


struct LUMIX_EDITOR_API AssetBrowser
{
public:
	struct LUMIX_EDITOR_API IPlugin
	{
		virtual ~IPlugin() {}

		virtual bool canCreateResource() const { return false; }
		virtual bool createResource(const char* path) { return false; }
		
		virtual const char* getFileDialogFilter() const { return ""; }
		virtual const char* getFileDialogExtensions() const { return ""; }
		virtual const char* getDefaultExtension() const { return ""; }

		virtual void onGUI(Span<Resource*> resource) = 0;
		virtual void onResourceUnloaded(Resource* resource) = 0;
		virtual const char* getName() const = 0;
		virtual ResourceType getResourceType() const = 0;
		virtual bool createTile(const char* in_path, const char* out_path, ResourceType type);
		virtual void update() {}
	};

	using OnResourceChanged = DelegateList<void(const Path&, const char*)>;

public:
	explicit AssetBrowser(StudioApp& app);
	~AssetBrowser();
	void onInitFinished();
	void onGUI();
	void update();
	bool onDropFile(const char* path);
	void selectResource(const Path& resource, bool record_history, bool additive);
	bool resourceInput(const char* str_id, Span<char> buf, ResourceType type);
	void addPlugin(IPlugin& plugin);
	void removePlugin(IPlugin& plugin);
	void openInExternalEditor(Resource* resource) const;
	void openInExternalEditor(const char* path) const;
	bool resourceList(Span<char> buf, Ref<u32> selected_idx, ResourceType type, float height, bool can_create_new) const;
	void tile(const Path& path, bool selected);
	OutputMemoryStream* beginSaveResource(Resource& resource);
	void endSaveResource(Resource& resource, OutputMemoryStream& file, bool success);

public:
	bool m_is_open;
	float m_left_column_width = 120;
	static const int TILE_SIZE = 96;

private:
	struct FileInfo
	{
		StaticString<MAX_PATH_LENGTH> clamped_filename;
		StaticString<MAX_PATH_LENGTH> filepath;
		u32 file_path_hash;
		void* tex = nullptr;
		bool create_called = false;
	};

	struct ImmediateTile : FileInfo{
		u32 gc_counter;
	};

private:
	void refreshLabels();
	void dirColumn();
	void fileColumn();
	void detailsGUI();
	void createTile(FileInfo& tile, const char* out_path);
	void thumbnail(FileInfo& tile, float scale);
	int getThumbnailIndex(int i, int j, int columns) const;
	void doFilter();
	void breadcrumbs();
	void changeDir(const char* path);
	void unloadResources();
	void selectResource(Resource* resource, bool record_history, bool additive);
	void goBack();
	void goForward();
	void deleteTile(u32 idx);
	void onResourceListChanged();


private:
	StudioApp& m_app;
	StaticString<MAX_PATH_LENGTH> m_dir;
	bool m_dirty = false;
	Array<StaticString<MAX_PATH_LENGTH> > m_subdirs;
	Array<FileInfo> m_file_infos;
	Array<ImmediateTile> m_immediate_tiles;
	Array<int> m_filtered_file_infos;
	Array<Path> m_history;
	EntityPtr m_dropped_entity = INVALID_ENTITY;
	char m_prefab_name[MAX_PATH_LENGTH] = "";
	int m_history_index;
	HashMap<ResourceType, IPlugin*> m_plugins;
	Array<Resource*> m_selected_resources;
	int m_context_resource;
	char m_filter[128];
	Path m_wanted_resource;
	bool m_is_focus_requested;
	bool m_show_thumbnails;
	bool m_show_subresources;
	float m_thumbnail_size = 1.f;
	Action* m_back_action;
	Action* m_forward_action;
};


} // namespace Lumix