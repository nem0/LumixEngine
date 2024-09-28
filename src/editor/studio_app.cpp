#include <imgui/imgui.h>
#include <imgui/imgui_freetype.h>
#include <imgui/imgui_internal.h>

#include "core/default_allocator.h"
#include "core/associative_array.h"
#include "core/atomic.h"
#include "core/command_line_parser.h"
#include "core/debug.h"
#include "engine/file_system.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"

#include "audio/audio_module.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/entity_folders.h"
#include "editor/file_system_watcher.h"
#include "editor/gizmo.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/signal_editor.h"
#include "editor/spline_editor.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/engine_hash_funcs.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "log_ui.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "settings.h"
#include "studio_app.h"
#include "utils.h"

#ifdef _WIN32
	#include "core/win/simple_win.h"
#endif

namespace Lumix
{

#define LUMIX_EDITOR_PLUGINS_DECLS
#include "engine/plugins.inl"
#undef LUMIX_EDITOR_PLUGINS_DECLS

struct TarHeader {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];   
};

struct StudioAppImpl final : StudioApp {
	struct HierarchyGUI : StudioApp::GUIPlugin {
		HierarchyGUI(StudioAppImpl& app)
			: m_app(app)
		{
			m_app.getSettings().registerPtr("entity_list_open", &m_is_open);
			m_app.getWorldEditor().entitySelectionChanged().bind<&HierarchyGUI::onEntitySelectionChanged>(this);
	
			m_focus_filter_action.init("Focus hierarchy", "Focus hierarchy filter", "focus_hierarchy", "", os::Keycode::F, Action::Modifiers::CTRL, Action::Type::IMGUI_PRIORITY);
			m_focus_filter_action.func.bind<&HierarchyGUI::focusHierarchyFilter>(this);
			m_app.addAction(&m_focus_filter_action);
		}

		~HierarchyGUI() {
			m_app.removeAction(&m_focus_filter_action);
		}

		void renameGUI() {
			WorldEditor& editor = m_app.getWorldEditor();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
			if (m_set_rename_focus) {
				ImGui::SetKeyboardFocusHere();
				m_set_rename_focus = false;
			}
			if (ImGui::InputText("##renamed_val", m_rename_buf, sizeof(m_rename_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
				editor.setEntityName((EntityRef)m_renaming_entity, m_rename_buf);
				m_renaming_entity = INVALID_ENTITY;
			}
			if (ImGui::IsItemDeactivated() && m_renaming_entity.isValid()) {
				if (ImGui::IsItemDeactivatedAfterEdit() && m_rename_buf[0]) {
					editor.setEntityName((EntityRef)m_renaming_entity, m_rename_buf);
				}
				m_renaming_entity = INVALID_ENTITY;
			}
			m_set_rename_focus = false;
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();			
		}

		void showHierarchy(EntityRef entity, const Array<EntityRef>& selected_entities, Span<const EntityRef> selection_chain) {
			WorldEditor& editor = m_app.getWorldEditor();
			World* world = editor.getWorld();
			bool is_selected = selected_entities.indexOf(entity) >= 0;
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap;
			bool has_child = world->getFirstChild(entity).isValid();
			if (!has_child) flags = ImGuiTreeNodeFlags_Leaf;
			if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;
			flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
			
			bool node_open;
			if (m_renaming_entity == entity) {
				node_open = ImGui::TreeNodeEx((void*)(intptr_t)entity.index, flags, "%s", "");
				renameGUI();
			}
			else {
				const ImVec2 cp = ImGui::GetCursorPos();
				ImGui::Dummy(ImVec2(1.f, ImGui::GetTextLineHeightWithSpacing()));
				if (selection_chain.length() > 0 && selection_chain[0] == entity) {
					ImGui::SetNextItemOpen(true);
					selection_chain.removePrefix(1);
					if (selection_chain.length() == 0) {
						ImGui::SetScrollHereY();
					}
				}
				if (ImGui::IsItemVisible()) {
					ImGui::SetCursorPos(cp);
					char buffer[1024];
					getEntityListDisplayName(m_app, *world, Span(buffer), entity);
					node_open = ImGui::TreeNodeEx((void*)(intptr_t)entity.index, flags, "%s", buffer);
				}
				else {
					const char* dummy = "";
					const ImGuiID id = ImGui::GetCurrentWindow()->GetID((void*)(intptr_t)entity.index);
					if (ImGui::TreeNodeUpdateNextOpen(id, flags)) {
						ImGui::SetCursorPos(cp);
						node_open = ImGui::TreeNodeBehavior(id, flags, dummy, dummy);
					}
					else {
						node_open = false;
					}
				}
			}
			
			if (ImGui::IsItemVisible()) {
				ImGui::PushID(entity.index);
				if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) ImGui::OpenPopup("entity_context_menu");
				if (ImGui::BeginPopup("entity_context_menu"))
				{
					if (ImGui::BeginMenu("Create child")) {
						m_app.onCreateEntityWithComponentGUI(entity);
						ImGui::EndMenu();
					}

					if (ImGui::MenuItem("Select all children")) {
						Array<EntityRef> tmp(m_app.m_allocator);
						for (EntityRef e : world->childrenOf(entity)) {
							tmp.push(e);
						}
						editor.selectEntities(tmp, false);
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				if (ImGui::BeginDragDropTarget()) {
					if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
						EntityRef dropped_entity = *(EntityRef*)payload->Data;
						if (dropped_entity != entity) {
							editor.makeParent(entity, dropped_entity);
							ImGui::EndDragDropTarget();
							if (node_open) ImGui::TreePop();
							return;
						}
					}

					if (auto* payload = ImGui::AcceptDragDropPayload("selected_entities")) {
						const Array<EntityRef>& selected = editor.getSelectedEntities();
						for (EntityRef e : selected) {
							if (e != entity) {
								editor.makeParent(entity, e);
							}
						}
						ImGui::EndDragDropTarget();
						if (node_open) ImGui::TreePop();
						return;
					}

					ImGui::EndDragDropTarget();
				}

				if (ImGui::BeginDragDropSource())
				{
					char buffer[1024];
					getEntityListDisplayName(m_app, *world, Span(buffer), entity);
					ImGui::TextUnformatted(buffer);
					
					const Array<EntityRef>& selected = editor.getSelectedEntities();
					if (selected.size() > 0 && selected.indexOf(entity) >= 0) {
						ImGui::SetDragDropPayload("selected_entities", nullptr, 0);
					}
					else {	
						ImGui::SetDragDropPayload("entity", &entity, sizeof(entity));
					}
					ImGui::EndDragDropSource();
				}
				else {
					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						editor.selectEntities(Span(&entity, 1), ImGui::GetIO().KeyCtrl);
					}
				}
			}

			if (node_open) {
				for (EntityRef e : world->childrenOf(entity)) {
					showHierarchy(e, selected_entities, selection_chain);
				}

				ImGui::TreePop();
			}
		}

		void folderUI(EntityFolders::FolderHandle folder_id, EntityFolders& folders, u32 level, Span<const EntityRef> selection_chain, const char* name_override, World::PartitionHandle partition) {
			WorldEditor& editor = m_app.getWorldEditor();
			static EntityFolders::FolderHandle force_open_folder = EntityFolders::INVALID_FOLDER;
			const EntityFolders::Folder* folder = &folders.getFolder(folder_id);
			ImGui::PushID((const char*)&folder->id, (const char*)&folder->id + sizeof(folder->id));
			bool node_open;
			ImGuiTreeNodeFlags flags = level == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0;
			flags |= ImGuiTreeNodeFlags_OpenOnArrow;
			if (folders.getSelectedFolder() == folder_id) flags |= ImGuiTreeNodeFlags_Selected;
			if (force_open_folder == folder_id) {
				ImGui::SetNextItemOpen(true);
				force_open_folder = EntityFolders::INVALID_FOLDER;
			}
			if (m_renaming_folder == folder_id) {
				node_open = ImGui::TreeNodeEx((void*)folder, flags, "%s", ICON_FA_FOLDER);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
				if (m_set_rename_focus) {
					ImGui::SetKeyboardFocusHere();
					m_set_rename_focus = false;
				}
				if (ImGui::InputText("##renamed_val", m_rename_buf, sizeof(m_rename_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
					editor.renameEntityFolder(m_renaming_folder, m_rename_buf);
					m_rename_buf[0] = 0;
				}
				if (ImGui::IsItemDeactivated()) {
					if (ImGui::IsItemDeactivatedAfterEdit() && m_rename_buf[0]) {
						editor.renameEntityFolder(m_renaming_folder, m_rename_buf);
					}
					m_renaming_folder = EntityFolders::INVALID_FOLDER;
				}
				m_set_rename_focus = false;
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor();
			}
			else {
				if (name_override) {
					node_open = ImGui::TreeNodeEx((void*)folder, flags, "%s%s", ICON_FA_HOME, name_override);
				}
				else {
					node_open = ImGui::TreeNodeEx((void*)folder, flags, "%s%s", ICON_FA_FOLDER, folder->name);
				}
			}
			
			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
					EntityRef dropped_entity = *(EntityRef*)payload->Data;
					editor.beginCommandGroup("move_entity_to_folder_group");
					editor.makeParent(INVALID_ENTITY, dropped_entity);
					editor.moveEntityToFolder(dropped_entity, folder_id);
					editor.endCommandGroup();
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) {
				folders.selectFolder(folder_id);
			}

			if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) {
				ImGui::OpenPopup("folder_context_menu");
			}
			if (ImGui::BeginPopup("folder_context_menu")) {
				if (ImGui::Selectable("New folder")) {
					force_open_folder = folder_id;
					EntityFolders::FolderHandle new_folder = editor.createEntityFolder(folder_id);
					folder = &folders.getFolder(folder_id);
					m_renaming_folder = new_folder;
					m_set_rename_focus = true;
				}
				const bool is_root = folder->parent == EntityFolders::INVALID_FOLDER;
				World* world = editor.getWorld();

				if (is_root) {
					const bool is_partition_named = world->getPartition(partition).name[0];
					if (is_partition_named) {
						if (ImGui::Selectable("Save")) {
							if (editor.isGameMode()) {
								logError("Could not save while the game is running");
							}
							else {
								editor.savePartition(partition);
							}
						}
					}
					else {
						if (ImGui::Selectable("Save As")) {
							EntityFolders& folders = editor.getEntityFolders();
							EntityFolders::FolderHandle root = folders.getRoot(partition);
							folders.selectFolder(root);
							m_app.saveAs();
						}
					}
				}

				if (!is_root || world->getPartitions().size() > 1) {
					if (ImGui::Selectable(is_root ? "Unload" : "Delete")) {
						if (is_root) {
							m_confirm_destroy_partition = true;
							m_partition_to_destroy = partition;
						}
						else {
							editor.destroyEntityFolder(folder_id);
							ImGui::EndPopup();
							if (node_open) ImGui::TreePop();
							ImGui::PopID();
							return;
						}
					}
				}

				const bool has_children = folders.getFolder(folder_id).first_entity.isValid();
				if (ImGui::Selectable("Select entities", false, has_children ? 0 : ImGuiSelectableFlags_Disabled)) {
					Array<EntityRef> entities(m_app.m_allocator);
					EntityPtr e = folders.getFolder(folder_id).first_entity;
					while (e.isValid()) {
						entities.push((EntityRef)e);
						const EntityPtr next = folders.getNextEntity((EntityRef)e);
						e = next;
					}
					editor.selectEntities(entities, false);
				}
				if (level > 0 && ImGui::Selectable("Rename")) {
					m_renaming_folder = folder_id;
					m_set_rename_focus = true;
				}
				ImGui::EndPopup();
			}

			if (!node_open) {
				ImGui::PopID();
				return;
			}

			EntityFolders::FolderHandle child_id = folder->first_child;
			while (child_id != EntityFolders::INVALID_FOLDER) {
				const EntityFolders::Folder& child = folders.getFolder(child_id);
				const EntityFolders::FolderHandle next = child.next;
				folderUI(child_id, folders, level + 1, selection_chain, nullptr, partition);
				child_id = next;
			}

			EntityPtr child_e = folder->first_entity;
			while (child_e.isValid()) {
				if (!editor.getWorld()->getParent((EntityRef)child_e).isValid()) {
					showHierarchy((EntityRef)child_e, editor.getSelectedEntities(), selection_chain);
				}
				child_e = folders.getNextEntity((EntityRef)child_e);
			}

			ImGui::TreePop();
			ImGui::PopID();
		}

		void getSelectionChain(Array<EntityRef>& chain, EntityPtr e) const {
			if (!e.isValid()) return;
			
			WorldEditor& editor = m_app.getWorldEditor();
			e = editor.getWorld()->getParent(*e);
			while (e.isValid()) {
				chain.push(*e);
				e = editor.getWorld()->getParent(*e);
			}
			for (i32 i = 0; i < chain.size() / 2; ++i) {
				swap(chain[i], chain[chain.size() - 1 - i]); 
			}
		}

		void focusHierarchyFilter() {
			m_request_focus_filter = true;
			m_is_open = true;
		}

		void onGUI() override {
			PROFILE_FUNCTION();

			WorldEditor& editor = m_app.getWorldEditor();
			if (m_confirm_destroy_partition) {
				ImGui::OpenPopup("Confirm##confirm_destroy_partition");
				m_confirm_destroy_partition = false;
			}
			if (ImGui::BeginPopupModal("Confirm##confirm_destroy_partition")) {
				ImGui::Text("All unsaved changes will be lost, do you want to continue?");
				if (ImGui::Button("Continue")) {
					editor.destroyWorldPartition(m_partition_to_destroy);
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}

			const Array<EntityRef>& entities = editor.getSelectedEntities();
			static TextFilter filter;
			m_has_focus = false;
			if (!m_is_open) return;

			if (m_request_focus_filter) ImGui::SetNextWindowFocus();
			if (ImGui::Begin(ICON_FA_STREAM "Hierarchy##hierarchy", &m_is_open)) {
				m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				if (m_request_focus_filter) {
					ImGui::SetKeyboardFocusHere();
					m_request_focus_filter = false;
				}
				
				bool select_first = false;
				World* world = editor.getWorld();
				filter.gui(ICON_FA_SEARCH "Filter", -1, false, &m_focus_filter_action);
				if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
					select_first = true;
				}

				if (ImGui::BeginChild("entities")) {
					ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x);
					
					if (filter.isActive()) {
						for (EntityPtr e = world->getFirstEntity(); e.isValid(); e = world->getNextEntity((EntityRef)e)) {
							char buffer[1024];
							getEntityListDisplayName(m_app, *world, Span(buffer), e);
							if (!filter.pass(buffer)) continue;

							ImGui::PushID(e.index);
							const EntityRef e_ref = (EntityRef)e;
							if (select_first) {
								select_first = false;
								editor.selectEntities(Span(&e_ref, 1), false);
							}

							if (m_renaming_entity == e_ref) {
								renameGUI();
							} else {
								bool selected = entities.indexOf(e_ref) >= 0;
								if (ImGui::Selectable(buffer, &selected, ImGuiSelectableFlags_SpanAvailWidth)) {
									editor.selectEntities(Span(&e_ref, 1), ImGui::GetIO().KeyCtrl);
								}
								if (ImGui::BeginDragDropSource()) {
									ImGui::TextUnformatted(buffer);
									ImGui::SetDragDropPayload("entity", &e, sizeof(e));
									ImGui::EndDragDropSource();
								}
							}
							ImGui::PopID();
						}
					} else {
						EntityFolders& folders = editor.getEntityFolders();
						Array<EntityRef> selection_chain(m_app.getAllocator());
						if (m_entity_selection_changed && !editor.getSelectedEntities().empty()) {
							getSelectionChain(selection_chain, editor.getSelectedEntities()[0]);
							m_entity_selection_changed = false;
						}
						for (const World::Partition& p : world->getPartitions()) {
							folderUI(folders.getRoot(p.handle), folders, 0, selection_chain, p.name, p.handle);
						}
					}
					ImGui::PopItemWidth();
				}
				ImGui::EndChild();
			}
			ImGui::End();
		}

