#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/math_utils.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/reflection.h"
#include "engine/universe/universe.h"
#include "gui/gui_scene.h"
#include "gui/sprite_manager.h"
#include "imgui/imgui.h"
#include "renderer/draw2d.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"


using namespace Lumix;


namespace
{


static const ComponentType GUI_RECT_TYPE = Reflection::getComponentType("gui_rect");
static const ComponentType GUI_IMAGE_TYPE = Reflection::getComponentType("gui_image");
static const ComponentType GUI_TEXT_TYPE = Reflection::getComponentType("gui_text");
static const ComponentType GUI_BUTTON_TYPE = Reflection::getComponentType("gui_button");


struct SpritePlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
	SpritePlugin(StudioApp& app) : app(app) {}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != Sprite::TYPE) return false;

		Sprite* sprite = (Sprite*)resource;
		
		char tmp[MAX_PATH_LENGTH];
		Texture* tex = sprite->getTexture();
		copyString(tmp, tex ? tex->getPath().c_str() : "");
		if (app.getAssetBrowser().resourceInput("Texture", "texture", tmp, lengthOf(tmp), Texture::TYPE))
		{
			sprite->setTexture(Path(tmp));
		}

		switch (sprite->type)
		{
			case Sprite::Type::PATCH9:
				ImGui::LabelText("Type", "%s", "9 patch");
				break;
			case Sprite::Type::SIMPLE:
				ImGui::LabelText("Type", "%s", "Simple");
				break;
			default:
				ImGui::LabelText("Type", "%s", "Unknown");
				ASSERT(false);
				break;
		}

		ImGui::InputInt("Top", &sprite->top);
		ImGui::InputInt("Right", &sprite->right);
		ImGui::InputInt("Bottom", &sprite->bottom);
		ImGui::InputInt("Left", &sprite->left);

		return true;
	}


	ResourceType getResourceType(const char* ext) override
	{
		return equalStrings(ext, "spr") ? Sprite::TYPE : INVALID_RESOURCE_TYPE;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Sprite"; }

	bool hasResourceManager(ResourceType type) const override { return type == Sprite::TYPE; }


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == Sprite::TYPE && equalStrings(ext, "spr");
	}

	StudioApp& app;
};


class GUIEditor LUMIX_FINAL : public StudioApp::IPlugin
{
enum class EdgeMask
{
	LEFT = 1 << 0,
	RIGHT = 1 << 1,
	TOP = 1 << 2,
	BOTTOM = 1 << 3,
	ALL = LEFT | RIGHT | TOP | BOTTOM
};

public:
	GUIEditor(StudioApp& app)
	{
		IAllocator& allocator = app.getWorldEditor().getAllocator();

		Action* action = LUMIX_NEW(allocator, Action)("GUI Editor", "Toggle gui editor", "gui_editor");
		action->func.bind<GUIEditor, &GUIEditor::onAction>(this);
		action->is_selected.bind<GUIEditor, &GUIEditor::isOpen>(this);
		app.addWindowAction(action);

		m_editor = &app.getWorldEditor();
		Renderer& renderer = *static_cast<Renderer*>(m_editor->getEngine().getPluginManager().getPlugin("renderer"));
		m_pipeline = Pipeline::create(renderer, Path("pipelines/draw2d.lua"), "", allocator);
		m_pipeline->load();

		m_editor->universeCreated().bind<GUIEditor, &GUIEditor::onUniverseChanged>(this);
		m_editor->universeDestroyed().bind<GUIEditor, &GUIEditor::onUniverseChanged>(this);
	}


	~GUIEditor()
	{
		m_editor->universeCreated().unbind<GUIEditor, &GUIEditor::onUniverseChanged>(this);
		m_editor->universeDestroyed().unbind<GUIEditor, &GUIEditor::onUniverseChanged>(this);
		Pipeline::destroy(m_pipeline);
	}


private:
	enum class MouseMode
	{
		NONE,
		RESIZE,
		MOVE
	};


	void onAction() { m_is_window_open = !m_is_window_open; }
	bool isOpen() const { return m_is_window_open; }


	void onUniverseChanged()
	{
		Universe* universe = m_editor->getUniverse();
		if (!universe)
		{
			m_pipeline->setScene(nullptr);
			return;
		}
		RenderScene* scene = (RenderScene*)universe->getScene(crc32("renderer"));
		m_pipeline->setScene(scene);
	}


