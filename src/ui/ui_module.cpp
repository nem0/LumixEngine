#include "core/allocator.h"
#include "core/color.h"
#include "core/delegate_list.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "ui.h"
#include "ui_module.h"
#include "ui_resource.h"
#include "ui_system.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/sprite.h"
#include "renderer/texture.h"

namespace Lumix {

struct UIModuleImpl : UIModule {
	struct UI3DComponent {
		UI3DComponent(UIModuleImpl& module, EntityRef entity)
			: module(module)
			, entity(entity)
			, document(&module.m_font_manager, module.m_allocator, &module.m_image_manager, &module.m_sprite_manager)
		{}

		~UI3DComponent() {
			setUIResource(nullptr);
		}

		void setUIResource(ui::DocumentResource* res) {
			if (ui_resource) {
				ui_resource->decRefCount();
				ui_resource->getObserverCb().unbind<&UI3DComponent::onUIStateChanged>(this);
			}

			ui_resource = res;
			is_ready = false;
			previous_canvas_size = Vec2(-1, -1);

			if (ui_resource) {
				ui_resource->onLoaded<&UI3DComponent::onUIStateChanged>(this);
			}
		}

		void setPath(const Path& path) {
			ui_path = path;
			is_ready = false;
			previous_canvas_size = Vec2(-1, -1);

			if (ui_path.isEmpty()) {
				setUIResource(nullptr);
				return;
			}

			ui::DocumentResource* res = module.m_system.getEngine().getResourceManager().load<ui::DocumentResource>(ui_path);
			setUIResource(res);
		}

		void onUIStateChanged(Resource::State old_state, Resource::State new_state, Resource& res) {
			if (new_state == Resource::State::READY) {
				is_ready = document.parse(static_cast<ui::DocumentResource&>(res).getContent(), res.getPath().c_str());
				if (is_ready) {
					previous_canvas_size = Vec2(-1, -1);
				}
			}
			else if (old_state == Resource::State::READY) {
				is_ready = false;
				previous_canvas_size = Vec2(-1, -1);
			}
		}

		UIModuleImpl& module;
		EntityRef entity;
		Path ui_path;
		ui::Document document;
		ui::DocumentResource* ui_resource = nullptr;
		Vec2 previous_canvas_size = Vec2(-1, -1);
		Vec2 virtual_size = Vec2(1000);
		bool orient_to_camera = true;
		bool is_ready = false;
	};

	UIModuleImpl(UISystem& system, World& world, IAllocator& allocator)
		: m_system(system)
		, m_world(world)
		, m_allocator(allocator)
		, m_font_manager(m_system.getEngine())
		, m_image_manager(m_system.getEngine())
		, m_sprite_manager(m_system.getEngine())
		, m_document(&m_font_manager, m_allocator, &m_image_manager, &m_sprite_manager)
		, m_ui_3d_components(m_allocator)
		, m_draw_2d(m_allocator)
	{
		float dpi_scale = os::getDPI() / 96.0f;
		m_document.setDPIScale(dpi_scale);
	}

	~UIModuleImpl() {
		for (UI3DComponent* component : m_ui_3d_components) {
			LUMIX_DELETE(m_allocator, component);
		}
		m_ui_3d_components.clear();
		setUIResource(nullptr);
	}

	static UniquePtr<UIModule> createInstance(UISystem& system, World& world, IAllocator& allocator) {
		return UniquePtr<UIModuleImpl>::create(allocator, system, world, allocator);
	}

	UISystem* getSystemPtr() const override {
		return &m_system;
	}

	ui::Document* getDocument() override {
		return &m_document;
	}

	Path getUI3DPath(EntityRef entity) override {
		return m_ui_3d_components[entity]->ui_path;
	}

	void setUI3DPath(EntityRef entity, const Path& path) override {
		m_ui_3d_components[entity]->setPath(path);
	}

	Vec2 getUI3DVirtualSize(EntityRef entity) override {
		return m_ui_3d_components[entity]->virtual_size;
	}

	void setUI3DVirtualSize(EntityRef entity, Vec2 value) override {
		if (value.x < 1) value.x = 1;
		if (value.y < 1) value.y = 1;
		UI3DComponent* component = m_ui_3d_components[entity];
		component->virtual_size = value;
		component->previous_canvas_size = Vec2(-1, -1);
	}

