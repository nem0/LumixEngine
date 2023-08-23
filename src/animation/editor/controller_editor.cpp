#include <imgui/imgui.h>

#include "animation/animation_module.h"
#include "controller_editor.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/settings.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "renderer/editor/world_viewer.h"
#include "renderer/model.h"
#include "renderer/render_module.h"
#include "../animation.h"
#include "../controller.h"
#include "../nodes.h"


namespace Lumix::anim {

struct ControllerEditorImpl : ControllerEditor, AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow, NodeEditor {
		struct INodeTypeVisitor {
			struct INodeCreator {
				virtual Node* create(EditorWindow&) const = 0;
			};

			virtual bool beginCategory(const char* category) { return true; }
			virtual void endCategory() {}
			virtual INodeTypeVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut = 0) = 0;

			INodeTypeVisitor& visitType(Node::Type type, const char* label, char shortcut = 0) {
				struct : INodeCreator {
					Node* create(EditorWindow& editor) const override {
						return Node::create(editor.m_current_node, type, editor.m_controller, editor.m_controller.m_allocator);
					}
					Node::Type type;
				} creator;
				creator.type = type;
				return visitType(label, creator, shortcut);
			}
		};

		void visitNodeTypes(INodeTypeVisitor& visitor) {
			if (visitor.beginCategory("Animations")) {
				struct : INodeTypeVisitor::INodeCreator {
					Node* create(EditorWindow& editor) const override {
						Node* n = Node::create(editor.m_current_node, Node::ANIMATION, editor.m_controller, editor.m_controller.m_allocator);
						((AnimationNode*)n)->m_slot = slot;
						return n;
					}
					u32 slot;
				} creator;
				for (String& slot : m_controller.m_animation_slots) {
					creator.slot = u32(&slot - m_controller.m_animation_slots.begin());
					visitor.visitType(slot.c_str(), creator);
				}
				visitor.endCategory();
			}
			visitor
				.visitType(Node::BLEND1D, "Blend 1D", '1')
				.visitType(Node::BLEND2D, "Blend 2D", '2')
				.visitType(Node::LAYERS, "Layers", 'L')
				.visitType(Node::SELECT, "Select", 'S')
				.visitType(Node::TREE, "Tree", 'T');
		}
	
		EditorWindow(const Path& path, ControllerEditorImpl& plugin, StudioApp& app, IAllocator& allocator)
			: AssetEditorWindow(app)
			, NodeEditor(allocator)
			, m_allocator(allocator)
			, m_app(app)
			, m_plugin(plugin)
			, m_copy_buffer(allocator)
			, m_controller(path, *app.getEngine().getResourceManager().get(Controller::TYPE), allocator)
			, m_viewer(app)
		{
			FileSystem& fs = m_app.getEngine().getFileSystem();
			OutputMemoryStream data(m_allocator);
			if (fs.getContentSync(Path(path), data)) {
				ResourceManager* res_manager = m_app.getEngine().getResourceManager().get(Controller::TYPE);
				InputMemoryStream str(data);
				if (m_controller.deserialize(str)) {
					m_current_node = m_controller.m_root;
					m_path = path;
					pushUndo(NO_MERGE_UNDO);
					m_dirty = false;
				}
				else {
					logError("Failed to load ", path);
				}
			}
			else {
				logError("Failed to read ", path);
			}

			const ComponentType animator_type = reflection::getComponentType("animator");
			m_viewer.m_world->createComponent(animator_type, *m_viewer.m_mesh);
			auto* anim_module = (AnimationModule*)m_viewer.m_world->getModule("animation");
			anim_module->setAnimatorSource(*m_viewer.m_mesh, path);
			anim_module->setAnimatorUseRootMotion(*m_viewer.m_mesh, true);

			char fbx_path[MAX_PATH];
			copyString(fbx_path, path);
			Path::replaceExtension(fbx_path, "fbx");
			if (fs.fileExists(fbx_path)) {
				auto* render_module = (RenderModule*)m_viewer.m_world->getModule("renderer");
				render_module->setModelInstancePath(*m_viewer.m_mesh, Path(fbx_path));
			}
		}