	MouseMode drawGizmo(Draw2D& draw, GUIScene& scene, const Vec2& canvas_size, const ImVec2& mouse_canvas_pos)
	{
		auto& selected_entities = m_editor->getSelectedEntities();
		if (selected_entities.size() != 1) return MouseMode::NONE;

		Entity e = selected_entities[0];
		if (!scene.hasGUI(e)) return MouseMode::NONE;

		GUIScene::Rect& rect = scene.getRectOnCanvas(e, canvas_size);
		Vec2 bottom_right = { rect.x + rect.w, rect.y + rect.h };
		draw.AddRect({ rect.x, rect.y }, bottom_right, 0xfff00fff);
		Vec2 mid = { rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f };

		auto drawHandle = [&](const Vec2& pos, const ImVec2& mouse_pos) {
			const float SIZE = 5;
			float dx = pos.x - mouse_pos.x;
			float dy = pos.y - mouse_pos.y;
			bool is_hovered = Math::abs(dx) < SIZE && Math::abs(dy) < SIZE;
			draw.AddRectFilled(pos - Vec2(SIZE, SIZE), pos + Vec2(SIZE, SIZE), is_hovered ? 0xffffffff : 0x77ffFFff);

			return is_hovered && ImGui::IsMouseClicked(0);
		};

		MouseMode ret = MouseMode::NONE;
		if (drawHandle(bottom_right, mouse_canvas_pos))
		{
			m_bottom_right_start_transform.x = scene.getRectRightPoints(e);
			m_bottom_right_start_transform.y = scene.getRectBottomPoints(e);
			ret = MouseMode::RESIZE;
		}
		if (drawHandle(mid, mouse_canvas_pos))
		{
			m_bottom_right_start_transform.x = scene.getRectRightPoints(e);
			m_bottom_right_start_transform.y = scene.getRectBottomPoints(e);
			m_top_left_start_move.y = scene.getRectTopPoints(e);
			m_top_left_start_move.x = scene.getRectLeftPoints(e);
			ret = MouseMode::MOVE;
		}
		return ret;
	}
		

	static Vec2 toLumix(const ImVec2& value)
	{
		return { value.x, value.y };
	}



	void onWindowGUI() override
	{
		if (ImGui::BeginDock("GUIEditor", &m_is_window_open))
		{
			ImVec2 mouse_canvas_pos = ImGui::GetMousePos();
			mouse_canvas_pos.x -= ImGui::GetCursorScreenPos().x;
			mouse_canvas_pos.y -= ImGui::GetCursorScreenPos().y;
			
			if (!m_pipeline->isReady()) return;
			ImVec2 size = ImGui::GetContentRegionAvail();
			GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));
			scene->render(*m_pipeline, { size.x, size.y });
			
			MouseMode new_mode = drawGizmo(m_pipeline->getDraw2D(), *scene, { size.x, size.y }, mouse_canvas_pos);
			if (m_mouse_mode == MouseMode::NONE) m_mouse_mode = new_mode;
			if (ImGui::IsMouseReleased(0)) m_mouse_mode = MouseMode::NONE;
			
			if (m_editor->getSelectedEntities().size() == 1)
			{
				Entity e = m_editor->getSelectedEntities()[0];
				switch (m_mouse_mode)
				{
					case MouseMode::RESIZE:
					{
						float b = m_bottom_right_start_transform.y + ImGui::GetMouseDragDelta(0).y;
						scene->setRectBottomPoints(e, b);
						float r = m_bottom_right_start_transform.x + ImGui::GetMouseDragDelta(0).x;
						scene->setRectRightPoints(e, r);
					}
					break;
					case MouseMode::MOVE:
					{
						float b = m_bottom_right_start_transform.y + ImGui::GetMouseDragDelta(0).y;
						scene->setRectBottomPoints(e, b);
						float r = m_bottom_right_start_transform.x + ImGui::GetMouseDragDelta(0).x;
						scene->setRectRightPoints(e, r);

						float t = m_top_left_start_move.y + ImGui::GetMouseDragDelta(0).y;
						scene->setRectTopPoints(e, t);
						float l = m_top_left_start_move.x + ImGui::GetMouseDragDelta(0).x;
						scene->setRectLeftPoints(e, l);
					}
					break;
				}
			}