		bool hasFocus() const override { return m_has_focus; }
		
		bool onAction(const Action& action) override {
			if (&action == &m_app.m_common_actions.rename) {
				WorldEditor& editor = m_app.getWorldEditor();
				const Array<EntityRef>& selected_entities = editor.getSelectedEntities();
				m_renaming_entity = selected_entities.empty() ? INVALID_ENTITY : selected_entities[0];
				if (m_renaming_entity.isValid()) {
					m_set_rename_focus = true;
					const char* name = editor.getWorld()->getEntityName(selected_entities[0]);
					copyString(m_rename_buf, name);
				}
				return true;
			}
			return false;
		}

		const char* getName() const override { return "hierarchy"; }

		void onEntitySelectionChanged() {
			m_entity_selection_changed = true;
		}

		StudioAppImpl& m_app;
		bool m_is_open = true;
		bool m_has_focus = false;
		bool m_entity_selection_changed = false;
		Action m_focus_filter_action;
		bool m_request_focus_filter = false;
		EntityPtr m_renaming_entity = INVALID_ENTITY;
		EntityFolders::FolderHandle m_renaming_folder = EntityFolders::INVALID_FOLDER;
		bool m_set_rename_focus = false;
		char m_rename_buf[World::ENTITY_NAME_MAX_LENGTH];
		bool m_confirm_destroy_partition = false;
		World::PartitionHandle m_partition_to_destroy;
	};

	StudioAppImpl()
		: m_finished(false)
		, m_deferred_game_mode_exit(false)
		, m_actions(m_allocator)
		, m_owned_actions(m_allocator)
		, m_window_actions(m_allocator)
		, m_tools_actions(m_allocator)
		, m_is_welcome_screen_open(true)
		, m_is_export_game_dialog_open(false)
		, m_settings(*this)
		, m_gui_plugins(m_allocator)
		, m_mouse_plugins(m_allocator)
		, m_plugins(m_allocator)
		, m_add_cmp_plugins(m_allocator)
		, m_component_labels(m_allocator)
		, m_component_icons(m_allocator)
		, m_exit_code(0)
		, m_events(m_allocator)
		, m_windows(m_allocator)
		, m_deferred_destroy_windows(m_allocator)
		, m_file_selector(*this)
		, m_dir_selector(*this)
		, m_debug_allocator(m_main_allocator)
		, m_imgui_allocator(m_debug_allocator, "imgui")
		, m_allocator(m_debug_allocator, "studio")
		, m_export(m_allocator)
	{
		PROFILE_FUNCTION();
		u32 cpus_count = minimum(os::getCPUsCount(), 64);
		u32 workers;
		if (workersCountOption(workers)) {
			cpus_count = workers;
		}
		if (!jobs::init(cpus_count, m_allocator)) {
			logError("Failed to initialize job system.");
		}

		memset(m_imgui_key_map, 0, sizeof(m_imgui_key_map));
		m_imgui_key_map[(int)os::Keycode::CTRL] = ImGuiMod_Ctrl;
		m_imgui_key_map[(int)os::Keycode::ALT] = ImGuiMod_Alt;
		m_imgui_key_map[(int)os::Keycode::SHIFT] = ImGuiMod_Shift;
		m_imgui_key_map[(int)os::Keycode::LSHIFT] = ImGuiKey_LeftShift;
		m_imgui_key_map[(int)os::Keycode::RSHIFT] = ImGuiKey_RightShift;
		m_imgui_key_map[(int)os::Keycode::SPACE] = ImGuiKey_Space;
		m_imgui_key_map[(int)os::Keycode::TAB] = ImGuiKey_Tab;
		m_imgui_key_map[(int)os::Keycode::LEFT] = ImGuiKey_LeftArrow;
		m_imgui_key_map[(int)os::Keycode::RIGHT] = ImGuiKey_RightArrow;
		m_imgui_key_map[(int)os::Keycode::UP] = ImGuiKey_UpArrow;
		m_imgui_key_map[(int)os::Keycode::DOWN] = ImGuiKey_DownArrow;
		m_imgui_key_map[(int)os::Keycode::PAGEUP] = ImGuiKey_PageUp;
		m_imgui_key_map[(int)os::Keycode::PAGEDOWN] = ImGuiKey_PageDown;
		m_imgui_key_map[(int)os::Keycode::HOME] = ImGuiKey_Home;
		m_imgui_key_map[(int)os::Keycode::END] = ImGuiKey_End;
		m_imgui_key_map[(int)os::Keycode::DEL] = ImGuiKey_Delete;
		m_imgui_key_map[(int)os::Keycode::BACKSPACE] = ImGuiKey_Backspace;
		m_imgui_key_map[(int)os::Keycode::F3] = ImGuiKey_F3;
		m_imgui_key_map[(int)os::Keycode::F11] = ImGuiKey_F11;
		m_imgui_key_map[(int)os::Keycode::RETURN] = ImGuiKey_Enter;
		m_imgui_key_map[(int)os::Keycode::ESCAPE] = ImGuiKey_Escape;
		m_imgui_key_map[(int)os::Keycode::A] = ImGuiKey_A;
		m_imgui_key_map[(int)os::Keycode::C] = ImGuiKey_C;
		m_imgui_key_map[(int)os::Keycode::D] = ImGuiKey_D;
		m_imgui_key_map[(int)os::Keycode::F] = ImGuiKey_F;
		m_imgui_key_map[(int)os::Keycode::V] = ImGuiKey_V;
		m_imgui_key_map[(int)os::Keycode::X] = ImGuiKey_X;
		m_imgui_key_map[(int)os::Keycode::Y] = ImGuiKey_Y;
		m_imgui_key_map[(int)os::Keycode::Z] = ImGuiKey_Z;
	}

	~StudioAppImpl() {
		jobs::shutdown();
	}

	void onEvent(const os::Event& event)
	{
		const bool handle_input = isFocused();
		m_events.push(event);
		switch (event.type) {
			case os::Event::Type::MOUSE_MOVE: break;
			case os::Event::Type::FOCUS: {
				ImGuiIO& io = ImGui::GetIO();
				io.AddFocusEvent(isFocused());
				break;
			}
			case os::Event::Type::MOUSE_BUTTON: {
				ImGuiIO& io = ImGui::GetIO();
				if (handle_input || !event.mouse_button.down) {
					io.AddMouseButtonEvent((int)event.mouse_button.button, event.mouse_button.down);
				}
				break;
			}
			case os::Event::Type::MOUSE_WHEEL:
				if (handle_input) {
					ImGuiIO& io = ImGui::GetIO();
					io.AddMouseWheelEvent(0, event.mouse_wheel.amount);
				}
				break;
			case os::Event::Type::WINDOW_SIZE:
				if (ImGui::GetCurrentContext()) {
					ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(event.window);
					if (vp) vp->PlatformRequestResize = true;
				}
				break;
			case os::Event::Type::WINDOW_MOVE:
				if (ImGui::GetCurrentContext()) {
					ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(event.window);
					if (vp) vp->PlatformRequestMove = true;
				}
				break;
			case os::Event::Type::WINDOW_CLOSE: {
				ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(event.window);
				if (vp) vp->PlatformRequestClose = true;
				if (event.window == m_main_window) exit();
				break;
			}
			case os::Event::Type::QUIT:
				exit();
				break;
			case os::Event::Type::CHAR:
				if (handle_input) {
					ImGuiIO& io = ImGui::GetIO();
					char tmp[5] = {};
					memcpy(tmp, &event.text_input.utf8, sizeof(event.text_input.utf8));
					io.AddInputCharactersUTF8(tmp);
				}
				break;
			case os::Event::Type::KEY:
				if (handle_input || !event.key.down) {
					ImGuiIO& io = ImGui::GetIO();
					ImGuiKey key = m_imgui_key_map[(int)event.key.keycode];
					if (key != ImGuiKey_None) io.AddKeyEvent(key, event.key.down);

					checkShortcuts();
				}
				break;
			case os::Event::Type::DROP_FILE:
				for(int i = 0, c = os::getDropFileCount(event); i < c; ++i) {
					char tmp[MAX_PATH];
					if (os::getDropFile(event, i, Span(tmp))) {
						for (GUIPlugin* plugin : m_gui_plugins) {
							if (plugin->onDropFile(tmp)) break;
						}
					}
				}
				break;
		}
	}

	bool isFocused() const {
		const os::WindowHandle focused = os::getFocused();
		const int idx = m_windows.find([focused](os::WindowHandle w){ return w == focused; });
		return idx >= 0;
	}

	void onShutdown() {
		while (m_engine->getFileSystem().hasWork()) {
			m_engine->getFileSystem().processCallbacks();
		}

		removeAction(&m_start_standalone_app);
		removeAction(&m_show_all_actions_action);
		removePlugin(*m_asset_browser.get());
		removePlugin(*m_log_ui.get());
		removePlugin(*m_property_grid.get());
		removePlugin(*m_profiler_ui.get());

		m_asset_browser->releaseResources();
		m_watched_plugin.watcher.reset();

		saveSettings();

		while (m_engine->getFileSystem().hasWork()) {
			m_engine->getFileSystem().processCallbacks();
		}

		m_editor->newWorld();

		destroyAddCmpTreeNode(m_add_cmp_root.child);

		for (auto* i : m_plugins) {
			LUMIX_DELETE(m_allocator, i);
		}
		m_plugins.clear();

		for (auto* i : m_gui_plugins) {
			LUMIX_DELETE(m_allocator, i);
		}
		m_gui_plugins.clear();

		PrefabSystem::destroyEditorPlugins(*this);
		ASSERT(m_mouse_plugins.empty());

		for (auto* i : m_add_cmp_plugins)
		{
			LUMIX_DELETE(m_allocator, i);
		}
		m_add_cmp_plugins.clear();

		m_hierarchy.destroy();
		m_profiler_ui.reset();
		m_asset_browser.reset();
		m_property_grid.destroy();
		m_log_ui.destroy();
		ASSERT(!m_render_interface);
		m_asset_compiler.reset();
		m_editor.reset();

		removeAction(&m_common_actions.save);
		removeAction(&m_common_actions.undo);
		removeAction(&m_common_actions.redo);
		removeAction(&m_common_actions.del);
		removeAction(&m_common_actions.cam_orbit);
		removeAction(&m_common_actions.cam_forward);
		removeAction(&m_common_actions.cam_backward);
		removeAction(&m_common_actions.cam_right);
		removeAction(&m_common_actions.cam_left);
		removeAction(&m_common_actions.cam_up);
		removeAction(&m_common_actions.cam_down);
		removeAction(&m_common_actions.select_all);
		removeAction(&m_common_actions.rename);

		for (Action* action : m_owned_actions) {
			removeAction(action);
			LUMIX_DELETE(m_allocator, action);
		}
		m_owned_actions.clear();
		ASSERT(m_actions.empty());
		m_actions.clear();

		m_engine.reset();
	}


	void onIdle() {
		PROFILE_FUNCTION();
		profiler::blockColor(0x7f, 0x7f, 0x7f);

		updateGizmoOffset();
		processDeferredWindowDestroy();

		if (m_watched_plugin.reload_request) tryReloadPlugin();

		guiBeginFrame();
		m_asset_compiler->update();
		m_editor->update();
		showGizmos();

		m_engine->update(*m_editor->getWorld());

		++m_fps_frame;
		if (m_fps_timer.getTimeSinceTick() > 1.0f) {
			m_fps = m_fps_frame / m_fps_timer.tick();
			m_fps_frame = 0;
		}

		if (m_deferred_game_mode_exit) {
			m_deferred_game_mode_exit = false;
			m_editor->toggleGameMode();
		}

		float time_delta = m_engine->getLastTimeDelta();
		for (auto* plugin : m_gui_plugins) {
			plugin->update(time_delta);
		}

		if (m_settings.getTimeSinceLastSave() > 30.f) saveSettings();

		guiEndFrame();

		if (m_first_update) {
			// we show window after the first update, so it does not show default (white) background
			// only to be replace with actual (potentially dark) content
			os::showWindow(m_main_window);
			if (m_settings.getBool("is_maximized", false)) os::maximizeWindow(m_main_window);
			m_first_update = false;
		}

		if (!isFocused()) ++m_frames_since_focused;
		else m_frames_since_focused = 0;

		if (m_sleep_when_inactive && m_frames_since_focused > 10) {
			const float frame_time = m_inactive_fps_timer.tick();
			const float wanted_fps = 5.0f;

			if (frame_time < 1 / wanted_fps) {
				PROFILE_BLOCK("sleep");
				os::sleep(u32(1000 / wanted_fps - frame_time * 1000));
			}
			m_inactive_fps_timer.tick();
		}

		profiler::frame();
		m_events.clear();
	}