	bool getUI3DOrientToCamera(EntityRef entity) override {
		return m_ui_3d_components[entity]->orient_to_camera;
	}

	void setUI3DOrientToCamera(EntityRef entity, bool value) override {
		m_ui_3d_components[entity]->orient_to_camera = value;
	}

	void createUI3D(EntityRef entity) override {
		if (m_ui_3d_components.find(entity).isValid()) return;
		UI3DComponent* component = LUMIX_NEW(m_allocator, UI3DComponent)(*this, entity);
		m_ui_3d_components.insert(entity, component);
		m_world.onComponentCreated(entity, types::ui_3d, this);
	}

	void destroyUI3D(EntityRef entity) override {
		auto iter = m_ui_3d_components.find(entity);
		if (!iter.isValid()) return;
		LUMIX_DELETE(m_allocator, iter.value());
		m_ui_3d_components.erase(entity);
		m_world.onComponentDestroyed(entity, types::ui_3d, this);
	}

	void load(const Path& path) override {
		m_previous_canvas_size = {-1, -1};
		m_is_ready = false;

		if (path.isEmpty()) {
			setUIResource(nullptr);
			return;
		}

		auto* res = m_system.getEngine().getResourceManager().load<ui::DocumentResource>(path);
		if (res && res->getState() == Resource::State::READY) {
			m_is_ready = m_document.parse(res->getContent(), path.c_str());
		}
		setUIResource(res);
	}

	void startGame() override {
		Path ui_path { m_world.getPartitions()[0].name };
		if (ui_path.isEmpty()) return;

		char tmp[MAX_PATH];
		copyString(tmp, ui_path.c_str());
		Path::replaceExtension(tmp, "ui");
		ui_path = tmp;
		if (!m_system.getEngine().getFileSystem().fileExists(ui_path)) {
			load(Path());
			return;
		}
		load(ui_path);
	}

	bool isReady() const override { return m_is_ready; }

	void setUIResource(ui::DocumentResource* res) {
		if (m_ui_resource) {
			m_ui_resource->decRefCount();
			m_ui_resource->getObserverCb().unbind<&UIModuleImpl::onUIStateChanged>(this);
		}
		m_ui_resource = res;
		if (m_ui_resource) {
			m_ui_resource->getObserverCb().bind<&UIModuleImpl::onUIStateChanged>(this);
		}
	}

	void onUIStateChanged(Resource::State old_state, Resource::State new_state, Resource& res) {
		if (new_state == Resource::State::READY) {
			m_is_ready = m_document.parse(static_cast<ui::DocumentResource&>(res).getContent(), res.getPath().c_str());
			if (m_is_ready) {
				m_previous_canvas_size = {-1, -1};
			}
		}
	}
 
	void stopGame() override {
		setUIResource(nullptr);
		m_is_ready = false;
	}

	bool shouldSerialize() override {
		return !m_ui_3d_components.empty();
	}

	void serialize(OutputMemoryStream& serializer) override {
		serializer.write(m_ui_3d_components.size());
		for (UI3DComponent* component : m_ui_3d_components) {
			serializer.write(component->entity);
			serializer.writeString(component->ui_path.c_str());
			serializer.write(component->virtual_size);
			serializer.write(component->orient_to_camera);
		}
	}

	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {
		i32 count;
		serializer.read(count);
		for (i32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);

			const char* path = serializer.readString();
			Vec2 virtual_size;
			serializer.read(virtual_size);
			bool orient_to_camera;
			serializer.read(orient_to_camera);

			createUI3D(entity);
			setUI3DVirtualSize(entity, virtual_size);
			setUI3DOrientToCamera(entity, orient_to_camera);
			if (path[0]) {
				setUI3DPath(entity, Path(path));
			}
		}
	}
	const char* getName() const override { return "ui"; }
	ISystem& getSystem() const override { return m_system; }
	World& getWorld() override { return m_world; }