		~EditorWindow() {
			if (m_model) m_model->decRefCount();
		}

		void onCanvasClicked(ImVec2 pos, i32 hovered_link) override {
			struct : INodeTypeVisitor {
				INodeTypeVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut) override {
					if (!n && shortcut && os::isKeyDown((os::Keycode)shortcut)) {
						n = creator.create(*win);
					}
					return *this;
				}
				EditorWindow* win;
				Node* n = nullptr;
			} visitor;
			visitor.win = this;
			visitNodeTypes(visitor);
			if (visitor.n) {
				visitor.n->m_pos = pos;
				if (hovered_link >= 0) splitLink(m_current_node->m_nodes.back(), m_current_node->m_links, hovered_link);
				pushUndo(NO_MERGE_UNDO);
			}
		}

		void onLinkDoubleClicked(NodeEditorLink& link, ImVec2 pos) override {}
		void onNodeDoubleClicked(NodeEditorNode& node) override {
			if (!((Node&)node).m_nodes.empty()) {
				m_current_node = (Node*)&node;
			}
		}

		void onContextMenu(ImVec2 pos) override {
			ImGuiEx::filter("Filter", m_node_filter, sizeof(m_node_filter), 150, ImGui::IsWindowAppearing());
			Node* n = nullptr;
			
			if (m_node_filter[0]) {
				struct : INodeTypeVisitor {
					INodeTypeVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut) override {
						if (!n && findInsensitive(label, win->m_node_filter) && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::Selectable(label))) {
							n = creator.create(*win);
							ImGui::CloseCurrentPopup();
						}
						return *this;
					}
					EditorWindow* win;
					Node* n = nullptr;
				} visitor;
				visitor.win = this;
				visitNodeTypes(visitor);

				if (visitor.n) {
					visitor.n->m_pos = pos;
					pushUndo(NO_MERGE_UNDO);
				}
			}
			else {
				struct : INodeTypeVisitor {
					bool beginCategory(const char* category) { return ImGui::BeginMenu(category); }
					void endCategory() { ImGui::EndMenu(); }
					INodeTypeVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut) override {
						if (!n && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::Selectable(label))) {
							n = creator.create(*win);
							ImGui::CloseCurrentPopup();
						}
						return *this;
					}
					EditorWindow* win;
					Node* n = nullptr;
				} visitor;
				visitor.win = this;
				visitNodeTypes(visitor);

				if (visitor.n) {
					visitor.n->m_pos = pos;
					pushUndo(NO_MERGE_UNDO);
				}
			}
		}

		void deserialize(InputMemoryStream& blob) override {
			m_controller.clear();
			m_controller.deserialize(blob);
			m_current_node = m_controller.m_root;
		}

		void serialize(OutputMemoryStream& blob) override { m_controller.serialize(blob); }

		bool onAction(const Action& action) override {
			const CommonActions& actions = m_app.getCommonActions();
			if (&action == &actions.save) saveAs(m_path);
			else if (&action == &actions.undo) undo();
			else if (&action == &actions.del) deleteSelectedNodes();
			else if (&action == &actions.redo) redo();
			else return false;
			return true;
		}

		void previewUI() { 
			if (!ImGui::BeginTable("tab", 2, ImGuiTableFlags_Resizable)) return;

			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 250);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			
			debuggerUI(*m_viewer.m_world, *m_viewer.m_mesh);

			ImGui::Separator();
			auto* render_module = (RenderModule*)m_viewer.m_world->getModule("renderer");
			auto* anim_module = (AnimationModule*)m_viewer.m_world->getModule("animation");
			Path model_path = render_module->getModelInstancePath(*m_viewer.m_mesh);
			ImGuiEx::Label("Preview model");
			if (m_app.getAssetBrowser().resourceInput("model", model_path, Model::TYPE)) {
				render_module->setModelInstancePath(*m_viewer.m_mesh, model_path);
				anim_module->setAnimatorSource(*m_viewer.m_mesh, m_controller.getPath());
			}
			Model* model = render_module->getModelInstanceModel(*m_viewer.m_mesh);
			if (model && model->isReady()) {
				if (!m_was_preview_ready) {
					m_viewer.resetCamera(*model);
				}
				m_was_preview_ready = true;
			}
			else {
				m_was_preview_ready = false;
			}
			bool show_mesh = render_module->isModelInstanceEnabled(*m_viewer.m_mesh);
			
			ImGuiEx::Label("Show mesh"); 
			if (ImGui::Checkbox("##sm", &show_mesh)) {
				render_module->enableModelInstance(*m_viewer.m_mesh, show_mesh);
			}
			ImGuiEx::Label("Show skeleton"); 
			ImGui::Checkbox("##ss", &m_show_skeleton);
			if (m_show_skeleton) m_viewer.drawSkeleton(BoneNameHash());
			m_viewer.drawMeshTransform();
			ImGuiEx::Label("Follow mesh"); 
			ImGui::Checkbox("##fm", &m_viewer.m_follow_mesh);
			ImGuiEx::Label("Playback speed"); 
			ImGui::DragFloat("##spd", &m_playback_speed, 0.1f, 0, FLT_MAX);
			if (ImGui::Button("Apply")) {
				anim::Controller* ctrl = anim_module->getAnimatorController(*m_viewer.m_mesh);
				OutputMemoryStream blob(m_allocator);
				m_controller.serialize(blob);
				InputMemoryStream tmp(blob);
				ctrl->clear();
				ctrl->deserialize(tmp);
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset")) {
				m_viewer.m_world->setTransform(*m_viewer.m_mesh, DVec3(0), Quat::IDENTITY, Vec3(1));
			}
			if (m_playback_speed == 0) {
				ImGui::SameLine();
				if (ImGui::Button("Step")) anim_module->updateAnimator(*m_viewer.m_mesh, 1 / 30.f);
			}
			else {
				anim_module->updateAnimator(*m_viewer.m_mesh, m_app.getEngine().getLastTimeDelta() * m_playback_speed);
			}
			
			ImGui::TableNextColumn();
			m_viewer.gui();
			ImGui::EndTable();
		}

		void debuggerUI() {
			const Array<EntityRef>& selected = m_app.getWorldEditor().getSelectedEntities();
			World& world = *m_app.getWorldEditor().getWorld();
			if (selected.empty()) {
				ImGui::TextUnformatted("No entity selected");
				return;
			}
			const EntityRef entity = selected[0];
			const ComponentType animator_type = reflection::getComponentType("animator");
			if (!world.hasComponent(entity, animator_type)) {
				ImGui::TextUnformatted("Entity does not have an animator component");
				return;
			}

			debuggerUI(world, entity);
		}

		void processControllerMapping(World& world, EntityRef entity) {
			if (m_controller_debug_mapping.axis_x < 0 && m_controller_debug_mapping.axis_y < 0) return;

			const ComponentType animator_type = reflection::getComponentType("animator");
			AnimationModule* module = (AnimationModule*)world.getModule(animator_type);
			const InputSystem& input_system = m_app.getEngine().getInputSystem();
			Span<const InputSystem::Event> events = input_system.getEvents();
			for (const InputSystem::Event& e : events) {
				if (e.device->type == InputSystem::Device::CONTROLLER && e.type == InputSystem::Event::AXIS) {
					if (m_controller_debug_mapping.axis_x >= 0) {
						module->setAnimatorInput(entity, m_controller_debug_mapping.axis_x, e.data.axis.x);
					}
					if (m_controller_debug_mapping.axis_y >= 0) {
						module->setAnimatorInput(entity, m_controller_debug_mapping.axis_y, e.data.axis.y);
					}
				}
			}
		}

		bool isControllerConnected() const {
			for (const InputSystem::Device* device : m_app.getEngine().getInputSystem().getDevices()) {
				if (device->type == InputSystem::Device::CONTROLLER) return true;
			}
			return false;
		}

		void debuggerUI(World& world, EntityRef entity) {
			processControllerMapping(world, entity);

			const ComponentType animator_type = reflection::getComponentType("animator");
			AnimationModule* module = (AnimationModule*)world.getModule(animator_type);
			Controller* ctrl = module->getAnimatorController(entity);
			if (!ctrl) {
				ImGui::TextUnformatted("Selected entity does not have resource assigned in animator component");
				return;
			}
			
			for (const Controller::Input& input : ctrl->m_inputs) {
				ImGui::PushID(&input);
				const u32 idx = u32(&input - ctrl->m_inputs.begin());
				switch (input.type) {
					case Controller::Input::Type::FLOAT: {
						float val = module->getAnimatorFloatInput(entity, idx);
		
						ImGuiEx::Label(input.name);
						if (ImGui::DragFloat("##i", &val)) {
							module->setAnimatorInput(entity, idx, val);
						}
						if (isControllerConnected() && ImGui::BeginPopupContextItem()) {
							if (m_controller_debug_mapping.axis_x == idx) {
								if (ImGui::Selectable("Unmap controller X axis")) m_controller_debug_mapping.axis_x = -1;
							}
							else {
								if (ImGui::Selectable("Map to controller X axis")) m_controller_debug_mapping.axis_x = idx;
							}
							if (m_controller_debug_mapping.axis_y == idx) {
								if (ImGui::Selectable("Unmap controller Y axis")) m_controller_debug_mapping.axis_y = -1;
							}
							else {
								if (ImGui::Selectable("Map to controller Y axis")) m_controller_debug_mapping.axis_y = idx;
							}
							ImGui::EndPopup();
						}
						break;
					}
					case Controller::Input::BOOL: {
						bool val = module->getAnimatorBoolInput(entity, idx);
						ImGuiEx::Label(input.name);
						if (ImGui::Checkbox("##i", &val)) {
							module->setAnimatorInput(entity, idx, val);
						}
						break;
					}
					case Controller::Input::I32: {
						i32 val = module->getAnimatorI32Input(entity, idx);
						ImGuiEx::Label(input.name);
						if (ImGui::DragInt("##i", (int*)&val, 1, 0, 0x7ffFFff)) {
							module->setAnimatorInput(entity, idx, val);
						}
						break;
					}
				}
				ImGui::PopID();
			}

			if (m_controller.m_ik_count > 0) {
				for (u32 i = 0; i < m_controller.m_ik_count; ++i) {
					if (ImGui::TreeNode((const void*)(uintptr)i, "IK chain %d", i)) {
						ImGui::Checkbox("Enabled", &m_ik_debug[i].enabled);
						if (m_ik_debug[i].enabled) ImGui::DragFloat3("Target", &m_ik_debug[i].target.x);
						ImGui::TreePop();
					}
					module->setAnimatorIK(entity, i, m_ik_debug[i].enabled ? 1.f : 0.f, m_ik_debug[i].target);
					if (m_ik_debug[i].enabled) {
						auto* render_module = (RenderModule*)world.getModule("renderer");
						Transform tr = world.getTransform(entity);
						render_module->addDebugCross(tr.transform(m_ik_debug[i].target), 0.25f, Color::RED);
					}
				}
			}
		}

		void deleteSelectedNodes() {
			for (i32 i = m_current_node->m_nodes.size() - 1; i >= 0; --i) {
				Node* n = m_current_node->m_nodes[i];
				if (n->m_selected) {
					m_current_node->m_links.eraseItems([&](const NodeEditorLink& link){ return link.getFromNode() == n->m_id || link.getToNode() == n->m_id; });
					LUMIX_DELETE(m_allocator, n);
					m_current_node->m_nodes.swapAndPop(i);
				}
			}
		}

		void saveAs(const Path& path) {
			OutputMemoryStream blob(m_controller.m_allocator);
			m_controller.serialize(blob);
			FileSystem& fs = m_app.getEngine().getFileSystem();
			if (!fs.saveContentSync(path, blob)) {
				logError("Failed to save ", path);
				return;
			}
			m_path = path;
			m_dirty = false;
		}

		void saveUndo(bool changed) {
			if (changed) pushUndo(ImGui::GetItemID());
		}

		void pushUndo(u32 tag) override {
			SimpleUndoRedo::pushUndo(tag);
			m_dirty = true;
		}

		void IKGUI() {
			Path model_path = m_model ? m_model->getPath() : Path();
			ImGuiEx::Label("Model");
			if (m_app.getAssetBrowser().resourceInput("model", model_path, Model::TYPE)) {
				m_model = m_app.getEngine().getResourceManager().load<Model>(model_path);
			}
			if(m_model) {
				for (u32 i = 0; i < m_controller.m_ik_count; ++i) {
					ImGui::PushID(i);
					if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
						if (i < m_controller.m_ik_count - 1) {
							memmove(&m_controller.m_ik[i]
								, &m_controller.m_ik[i + 1]
								, sizeof(m_controller.m_ik[i + 1]) * (m_controller.m_ik_count - 1 - i)
							);
						}
						--m_controller.m_ik_count;
						ImGui::PopID();
						continue;
					}
					ImGui::PopID();
					ImGui::SameLine();

					if (ImGui::TreeNode((const void*)(uintptr)i, "Chain %d", i)) {
						Controller::IK& ik = m_controller.m_ik[i];
						ASSERT(ik.bones_count > 0);
						const u32 bones_count = m_model->getBoneCount();
						auto leaf_iter = m_model->getBoneIndex(ik.bones[ik.bones_count - 1]);
						ImGuiEx::Label("Leaf");
						if (ImGui::BeginCombo("##leaf", leaf_iter.isValid() ? m_model->getBone(leaf_iter.value()).name.c_str() : "N/A")) {
							bool selected = false;
							for (u32 j = 0; j < bones_count; ++j) {
								const char* bone_name = m_model->getBone(j).name.c_str();
								if (ImGui::Selectable(bone_name)) {
									ik.bones_count = 1;
									ik.bones[0] = BoneNameHash(bone_name);
									selected = true;
								}
							}
							ImGui::EndCombo();
							saveUndo(selected);
						}
						for (u32 j = ik.bones_count - 2; j != 0xffFFffFF; --j) {
							auto iter = m_model->getBoneIndex(ik.bones[j]);
							if (iter.isValid()) {
								ImGuiEx::TextUnformatted(m_model->getBone(iter.value()).name);
							}
							else {
								ImGui::Text("Unknown bone");
							}
						}

						auto iter = m_model->getBoneIndex(ik.bones[0]);
						if (iter.isValid()) {
							if (ik.bones_count < lengthOf(ik.bones)) {
								const int parent_idx = m_model->getBone(iter.value()).parent_idx;
								if (parent_idx >= 0) {
									const char* bone_name = m_model->getBone(parent_idx).name.c_str();
									const StaticString<64> add_label("Add ", bone_name);
									if (ImGui::Button(add_label)) {
										memmove(&ik.bones[1], &ik.bones[0], sizeof(ik.bones[0]) * ik.bones_count);
										ik.bones[0] = BoneNameHash(bone_name);
										++ik.bones_count;
										saveUndo(true);
									}
								}
							}
							else {
								ImGui::Text("IK is full");
							}
						}
						else {
							ImGui::Text("Unknown bone.");
						}
						if (ik.bones_count > 1) {
							ImGui::SameLine();
							if (ImGui::Button("Pop")) {
								memmove(&ik.bones[0], &ik.bones[1], sizeof(ik.bones[0]) * ik.bones_count - 1);
								--ik.bones_count;
								saveUndo(true);
							}
						} 

						ImGui::TreePop();
					}
				}

				if (m_controller.m_ik_count < (u32)lengthOf(m_controller.m_ik) && ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
					m_controller.m_ik[m_controller.m_ik_count].bones_count = 1;
					m_controller.m_ik[m_controller.m_ik_count].bones[0] = BoneNameHash();
					++m_controller.m_ik_count;
					saveUndo(true);
				}
			}
			else {
				ImGui::Text("Please select a model.");
			}
		}

		void removeSlot(u32 idx) {
			m_controller.m_animation_slots.erase(idx);
			for (Controller::AnimationEntry& e : m_controller.m_animation_entries) {
				if (e.slot > idx) --e.slot;
			}
			m_controller.m_animation_entries.eraseItems([idx](Controller::AnimationEntry& e){ return e.slot == idx; });
			// TODO update slots in nodes
			pushUndo(NO_MERGE_UNDO);
		}

		void boneMasksGUI() {
			Path model_path = m_model ? m_model->getPath() : Path();
			ImGuiEx::Label("Model");
			if (m_app.getAssetBrowser().resourceInput("model", model_path, Model::TYPE)) {
				m_model = m_app.getEngine().getResourceManager().load<Model>(model_path);
			}

			if (!m_model) {
				ImGui::Text("Please select a model.");
				return;
			}

			for (BoneMask& mask : m_controller.m_bone_masks) {
				ImGui::PushID(&mask);
				if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
					m_controller.m_bone_masks.erase(u32(&mask - m_controller.m_bone_masks.begin()));
					// TODO update references in nodes
					ImGui::PopID();
					continue;
				}
				ImGui::PopID();

				ImGui::SameLine();
				if (!ImGui::TreeNodeEx(&mask, 0, "%s", mask.name.data)) continue;
				
				ImGuiEx::Label("Name");
				saveUndo(ImGui::InputText("##name", mask.name.data, sizeof(mask.name.data)));
				for (u32 i = 0, c = m_model->getBoneCount(); i < c; ++i) {
					const char* bone_name = m_model->getBone(i).name.c_str();
					const BoneNameHash bone_name_hash(bone_name);
					const bool is_masked = mask.bones.find(bone_name_hash).isValid();
					bool b = is_masked;
					ImGuiEx::Label(bone_name);
					ImGui::PushID(bone_name);
					if (ImGui::Checkbox("##bn", &b)) {
						ASSERT(b != is_masked);
						if (is_masked) {
							mask.bones.erase(bone_name_hash);
						}
						else {
							mask.bones.insert(bone_name_hash, 1);
						}
						saveUndo(true);
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
			}

			if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
				m_controller.m_bone_masks.emplace(m_controller.m_allocator);
				saveUndo(true);
			}
		}

		void slotsGUI() {
			for (u32 i = 0; i < (u32)m_controller.m_animation_slots.size(); ++i) {
				ImGui::PushID(i);
				String& slot = m_controller.m_animation_slots[i];
				char tmp[64];
				copyString(tmp, slot);
				if (ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Remove")) {
					removeSlot(i);
				}
				else {
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText("##name", tmp, sizeof(tmp))) {
						slot = tmp;
						saveUndo(true);
					}
				}
				ImGui::PopID();
			}
			if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add slot")) {
				m_controller.m_animation_slots.emplace("", m_controller.m_allocator);
				saveUndo(true);
			}

			if (ImGui::BeginDragDropTarget()) {
				auto* payload = ImGui::AcceptDragDropPayload(nullptr);
				if (payload && payload->IsDataType("asset_browser_selection")) {
					Span<const Path> selection = m_app.getAssetBrowser().getSelectedResources();
					bool any = false;
					for (const Path& path : selection) {
						StringView subres = Path::getSubresource(path);
						if (Path::hasExtension(subres, "ani")) {
							Controller::AnimationEntry& entry = m_controller.m_animation_entries.emplace();
							ResourceManagerHub& res_manager = m_app.getEngine().getResourceManager();
							entry.animation = res_manager.load<Animation>(Path(path));
							entry.set = 0;
							entry.slot = m_controller.m_animation_slots.size();
	
							m_controller.m_animation_slots.emplace(Path::getBasename(path), m_controller.m_allocator);
							any = true;	
						}
					}
					if (any) pushUndo(NO_MERGE_UNDO);
				}
				else if (payload && payload->IsDataType("path")) {
					const char* path = (const char*)payload->Data;
					StringView subres = Path::getSubresource(path);
					if (Path::hasExtension(subres, "ani")) {
						Controller::AnimationEntry& entry = m_controller.m_animation_entries.emplace();
						ResourceManagerHub& res_manager = m_app.getEngine().getResourceManager();
						entry.animation = res_manager.load<Animation>(Path(path));
						entry.set = 0;
						entry.slot = m_controller.m_animation_slots.size();

						m_controller.m_animation_slots.emplace(Path::getBasename(path), m_controller.m_allocator);

						pushUndo(NO_MERGE_UNDO);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		void setsGUI() {
			u32 max_set = 0;
			for (const Controller::AnimationEntry& e : m_controller.m_animation_entries) {
				max_set = maximum(max_set, e.set);
			}

			for (u32 set_idx = 0; set_idx <= max_set; ++set_idx) {
				ImGui::PushID(set_idx);
				if (ImGui::Button(ICON_FA_TIMES_CIRCLE "##del_set")) {
					m_controller.m_animation_entries.eraseItems([set_idx](Controller::AnimationEntry& e){ return e.set == set_idx; });
					saveUndo(true);
					ImGui::PopID();
					continue;
				}
				ImGui::PopID();
				ImGui::SameLine();
				if (ImGui::TreeNode((const void*)(uintptr)set_idx, "Set %d", set_idx)) {

					ImGui::Columns(2);
					for (u32 entry_idx = 0; entry_idx < (u32)m_controller.m_animation_entries.size(); ++entry_idx) {
						Controller::AnimationEntry& entry = m_controller.m_animation_entries[entry_idx];
						if (entry.set != set_idx) continue;

						ImGui::PushID(entry_idx);
						if (ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Delete")) {
							m_controller.m_animation_entries.swapAndPop(entry_idx);
							saveUndo(true);
							ImGui::PopID();
							continue;
						}
						ImGui::SameLine();
						ImGui::SetNextItemWidth(-1);
						saveUndo(inputSlot(m_controller, "##slot", &entry.slot));
						ImGui::NextColumn();
						ImGui::PushItemWidth(-1);
						Path path = entry.animation ? entry.animation->getPath() : Path();
						if (m_app.getAssetBrowser().resourceInput("anim", path, Animation::TYPE)) {
							if (entry.animation) entry.animation->decRefCount();
							ResourceManagerHub& res_manager = m_app.getEngine().getResourceManager();
							entry.animation = res_manager.load<Animation>(path);
							saveUndo(true);
						}
						ImGui::PopItemWidth();
						ImGui::NextColumn();
						ImGui::PopID();
					}
					ImGui::Columns();
					if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add animation")) {
						Controller::AnimationEntry& entry = m_controller.m_animation_entries.emplace();
						entry.animation = nullptr;
						entry.set = set_idx;
						entry.slot = 0;
						saveUndo(true);
					}
					ImGui::Columns();
					ImGui::TreePop();
				}
			}

			if (ImGui::Button(ICON_FA_PLUS_CIRCLE "##create_set")) {
				Controller::AnimationEntry& entry = m_controller.m_animation_entries.emplace();
				entry.animation = nullptr;
				entry.set = max_set + 1;
				entry.slot = 0;
				saveUndo(true);
			}
			ImGui::SameLine();
			ImGui::Bullet();
			ImGui::TextUnformatted("New set");
		}

		void inputsGUI() {
			ImGui::Columns(2);
			ImGui::TextUnformatted("Name"); ImGui::NextColumn();
			ImGui::TextUnformatted("Type"); ImGui::NextColumn();
			for (u32 idx = 0; idx < (u32)m_controller.m_inputs.size(); ++idx) {
				Controller::Input& input = m_controller.m_inputs[idx];
				ImGui::PushID(&input);
				if(ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Remove")) {
					m_controller.m_inputs.erase(idx);
					--idx;
					saveUndo(true);
					ImGui::NextColumn();
					ImGui::NextColumn();
					ImGui::PopID();
					continue;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				saveUndo(ImGui::InputText("##name", input.name.data, sizeof(input.name.data)));
				ImGui::NextColumn();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::Combo("##type", (int*)&input.type, "float\0u32\0bool")) {
					saveUndo(true);
				}
				ImGui::NextColumn();
				ImGui::PopID();
			}
			ImGui::Columns();
			if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add input")) {
				m_controller.m_inputs.emplace();
				saveUndo(true);
			}
		}

		void breadcrumbs(Node* node) {
			if (node->m_parent) {
				breadcrumbs(node->m_parent);
				ImGui::SameLine();
				ImGui::TextUnformatted("/");
				ImGui::SameLine();
			}
			
			const char* name = "N/A";
			switch (node->type()) {
				case Node::TREE: name = ((TreeNode*)node)->m_name.c_str(); break;
				default: ASSERT(false);
			};
			if (m_current_node == node) {
				ImGui::TextUnformatted(name);
			}
			else {
				if (ImGui::Button(name)) m_current_node = node;
			}
		}

		void windowGUI() override {
			const CommonActions& actions = m_app.getCommonActions();

			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) saveAs(m_path);
				if (ImGuiEx::IconButton(ICON_FA_UNDO, "Undo")) undo();
				if (ImGuiEx::IconButton(ICON_FA_REDO, "Redo")) redo();
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(m_path);
				ImGui::EndMenuBar();
			}

			if (ImGui::BeginTabBar("ctb")) {
				if (ImGui::BeginTabItem("Tree")) {
					ImGui::Columns(2);
					breadcrumbs(m_current_node);
					if (m_current_node) nodeEditorGUI(m_current_node->m_nodes, m_current_node->m_links);
					ImGui::NextColumn();
					if (m_current_node) {
						for (Node* n : m_current_node->m_nodes) {
							if (n->m_selected) {
								n->propertiesGUI();
								break;
							}
						}
					}
					ImGui::Columns();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Controller")) {
					ImGuiEx::Label("Root motion bone");
					saveUndo(ImGui::InputText("##rmb", m_controller.m_root_motion_bone.data, sizeof(m_controller.m_root_motion_bone.data)));
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Inputs")) {
					inputsGUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Bone masks")) {
					boneMasksGUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("IK")) {
					IKGUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Slots")) {
					slotsGUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Sets")) {
					setsGUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Debugger")) {
					debuggerUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Preview")) {
					previewUI();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

		}

		const Path& getPath() override { return m_path; }
		const char* getName() const override { return "Animation Editor"; }

		struct CopyBuffer {
			CopyBuffer(IAllocator& allocator) : data(allocator) {}
			OutputMemoryStream data;
			Node::Type node_type;
		} m_copy_buffer;

		struct ControllerDebugMapping {
			i32 axis_x = -1;
			i32 axis_y = -1;
		};

		struct IKDebug {
			bool enabled = false;
			Vec3 target = Vec3(0); 
		};

		WorldViewer m_viewer;
		IAllocator& m_allocator;
		StudioApp& m_app;
		ControllerEditorImpl& m_plugin;
		Controller m_controller;
		Node* m_current_node = nullptr;
		Model* m_model = nullptr;
		Path m_path;
		bool m_was_preview_ready = false;
		float m_playback_speed = 1.f;
		bool m_show_skeleton = true;
		ControllerDebugMapping m_controller_debug_mapping;
		IKDebug m_ik_debug[4];
		char m_node_filter[64] = "";
	};

	ControllerEditorImpl(StudioApp& app)
		: m_allocator(app.getAllocator(), "anim controller editor")
		, m_app(app)
	{
		AssetCompiler& compiler = app.getAssetCompiler();
		const char* act_exts[] = { "act" };
		compiler.registerExtension("act", anim::Controller::TYPE);
		compiler.addPlugin(*this, Span(act_exts));
		app.getAssetBrowser().addPlugin(*this, Span(act_exts));
	}

	~ControllerEditorImpl() {
		m_app.getAssetCompiler().removePlugin(*this);
		m_app.getAssetBrowser().removePlugin(*this);
	}

	void createResource(OutputMemoryStream& blob) override {
		ResourceManager* rm = m_app.getEngine().getResourceManager().get(anim::Controller::TYPE);
		anim::Controller controller(Path("new controller"), *rm, m_app.getAllocator());
		controller.serialize(blob);
	}

	void openEditor(const Path& path) override {
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(m_allocator, path, *this, m_app, m_allocator);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "act"; }
	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Animation Controller"; }

	TagAllocator m_allocator;
	StudioApp& m_app;
};

UniquePtr<ControllerEditor> ControllerEditor::create(StudioApp& app) {
	return UniquePtr<ControllerEditorImpl>::create(app.getAllocator(), app);
}

} // namespace Lumix::Anim
