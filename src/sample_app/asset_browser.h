#pragma once
#include "core/array.h"
#include "core/path.h"


namespace Lumix
{
	class Material;
	class Resource;
	class WorldEditor;
}


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

		Count
	};

public:
	AssetBrowser(Lumix::WorldEditor& editor);
	void onGui();
	const Lumix::Array<Lumix::Path>& getResources(Type type) const;

private:
	void findResources();
	void processDir(const char* path);
	void addResource(const char* path, const char* filename);
	void onGuiResource();
	void onGuiMaterial();
	void onGuiModel();
	void onGuiTexture();
	void saveMaterial(Lumix::Material* material);
	bool resourceInput(const char* label, char* buf, int max_size, Type type);

private:
	Lumix::Array<Lumix::Array<Lumix::Path> > m_resources;
	Lumix::Resource* m_selected_resouce;
	Lumix::WorldEditor& m_editor;
};