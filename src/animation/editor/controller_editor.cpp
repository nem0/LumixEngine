#include <imgui/imgui.h>

#include "animation/animation_scene.h"
#include "controller_editor.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "renderer/model.h"
#include "../animation.h"
#include "../controller.h"
#include "../nodes.h"


namespace Lumix::Anim {

struct ControllerEditorImpl : ControllerEditor {
	ControllerEditorImpl(StudioApp& app)
		: m_app(app)
	{
		IAllocator& allocator = app.getAllocator();
		ResourceManager* res_manager = app.getEngine().getResourceManager().get(Controller::TYPE);
		ASSERT(res_manager);

		m_toggle_ui.init("Animation editor", "Toggle animation editor", "animation_editor", "", true);
		m_toggle_ui.func.bind<&ControllerEditorImpl::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ControllerEditorImpl::isOpen>(this);
		app.addWindowAction(&m_toggle_ui);

		m_controller = LUMIX_NEW(allocator, Controller)(Path("anim_editor"), *res_manager, allocator);
		m_controller->initEmpty();
		m_current_node = m_controller->m_root;
	}

	~ControllerEditorImpl() {
		m_app.removeAction(&m_toggle_ui);
		IAllocator& allocator = m_app.getAllocator();
		m_controller->destroy();
		LUMIX_DELETE(allocator, m_controller);
	}

	void onSettingsLoaded() override {
		m_open = m_app.getSettings().getValue("is_anim_editor_open", false);
	}
	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue("is_anim_editor_open", m_open);
	}

	bool isOpen() const { return m_open; }
	void toggleOpen() { m_open = !m_open; }

	static void createChild(GroupNode& parent, Node::Type type, IAllocator& allocator) {
		Node* node = nullptr;
		switch(type) {
			case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
			case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
			case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(&parent, allocator); break;
			default: ASSERT(false); return;
		}

		node->m_name = "new";
		parent.m_children.emplace(allocator);
		parent.m_children.back().node = node;
	}

	void properties_ui(AnimationNode& node) {
		const Array<String>& names = m_controller->m_animation_slots;
	
		const char* preview = node.m_slot < (u32)names.size() ? names[node.m_slot].c_str() : "";
		if (ImGui::BeginCombo("Animation", preview)) {
			for (u32 i = 0; i < (u32)names.size(); ++i) {
				if (ImGui::Selectable(names[i].c_str())) {
					node.m_slot = i;
				}
			}
			ImGui::EndCombo();
		}
	}

	void properties_ui(GroupNode& node) {}

	void properties_ui(Blend1DNode& node) {
		const InputDecl::Input& input = m_controller->m_inputs.inputs[node.m_input_index];
		if (ImGui::BeginCombo("Input", input.name)) {
			for (const InputDecl::Input& input : m_controller->m_inputs.inputs) {
				if (ImGui::Selectable(input.name)) {
					node.m_input_index = u32(&input - m_controller->m_inputs.inputs);
				}
			}
			ImGui::EndCombo();
		}

		ImGui::Columns(2);
		ImGui::Text("Value");
		ImGui::NextColumn();
		ImGui::Text("Slot");
		ImGui::NextColumn();
		ImGui::Separator();

		for (Blend1DNode::Child& child : node.m_children) {
			ImGui::PushID(&child);
		
			ImGui::PushItemWidth(-1);
			ImGui::InputFloat("##val", &child.value);
			ImGui::PopItemWidth();
			ImGui::NextColumn();
		
			ImGui::PushItemWidth(-1);
		
			const Array<String>& slots = m_controller->m_animation_slots;
			if (ImGui::BeginCombo("##anim", child.slot < (u32)slots.size() ? slots[child.slot].c_str() : "")) {
				for (u32 i = 0; i < (u32)slots.size(); ++i) {
					if (ImGui::Selectable(slots[i].c_str())) {
						child.slot = i;
					}
				}
				ImGui::EndCombo();
			}
		
			ImGui::PopItemWidth();
			ImGui::NextColumn();
		
		
			ImGui::PopID();
		}
		ImGui::Columns();
		if (ImGui::Button("Add")) {
			node.m_children.emplace();
			if(node.m_children.size() > 1) {
				node.m_children.back().value = node.m_children[node.m_children.size() - 2].value;
			}
		}
	}

