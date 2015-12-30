#pragma once
#include "core/array.h"
#include "core/path.h"
#include "core/mt/sync.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
	class Material;
	class Resource;
	class WorldEditor;
}


class FileSystemWatcher;
class Metadata;


class LUMIX_STUDIO_LIB_API AssetBrowser
{
public:
	enum Type
	{
		MATERIAL,
		MODEL,
		SHADER,
		TEXTURE,
		UNIVERSE,
		LUA_SCRIPT,

		Count
	};

	class IPlugin
	{
	public:
		virtual ~IPlugin() {}

		virtual bool onGUI(Lumix::Resource* resource, Lumix::uint32 type) = 0;
		virtual Lumix::uint32 getResourceType(const char* path) = 0;
		virtual void onResourceUnloaded(Lumix::Resource* resource) = 0;
		virtual const char* getName() const = 0;
		virtual bool hasResourceManager(Lumix::uint32 type) const = 0;
	};

public:
	AssetBrowser(Lumix::WorldEditor& editor, Metadata& metadata);
	~AssetBrowser();
	void onGUI();
	void update();
	const Lumix::Array<Lumix::Path>& getResources(int type) const;
	int getTypeFromResourceManagerType(Lumix::uint32 type) const;
	void selectResource(const Lumix::Path& resource);
	bool resourceInput(const char* label, const char* str_id, char* buf, int max_size, int type);
	void addPlugin(IPlugin& plugin);

public:
	bool m_is_opened;

private:
	void onFileChanged(const char* path);
	void findResources();
	void processDir(const char* path);
	void addResource(const char* path, const char* filename);
	void onGUIResource();
	void onGUIMaterial();
	void onGUIShader();
	void onGUIModel();
	void onGUITexture();
	void onGUILuaScript();
	void saveMaterial(Lumix::Material* material);
	void unloadResource();
	void selectResource(Lumix::Resource* resource);
	void openInExternalEditor(Lumix::Resource* resource);

	Lumix::uint32 getResourceType(const char* path) const;

private:
	Metadata& m_metadata;
	Lumix::Array<Lumix::Path> m_changed_files;
	Lumix::Array<Lumix::Path> m_history;
	Lumix::Array<IPlugin*> m_plugins;
	Lumix::MT::SpinMutex m_changed_files_mutex;
	Lumix::Array<Lumix::Array<Lumix::Path> > m_resources;
	Lumix::Resource* m_selected_resource;
	Lumix::WorldEditor& m_editor;
	FileSystemWatcher* m_watcher;
	int m_current_type;
	char m_filter[128];
	char m_text_buffer[8192];
	Lumix::Path m_wanted_resource;
	bool m_autoreload_changed_resource;
	bool m_is_focus_requested;
	bgfx::TextureHandle m_texture_handle;
};