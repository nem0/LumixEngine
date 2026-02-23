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

	Engine& m_engine;
};

//@ module GUINGModule gui_ng "GUI NG"
struct GUINGModule : IModule {
	static UniquePtr<GUINGModule> createInstance(struct GUINGSystem& system, World& world, struct IAllocator& allocator);
	static void reflect();

	virtual void render(struct Pipeline& pipeline, Vec2 size) = 0;
};

} // namespace Lumix