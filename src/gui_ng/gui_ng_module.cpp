#include "core/allocator.h"
#include "core/color.h"
#include "core/delegate_list.h"
#include "core/log.h"
#include "core/path.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui_ng/ui.h"
#include "gui_ng/ui_resource.h"
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
	{
		m_document.m_resource_manager = &m_system.getEngine().getResourceManager();
	}

	~GUINGModuleImpl() {
		setUIResource(nullptr);
	}

	static UniquePtr<GUINGModule> createInstance(GUINGSystem& system, World& world, IAllocator& allocator) {
		return UniquePtr<GUINGModuleImpl>::create(allocator, system, world, allocator);
	}

	ui::Document* getDocument() override {
		return &m_document;
	}

	void startGame() override {
		Path ui_path = m_world.getPath();
		if (ui_path.isEmpty()) return;

		m_previous_canvas_size = {-1, -1};
		char tmp[MAX_PATH];
		copyString(tmp, ui_path.c_str());
		Path::replaceExtension(tmp, "ui");
		ui_path = tmp;
		auto* res = m_system.getEngine().getResourceManager().load<UIDocument>(ui_path);
		if (res && res->getState() == Resource::State::READY) {
			m_is_ready = m_document.parse(res->getBlob(), ui_path.c_str());
		}
		setUIResource(res);
	}

	bool isReady() const override { return m_is_ready; }

	void setUIResource(UIDocument* res) {
		if (m_ui_resource) {
			m_ui_resource->decRefCount();
			m_ui_resource->getObserverCb().unbind<&GUINGModuleImpl::onUIStateChanged>(this);
		}
		m_ui_resource = res;
		if (m_ui_resource) {
			m_ui_resource->getObserverCb().bind<&GUINGModuleImpl::onUIStateChanged>(this);
		}
	}

	void onUIStateChanged(Resource::State old_state, Resource::State new_state, Resource& res) {
		if (new_state == Resource::State::READY) {
			m_is_ready = m_document.parse(static_cast<UIDocument&>(res).getBlob(), res.getPath().c_str());
			if (m_is_ready) {
				m_previous_canvas_size = {-1, -1};
			}
		}
	}

	void stopGame() override {
		setUIResource(nullptr);
		m_is_ready = false;
	}

	void serialize(OutputMemoryStream& serializer) override {}
	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {}
	const char* getName() const override { return "gui_ng"; }
	ISystem& getSystem() const override { return m_system; }
	World& getWorld() override { return m_world; }

	void handleInput() {
		InputSystem& input = m_system.getEngine().getInputSystem();
		Span<const InputSystem::Event> events = input.getEvents();
		for (const InputSystem::Event& event : events) {
			m_document.injectEvent(event);
		}
	}

	void handleUIEvents() {
		Span<const ui::Event> events = m_document.getEvents();
		for (const ui::Event& event : events) {
			switch (event.type) {
				case ui::EventType::MOUSE_ENTER:
					// TODO this is called, but seem not to work
					m_document.addClass(event.element_index, "hovered");
					break;
				case ui::EventType::MOUSE_LEAVE:
					m_document.removeClass(event.element_index, "hovered");
					break;
				default:
					break;
			}
		}
	}

	void update(float time_delta) override {
		handleInput();
		handleUIEvents();
		m_document.clearEvents();
		if (m_is_ready && m_canvas_size != m_previous_canvas_size) {
			m_previous_canvas_size = m_canvas_size;
			m_document.computeLayout(m_canvas_size);
		}
	}

	void render(Pipeline& pipeline, Vec2 size) {
		m_canvas_size = size;
		if (m_is_ready && m_canvas_size != m_previous_canvas_size) {
			m_previous_canvas_size = m_canvas_size;
			m_document.computeLayout(m_canvas_size);
		}

		if (!m_is_ready) return;

		Draw2D& draw2d = pipeline.getDraw2D();

		m_document.render(draw2d);
	}

	GUINGSystem& m_system;
	World& m_world;
	IAllocator& m_allocator;
	UIFontManager m_font_manager;
	ui::Document m_document;
	UIDocument* m_ui_resource = nullptr;
	Vec2 m_canvas_size = Vec2(800, 600);
	Vec2 m_previous_canvas_size = Vec2(-1, -1);
	bool m_is_ready = false;
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

bool UIFontManager::isReady(FontHandle font) {
	if (!font) return false;
	return Lumix::isBuilt(*static_cast<Font*>(font));
}

SplitWord UIFontManager::splitFirstWord(FontHandle font, StringView text) {
	return Lumix::splitFirstWord(*static_cast<Font*>(font), text);
}

UniquePtr<GUINGModule> GUINGModule::createInstance(GUINGSystem& system, World& world, IAllocator& allocator) {
	return GUINGModuleImpl::createInstance(system, world, allocator);
}

} // namespace Lumix