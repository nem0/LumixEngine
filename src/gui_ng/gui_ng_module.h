#pragma once

#include "engine/plugin.h"
#include "gui_ng/ui.h"

namespace Lumix {

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

//@ module GUINGModule gui_ng "GUI NG"
struct GUINGModule : IModule {
	static UniquePtr<GUINGModule> createInstance(struct GUINGSystem& system, World& world, struct IAllocator& allocator);
	static void reflect();

	//@ functions
	virtual ui::Document* getDocument() = 0; // TODO use a reference once meta supports references
	virtual bool isReady() const = 0;
	//@ end
	virtual void render(struct Pipeline& pipeline, Vec2 size) = 0;
};

} // namespace Lumix