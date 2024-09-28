#include <imgui/imgui.h>

#include "core/crt.h"
#include "core/geometry.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/tokenizer.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_module.h"
#include "gui/sprite.h"
#include "renderer/draw2d.h"
#include "renderer/gpu/gpu.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"

using namespace Lumix;


namespace {

static const ComponentType GUI_CANVAS_TYPE = reflection::getComponentType("gui_canvas");
static const ComponentType GUI_RECT_TYPE = reflection::getComponentType("gui_rect");
static const ComponentType GUI_IMAGE_TYPE = reflection::getComponentType("gui_image");
static const ComponentType GUI_TEXT_TYPE = reflection::getComponentType("gui_text");
static const ComponentType GUI_BUTTON_TYPE = reflection::getComponentType("gui_button");
static const ComponentType GUI_RENDER_TARGET_TYPE = reflection::getComponentType("gui_render_target");

struct SpritePlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, m_app(app)
		{
			m_resource = app.getEngine().getResourceManager().load<Sprite>(path);
		}

		~EditorWindow() {
			m_resource->decRefCount();
		}

		static void serialize(Sprite& sprite, OutputMemoryStream& out) {
			ASSERT(sprite.isReady());
			out << "type = " << (sprite.type == Sprite::PATCH9 ? "patch9\n" : "simple\n");
			out << "top = " << sprite.top << "\n";
			out << "bottom = " << sprite.bottom << "\n";
			out << "left = " << sprite.left << "\n";
			out << "right = " << sprite.right << "\n";
			if (sprite.getTexture()) {
				out << "texture = \"/" << sprite.getTexture()->getPath() << "\"";
			} else {
				out << "texture = \"\"";
			}
		}

		void save() {
			OutputMemoryStream blob(m_app.getAllocator());
			serialize(*m_resource, blob);
			m_app.getAssetBrowser().saveResource(*m_resource, blob);
			m_dirty = false;
		}
		