			if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && m_mouse_mode == MouseMode::NONE)
			{
				Entity e = scene->getRectAt(toLumix(mouse_canvas_pos), toLumix(size));
				if (e.isValid()) m_editor->selectEntities(&e, 1);
			}

			m_pipeline->resize(int(size.x), int(size.y));
			m_pipeline->render();
			m_texture_handle = m_pipeline->getRenderbuffer("default", 0);
			ImGui::Image(&m_texture_handle, size);

			bool has_rect = false;
			if (m_editor->getSelectedEntities().size() == 1)
			{
				has_rect = m_editor->getUniverse()->hasComponent(m_editor->getSelectedEntities()[0], GUI_RECT_TYPE);
			}
			if (has_rect && ImGui::BeginPopupContextItem("context"))
			{
				Entity e = m_editor->getSelectedEntities()[0];
				if (ImGui::BeginMenu("Make relative"))
				{
					if (ImGui::MenuItem("All")) makeRelative(e, toLumix(size), (u8)EdgeMask::ALL);
					if (ImGui::MenuItem("Top")) makeRelative(e, toLumix(size), (u8)EdgeMask::TOP);
					if (ImGui::MenuItem("Right")) makeRelative(e, toLumix(size), (u8)EdgeMask::RIGHT);
					if (ImGui::MenuItem("Bottom")) makeRelative(e, toLumix(size), (u8)EdgeMask::BOTTOM);
					if (ImGui::MenuItem("Left")) makeRelative(e, toLumix(size), (u8)EdgeMask::LEFT);
					
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Make absolute"))
				{
					if (ImGui::MenuItem("All")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::ALL);
					if (ImGui::MenuItem("Top")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::TOP);
					if (ImGui::MenuItem("Right")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::RIGHT);
					if (ImGui::MenuItem("Bottom")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::BOTTOM);
					if (ImGui::MenuItem("Left")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::LEFT);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Create child"))
				{
					if (ImGui::MenuItem("Button")) createChild(e, GUI_BUTTON_TYPE);
					if (ImGui::MenuItem("Image")) createChild(e, GUI_IMAGE_TYPE);
					if (ImGui::MenuItem("Rect")) createChild(e, GUI_RECT_TYPE);
					if (ImGui::MenuItem("Text")) createChild(e, GUI_TEXT_TYPE);
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}
		}

		ImGui::EndDock();
	}


	void createChild(Entity entity, ComponentType child_type)
	{
		m_editor->beginCommandGroup(crc32("create_gui_rect_child"));
		Entity child = m_editor->addEntity();
		m_editor->makeParent(entity, child);
		m_editor->selectEntities(&child, 1);
		m_editor->addComponent(child_type);
		m_editor->endCommandGroup();
	}


	void setRectProperty(Entity e, const char* prop_name, float value)
	{
		const Reflection::PropertyBase* prop = Reflection::getProperty(GUI_RECT_TYPE, crc32(prop_name));
		ASSERT(prop);
		m_editor->setProperty(GUI_RECT_TYPE, -1, *prop, &e, 1, &value, sizeof(value));
	}

	
	void makeAbsolute(Entity entity, const Vec2& canvas_size, u8 mask)
	{
		GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));

		Entity parent = scene->getUniverse().getParent(entity);
		GUIScene::Rect parent_rect = scene->getRectOnCanvas(parent, canvas_size);
		GUIScene::Rect child_rect = scene->getRectOnCanvas(entity, canvas_size);

		m_editor->beginCommandGroup(crc32("make_gui_rect_absolute"));

		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Top Relative", 0);
			setRectProperty(entity, "Top Points", child_rect.y - parent_rect.y);
		}
		
		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Left Relative", 0);
			setRectProperty(entity, "Left Points", child_rect.x - parent_rect.x);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Right Relative", 0);
			setRectProperty(entity, "Right Points", child_rect.x + child_rect.w - parent_rect.x);
		}
		
		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Bottom Relative", 0);
			setRectProperty(entity, "Bottom Points", child_rect.y + child_rect.h - parent_rect.y);
		}

		m_editor->endCommandGroup();
	}


	void makeRelative(Entity entity, const Vec2& canvas_size, u8 mask)
	{
		GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));
		
		Entity parent = scene->getUniverse().getParent(entity);
		GUIScene::Rect parent_rect = scene->getRectOnCanvas(parent, canvas_size);
		GUIScene::Rect child_rect = scene->getRectOnCanvas(entity, canvas_size);

		m_editor->beginCommandGroup(crc32("make_gui_rect_relative"));
		
		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Top Points", 0);
			setRectProperty(entity, "Top Relative", (child_rect.y - parent_rect.y) / parent_rect.h);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Right Points", 0);
			setRectProperty(entity, "Right Relative", (child_rect.x + child_rect.w - parent_rect.x) / parent_rect.w);
		}

		
		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Left Points", 0);
			setRectProperty(entity, "Left Relative", (child_rect.x - parent_rect.x) / parent_rect.w);
		}
			
		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Bottom Points", 0);
			setRectProperty(entity, "Bottom Relative", (child_rect.y + child_rect.h - parent_rect.y) / parent_rect.h);
		}

		m_editor->endCommandGroup();
	}


	bool hasFocus() override { return false; }
	void update(float) override {}
	const char* getName() const override { return "gui_editor"; }


	Pipeline* m_pipeline = nullptr;
	WorldEditor* m_editor = nullptr;
	bool m_is_window_open = false;
	bgfx::TextureHandle m_texture_handle;
	MouseMode m_mouse_mode = MouseMode::NONE;
	Vec2 m_bottom_right_start_transform;
	Vec2 m_top_left_start_move;
};



} // anonymous namespace


LUMIX_STUDIO_ENTRY(gui)
{
	app.registerComponent("gui_button", "GUI/Button");
	app.registerComponent("gui_image", "GUI/Image");
	app.registerComponent("gui_rect", "GUI/Rect");
	app.registerComponent("gui_text", "GUI/Text");

	IAllocator& allocator = app.getWorldEditor().getAllocator();
	app.addPlugin(*LUMIX_NEW(allocator, GUIEditor)(app));
	
	app.getAssetBrowser().addPlugin(*LUMIX_NEW(allocator, SpritePlugin)(app));
}