	void run() override {
		profiler::setThreadName("Main thread");
		Semaphore semaphore(0, 1);
		struct Data {
			StudioAppImpl* that;
			Semaphore* semaphore;
		} data = {this, &semaphore};
		jobs::runLambda([&data]() {
			data.that->onInit();
			if (CommandLineParser::isOn("-profile_start")) {
				profiler::pause(true);
			}
			while (!data.that->m_finished) {
				os::Event e;
				while(os::getEvent(e)) {
					data.that->onEvent(e);
				}
				data.that->onIdle();
			}
			data.that->onShutdown();
			data.semaphore->signal();
		}, nullptr, 0);
		PROFILE_BLOCK("sleeping");
		semaphore.wait();
	}

	
	static void* imguiAlloc(size_t size, void* user_data) {
		StudioAppImpl* app = (StudioAppImpl*)user_data;
		return app->m_imgui_allocator.allocate(size, 8);
	}


	static void imguiFree(void* ptr, void* user_data) {
		StudioAppImpl* app = (StudioAppImpl*)user_data;
		return app->m_imgui_allocator.deallocate(ptr);
	}

	static os::HitTestResult childHitTestCallback(void* user_data, os::WindowHandle window, os::Point mp) {
		#if 1
			// let imgui handle size of secondary windows
			// otherwise it has issues with docking
			return os::HitTestResult::CLIENT;
		#else
			StudioAppImpl* studio = (StudioAppImpl*)user_data;
			if (mp.y < os::getWindowScreenRect(window).top + 20) return os::HitTestResult::CAPTION;
			if (ImGui::IsAnyItemHovered()) return os::HitTestResult::CLIENT;
			return os::HitTestResult::NONE;
		#endif
	}

	static os::HitTestResult hitTestCallback(void* user_data, os::WindowHandle window, os::Point mp) {
		StudioAppImpl* studio = (StudioAppImpl*)user_data;
		if (studio->m_is_caption_hovered) return os::HitTestResult::CAPTION;
		if (ImGui::IsAnyItemHovered()) return os::HitTestResult::CLIENT;
		return os::HitTestResult::NONE;
	}

	void onInit()
	{
		PROFILE_FUNCTION();

		os::Timer init_timer;
		m_add_cmp_root.label[0] = '\0';

		char saved_data_dir[MAX_PATH] = {};
		os::InputFile cfg_file;
		if (cfg_file.open(".lumixuser")) {
			if (!cfg_file.read(saved_data_dir, minimum(lengthOf(saved_data_dir), (int)cfg_file.size()))) {
				logError("Failed to read .lumixuser");
			}
			cfg_file.close();
		}

		char current_dir[MAX_PATH] = "";
		os::getCurrentDirectory(Span(current_dir));

		char data_dir[MAX_PATH] = "";
		checkDataDirCommandLine(data_dir, lengthOf(data_dir));

		Engine::InitArgs init_data = {};
		init_data.working_dir = data_dir[0] ? data_dir : (saved_data_dir[0] ? saved_data_dir : current_dir);
		const char* plugins[] = {
			#define LUMIX_PLUGINS_STRINGS
				#include "engine/plugins.inl"
			#undef LUMIX_PLUGINS_STRINGS
		};
		init_data.plugins = Span(plugins, plugins + lengthOf(plugins) - 1);
		m_engine = Engine::create(static_cast<Engine::InitArgs&&>(init_data), m_allocator);
		m_settings.registerPtr("report_crashes", &m_crash_reporting, "General");
		m_settings.registerPtr("sleep_when_inactive", &m_sleep_when_inactive);
		m_settings.registerPtr("fileselector_dir", &m_file_selector.m_current_dir);
		m_settings.registerPtr("font_size", &m_font_size, "General");
		m_settings.registerPtr("export_pack", &m_export.pack);
		m_settings.registerPtr("export_dir", &m_export.dest_dir);
		m_settings.registerPtr("gizmo_scale", &m_gizmo_config.scale, "General");
		m_settings.registerPtr("fov", &m_fov, "General", true);
		// we need some stuff (font_size) from settings at this point
		m_settings.load();

		os::InitWindowArgs init_window_args;
		init_window_args.icon = "editor/logo.ico";
		init_window_args.user_data = this;
		init_window_args.hit_test_callback = &StudioAppImpl::hitTestCallback;
		init_window_args.flags = os::InitWindowArgs::NO_DECORATION;
		init_window_args.handle_file_drops = true;
		init_window_args.name = "Lumix Studio";
		init_window_args.is_hidden = true;

		m_main_window = os::createWindow(init_window_args);
		m_windows.push(m_main_window);
		m_engine->setMainWindow(m_main_window);
		
		beginInitIMGUI();
		// we need to create the asset compiler before plugins, since asset compiler installs a hook for asset loading
		// and plugins can try to load stuf, e.g. renderer loads postprocess shaders
		m_asset_compiler = AssetCompiler::create(*this);
		m_engine->init();
		jobs::wait(&m_init_imgui_signal);
		
		logInfo("Current directory: ", current_dir);

		extractBundled();

		m_editor = WorldEditor::create(*m_engine, m_allocator);
		loadUserPlugins();
		addActions();

		m_asset_browser = AssetBrowser::create(*this);
		m_hierarchy.create(*this);
		m_property_grid.create(*this);
		m_profiler_ui = createProfilerUI(*this);
		m_log_ui.create(*this, m_allocator);

		initPlugins(); // needs initialized imgui
		loadSettings(); // we can load settings now, we have everything (i.e. actions, imgui context) available

		loadWorldFromCommandLine();

		m_asset_compiler->onInitFinished();
		m_asset_browser->onInitFinished();
		
		loadLogo();

		logInfo("Init took ", init_timer.getTimeSinceStart(), " s");
		#ifdef _WIN32
			logInfo(os::getTimeSinceProcessStart(), " s since process started");
		#endif
	}

	void loadLogo() {
		if (!m_render_interface) return;
		m_logo = m_render_interface->loadTexture(Path("editor/logo.png"));
	}

	void destroyAddCmpTreeNode(AddCmpTreeNode* node)
	{
		if (!node) return;
		destroyAddCmpTreeNode(node->child);
		destroyAddCmpTreeNode(node->next);
		LUMIX_DELETE(m_allocator, node);
	}

	const char* getComponentIcon(ComponentType cmp_type) const override
	{
		auto iter = m_component_icons.find(cmp_type);
		if (iter == m_component_icons.end()) return "";
		return iter.value();
	}

	const char* getComponentTypeName(ComponentType cmp_type) const override
	{
		auto iter = m_component_labels.find(cmp_type);
		if (iter == m_component_labels.end()) return "Unknown";
		return iter.value().c_str();
	}


	const AddCmpTreeNode& getAddComponentTreeRoot() const override { return m_add_cmp_root; }


	void addPlugin(IAddComponentPlugin& plugin)
	{
		int i = 0;
		while (i < m_add_cmp_plugins.size() && compareString(plugin.getLabel(), m_add_cmp_plugins[i]->getLabel()) > 0)
		{
			++i;
		}
		m_add_cmp_plugins.insert(i, &plugin);

		auto* node = LUMIX_NEW(m_allocator, AddCmpTreeNode);
		copyString(node->label, plugin.getLabel());
		node->plugin = &plugin;
		insertAddCmpNode(m_add_cmp_root, node);
	}


	static void insertAddCmpNodeOrdered(AddCmpTreeNode& parent, AddCmpTreeNode* node)
	{
		if (!parent.child)
		{
			parent.child = node;
			return;
		}
		if (compareString(parent.child->label, node->label) > 0)
		{
			node->next = parent.child;
			parent.child = node;
			return;
		}
		auto* i = parent.child;
		while (i->next && compareString(i->next->label, node->label) < 0)
		{
			i = i->next;
		}
		node->next = i->next;
		i->next = node;
	}


	void insertAddCmpNode(AddCmpTreeNode& parent, AddCmpTreeNode* node)
	{
		for (auto* i = parent.child; i; i = i->next)
		{
			if (!i->plugin && startsWith(node->label, i->label))
			{
				insertAddCmpNode(*i, node);
				return;
			}
		}
		const char* rest = node->label + stringLength(parent.label);
		if (parent.label[0] != '\0') ++rest; // include '/'
		const char* slash = find(rest, '/');
		if (!slash)
		{
			insertAddCmpNodeOrdered(parent, node);
			return;
		}
		auto* new_group = LUMIX_NEW(m_allocator, AddCmpTreeNode);
		copyString(Span(new_group->label), StringView(node->label, u32(slash - node->label)));
		insertAddCmpNodeOrdered(parent, new_group);
		insertAddCmpNode(*new_group, node);
	}

	void registerComponent(const char* icon, ComponentType cmp_type, const char* label, ResourceType resource_type, const char* property) {
		struct Plugin final : IAddComponentPlugin {
			void onGUI(bool create_entity, bool from_filter, EntityPtr parent, WorldEditor& editor) override {
				const char* last = reverseFind(label, '/');
				last = last && !from_filter ? last + 1 : label;
				if (last[0] == ' ') ++last;
				if (!ImGui::BeginMenu(last)) return;
				Path path;
				bool create_empty = ImGui::MenuItem(ICON_FA_BROOM " Empty");
				static FilePathHash selected_res_hash;
				if (asset_browser->resourceList(path, selected_res_hash, resource_type, true) || create_empty) {
					editor.beginCommandGroup("createEntityWithComponent");
					if (create_entity) {
						EntityRef entity = editor.addEntity();
						editor.selectEntities(Span(&entity, 1), false);
					}

					const Array<EntityRef>& selected_entites = editor.getSelectedEntities();
					editor.addComponent(selected_entites, type);
					if (!create_empty) {
						editor.setProperty(type, "", -1, property, editor.getSelectedEntities(), path);
					}
					if (parent.isValid()) editor.makeParent(parent, selected_entites[0]);
					editor.endCommandGroup();
					editor.lockGroupCommand();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}


			const char* getLabel() const override { return label; }

			PropertyGrid* property_grid;
			AssetBrowser* asset_browser;
			ComponentType type;
			ResourceType resource_type;
			StaticString<64> property;
			char label[50];
		};

		Plugin* plugin = LUMIX_NEW(m_allocator, Plugin);
		plugin->property_grid = m_property_grid.get();
		plugin->asset_browser = m_asset_browser.get();
		plugin->type = cmp_type;
		plugin->property = property;
		plugin->resource_type = resource_type;
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, String(label, m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(plugin->type, icon);
		}
	}

	void registerComponent(const char* icon, const char* id, IAddComponentPlugin& plugin) override {
		addPlugin(plugin);
		m_component_labels.insert(reflection::getComponentType(id), String(plugin.getLabel(), m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(reflection::getComponentType(id), icon);
		}
	}

	void registerComponent(const char* icon, ComponentType type, const char* label)
	{
		struct Plugin final : IAddComponentPlugin
		{
			void onGUI(bool create_entity, bool from_filter, EntityPtr parent, WorldEditor& editor) override
			{
				const char* last = reverseFind(label, '/');
				last = last && !from_filter ? last + 1 : label;
				if (last[0] == ' ') ++last;
				if (ImGui::MenuItem(last))
				{
					editor.beginCommandGroup("createEntityWithComponent");
					if (create_entity)
					{
						EntityRef entity = editor.addEntity();
						editor.selectEntities(Span(&entity, 1), false);
					}

					const Array<EntityRef>& selected = editor.getSelectedEntities();
					editor.addComponent(selected, type);
					if (parent.isValid()) editor.makeParent(parent, selected[0]);
					editor.endCommandGroup();
					editor.lockGroupCommand();
				}
			}

			const char* getLabel() const override { return label; }

			PropertyGrid* property_grid;
			ComponentType type;
			char label[64];
		};

		Plugin* plugin = LUMIX_NEW(m_allocator, Plugin);
		plugin->property_grid = m_property_grid.get();
		plugin->type = type;
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, String(label, m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(plugin->type, icon);
		}
	}


	const Array<Action*>& getActions() override { return m_actions; }


	void guiBeginFrame()
	{
		PROFILE_FUNCTION();

		ImGuiIO& io = ImGui::GetIO();

		updateIMGUIMonitors();
		const os::Point client_size = os::getWindowClientSize(m_main_window);
		if (client_size.x > 0 && client_size.y > 0) {
			io.DisplaySize = ImVec2(float(client_size.x), float(client_size.y));
		}
		else if(io.DisplaySize.x <= 0) {
			io.DisplaySize.x = 800;
			io.DisplaySize.y = 600;
		}
		io.DeltaTime = m_engine->getLastTimeDelta();

		if (!m_cursor_clipped) {
			const os::Point cp = os::getMouseScreenPos();
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
				io.AddMousePosEvent((float)cp.x, (float)cp.y);
			}
			else {
				const os::Rect screen_rect = os::getWindowScreenRect(m_main_window);
				io.AddMousePosEvent((float)cp.x - screen_rect.left, (float)cp.y - screen_rect.top);
			}
		}

		const ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
		ImGui::NewFrame();
		if (!m_cursor_clipped) {
			static ImGuiMouseCursor last_cursor = ImGuiMouseCursor_COUNT;
			if (imgui_cursor != last_cursor) {
				switch (imgui_cursor) {
					case ImGuiMouseCursor_Arrow: os::setCursor(os::CursorType::DEFAULT); break;
					case ImGuiMouseCursor_ResizeNS: os::setCursor(os::CursorType::SIZE_NS); break;
					case ImGuiMouseCursor_ResizeEW: os::setCursor(os::CursorType::SIZE_WE); break;
					case ImGuiMouseCursor_ResizeNWSE: os::setCursor(os::CursorType::SIZE_NWSE); break;
					case ImGuiMouseCursor_TextInput: os::setCursor(os::CursorType::TEXT_INPUT); break;
					case ImGuiMouseCursor_Hand: os::setCursor(os::CursorType::HAND); break;
					default: os::setCursor(os::CursorType::DEFAULT); break;
				}
				last_cursor = imgui_cursor;
			}
		}
		ImGui::PushFont(m_font);

		if (os::getFocused() != m_main_window && m_cursor_clipped) unclipMouseCursor();
	}

	u32 getDockspaceID() const override {
		return m_dockspace_id;
	}

	void guiEndFrame()
	{
		PROFILE_FUNCTION();
		if (m_is_welcome_screen_open) {
			m_dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
			guiWelcomeScreen();
		}
		else {
			mainMenu();

			m_dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
			m_asset_compiler->onGUI();
			guiAllActions();
			guiSaveAsDialog();
			for (i32 i = m_gui_plugins.size() - 1; i >= 0; --i) {
				GUIPlugin* win = m_gui_plugins[i];
				win->onGUI();
			}

			m_settings.gui();
			guiExportData();
		}
		ImGui::PopFont();
		ImGui::Render();
		ImGui::UpdatePlatformWindows();

		for (auto* plugin : m_gui_plugins)
		{
			plugin->guiEndFrame();
		}
	}

	void showGizmos() {
		const Array<EntityRef>& ents = m_editor->getSelectedEntities();
		if (ents.empty()) return;

		World* world = m_editor->getWorld();

		WorldView& view = m_editor->getView();
		if (ents.size() > 1) {
			DVec3 min(FLT_MAX), max(-FLT_MAX);
			for (EntityRef e : ents) {
				const DVec3 p = world->getPosition(e);
				min = minimum(p, min);
				max = maximum(p, max);
			}

			addCube(view, min, max, 0xffffff00);
			return;
		}

		for (ComponentUID cmp = world->getFirstComponent(ents[0]); cmp.isValid(); cmp = world->getNextComponent(cmp)) {
			for (auto* plugin : m_plugins) {
				if (plugin->showGizmo(view, cmp)) break;
			}
		}
	}

	void updateGizmoOffset() {
		const Array<EntityRef>& ents = m_editor->getSelectedEntities();
		if (ents.size() != 1) {
			m_gizmo_config.offset = Vec3::ZERO;
			return;
		}
		static EntityPtr last_selected = INVALID_ENTITY;
		if (last_selected != ents[0]) {
			m_gizmo_config.offset = Vec3::ZERO;
			last_selected = ents[0];
		}
	}

	void endCustomTick() override {
		for (auto* plugin : m_gui_plugins) plugin->guiEndFrame();
	}

	void endCustomTicking() override {
		guiBeginFrame();
	}

	void beginCustomTicking() override {
		for (auto* plugin : m_gui_plugins) plugin->guiEndFrame();
	}

	void beginCustomTick() override {
		os::Event e;
		while (os::getEvent(e)) {
			// pass events to imgui
			onEvent(e);
			m_events.clear();
		}

		processDeferredWindowDestroy();
		guiBeginFrame();
	}

	void processDeferredWindowDestroy() {
		for (i32 i = m_deferred_destroy_windows.size() - 1; i >= 0; --i) {
			--m_deferred_destroy_windows[i].counter;
			if (m_deferred_destroy_windows[i].counter == 0) {
				os::destroyWindow(m_deferred_destroy_windows[i].window);
				m_deferred_destroy_windows.swapAndPop(i);
			}
		}
	}

	void extractBundled() {
		#ifdef _WIN32
			HRSRC hrsrc = FindResourceA(GetModuleHandle(NULL), MAKEINTRESOURCE(102), "TAR");
			if (!hrsrc) return;
			HGLOBAL hglobal = LoadResource(GetModuleHandle(NULL), hrsrc);
			if (!hglobal) return;
			const DWORD res_size = SizeofResource(GetModuleHandle(NULL), hrsrc);
			if (res_size == 0) return;
			const void* res_mem = LockResource(hglobal);
			if (!res_mem) return;
	
			TCHAR exe_path[MAX_PATH];
			GetModuleFileNameA(NULL, exe_path, MAX_PATH);

			// TODO extract only nonexistent files
			InputMemoryStream str(res_mem, res_size);

			TarHeader header;
			while (str.getPosition() < str.size()) {
				const u8* ptr = (const u8*)str.getData() + str.getPosition();
				str.read(&header, sizeof(header));
				u32 size;
				fromCStringOctal(StringView(header.size, sizeof(header.size)), size); 
				if (header.name[0] && (header.typeflag == 0 || header.typeflag == '0')) {
					const Path path(m_engine->getFileSystem().getBasePath(), "/", header.name);
					char dir[MAX_PATH];
					copyString(Span(dir), Path::getDir(path));
					if (!os::makePath(dir)) logError("");
					if (!os::fileExists(path)) {
						os::OutputFile file;
						if (file.open(path.c_str())) {
							if (!file.write(ptr + 512, size)) {
								logError("Failed to write ", path);
							}
							file.close();
						}
						else {
							logError("Failed to extract ", path);
						}
					}
				}

				str.setPosition(str.getPosition() + (512 - str.getPosition() % 512) % 512);
				str.setPosition(str.getPosition() + size + (512 - size % 512) % 512);
			}
		#endif
	}


	void initDefaultWorld() {
		m_editor->beginCommandGroup("initWorld");
		EntityRef env = m_editor->addEntity();
		m_editor->setEntityName(env, "environment");
		ComponentType env_cmp_type = reflection::getComponentType("environment");
		Span<EntityRef> entities(&env, 1);
		m_editor->addComponent(entities, env_cmp_type);
		Quat rot;
		rot.fromEuler(Vec3(degreesToRadians(45.f), 0, 0));
		m_editor->setEntitiesRotations(&env, &rot, 1);
	}

	void tryLoadWorld(const Path& path, bool additive) override {
		if (!additive && m_editor->isWorldChanged()) {
			m_world_to_load = path;
			m_confirm_load = true;
		}
		else {
			loadWorld(path, additive);
		}
	}


	void loadWorld(const Path& path, bool additive) {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);

		if (!fs.getContentSync(path, data)) {
			logError("Failed to read ", path);
			m_editor->newWorld();
			return;
		}

		InputMemoryStream blob(data); 
		m_editor->loadWorld(blob, path.c_str(), additive);
	}

