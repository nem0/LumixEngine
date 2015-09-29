#pragma once
#include "core/array.h"
#include "core/path.h"
#include <bgfx.h>


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
	void onGui();
	void update();
	const Lumix::Array<Lumix::Path>& getResources(Type type) const;
	Type getTypeFromResourceManagerType(uint32_t type) const;

public:
	bool m_is_opened;

private:
	void onFileChanged(const char* path);
	void findResources();
	void processDir(const char* path);
	void addResource(const char* path, const char* filename);
	void onGuiResource();
	void onGuiMaterial();
	void onGuiShader();
	void onGuiModel();
	void onGuiTexture();
	void onGuiLuaScript();
	void saveMaterial(Lumix::Material* material);
	bool resourceInput(const char* label, char* buf, int max_size, Type type);
	void unloadResource();
	void selectResource(const Lumix::Path& resource);
	void selectResource(Lumix::Resource* resource);

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
	bool m_autoreload_changed_resource;
	bgfx::TextureHandle m_texture_handle;
};