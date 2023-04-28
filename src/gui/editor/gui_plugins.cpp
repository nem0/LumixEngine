#include <imgui/imgui.h>

#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_scene.h"
#include "gui/sprite.h"
#include "renderer/draw2d.h"
#include "renderer/gpu/gpu.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"

using namespace Lumix;


namespace
{


static const ComponentType GUI_RECT_TYPE = reflection::getComponentType("gui_rect");
static const ComponentType GUI_IMAGE_TYPE = reflection::getComponentType("gui_image");
static const ComponentType GUI_TEXT_TYPE = reflection::getComponentType("gui_text");
static const ComponentType GUI_BUTTON_TYPE = reflection::getComponentType("gui_button");
static const ComponentType GUI_RENDER_TARGET_TYPE = reflection::getComponentType("gui_render_target");


struct SpritePlugin final : AssetBrowser::Plugin, AssetCompiler::IPlugin
{
	SpritePlugin(StudioApp& app) 
		: m_app(app)
		, AssetBrowser::Plugin(app.getAllocator())
	{
		m_app.getAssetCompiler().registerExtension("spr", Sprite::TYPE);
	}

	bool compile(const Path& src) override {
		return m_app.getAssetCompiler().copyCompile(src);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "spr"; }

	bool createResource(const char* path) override {
		os::OutputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.open(path, file)) {
			logError("Failed to create ", path);
			return false;
		}

		file << "type \"simple\"";
		file.close();
		return true;
	}
	
	void deserialize(InputMemoryStream& blob) override { 
		((Sprite*)m_current_resources[0])->load(blob.size(), (const u8*)blob.getData());
	}
	
	void serialize(OutputMemoryStream& blob) override {
		((Sprite*)m_current_resources[0])->serialize(blob);
	}