	void guiWelcomeScreen()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		if (ImGui::Begin("Welcome", nullptr, flags)) {
			#ifdef _WIN32
				const ImVec2 cp = ImGui::GetCursorPos();
				ImGui::InvisibleButton("titlebardrag", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), ImGuiButtonFlags_AllowOverlap);
				m_is_caption_hovered = ImGui::IsItemHovered();
				ImGui::SetCursorPos(cp);
				alignGUIRight([&](){
					if (ImGuiEx::IconButton(ICON_FA_WINDOW_MINIMIZE, nullptr)) os::minimizeWindow(m_main_window);
					ImGui::SameLine();
					if (os::isMaximized(m_main_window)) {
						if (ImGuiEx::IconButton(ICON_FA_WINDOW_RESTORE, nullptr)) os::restore(m_main_window);
					}
					else {
						if (ImGuiEx::IconButton(ICON_FA_WINDOW_MAXIMIZE, nullptr)) os::maximizeWindow(m_main_window);
					}
					ImGui::SameLine();
					if (ImGuiEx::IconButton(ICON_FA_WINDOW_CLOSE, nullptr)) exit();
				});
			#endif

			ImGui::Text("Welcome to Lumix Studio");

			ImVec2 half_size = ImGui::GetContentRegionAvail();
			half_size.x = half_size.x * 0.5f - ImGui::GetStyle().FramePadding.x;
			half_size.y *= 0.99f;
			auto right_pos = ImGui::GetCursorPos();
			right_pos.x += half_size.x + ImGui::GetStyle().FramePadding.x;
			if (ImGui::BeginChild("left", half_size, true))
			{
				ImGui::Text("Working directory: %s", m_engine->getFileSystem().getBasePath());
				ImGui::SameLine();
				if (ImGui::Button("Change...")) {
					char dir[MAX_PATH];
					if (os::getOpenDirectory(Span(dir), m_engine->getFileSystem().getBasePath())) {
						os::OutputFile cfg_file;
						if (cfg_file.open(".lumixuser")) {
							cfg_file << dir;
							cfg_file.close();
						}
						m_engine->getFileSystem().setBasePath(dir);
						extractBundled();
						m_editor->loadProject();
						m_asset_compiler->onBasePathChanged();
						m_engine->getResourceManager().reloadAll();
					}
				}
				ImGui::Separator();
				if (ImGui::Button("New world")) {
					initDefaultWorld();
					m_is_welcome_screen_open = false;
				}
				ImGui::Text("Open world:");
				ImGui::Indent();
				forEachWorld([&](const Path& path){
					if (ImGui::MenuItem(path.c_str())) {
						loadWorld(path, false);
						m_is_welcome_screen_open = false;
					}
				});
				ImGui::Unindent();
			}
			ImGui::EndChild();

			ImGui::SetCursorPos(right_pos);

			if (ImGui::BeginChild("right", half_size, true))
			{
				ImGui::Text("Using NVidia PhysX");

				if (ImGui::Button("Wiki"))
				{
					os::shellExecuteOpen("https://github.com/nem0/LumixEngine/wiki", {}, {});
				}

				if (ImGui::Button("Show major releases"))
				{
					os::shellExecuteOpen("https://github.com/nem0/LumixEngine/releases", {}, {});
				}

				if (ImGui::Button("Show latest commits"))
				{
					os::shellExecuteOpen("https://github.com/nem0/LumixEngine/commits/master", {}, {});
				}

				if (ImGui::Button("Show issues"))
				{
					os::shellExecuteOpen("https://github.com/nem0/lumixengine/issues", {}, {});
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}

	void save() {
		if (m_editor->isGameMode()) {
			logError("Could not save while the game is running");
			return;
		}

		World* world = m_editor->getWorld();
		const Array<World::Partition>& partitions = world->getPartitions();
		
		if (partitions.size() == 1 && partitions[0].name[0] == '\0') {
			saveAs();
		}
		else {
			for (const World::Partition& partition : partitions) {
				m_editor->savePartition(partition.handle);
			}
		}
	}

	void guiSaveAsDialog() {
		if (m_file_selector.gui("Save world as", &m_show_save_world_ui, "unv", true)) {
			ASSERT(!m_editor->isGameMode());
			World* world = m_editor->getWorld();
			World::PartitionHandle active_partition_handle = world->getActivePartition();
			World::Partition& active_partition = world->getPartition(active_partition_handle);
			copyString(active_partition.name, m_file_selector.getPath());
			m_editor->savePartition(active_partition_handle);
		}
	}


	void saveAs() {
		if (m_editor->isGameMode()) {
			logError("Can not save while the game is running");
			return;
		}

		m_show_save_world_ui = true;
	}

	void exit() {
		if (m_editor->isWorldChanged()) {
			m_confirm_exit = true;
		}
		else {
			m_finished = true;
		}
	}

	void newWorld() override {
		if (m_editor->isWorldChanged()) {
			m_confirm_new = true;
		}
		else {
			m_editor->newWorld();
			initDefaultWorld();
		}
	}

	GUIPlugin* getFocusedWindow() {
		for (GUIPlugin* win : m_gui_plugins) {
			if (win->hasFocus()) return win;
		}
		return nullptr;
	}

	Gizmo::Config& getGizmoConfig() override { return m_gizmo_config; }
	
	void clipMouseCursor() override { m_cursor_clipped = true; }
	void unclipMouseCursor() override {
		os::clipCursor(os::INVALID_WINDOW, {});
		m_cursor_clipped = false;
	}
	bool isMouseCursorClipped() const override {return m_cursor_clipped; }

	void setMouseClipRect(os::WindowHandle win, const os::Rect &screen_rect) override {
		if (!m_cursor_clipped) return;
		os::clipCursor(win, screen_rect);
	}

	void addEntity() {
		const EntityRef e = m_editor->addEntity();
		m_editor->selectEntities(Span(&e, 1), false);
	}

	void undo() { m_editor->undo(); }
	void redo() { m_editor->redo(); }
	void copy() { m_editor->copyEntities(); }
	void paste() { m_editor->pasteEntities(); }
	void duplicate() { m_editor->duplicateEntities(); }
	void setLocalCoordSystem() { getGizmoConfig().coord_system = Gizmo::Config::LOCAL; }
	void setGlobalCoordSystem() { getGizmoConfig().coord_system = Gizmo::Config::GLOBAL; }
	void toggleSettings() { m_settings.m_is_open = !m_settings.m_is_open; }
	bool areSettingsOpen() const { return m_settings.m_is_open; }
	void toggleEntityList() { m_hierarchy->m_is_open = !m_hierarchy->m_is_open; }
	bool isEntityListOpen() const { return m_hierarchy->m_is_open; }
	int getExitCode() const override { return m_exit_code; }
	
	DirSelector& getDirSelector() override {
		return m_dir_selector;
	}

	FileSelector& getFileSelector() override {
		return m_file_selector;
	}
	
	AssetBrowser& getAssetBrowser() override
	{
		ASSERT(m_asset_browser.get());
		return *m_asset_browser;
	}
	AssetCompiler& getAssetCompiler() override
	{
		ASSERT(m_asset_compiler.get());
		return *m_asset_compiler;
	}
	PropertyGrid& getPropertyGrid() override
	{
		ASSERT(m_property_grid.get());
		return *m_property_grid;
	}
	LogUI& getLogUI() override
	{
		ASSERT(m_log_ui.get());
		return *m_log_ui;
	}

	void nextFrame() { m_engine->nextFrame(); }
	void pauseGame() { m_engine->pause(!m_engine->isPaused()); }
	void toggleGameMode() { m_editor->toggleGameMode(); }
	void setTranslateGizmoMode() { getGizmoConfig().mode = Gizmo::Config::TRANSLATE; }
	void setRotateGizmoMode() { getGizmoConfig().mode = Gizmo::Config::ROTATE; }
	void setScaleGizmoMode() { getGizmoConfig().mode = Gizmo::Config::SCALE; }


	void makeParent()
	{
		const auto& entities = m_editor->getSelectedEntities();
		if (entities.size() == 2) {
			m_editor->makeParent(entities[0], entities[1]);
		}
	}


	void unparent()
	{
		const auto& entities = m_editor->getSelectedEntities();
		if (entities.size() != 1) return;
		m_editor->makeParent(INVALID_ENTITY, entities[0]);
	}


	void snapDown() override {
		const Array<EntityRef>& selected = m_editor->getSelectedEntities();
		if (selected.empty()) return;

		Array<DVec3> new_positions(m_allocator);
		World* world = m_editor->getWorld();

		for (EntityRef entity : selected) {
			const DVec3 origin = world->getPosition(entity);
			auto hit = getRenderInterface()->castRay(*world, Ray{origin, Vec3(0, -1, 0)}, entity);
			if (hit.is_hit) {
				new_positions.push(origin + Vec3(0, -hit.t, 0));
			}
			else {
				hit = getRenderInterface()->castRay(*world, Ray{origin, Vec3(0, 1, 0)}, entity);
				if (hit.is_hit) {
					new_positions.push(origin + Vec3(0, hit.t, 0));
				}
				else {
					new_positions.push(world->getPosition(entity));
				}
			}
		}
		m_editor->setEntitiesPositions(&selected[0], &new_positions[0], new_positions.size());
	}

	void autosnapDown()
	{
		Gizmo::Config& cfg = getGizmoConfig();
		cfg.setAutosnapDown(!cfg.isAutosnapDown());
	}


	void destroySelectedEntity()
	{
		auto& selected_entities = m_editor->getSelectedEntities();
		if (selected_entities.empty()) return;
		m_editor->destroyEntities(&selected_entities[0], selected_entities.size());
	}


	void removeAction(Action* action) override
	{
		m_actions.eraseItem(action);
		m_window_actions.eraseItem(action);
		m_tools_actions.eraseItem(action);
	}

	void addToolAction(Action* action) override {
		addAction(action);
		m_tools_actions.push(action);
	}

	void addWindowAction(Action* action) override
	{
		addAction(action);
		for (int i = 0; i < m_window_actions.size(); ++i)
		{
			if (compareString(m_window_actions[i]->label_short, action->label_short) > 0)
			{
				m_window_actions.insert(i, action);
				return;
			}
		}
		m_window_actions.push(action);
	}

	void addAction(Action* action) override {
		for (int i = 0; i < m_actions.size(); ++i) {
			if (compareString(m_actions[i]->label_long, action->label_long) > 0) {
				m_actions.insert(i, action);
				return;
			}
		}
		m_actions.push(action);
	}

	template <void (StudioAppImpl::*Func)()>
	Action& addAction(const char* label_short, const char* label_long, const char* name, const char* font_icon = "")
	{
		Action* a = LUMIX_NEW(m_allocator, Action);
		a->init(label_short, label_long, name, font_icon, Action::IMGUI_PRIORITY);
		a->func.bind<Func>(this);
		addAction(a);
		m_owned_actions.push(a);
		return *a;
	}


	template <void (StudioAppImpl::*Func)()>
	void addAction(const char* label_short,
		const char* label_long,
		const char* name,
		const char* font_icon,
		os::Keycode shortcut,
		Action::Modifiers modifiers)
	{
		Action* a = LUMIX_NEW(m_allocator, Action);
		a->init(label_short, label_long, name, font_icon, shortcut, modifiers, Action::IMGUI_PRIORITY);
		a->func.bind<Func>(this);
		m_owned_actions.push(a);
		addAction(a);
	}


	Action* getAction(const char* name) override
	{
		for (auto* a : m_actions)
		{
			if (equalStrings(a->name, name)) return a;
		}
		return nullptr;
	}


	static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const TextFilter& filter, EntityPtr parent, WorldEditor& editor)
	{
		if (!node) return;

		if (filter.isActive()) {
			if (!node->plugin) showAddComponentNode(node->child, filter, parent, editor);
			else if (filter.pass(node->plugin->getLabel())) node->plugin->onGUI(true, true, parent, editor);
			showAddComponentNode(node->next, filter, parent, editor);
			return;
		}

		if (node->plugin) {
			node->plugin->onGUI(true, false, parent, editor);
			showAddComponentNode(node->next, filter, parent, editor);
			return;
		}

		const char* last = reverseFind(node->label, '/');
		last = last ? last + 1 : node->label;
		if (last[0] == ' ') ++last;
		if (ImGui::BeginMenu(last)) {
			showAddComponentNode(node->child, filter, parent, editor);
			ImGui::EndMenu();
		}
		showAddComponentNode(node->next, filter, parent, editor);
	}


