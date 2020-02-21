#include <imgui/imgui.h>

#include "animation/animation_scene.h"
#include "controller_editor.h"
#include "editor/asset_browser.h"
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


ControllerEditor::ControllerEditor(StudioApp& app)
	: m_app(app)
{
	IAllocator& allocator = app.getAllocator();
	ResourceManager* res_manager = app.getEngine().getResourceManager().get(Controller::TYPE);
	ASSERT(res_manager);

	m_controller = LUMIX_NEW(allocator, Controller)(Path("anim_editor"), *res_manager, allocator);
	m_controller->initEmpty();
	m_current_level = m_controller->m_root;
}

ControllerEditor::~ControllerEditor() {
	IAllocator& allocator = m_app.getAllocator();
	m_controller->destroy();
	LUMIX_DELETE(allocator, m_controller);
}

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

static void ui(Node& node, ControllerEditor& editor) {
	char tmp[64];
	copyString(tmp, node.m_name.c_str());
	if (ImGui::InputText("Name", tmp, sizeof(tmp))) {
		node.m_name = tmp;
	}
}

static void ui(AnimationNode& node, ControllerEditor& editor) {
	ui((Node&)node, editor);

	const Array<String>& names = editor.m_controller->m_animation_slots;
	
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

static void ui(GroupNode& node, ControllerEditor& editor) {
	ui((Node&)node, editor);
	if (ImGui::Button("View content")) {
		editor.m_current_level = &node;
	}
}

static void ui(Blend1DNode& node, ControllerEditor& editor) {
	ui((Node&)node, editor);
	const InputDecl::Input& input = editor.m_controller->m_inputs.inputs[node.m_input_index];
	if (ImGui::BeginCombo("Input", input.name)) {
		for (const InputDecl::Input& input : editor.m_controller->m_inputs.inputs) {
			if (ImGui::Selectable(input.name)) {
				node.m_input_index = u32(&input - editor.m_controller->m_inputs.inputs);
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
		
		const Array<String>& slots = editor.m_controller->m_animation_slots;
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

static void ui_dispatch(Node& node, ControllerEditor& editor) {
	switch(node.type()) {
		case Node::ANIMATION: ui((AnimationNode&)node, editor); break;
		case Node::GROUP: ui((GroupNode&)node, editor); break;
		case Node::BLEND1D: ui((Blend1DNode&)node, editor); break;
		default: ASSERT(false); break;
	}
}

static bool canLoadFromEntity(StudioApp& app) {
	const Array<EntityRef>& selected = app.getWorldEditor().getSelectedEntities();
	if (selected.size() != 1) return false;
	return app.getWorldEditor().getUniverse()->hasComponent(selected[0], Reflection::getComponentType("animator"));
}

static const char* getPathFromEntity(StudioApp& app) {
	const Array<EntityRef>& selected = app.getWorldEditor().getSelectedEntities();
	if (selected.size() != 1) return "";
	Universe* universe = app.getWorldEditor().getUniverse();
	const ComponentType cmp_type = Reflection::getComponentType("animator");
	if (!universe->hasComponent(selected[0], cmp_type)) return "";
	AnimationScene* scene = (AnimationScene*)universe->getScene(cmp_type);
	return scene->getAnimatorSource(selected[0]).c_str();
}

static void load(ControllerEditor& editor, const char* path) {
	OS::InputFile file;
	if (file.open(path)) {
		IAllocator& allocator = editor.m_app.getAllocator();
		ResourceManager* res_manager = editor.m_app.getEngine().getResourceManager().get(Controller::TYPE);
		Array<u8> data(allocator);
		data.resize((u32)file.size());
		if (!file.read(data.begin(), data.byte_size())) {
			logError("Animation") << "Failed to read " << path;
		}
		else {
			InputMemoryStream str(data.begin(), data.byte_size());
			Controller* new_controller = LUMIX_NEW(allocator, Controller)(Path("anim_editor"), *res_manager, allocator);
			if (new_controller ->deserialize(str)) {
				LUMIX_DELETE(allocator, editor.m_controller);
				editor.m_controller = new_controller;
				editor.m_current_level = editor.m_controller->m_root;
			}
			else {
				LUMIX_DELETE(allocator, new_controller);
			}
		}
		file.close();
	}
	else {
		logError("Animation") << "Failed to open " << path;
	}
}

static bool conditionInput(const char* label, Anim::InputDecl& input, Ref<String> condition_str, Ref<Condition> condition) {
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

void ControllerEditor::onWindowGUI() {
	if (ImGui::Begin("Animation editor", nullptr, ImGuiWindowFlags_MenuBar)) {
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Save")) {
					char path[MAX_PATH_LENGTH];
					if (OS::getSaveFilename(Span(path), "Animation controller\0*.act", "act")) {
						OutputMemoryStream str(m_controller->m_allocator);
						m_controller->serialize(str);
						OS::OutputFile file;
						if (file.open(path)) {
							if (!file.write(str.getData(), str.getPos())) {
								logError("Animation") << "Failed to write " << path;
							}
							file.close();
						}
						else {
							logError("Animation") << "Failed to create " << path;
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
					char tmp[MAX_PATH_LENGTH];
					copyString(tmp, m_app.getEngine().getFileSystem().getBasePath());
					catString(tmp, getPathFromEntity(m_app));
					load(*this, tmp);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Nodes")) {
				if (ImGui::BeginMenu("Create")) {
					if (ImGui::MenuItem("Animation")) {
						createChild(*m_current_level, Node::ANIMATION, m_controller->m_allocator);
					}
					if (ImGui::MenuItem("Blend 1D")) {
						createChild(*m_current_level, Node::BLEND1D, m_controller->m_allocator);
					}
					if (ImGui::MenuItem("Group")) {
						createChild(*m_current_level, Node::GROUP, m_controller->m_allocator);
					}
					if (ImGui::MenuItem("Layers")) {
						createChild(*m_current_level, Node::LAYERS, m_controller->m_allocator);
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			if (m_current_level->m_parent && ImGui::Button("Go up")) {
				m_current_level = m_current_level->m_parent;
			}

			ImGui::EndMenuBar();
		}
		
		if (ImGui::CollapsingHeader("Inputs")) {
			Anim::InputDecl& inputs = m_controller->m_inputs;
			for (Anim::InputDecl::Input& input : inputs.inputs) {
				if (input.type == Anim::InputDecl::Type::EMPTY) continue;

				if (ImGui::TreeNodeEx(&input, 0, "%s", input.name.data)) {
					ImGui::InputText("Name", input.name.data, sizeof(input.name.data));
					if (ImGui::Combo("Type", (int*)&input.type, "float\0u32\0bool")) {
						inputs.recalculateOffsets();
					}
					if(ImGui::Button("Remove")) {
						inputs.removeInput(int(&input - inputs.inputs));
					}
					ImGui::TreePop();
				}
			}
			if (ImGui::Button("Add")) inputs.addInput();
		}

		if (ImGui::CollapsingHeader("Slots")) {
			for (u32 i = 0; i < (u32)m_controller->m_animation_slots.size(); ++i) {
				String& slot = m_controller->m_animation_slots[i];
				char tmp[64];
				copyString(tmp, slot.c_str());
				if (ImGui::InputText(StaticString<32>("##", (u64)(uintptr)&slot), tmp, sizeof(tmp))) {
					// update AnimationNode::m_animation_hash
					slot = tmp;
				}
			}
			if (ImGui::Button("Create")) {
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
				if (m_app.getAssetBrowser().resourceInput("", "anim", Span(path), Animation::TYPE)) {
					if (entry.animation) entry.animation->getResourceManager().unload(*entry.animation);
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

		if (ImGui::CollapsingHeader("Nodes")) {
			GroupNode* parent = m_current_level;
			bool use_root_motion = m_controller->m_flags.isSet(Controller::Flags::USE_ROOT_MOTION);
			if (ImGui::Checkbox("Use root motion", &use_root_motion)) {
				m_controller->m_flags.set(Controller::Flags::USE_ROOT_MOTION, use_root_motion);
			}
			for (u32 i = 0; i < (u32)parent->m_children.size(); ++i) {
				GroupNode::Child& ch = parent->m_children[i];
				if (ImGui::TreeNodeEx(ch.node, 0, "%s", ch.node->m_name.c_str())) {
					if (ImGui::SmallButton("Remove")) {
						LUMIX_DELETE(m_controller->m_allocator, parent->m_children[i].node);
						parent->m_children.erase(i);
						ImGui::TreePop();
						continue;
					}
					conditionInput("Condition", m_controller->m_inputs, Ref(ch.condition_str), Ref(ch.condition));
					ui_dispatch(*ch.node, *this);
					ImGui::TreePop();
				}
			}
		}

		if (ImGui::CollapsingHeader("Transitions")) {
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
		}

		if (ImGui::CollapsingHeader("Bone masks")) {
			char model_path[MAX_PATH_LENGTH];
			copyString(model_path, m_model ? m_model->getPath().c_str() : "");
			if (m_app.getAssetBrowser().resourceInput("Model", "model", Span(model_path), Model::TYPE)) {
				m_model = m_app.getEngine().getResourceManager().load<Model>(Path(model_path));
			}

			if (m_model) {
				for (BoneMask& mask : m_controller->m_bone_masks) {
					if (!ImGui::TreeNodeEx(&mask, 0, "%s", mask.name.data)) continue;
				
					ImGui::InputText("Name", mask.name.data, sizeof(mask.name.data));
					for (u32 i = 0, c = m_model->getBoneCount(); i < c; ++i) {
						const char* bone_name = m_model->getBone(i).name.c_str();
						const u32 bone_name_hash = crc32(bone_name);
						const bool is_masked = mask.bones.find(bone_name_hash).isValid();
						bool b = is_masked;
						if (ImGui::Checkbox(bone_name, &b)) {
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
			if (m_app.getAssetBrowser().resourceInput("Model", "model", Span(model_path), Model::TYPE)) {
				m_model = m_app.getEngine().getResourceManager().load<Model>(Path(model_path));
			}
			if(m_model) {
				for (u32 i = 0; i < m_controller->m_ik_count; ++i) {
					const StaticString<32> label("Chain ", i);
					if (ImGui::TreeNode(label)) {
						Anim::Controller::IK& ik = m_controller->m_ik[i];
						ASSERT(ik.bones_count > 0);
						const u32 bones_count = m_model->getBoneCount();
						auto leaf_iter = m_model->getBoneIndex(ik.bones[ik.bones_count - 1]);
						if (ImGui::BeginCombo("Leaf", leaf_iter.isValid() ? m_model->getBone(leaf_iter.value()).name.c_str() : "N/A")) {
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


} // ns Lumix::Anim