		bool patch9edit(Sprite* sprite) {
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

		void windowGUI() override {
			if (m_app.checkShortcut(m_app.getCommonActions().save)) save();

			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(*m_resource);
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!m_resource->isReady()) return;
		
			Texture* tex = m_resource->getTexture();
			Path tmp = tex ? tex->getPath() : Path();
			ImGuiEx::Label("Texture");
			if (m_app.getAssetBrowser().resourceInput("texture", tmp, Texture::TYPE)) {
				m_resource->setTexture(tmp);
				m_dirty = true;
			}

			static const char* TYPES_STR[] = { "9 patch", "Simple" };
			ImGuiEx::Label("type");
			if (ImGui::BeginCombo("##type", TYPES_STR[m_resource->type]))
			{
				if (ImGui::Selectable("9 patch")) {
					m_dirty = true;
					m_resource->type = Sprite::Type::PATCH9;
				}
				if (ImGui::Selectable("Simple")) {
					m_dirty = true;
					m_resource->type = Sprite::Type::SIMPLE;
				}
				ImGui::EndCombo();
			}
			switch (m_resource->type) {
				case Sprite::Type::PATCH9:
					ImGuiEx::Label("Top");
					m_dirty = ImGui::InputInt("##top", &m_resource->top) || m_dirty;
					ImGuiEx::Label("Right");
					m_dirty = ImGui::InputInt("##right", &m_resource->right) || m_dirty;
					ImGuiEx::Label("Bottom");
					m_dirty = ImGui::InputInt("##bottom", &m_resource->bottom) || m_dirty;
					ImGuiEx::Label("Left");
					m_dirty = ImGui::InputInt("##left", &m_resource->left) || m_dirty;
					m_dirty = patch9edit(m_resource) || m_dirty;
					break;
				case Sprite::Type::SIMPLE: break;
			}
		}
	
		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "sprite editor"; }

		StudioApp& m_app;
		Sprite* m_resource;
	};

	SpritePlugin(StudioApp& app) 
		: m_app(app)
	{
		m_app.getAssetCompiler().registerExtension("spr", Sprite::TYPE);
	}

	bool compile(const Path& src) override {
		// load
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;

		// parse
		StringView type_str, texture_str;
		i32 top, bottom, left, right;
		const ParseItemDesc descs[] = {
			{"type", &type_str},
			{"top", &top},
			{"bottom", &bottom},
			{"left", &left},
			{"right", &right},
			{"texture", &texture_str}
		};
		StringView sv((const char*)src_data.data(), (u32)src_data.size());
		if (!parse(sv, src.c_str(), descs)) return false;

		// write compiled
		OutputMemoryStream compiled(m_app.getAllocator());
		Sprite::Header header;
		compiled.write(header);
		compiled.write(top);
		compiled.write(bottom);
		compiled.write(left);
		compiled.write(right);
		compiled.writeString(texture_str);
		compiled.write(equalIStrings(type_str, "patch9") ? Sprite::PATCH9 : Sprite::SIMPLE);
		return m_app.getAssetCompiler().writeCompiledResource(src, compiled);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "spr"; }
	void createResource(OutputMemoryStream& blob) override { blob << "type = simple"; }

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	const char* getLabel() const override { return "Sprite"; }

	StudioApp& m_app;
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
		m_toggle_ui.init("GUI Editor", "Toggle gui editor", "gui_editor", "");
		m_toggle_ui.func.bind<&GUIEditor::onToggleOpen>(this);
		m_toggle_ui.is_selected.bind<&GUIEditor::isOpen>(this);
		app.addWindowAction(&m_toggle_ui);

		m_hcenter_action.init("Center horizontally", "GUI editor - center horizontally", "guied_hcenter", "");
		m_vcenter_action.init("Center vertically", "GUI editor - center vertically", "guied_vcenter", "");
		m_hexpand_action.init("Expand horizontally", "GUI editor - expand horizontally", "guied_hexpand", "");
		m_vexpand_action.init("Expand vertically", "GUI editor - expand vertically", "guied_vexpand", "");
		m_make_rel_action.init("Make relative", "GUI editor - make relative", "guied_makerel", "");
		m_app.addAction(&m_hcenter_action);
		m_app.addAction(&m_vcenter_action);
		m_app.addAction(&m_hexpand_action);
		m_app.addAction(&m_vexpand_action);
		m_app.addAction(&m_make_rel_action);

		m_app.getSettings().registerPtr("gui_editor_open", &m_is_window_open);
	}

	void init() {
		Engine& engine = m_app.getEngine();
		Renderer& renderer = *static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
		m_pipeline = Pipeline::create(renderer, PipelineType::GUI_EDITOR);
	}


	~GUIEditor() {
		m_app.removeAction(&m_toggle_ui);
		m_app.removeAction(&m_hcenter_action);
		m_app.removeAction(&m_vcenter_action);
		m_app.removeAction(&m_hexpand_action);
		m_app.removeAction(&m_vexpand_action);
		m_app.removeAction(&m_make_rel_action);
	}