	void child_properties_ui(Node& node) {
		if (node.m_parent && node.m_parent->type() == Node::Type::GROUP) {
			GroupNode* group = (GroupNode*)node.m_parent;
			for (GroupNode::Child& c : group->m_children) {
				if (c.node != &node) continue;
				
				conditionInput("Condition", m_controller->m_inputs, Ref(c.condition_str), Ref(c.condition));
				break;
			}
		}
	}

	void ui_dispatch(Node& node) {
		char tmp[64];
		copyString(tmp, node.m_name.c_str());
		if (ImGui::InputText("Name", tmp, sizeof(tmp))) {
			node.m_name = tmp;
		}

		child_properties_ui(node);

		switch(node.type()) {
			case Node::ANIMATION: properties_ui((AnimationNode&)node); break;
			case Node::GROUP: properties_ui((GroupNode&)node); break;
			case Node::BLEND1D: properties_ui((Blend1DNode&)node); break;
			default: ASSERT(false); break;
		}
	}

	static bool canLoadFromEntity(StudioApp& app) {
		const Array<EntityRef>& selected = app.getWorldEditor().getSelectedEntities();
		if (selected.size() != 1) return false;
		return app.getWorldEditor().getUniverse()->hasComponent(selected[0], Reflection::getComponentType("animator"));
	}

	static Path getPathFromEntity(StudioApp& app) {
		const Array<EntityRef>& selected = app.getWorldEditor().getSelectedEntities();
		if (selected.size() != 1) return Path();
		Universe* universe = app.getWorldEditor().getUniverse();
		const ComponentType cmp_type = Reflection::getComponentType("animator");
		if (!universe->hasComponent(selected[0], cmp_type)) return Path();
		AnimationScene* scene = (AnimationScene*)universe->getScene(cmp_type);
		return scene->getAnimatorSource(selected[0]);
	}

