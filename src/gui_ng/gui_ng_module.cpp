#include "core/allocator.h"
#include "core/color.h"
#include "core/log.h"
#include "core/path.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/world.h"
#include "gui_ng/ui.h"
#include "gui_ng_module.h"
#include "gui_ng_system.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/pipeline.h"

namespace Lumix {

	
struct GUINGModuleImpl : GUINGModule {
	
	GUINGModuleImpl(GUINGSystem& system, World& world, IAllocator& allocator)
		: m_system(system)
		, m_world(world)
		, m_allocator(allocator)
		, m_font_manager(m_system.getEngine())
		, m_document(&m_font_manager, m_allocator)
	{}

	~GUINGModuleImpl() {}

	static UniquePtr<GUINGModule> createInstance(GUINGSystem& system, World& world, IAllocator& allocator) {
		return UniquePtr<GUINGModuleImpl>::create(allocator, system, world, allocator);
	}

	void startGame() override {
		Path ui_path = m_world.getPath();
		if (ui_path.isEmpty()) return;

		m_previous_canvas_size = {-1, -1};
		char tmp[MAX_PATH];
		copyString(tmp, ui_path.c_str());
		Path::replaceExtension(tmp, "ui");
		ui_path = tmp;
		FileSystem& fs = m_system.getEngine().getFileSystem();
		OutputMemoryStream content(m_allocator);
		if (fs.getContentSync(ui_path, content)) {
			StringView markup((const char*)content.data(), (u32)content.size());
			m_document.parse(markup, ui_path.c_str());
		}
	}

	void serialize(OutputMemoryStream& serializer) override {}
	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {}
	const char* getName() const override { return "gui_ng"; }
	ISystem& getSystem() const override { return m_system; }
	World& getWorld() override { return m_world; }

	void update(float time_delta) override {
		if (m_canvas_size != m_previous_canvas_size) {
			m_previous_canvas_size = m_canvas_size;
			m_document.computeLayout(m_canvas_size);
		}
	}

	void render(Pipeline& pipeline, Vec2 size) {
		m_canvas_size = size;
		if (m_canvas_size != m_previous_canvas_size) {
			m_previous_canvas_size = m_canvas_size;
			m_document.computeLayout(m_canvas_size);
		}

		Draw2D& draw2d = pipeline.getDraw2D();

		m_document.render(draw2d);
	}

	GUINGSystem& m_system;
	World& m_world;
	IAllocator& m_allocator;
	UIFontManager m_font_manager;
	ui::Document m_document;
	Vec2 m_canvas_size = Vec2(800, 600);
	Vec2 m_previous_canvas_size = Vec2(-1, -1);
};

ui::IFontManager::FontHandle UIFontManager::loadFont(StringView path, int font_size) {
	Path p(path);
	FontResource* res = m_engine.getResourceManager().load<FontResource>(p);
	if (!res) return nullptr;
	return res->addRef(font_size);
}

Vec2 UIFontManager::measureTextA(FontHandle font, StringView text) {
	if (!font) return Vec2(text.size() * 8.0f, 16.0f);
	return Lumix::measureTextA(*static_cast<Font*>(font), text.begin, text.end);
}

float UIFontManager::getHeight(FontHandle font) {
	if (!font) return 16.0f;
	return Lumix::getHeight(*static_cast<Font*>(font));
}

float UIFontManager::getAscender(FontHandle font) {
	if (!font) return 12.8f;
	return Lumix::getAscender(*static_cast<Font*>(font));
}

UniquePtr<GUINGModule> GUINGModule::createInstance(GUINGSystem& system, World& world, IAllocator& allocator) {
	return GUINGModuleImpl::createInstance(system, world, allocator);
}

} // namespace Lumix