	bool onGUI(Span<Resource*> resources) override
	{
		m_current_resources = resources;
		if (resources.length() > 1) return false;

		Sprite* sprite = (Sprite*)resources[0];
		if (!sprite->isReady()) return false;
		
		if (ImGui::Button(ICON_FA_SAVE "Save")) saveSprite(*sprite);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "Open externally")) m_app.getAssetBrowser().openInExternalEditor(sprite);

		bool changed = false;
		char tmp[LUMIX_MAX_PATH];
		Texture* tex = sprite->getTexture();
		copyString(tmp, tex ? tex->getPath().c_str() : "");
		ImGuiEx::Label("Texture");
		if (m_app.getAssetBrowser().resourceInput("texture", Span(tmp), Texture::TYPE))
		{
			sprite->setTexture(Path(tmp));
			changed = true;
		}

		static const char* TYPES_STR[] = { "9 patch", "Simple" };
		ImGuiEx::Label("type");
		if (ImGui::BeginCombo("##type", TYPES_STR[sprite->type]))
		{
			if (ImGui::Selectable("9 patch")) {
				changed = true;
				sprite->type = Sprite::Type::PATCH9;
			}
			if (ImGui::Selectable("Simple")) {
				changed = true;
				sprite->type = Sprite::Type::SIMPLE;
			}
			ImGui::EndCombo();
		}
		switch (sprite->type) {
			case Sprite::Type::PATCH9:
				ImGuiEx::Label("Top");
				changed = ImGui::InputInt("##top", &sprite->top) || changed;
				ImGuiEx::Label("Right");
				changed = ImGui::InputInt("##right", &sprite->right) || changed;
				ImGuiEx::Label("Bottom");
				changed = ImGui::InputInt("##bottom", &sprite->bottom) || changed;
				ImGuiEx::Label("Left");
				changed = ImGui::InputInt("##left", &sprite->left) || changed;
				changed = patch9edit(sprite) || changed;
				break;
			case Sprite::Type::SIMPLE: break;
		}
		return changed;
	}



	bool patch9edit(Sprite* sprite)
	{
		Texture* texture = sprite->getTexture();

		if (sprite->type != Sprite::Type::PATCH9 || !texture || !texture->isReady()) return false;
		ImVec2 size;
		size.x = minimum(ImGui::GetContentRegionAvail().x, texture->width * 2.0f);
		size.y = size.x / texture->width * texture->height;
		float scale = size.x / texture->width;
		const float SIZE = 5;
		ImGui::Dummy(size + ImVec2(4 * SIZE, 4 * SIZE));

		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 a = ImGui::GetItemRectMin() + ImVec2(2 * SIZE, 2 * SIZE);
		ImVec2 b = ImGui::GetItemRectMax() - ImVec2(2 * SIZE, 2 * SIZE);
		draw->AddImage(texture->handle, a, b);

		auto drawHandle = [&](const char* id, const ImVec2& a, const ImVec2& b, int* value, bool vertical) {
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
				if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
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
		bool changed = drawHandle("left", { a.x + sprite->left * scale, a.y }, { a.x + sprite->left * scale, b.y }, &sprite->left, false);
		changed = drawHandle("right", { a.x + sprite->right * scale, a.y }, { a.x + sprite->right * scale, b.y }, &sprite->right, false) || changed;
		changed = drawHandle("top", { a.x, a.y + sprite->top * scale }, { b.x, a.y + sprite->top * scale }, &sprite->top, true) || changed;
		changed = drawHandle("bottom", { a.x, a.y + sprite->bottom * scale }, { b.x, a.y + sprite->bottom * scale }, &sprite->bottom, true) || changed;
		ImGui::SetCursorScreenPos(cp);
		return changed;
	}


	void saveSprite(Sprite& sprite)
	{
		OutputMemoryStream blob(m_app.getAllocator());
		sprite.serialize(blob);
		m_app.getAssetBrowser().saveResource(sprite, blob);
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Sprite"; }
	ResourceType getResourceType() const override { return Sprite::TYPE; }


	StudioApp& m_app;
	Span<Resource*> m_current_resources;
};


struct GUIEditor final : StudioApp::GUIPlugin
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
		: m_app(app)
	{
		m_toggle_ui.init("GUI Editor", "Toggle gui editor", "gui_editor", "", true);
		m_toggle_ui.func.bind<&GUIEditor::onAction>(this);
		m_toggle_ui.is_selected.bind<&GUIEditor::isOpen>(this);
		app.addWindowAction(&m_toggle_ui);
	}

	void init() {
		Engine& engine = m_app.getEngine();
		Renderer& renderer = *static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/gui_editor.pln"));
		m_pipeline = Pipeline::create(renderer, pres, "", m_app.getAllocator());
	}


	~GUIEditor() {
		m_app.removeAction(&m_toggle_ui);
	}


private:
	enum class MouseMode {
		NONE,
		RESIZE,
		MOVE
	};

	void onSettingsLoaded() override {
		m_is_window_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_gui_editor_open", false);
	}

	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue(Settings::GLOBAL, "is_gui_editor_open", m_is_window_open);
	}

	void onAction() { m_is_window_open = !m_is_window_open; }
	bool isOpen() const { return m_is_window_open; }


	MouseMode drawGizmo(Draw2D& draw, GUIModule& module, const Vec2& canvas_size, const ImVec2& mouse_canvas_pos, Span<const EntityRef> selected_entities)
	{
		if (selected_entities.length() != 1) return MouseMode::NONE;

		EntityRef e = selected_entities[0];
		if (!module.hasGUI(e)) return MouseMode::NONE;

		const EntityPtr parent = module.getWorld().getParent(e);
		const GUIModule::Rect rect = module.getRectEx(e, canvas_size);
		GUIModule::Rect parent_rect = module.getRectEx(parent, canvas_size);

		const float br = module.getRectBottomRelative(e);
		const float tr = module.getRectTopRelative(e);
		const float lr = module.getRectLeftRelative(e);
		const float rr = module.getRectRightRelative(e);

		const Vec2 bottom_right = { rect.x + rect.w, rect.y + rect.h };
		draw.addRect({ rect.x, rect.y }, bottom_right, Color::BLACK, 1);
		draw.addRect({ rect.x - 1, rect.y - 1 }, bottom_right + Vec2(1, 1), Color::WHITE, 1);
		const Vec2 mid = { rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f };

		auto drawAnchor = [&draw](const Vec2& pos, bool top, bool left){
			const float SIZE = 10;
			const Vec2 h = left ? Vec2(-SIZE, 0) : Vec2(SIZE, 0);
			const Vec2 v = top ? Vec2(0, -SIZE) : Vec2(0, SIZE);
			draw.addLine(pos, pos + v, Color::RED, 1);
			draw.addLine(pos + h, pos + v, Color::RED, 1);
			draw.addLine(pos + h, pos, Color::RED, 1);
		};

		drawAnchor(Vec2(parent_rect.x + parent_rect.w * lr, parent_rect.y + parent_rect.h * tr), true, true);
		drawAnchor(Vec2(parent_rect.x + parent_rect.w * lr, parent_rect.y + parent_rect.h * br), false, true);
		drawAnchor(Vec2(parent_rect.x + parent_rect.w * rr, parent_rect.y + parent_rect.h * br), false, false);
		drawAnchor(Vec2(parent_rect.x + parent_rect.w * rr, parent_rect.y + parent_rect.h * tr), true, false);

		auto drawHandle = [&](const Vec2& pos, const ImVec2& mouse_pos) {
			const float SIZE = 5;
			float dx = pos.x - mouse_pos.x;
			float dy = pos.y - mouse_pos.y;
			bool is_hovered = fabsf(dx) < SIZE && fabsf(dy) < SIZE;
			
			draw.addRectFilled(pos - Vec2(SIZE, SIZE), pos + Vec2(SIZE, SIZE), is_hovered ? Color::WHITE : Color{0xff, 0xff, 0xff, 0x77});
			draw.addRect(pos - Vec2(SIZE, SIZE), pos + Vec2(SIZE, SIZE), Color::BLACK, 1);

			return is_hovered && ImGui::IsMouseClicked(0);
		};

		MouseMode ret = MouseMode::NONE;
		if (drawHandle(bottom_right, mouse_canvas_pos))
		{
			m_bottom_right_start_transform.x = module.getRectRightPoints(e);
			m_bottom_right_start_transform.y = module.getRectBottomPoints(e);
			ret = MouseMode::RESIZE;
		}
		if (drawHandle(mid, mouse_canvas_pos))
		{
			m_bottom_right_start_transform.x = module.getRectRightPoints(e);
			m_bottom_right_start_transform.y = module.getRectBottomPoints(e);
			m_top_left_start_move.y = module.getRectTopPoints(e);
			m_top_left_start_move.x = module.getRectLeftPoints(e);
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
		const char* prop;
		float value;

		void set(GUIModule* module, EntityRef e, const char* prop_name)
		{
			const bool found = reflection::getPropertyValue(*module, e, GUI_RECT_TYPE, prop_name, value);
			ASSERT(found);
			prop = prop_name;
		}
	} m_copy_position_buffer[8];
	
	int m_copy_position_buffer_count = 0;

	void copy(EntityRef e, u8 mask, WorldEditor& editor)
	{
		GUIModule* module = (GUIModule*)editor.getWorld()->getModule("gui");
		m_copy_position_buffer_count = 0;

		if (mask & (u8)EdgeMask::TOP)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(module, e, "Top Points");
			m_copy_position_buffer[m_copy_position_buffer_count+1].set(module, e, "Top Relative");
			m_copy_position_buffer_count += 2;
		}

		if (mask & (u8)EdgeMask::BOTTOM)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(module, e, "Bottom Points");
			m_copy_position_buffer[m_copy_position_buffer_count + 1].set(module, e, "Bottom Relative");
			m_copy_position_buffer_count += 2;
		}

		if (mask & (u8)EdgeMask::LEFT)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(module, e, "Left Points");
			m_copy_position_buffer[m_copy_position_buffer_count + 1].set(module, e, "Left Relative");
			m_copy_position_buffer_count += 2;
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			m_copy_position_buffer[m_copy_position_buffer_count].set(module, e, "Right Points");
			m_copy_position_buffer[m_copy_position_buffer_count + 1].set(module, e, "Right Relative");
			m_copy_position_buffer_count += 2;
		}
	}


	void paste(EntityRef e, WorldEditor& editor)
	{
		editor.beginCommandGroup("gui_editor_paste");
		for (int i = 0; i < m_copy_position_buffer_count; ++i)
		{
			CopyPositionBufferItem& item = m_copy_position_buffer[i];
			editor.setProperty(GUI_RECT_TYPE, "", -1, item.prop, Span(&e, 1), item.value);
		}
		editor.endCommandGroup();
	}


	void onWindowGUI() override
	{
		if (!m_is_window_open) return;
		if (ImGui::Begin("GUIEditor", &m_is_window_open))
		{
			WorldEditor& editor = m_app.getWorldEditor();
			ImVec2 mouse_canvas_pos = ImGui::GetMousePos();
			mouse_canvas_pos.x -= ImGui::GetCursorScreenPos().x;
			mouse_canvas_pos.y -= ImGui::GetCursorScreenPos().y;
			
			ImVec2 size = ImGui::GetContentRegionAvail();
			if (!m_pipeline->isReady() || size.x == 0 || size.y == 0) {
				ImGui::End();
				return;
			}

			m_pipeline->setWorld(editor.getWorld());

			GUIModule* module = (GUIModule*)editor.getWorld()->getModule("gui");
			module->render(*m_pipeline, { size.x, size.y }, false);
			
			MouseMode new_mode = drawGizmo(m_pipeline->getDraw2D(), *module, { size.x, size.y }, mouse_canvas_pos, editor.getSelectedEntities());
			if (m_mouse_mode == MouseMode::NONE) m_mouse_mode = new_mode;
			if (ImGui::IsMouseReleased(0)) m_mouse_mode = MouseMode::NONE;
			
			if (editor.getSelectedEntities().size() == 1)
			{
				EntityRef e = editor.getSelectedEntities()[0];
				switch (m_mouse_mode)
				{
					case MouseMode::NONE: break;
					case MouseMode::RESIZE:
					{
						editor.beginCommandGroup("gui_mouse_resize");
						float b = m_bottom_right_start_transform.y + ImGui::GetMouseDragDelta(0).y;
						setRectProperty(e, "Bottom Points", b, editor);
						float r = m_bottom_right_start_transform.x + ImGui::GetMouseDragDelta(0).x;
						setRectProperty(e, "Right Points", r, editor);
						editor.endCommandGroup();
					}
					break;
					case MouseMode::MOVE:
					{
						editor.beginCommandGroup("gui_mouse_move");
						float b = m_bottom_right_start_transform.y + ImGui::GetMouseDragDelta(0).y;
						setRectProperty(e, "Bottom Points", b, editor);
						float r = m_bottom_right_start_transform.x + ImGui::GetMouseDragDelta(0).x;
						setRectProperty(e, "Right Points", r, editor);

						float t = m_top_left_start_move.y + ImGui::GetMouseDragDelta(0).y;
						setRectProperty(e, "Top Points", t, editor);
						float l = m_top_left_start_move.x + ImGui::GetMouseDragDelta(0).x;
						setRectProperty(e, "Left Points", l, editor);
						editor.endCommandGroup();
					}
					break;
				}
			}

			Viewport vp = {};
			vp.w = (int)size.x;
			vp.h = (int)size.y;
			m_pipeline->setViewport(vp);
			
			if (m_pipeline->render(true)) {
				m_texture_handle = m_pipeline->getOutput();

				if(m_texture_handle) {
					if (gpu::isOriginBottomLeft()) {
						ImGui::Image(m_texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
					}
					else {
						ImGui::Image(m_texture_handle, size);
					}
				}
			}

			if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && m_mouse_mode == MouseMode::NONE)
			{
				const Array<EntityRef>& selected = editor.getSelectedEntities();
				bool parent_selected = false;
				if (!selected.empty()) {
					const EntityPtr parent = editor.getWorld()->getParent(selected[0]);
					if (parent.isValid()) {
						const GUIModule::Rect rect = module->getRect(*parent);
						if (mouse_canvas_pos.x >= rect.x 
							&& mouse_canvas_pos.y >= rect.y 
							&& mouse_canvas_pos.x <= rect.x + rect.w
							&& mouse_canvas_pos.y <= rect.y + rect.h)
						{
							EntityRef e = *parent;
							editor.selectEntities(Span(&e, 1), false);
							parent_selected =  true;
						}
					}
				}

				if (!parent_selected) {
					EntityPtr e = module->getRectAtEx(toLumix(mouse_canvas_pos), toLumix(size), INVALID_ENTITY);
					if (e.isValid()) {
						EntityRef r = (EntityRef)e;
						editor.selectEntities(Span(&r, 1), false);
					}
				}
			}

			bool has_rect = false;
			if (editor.getSelectedEntities().size() == 1)
			{
				has_rect = editor.getWorld()->hasComponent(editor.getSelectedEntities()[0], GUI_RECT_TYPE);
			}
			if (has_rect && ImGui::BeginPopupContextItem("context"))
			{
				EntityRef e = editor.getSelectedEntities()[0];
				if (ImGui::BeginMenu("Create child"))
				{
					if (ImGui::MenuItem("Button")) createChild(e, GUI_BUTTON_TYPE, editor);
					if (ImGui::MenuItem("Image")) createChild(e, GUI_IMAGE_TYPE, editor);
					if (ImGui::MenuItem("Rect")) createChild(e, GUI_RECT_TYPE, editor);
					if (ImGui::MenuItem("Text")) createChild(e, GUI_TEXT_TYPE, editor);
					if (ImGui::MenuItem("Render target")) createChild(e, GUI_RENDER_TARGET_TYPE, editor);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Align"))
				{
					if (ImGui::MenuItem("Top")) align(e, (u8)EdgeMask::TOP, editor);
					if (ImGui::MenuItem("Right")) align(e, (u8)EdgeMask::RIGHT, editor);
					if (ImGui::MenuItem("Bottom")) align(e, (u8)EdgeMask::BOTTOM, editor);
					if (ImGui::MenuItem("Left")) align(e, (u8)EdgeMask::LEFT, editor);
					if (ImGui::MenuItem("Center horizontal")) align(e, (u8)EdgeMask::CENTER_HORIZONTAL, editor);
					if (ImGui::MenuItem("Center vertical")) align(e, (u8)EdgeMask::CENTER_VERTICAL, editor);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Expand"))
				{
					if (ImGui::MenuItem("All")) expand(e, (u8)EdgeMask::ALL, editor);
					if (ImGui::MenuItem("Top")) expand(e, (u8)EdgeMask::TOP, editor);
					if (ImGui::MenuItem("Right")) expand(e, (u8)EdgeMask::RIGHT, editor);
					if (ImGui::MenuItem("Bottom")) expand(e, (u8)EdgeMask::BOTTOM, editor);
					if (ImGui::MenuItem("Left")) expand(e, (u8)EdgeMask::LEFT, editor);
					if (ImGui::MenuItem("Horizontal")) expand(e, (u8)EdgeMask::HORIZONTAL, editor);
					if (ImGui::MenuItem("Vertical")) expand(e, (u8)EdgeMask::VERTICAL, editor);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Make relative"))
				{
					if (ImGui::MenuItem("All")) makeRelative(e, toLumix(size), (u8)EdgeMask::ALL, editor);
					if (ImGui::MenuItem("Top")) makeRelative(e, toLumix(size), (u8)EdgeMask::TOP, editor);
					if (ImGui::MenuItem("Right")) makeRelative(e, toLumix(size), (u8)EdgeMask::RIGHT, editor);
					if (ImGui::MenuItem("Bottom")) makeRelative(e, toLumix(size), (u8)EdgeMask::BOTTOM, editor);
					if (ImGui::MenuItem("Left")) makeRelative(e, toLumix(size), (u8)EdgeMask::LEFT, editor);
					
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Make absolute"))
				{
					if (ImGui::MenuItem("All")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::ALL, editor);
					if (ImGui::MenuItem("Top")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::TOP, editor);
					if (ImGui::MenuItem("Right")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::RIGHT, editor);
					if (ImGui::MenuItem("Bottom")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::BOTTOM, editor);
					if (ImGui::MenuItem("Left")) makeAbsolute(e, toLumix(size), (u8)EdgeMask::LEFT, editor);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Anchor")) {
					if (ImGui::MenuItem("Center")) anchor(e, (u8)EdgeMask::CENTER_HORIZONTAL |  (u8)EdgeMask::CENTER_VERTICAL, editor);
					if (ImGui::MenuItem("Left middle")) anchor(e, (u8)EdgeMask::LEFT |  (u8)EdgeMask::CENTER_VERTICAL, editor);
					if (ImGui::MenuItem("Right middle")) anchor(e, (u8)EdgeMask::RIGHT |  (u8)EdgeMask::CENTER_VERTICAL, editor);
					if (ImGui::MenuItem("Top center")) anchor(e, (u8)EdgeMask::TOP |  (u8)EdgeMask::CENTER_HORIZONTAL, editor);
					if (ImGui::MenuItem("Bottom center")) anchor(e, (u8)EdgeMask::BOTTOM |  (u8)EdgeMask::CENTER_HORIZONTAL, editor);
					if (ImGui::MenuItem("Top left")) anchor(e, (u8)EdgeMask::TOP |  (u8)EdgeMask::LEFT, editor);
					if (ImGui::MenuItem("Top right")) anchor(e, (u8)EdgeMask::TOP |  (u8)EdgeMask::RIGHT, editor);
					if (ImGui::MenuItem("Bottom left")) anchor(e, (u8)EdgeMask::BOTTOM |  (u8)EdgeMask::LEFT, editor);
					if (ImGui::MenuItem("Bottom right")) anchor(e, (u8)EdgeMask::BOTTOM |  (u8)EdgeMask::RIGHT, editor);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Copy position"))
				{
					if (ImGui::MenuItem("All")) copy(e, (u8)EdgeMask::ALL, editor);
					if (ImGui::MenuItem("Top")) copy(e, (u8)EdgeMask::TOP, editor);
					if (ImGui::MenuItem("Right")) copy(e, (u8)EdgeMask::RIGHT, editor);
					if (ImGui::MenuItem("Bottom")) copy(e, (u8)EdgeMask::BOTTOM, editor);
					if (ImGui::MenuItem("Left")) copy(e, (u8)EdgeMask::LEFT, editor);
					if (ImGui::MenuItem("Horizontal")) copy(e, (u8)EdgeMask::HORIZONTAL, editor);
					if (ImGui::MenuItem("Vertical")) copy(e, (u8)EdgeMask::VERTICAL, editor);
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Paste")) paste(e, editor);

				if (ImGui::BeginMenu("Layout")) {
					static int cols = 1;
					static int row_height = 20;
					static int row_spacing = 0;
					static int col_spacing = 0;
					ImGui::InputInt("Columns", &cols);
					ImGui::InputInt("Row height", &row_height);
					ImGui::InputInt("Row spacing", &row_spacing);
					ImGui::InputInt("Column spacing", &col_spacing);
					if (editor.getSelectedEntities().empty()) {
						ImGui::TextUnformatted("Please select an entity");
					}
					else {
						if (ImGui::Button("Do")) {
							layout(cols, row_height, row_spacing, col_spacing, editor);
						}
					}
					ImGui::EndMenu();
				}


				ImGui::EndPopup();
			}
		}

		ImGui::End();
	}


	void layout(u32 cols, u32 row_height, u32 row_spacing, u32 col_spacing, WorldEditor& editor) {
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		ASSERT(!selected.empty());
		ASSERT(cols > 0);
		const World& world = *editor.getWorld();
		const EntityRef e = selected[0];

		editor.beginCommandGroup("layout_gui");

		u32 y = 0;
		u32 col = 0;
		for (EntityRef ch : world.childrenOf(e)) {
			if (!world.hasComponent(ch, GUI_RECT_TYPE)) continue;

			setRectProperty(ch, "Top Points", (float)y, editor);
			setRectProperty(ch, "Bottom Points", (float)y + row_height, editor);

			const float l = col / (float)cols;
			const float r = (col + 1) / (float)cols;
			setRectProperty(ch, "Left Relative", l, editor);
			setRectProperty(ch, "Right Points", -(float)(col_spacing / 2), editor);
			setRectProperty(ch, "Left Points", (float)((col_spacing + 1) / 2), editor);
			setRectProperty(ch, "Right Relative", r, editor);

			++col;
			if (col == cols) {
				col = 0;
				y += row_height + row_spacing;
			}
		}

		editor.endCommandGroup();
	}


	void createChild(EntityRef entity, ComponentType child_type, WorldEditor& editor)
	{
		editor.beginCommandGroup("create_gui_rect_child");
		EntityRef child = editor.addEntity();
		editor.makeParent(entity, child);
		editor.selectEntities(Span(&child, 1), false);
		editor.addComponent(Span(&child, 1), GUI_RECT_TYPE);
		if (child_type != GUI_RECT_TYPE) {
			editor.addComponent(Span(&child, 1), child_type);
		}
		editor.endCommandGroup();
	}


	void setRectProperty(EntityRef e, const char* prop_name, float value, WorldEditor& editor)
	{
		editor.setProperty(GUI_RECT_TYPE, "", -1, prop_name, Span(&e, 1), value);
	}


	void makeAbsolute(EntityRef entity, const Vec2& canvas_size, u8 mask, WorldEditor& editor) {
		GUIModule* module = (GUIModule*)editor.getWorld()->getModule("gui");

		EntityRef parent = (EntityRef)module->getWorld().getParent(entity);
		GUIModule::Rect parent_rect = module->getRectEx(parent, canvas_size);
		GUIModule::Rect child_rect = module->getRectEx(entity, canvas_size);

		editor.beginCommandGroup("make_gui_rect_absolute");

		if (mask & (u8)EdgeMask::TOP) {
			setRectProperty(entity, "Top Relative", 0, editor);
			setRectProperty(entity, "Top Points", child_rect.y - parent_rect.y, editor);
		}
		
		if (mask & (u8)EdgeMask::LEFT) {
			setRectProperty(entity, "Left Relative", 0, editor);
			setRectProperty(entity, "Left Points", child_rect.x - parent_rect.x, editor);
		}

		if (mask & (u8)EdgeMask::RIGHT) {
			setRectProperty(entity, "Right Relative", 0, editor);
			setRectProperty(entity, "Right Points", child_rect.x + child_rect.w - parent_rect.x, editor);
		}
		
		if (mask & (u8)EdgeMask::BOTTOM) {
			setRectProperty(entity, "Bottom Relative", 0, editor);
			setRectProperty(entity, "Bottom Points", child_rect.y + child_rect.h - parent_rect.y, editor);
		}

		editor.endCommandGroup();
	}

	void anchor(EntityRef entity, u8 mask, WorldEditor& editor) {
		editor.beginCommandGroup("anchor_gui_rect");

		if (mask & (u8)EdgeMask::TOP) {
			setRectProperty(entity, "Bottom Relative", 0, editor);
			setRectProperty(entity, "Top Relative", 0, editor);
		}

		if (mask & (u8)EdgeMask::LEFT) {
			setRectProperty(entity, "Right Relative", 0, editor);
			setRectProperty(entity, "Left Relative", 0, editor);
		}

		if (mask & (u8)EdgeMask::RIGHT) {
			setRectProperty(entity, "Left Relative", 1, editor);
			setRectProperty(entity, "Right Relative", 1, editor);
		}

		if (mask & (u8)EdgeMask::BOTTOM) {
			setRectProperty(entity, "Top Relative", 1, editor);
			setRectProperty(entity, "Bottom Relative", 1, editor);
		}

		if (mask & (u8)EdgeMask::CENTER_VERTICAL) {
			setRectProperty(entity, "Top Relative", 0.5f, editor);
			setRectProperty(entity, "Bottom Relative", 0.5f, editor);
		}

		if (mask & (u8)EdgeMask::CENTER_HORIZONTAL) {
			setRectProperty(entity, "Left Relative", 0.5f, editor);
			setRectProperty(entity, "Right Relative", 0.5f, editor);
		}

		editor.endCommandGroup();
	}

	void align(EntityRef entity, u8 mask, WorldEditor& editor)
	{
		GUIModule* module = (GUIModule*)editor.getWorld()->getModule("gui");

		editor.beginCommandGroup("align_gui_rect");

		float br = module->getRectBottomRelative(entity);
		float bp = module->getRectBottomPoints(entity);
		float tr = module->getRectTopRelative(entity);
		float tp = module->getRectTopPoints(entity);
		float rr = module->getRectRightRelative(entity);
		float rp = module->getRectRightPoints(entity);
		float lr = module->getRectLeftRelative(entity);
		float lp = module->getRectLeftPoints(entity);

		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Bottom Relative", br - tr, editor);
			setRectProperty(entity, "Bottom Points", bp - tp, editor);
			setRectProperty(entity, "Top Relative", 0, editor);
			setRectProperty(entity, "Top Points", 0, editor);
		}

		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Right Relative", rr - lr, editor);
			setRectProperty(entity, "Right Points", rp - lp, editor);
			setRectProperty(entity, "Left Relative", 0, editor);
			setRectProperty(entity, "Left Points", 0, editor);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Left Relative", lr + 1 - rr, editor);
			setRectProperty(entity, "Left Points", lp - rp, editor);
			setRectProperty(entity, "Right Relative", 1, editor);
			setRectProperty(entity, "Right Points", 0, editor);
		}

		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Top Relative", tr + 1 - br, editor);
			setRectProperty(entity, "Top Points", tp - bp, editor);
			setRectProperty(entity, "Bottom Relative", 1, editor);
			setRectProperty(entity, "Bottom Points", 0, editor);
		}

		if (mask & (u8)EdgeMask::CENTER_VERTICAL)
		{
			setRectProperty(entity, "Top Relative", 0.5f - (br - tr) * 0.5f, editor);
			setRectProperty(entity, "Top Points", -(bp - tp) * 0.5f, editor);
			setRectProperty(entity, "Bottom Relative", 0.5f + (br - tr) * 0.5f, editor);
			setRectProperty(entity, "Bottom Points", (bp - tp) * 0.5f, editor);
		}

		if (mask & (u8)EdgeMask::CENTER_HORIZONTAL)
		{
			setRectProperty(entity, "Left Relative", 0.5f - (rr - lr) * 0.5f, editor);
			setRectProperty(entity, "Left Points", -(rp - lp) * 0.5f, editor);
			setRectProperty(entity, "Right Relative", 0.5f + (rr - lr) * 0.5f, editor);
			setRectProperty(entity, "Right Points", (rp - lp) * 0.5f, editor);
		}

		editor.endCommandGroup();
	}

	void expand(EntityRef entity, u8 mask, WorldEditor& editor)
	{
		editor.beginCommandGroup("expand_gui_rect");

		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Top Points", 0, editor);
			setRectProperty(entity, "Top Relative", 0, editor);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Right Points", 0, editor);
			setRectProperty(entity, "Right Relative", 1, editor);
		}


		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Left Points", 0, editor);
			setRectProperty(entity, "Left Relative", 0, editor);
		}

		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Bottom Points", 0, editor);
			setRectProperty(entity, "Bottom Relative", 1, editor);
		}

		editor.endCommandGroup();
	}


	void makeRelative(EntityRef entity, const Vec2& canvas_size, u8 mask, WorldEditor& editor)
	{
		World* world = editor.getWorld();
		GUIModule* module = (GUIModule*)world->getModule("gui");
		
		EntityPtr parent = world->getParent(entity);
		GUIModule::Rect parent_rect = module->getRectEx(parent, canvas_size);
		GUIModule::Rect child_rect = module->getRectEx(entity, canvas_size);

		editor.beginCommandGroup("make_gui_rect_relative");
		
		if (mask & (u8)EdgeMask::TOP)
		{
			setRectProperty(entity, "Top Points", 0, editor);
			setRectProperty(entity, "Top Relative", (child_rect.y - parent_rect.y) / parent_rect.h, editor);
		}

		if (mask & (u8)EdgeMask::RIGHT)
		{
			setRectProperty(entity, "Right Points", 0, editor);
			setRectProperty(entity, "Right Relative", (child_rect.x + child_rect.w - parent_rect.x) / parent_rect.w, editor);
		}

		
		if (mask & (u8)EdgeMask::LEFT)
		{
			setRectProperty(entity, "Left Points", 0, editor);
			setRectProperty(entity, "Left Relative", (child_rect.x - parent_rect.x) / parent_rect.w, editor);
		}
			
		if (mask & (u8)EdgeMask::BOTTOM)
		{
			setRectProperty(entity, "Bottom Points", 0, editor);
			setRectProperty(entity, "Bottom Relative", (child_rect.y + child_rect.h - parent_rect.y) / parent_rect.h, editor);
		}

		editor.endCommandGroup();
	}


	void update(float) override {}
	const char* getName() const override { return "gui_editor"; }


	StudioApp& m_app;
	Action m_toggle_ui;
	UniquePtr<Pipeline> m_pipeline;
	bool m_is_window_open = false;
	gpu::TextureHandle m_texture_handle;
	MouseMode m_mouse_mode = MouseMode::NONE;
	Vec2 m_bottom_right_start_transform;
	Vec2 m_top_left_start_move;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_sprite_plugin(app)
		, m_gui_editor(app)
	{
	}


	const char* getName() const override { return "gui"; }
	bool dependsOn(IPlugin& plugin) const override { return equalStrings(plugin.getName(), "renderer"); }


	void init() override {
		m_gui_editor.init();

		m_app.addPlugin(m_gui_editor);

		m_app.getAssetBrowser().addPlugin(m_sprite_plugin);

		const char* sprite_exts[] = {"spr", nullptr};
		m_app.getAssetCompiler().addPlugin(m_sprite_plugin, sprite_exts);
	}

	bool showGizmo(WorldView&, ComponentUID) override { return false; }
	
	~StudioAppPlugin() {
		m_app.removePlugin(m_gui_editor);

		m_app.getAssetCompiler().removePlugin(m_sprite_plugin);
		m_app.getAssetBrowser().removePlugin(m_sprite_plugin);
	}


	StudioApp& m_app;
	GUIEditor m_gui_editor;
	SpritePlugin m_sprite_plugin;
};



} // anonymous namespace


LUMIX_STUDIO_ENTRY(gui)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
