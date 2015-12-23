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
class GUIInterface;
class Metadata;


class AssetBrowser
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
		AUDIO,

		Count
	};

public:
	AssetBrowser(Lumix::WorldEditor& editor, Metadata& metadata);
	~AssetBrowser();
	void setGUIInterface(GUIInterface& gui);
	void onGUI();
	void update();
	const Lumix::Array<Lumix::Path>& getResources(Type type) const;
	Type getTypeFromResourceManagerType(Lumix::uint32 type) const;
	void selectResource(const Lumix::Path& resource);
	bool resourceInput(const char* label, const char* str_id, char* buf, int max_size, Type type);

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
	void onGUIClip();
	void onGUILuaScript();
	void saveMaterial(Lumix::Material* material);
	void unloadResource();
	void selectResource(Lumix::Resource* resource);
	void openInExternalEditor(Lumix::Resource* resource);
	void stopAudio();

private:
	Metadata& m_metadata;
	Lumix::Array<Lumix::Path> m_changed_files;
	Lumix::Array<Lumix::Path> m_history;
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
	void* m_playing_clip;
	GUIInterface* m_gui;
};