	void load(ControllerEditor& editor, const char* path) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OS::InputFile file;
		if (fs.open(path, Ref(file))) {
			IAllocator& allocator = m_app.getAllocator();
			ResourceManager* res_manager = m_app.getEngine().getResourceManager().get(Controller::TYPE);
			Array<u8> data(allocator);
			data.resize((u32)file.size());
			if (!file.read(data.begin(), data.byte_size())) {
				logError("Failed to read ", path);
			}
			else {
				InputMemoryStream str(data.begin(), data.byte_size());
				Controller* new_controller = LUMIX_NEW(allocator, Controller)(Path("anim_editor"), *res_manager, allocator);
				if (new_controller ->deserialize(str)) {
					LUMIX_DELETE(allocator, m_controller);
					m_controller = new_controller;
					m_current_node = m_controller->m_root;
				}
				else {
					LUMIX_DELETE(allocator, new_controller);
				}
			}
			file.close();
		}
		else {
			logError("Failed to open ", path);
		}
	}

	static bool conditionInput(const char* label, InputDecl& input, Ref<String> condition_str, Ref<Condition> condition) {
		char tmp[1024];
		copyString(tmp, condition_str->c_str());
		if (ImGui::InputText(label, tmp, sizeof(tmp), ImGuiInputTextFlags_EnterReturnsTrue)) {
			condition_str = tmp;
			condition->compile(tmp, input);
			return true;
		}

		return false;
	}

	static bool nodeInput(const char* label, Ref<u32> value, const Array<GroupNode::Child>& children) {
		if (!ImGui::BeginCombo(label, children[value].node->m_name.c_str())) return false;

		for (GroupNode::Child& child : children) {
			if (ImGui::Selectable(child.node->m_name.c_str())) {
				value = u32(&child - children.begin());
				return true;
			}
		}

		ImGui::EndCombo();
		return false;
	}

	static const char* toString(Node::Type type) {
		switch (type) {
			case Node::Type::ANIMATION: return "Animation"; break;
			case Node::Type::BLEND1D: return "Blend 1D"; break;
			case Node::Type::GROUP: return "Group"; break;
			case Node::Type::LAYERS: return "Layers"; break;
			default: ASSERT(false); return "N/A"; break;
		}
	}

	static bool isContainer(const Node& node) {
		switch (node.type()) {
			case Node::Type::ANIMATION: 
			case Node::Type::BLEND1D: return false;
			case Node::Type::GROUP:
			case Node::Type::LAYERS: return true;
			default: ASSERT(false); return false;
		}
	}

	void hierarchy_ui(Node& node) {
		const bool is_container = isContainer(node);
		bool is_parent_group = node.m_parent && node.m_parent->type() == Node::Type::GROUP;
		bool is_layer = node.m_parent && node.m_parent->type() == Node::Type::LAYERS;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | (&node == m_current_node ? ImGuiTreeNodeFlags_Selected : 0);
		if (!is_container) flags |= ImGuiTreeNodeFlags_Leaf; 

		const char* type_str = toString(node.type());
		if (ImGui::TreeNodeEx(&node, flags, "%s (%s)", node.m_name.c_str(), type_str)) {
			if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) {
				m_current_node = &node;
			}

			if (is_parent_group && ImGui::IsMouseClicked(1) && ImGui::IsItemHovered()) {
				ImGui::OpenPopup("group_popup");
			}
			if (is_layer && ImGui::IsMouseClicked(1) && ImGui::IsItemHovered()) {
				ImGui::OpenPopup("layer_popup");
			}

			if (ImGui::BeginPopup("group_popup")) {
				ImGui::TextUnformatted(node.m_name.c_str());
				ImGui::Separator();
				if (ImGui::Selectable("Remove")) {
					if (m_current_node == &node) m_current_node = nullptr;
					((GroupNode*)node.m_parent)->m_children.eraseItems([&node](GroupNode::Child& c){ return c.node == &node; });
					LUMIX_DELETE(m_controller->m_allocator, &node);
					ImGui::EndPopup();
					ImGui::TreePop();
					return;
				}
				ImGui::EndPopup();
			}

			switch (node.type()) {
				case Node::Type::GROUP: {
					GroupNode& group = (GroupNode&)node;
					for (GroupNode::Child& c : group.m_children) {
						hierarchy_ui(*c.node);
					}
					break;
				}
				case Node::Type::LAYERS: {
					LayersNode& layers = (LayersNode&)node;
					for (LayersNode::Layer& l : layers.m_layers) {
						hierarchy_ui(l.node);
					}
					break;
				}
			}
			ImGui::TreePop();
		}
	}

	void debuggerUI() {
		if (ImGui::Begin("Animation debugger", &m_open)) {
			const Array<EntityRef>& selected = m_app.getWorldEditor().getSelectedEntities();
			if (selected.empty()) {
				ImGui::End();
				return;
			}

			Universe* universe = m_app.getWorldEditor().getUniverse();
			const ComponentType cmp_type = Reflection::getComponentType("animator");
			if (!universe->hasComponent(selected[0], cmp_type)) {
				ImGui::End();
				return;
			}
			AnimationScene* scene = (AnimationScene*)universe->getScene(cmp_type);
			Controller* ctrl = scene->getAnimatorController(selected[0]);
			if (!ctrl) {
				ImGui::End();
				return;
			}
			
			for (const InputDecl::Input& input : ctrl->m_inputs.inputs) {
				const u32 idx = u32(&input - ctrl->m_inputs.inputs);
				switch (input.type) {
					case InputDecl::Type::EMPTY: break;
					case InputDecl::Type::FLOAT: {
						float val = scene->getAnimatorFloatInput(selected[0], idx);
						if (ImGui::DragFloat(input.name, &val)) {
							scene->setAnimatorInput(selected[0], idx, val);
						}
						break;
					}
					case InputDecl::Type::BOOL: {
						bool val = scene->getAnimatorBoolInput(selected[0], idx);
						if (ImGui::Checkbox(input.name, &val)) {
							scene->setAnimatorInput(selected[0], idx, val);
						}
						break;
					}
					case InputDecl::Type::U32: {
						u32 val = scene->getAnimatorU32Input(selected[0], idx);
						if (ImGui::DragInt(input.name, (int*)&val, 1, 0, 0x7ffFFff)) {
							scene->setAnimatorInput(selected[0], idx, val);
						}
						break;
					}
					default: ASSERT(false); break;
				}
			}
		}
		ImGui::End();
	}

	void onWindowGUI() override {
		if (!m_open) return;

		debuggerUI();

		if (ImGui::Begin("Animation hierarchy", &m_open, ImGuiWindowFlags_MenuBar)) {
			if (ImGui::BeginMenuBar()) {
				if (ImGui::BeginMenu("File")) {
					if (ImGui::MenuItem("Save")) {
						char path[MAX_PATH_LENGTH];
						if (OS::getSaveFilename(Span(path), "Animation controller\0*.act", "act")) {
							OutputMemoryStream str(m_controller->m_allocator);
							m_controller->serialize(str);
							OS::OutputFile file;
							if (file.open(path)) {
								if (!file.write(str.data(), str.size())) {
									logError("Failed to write ", path);
								}
								file.close();
							}
							else {
								logError("Failed to create ", path);
							}
						}
					}
					if (ImGui::MenuItem("Load")) {
						char path[MAX_PATH_LENGTH];

						if (OS::getOpenFilename(Span(path), "Animation controller\0*.act", nullptr)) {
							load(*this, path);
						}
					}
				
					if (ImGui::MenuItem("Load from entity", nullptr, false, canLoadFromEntity(m_app))) {
						load(*this, getPathFromEntity(m_app).c_str());
					}
					ImGui::EndMenu();
				}

				if (m_current_node && m_current_node->type() == Node::Type::GROUP) {
					if (ImGui::BeginMenu("Create node")) {
						GroupNode& group = (GroupNode&)*m_current_node;
						if (ImGui::MenuItem("Animation")) createChild(group, Node::ANIMATION, m_controller->m_allocator);
						if (ImGui::MenuItem("Blend1D")) createChild(group, Node::BLEND1D, m_controller->m_allocator);
						if (ImGui::MenuItem("Group")) createChild(group, Node::GROUP, m_controller->m_allocator);
						if (ImGui::MenuItem("Layers")) createChild(group, Node::LAYERS, m_controller->m_allocator);
						ImGui::EndMenu();
					}
				}

				ImGui::EndMenuBar();
			}

			hierarchy_ui(*m_controller->m_root);
		}
		ImGui::End();

		if (ImGui::Begin("Animation controller", &m_open)) {
			if (ImGui::CollapsingHeader("Controller")) {
				ImGui::InputText("Root motion bone", m_controller->m_root_motion_bone.data, sizeof(m_controller->m_root_motion_bone.data));
				bool xz_root_motion = m_controller->m_flags.isSet(Controller::Flags::XZ_ROOT_MOTION);
				if (ImGui::Checkbox("XZ root motion", &xz_root_motion)) {
					m_controller->m_flags.set(Controller::Flags::XZ_ROOT_MOTION, xz_root_motion);
				}
			}

			if (m_current_node && ImGui::CollapsingHeader("Node")) {
				ui_dispatch(*m_current_node);
			}

			if (ImGui::CollapsingHeader("Inputs")) {
				InputDecl& inputs = m_controller->m_inputs;
				
				ImGui::Columns(2);
				if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) inputs.addInput();
				ImGui::SameLine();
				ImGui::TextUnformatted("Name"); ImGui::NextColumn();
				ImGui::TextUnformatted("Type"); ImGui::NextColumn();
				for (InputDecl::Input& input : inputs.inputs) {
					if (input.type == InputDecl::Type::EMPTY) continue;
					ImGui::PushID(&input);
					if(ImGui::Button(ICON_FA_MINUS_CIRCLE)) inputs.removeInput(int(&input - inputs.inputs));
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1);
					ImGui::InputText("##name", input.name.data, sizeof(input.name.data));
					ImGui::NextColumn();
					ImGui::SetNextItemWidth(-1);
					if (ImGui::Combo("##type", (int*)&input.type, "float\0u32\0bool")) {
						inputs.recalculateOffsets();
					}
					ImGui::NextColumn();
					ImGui::PopID();
				}
				ImGui::Columns();
			}

			if (ImGui::CollapsingHeader("Slots")) {
				for (u32 i = 0; i < (u32)m_controller->m_animation_slots.size(); ++i) {
					String& slot = m_controller->m_animation_slots[i];
					char tmp[64];
					copyString(tmp, slot.c_str());
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText(StaticString<32>("##", (u64)(uintptr)&slot), tmp, sizeof(tmp))) {
						// update AnimationNode::m_animation_hash
						slot = tmp;
					}
				}
				if (ImGui::Button(ICON_FA_PLUS_CIRCLE "##create_slot")) {
					m_controller->m_animation_slots.emplace("", m_controller->m_allocator);
				}
			}

			if (ImGui::CollapsingHeader("Animations")) {
				ImGui::Columns(3);
				ImGui::Text("Set");
				ImGui::NextColumn();
				ImGui::Text("Slot");
				ImGui::NextColumn();
				ImGui::Text("Animation");
				ImGui::NextColumn();
				ImGui::Separator();
				for (u32 i = 0; i < (u32)m_controller->m_animation_entries.size(); ++i) {
					ImGui::PushID(i);
					Controller::AnimationEntry& entry = m_controller->m_animation_entries[i];
					ImGui::PushItemWidth(-1);
					ImGui::InputInt("##set", (int*)&entry.set);
					ImGui::PopItemWidth();
					ImGui::NextColumn();
					const char* preview = entry.slot < (u32)m_controller->m_animation_slots.size() ? m_controller->m_animation_slots[entry.slot].c_str() : "N/A";
					ImGui::PushItemWidth(-1);
					if (ImGui::BeginCombo("##slot", preview, 0)) {
						for (u32 i = 0, c = m_controller->m_animation_slots.size(); i < c; ++i) {
							if (ImGui::Selectable(m_controller->m_animation_slots[i].c_str())) {
								entry.slot = i;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::PopItemWidth();
					ImGui::NextColumn();
					ImGui::PushItemWidth(-1);
					char path[MAX_PATH_LENGTH];
					copyString(path, entry.animation ? entry.animation->getPath().c_str() : "");
					if (m_app.getAssetBrowser().resourceInput("anim", Span(path), Animation::TYPE)) {
						if (entry.animation) entry.animation->decRefCount();
						ResourceManagerHub& res_manager = m_app.getEngine().getResourceManager();
						entry.animation = res_manager.load<Animation>(Path(path));
					}
					ImGui::PopItemWidth();
					ImGui::NextColumn();
					ImGui::PopID();
				}
				ImGui::Columns();
				if (ImGui::Button("Create##create_animation")) {
					Controller::AnimationEntry& entry = m_controller->m_animation_entries.emplace();
					entry.animation = nullptr;
					entry.set = 0;
					entry.slot = 0;
				}
			}

			// TODO
			/*if (ImGui::CollapsingHeader("Transitions")) {
				Array<GroupNode::Child>& children = m_current_level->m_children;
				if (children.empty()) {
					ImGui::Text("No child nodes.");
				}
				else {
					for (GroupNode::Child& child : children) {
						for (GroupNode::Child::Transition& tr : child.transitions) {
							const char* name_from = child.node->m_name.c_str();
							const char* name_to = children[tr.to].node->m_name.c_str();
							if (!ImGui::TreeNodeEx(&tr, 0, "%s -> %s", name_from, name_to)) continue;

							u32 from = u32(&child - children.begin());
							if (nodeInput("From", Ref(from), children)) {
								children[from].transitions.push(tr);
								child.transitions.erase(u32(&tr - child.transitions.begin()));
								ImGui::TreePop();
								break;
							}

							nodeInput("To", Ref(tr.to), children);
							conditionInput("Condition", m_controller->m_inputs, Ref(tr.condition_str), Ref(tr.condition));

							ImGui::TreePop();
						}
					}

					if (ImGui::Button("Add")) {
						GroupNode::Child::Transition& transition = children[0].transitions.emplace(m_controller->m_allocator);
						transition.to = 0;
					}
				}
			}*/

			if (ImGui::CollapsingHeader("Bone masks")) {
				char model_path[MAX_PATH_LENGTH];
				copyString(model_path, m_model ? m_model->getPath().c_str() : "");
				ImGuiEx::Label("Model");
				if (m_app.getAssetBrowser().resourceInput("model", Span(model_path), Model::TYPE)) {
					m_model = m_app.getEngine().getResourceManager().load<Model>(Path(model_path));
				}

				if (m_model) {
					for (BoneMask& mask : m_controller->m_bone_masks) {
						if (!ImGui::TreeNodeEx(&mask, 0, "%s", mask.name.data)) continue;
				
						ImGuiEx::Label("Name");
						ImGui::InputText("##name", mask.name.data, sizeof(mask.name.data));
						for (u32 i = 0, c = m_model->getBoneCount(); i < c; ++i) {
							const char* bone_name = m_model->getBone(i).name.c_str();
							const u32 bone_name_hash = crc32(bone_name);
							const bool is_masked = mask.bones.find(bone_name_hash).isValid();
							bool b = is_masked;
							ImGuiEx::Label(bone_name);
							if (ImGui::Checkbox(StaticString<256>("##", bone_name), &b)) {
								ASSERT(b != is_masked);
								if (is_masked) {
									mask.bones.erase(bone_name_hash);
								}
								else {
									mask.bones.insert(bone_name_hash, 1);
								}
							}
						}
						ImGui::TreePop();
					}
					if (ImGui::Button("Add layer")) m_controller->m_bone_masks.emplace(m_controller->m_allocator);
				}
			}

			if (ImGui::CollapsingHeader("IK")) {
				char model_path[MAX_PATH_LENGTH];
				copyString(model_path, m_model ? m_model->getPath().c_str() : "");
				ImGuiEx::Label("Model");
				if (m_app.getAssetBrowser().resourceInput("model", Span(model_path), Model::TYPE)) {
					m_model = m_app.getEngine().getResourceManager().load<Model>(Path(model_path));
				}
				if(m_model) {
					for (u32 i = 0; i < m_controller->m_ik_count; ++i) {
						const StaticString<32> label("Chain ", i);
						if (ImGui::TreeNode(label)) {
							Controller::IK& ik = m_controller->m_ik[i];
							ASSERT(ik.bones_count > 0);
							const u32 bones_count = m_model->getBoneCount();
							auto leaf_iter = m_model->getBoneIndex(ik.bones[ik.bones_count - 1]);
							ImGuiEx::Label("Leaf");
							if (ImGui::BeginCombo("##leaf", leaf_iter.isValid() ? m_model->getBone(leaf_iter.value()).name.c_str() : "N/A")) {
								for (u32 j = 0; j < bones_count; ++j) {
									const char* bone_name = m_model->getBone(j).name.c_str();
									if (ImGui::Selectable(bone_name)) {
										ik.bones_count = 1;
										ik.bones[0] = crc32(bone_name);
									}
								}
								ImGui::EndCombo();
							}
							for (u32 j = ik.bones_count - 2; j != 0xffFFffFF; --j) { //-V621
								auto iter = m_model->getBoneIndex(ik.bones[j]);
								if (iter.isValid()) {
									ImGui::Text("%s", m_model->getBone(iter.value()).name.c_str());
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
											ik.bones[0] = crc32(bone_name);
											++ik.bones_count;
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
								if (ImGui::Button("Pop")) --ik.bones_count;
							} 

							ImGui::TreePop();
						}
					}

					if (m_controller->m_ik_count < (u32)lengthOf(m_controller->m_ik[0].bones) && ImGui::Button("Add chain")) {
						m_controller->m_ik[m_controller->m_ik_count].bones_count = 1;
						m_controller->m_ik[m_controller->m_ik_count].bones[0] = 0;
						++m_controller->m_ik_count;
					}
				}
				else {
					ImGui::Text("Please select a model.");
				}
			}
		}
		ImGui::End();
	}

	const char* getName() const override { return "Animation Editor"; }

	void show(const char* path) override {
		m_open = true;
		load(*this, path);
	}

	StudioApp& m_app;
	Controller* m_controller;
	Node* m_current_node = nullptr;
	Model* m_model = nullptr;
	bool m_open = false;
	Action m_toggle_ui;
}; // ControllerEditorImpl

UniquePtr<ControllerEditor> ControllerEditor::create(StudioApp& app) {
	return UniquePtr<ControllerEditorImpl>::create(app.getAllocator(), app);
}

} // ns Lumix::Anim