	void onCreateEntityWithComponentGUI(EntityPtr parent)
	{
		char shortcut[64] = "";
		const Action* create_entity_action = getAction("createEntity");
		if (create_entity_action) getShortcut(*create_entity_action, Span(shortcut));
		
		if (ImGui::MenuItem("Create empty", shortcut)) {
			m_editor->beginCommandGroup("create_child");
			const EntityRef e = m_editor->addEntity();
			m_editor->selectEntities(Span(&e, 1), false);
			if (parent.isValid()) m_editor->makeParent(parent, e);
			m_editor->endCommandGroup();
		}

		m_component_filter.gui("Filter", 150);
		
		showAddComponentNode(m_add_cmp_root.child, m_component_filter, parent, *m_editor);
	}


	void entityMenu()
	{
		if (!ImGui::BeginMenu("Entity")) return;

		const auto& selected_entities = m_editor->getSelectedEntities();
		bool is_any_entity_selected = !selected_entities.empty();
		if (ImGuiEx::BeginMenuEx("Create", ICON_FA_PLUS_SQUARE))
		{
			onCreateEntityWithComponentGUI(INVALID_ENTITY);
			ImGui::EndMenu();
		}
		menuItem("delete", is_any_entity_selected);
		
		if (ImGui::BeginMenu("Save as prefab", selected_entities.size() == 1)) {
			bool selected = m_file_selector.gui(false, "fab");
			selected = ImGui::Button(ICON_FA_SAVE " Save") || selected;
			if (selected) {
				char filename[MAX_PATH];
				Path::normalize(m_file_selector.getPath(), filename);
				EntityRef entity = selected_entities[0];
				m_editor->getPrefabSystem().savePrefab(entity, Path(filename));
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}
		
		menuItem("makeParent", selected_entities.size() == 2);
		bool can_unparent = selected_entities.size() == 1 && m_editor->getWorld()->getParent(selected_entities[0]).isValid();
		menuItem("unparent", can_unparent);
		ImGui::EndMenu();
	}

	void menuItem(const char* name, bool enabled) {
		Action* action = getAction(name);
		if (!action) {
			ASSERT(false);
			return;
		}
		if (Lumix::menuItem(*action, enabled)) action->func.invoke();
	}

	void editMenu()
	{
		if (!ImGui::BeginMenu("Edit")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		menuItem("undo", m_editor->canUndo());
		menuItem("redo", m_editor->canRedo());
		ImGui::Separator();
		menuItem("copy", is_any_entity_selected);
		menuItem("paste", m_editor->canPasteEntities());
		menuItem("duplicate", is_any_entity_selected);
		ImGui::Separator();
		menuItem("setTranslateGizmoMode", true);
		menuItem("setRotateGizmoMode", true);
		menuItem("setScaleGizmoMode", true);
		menuItem("setLocalCoordSystem", true);
		menuItem("setGlobalCoordSystem", true);
		if (ImGuiEx::BeginMenuEx("View", ICON_FA_CAMERA, true))
		{
			menuItem("toggleProjection", true);
			menuItem("viewTop", true);
			menuItem("viewFront", true);
			menuItem("viewSide", true);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	template <typename T>
	void forEachWorld(const T& f) {
		const HashMap<FilePathHash, AssetCompiler::ResourceItem>& resources = m_asset_compiler->lockResources();
		ResourceType WORLD_TYPE("world");
		for (const AssetCompiler::ResourceItem& ri : resources) {
			if (ri.type != WORLD_TYPE) continue;
			f(ri.path);
		}
		m_asset_compiler->unlockResources();
	}


	void fileMenu()
	{
		if (!ImGui::BeginMenu("File")) return;

		menuItem("newWorld", true);
		const Array<World::Partition>& partitions = m_editor->getWorld()->getPartitions();
		auto open_ui = [&](const char* label, bool additive){
			if (ImGui::BeginMenu(label)) {
				m_open_filter.gui("Filter", 150);
	
				forEachWorld([&](const Path& path){
					StringView basename = Path::getBasename(path);
					StaticString<MAX_PATH> tmp(basename);
					if (m_open_filter.pass(path.c_str()) && ImGui::MenuItem(tmp)) {
						tryLoadWorld(path, additive);
					}
				});
				ImGui::EndMenu();
			}
		};
		open_ui("Open", false);
		const bool can_load_additive = partitions.size() != 1 || partitions[0].name[0] != '\0';
		if (can_load_additive) {
			open_ui("Open additive", true);
		}
		else {
			if (ImGui::BeginMenu("Open additive")) {
				ImGui::TextUnformatted("Please save current partition first");
				ImGui::EndMenu();
			}
		}
		menuItem("save", !m_editor->isGameMode());
		menuItem("saveAs", !m_editor->isGameMode());
		menuItem("exit", true);
		ImGui::EndMenu();
	}


	void toolsMenu()
	{
		if (!ImGui::BeginMenu("Tools")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		menuItem("focus_asset_search", true);
		menuItem("snapDown", is_any_entity_selected);
		menuItem("autosnapDown", true);
		menuItem("export_game", true);
		for (Action* action : m_tools_actions) {
			if (Lumix::menuItem(*action, true)) {
				action->func.invoke();
			}
		}
		ImGui::EndMenu();
	}

	void viewMenu() {
		if (!ImGui::BeginMenu("View")) return;

		menuItem("entityList", true);
		menuItem("settings", true);
		ImGui::Separator();
		for (Action* action : m_window_actions) {
			if (Lumix::menuItem(*action, true)) action->func.invoke();
		}
		ImGui::EndMenu();
	}

	void mainMenu()
	{
		if (m_confirm_exit) {
			openCenterStrip("Confirm##confirm_exit");
			m_confirm_exit = false;
		}
		if (beginCenterStrip("Confirm##confirm_exit", 6)) {
			ImGui::NewLine();
			ImGuiEx::TextCentered("All unsaved changes will be lost, do you want to continue?");
			ImGui::NewLine();
			alignGUICenter([&](){
				if (ImGui::Button("Continue")) {
					m_finished = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			});
			endCenterStrip();
		}

		if (m_confirm_new) {
			openCenterStrip("Confirm##confirm_new");
			m_confirm_new = false;
		}
		if (beginCenterStrip("Confirm##confirm_new", 6)) {
			ImGui::NewLine();
			ImGuiEx::TextCentered("All unsaved changes will be lost, do you want to continue?");
			ImGui::NewLine();
			alignGUICenter([&](){
				if (ImGui::Button("Continue")) {
					m_editor->newWorld();
					initDefaultWorld();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			});
			endCenterStrip();
		}

		if (m_confirm_load) {
			openCenterStrip("Confirm");
			m_confirm_load = false;
		}
		if (beginCenterStrip("Confirm", 6)) {
			ImGui::NewLine();
			ImGuiEx::TextCentered("All unsaved changes will be lost, do you want to continue?");
			ImGui::NewLine();
			alignGUICenter([&](){
				if (ImGui::Button("Continue")) {
					loadWorld(m_world_to_load, false);
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			});
			endCenterStrip();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 8));
		
		if (ImGui::BeginMainMenuBar()) {
			if(m_render_interface && m_render_interface->isValid(m_logo)) {
				ImGui::Image(*(void**)m_logo, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
			}
			
			ImGui::PopStyleVar(2);
			const ImVec2 menu_min = ImGui::GetCursorPos();
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("titlebardrag", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
			m_is_caption_hovered = ImGui::IsItemHovered();
			ImGui::SetCursorPos(menu_min);
			fileMenu();
			editMenu();
			entityMenu();
			toolsMenu();
			viewMenu();

			float w = (ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x) * 0.5f - 30 - ImGui::GetCursorPosX();
			ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeightWithSpacing()));
			getAction("toggleGameMode")->toolbarButton(m_big_icon_font);
			getAction("pauseGameMode")->toolbarButton(m_big_icon_font);
			getAction("nextFrame")->toolbarButton(m_big_icon_font);

			// we don't have custom titlebar on linux
			#ifdef _WIN32
				alignGUIRight([&](){
					StaticString<200> stats;
					if (m_engine->getFileSystem().hasWork()) stats.append(ICON_FA_HOURGLASS_HALF "Loading... | ");
					stats.append("FPS: ", u32(m_fps + 0.5f));
					if (m_frames_since_focused > 10) stats.append(" - inactive window");
					ImGuiEx::TextUnformatted(stats);

					if (m_log_ui->getUnreadErrorCount() == 1) {
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1, 0, 0, 1), ICON_FA_EXCLAMATION_TRIANGLE "1 error | ");
					}
					else if (m_log_ui->getUnreadErrorCount() > 1)
					{
						StaticString<50> error_stats(ICON_FA_EXCLAMATION_TRIANGLE, m_log_ui->getUnreadErrorCount(), " errors | ");
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", (const char*)error_stats);
					}

					ImGui::SameLine();
					if (ImGuiEx::IconButton(ICON_FA_WINDOW_MINIMIZE, nullptr)) os::minimizeWindow(m_main_window);
					ImGui::SameLine();
					if (os::isMaximized(m_main_window)) {
						if (ImGuiEx::IconButton(ICON_FA_WINDOW_RESTORE, nullptr)) os::restore(m_main_window);
					}
					else {
						if (ImGuiEx::IconButton(ICON_FA_WINDOW_MAXIMIZE, nullptr)) os::maximizeWindow(m_main_window);
					}
					ImGui::SameLine();
					if (ImGuiEx::IconButton(ICON_FA_WINDOW_CLOSE, nullptr)) exit();
				});
			#endif

			ImGui::EndMainMenuBar();
		}
	}

	void setFullscreen(bool fullscreen) override
	{
		if (fullscreen) {
			m_fullscreen_restore_state = os::setFullscreen(m_main_window);
		}
		else {
			os::restore(m_main_window, m_fullscreen_restore_state);
		}
	}

	void saveSettings() override {
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantSaveIniSettings) {
			const char* data = ImGui::SaveIniSettingsToMemory();
			m_settings.m_imgui_state = data;
			io.WantSaveIniSettings = false;
		}

		m_settings.setBool("is_maximized", os::isMaximized(m_main_window), Settings::WORKSPACE);
		if (!os::isMinimized(m_main_window)) {
			os::Rect win_rect = os::getWindowScreenRect(m_main_window);
			m_settings.setI32("window_x", win_rect.left, Settings::WORKSPACE);
			m_settings.setI32("window_y", win_rect.top, Settings::WORKSPACE);
			m_settings.setI32("window_w", win_rect.width, Settings::WORKSPACE);
			m_settings.setI32("window_h", win_rect.height, Settings::WORKSPACE);
		}

		for (auto* i : m_gui_plugins) {
			i->onBeforeSettingsSaved();
		}

		m_settings.save();
	}

	ImFont* addFontFromFile(const char* path, float size, bool merge_icons) {
		PROFILE_FUNCTION();
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path(path), data)) return nullptr;
		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg;
		copyString(cfg.Name, path);
		cfg.FontDataOwnedByAtlas = false;
		auto font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg);
		if (merge_icons) {
			ImFontConfig config;
			copyString(config.Name, "editor/fonts/fa-regular-400.ttf");
			config.MergeMode = true;
			config.FontDataOwnedByAtlas = false;
			config.GlyphMinAdvanceX = size; // Use if you want to make the icon monospaced
			static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
			OutputMemoryStream icons_data(m_allocator);
			if (fs.getContentSync(Path("editor/fonts/fa-regular-400.ttf"), icons_data)) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)icons_data.data(), (i32)icons_data.size(), size * 0.75f, &config, icon_ranges);
				ASSERT(icons_font);
			}
			copyString(config.Name, "editor/fonts/fa-solid-900.ttf");
			icons_data.clear();
			if (fs.getContentSync(Path("editor/fonts/fa-solid-900.ttf"), icons_data)) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)icons_data.data(), (i32)icons_data.size(), size * 0.75f, &config, icon_ranges);
				ASSERT(icons_font);
			}
		}

		return font;
	}

	void initIMGUIPlatformIO() {
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		static StudioAppImpl* that = this;
		ASSERT(that == this);
		pio.Platform_CreateWindow = [](ImGuiViewport* vp){
			os::InitWindowArgs args = {};
			args.flags = os::InitWindowArgs::NO_DECORATION | os::InitWindowArgs::NO_TASKBAR_ICON;
			ImGuiViewport* parent = ImGui::FindViewportByID(vp->ParentViewportId);
			args.parent = parent ? parent->PlatformHandle : os::INVALID_WINDOW;
			args.name = "child";
			//args.hit_test_callback = &StudioAppImpl::childHitTestCallback;
			vp->PlatformHandle = os::createWindow(args);
			that->m_windows.push(vp->PlatformHandle);

			ASSERT(vp->PlatformHandle != os::INVALID_WINDOW);
		};
		pio.Platform_DestroyWindow = [](ImGuiViewport* vp){
			os::WindowHandle w = (os::WindowHandle)vp->PlatformHandle;
			that->m_deferred_destroy_windows.push({w, 8});
			vp->PlatformHandle = nullptr;
			vp->PlatformUserData = nullptr;
			that->m_windows.eraseItem(w);
		};
		pio.Platform_ShowWindow = [](ImGuiViewport* vp){};
		pio.Platform_SetWindowPos = [](ImGuiViewport* vp, ImVec2 pos) {
			const os::WindowHandle h = vp->PlatformHandle;
			os::Rect r = os::getWindowScreenRect(h);
			r.left = int(pos.x);
			r.top = int(pos.y);
			os::setWindowScreenRect(h, r);
		};
		pio.Platform_GetWindowPos = [](ImGuiViewport* vp) -> ImVec2 {
			os::WindowHandle win = (os::WindowHandle)vp->PlatformHandle;
			const os::Point p = os::clientToScreen(win, 0, 0);
			return {(float)p.x, (float)p.y};

		};
		pio.Platform_SetWindowSize = [](ImGuiViewport* vp, ImVec2 pos) {
			const os::WindowHandle h = vp->PlatformHandle;
			os::Rect r = os::getWindowScreenRect(h);
			r.width = int(pos.x);
			r.height = int(pos.y);
			os::setWindowScreenRect(h, r);
		};
		pio.Platform_GetWindowSize = [](ImGuiViewport* vp) -> ImVec2 {
			const os::Point client_size = os::getWindowClientSize((os::WindowHandle)vp->PlatformHandle);
			return {(float)client_size.x, (float)client_size.y};
		};
		pio.Platform_SetWindowTitle = [](ImGuiViewport* vp, const char* title){
			os::setWindowTitle((os::WindowHandle)vp->PlatformHandle, title);
		};
		pio.Platform_GetWindowFocus = [](ImGuiViewport* vp){
			return os::getFocused() == vp->PlatformHandle;
		};
		pio.Platform_SetWindowFocus = nullptr;

		ImGuiViewport* mvp = ImGui::GetMainViewport();
		mvp->PlatformHandle = m_main_window;
		mvp->DpiScale = 1;
		mvp->PlatformUserData = (void*)1;

		updateIMGUIMonitors();
	}

	static void updateIMGUIMonitors() {
		os::Monitor monitors[32];
		const u32 monitor_count = os::getMonitors(Span(monitors));
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		pio.Monitors.resize(0);
		for (u32 i = 0; i < monitor_count; ++i) {
			const os::Monitor& m = monitors[i];
			ImGuiPlatformMonitor im;
			im.MainPos = ImVec2((float)m.monitor_rect.left, (float)m.monitor_rect.top);
			im.MainSize = ImVec2((float)m.monitor_rect.width, (float)m.monitor_rect.height);
			im.WorkPos = ImVec2((float)m.work_rect.left, (float)m.work_rect.top);
			im.WorkSize = ImVec2((float)m.work_rect.width, (float)m.work_rect.height);

			if (m.primary) {
				pio.Monitors.push_front(im);
			}
			else {
				pio.Monitors.push_back(im);
			}
		}
	}

	void beginInitIMGUI() {
		PROFILE_FUNCTION();
		ImGui::SetAllocatorFunctions(imguiAlloc, imguiFree, this);
		ImGui::CreateContext();

		jobs::runLambda([this](){
			PROFILE_BLOCK("init imgui");
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			#ifdef __linux__ 
				io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
				io.BackendFlags = ImGuiBackendFlags_HasMouseCursors;
			#else
				io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
				io.BackendFlags = ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports | ImGuiBackendFlags_HasMouseCursors;
			#endif

			initIMGUIPlatformIO();

			const int dpi = os::getDPI();
			float font_scale = dpi / 96.f;
			FileSystem& fs = m_engine->getFileSystem();
		
			ImGui::LoadIniSettingsFromMemory(m_settings.m_imgui_state.c_str());

			m_font = addFontFromFile("editor/fonts/notosans-regular.ttf", (float)m_font_size * font_scale, true);
			m_bold_font = addFontFromFile("editor/fonts/notosans-bold.ttf", (float)m_font_size * font_scale, true);
			m_monospace_font = addFontFromFile("editor/fonts/sourcecodepro-regular.ttf", (float)m_font_size * font_scale, false);

			OutputMemoryStream data(m_allocator);
			if (fs.getContentSync(Path("editor/fonts/fa-solid-900.ttf"), data)) {
				const float size = (float)m_font_size * font_scale * 1.25f;
				ImFontConfig cfg;
				copyString(cfg.Name, "editor/fonts/fa-solid-900.ttf");
				cfg.FontDataOwnedByAtlas = false;
				cfg.GlyphMinAdvanceX = size; // Use if you want to make the icon monospaced
				static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
				m_big_icon_font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg, icon_ranges);
				cfg.MergeMode = true;
				copyString(cfg.Name, "editor/fonts/fa-regular-400.ttf");
				if (fs.getContentSync(Path("editor/fonts/fa-regular-400.ttf"), data)) {
					ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg, icon_ranges);
					ASSERT(icons_font);
				}
			}

			if (!m_font || !m_bold_font) {
				os::messageBox(
					"Could not open editor/fonts/notosans-regular.ttf or editor/fonts/NotoSans-Bold.ttf\n"
					"It very likely means that data are not bundled with\n"
					"the exe and the exe is not in the correct directory.\n"
					"The program will eventually crash!"
				);
			}
			if (!m_monospace_font) logError("Failed to load monospace font");

			{
				PROFILE_BLOCK("build atlas");
				ImFontAtlas* atlas = ImGui::GetIO().Fonts;
				atlas->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
				atlas->FontBuilderFlags = 0;
				atlas->Build();
			}

			ImGuiStyle& style = ImGui::GetStyle();
			style.FramePadding.y = 0;
			style.ItemSpacing.y = 2;
			style.ItemInnerSpacing.x = 2;
		}, &m_init_imgui_signal);
	}

	void setRenderInterface(RenderInterface* ri) override { m_render_interface = ri; }
	RenderInterface* getRenderInterface() override { return m_render_interface; }

	float getFOV() const override { return m_fov; }
	void setFOV(float fov_radians) override { m_fov = fov_radians; }
	Settings& getSettings() override { return m_settings; }

	void loadSettings() {
		PROFILE_FUNCTION();
		logInfo("Loading settings...");

		m_settings.load();
		if (CommandLineParser::isOn("-no_crash_report")) enableCrashReporting(false);
		else enableCrashReporting(m_crash_reporting);

		for (auto* i : m_gui_plugins) {
			i->onSettingsLoaded();
		}

		if (m_settings.getBool("is_maximized", false)){
			os::maximizeWindow(m_main_window);
		}
		else {
			os::Rect r;
			r.width = m_settings.getI32("window_w", -1);
			if (r.width > 0) {
				r.left = m_settings.getI32("window_x", -1);
				r.top = m_settings.getI32("window_y", -1);
				r.height = m_settings.getI32("window_h", -1);
				os::setWindowScreenRect(m_main_window, r);
			}
		}
	}

	CommonActions& getCommonActions() override { return m_common_actions; }

	void startStandaloneApp() {
		if (m_editor->getWorld()->getPartitions()[0].name[0] == '\0') {
			logError("Save your scene before running it standalone");
			return;
		}

		char studio_exe_path[MAX_PATH];
		os::getExecutablePath(studio_exe_path);
		StringView dir = Path::getDir(studio_exe_path);
		#ifdef _WIN32
			StaticString<MAX_PATH> exe_path(dir, "app.exe");
		#else
			StaticString<MAX_PATH> exe_path(dir, "app");
		#endif
		const char* working_dir = m_engine->getFileSystem().getBasePath();
		StaticString<MAX_PATH + 64> args("-window -world ", m_editor->getWorld()->getPartitions()[0].name);
		if (os::shellExecuteOpen(exe_path, args, working_dir, false) != os::ExecuteOpenResult::SUCCESS) {
			logError("Failed to run ", exe_path, " ", args);
		}
	}

	void showAllActionsGUI() { m_show_all_actions_request = true; }

	void addActions()
	{
		m_common_actions.save.init("Save", "Save", "save", ICON_FA_SAVE, os::Keycode::S, Action::Modifiers::CTRL, Action::GLOBAL);
		m_common_actions.save.func.bind<&StudioAppImpl::save>(this);
		addAction(&m_common_actions.save);
		m_common_actions.undo.init("Undo", "Undo", "undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL, Action::IMGUI_PRIORITY);
		m_common_actions.undo.func.bind<&StudioAppImpl::undo>(this);
		addAction(&m_common_actions.undo);
		m_common_actions.redo.init("Redo", "Redo", "redo", ICON_FA_REDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, Action::IMGUI_PRIORITY);
		m_common_actions.redo.func.bind<&StudioAppImpl::redo>(this);
		addAction(&m_common_actions.redo);
		m_common_actions.del.init("Delete", "Delete", "delete", ICON_FA_MINUS_SQUARE, os::Keycode::DEL, Action::Modifiers::NONE, Action::IMGUI_PRIORITY);
		m_common_actions.del.func.bind<&StudioAppImpl::destroySelectedEntity>(this);
		addAction(&m_common_actions.del);

		m_common_actions.cam_orbit.init("Orbit", "Orbit with RMB", "orbitRMB", "", Action::LOCAL);
		addAction(&m_common_actions.cam_orbit);
		m_common_actions.cam_forward.init("Move forward", "Move camera forward", "moveForward", "", Action::LOCAL);
		addAction(&m_common_actions.cam_forward);
		m_common_actions.cam_backward.init("Move back", "Move camera back", "moveBack", "", Action::LOCAL);
		addAction(&m_common_actions.cam_backward);
		m_common_actions.cam_left.init("Move left", "Move camera left", "moveLeft", "", Action::LOCAL);
		addAction(&m_common_actions.cam_left);
		m_common_actions.cam_right.init("Move right", "Move camera right", "moveRight", "", Action::LOCAL);
		addAction(&m_common_actions.cam_right);
		m_common_actions.cam_up.init("Move up", "Move camera up", "moveUp", "", Action::LOCAL);
		addAction(&m_common_actions.cam_up);
		m_common_actions.cam_down.init("Move down", "Move camera down", "moveDown", "", Action::LOCAL);
		addAction(&m_common_actions.cam_down);
		m_common_actions.select_all.init("Select all", "Select all", "select_all", "", Action::IMGUI_PRIORITY);
		addAction(&m_common_actions.select_all);
		m_common_actions.rename.init("Rename", "Rename", "rename", "", os::Keycode::F2, Action::NONE, Action::IMGUI_PRIORITY);
		addAction(&m_common_actions.rename);

		m_show_all_actions_action.init("Show all actions", "Show all actions", "show_all_actions", "", os::Keycode::P, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, Action::Type::IMGUI_PRIORITY);
		m_show_all_actions_action.func.bind<&StudioAppImpl::showAllActionsGUI>(this);
		addAction(&m_show_all_actions_action);
		
		m_start_standalone_app.init("Start standalone app", "Start standalone app", "start_standalone_app", "", Action::Type::IMGUI_PRIORITY);
		m_start_standalone_app.func.bind<&StudioAppImpl::startStandaloneApp>(this);
		addToolAction(&m_start_standalone_app);

		addAction<&StudioAppImpl::newWorld>("New", "New world", "newWorld", ICON_FA_PLUS);
		addAction<&StudioAppImpl::saveAs>("Save As", "Save world as", "saveAs", "", os::Keycode::S, Action::Modifiers::CTRL | Action::Modifiers::SHIFT);
		addAction<&StudioAppImpl::exit>("Exit", "Exit Studio", "exit", ICON_FA_SIGN_OUT_ALT);
		addAction<&StudioAppImpl::copy>("Copy", "Copy entity", "copy", ICON_FA_CLIPBOARD, os::Keycode::C, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::paste>("Paste", "Paste entity", "paste", ICON_FA_PASTE, os::Keycode::V, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::duplicate>("Duplicate", "Duplicate entity", "duplicate", ICON_FA_CLONE, os::Keycode::D, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::setTranslateGizmoMode>("Translate", "Set translate mode", "setTranslateGizmoMode", ICON_FA_ARROWS_ALT)
			.is_selected.bind<&Gizmo::Config::isTranslateMode>(&getGizmoConfig());
		addAction<&StudioAppImpl::setRotateGizmoMode>("Rotate", "Set rotate mode", "setRotateGizmoMode", ICON_FA_UNDO)
			.is_selected.bind<&Gizmo::Config::isRotateMode>(&getGizmoConfig());
		addAction<&StudioAppImpl::setScaleGizmoMode>("Scale", "Set scale mode", "setScaleGizmoMode", ICON_FA_EXPAND_ALT)
			.is_selected.bind<&Gizmo::Config::isScaleMode>(&getGizmoConfig());
		addAction<&StudioAppImpl::setLocalCoordSystem>("Local", "Set local transform system", "setLocalCoordSystem", ICON_FA_HOME)
			.is_selected.bind<&Gizmo::Config::isLocalCoordSystem>(&getGizmoConfig());
		addAction<&StudioAppImpl::setGlobalCoordSystem>("Global", "Set global transform system", "setGlobalCoordSystem", ICON_FA_GLOBE)
			.is_selected.bind<&Gizmo::Config::isGlobalCoordSystem>(&getGizmoConfig());

		addAction<&StudioAppImpl::addEntity>("Create empty", "Create empty entity", "createEntity", ICON_FA_PLUS_SQUARE);
		
		addAction<&StudioAppImpl::makeParent>("Make parent", "Make entity parent", "makeParent", ICON_FA_OBJECT_GROUP);
		addAction<&StudioAppImpl::unparent>("Unparent", "Unparent entity", "unparent", ICON_FA_OBJECT_UNGROUP);

		addAction<&StudioAppImpl::nextFrame>("Next frame", "Next frame", "nextFrame", ICON_FA_STEP_FORWARD);
		addAction<&StudioAppImpl::pauseGame>("Pause", "Pause game mode", "pauseGameMode", ICON_FA_PAUSE)
			.is_selected.bind<&Engine::isPaused>(m_engine.get());
		addAction<&StudioAppImpl::toggleGameMode>("Game Mode", "Toggle game mode", "toggleGameMode", ICON_FA_PLAY)
			.is_selected.bind<&WorldEditor::isGameMode>(m_editor.get());
		addAction<&StudioAppImpl::autosnapDown>("Autosnap down", "Toggle autosnap down", "autosnapDown")
			.is_selected.bind<&Gizmo::Config::isAutosnapDown>(&getGizmoConfig());
		addAction<&StudioAppImpl::snapDown>("Snap down", "Snap entities down", "snapDown");
		addAction<&StudioAppImpl::toggleEntityList>("Hierarchy", "Toggle hierarchy", "entityList", ICON_FA_STREAM)
			.is_selected.bind<&StudioAppImpl::isEntityListOpen>(this);
		addAction<&StudioAppImpl::toggleSettings>("Settings", "Toggle settings UI", "settings", ICON_FA_COG)
			.is_selected.bind<&StudioAppImpl::areSettingsOpen>(this);
		addAction<&StudioAppImpl::showExportGameDialog>("Export game", "Export game", "export_game", ICON_FA_FILE_EXPORT);
	}


	static bool copyPlugin(const char* src, int iteration, char (&out)[MAX_PATH])
	{
		char tmp_path[MAX_PATH];
		os::getExecutablePath(Span(tmp_path));
		StaticString<MAX_PATH> copy_path(Path::getDir(tmp_path), "plugins/", iteration);
		if (!os::makePath(copy_path)) logError("Could not create ", copy_path);
		copyString(Span(tmp_path), Path::getBasename(src));
		copy_path.append("/", tmp_path, ".", getPluginExtension());
#ifdef _WIN32
		StaticString<MAX_PATH> src_pdb(src);
		StaticString<MAX_PATH> dest_pdb(copy_path);
		if (Path::replaceExtension(dest_pdb.data, "pdb") && Path::replaceExtension(src_pdb.data, "pda"))
		{
			os::deleteFile(dest_pdb);
			if (!os::copyFile(src_pdb, dest_pdb))
			{
				copyString(out, src);
				return false;
			}
		}
#endif

		os::deleteFile(copy_path);
		if (!os::copyFile(src, copy_path))
		{
			copyString(out, src);
			return false;
		}
		copyString(out, copy_path);
		return true;
	}


	void loadUserPlugins() {
		PROFILE_FUNCTION();
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		SystemManager& system_manager = m_engine->getSystemManager();
		while (parser.next())
		{
			if (!parser.currentEquals("-plugin")) continue;
			if (!parser.next()) break;

			char src[MAX_PATH];
			parser.getCurrent(src, lengthOf(src));

			bool is_full_path = contains(src, '.');
			Lumix::ISystem* loaded_plugin;
			if (is_full_path)
			{
				char copy_path[MAX_PATH];
				copyPlugin(src, 0, copy_path);
				loaded_plugin = system_manager.load(copy_path);
			}
			else
			{
				loaded_plugin = system_manager.load(src);
			}

			if (!loaded_plugin)
			{
				logError("Could not load plugin ", src, " requested by command line");
			}
			else if (is_full_path && !m_watched_plugin.watcher.get())
			{
				char dir[MAX_PATH];
				copyString(Span(m_watched_plugin.basename.data), Path::getBasename(src));
				copyString(Span(dir), Path::getDir(src));
				m_watched_plugin.watcher = FileSystemWatcher::create(dir, m_allocator);
				m_watched_plugin.watcher->getCallback().bind<&StudioAppImpl::onPluginChanged>(this);
				m_watched_plugin.dir = dir;
				m_watched_plugin.system = loaded_plugin;
			}
		}
	}


	static const char* getPluginExtension()
	{
		const char* ext =
#ifdef _WIN32
			"dll";
#elif defined __linux__
			"so";
#else
#error Unknown platform
#endif
		return ext;
	}


	void onPluginChanged(const char* path)
	{
		const char* ext = getPluginExtension();
		if (!Path::hasExtension(path, ext)
#ifdef _WIN32
			&& !Path::hasExtension(path, "pda")
#endif
		)
			return;

		if (!equalIStrings(Path::getBasename(path), m_watched_plugin.basename)) return;

		m_watched_plugin.reload_request = true;
	}


	void tryReloadPlugin() {
		m_watched_plugin.reload_request = false;

		StaticString<MAX_PATH> src(m_watched_plugin.dir, m_watched_plugin.basename, ".", getPluginExtension());
		char copy_path[MAX_PATH];
		++m_watched_plugin.iteration;

		if (!copyPlugin(src, m_watched_plugin.iteration, copy_path)) return;

		logInfo("Trying to reload plugin ", m_watched_plugin.basename);

		OutputMemoryStream blob(m_allocator);
		blob.reserve(16 * 1024);
		SystemManager& system_manager = m_engine->getSystemManager();

		World* world = m_editor->getWorld();
		auto& modules = world->getModules();
		for (i32 i = 0, c = modules.size(); i < c; ++i) {
			UniquePtr<IModule>& module = modules[i];
			if (&module->getSystem() != m_watched_plugin.system) continue;

			module->beforeReload(blob);
			modules.erase(i);
			break;
		}
		system_manager.unload(m_watched_plugin.system);

		// TODO try to delete the old version

		m_watched_plugin.system = system_manager.load(copy_path);
		if (!m_watched_plugin.system) {
			logError("Failed to load plugin ", copy_path, ". Reload failed.");
			return;
		}

		InputMemoryStream input_blob(blob);
		m_watched_plugin.system->createModules(*world);
		for (const UniquePtr<IModule>& module : world->getModules()) {
			if (&module->getSystem() != m_watched_plugin.system) continue;
			module->afterReload(input_blob);
		}
		logInfo("Finished reloading plugin.");
	}

	bool workersCountOption(u32& workers_count) {
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-workers")) {
				if(!parser.next()) {
					logError("command line option '-workers` without value");
					return false;
				}
				char tmp[64];
				parser.getCurrent(tmp, sizeof(tmp));
				fromCString(tmp, workers_count);
				return true;
			}
		}
		return false;

	}

	void loadWorldFromCommandLine()
	{
		char cmd_line[2048];
		char path[MAX_PATH];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-open")) continue;
			if (!parser.next()) break;

			parser.getCurrent(path, lengthOf(path));
			loadWorld(Path(path), false);
			m_is_welcome_screen_open = false;
			break;
		}
	}

	static void checkDataDirCommandLine(char* dir, int max_size)
	{
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-data_dir")) continue;
			if (!parser.next()) break;

			parser.getCurrent(dir, max_size);
			break;
		}
	}
	
	Span<MousePlugin*> getMousePlugins() override { return m_mouse_plugins; }

	MousePlugin* getMousePlugin(const char* name) override {
		for (auto* i : m_mouse_plugins) {
			if (equalStrings(i->getName(), name)) return i;
		}
		return nullptr;
	}

	IPlugin* getIPlugin(const char* name) override {
		for (auto* i : m_plugins) {
			if (equalStrings(i->getName(), name)) return i;
		}
		return nullptr;
	}

	GUIPlugin* getGUIPlugin(const char* name) override {
		for (auto* i : m_gui_plugins) {
			if (equalStrings(i->getName(), name)) return i;
		}
		return nullptr;
	}


	void initPlugins() {
		PROFILE_FUNCTION();
		#ifdef STATIC_PLUGINS
			#define LUMIX_EDITOR_PLUGINS
			#include "engine/plugins.inl"
			#undef LUMIX_EDITOR_PLUGINS
		#else
			auto& plugin_manager = m_engine->getSystemManager();
			for (auto* lib : plugin_manager.getLibraries())
			{
				auto* f = (StudioApp::IPlugin * (*)(StudioApp&)) os::getLibrarySymbol(lib, "setStudioApp");
				if (f)
				{
					StudioApp::IPlugin* plugin = f(*this);
					if (plugin) addPlugin(*plugin);
				}
			}
		#endif

		addPlugin(*m_hierarchy.get());
		addPlugin(*createSplineEditor(*this));
		addPlugin(*createSignalEditor(*this));
		addPlugin(*m_property_grid.get());
		addPlugin(*m_log_ui.get());
		addPlugin(*m_asset_browser.get());
		addPlugin(*m_profiler_ui.get());

		for (IPlugin* plugin : m_plugins) {
			logInfo("Studio plugin ", plugin->getName(), " loaded");
		}

		for (int i = 1, c = m_plugins.size(); i < c; ++i) {
			for (int j = 0; j < i; ++j) {
				IPlugin* p = m_plugins[i];
				if (m_plugins[j]->dependsOn(*p)) {
					m_plugins.erase(i);
					--i;
					m_plugins.insert(j, p);
				}
			}
		}

		for (IPlugin* plugin : m_plugins) {
			plugin->init();
		}

		for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
			ASSERT(cmp.cmp->component_type != INVALID_COMPONENT_TYPE);
			const reflection::ComponentBase* r = cmp.cmp;
			
			if (m_component_labels.find(r->component_type).isValid()) continue;

			struct : reflection::IEmptyPropertyVisitor {
				void visit(const reflection::Property<Path>& prop) override {
					for (const reflection::IAttribute* attr : prop.attributes) {
						if (attr->getType() == reflection::IAttribute::RESOURCE) {
							is_res = true;
							reflection::ResourceAttribute* a = (reflection::ResourceAttribute*)attr;
							res_type = a->resource_type;
							prop_name = prop.name;
						}
					}
				}
				bool is_res = false;
				const char* prop_name;
				ResourceType res_type;
			} visitor;

			r->visit(visitor);
			if (visitor.is_res) {
				registerComponent(r->icon, r->component_type, r->label, visitor.res_type, visitor.prop_name);
			}
			else {
				registerComponent(r->icon, r->component_type, r->label);
			}
		}
		PrefabSystem::createEditorPlugins(*this, m_editor->getPrefabSystem());
	}


	void addPlugin(IPlugin& plugin) override { m_plugins.push(&plugin); }


	void addPlugin(GUIPlugin& plugin) override
	{
		m_gui_plugins.push(&plugin);
		for (auto* i : m_gui_plugins)
		{
			i->pluginAdded(plugin);
			plugin.pluginAdded(*i);
		}
	}

	void addPlugin(MousePlugin& plugin) override { m_mouse_plugins.push(&plugin); }
	void removePlugin(GUIPlugin& plugin) override { m_gui_plugins.swapAndPopItem(&plugin); }
	void removePlugin(MousePlugin& plugin) override { m_mouse_plugins.swapAndPopItem(&plugin); }

	void exitGameMode() override { m_deferred_game_mode_exit = true; }

	void exitWithCode(int exit_code) override {
		m_finished = true;
		m_exit_code = exit_code;
	}

	void guiAllActions() {
		if (m_show_all_actions_request) ImGui::OpenPopup("Action palette");

		if (ImGuiEx::BeginResizablePopup("Action palette", ImVec2(300, 200))) {
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

			if(m_show_all_actions_request) m_all_actions_selected = 0;
			if (m_all_actions_filter.gui(ICON_FA_SEARCH " Search", -1, m_show_all_actions_request)) {
				m_all_actions_selected = 0;
			}
			bool scroll = false;
			const bool insert_enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
			if (ImGui::IsItemFocused()) {
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_all_actions_selected > 0) {
					--m_all_actions_selected;
					scroll =  true;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
					++m_all_actions_selected;
					scroll =  true;
				}
			}
			if (m_all_actions_filter.isActive()) {
				if (ImGui::BeginChild("##list")) {
					u32 idx = 0;
					for (Action* act : m_actions) {
						if (!m_all_actions_filter.pass(act->label_long)) continue;

						char buf[20] = " (";
						getShortcut(*act, Span(buf + 2, sizeof(buf) - 2));
						if (buf[2]) {
							catString(buf, ")");
						}
						else { 
							buf[0] = '\0';
						}
						bool selected = idx == m_all_actions_selected;
						if (ImGui::Selectable(StaticString<128>(act->font_icon, act->label_long, buf), selected) || (selected && insert_enter)) {
							ImGui::CloseCurrentPopup();
							GUIPlugin* window = getFocusedWindow();
							if (!window || !window->onAction(*act)) {
								if (act->func.isValid()) {
									act->func.invoke();
								}
							}
							break;
						}
						++idx;
					}
				}
				ImGui::EndChild();
			}
			ImGui::EndPopup();
		}
		m_show_all_actions_request = false;
	}

	static bool includeFileInExport(const char* filename) {
		if (filename[0] == '.') return false;
		if (startsWith(filename, "bin/")) return false;
		if (equalStrings("main.pak", filename)) return false;
		if (equalStrings("error.log", filename)) return false;
		return true;
	}

	static bool includeDirInExport(const char* filename) {
		if (filename[0] == '.') return false;
		if (startsWith(filename, "bin") == 0) return false;
		return true;
	}

	struct ExportFileInfo {
		FilePathHash hash;
		u64 offset;
		u64 size;

		char path[MAX_PATH];
	};

	void scanCompiled(AssociativeArray<FilePathHash, ExportFileInfo>& infos) {
		os::FileIterator* iter = m_engine->getFileSystem().createFileIterator(".lumix/resources");
		const char* base_path = m_engine->getFileSystem().getBasePath();
		os::FileInfo info;
		exportFile("lumix.prj", infos);
		while (os::getNextFile(iter, &info)) {
			if (info.is_directory) continue;

			StringView basename = Path::getBasename(info.filename);
			ExportFileInfo rec;
			u64 tmp_hash;
			fromCString(basename, tmp_hash);
			rec.hash = FilePathHash::fromU64(tmp_hash);
			rec.offset = 0;
			const Path path(base_path, ".lumix/resources/", info.filename);
			rec.size = os::getFileSize(path);
			copyString(rec.path, ".lumix/resources/");
			catString(rec.path, info.filename);
			infos.insert(rec.hash, rec);
		}
		exportDataScan("pipelines/", infos);
		exportDataScan("universes/", infos);
		
		os::destroyFileIterator(iter);
	}


	void exportFile(const char* file_path, AssociativeArray<FilePathHash, ExportFileInfo>& infos) {
		const char* base_path = m_engine->getFileSystem().getBasePath();
		const FilePathHash hash(file_path);
		ExportFileInfo& out_info = infos.emplace(hash);
		copyString(out_info.path, file_path);
		out_info.hash = hash;
		const Path path(base_path, file_path);
		out_info.size = os::getFileSize(path);
		out_info.offset = ~0UL;
	}

	void exportDataScan(const char* dir_path, AssociativeArray<FilePathHash, ExportFileInfo>& infos)
	{
		auto* iter = m_engine->getFileSystem().createFileIterator(dir_path);
		const char* base_path = m_engine->getFileSystem().getBasePath();
		os::FileInfo info;
		while (os::getNextFile(iter, &info)) {
			char normalized_path[MAX_PATH];
			Path::normalize(info.filename, Span(normalized_path));
			if (info.is_directory)
			{
				if (!includeDirInExport(normalized_path)) continue;

				char dir[MAX_PATH] = {0};
				if (dir_path[0] != '.') copyString(dir, dir_path);
				catString(dir, info.filename);
				catString(dir, "/");
				exportDataScan(dir, infos);
				continue;
			}

			if (!includeFileInExport(normalized_path)) continue;

			StaticString<MAX_PATH> out_path;
			if (dir_path[0] == '.')
			{
				copyString(out_path.data, normalized_path);
			}
			else
			{
				copyString(out_path.data, dir_path);
				catString(out_path.data, normalized_path);
			}
			const FilePathHash hash(out_path.data);
			if (infos.find(hash) >= 0) continue;

			auto& out_info = infos.emplace(hash);
			copyString(out_info.path, out_path);
			out_info.hash = hash;
			const Path path(base_path, out_path);
			out_info.size = os::getFileSize(path);
			out_info.offset = ~0UL;
		}
		os::destroyFileIterator(iter);
	}


	void exportDataScanResources(AssociativeArray<FilePathHash, ExportFileInfo>& infos)
	{
		ResourceManagerHub& rm = m_engine->getResourceManager();
		exportFile("lumix.prj", infos);
		for (auto iter = rm.getAll().begin(), end = rm.getAll().end(); iter != end; ++iter) {
			const auto& resources = iter.value()->getResourceTable();
			for (Resource* res : resources) {
				const FilePathHash hash = res->getPath().getHash();
				const Path baked_path(".lumix/resources/", hash, ".res");

				if (infos.find(hash) < 0) {
					auto& out_info = infos.emplace(hash);
					copyString(Span(out_info.path), baked_path);
					out_info.hash = hash;
					out_info.size = os::getFileSize(baked_path);
					out_info.offset = ~0UL;
				}
			}
		}
		exportDataScan("pipelines/", infos);
		exportDataScan("universes/", infos);
	}


	void showExportGameDialog() { m_is_export_game_dialog_open = true; }


	void guiExportData() {
		if (!m_is_export_game_dialog_open) {
			m_export_msg_timer = -1;
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Export game", &m_is_export_game_dialog_open)) {
			ImGuiEx::Label("Destination dir");
			if (ImGui::Button(m_export.dest_dir.length() == 0 ? "..." : m_export.dest_dir.c_str())) {
				char tmp[MAX_PATH];
				if (os::getOpenDirectory(Span(tmp), m_engine->getFileSystem().getBasePath())) {
					m_export.dest_dir = tmp;
				}
			}

			ImGuiEx::Label("Pack data");
			ImGui::Checkbox("##pack", &m_export.pack);
			ImGuiEx::Label("Mode");
			ImGui::Combo("##mode", (int*)&m_export.mode, "All files\0Loaded world\0");

			ImGuiEx::Label("Startup world");
			if (ImGui::BeginCombo("##startunv", m_export.startup_world.c_str())) {
				forEachWorld([&](const Path& path){
					if (ImGui::Selectable(path.c_str())) m_export.startup_world = path;
				});
				ImGui::EndCombo();
			}
			if (m_export.startup_world.isEmpty()) {
				forEachWorld([&](const Path& path){
					if (m_export.startup_world.isEmpty()) {
						m_export.startup_world = path;
					}
				});
			}

			if (m_export_msg_timer > 0) {
				m_export_msg_timer -= m_engine->getLastTimeDelta();
				if (ImGui::Button("Export finished")) m_export_msg_timer = -1;
			}
			else {
				if (ImGui::Button("Export")) {
					if (exportData()) m_export_msg_timer = 3.f;
				}
			}
		}
		ImGui::End();
	}


	bool exportData() {
		if (m_export.dest_dir.length() == 0) return false;

		FileSystem& fs = m_engine->getFileSystem(); 
		{
			OutputMemoryStream prj_blob(m_allocator);
			m_engine->serializeProject(prj_blob, m_export.startup_world);

			const Path prj_file("lumix.prj");
			if (!fs.saveContentSync(prj_file, prj_blob)) {
				logError("Could not save ", prj_file);
				return false;
			}
		}

		AssociativeArray<FilePathHash, ExportFileInfo> infos(m_allocator);
		infos.reserve(10000);

		switch (m_export.mode) {
			case ExportConfig::Mode::ALL_FILES: scanCompiled(infos); break;
			case ExportConfig::Mode::CURRENT_WORLD: exportDataScanResources(infos); break;
		}

		if (m_export.pack) {
			StaticString<MAX_PATH> dest(m_export.dest_dir, "main.pak");
			if (infos.size() == 0) {
				logError("No files found while trying to create ", dest);
				return false;
			}
			u64 total_size = 0;
			for (ExportFileInfo& info : infos) {
				info.offset = total_size;
				total_size += info.size;
			}
			
			os::OutputFile file;
			if (!file.open(dest)) {
				logError("Could not create ", dest);
				return false;
			}

			const u32 count = (u32)infos.size();
			bool success = file.write(&count, sizeof(count));

			for (auto& info : infos) {
				success = file.write(&info.hash, sizeof(info.hash)) && success;
				success = file.write(&info.offset, sizeof(info.offset)) && success;
				success = file.write(&info.size, sizeof(info.size)) && success;
			}

			OutputMemoryStream src(m_allocator);
			for (const ExportFileInfo& info : infos) {
				src.clear();
				if (!fs.getContentSync(Path(info.path), src)) {
					logError("Could not read ", info.path);
					file.close();
					return false;
				}
				success = file.write(src.data(), src.size()) && success;
			}
			file.close();

			if (!success) {
				logError("Could not write ", dest);
				return false;
			}
		}
		else {
			const char* base_path = fs.getBasePath();
			for (auto& info : infos) {
				const Path src(base_path, info.path);
				StaticString<MAX_PATH> dst(m_export.dest_dir, info.path);
				StaticString<MAX_PATH> dst_dir(m_export.dest_dir, Path::getDir(info.path));
				if (!os::makePath(dst_dir) && !os::dirExists(dst_dir)) {
					logError("Failed to create ", dst_dir);
					return false;
				}

				if (!os::copyFile(src, dst)) {
					logError("Failed to copy ", src, " to ", dst);
					return false;
				}
			}
		}

		const char* bin_files[] = {"app.exe", "dbghelp.dll", "dbgcore.dll"};
		StaticString<MAX_PATH> src_dir("bin/");
		if (!os::fileExists("bin/app.exe")) {
			char tmp[MAX_PATH];
			os::getExecutablePath(Span(tmp));
			copyString(Span(src_dir.data), Path::getDir(tmp));
		}

		for (auto& file : bin_files) {
			StaticString<MAX_PATH> tmp(m_export.dest_dir, file);
			StaticString<MAX_PATH> src(src_dir, file);
			if (!os::copyFile(src, tmp)) {
				logError("Failed to copy ", src, " to ", tmp);
			}
		}

		for (GUIPlugin* plugin : m_gui_plugins)	{
			if (!plugin->exportData(m_export.dest_dir.c_str())) {
				logError("Plugin ", plugin->getName(), " failed to pack data.");
			}
		}
		logInfo("Exporting finished.");
		return true;
	}

	Span<const os::Event> getEvents() const override { return m_events; }

	void setCaptureInput(bool capture) override {
		m_capture_input = capture;
	}

	void checkShortcuts() {
		if (m_capture_input) return;
		u8 pressed_modifiers = 0;
		if (os::isKeyDown(os::Keycode::SHIFT)) pressed_modifiers |= Action::Modifiers::SHIFT;
		if (os::isKeyDown(os::Keycode::CTRL)) pressed_modifiers |= Action::Modifiers::CTRL;
		if (os::isKeyDown(os::Keycode::ALT)) pressed_modifiers |= Action::Modifiers::ALT;
		GUIPlugin* window = getFocusedWindow();
		
		ImGuiIO& io = ImGui::GetIO();
		for (Action*& a : m_actions) {
			if (a->type == Action::LOCAL) continue;
			if (a->type == Action::IMGUI_PRIORITY && io.WantCaptureKeyboard) continue;
			if (a->shortcut == os::Keycode::INVALID && a->modifiers == 0) continue;
			if (a->shortcut != os::Keycode::INVALID && !os::isKeyDown(a->shortcut)) continue;
			if (a->modifiers != pressed_modifiers) continue;
			
			if (window && window->onAction(*a))
				return;

			if (a->func.isValid()) {
				a->func.invoke();
				return;
			}
		}
	}

	IAllocator& getAllocator() override { return m_allocator; }
	Engine& getEngine() override { return *m_engine; }

	WorldEditor& getWorldEditor() override
	{
		ASSERT(m_editor.get());
		return *m_editor;
	}

	int getImGuiKey(int keycode) const override{
		return m_imgui_key_map[keycode];
	}
	
	ImFont* getDefaultFont() override { return m_font; }
	ImFont* getBigIconFont() override { return m_big_icon_font; }
	ImFont* getBoldFont() override { return m_bold_font; }
	ImFont* getMonospaceFont() override { return m_monospace_font; }

	struct WindowToDestroy {
		os::WindowHandle window;
		u32 counter;
	};

	DefaultAllocator m_main_allocator;
	debug::Allocator m_debug_allocator;
	TagAllocator m_allocator;
	TagAllocator m_imgui_allocator;
	
	UniquePtr<Engine> m_engine;
	UniquePtr<WorldEditor> m_editor;
	ImGuiKey m_imgui_key_map[255];
	Array<os::WindowHandle> m_windows;
	u32 m_frames_since_focused = 0;
	Array<WindowToDestroy> m_deferred_destroy_windows;
	os::WindowHandle m_main_window;
	os::WindowState m_fullscreen_restore_state;
	jobs::Signal m_init_imgui_signal;

	Array<Action*> m_owned_actions;
	Array<Action*> m_tools_actions;
	Array<Action*> m_actions;
	Array<Action*> m_window_actions;

	CommonActions m_common_actions;
	Action m_show_all_actions_action;
	Action m_start_standalone_app;

	Array<GUIPlugin*> m_gui_plugins;
	Array<MousePlugin*> m_mouse_plugins;
	Array<IPlugin*> m_plugins;
	Array<IAddComponentPlugin*> m_add_cmp_plugins;

	AddCmpTreeNode m_add_cmp_root;
	HashMap<ComponentType, String> m_component_labels;
	HashMap<ComponentType, StaticString<5>> m_component_icons;
	Gizmo::Config m_gizmo_config;

	bool m_first_update = true;
	bool m_show_save_world_ui = false;
	bool m_cursor_clipped = false;
	bool m_confirm_exit = false;
	bool m_confirm_load = false;
	bool m_confirm_new = false;
	bool m_is_caption_hovered = false;
	bool m_capture_input = false;
	
	Path m_world_to_load;
	
	ImTextureID m_logo = nullptr;
	UniquePtr<AssetBrowser> m_asset_browser;
	UniquePtr<AssetCompiler> m_asset_compiler;
	Local<PropertyGrid> m_property_grid;
	Local<HierarchyGUI> m_hierarchy;
	UniquePtr<GUIPlugin> m_profiler_ui;
	Local<LogUI> m_log_ui;
	Settings m_settings;
	
	FileSelector m_file_selector;
	DirSelector m_dir_selector;
	
	float m_fov = degreesToRadians(60);
	RenderInterface* m_render_interface = nullptr;
	Array<os::Event> m_events;
	TextFilter m_open_filter;
	TextFilter m_component_filter;
	
	float m_fps = 0;
	os::Timer m_fps_timer;
	os::Timer m_inactive_fps_timer;
	u32 m_fps_frame = 0;

	struct ExportConfig {
		ExportConfig(IAllocator& allocator) : dest_dir(allocator) {}
		enum class Mode : i32 {
			ALL_FILES,
			CURRENT_WORLD
		};

		Mode mode = Mode::ALL_FILES;

		bool pack = false;
		Path startup_world;
		String dest_dir;
	};

	ExportConfig m_export;
	float m_export_msg_timer = -1;
	bool m_entity_selection_changed = false;
	bool m_finished;
	bool m_deferred_game_mode_exit;
	int m_exit_code;

	bool m_is_welcome_screen_open;
	bool m_is_export_game_dialog_open;

	ImFont* m_font;
	ImFont* m_big_icon_font;
	ImFont* m_bold_font;
	ImFont* m_monospace_font;
	ImGuiID m_dockspace_id = 0;

	struct WatchedPlugin {
		UniquePtr<FileSystemWatcher> watcher;
		StaticString<MAX_PATH> dir;
		StaticString<MAX_PATH> basename;
		Lumix::ISystem* system = nullptr;
		int iteration = 0;
		bool reload_request = false;
	} m_watched_plugin;

	bool m_show_all_actions_request = false;
	i32 m_all_actions_selected = 0;
	TextFilter m_all_actions_filter;
	bool m_sleep_when_inactive = true;
	bool m_crash_reporting = true;
	i32 m_font_size = 16;
};

static Local<StudioAppImpl> g_studio;

StudioApp* StudioApp::create()
{
	g_studio.create();
	return g_studio.get();
}


void StudioApp::destroy(StudioApp& app)
{
	ASSERT(&app == g_studio.get());
	g_studio.destroy();
}


} // namespace Lumix
