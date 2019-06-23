#pragma once


#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/hash_map.h"
#include "engine/delegate_list.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/mt/sync.h"


namespace Lumix
{

struct Action;
class FileSystemWatcher;
class Material;
class OutputMemoryStream;
class StudioApp;
class WorldEditor;


class LUMIX_EDITOR_API AssetBrowser
{
public:
	struct LUMIX_EDITOR_API IPlugin
	{
		virtual ~IPlugin() {}

		virtual bool canCreateResource() const { return false; }
		virtual bool createResource(char* out_path, int max_size) { return false; }
		virtual void onGUI(Resource* resource) = 0;
		virtual void onResourceUnloaded(Resource* resource) = 0;
		virtual const char* getName() const = 0;
		virtual ResourceType getResourceType() const = 0;
		virtual bool createTile(const char* in_path, const char* out_path, ResourceType type);
		virtual void update() {}
	};

	typedef DelegateList<void(const Path&, const char*)> OnResourceChanged;

public:
	explicit AssetBrowser(StudioApp& app);
	~AssetBrowser();
	void onGUI();
	void update();
	int getTypeIndex(ResourceType type) const;
	void selectResource(const Path& resource, bool record_history);
	bool resourceInput(const char* label, const char* str_id, char* buf, int max_size, ResourceType type);
	void addPlugin(IPlugin& plugin);
	void removePlugin(IPlugin& plugin);
	void openInExternalEditor(Resource* resource) const;
	void openInExternalEditor(const char* path) const;
	bool resourceList(char* buf, int max_size, ResourceType type, float height) const;
	OutputMemoryStream* beginSaveResource(Resource& resource);
	void endSaveResource(Resource& resource, OutputMemoryStream& file, bool success);

public:
	bool m_is_open;
	float m_left_column_width = 120;
	static const int TILE_SIZE = 128;

private:
	struct FileInfo
	{
		StaticString<MAX_PATH_LENGTH> clamped_filename;
		StaticString<MAX_PATH_LENGTH> filepath;
		u32 file_path_hash;
		void* tex = nullptr;
		bool create_called = false;
	};

private:
	void dirColumn();
	void fileColumn();
	void detailsGUI();
	void createTile(FileInfo& tile, const char* out_path);
	void thumbnail(FileInfo& tile);
	int getThumbnailIndex(int i, int j, int columns) const;
	void doFilter();
	void breadcrumbs();
	void changeDir(const char* path);
	void unloadResource();
	void selectResource(Resource* resource, bool record_history);
	void goBack();
	void goForward();


private:
	StudioApp& m_app;
	StaticString<MAX_PATH_LENGTH> m_dir;
	Array<StaticString<MAX_PATH_LENGTH> > m_subdirs;
	Array<FileInfo> m_file_infos;
	Array<int> m_filtered_file_infos;
	Array<Path> m_history;
	int m_history_index;
	HashMap<ResourceType, IPlugin*> m_plugins;
	Resource* m_selected_resource;
	WorldEditor& m_editor;
	int m_current_type;
	char m_filter[128];
	Path m_wanted_resource;
	bool m_is_focus_requested;
	bool m_show_thumbnails;
	Action* m_back_action;
	Action* m_forward_action;
};


} // namespace Lumix