private:
	enum class ResizeSide {
		NONE = 0,

		N = 1 << 0,
		E = 1 << 1,
		S = 1 << 2,
		W = 1 << 3,
		
		NE = N | E,
		NW = N | W,
		SE = S | E,
		SW = S | W
	};

	enum class MouseMode {
		NONE,
		RESIZE,
		MOVE
	};


	void onToggleOpen() { m_is_window_open = !m_is_window_open; }
	bool isOpen() const { return m_is_window_open; }

	void handleDrop(const char* path, const ImVec2& drop_pos, const ImVec2& canvas_size) {
		if (!Path::hasExtension(path, "spr")) return;

		WorldEditor& editor = m_app.getWorldEditor();
		GUIModule* module = (GUIModule*)editor.getWorld()->getModule("gui");
		EntityPtr entity = module->getRectAtEx(drop_pos, canvas_size, INVALID_ENTITY);
		if (!entity) return;

		const GUIModule::Rect rect = module->getRectEx(*entity, canvas_size);

		editor.beginCommandGroup("gui_drop_sprite");
		EntityRef child = editor.addEntity();
		editor.makeParent(entity, child);
		editor.selectEntities(Span(&child, 1), false);
		editor.addComponent(Span(&child, 1), GUI_RECT_TYPE);
		editor.addComponent(Span(&child, 1), GUI_IMAGE_TYPE);
		editor.setProperty(GUI_IMAGE_TYPE, "", 0, "Sprite", Span(&child, 1), Path(path));
		
		Sprite* sprite = m_app.getEngine().getResourceManager().load<Sprite>(Path(path));
		Texture* texture = sprite->getTexture();
		if (sprite->isReady() && texture) {
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Top Relative", Span(&child, 1), 0.f);
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Bottom Relative", Span(&child, 1), 0.f);
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Left Relative", Span(&child, 1), 0.f);
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Right Relative", Span(&child, 1), 0.f);

			float w = (float)texture->width;
			float h = (float)texture->height;
			float x = drop_pos.x - rect.x - w / 2;
			float y = drop_pos.y - rect.y - h / 2;

			editor.setProperty(GUI_RECT_TYPE, "", 0, "Top Points", Span(&child, 1), y);
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Bottom Points", Span(&child, 1), y + h);
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Left Points", Span(&child, 1), x);
			editor.setProperty(GUI_RECT_TYPE, "", 0, "Right Points", Span(&child, 1), x + w);
		}
		sprite->decRefCount();

		editor.endCommandGroup();
	}

	MouseMode drawGizmo(Draw2D& draw, GUIModule& module, const Vec2& canvas_size, const ImVec2& mouse_canvas_pos, EntityRef e)
	{
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

			return is_hovered;
		};

		constexpr float RESIZE_EDGE_SIZE = 5;
		if (mouse_canvas_pos.x < rect.x - RESIZE_EDGE_SIZE) return MouseMode::NONE;
		if (mouse_canvas_pos.y < rect.y - RESIZE_EDGE_SIZE) return MouseMode::NONE;
		if (mouse_canvas_pos.x > bottom_right.x + RESIZE_EDGE_SIZE) return MouseMode::NONE;
		if (mouse_canvas_pos.y > bottom_right.y + RESIZE_EDGE_SIZE) return MouseMode::NONE;

		if (ImGui::IsMouseClicked(0)) {
			m_bottom_right_start_transform.x = module.getRectRightPoints(e);
			m_bottom_right_start_transform.y = module.getRectBottomPoints(e);
			m_top_left_start_transform.y = module.getRectTopPoints(e);
			m_top_left_start_transform.x = module.getRectLeftPoints(e);
		}

		if (m_mouse_mode == MouseMode::NONE) {
			m_resize_side = ResizeSide::NONE;
			if (mouse_canvas_pos.x < rect.x + RESIZE_EDGE_SIZE) {
				m_resize_side = ResizeSide::W;
			}
			if (mouse_canvas_pos.x > bottom_right.x - RESIZE_EDGE_SIZE) {
				m_resize_side |= ResizeSide::E;
			}
			if (mouse_canvas_pos.y < rect.y + RESIZE_EDGE_SIZE) {
				m_resize_side |= ResizeSide::N;
			}
			if (mouse_canvas_pos.y > bottom_right.y - RESIZE_EDGE_SIZE) {
				m_resize_side |= ResizeSide::S;
			}
			switch (m_resize_side) {
				case ResizeSide::W:
				case ResizeSide::E:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					break;
				case ResizeSide::N:
				case ResizeSide::S:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
					break;
				case ResizeSide::NE:
				case ResizeSide::SW:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
					break;
				case ResizeSide::NW:
				case ResizeSide::SE:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
					break;
				default: break;
			}
			if (m_resize_side != ResizeSide::NONE && ImGui::IsMouseClicked(0)) return MouseMode::RESIZE;
		}
		if (m_resize_side == ResizeSide::NONE && module.isOver(mouse_canvas_pos, e)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if (ImGui::IsMouseClicked(0)) return MouseMode::MOVE;
		return MouseMode::NONE;
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

	[[nodiscard]] bool menuActionItem(const Action& action, const char* label = nullptr) {
		char shortcut[64];
		getShortcut(action, Span(shortcut));
		return ImGui::MenuItem(label ? label : action.label_short.data, shortcut);
	}

	bool isInCanvas(EntityRef entity, EntityRef canvas) {
		WorldEditor& editor = m_app.getWorldEditor();
		World& world = *editor.getWorld();
		EntityPtr iter = entity;
		while (iter.isValid()) {
			if (iter == canvas) return true;
			iter = world.getParent(*iter);
		}
		return false;
	}

	void onGUI() override {
		if (!m_is_window_open) return;

		if (!ImGui::Begin("GUIEditor", &m_is_window_open)) {
			ImGui::End();
			return;
		}

		WorldEditor& editor = m_app.getWorldEditor();
		if (editor.getSelectedEntities().size() == 1) {
			const EntityRef e = editor.getSelectedEntities()[0];
			if (m_app.checkShortcut(m_hcenter_action)) align(e, (u8)EdgeMask::CENTER_HORIZONTAL, editor);
			else if (m_app.checkShortcut(m_vcenter_action)) align(e, (u8)EdgeMask::CENTER_VERTICAL, editor);
			else if (m_app.checkShortcut(m_hexpand_action)) expand(e, (u8)EdgeMask::HORIZONTAL, editor);
			else if (m_app.checkShortcut(m_vexpand_action)) expand(e, (u8)EdgeMask::VERTICAL, editor);
			else if (m_app.checkShortcut(m_make_rel_action)) makeRelative(e, m_canvas_size, (u8)EdgeMask::ALL, editor);
		}

		World& world = *editor.getWorld();
		if (m_canvas_entity.isValid() && (!world.hasEntity(*m_canvas_entity) || !world.hasComponent(*m_canvas_entity, GUI_CANVAS_TYPE))) {
			// new world or entity deleted or component deleted
			m_canvas_entity = INVALID_ENTITY;
		}

		m_pipeline->setWorld(&world);
		GUIModule* module = (GUIModule*)world.getModule("gui");
		HashMap<EntityRef, GUICanvas>& canvases = module->getCanvases();
		if (!m_canvas_entity.isValid() && canvases.size() > 0) {
			m_canvas_entity = canvases.begin().key();
		}

		if (canvases.size() > 1) {
			char entity_name[64] = "N/A";
			getEntityListDisplayName(m_app, world, Span(entity_name), m_canvas_entity, true);
			if (ImGui::BeginCombo("Canvas", entity_name)) {
				for (auto iter : canvases.iterated()) {
					getEntityListDisplayName(m_app, world, Span(entity_name), iter.key(), true);
					if (ImGui::Selectable(entity_name)) {
						m_canvas_entity = iter.key();
					}
				}
				ImGui::EndCombo();
			}
		}


		ImGui::ColorEdit3("Background", &m_clear_color.x);

		if (!m_canvas_entity.isValid()) {
			if (canvases.empty()) {
				ImGui::TextUnformatted("No canvases found.");
				if (ImGui::Button("Create canvas")) {
					editor.beginCommandGroup("create_gui_canvas");
					EntityRef e = editor.addEntity();
					editor.setEntityName(e, "GUI canvas");
					editor.addComponent(Span(&e, 1), GUI_CANVAS_TYPE);
					editor.addComponent(Span(&e, 1), GUI_RECT_TYPE);
					editor.endCommandGroup();
				}
			}
			ImGui::End();
			return;
		}
		
		const ImVec2 mouse_canvas_pos = ImGui::GetMousePos() - ImGui::GetCursorScreenPos();

		const ImVec2 size = ImGui::GetContentRegionAvail();
		m_canvas_size = size;
		if (size.x == 0 || size.y == 0) {
			ImGui::End();
			return;
		}

		module->renderCanvas(*m_pipeline, { size.x, size.y }, false, *m_canvas_entity);

		if (editor.getSelectedEntities().size() == 1) {
			EntityRef e = editor.getSelectedEntities()[0];
			if (isInCanvas(e, *m_canvas_entity)) {
				MouseMode new_mode = drawGizmo(m_pipeline->getDraw2D(), *module, { size.x, size.y }, mouse_canvas_pos, e);
				if (ImGui::IsWindowHovered() && m_mouse_mode == MouseMode::NONE) m_mouse_mode = new_mode;
			}
		}

		if (editor.getSelectedEntities().size() == 1) {
			EntityRef e = editor.getSelectedEntities()[0];
			switch (m_mouse_mode) {
				case MouseMode::NONE: break;
				case MouseMode::RESIZE: {
					editor.beginCommandGroup("gui_mouse_resize");
					if (isFlagSet(m_resize_side, ResizeSide::N)) {
						float b = m_top_left_start_transform.y + ImGui::GetMouseDragDelta(0).y;
						setRectProperty(e, "Top Points", b, editor);
					}
					if (isFlagSet(m_resize_side, ResizeSide::S)) {
						float b = m_bottom_right_start_transform.y + ImGui::GetMouseDragDelta(0).y;
						setRectProperty(e, "Bottom Points", b, editor);
					}
					if (isFlagSet(m_resize_side, ResizeSide::W)) {
						float b = m_top_left_start_transform.x + ImGui::GetMouseDragDelta(0).x;
						setRectProperty(e, "Left Points", b, editor);
					}
					if (isFlagSet(m_resize_side, ResizeSide::E)) {
						float b = m_bottom_right_start_transform.x + ImGui::GetMouseDragDelta(0).x;
						setRectProperty(e, "Right Points", b, editor);
					}
					editor.endCommandGroup();
				}
				break;
				case MouseMode::MOVE: {
					editor.beginCommandGroup("gui_mouse_move");
					float b = m_bottom_right_start_transform.y + ImGui::GetMouseDragDelta(0).y;
					setRectProperty(e, "Bottom Points", b, editor);
					float r = m_bottom_right_start_transform.x + ImGui::GetMouseDragDelta(0).x;
					setRectProperty(e, "Right Points", r, editor);

					float t = m_top_left_start_transform.y + ImGui::GetMouseDragDelta(0).y;
					setRectProperty(e, "Top Points", t, editor);
					float l = m_top_left_start_transform.x + ImGui::GetMouseDragDelta(0).x;
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
		m_pipeline->setClearColor(m_clear_color);	

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

			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload("path")) {
					handleDrop((const char*)payload->Data, mouse_canvas_pos, m_canvas_size);
				}
				ImGui::EndDragDropTarget();
			}
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && ImGui::IsItemHovered() && ImGui::GetMouseDragDelta().x == 0 && ImGui::GetMouseDragDelta().x == 0) {
			const Array<EntityRef>& selected = editor.getSelectedEntities();
			bool parent_selected = false;
			if (!selected.empty() && isInCanvas(selected[0], *m_canvas_entity)) {
				const EntityPtr parent = editor.getWorld()->getParent(selected[0]);
				if (module->isOver(mouse_canvas_pos, selected[0]) && parent.isValid()) {
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
				EntityPtr e = module->getRectAtEx(mouse_canvas_pos, size, INVALID_ENTITY, *m_canvas_entity);
				if (e.isValid()) {
					EntityRef r = (EntityRef)e;
					editor.selectEntities(Span(&r, 1), false);
				}
			}
		}
		
		if (ImGui::IsMouseReleased(0)) {
			m_mouse_mode = MouseMode::NONE;
		}

		bool has_rect = false;
		if (editor.getSelectedEntities().size() == 1) {
			has_rect = editor.getWorld()->hasComponent(editor.getSelectedEntities()[0], GUI_RECT_TYPE);
		}
		
		if (has_rect && ImGui::BeginPopupContextItem("context")) {
			entityContextMenu(editor.getSelectedEntities()[0], size);
			ImGui::EndPopup();
		}

		ImGui::End();
	}

	void entityContextMenu(EntityRef e, Vec2 canvas_size) {
		WorldEditor& editor = m_app.getWorldEditor();
		if (ImGui::BeginMenu("Create child")) {
			if (ImGui::MenuItem("Button + Image + Text")) createChildren(e, editor, GUI_BUTTON_TYPE, GUI_IMAGE_TYPE, GUI_TEXT_TYPE);
			if (ImGui::MenuItem("Button")) createChild(e, GUI_BUTTON_TYPE, editor);
			if (ImGui::MenuItem("Image")) createChild(e, GUI_IMAGE_TYPE, editor);
			if (ImGui::MenuItem("Rect")) createChild(e, GUI_RECT_TYPE, editor);
			if (ImGui::MenuItem("Text")) createChild(e, GUI_TEXT_TYPE, editor);
			if (ImGui::MenuItem("Render target")) createChild(e, GUI_RENDER_TARGET_TYPE, editor);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Align")) {
			if (ImGui::MenuItem("Top")) align(e, (u8)EdgeMask::TOP, editor);
			if (ImGui::MenuItem("Right")) align(e, (u8)EdgeMask::RIGHT, editor);
			if (ImGui::MenuItem("Bottom")) align(e, (u8)EdgeMask::BOTTOM, editor);
			if (ImGui::MenuItem("Left")) align(e, (u8)EdgeMask::LEFT, editor);

			if (menuActionItem(m_hcenter_action)) align(e, (u8)EdgeMask::CENTER_HORIZONTAL, editor);
			if (menuActionItem(m_vcenter_action)) align(e, (u8)EdgeMask::CENTER_VERTICAL, editor);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Expand")) {
			if (ImGui::MenuItem("All")) expand(e, (u8)EdgeMask::ALL, editor);
			if (ImGui::MenuItem("Top")) expand(e, (u8)EdgeMask::TOP, editor);
			if (ImGui::MenuItem("Right")) expand(e, (u8)EdgeMask::RIGHT, editor);
			if (ImGui::MenuItem("Bottom")) expand(e, (u8)EdgeMask::BOTTOM, editor);
			if (ImGui::MenuItem("Left")) expand(e, (u8)EdgeMask::LEFT, editor);
			if (menuActionItem(m_hexpand_action, "Horizontal")) expand(e, (u8)EdgeMask::HORIZONTAL, editor);
			if (menuActionItem(m_vexpand_action, "Vertical")) expand(e, (u8)EdgeMask::VERTICAL, editor);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Make relative")) {
			if (menuActionItem(m_make_rel_action, "All")) makeRelative(e, canvas_size, (u8)EdgeMask::ALL, editor);
			if (ImGui::MenuItem("Top")) makeRelative(e, canvas_size, (u8)EdgeMask::TOP, editor);
			if (ImGui::MenuItem("Right")) makeRelative(e, canvas_size, (u8)EdgeMask::RIGHT, editor);
			if (ImGui::MenuItem("Bottom")) makeRelative(e, canvas_size, (u8)EdgeMask::BOTTOM, editor);
			if (ImGui::MenuItem("Left")) makeRelative(e, canvas_size, (u8)EdgeMask::LEFT, editor);

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Make absolute")) {
			if (ImGui::MenuItem("All")) makeAbsolute(e, canvas_size, (u8)EdgeMask::ALL, editor);
			if (ImGui::MenuItem("Top")) makeAbsolute(e, canvas_size, (u8)EdgeMask::TOP, editor);
			if (ImGui::MenuItem("Right")) makeAbsolute(e, canvas_size, (u8)EdgeMask::RIGHT, editor);
			if (ImGui::MenuItem("Bottom")) makeAbsolute(e, canvas_size, (u8)EdgeMask::BOTTOM, editor);
			if (ImGui::MenuItem("Left")) makeAbsolute(e, canvas_size, (u8)EdgeMask::LEFT, editor);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Anchor")) {
			if (ImGui::MenuItem("Center")) anchor(e, (u8)EdgeMask::CENTER_HORIZONTAL | (u8)EdgeMask::CENTER_VERTICAL, editor);
			if (ImGui::MenuItem("Left middle")) anchor(e, (u8)EdgeMask::LEFT | (u8)EdgeMask::CENTER_VERTICAL, editor);
			if (ImGui::MenuItem("Right middle")) anchor(e, (u8)EdgeMask::RIGHT | (u8)EdgeMask::CENTER_VERTICAL, editor);
			if (ImGui::MenuItem("Top center")) anchor(e, (u8)EdgeMask::TOP | (u8)EdgeMask::CENTER_HORIZONTAL, editor);
			if (ImGui::MenuItem("Bottom center")) anchor(e, (u8)EdgeMask::BOTTOM | (u8)EdgeMask::CENTER_HORIZONTAL, editor);
			if (ImGui::MenuItem("Top left")) anchor(e, (u8)EdgeMask::TOP | (u8)EdgeMask::LEFT, editor);
			if (ImGui::MenuItem("Top right")) anchor(e, (u8)EdgeMask::TOP | (u8)EdgeMask::RIGHT, editor);
			if (ImGui::MenuItem("Bottom left")) anchor(e, (u8)EdgeMask::BOTTOM | (u8)EdgeMask::LEFT, editor);
			if (ImGui::MenuItem("Bottom right")) anchor(e, (u8)EdgeMask::BOTTOM | (u8)EdgeMask::RIGHT, editor);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Copy position")) {
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


	void createChildren(EntityRef entity, WorldEditor& editor, ComponentType child_type0, ComponentType child_type1, ComponentType child_type2) {
		editor.beginCommandGroup("create_gui_rect_child");
		EntityRef child = editor.addEntity();
		editor.makeParent(entity, child);
		editor.selectEntities(Span(&child, 1), false);
		editor.addComponent(Span(&child, 1), GUI_RECT_TYPE);
		ASSERT(child_type0 != GUI_RECT_TYPE);
		ASSERT(child_type1 != GUI_RECT_TYPE);
		ASSERT(child_type2 != GUI_RECT_TYPE);
		editor.addComponent(Span(&child, 1), child_type0);
		editor.addComponent(Span(&child, 1), child_type1);
		editor.addComponent(Span(&child, 1), child_type2);
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
	ResizeSide m_resize_side = ResizeSide::NONE;
	Vec2 m_bottom_right_start_transform;
	Vec2 m_top_left_start_transform;
	Vec2 m_canvas_size;
	EntityPtr m_canvas_entity = INVALID_ENTITY;
	Vec3 m_clear_color = Vec3(0);

	Action m_hcenter_action;
	Action m_vcenter_action;
	Action m_hexpand_action;
	Action m_vexpand_action;
	Action m_make_rel_action;
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
		PROFILE_FUNCTION();
		m_gui_editor.init();
		m_app.addPlugin(m_gui_editor);

		const char* exts[] = {"spr"};
		m_app.getAssetBrowser().addPlugin(m_sprite_plugin, Span(exts));
		m_app.getAssetCompiler().addPlugin(m_sprite_plugin, Span(exts));
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


LUMIX_STUDIO_ENTRY(gui) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
