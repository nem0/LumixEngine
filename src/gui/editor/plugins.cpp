#include "editor/asset_browser.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
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

		static const char* TYPES_STR[] = { "9 patch", "Simple" };
		if (ImGui::BeginCombo("Type", TYPES_STR[sprite->type]))
		{
			if (ImGui::Selectable("9 patch")) sprite->type = Sprite::Type::PATCH9;
			if (ImGui::Selectable("Simple")) sprite->type = Sprite::Type::SIMPLE;
			ImGui::EndCombo();
		}
		switch (sprite->type)
		{
			case Sprite::Type::PATCH9:
				ImGui::InputInt("Top", &sprite->top);
				ImGui::InputInt("Right", &sprite->right);
				ImGui::InputInt("Bottom", &sprite->bottom);
				ImGui::InputInt("Left", &sprite->left);
				patch9edit(sprite);
				break;
			case Sprite::Type::SIMPLE: break;
			default: ASSERT(false); break;
		}


		if (ImGui::Button("Save")) saveSprite(sprite);

		return true;
	}



	void patch9edit(Sprite* sprite)
	{
		Texture* texture = sprite->getTexture();

		if (sprite->type != Sprite::Type::PATCH9 || !texture || !texture->isReady()) return;
		ImVec2 size;
		size.x = Math::minimum(ImGui::GetContentRegionAvailWidth(), texture->width * 2.0f);
		size.y = size.x / texture->width * texture->height;
		float scale = size.x / texture->width;
		ImGui::Dummy(size);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 a = ImGui::GetItemRectMin();
		ImVec2 b = ImGui::GetItemRectMax();
		draw->AddImage(&texture->handle, a, b);

		auto drawHandle = [&](const char* id, const ImVec2& a, const ImVec2& b, int* value, bool vertical) {
			const float SIZE = 5;
			ImVec2 rect_pos((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
			if (vertical)
			{
				rect_pos.x = a.x + (sprite->left + sprite->right) * 0.5f * scale;
			}
			else
			{
				rect_pos.y = a.y + (sprite->top + sprite->bottom) * 0.5f * scale;
			}

			ImGui::SetCursorScreenPos({ rect_pos.x - SIZE, rect_pos.y - SIZE });
			ImGui::InvisibleButton(id, { SIZE * 2, SIZE * 2 });
			bool changed = false;
			if (ImGui::IsItemActive())
			{
				static int start_drag_value;
				if (ImGui::IsMouseDragging())
				{
					ImVec2 drag = ImGui::GetMouseDragDelta();
					if (vertical)
					{
						*value = int(start_drag_value + drag.y / scale);
					}
					else
					{
						*value = int(start_drag_value + drag.x / scale);
					}
				}
				else if (ImGui::IsMouseClicked(0))
				{
					start_drag_value = *value;
				}
				changed = true;
			}


			bool is_hovered = ImGui::IsItemHovered();
			draw->AddLine(a, b, 0xffff00ff);
			draw->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), is_hovered ? 0xffffffff : 0x77ffFFff);
			draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 0xff777777);

			return changed;
		};

		ImVec2 cp = ImGui::GetCursorScreenPos();
		drawHandle("left", { a.x + sprite->left * scale, a.y }, { a.x + sprite->left * scale, b.y }, &sprite->left, false);
		drawHandle("right", { a.x + sprite->right * scale, a.y }, { a.x + sprite->right * scale, b.y }, &sprite->right, false);
		drawHandle("top", { a.x, a.y + sprite->top * scale }, { b.x, a.y + sprite->top * scale }, &sprite->top, true);
		drawHandle("bottom", { a.x, a.y + sprite->bottom * scale }, { b.x, a.y + sprite->bottom * scale }, &sprite->bottom, true);
		ImGui::SetCursorScreenPos(cp);
	}


	void saveSprite(Sprite* sprite)
	{
		FS::FileSystem& fs = app.getWorldEditor().getEngine().getFileSystem();
		// use temporary because otherwise the material is reloaded during saving
		StaticString<MAX_PATH_LENGTH> tmp_path(sprite->getPath().c_str(), ".tmp");
		FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(tmp_path), FS::Mode::CREATE_AND_WRITE);
		if (!file)
		{
			g_log_error.log("Editor") << "Could not save file " << sprite->getPath().c_str();
			return;
		}

		IAllocator& allocator = app.getWorldEditor().getAllocator();
		JsonSerializer serializer(*file, sprite->getPath());
		if (!sprite->save(serializer))
		{
			g_log_error.log("Editor") << "Could not save file " << sprite->getPath().c_str();
			fs.close(*file);
			return;
		}
		fs.close(*file);

		Engine& engine = app.getWorldEditor().getEngine();
		StaticString<MAX_PATH_LENGTH> src_full_path;
		StaticString<MAX_PATH_LENGTH> dest_full_path;
		if (engine.getPatchFileDevice())
		{
			src_full_path << engine.getPatchFileDevice()->getBasePath() << tmp_path;
			dest_full_path << engine.getPatchFileDevice()->getBasePath() << sprite->getPath().c_str();
		}
		if (!engine.getPatchFileDevice() || !PlatformInterface::fileExists(src_full_path))
		{
			src_full_path.data[0] = 0;
			dest_full_path.data[0] = 0;
			src_full_path << engine.getDiskFileDevice()->getBasePath() << tmp_path;
			dest_full_path << engine.getDiskFileDevice()->getBasePath() << sprite->getPath().c_str();
		}

		PlatformInterface::deleteFile(dest_full_path);

		if (!PlatformInterface::moveFile(src_full_path, dest_full_path))
		{
			g_log_error.log("Editor") << "Could not save file " << sprite->getPath().c_str();
		}
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
	CENTER_HORIZONTAL = 1 << 4,
	CENTER_VERTICAL = 1 << 5,
	ALL = LEFT | RIGHT | TOP | BOTTOM,
	HORIZONTAL = LEFT | RIGHT,
	VERTICAL = TOP | BOTTOM
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
			draw.AddRect(pos - Vec2(SIZE, SIZE), pos + Vec2(SIZE, SIZE), 0xff777777);

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



	struct CopyPositionBufferItem
	{
		const Reflection::PropertyBase* prop = nullptr;
		float value;

		void set(GUIScene* scene, Entity e, const char* prop_name)
		{
			prop = Reflection::getProperty(GUI_RECT_TYPE, prop_name);
			OutputBlob blob(&value, sizeof(value));
			prop->getValue({ e, GUI_RECT_TYPE, scene }, -1, blob);
		}
	} m_copy_position_buffer[8];
	
	int m_copy_position_buffer_count = 0;

	void copy(Entity e, u8 mask)
	{
		GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));
		m_copy_position_buffer_count = 0;

		if (mask & (u8)EdgeMask::TOP)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(scene, e, "Top Points");
			m_copy_position_buffer[m_copy_position_buffer_count+1].set(scene, e, "Top Relative");
			m_copy_position_buffer_count += 2;
		}

		if (mask & (u8)EdgeMask::BOTTOM)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(scene, e, "Bottom Points");
			m_copy_position_buffer[m_copy_position_buffer_count + 1].set(scene, e, "Bottom Relative");
			m_copy_position_buffer_count += 2;
		}

		if (mask & (u8)EdgeMask::LEFT)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(scene, e, "Left Points");
			m_copy_position_buffer[m_copy_position_buffer_count + 1].set(scene, e, "Left Relative");
			m_copy_position_buffer_count += 2;
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(scene, e, "Right Points");
			m_copy_position_buffer[m_copy_position_buffer_count + 1].set(scene, e, "Right Relative");
			m_copy_position_buffer_count += 2;
		}
	}


	void paste(Entity e)
	{
		m_editor->beginCommandGroup(crc32("gui_editor_paste"));
		GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));
		for (int i = 0; i < m_copy_position_buffer_count; ++i)
		{
			CopyPositionBufferItem& item = m_copy_position_buffer[i];
			m_editor->setProperty(GUI_RECT_TYPE, -1, *item.prop, &e, 1, &item.value, sizeof(item.value));
		}
		m_editor->endCommandGroup();
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

			m_pipeline->resize(int(size.x), int(size.y));
			m_pipeline->render();
			m_texture_handle = m_pipeline->getRenderbuffer("default", 0);
			ImGui::Image(&m_texture_handle, size);

			if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && m_mouse_mode == MouseMode::NONE)
			{
				Entity e = scene->getRectAt(toLumix(mouse_canvas_pos), toLumix(size));
				if (e.isValid()) m_editor->selectEntities(&e, 1);
			}

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
				if (ImGui::BeginMenu("Expand"))
				{
					if (ImGui::MenuItem("All")) expand(e, (u8)EdgeMask::ALL);
					if (ImGui::MenuItem("Top")) expand(e, (u8)EdgeMask::TOP);
					if (ImGui::MenuItem("Right")) expand(e, (u8)EdgeMask::RIGHT);
					if (ImGui::MenuItem("Bottom")) expand(e, (u8)EdgeMask::BOTTOM);
					if (ImGui::MenuItem("Left")) expand(e, (u8)EdgeMask::LEFT);
					if (ImGui::MenuItem("Horizontal")) expand(e, (u8)EdgeMask::HORIZONTAL);
					if (ImGui::MenuItem("Vertical")) expand(e, (u8)EdgeMask::VERTICAL);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Align"))
				{
					if (ImGui::MenuItem("Top")) align(e, (u8)EdgeMask::TOP);
					if (ImGui::MenuItem("Right")) align(e, (u8)EdgeMask::RIGHT);
					if (ImGui::MenuItem("Bottom")) align(e, (u8)EdgeMask::BOTTOM);
					if (ImGui::MenuItem("Left")) align(e, (u8)EdgeMask::LEFT);
					if (ImGui::MenuItem("Center horizontal")) align(e, (u8)EdgeMask::CENTER_HORIZONTAL);
					if (ImGui::MenuItem("Center vertical")) align(e, (u8)EdgeMask::CENTER_VERTICAL);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Copy position"))
				{
					if (ImGui::MenuItem("All")) copy(e, (u8)EdgeMask::ALL);
					if (ImGui::MenuItem("Top")) copy(e, (u8)EdgeMask::TOP);
					if (ImGui::MenuItem("Right")) copy(e, (u8)EdgeMask::RIGHT);
					if (ImGui::MenuItem("Bottom")) copy(e, (u8)EdgeMask::BOTTOM);
					if (ImGui::MenuItem("Left")) copy(e, (u8)EdgeMask::LEFT);
					if (ImGui::MenuItem("Horizontal")) copy(e, (u8)EdgeMask::HORIZONTAL);
					if (ImGui::MenuItem("Vertical")) copy(e, (u8)EdgeMask::VERTICAL);
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Paste")) paste(e);

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


	void align(Entity entity, u8 mask)
	{
		GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));

		m_editor->beginCommandGroup(crc32("align_gui_rect"));

		float br = scene->getRectBottomRelative(entity);
		float bp = scene->getRectBottomPoints(entity);
		float tr = scene->getRectTopRelative(entity);
		float tp = scene->getRectTopPoints(entity);
		float rr = scene->getRectRightRelative(entity);
		float rp = scene->getRectRightPoints(entity);
		float lr = scene->getRectLeftRelative(entity);
		float lp = scene->getRectLeftPoints(entity);

		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Bottom Relative", br - tr);
			setRectProperty(entity, "Bottom Points", bp - tp);
			setRectProperty(entity, "Top Relative", 0);
			setRectProperty(entity, "Top Points", 0);
		}

		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Right Relative", rr - lr);
			setRectProperty(entity, "Right Points", rp - lp);
			setRectProperty(entity, "Left Relative", 0);
			setRectProperty(entity, "Left Points", 0);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Left Relative", lr + 1 - rr);
			setRectProperty(entity, "Left Points", lp - rp);
			setRectProperty(entity, "Right Relative", 1);
			setRectProperty(entity, "Right Points", 0);
		}

		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Top Relative", tr + 1 - br);
			setRectProperty(entity, "Top Points", tp - bp);
			setRectProperty(entity, "Bottom Relative", 1);
			setRectProperty(entity, "Bottom Points", 0);
		}

		if (mask & (u8)EdgeMask::CENTER_VERTICAL)
		{
			setRectProperty(entity, "Top Relative", 0.5f - (br - tr) * 0.5f);
			setRectProperty(entity, "Top Points", -(bp - tp) * 0.5f);
			setRectProperty(entity, "Bottom Relative", 0.5f + (br - tr) * 0.5f);
			setRectProperty(entity, "Bottom Points", (bp - tp) * 0.5f);
		}

		if (mask & (u8)EdgeMask::CENTER_HORIZONTAL)
		{
			setRectProperty(entity, "Left Relative", 0.5f - (rr - lr) * 0.5f);
			setRectProperty(entity, "Left Points", -(rp - lp) * 0.5f);
			setRectProperty(entity, "Right Relative", 0.5f + (rr - lr) * 0.5f);
			setRectProperty(entity, "Right Points", (rp - lp) * 0.5f);
		}

		m_editor->endCommandGroup();
	}

	void expand(Entity entity, u8 mask)
	{
		GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));
		m_editor->beginCommandGroup(crc32("expand_gui_rect"));

		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Top Points", 0);
			setRectProperty(entity, "Top Relative", 0);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Right Points", 0);
			setRectProperty(entity, "Right Relative", 1);
		}


		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Left Points", 0);
			setRectProperty(entity, "Left Relative", 0);
		}

		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Bottom Points", 0);
			setRectProperty(entity, "Bottom Relative", 1);
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
	app.registerComponent("gui_input_field", "GUI/Input field");
	app.registerComponent("gui_rect", "GUI/Rect");
	app.registerComponent("gui_text", "GUI/Text");

	IAllocator& allocator = app.getWorldEditor().getAllocator();
	app.addPlugin(*LUMIX_NEW(allocator, GUIEditor)(app));
	
	app.getAssetBrowser().addPlugin(*LUMIX_NEW(allocator, SpritePlugin)(app));
}
