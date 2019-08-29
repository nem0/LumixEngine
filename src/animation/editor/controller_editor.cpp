#include "controller_editor.h"
#include "editor/asset_browser.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "imgui/imgui.h"
#include "../animation.h"
#include "../controller.h"
#include "../nodes.h"


namespace Lumix::Anim {


ControllerEditor::ControllerEditor(StudioApp& app)
	: m_app(app)
{
	IAllocator& allocator = app.getWorldEditor().getAllocator();
	ResourceManager* res_manager = app.getWorldEditor().getEngine().getResourceManager().get(Controller::TYPE);
	ASSERT(res_manager);

	m_controller = LUMIX_NEW(allocator, Controller)(Path("anim_editor"), *res_manager, allocator);
	m_controller->initEmpty();
	m_current_level = m_controller->m_root;
}


static void createChild(GroupNode& parent, Node::Type type, IAllocator& allocator) {
	Node* node = nullptr;
	switch(type) {
		case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
		case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
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
	
	const int idx = names.find([&](const String& name){
		return crc32(name.c_str()) == node.m_animation_hash;
	});
	const char* preview = idx >= 0 ? names[idx].c_str() : "";
	if (ImGui::BeginCombo("Animation", preview)) {
		for (u32 i = 0; i < (u32)names.size(); ++i) {
			if (ImGui::Selectable(names[i].c_str())) {
				node.m_animation_hash = crc32(names[i].c_str());
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

static void ui_dispatch(Node& node, ControllerEditor& editor) {
	switch(node.type()) {
		case Node::ANIMATION: ui((AnimationNode&)node, editor); break;
		case Node::GROUP: ui((GroupNode&)node, editor); break;
		default: ASSERT(false); break;
	}
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
						OS::InputFile file;
						if (file.open(path)) {
							IAllocator& allocator = m_app.getWorldEditor().getAllocator();
							ResourceManager* res_manager = m_app.getWorldEditor().getEngine().getResourceManager().get(Controller::TYPE);
							Array<u8> data(allocator);
							data.resize((u32)file.size());
							if (!file.read(data.begin(), data.byte_size())) {
								logError("Animation") << "Failed to read " << path;
							}
							else {
								InputMemoryStream str(data.begin(), data.byte_size());
								Controller* new_controller = LUMIX_NEW(allocator, Controller)(Path("anim_editor"), *res_manager, allocator);
								if (new_controller ->deserialize(str)) {
									LUMIX_DELETE(allocator, m_controller);
									m_controller = new_controller;
									m_current_level = m_controller->m_root;
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
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Structure")) {
				if (ImGui::BeginMenu("Create")) {
					if (ImGui::MenuItem("Animation")) {
						createChild(*m_current_level, Node::ANIMATION, m_controller->m_allocator);
					}
					if (ImGui::MenuItem("Group")) {
						createChild(*m_current_level, Node::GROUP, m_controller->m_allocator);
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
		
		if(ImGui::CollapsingHeader("Slots")) {
			for (u32 i = 0; i < (u32)m_controller->m_animation_slots.size(); ++i) {
				String& slot = m_controller->m_animation_slots[i];
				char tmp[64];
				copyString(tmp, slot.c_str());
				if (ImGui::InputText(StaticString<32>("##", (u64)(uintptr_t)&slot), tmp, sizeof(tmp))) {
					// update AnimationNode::m_animation_hash
					slot = tmp;
				}
			}
			if (ImGui::Button("Create")) {
				m_controller->m_animation_slots.emplace("", m_controller->m_allocator);
			}
		}

		if(ImGui::CollapsingHeader("Animations")) {
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
				const int slot_idx = m_controller->m_animation_slots.find([&](const String& value){
					return crc32(value.c_str()) == entry.slot_hash;
				});
				const char* preview = slot_idx >= 0 ? m_controller->m_animation_slots[slot_idx].c_str() : "N/A";
					"";
				ImGui::PushItemWidth(-1);
				if (ImGui::BeginCombo("##slot", preview, 0)) {
					for (u32 i = 0, c = m_controller->m_animation_slots.size(); i < c; ++i) {
						if (ImGui::Selectable(m_controller->m_animation_slots[i].c_str())) {
							entry.slot_hash = crc32(m_controller->m_animation_slots[i].c_str());
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
					ResourceManagerHub& res_manager = m_app.getWorldEditor().getEngine().getResourceManager();
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
				entry.slot_hash = 0;
			}
		}

		if(ImGui::CollapsingHeader("Structure")) {
			GroupNode* parent = m_current_level;
			bool use_root_motion = m_controller->m_flags.isSet(Controller::Flags::USE_ROOT_MOTION);
			if (ImGui::Checkbox("Use root motion", &use_root_motion)) {
				m_controller->m_flags.set(Controller::Flags::USE_ROOT_MOTION, use_root_motion);
			}
			for (u32 i = 0; i < (u32)parent->m_children.size(); ++i) {
				GroupNode::Child& ch = parent->m_children[i];
				if (ImGui::TreeNodeEx(ch.node, 0, "%s", ch.node->m_name.c_str())) {
					char tmp[256];
					copyString(tmp, ch.condition_str.c_str());
					if (ImGui::InputText("Condition", tmp, sizeof(tmp), ImGuiInputTextFlags_EnterReturnsTrue)) {
						ch.condition_str = tmp;
						ch.condition.compile(tmp, m_controller->m_inputs);
					}
					ui_dispatch(*ch.node, *this);
					ImGui::TreePop();
				}
			}
		}
	}
	ImGui::End();
}


} // ns Lumix::Anim