	void update(float time_delta) override {
		m_document.clearEvents();
		InputSystem& input = m_system.getEngine().getInputSystem();
		Span<const InputSystem::Event> events = input.getEvents();
		for (const InputSystem::Event& event : events) {
			m_document.injectEvent(event);
		}
		if (m_is_ready && m_canvas_size != m_previous_canvas_size) {
			m_previous_canvas_size = m_canvas_size;
			m_document.computeLayout(m_canvas_size);
		}
	}

	void render(Pipeline& pipeline, Vec2 size) override {
		m_canvas_size = size;
		if (m_is_ready && m_canvas_size != m_previous_canvas_size) {
			m_previous_canvas_size = m_canvas_size;
			m_document.computeLayout(m_canvas_size);
		}

		if (!m_is_ready) return;

		Draw2D& draw2d = pipeline.getDraw2D();

		m_document.render(draw2d);
	}

	void render3D(Pipeline& pipeline) override {
		const Texture* atlas_texture = pipeline.getRenderer().getFontManager().getAtlasTexture();
		const Vec2 atlas_size = atlas_texture ? Vec2((float)atlas_texture->width, (float)atlas_texture->height) : Vec2(1, 1);

		for (UI3DComponent* component : m_ui_3d_components) {
			if (!component->is_ready) continue;
			if (component->virtual_size.x <= 0 || component->virtual_size.y <= 0) continue;

			if (component->virtual_size != component->previous_canvas_size) {
				component->previous_canvas_size = component->virtual_size;
				component->document.computeLayout(component->virtual_size);
			}

			m_draw_2d.clear(atlas_size);
			component->document.render(m_draw_2d);
			pipeline.render3DUI(component->entity, m_draw_2d, component->virtual_size, component->orient_to_camera);
		}
	}

	UISystem& m_system;
	World& m_world;
	IAllocator& m_allocator;
	UIFontManager m_font_manager;
	UIImageManager m_image_manager;
	UISpriteManager m_sprite_manager;
	ui::Document m_document;
	ui::DocumentResource* m_ui_resource = nullptr;
	Vec2 m_canvas_size = Vec2(800, 600);
	Vec2 m_previous_canvas_size = Vec2(-1, -1);
	bool m_is_ready = false;
	HashMap<EntityRef, UI3DComponent*> m_ui_3d_components;
	Draw2D m_draw_2d;
};

ui::IFontManager::FontHandle UIFontManager::loadFont(StringView path, int font_size) {
	Path p(path);
	FontResource* res = m_engine.getResourceManager().load<FontResource>(p);
	if (!res) return nullptr;
	return res->addRef(font_size);
}

void UIFontManager::unloadFont(FontHandle font) {
	if (!font) return;
	Lumix::release(*static_cast<Font*>(font));
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

ui::IImageManager::ImageHandle UIImageManager::loadImage(StringView path) {
	if (!m_engine) return nullptr;
	Texture* res = m_engine->getResourceManager().load<Texture>(Path(path));
	return (ImageHandle)res;
}

void UIImageManager::unloadImage(ImageHandle image) {
	if (!image) return;
	static_cast<Texture*>(image)->decRefCount();
}

bool UIImageManager::isReady(ImageHandle image) {
	if (!image) return false;
	return static_cast<Texture*>(image)->isReady();
}

Vec2 UIImageManager::getIntrinsicSize(ImageHandle image) {
	if (!image) return Vec2(0);
	Texture* texture = static_cast<Texture*>(image);
	return Vec2((float)texture->width, (float)texture->height);
}

ui::ISpriteManager::SpriteHandle UISpriteManager::loadSprite(StringView path) {
	if (!m_engine) return nullptr;
	return m_engine->getResourceManager().load<Sprite>(Path(path));
}

void UISpriteManager::unloadSprite(SpriteHandle sprite) {
	if (!sprite) return;
	static_cast<Sprite*>(sprite)->decRefCount();
}

bool UISpriteManager::isReady(SpriteHandle sprite) {
	if (!sprite) return false;
	return static_cast<Sprite*>(sprite)->isReady();
}

UniquePtr<UIModule> UIModule::createInstance(UISystem& system, World& world, IAllocator& allocator) {
	return UIModuleImpl::createInstance(system, world, allocator);
}

void UIModule::reflect() {
	#include "ui_module.gen.h"
}

} // namespace Lumix::ui
