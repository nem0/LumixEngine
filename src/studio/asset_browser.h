#pragma once
#include "core/array.h"
#include "core/path.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
	class Material;
	class Resource;
	class WorldEditor;
}


class FileSystemWatcher;
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

		Count
	};

public:
	AssetBrowser(Lumix::WorldEditor& editor, Metadata& metadata);
	~AssetBrowser();
	void onGUI();
	void update();
	const Lumix::Array<Lumix::Path>& getResources(Type type) const;
	Type getTypeFromResourceManagerType(uint32_t type) const;
	void selectResource(const Lumix::Path& resource);
	bool resourceInput(const char* label, char* buf, int max_size, Type type);

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

private:
	Metadata& m_metadata;
	Lumix::Array<Lumix::Path> m_changed_files;
	Lumix::Array<Lumix::Array<Lumix::Path> > m_resources;
	Lumix::Resource* m_selected_resource;
	Lumix::WorldEditor& m_editor;
	FileSystemWatcher* m_watcher;
	int m_current_type;
	char m_filter[128];
	char m_text_buffer[8192];
	Lumix::Path m_wanted_resource;
	bool m_autoreload_changed_resource;
	bgfx::TextureHandle m_texture_handle;
};