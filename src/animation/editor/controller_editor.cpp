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

static bool editInput(const char* label, u32* input_index, const Controller& controller) {
	ASSERT(input_index);
	bool changed = false;
	ImGuiEx::Label(label);
	if (controller.m_inputs.inputs_count == 0) {
		ImGui::Text("No inputs");
		return false;
	}
	const InputDecl::Input& current_input = controller.m_inputs.inputs[*input_index];
	if (ImGui::BeginCombo("##input", current_input.name)) {
		for (const InputDecl::Input& input : controller.m_inputs.inputs) {
			if (input.type == InputDecl::EMPTY) continue;
			if (ImGui::Selectable(input.name)) {
				changed = true;
				*input_index = u32(&input - controller.m_inputs.inputs);
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

struct ControllerEditorImpl : ControllerEditor, AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct SetInputEventType : EventType {
		SetInputEventType() {
			type = RuntimeHash("set_input");
			label = "Set input";
			size = sizeof(u32) + sizeof(float);
		}

		bool onGUI(u8* data, const Controller& controller) const override {
			bool changed = editInput("Input", (u32*)data, controller);
			if (changed) {
				memset(data + sizeof(u32), 0, sizeof(float));
			}
			const u32 input_index = *(u32*)data;
			auto& inputs = controller.m_inputs;
			if (inputs.inputs_count == 0) return changed;
			ImGuiEx::Label("Value");
			switch (inputs.inputs[input_index].type) {
				case InputDecl::BOOL: {
					bool b = *(u32*)(data + sizeof(u32)) != 0;
					if (ImGui::Checkbox("##v", &b)) {
						changed = true;
						*(u32*)(data + sizeof(u32)) = b;
					}
					break;
				}
				case InputDecl::FLOAT:
					changed = ImGui::DragFloat("##v", (float*)(data + sizeof(u32))) || changed;
					break;
				case InputDecl::U32:
					changed = ImGui::DragInt("##v", (i32*)(data + sizeof(u32))) || changed;
					break;
				default: ASSERT(false); break;
			}
			return changed;
		}
	};

	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, ControllerEditorImpl& plugin, StudioApp& app, IAllocator& allocator)
			: AssetEditorWindow(app)
			, m_allocator(allocator)
			, m_app(app)
			, m_plugin(plugin)
			, m_undo_stack(allocator)
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
					m_undo_idx = -1;
					pushUndo();
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

		bool onAction(const Action& action) override {
			const CommonActions& actions = m_app.getCommonActions();
			if (&action == &actions.save) saveAs(m_path);
			else if (&action == &actions.undo) undo();
			else if (&action == &actions.redo) redo();
			else return false;
			return true;
		}

		Node* createRoot(Node::Type type, IAllocator& allocator) {
			Node* node = nullptr;
			switch(type) {
				case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(nullptr, allocator); break;
				case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(nullptr, allocator); break;
				case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(nullptr, allocator); break;
				case Node::CONDITION: node = LUMIX_NEW(allocator, ConditionNode)(nullptr, allocator); break;
				case Node::LAYERS: node = LUMIX_NEW(allocator, LayersNode)(nullptr, allocator); break;
				case Node::SELECT: node = LUMIX_NEW(allocator, SelectNode)(nullptr, allocator); break;
				case Node::NONE: ASSERT(false); return nullptr;
			}

			node->m_name = "root";
			m_controller.m_root = node;
			pushUndo();
			return node;
		}

		Node* createChild(GroupNode& parent, Node::Type type, IAllocator& allocator) {
			Node* node = nullptr;
			switch(type) {
				case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
				case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
				case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(&parent, allocator); break;
				case Node::CONDITION: node = LUMIX_NEW(allocator, ConditionNode)(&parent, allocator); break;
				case Node::LAYERS: node = LUMIX_NEW(allocator, LayersNode)(&parent, allocator); break;
				case Node::SELECT: node = LUMIX_NEW(allocator, SelectNode)(&parent, allocator); break;
				case Node::NONE: ASSERT(false); return nullptr;
			}

			node->m_name = "new";
			parent.m_children.emplace(allocator);
			parent.m_children.back().node = node;
			pushUndo();
			return node;
		}

		Node* createChild(SelectNode& parent, Node::Type type, IAllocator& allocator) {
			Node* node = nullptr;
			switch(type) {
				case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
				case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
				case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(&parent, allocator); break;
				case Node::CONDITION: node = LUMIX_NEW(allocator, ConditionNode)(&parent, allocator); break;
				case Node::LAYERS: node = LUMIX_NEW(allocator, LayersNode)(&parent, allocator); break;
				case Node::SELECT: node = LUMIX_NEW(allocator, SelectNode)(&parent, allocator); break;
				case Node::NONE: ASSERT(false); return nullptr;
			}

			node->m_name = "new";
			parent.m_children.emplace();
			parent.m_children.back().node = node;
			pushUndo();
			return node;
		}

		void deleteNode(Node& node) {
			if (m_current_node == &node) m_current_node = nullptr;
						
			if (!node.m_parent) { 
				m_controller.m_root = nullptr;
			}
			else {
				switch(node.m_parent->type()) {
					case Node::NONE:
					case Node::BLEND1D:
					case Node::ANIMATION: ASSERT(false); break;
					case Node::LAYERS: 
						((LayersNode*)node.m_parent)->m_layers.eraseItems([&node](LayersNode::Layer& c){ return c.node == &node; });
						break;
					case Node::CONDITION: {
						ConditionNode* cond = (ConditionNode*)node.m_parent;
						if (cond->m_true_node == &node) cond->m_true_node = nullptr;
						else if (cond->m_false_node == &node) cond->m_false_node = nullptr;
						break;
					}
					case Node::SELECT:
						((SelectNode*)node.m_parent)->m_children.eraseItems([&node](SelectNode::Child& c){ return c.node == &node; });
						break;
					case Node::GROUP:
						((GroupNode*)node.m_parent)->m_children.eraseItems([&node](GroupNode::Child& c){ return c.node == &node; });
						break;
				}
			} 
				
			LUMIX_DELETE(m_controller.m_allocator, &node);
		}

		Node* createChild(Node& parent, Node::Type type, IAllocator& allocator) {
			switch(parent.type()) {
				case Node::BLEND1D:
				case Node::NONE:
				case Node::ANIMATION: ASSERT(false); return nullptr;
				case Node::GROUP: return createChild((GroupNode&)parent, type, allocator);
				case Node::CONDITION: return createChild((ConditionNode&)parent, type, allocator);
				case Node::LAYERS: return createChild((LayersNode&)parent, type, allocator);
				case Node::SELECT: return createChild((SelectNode&)parent, type, allocator);
			}
			ASSERT(false);
			return nullptr;
		}

		Node* createChild(LayersNode& parent, Node::Type type, IAllocator& allocator) {
			Node* node = nullptr;
			switch(type) {
				case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
				case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
				case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(&parent, allocator); break;
				case Node::CONDITION: node = LUMIX_NEW(allocator, ConditionNode)(&parent, allocator); break;
				case Node::LAYERS: node = LUMIX_NEW(allocator, LayersNode)(&parent, allocator); break;
				case Node::SELECT: node = LUMIX_NEW(allocator, SelectNode)(&parent, allocator); break;
				case Node::NONE: ASSERT(false); return nullptr;
			}
			node->m_name = "new";
			LayersNode::Layer& layer = parent.m_layers.emplace(allocator);
			layer.node = node;
			pushUndo();
			return node;
		}

		Node* createChild(ConditionNode& parent, Node::Type type, IAllocator& allocator) {
			if (parent.m_true_node && parent.m_false_node) return nullptr;

			Node* node = nullptr;
			switch(type) {
				case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
				case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
				case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(&parent, allocator); break;
				case Node::CONDITION: node = LUMIX_NEW(allocator, ConditionNode)(&parent, allocator); break;
				case Node::SELECT: node = LUMIX_NEW(allocator, SelectNode)(&parent, allocator); break;
				case Node::LAYERS: ASSERT(false); return nullptr;
				case Node::NONE: ASSERT(false); return nullptr;
			}

			node->m_name = "new";
			if (!parent.m_true_node) {
				parent.m_true_node = node;
			}
			else {
				parent.m_false_node = node;
			}
			pushUndo();
			return node;
		}

		void properties_ui(AnimationNode& node) {
			ImGuiEx::Label("Animation");
			saveUndo(inputSlot("##anim", &node.m_slot));

			bool looped = node.m_flags & AnimationNode::LOOPED;
			ImGuiEx::Label("Looped");
			if (ImGui::Checkbox("##looped", &looped)) {
				if (looped) {
					node.m_flags = node.m_flags | AnimationNode::LOOPED;
				}
				else {
					node.m_flags = node.m_flags & ~AnimationNode::LOOPED;
				}
				saveUndo(true);
			}
		}

		const char* getName(const Node& node) const {
			if (node.type() == Node::ANIMATION) {
				const u32 slot = ((AnimationNode&)node).m_slot;
				if (slot < (u32)m_controller.m_animation_slots.size()) {
					return m_controller.m_animation_slots[slot].c_str();
				}
				return "Deleted slot";
			}
			return node.m_name.c_str();
		}

		void properties_ui(GroupNode& node) {
			float node_blend_length = node.m_blend_length.seconds();
			ImGuiEx::Label("Blend length");
			if (ImGui::DragFloat("##bl", &node_blend_length)) {
				node.m_blend_length = Time::fromSeconds(node_blend_length);
				saveUndo(true);
			}

			if (ImGui::TreeNode("Transitions")) {
				for (GroupNode::Transition& tr : node.m_transitions) {
					const char* name_from = tr.from == 0xFFffFFff ? "*" : getName(*node.m_children[tr.from].node);
					const char* name_to = tr.to == 0xFFffFFff ? "*" : getName(*node.m_children[tr.to].node);
					const bool open = ImGui::TreeNodeEx(&tr, 0, "%s -> %s", name_from, name_to);
					if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) ImGui::OpenPopup("context_menu");
					if (ImGui::BeginPopup("context_menu")) {
						if (ImGui::Selectable("Remove")) {
							node.m_transitions.erase(u32(&tr - node.m_transitions.begin()));
							pushUndo();
							ImGui::EndPopup();
							if (open) ImGui::TreePop();
							break;
						}
						ImGui::EndPopup();
					}
					if (open) {
						saveUndo(inputChild("From", tr.from, node.m_children, true));
						saveUndo(inputChild("To", tr.to, node.m_children, true));
						bool has_exit_time = tr.exit_time >= 0;
						
						ImGuiEx::Label("Blend length");
						float l = tr.blend_length.seconds();
						if (ImGui::DragFloat("##bl", &l, 0.1f, 0.0, FLT_MAX, "%.2f")) {
							tr.blend_length = Time::fromSeconds(l);
							saveUndo(true);
						}
						
						ImGuiEx::Label("Has exit time");
						if (ImGui::Checkbox("##het", &has_exit_time)) {
							if (has_exit_time) tr.exit_time = 0;
							else tr.exit_time = -1;
							saveUndo(true);
						}
						if (has_exit_time) {
							ImGuiEx::Label("Exit time");
							saveUndo(ImGui::DragFloat("##et", &tr.exit_time, 0.01f, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp));
						}

						ImGui::TreePop();
					}
				}

				if (!node.m_children.empty() && ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
					node.m_transitions.emplace();
					pushUndo();
				}

				ImGui::TreePop();
			}
		}

		bool properties_ui(SelectNode& node) {
			const InputDecl::Input& current_input = m_controller.m_inputs.inputs[node.m_input_index];
			bool changed = false;
			ImGuiEx::Label("Input");
			if (ImGui::BeginCombo("##input", current_input.name)) {
				for (const InputDecl::Input& input : m_controller.m_inputs.inputs) {
					if (input.type == InputDecl::EMPTY) continue;
					if (ImGui::Selectable(input.name)) {
						changed = true;
						node.m_input_index = u32(&input - m_controller.m_inputs.inputs);
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		void properties_ui(ConditionNode& node) {
			if (conditionInput("Condition", m_controller.m_inputs, node.m_condition_str, node.m_condition)) pushUndo();

			float node_blend_length = node.m_blend_length.seconds();
			ImGuiEx::Label("Blend length");
			if (ImGui::DragFloat("##bl", &node_blend_length)) {
				node.m_blend_length = Time::fromSeconds(node_blend_length);
				saveUndo(true);
			}
		}

		void properties_ui(Blend1DNode& node) {
			const InputDecl::Input& current_input = m_controller.m_inputs.inputs[node.m_input_index];
			ImGuiEx::Label("Input");
			if (ImGui::BeginCombo("##input", current_input.name)) {
				bool selected = false;
				for (const InputDecl::Input& input : m_controller.m_inputs.inputs) {
					if (input.type == InputDecl::EMPTY) continue;
					if (ImGui::Selectable(input.name.empty() ? "##tmp" : input.name)) {
						selected = true;
						node.m_input_index = u32(&input - m_controller.m_inputs.inputs);
					}
				}
				ImGui::EndCombo();
				saveUndo(selected);
			}

			ImGui::Columns(2);
			ImGui::Text("Value");
			ImGui::NextColumn();
			ImGui::Text("Slot");
			ImGui::NextColumn();
			ImGui::Separator();

			for (Blend1DNode::Child& child : node.m_children) {
				ImGui::PushID(&child);
		
				ImGui::SetNextItemWidth(-1);
				saveUndo(ImGui::InputFloat("##val", &child.value));
				ImGui::NextColumn();
		
				ImGui::SetNextItemWidth(-1);
				saveUndo(inputSlot("##anim", &child.slot));
				ImGui::NextColumn();

				ImGui::PopID();
			}
			ImGui::Columns();

			if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
				node.m_children.emplace();
				if(node.m_children.size() > 1) {
					node.m_children.back().value = node.m_children[node.m_children.size() - 2].value;
				}
				pushUndo();
			}
		}

		void child_properties_ui(Node& node) {
			if (!node.m_parent) return;

			switch (node.m_parent->type()) {
				default: break;
				case Node::Type::SELECT: {
					SelectNode* select = (SelectNode*)node.m_parent;
					for (SelectNode::Child& c : select->m_children) {
						if (c.node != &node) continue;
					
						const InputDecl::Input& input = m_controller.m_inputs.inputs[select->m_input_index];
						ImGuiEx::Label(StaticString<64>(input.name, " <= "));
						saveUndo(ImGui::DragFloat("##mv", &c.max_value));
						// TODO sort children by c.max_value
					}
					break;
				}
				case Node::Type::LAYERS:
					if (!m_controller.m_bone_masks.empty()) {
						LayersNode* layers = (LayersNode*)node.m_parent;
						i32 idx = layers->m_layers.find([&](const LayersNode::Layer& l){ return l.node == &node; });
						ASSERT(idx >= 0);

						const char* preview = m_controller.m_bone_masks[layers->m_layers[idx].mask].name;
						ImGuiEx::Label("Bone mask");
						if (ImGui::BeginCombo("##bonemask", preview)) {
							for (const BoneMask& mask : m_controller.m_bone_masks) {
								if (ImGui::Selectable(mask.name)) {
									layers->m_layers[idx].mask = u32(&mask - m_controller.m_bone_masks.begin());
								}
							}
							ImGui::EndCombo();
						}
					}
					break;
				case Node::Type::GROUP: {
					GroupNode* group = (GroupNode*)node.m_parent;
					for (GroupNode::Child& c : group->m_children) {
						if (c.node != &node) continue;
				
						saveUndo(conditionInput("Group condition", m_controller.m_inputs, c.condition_str, c.condition));
						bool selectable = c.flags & GroupNode::Child::SELECTABLE;
						ImGuiEx::Label("Selectable");
						if (ImGui::Checkbox("##sel", &selectable)) {
							if (selectable) c.flags |= GroupNode::Child::SELECTABLE;
							else c.flags &= ~GroupNode::Child::SELECTABLE;
							saveUndo(true);
						}
						break;
					}
					break;
				}
			}
		}

		void addEvent(OutputMemoryStream& events, const EventType& type) {
			events.write(type.type);
			events.write(type.size);
			u16 rel_time = 0;
			events.write(rel_time);
			const u32 ptr = (u32)events.size();
			events.resize(events.size() + type.size);
			memset(events.getMutableData() + ptr, 0, type.size);
		}

		void removeEvent(OutputMemoryStream& events, u32 idx) const {
			OutputMemoryStream tmp(m_allocator);
			InputMemoryStream blob(events);
			u32 i = 0;
			while(blob.getPosition() != blob.size()) {
				const u32 type = blob.read<u32>();
				const u16 data_size = blob.read<u16>();
				u16* rel_time = (u16*)blob.skip(sizeof(u16));
				u8* data = (u8*)blob.skip(data_size);
				if (i != idx) {
					tmp.write(type);
					tmp.write(data_size);
					tmp.write(*rel_time);
					tmp.write(data, data_size);
				}
				++i;
			}
			events = static_cast<OutputMemoryStream&&>(tmp);
		}

		void editEvents(OutputMemoryStream& events) {
			if (!ImGui::TreeNode("Events")) return;

			if (!events.empty()) {
				u32 i = 0;
				InputMemoryStream blob(events);
				while(blob.getPosition() != blob.size()) {
					++i;
					const RuntimeHash type = blob.read<RuntimeHash>();
					const u16 data_size = blob.read<u16>();
					const EventType& type_obj = m_plugin.getEventType(type);
					ASSERT(data_size == type_obj.size);
					u16* rel_time = (u16*)blob.skip(sizeof(u16));
					u8* data = (u8*)blob.skip(type_obj.size);
				
					bool open = ImGui::TreeNodeEx((void*)(uintptr)i, ImGuiTreeNodeFlags_AllowItemOverlap, "%d %s", i, type_obj.label.data);
					ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_TIMES_CIRCLE).x);
					if (ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Delete")) {
						ImGui::TreePop();
						removeEvent(events, i - 1);
						pushUndo();
						break;
					}
				
					if (open) {
						ImGuiEx::Label("Time");
						float t = *rel_time / float(0xffff);
						if (ImGui::DragFloat("##t", &t, 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
							*rel_time = u16(t * 0xffFF);
							saveUndo(true);
						}
						if (type_obj.onGUI(data, m_controller)) pushUndo();
						ImGui::TreePop();
					}
				}
			}

			if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) ImGui::OpenPopup("add_event_popup");
			if (ImGui::BeginPopup("add_event_popup")) {
				for (const UniquePtr<EventType>& type : m_plugin.m_event_types) {
					if (ImGui::Selectable(type->label)) {
						addEvent(events, *type.get());
						pushUndo();
					}
				}
				ImGui::EndPopup();
			}
			ImGui::TreePop();
		}

		void ui_dispatch(Node& node) {
			if (node.type() != Node::ANIMATION) {
				char tmp[64];
				copyString(tmp, getName(node));

				ImGuiEx::Label("Name");
				if (ImGui::InputText("##name", tmp, sizeof(tmp))) {
					node.m_name = tmp;
					saveUndo(true);
				}
			}

			child_properties_ui(node);

			switch(node.type()) {
				case Node::ANIMATION: properties_ui((AnimationNode&)node); break;
				case Node::GROUP: properties_ui((GroupNode&)node); break;
				case Node::BLEND1D: properties_ui((Blend1DNode&)node); break;
				case Node::CONDITION: properties_ui((ConditionNode&)node); break;
				case Node::SELECT: properties_ui((SelectNode&)node); break;
				case Node::LAYERS: break;
				case Node::NONE: ASSERT(false); break;
			}

			editEvents(node.m_events);
		}

		static bool conditionInput(const char* label, InputDecl& input, String& condition_str, Condition& condition) {
			char tmp[1024];
			copyString(tmp, condition_str);
			if (condition.error != Condition::Error::NONE) {
				ImGui::TextUnformatted(Condition::errorToString(condition.error));
			}
			ImGuiEx::Label(label);
			ImGui::PushID(label);
			if (ImGui::InputText("##ci", tmp, sizeof(tmp), ImGuiInputTextFlags_EnterReturnsTrue)) {
				condition_str = tmp;
				condition.compile(tmp, input);
				ImGui::PopID();
				return true;
			}

			ImGui::PopID();
			return false;
		}

		bool inputChild(const char* label, u32& value, const Array<GroupNode::Child>& children, bool has_any) const {
			ImGuiEx::Label(label);		
			bool changed = false;
			if (!ImGui::BeginCombo(StaticString<64>("##_", label), value == 0xffFFffFF ? "*" : getName(*children[value].node))) return false;

			if (has_any) {
				if (ImGui::Selectable("*")) {
					value = 0xffFFffFF;
					changed = true;
				}
			}

			for (GroupNode::Child& child : children) {
				if (ImGui::Selectable(getName(*child.node))) {
					value = u32(&child - children.begin());
					changed = true;
				}
			}

			ImGui::EndCombo();
			return changed;
		}

		static const char* toString(Node::Type type) {
			switch (type) {
				case Node::Type::ANIMATION: return "Animation";
				case Node::Type::BLEND1D: return "Blend 1D";
				case Node::Type::GROUP: return "Group";
				case Node::Type::LAYERS: return "Layers";
				case Node::Type::CONDITION: return "Condition";
				case Node::Type::SELECT: return "Select";
				case Node::Type::NONE: return "N/A";
			}
			ASSERT(false);
			return "N/A";
		}

		static bool isContainer(const Node& node) {
			switch (node.type()) {
				case Node::Type::ANIMATION: 
				case Node::Type::BLEND1D: return false;
				case Node::Type::CONDITION:
				case Node::Type::GROUP:
				case Node::Type::SELECT:
				case Node::Type::LAYERS: return true;
				case Node::Type::NONE:
					ASSERT(false);
					return false;
			}
			ASSERT(false);
			return false;
		}

		bool isSelectable(Node& node) {
			if (!node.m_parent) return false;
			if (node.m_parent->type() == Node::SELECT) return true;
			if (node.m_parent->type() == Node::CONDITION) return true;
			if (node.m_parent->type() == Node::GROUP) {
				GroupNode* group = (GroupNode*)node.m_parent;
				for (auto& c : group->m_children) {
					if (c.node == &node && (c.flags & GroupNode::Child::SELECTABLE)) return true;
				}
			}
			return false;
		}

		void copyChildData(Node& src, Node& dst) {
			if (!dst.m_parent) return;
			if (!src.m_parent) return;
			if (src.m_parent->type() != dst.m_parent->type()) return;

			switch (src.m_parent->type()) {
				case Node::NONE:
				case Node::ANIMATION:
				case Node::BLEND1D: ASSERT(false); break;
				case Node::CONDITION: break;
				case Node::LAYERS: {
					auto* src_p = (LayersNode*)src.m_parent;
					auto* dst_p = (LayersNode*)dst.m_parent;
					for (LayersNode::Layer& s : src_p->m_layers) {
						if (s.node == &src) {
							for (LayersNode::Layer& c : dst_p->m_layers) {
								if (c.node == &dst) {
									c.mask = s.mask;
									c.name = s.name;
									break;
								}
							}
							break;
						}
					}
					break;
				}
				case Node::SELECT: {
					auto* src_p = (SelectNode*)src.m_parent;
					auto* dst_p = (SelectNode*)dst.m_parent;
					if (src_p->m_input_index != dst_p->m_input_index) return;

					for (SelectNode::Child& s : src_p->m_children) {
						if (s.node == &src) {
							for (SelectNode::Child& c : dst_p->m_children) {
								if (c.node == &dst) {
									c.max_value = s.max_value;
									break;
								}
							}
							break;
						}
					}
					break;
				}
				case Node::GROUP:
					for (GroupNode::Child& s : ((GroupNode*)src.m_parent)->m_children) {
						if (s.node == &src) {
							for (GroupNode::Child& c : ((GroupNode*)dst.m_parent)->m_children) {
								if (c.node == &dst) {
									c.flags = s.flags;
									c.condition_str = s.condition_str;
									c.condition.compile(s.condition_str.c_str(), m_controller.m_inputs);
									break;
								}
							}
							break;
						}
					}
					break;
			}
		}

		void hierarchy_ui(Node& node) {
			const bool is_container = isContainer(node);
			const bool is_parent_group = node.m_parent && node.m_parent->type() == Node::Type::GROUP;
			const bool is_parent_condition = node.m_parent && node.m_parent->type() == Node::Type::CONDITION;
			const bool is_layer = node.m_parent && node.m_parent->type() == Node::Type::LAYERS;
			const bool is_group = node.type() == Node::Type::GROUP;

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | (&node == m_current_node ? ImGuiTreeNodeFlags_Selected : 0);
			if (!is_container) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet; 

			const char* type_str = toString(node.type());
			const bool is_selectable = isSelectable(node);
			if (!is_selectable) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			bool open;
			ImGui::PushID(&node);
			if (node.type() == Node::ANIMATION) {
				open = ImGui::TreeNodeEx(&node, flags, "%s", getName(node));
			}
			else {
				open = ImGui::TreeNodeEx(&node, flags, "%s (%s)", getName(node), type_str);
			}

			if (ImGui::BeginDragDropSource()) {
				ImGui::TextUnformatted(getName(node));
				void* ptr = &node;
				ImGui::SetDragDropPayload("anim_node", &ptr, sizeof(ptr));
				ImGui::EndDragDropSource();
			}

			if (is_container) {
				if (ImGui::BeginDragDropTarget()) {
					if (auto* payload = ImGui::AcceptDragDropPayload("anim_node")) {
						Node* dropped_node = *(Node**)payload->Data;
						ASSERT(dropped_node->m_parent);
						Node* new_node = createChild(node, dropped_node->type(), m_controller.m_allocator);
						if (new_node) {
							OutputMemoryStream blob(m_app.getAllocator());
							dropped_node->serialize(blob);
							InputMemoryStream iblob(blob);
							new_node->deserialize(iblob, m_controller, (u32)ControllerVersion::LATEST);
							copyChildData(*dropped_node, *new_node);
							if (m_current_node == dropped_node) m_current_node = nullptr;
							deleteNode(*dropped_node);
							ImGui::TreePop();
							ImGui::PopID();
							if (!is_selectable) ImGui::PopStyleColor();
							pushUndo();
							return;
						}
					}
				}
			}

			if (!is_selectable) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				if (ImGui::IsMouseClicked(0)) m_current_node = &node;
				if (ImGui::IsMouseClicked(1)) ImGui::OpenPopup("popup");
			}

			if (ImGui::BeginPopup("popup")) {
				ImGui::TextUnformatted(getName(node));
				ImGui::Separator();

				switch (node.type()) {
					case Node::Type::NONE: break;
					case Node::Type::ANIMATION: break;
					case Node::Type::BLEND1D: break;
					case Node::Type::LAYERS: {
						LayersNode& layers = (LayersNode&)node;
						if (ImGui::BeginMenu("Create layer")) {
							if (ImGui::MenuItem("Animation")) createChild(layers, Node::ANIMATION, m_controller.m_allocator);
							if (ImGui::MenuItem("Blend1D")) createChild(layers, Node::BLEND1D, m_controller.m_allocator);
							if (ImGui::MenuItem("Condition")) createChild(layers, Node::CONDITION, m_controller.m_allocator);
							if (ImGui::MenuItem("Group")) createChild(layers, Node::GROUP, m_controller.m_allocator);
							if (ImGui::MenuItem("Select")) createChild(layers, Node::SELECT, m_controller.m_allocator);
							ImGui::EndMenu();
						}
						break;
					}
					case Node::Type::CONDITION: {
						ConditionNode& cond = (ConditionNode&)node;
						if ((!cond.m_true_node || !cond.m_false_node) && ImGui::BeginMenu("Create child")) {
							if (ImGui::MenuItem("Animation")) createChild(cond, Node::ANIMATION, m_controller.m_allocator);
							if (ImGui::MenuItem("Blend1D")) createChild(cond, Node::BLEND1D, m_controller.m_allocator);
							if (ImGui::MenuItem("Condition")) createChild(cond, Node::CONDITION, m_controller.m_allocator);
							if (ImGui::MenuItem("Group")) createChild(cond, Node::GROUP, m_controller.m_allocator);
							if (ImGui::MenuItem("Select")) createChild(cond, Node::SELECT, m_controller.m_allocator);
							ImGui::EndMenu();
						}
						break;
					}
					case Node::Type::SELECT: {
						SelectNode& select = (SelectNode&)node;
						if (ImGui::BeginMenu("Create child")) {
							if (ImGui::MenuItem("Animation")) createChild(select, Node::ANIMATION, m_controller.m_allocator);
							if (ImGui::MenuItem("Blend1D")) createChild(select, Node::BLEND1D, m_controller.m_allocator);
							if (ImGui::MenuItem("Condition")) createChild(select, Node::CONDITION, m_controller.m_allocator);
							if (ImGui::MenuItem("Group")) createChild(select, Node::GROUP, m_controller.m_allocator);
							if (ImGui::MenuItem("Select")) createChild(select, Node::SELECT, m_controller.m_allocator);
							ImGui::EndMenu();
						}
						break;
					}
					case Node::Type::GROUP: {
						if (ImGui::BeginMenu("Create child")) {
							GroupNode& group = (GroupNode&)node;
							if (ImGui::MenuItem("Animation")) createChild(group, Node::ANIMATION, m_controller.m_allocator);
							if (ImGui::MenuItem("Blend1D")) createChild(group, Node::BLEND1D, m_controller.m_allocator);
							if (ImGui::MenuItem("Condition")) createChild(group, Node::CONDITION, m_controller.m_allocator);
							if (ImGui::MenuItem("Group")) createChild(group, Node::GROUP, m_controller.m_allocator);
							if (ImGui::MenuItem("Layers")) createChild(group, Node::LAYERS, m_controller.m_allocator);
							if (ImGui::MenuItem("Select")) createChild(group, Node::SELECT, m_controller.m_allocator);
							ImGui::EndMenu();
						}
						break;
					}
				}

				if (ImGui::Selectable("Copy")) {
					m_copy_buffer.node_type = node.type();
					m_copy_buffer.data.clear();
					node.serialize(m_copy_buffer.data);
				}
				if (ImGui::Selectable("Paste", false, m_copy_buffer.data.empty() ? ImGuiSelectableFlags_Disabled : 0)) {
						
					Node* pasted = nullptr;
					switch (node.type()) {
						case Node::Type::NONE:
						case Node::Type::ANIMATION:
						case Node::Type::BLEND1D:
							break;
						case Node::Type::SELECT: pasted = createChild((SelectNode&)node, m_copy_buffer.node_type, m_controller.m_allocator); break;
						case Node::Type::GROUP: pasted = createChild((GroupNode&)node, m_copy_buffer.node_type, m_controller.m_allocator); break;
						case Node::Type::CONDITION: pasted = createChild((ConditionNode&)node, m_copy_buffer.node_type, m_controller.m_allocator); break;
						case Node::Type::LAYERS: pasted = createChild((LayersNode&)node, m_copy_buffer.node_type, m_controller.m_allocator); break;
					}
					if (pasted) {
						InputMemoryStream blob(m_copy_buffer.data);
						pasted->deserialize(blob, m_controller, (u32)ControllerVersion::LATEST);
					}
				}

				if (ImGui::Selectable("Remove")) {
					deleteNode(node);
					ImGui::EndPopup();
					ImGui::TreePop();
					ImGui::PopID();
					pushUndo();
					return;
				}

				ImGui::EndPopup();
			}

			if (open) {
				switch (node.type()) {
					case Node::Type::NONE:
					case Node::Type::BLEND1D:
					case Node::Type::ANIMATION: break;
					case Node::Type::GROUP: {
						GroupNode& group = (GroupNode&)node;
						for (GroupNode::Child& c : group.m_children) {
							hierarchy_ui(*c.node);
						}
						break;
					}
					case Node::Type::SELECT: {
						SelectNode& select = (SelectNode&)node;
						for (SelectNode::Child& c : select.m_children) {
							hierarchy_ui(*c.node);
						}
						break;
					}
					case Node::Type::CONDITION: {
						ConditionNode& cond = (ConditionNode&)node;
						if (cond.m_true_node) hierarchy_ui(*cond.m_true_node);
						if (cond.m_false_node) hierarchy_ui(*cond.m_false_node);
						break;
					}
					case Node::Type::LAYERS: {
						LayersNode& layers = (LayersNode&)node;
						for (LayersNode::Layer& l : layers.m_layers) {
							hierarchy_ui(*l.node);
						}
						break;
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		void previewUI() { 
			bool open = m_preview_on_right || ImGui::CollapsingHeader("Preview", nullptr, ImGuiTreeNodeFlags_AllowOverlap);
			if (!m_preview_on_right) ImGui::SameLine(0, 25);
			if (ImGuiEx::IconButton(m_preview_on_right ? ICON_FA_ARROW_LEFT : ICON_FA_ARROW_RIGHT, m_preview_on_right ? "Move to left" : "Move to right")) m_preview_on_right = !m_preview_on_right;
			if (!open) return;

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
			ImGuiEx::Label("Focus mesh"); 
			ImGui::Checkbox("##fm", &m_viewer.m_focus_mesh);
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
			if (!ImGui::CollapsingHeader("Debugger")) return;

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
			
			for (const InputDecl::Input& input : ctrl->m_inputs.inputs) {
				ImGui::PushID(&input);
				const u32 idx = u32(&input - ctrl->m_inputs.inputs);
				switch (input.type) {
					case InputDecl::Type::EMPTY: break;
					case InputDecl::Type::FLOAT: {
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
					case InputDecl::Type::BOOL: {
						bool val = module->getAnimatorBoolInput(entity, idx);
						ImGuiEx::Label(input.name);
						if (ImGui::Checkbox("##i", &val)) {
							module->setAnimatorInput(entity, idx, val);
						}
						break;
					}
					case InputDecl::Type::U32: {
						u32 val = module->getAnimatorU32Input(entity, idx);
						ImGuiEx::Label(input.name);
						if (ImGui::DragInt("##i", (int*)&val, 1, 0, 0x7ffFFff)) {
							module->setAnimatorInput(entity, idx, val);
						}
						break;
					}
				}
				ImGui::PopID();
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

		void pushUndo(u64 tag = ~u64(0)) {
			while (m_undo_idx >= m_undo_stack.size()) m_undo_stack.pop();

			if (tag == ~u64(0) || m_undo_stack.back().tag != tag) {
				UndoRecord& r = m_undo_stack.emplace(m_allocator);
				r.tag = tag;
				m_controller.serialize(r.data);
				m_undo_idx = m_undo_stack.size() - 1;
				return;
			}

			m_undo_stack.back().data.clear();
			m_controller.serialize(m_undo_stack.back().data);
			m_dirty = true;
		}

		void undoRedo() {
			ResourceManager* res_manager = m_app.getEngine().getResourceManager().get(Controller::TYPE);
			m_controller.clear();
			InputMemoryStream tmp(m_undo_stack[m_undo_idx].data);
			bool success = m_controller.deserialize(tmp);
			ASSERT(success);
			m_current_node = m_controller.m_root;
		}

		void redo() {
			if (m_undo_idx >= m_undo_stack.size() - 1) return;
		
			++m_undo_idx;
			undoRedo();
		}

		void undo() {
			if (m_undo_idx <= 0) return;

			--m_undo_idx;
			undoRedo();
		}

		bool canUndo() const { return m_undo_idx > 0; }
		bool canRedo() const { return m_undo_idx < m_undo_stack.size() - 1; }

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
										pushUndo();
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
								--ik.bones_count;
								pushUndo();
							}
						} 

						ImGui::TreePop();
					}
				}

				if (m_controller.m_ik_count < (u32)lengthOf(m_controller.m_ik) && ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
					m_controller.m_ik[m_controller.m_ik_count].bones_count = 1;
					m_controller.m_ik[m_controller.m_ik_count].bones[0] = BoneNameHash();
					++m_controller.m_ik_count;
					pushUndo();
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
			pushUndo();
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
				pushUndo();
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
				pushUndo();
			}

			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("path")) {
					const char* path = (const char*)payload->Data;
					StringView subres = Path::getSubresource(path);
					if (Path::hasExtension(subres, "ani")) {
						Controller::AnimationEntry& entry = m_controller.m_animation_entries.emplace();
						ResourceManagerHub& res_manager = m_app.getEngine().getResourceManager();
						entry.animation = res_manager.load<Animation>(Path(path));
						entry.set = 0;
						entry.slot = m_controller.m_animation_slots.size();

						m_controller.m_animation_slots.emplace(Path::getBasename(path), m_controller.m_allocator);

						pushUndo();
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		bool inputSlot(const char* str_id, u32* slot) {
			bool changed = false;
			const char* preview = *slot < (u32)m_controller.m_animation_slots.size() ? m_controller.m_animation_slots[*slot].c_str() : "N/A";
			if (ImGui::BeginCombo(str_id, preview, 0)) {
				static char filter[64] = "";
				ImGuiEx::filter("Filter", filter, sizeof(filter), -1, ImGui::IsWindowAppearing());
				bool selected = false;
				for (u32 i = 0, c = m_controller.m_animation_slots.size(); i < c; ++i) {
					const char* name = m_controller.m_animation_slots[i].c_str();
					if ((!filter[0] || findInsensitive(name, filter)) && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::Selectable(name))) {
						*slot = i;
						changed = true;
						filter[0] = '\0';
						ImGui::CloseCurrentPopup();
						break;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
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
					pushUndo();
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
							pushUndo();
							ImGui::PopID();
							continue;
						}
						ImGui::SameLine();
						ImGui::SetNextItemWidth(-1);
						saveUndo(inputSlot("##slot", &entry.slot));
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
						pushUndo();
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
				pushUndo();
			}
			ImGui::SameLine();
			ImGui::Bullet();
			ImGui::TextUnformatted("New set");
		}

		void inputsGUI() {
			InputDecl& inputs = m_controller.m_inputs;

			ImGui::Columns(2);
			ImGui::TextUnformatted("Name"); ImGui::NextColumn();
			ImGui::TextUnformatted("Type"); ImGui::NextColumn();
			for (InputDecl::Input& input : inputs.inputs) {
				if (input.type == InputDecl::Type::EMPTY) continue;
				ImGui::PushID(&input);
				if(ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Remove")) {
					inputs.removeInput(int(&input - inputs.inputs));
					// TODO update input references in nodes
					pushUndo();
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				saveUndo(ImGui::InputText("##name", input.name.data, sizeof(input.name.data)));
				ImGui::NextColumn();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::Combo("##type", (int*)&input.type, "float\0u32\0bool")) {
					inputs.recalculateOffsets();
					saveUndo(true);
				}
				ImGui::NextColumn();
				ImGui::PopID();
			}
			ImGui::Columns();
			if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add input")) {
				inputs.addInput();
				pushUndo();
			}
		}

		void windowGUI() override {
			const CommonActions& actions = m_app.getCommonActions();

			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) saveAs(m_path);
				if (ImGuiEx::IconButton(ICON_FA_UNDO, "Undo")) undo();
				if (ImGuiEx::IconButton(ICON_FA_REDO, "Redo")) redo();
				ImGui::EndMenuBar();
			}

			if (!m_controller.m_root) {
				ImGui::TextUnformatted("Create root:");
				ImGui::Indent();
				if (ImGui::Selectable("Animation")) createRoot(Node::ANIMATION, m_controller.m_allocator);
				if (ImGui::Selectable("Blend1D")) createRoot(Node::BLEND1D, m_controller.m_allocator);
				if (ImGui::Selectable("Condition")) createRoot(Node::CONDITION, m_controller.m_allocator);
				if (ImGui::Selectable("Group")) createRoot(Node::GROUP, m_controller.m_allocator);
				if (ImGui::Selectable("Layers")) createRoot(Node::LAYERS, m_controller.m_allocator);
				if (ImGui::Selectable("Select")) createRoot(Node::SELECT, m_controller.m_allocator);
				ImGui::Unindent();
			}

			bool begin_table = false;
			if (m_preview_on_right) {
				begin_table = ImGui::BeginTable("tbl", 2, ImGuiTableFlags_Resizable);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
			}

			if (m_controller.m_root) hierarchy_ui(*m_controller.m_root);

			if (m_current_node && ImGui::CollapsingHeader("Selected node")) {
				ui_dispatch(*m_current_node);
			}

			if (ImGui::CollapsingHeader("Controller")) {
				ImGuiEx::Label("Root motion bone");
				saveUndo(ImGui::InputText("##rmb", m_controller.m_root_motion_bone.data, sizeof(m_controller.m_root_motion_bone.data)));
				bool xz_root_motion = m_controller.m_flags.isSet(Controller::Flags::XZ_ROOT_MOTION);
				ImGuiEx::Label("XZ root motion");
				if (ImGui::Checkbox("##xzrm", &xz_root_motion)) {
					m_controller.m_flags.set(Controller::Flags::XZ_ROOT_MOTION, xz_root_motion);
					saveUndo(true);
				}

				if (ImGui::BeginTabBar("ctb")) {
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

					ImGui::EndTabBar();
				}
			}

			if (ImGui::CollapsingHeader("Animations") && ImGui::BeginTabBar("tb")) {
				if (ImGui::BeginTabItem("Slots")) {
					slotsGUI();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Sets")) {
					setsGUI();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

			debuggerUI();
			if (m_preview_on_right) ImGui::TableNextColumn();
			previewUI();
			if (begin_table) ImGui::EndTable();
		}

		const Path& getPath() override { return m_path; }
		const char* getName() const override { return "Animation Editor"; }

		struct UndoRecord {
			UndoRecord(IAllocator& allocator) : data(allocator) {}
			OutputMemoryStream data;
			u64 tag;
		};

		struct CopyBuffer {
			CopyBuffer(IAllocator& allocator) : data(allocator) {}
			OutputMemoryStream data;
			Node::Type node_type;
		} m_copy_buffer;

		struct ControllerDebugMapping {
			i32 axis_x = -1;
			i32 axis_y = -1;
		};

		WorldViewer m_viewer;
		IAllocator& m_allocator;
		StudioApp& m_app;
		ControllerEditorImpl& m_plugin;
		Array<UndoRecord> m_undo_stack;
		i32 m_undo_idx;
		Controller m_controller;
		Node* m_current_node = nullptr;
		Model* m_model = nullptr;
		Path m_path;
		bool m_was_preview_ready = false;
		bool m_preview_on_right = false;
		float m_playback_speed = 1.f;
		bool m_show_skeleton = true;
		ControllerDebugMapping m_controller_debug_mapping;
	};

	ControllerEditorImpl(StudioApp& app)
		: m_allocator(app.getAllocator(), "anim controller editor")
		, m_app(app)
		, m_event_types(m_allocator)
	{
		m_event_types.push(UniquePtr<SetInputEventType>::create(m_allocator));
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

	const EventType& getEventType(RuntimeHash type) const {
		for (const UniquePtr<EventType>& t : m_event_types) {
			if (t->type == type) return *t.get();
		}
		ASSERT(false);
		return *m_event_types[0].get();
	}

	void registerEventType(UniquePtr<EventType>&& type) override {
		m_event_types.push(type.move());
	}

	TagAllocator m_allocator;
	StudioApp& m_app;
	Array<UniquePtr<EventType>> m_event_types;
};

UniquePtr<ControllerEditor> ControllerEditor::create(StudioApp& app) {
	return UniquePtr<ControllerEditorImpl>::create(app.getAllocator(), app);
}

} // namespace Lumix::Anim
