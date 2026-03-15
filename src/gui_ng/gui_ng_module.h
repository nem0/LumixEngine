#pragma once

#include "engine/plugin.h"
#include "gui_ng/ui.h"

namespace Lumix {

struct Path;

struct UIFontManager : ui::IFontManager {
	UIFontManager(Engine& engine) : m_engine(engine) {}

	// TODO leaks fonts
	ui::IFontManager::FontHandle loadFont(StringView path, int font_size) override;
	Vec2 measureTextA(FontHandle font, StringView text) override;
	float getHeight(FontHandle font) override;
	float getAscender(FontHandle font) override;
	bool isReady(FontHandle font) override;
	SplitWord splitFirstWord(FontHandle font, StringView text) override;

	Engine& m_engine;
};

struct UIImageManager : ui::IImageManager {
	UIImageManager(Engine& engine) : m_engine(&engine) {}

	ImageHandle loadImage(StringView path) override;
	bool isReady(ImageHandle image) override;
	Vec2 getIntrinsicSize(ImageHandle image) override;
	Engine* m_engine;
};

//@ module UIModule ui "UI"
struct UIModule : IModule {
	static UniquePtr<UIModule> createInstance(struct UISystem& system, World& world, struct IAllocator& allocator);
	static void reflect();
	
	virtual void createUI3D(EntityRef entity) = 0;
	virtual void destroyUI3D(EntityRef entity) = 0;

	//@ component UI3D id ui_3d label "3D"
	virtual Path getUI3DPath(EntityRef entity) = 0; //@ label "Source" resource_type ui::DocumentResource::TYPE
	virtual void setUI3DPath(EntityRef entity, const Path& path) = 0;
	virtual Vec2 getUI3DVirtualSize(EntityRef entity) = 0;
	virtual void setUI3DVirtualSize(EntityRef entity, Vec2 value) = 0;
	virtual bool getUI3DOrientToCamera(EntityRef entity) = 0;
	virtual void setUI3DOrientToCamera(EntityRef entity, bool value) = 0;
	//@ end

	//@ functions
	virtual ui::Document* getDocument() = 0; // TODO use a reference once meta supports references
	virtual bool isReady() const = 0;
	//@ end
	virtual void render(struct Pipeline& pipeline, Vec2 size) = 0;
	virtual void render3D(struct Pipeline& pipeline) = 0;
};

} // namespace Lumix
