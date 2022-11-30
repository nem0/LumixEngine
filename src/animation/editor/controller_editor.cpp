#include <imgui/imgui.h>

#include "animation/animation_scene.h"
#include "controller_editor.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "renderer/model.h"
#include "../animation.h"
#include "../controller.h"
#include "../nodes.h"


namespace Lumix::anim {

struct ControllerEditorImpl : ControllerEditor {
	struct SetInputEventType : EventType {
		SetInputEventType() {
			type = RuntimeHash("set_input");
			label = "Set input";
			size = sizeof(u32) + sizeof(float);
		}

		bool onGUI(u8* data, const ControllerEditor& editor) const override {
			ControllerEditorImpl& ed = (ControllerEditorImpl&)editor;
			bool changed = ed.inputInput("Input", (u32*)data);
			if (changed) {
				memset(data + sizeof(u32), 0, sizeof(float));
			}
			const u32 input_index = *(u32*)data;
			auto& inputs = ed.m_controller->m_inputs;
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

	ControllerEditorImpl(StudioApp& app)
		: m_app(app)
		, m_undo_stack(app.getAllocator())
		, m_event_types(app.getAllocator())
		, m_copy_buffer(app.getAllocator())
	{
		IAllocator& allocator = app.getAllocator();
		ResourceManager* res_manager = app.getEngine().getResourceManager().get(Controller::TYPE);
		ASSERT(res_manager);

		m_toggle_ui.init("Animation editor", "Toggle animation editor", "animation_editor", "", true);
		m_toggle_ui.func.bind<&ControllerEditorImpl::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ControllerEditorImpl::isOpen>(this);

		m_undo_action.init(ICON_FA_UNDO "Undo", "Animation editor undo", "animation_editor_undo", ICON_FA_UNDO, true);
		m_undo_action.func.bind<&ControllerEditorImpl::undo>(this);

		m_redo_action.init(ICON_FA_REDO "Redo", "Animation editor redo", "animation_editor_redo", ICON_FA_REDO, true);
		m_redo_action.func.bind<&ControllerEditorImpl::redo>(this);

		app.addWindowAction(&m_toggle_ui);
		app.addAction(&m_undo_action);
		app.addAction(&m_redo_action);

		newGraph();

		m_event_types.push(UniquePtr<SetInputEventType>::create(m_app.getAllocator()));
	}

	~ControllerEditorImpl() {
		m_app.removeAction(&m_toggle_ui);
		m_app.removeAction(&m_undo_action);
		m_app.removeAction(&m_redo_action);
	}

	bool inputInput(const char* label, u32* input_index) const {
		ASSERT(input_index);
		bool changed = false;
		ImGuiEx::Label(label);
		if (m_controller->m_inputs.inputs_count == 0) {
			ImGui::Text("No inputs");
			return false;
		}
		const InputDecl::Input& current_input = m_controller->m_inputs.inputs[*input_index];
		if (ImGui::BeginCombo("##input", current_input.name)) {
			for (const InputDecl::Input& input : m_controller->m_inputs.inputs) {
				if (input.type == InputDecl::EMPTY) continue;
				if (ImGui::Selectable(input.name)) {
					changed = true;
					*input_index = u32(&input - m_controller->m_inputs.inputs);
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	template <typename F> void forEachNode(F f, Node* node = nullptr) {
		if (!node) node = m_controller->m_root;
		f(*node);

		switch (node->type()) {
			case Node::ANIMATION: break;
			case Node::BLEND1D: break;
			case Node::LAYERS: {
				LayersNode* n = (LayersNode*)node;
				for (LayersNode::Layer& layer : n->m_layers) {
					forEachNode(f, &layer.node);
				}
				break;
			}
			case Node::GROUP: {
				GroupNode* g = (GroupNode*)node;
				for (GroupNode::Child& ch : g->m_children) {
					forEachNode(f, ch.node);
				}
				break;
			}
		}
	}

	void onSettingsLoaded() override {
		m_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_anim_editor_open", false);
	}

	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue(Settings::GLOBAL, "is_anim_editor_open", m_open);
	}

	bool isOpen() const { return m_open; }
	void toggleOpen() { m_open = !m_open; }

	Node* createChild(GroupNode& parent, Node::Type type, IAllocator& allocator) {
		Node* node = nullptr;
		switch(type) {
			case Node::ANIMATION: node = LUMIX_NEW(allocator, AnimationNode)(&parent, allocator); break;
			case Node::GROUP: node = LUMIX_NEW(allocator, GroupNode)(&parent, allocator); break;
			case Node::BLEND1D: node = LUMIX_NEW(allocator, Blend1DNode)(&parent, allocator); break;
			default: ASSERT(false); return nullptr;
		}

		node->m_name = "new";
		parent.m_children.emplace(allocator);
		parent.m_children.back().node = node;
		pushUndo();
		return node;
	}

	bool properties_ui(AnimationNode& node) {
		const Array<String>& names = m_controller->m_animation_slots;
	
		const char* preview = node.m_slot < (u32)names.size() ? names[node.m_slot].c_str() : "";
		bool changed = false;
		ImGuiEx::Label("Animation");
		if (ImGui::BeginCombo("##anim", preview)) {
			for (u32 i = 0; i < (u32)names.size(); ++i) {
				if (ImGui::Selectable(names[i].c_str())) {
					node.m_slot = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		bool looped = node.m_flags & AnimationNode::LOOPED;
		ImGuiEx::Label("Looped");
		if (ImGui::Checkbox("##looped", &looped)) {
			changed = true;
			if (looped) {
				node.m_flags = node.m_flags | AnimationNode::LOOPED;
			}
			else {
				node.m_flags = node.m_flags & ~AnimationNode::LOOPED;
			}
		}
		return changed;
	}

	bool properties_ui(GroupNode& node) {
		float node_blend_length = node.m_blend_length.seconds();
		ImGuiEx::Label("Blend length");
		bool changed = false;
		if (ImGui::DragFloat("##bl", &node_blend_length)) {
			node.m_blend_length = Time::fromSeconds(node_blend_length);
			changed = true;
		}

		if (ImGui::TreeNode("Transitions")) {
			for (GroupNode::Transition& tr : node.m_transitions) {
				const char* name_from = tr.from == 0xFFffFFff ? "*" : node.m_children[tr.from].node->m_name.c_str();
				const char* name_to = tr.to == 0xFFffFFff ? "*" : node.m_children[tr.to].node->m_name.c_str();
				const bool open = ImGui::TreeNodeEx(&tr, 0, "%s -> %s", name_from, name_to);
				if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) ImGui::OpenPopup("context_menu");
				if (ImGui::BeginPopup("context_menu")) {
					if (ImGui::Selectable("Remove")) {
						changed = true;
						node.m_transitions.erase(u32(&tr - node.m_transitions.begin()));
						ImGui::EndPopup();
						if (open) ImGui::TreePop();
						break;
					}
					ImGui::EndPopup();
				}
				if (open) {
					changed = nodeInput("From", tr.from, node.m_children, true) || changed;
					changed = nodeInput("To", tr.to, node.m_children, true) || changed;
					bool has_exit_time = tr.exit_time >= 0;
					ImGuiEx::Label("Blend length");
					float l = tr.blend_length.seconds();
					if (ImGui::DragFloat("##bl", &l, 0.1f, 0.0, FLT_MAX, "%.2f")) {
						tr.blend_length = Time::fromSeconds(l);
						changed = true;
					}
					ImGuiEx::Label("Has exit time");
					if (ImGui::Checkbox("##het", &has_exit_time)) {
						if (has_exit_time) tr.exit_time = 0;
						else tr.exit_time = -1;
						changed = true;
					}
					if (has_exit_time) {
						ImGuiEx::Label("Exit time");
						changed = ImGui::DragFloat("##et", &tr.exit_time, 0.01f, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp) || changed;
					}

					ImGui::TreePop();
				}
			}

			if (!node.m_children.empty() && ImGui::Button(ICON_FA_PLUS_CIRCLE "Add")) {
				node.m_transitions.emplace();
				changed = true;
			}

			ImGui::TreePop();
		}

		return changed;
	}

	bool properties_ui(Blend1DNode& node) {
		const InputDecl::Input& current_input = m_controller->m_inputs.inputs[node.m_input_index];
		bool changed = false;
		ImGuiEx::Label("Input");
		if (ImGui::BeginCombo("##input", current_input.name)) {
			for (const InputDecl::Input& input : m_controller->m_inputs.inputs) {
				if (ImGui::Selectable(input.name)) {
					changed = true;
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
		
			ImGui::SetNextItemWidth(-1);
			changed = ImGui::InputFloat("##val", &child.value) || changed;
			ImGui::NextColumn();
		
			ImGui::SetNextItemWidth(-1);
			const Array<String>& slots = m_controller->m_animation_slots;
			if (ImGui::BeginCombo("##anim", child.slot < (u32)slots.size() ? slots[child.slot].c_str() : "")) {
				for (u32 i = 0; i < (u32)slots.size(); ++i) {
					if (ImGui::Selectable(slots[i].c_str())) {
						child.slot = i;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
		
			ImGui::NextColumn();
		
		
			ImGui::PopID();
		}
		ImGui::Columns();
		if (ImGui::Button("Add")) {
			changed = true;
			node.m_children.emplace();
			if(node.m_children.size() > 1) {
				node.m_children.back().value = node.m_children[node.m_children.size() - 2].value;
			}
		}
		return changed;
	}

	bool child_properties_ui(Node& node) {
		bool changed = false;
		if (node.m_parent && node.m_parent->type() == Node::Type::GROUP) {
			GroupNode* group = (GroupNode*)node.m_parent;
			for (GroupNode::Child& c : group->m_children) {
				if (c.node != &node) continue;
				
				changed = conditionInput("Condition", m_controller->m_inputs, c.condition_str, c.condition) || changed;
				bool selectable = c.flags & GroupNode::Child::SELECTABLE;
				ImGuiEx::Label("Selectable");
				if (ImGui::Checkbox("##sel", &selectable)) {
					if (selectable) c.flags |= GroupNode::Child::SELECTABLE;
					else c.flags &= ~GroupNode::Child::SELECTABLE;
					changed = true;
				}
				break;
			}
		}
		return changed;
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

	const EventType& getEventType(RuntimeHash type) {
		for (const UniquePtr<EventType>& t : m_event_types) {
			if (t->type == type) return *t.get();
		}
		ASSERT(false);
		return *m_event_types[0].get();
	}

	void removeEvent(OutputMemoryStream& events, u32 idx) const {
		OutputMemoryStream tmp(m_app.getAllocator());
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

	bool editEvents(OutputMemoryStream& events) {
		if (!ImGui::TreeNode("Events")) return false;
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_PLUS_CIRCLE).x);
		if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add event")) ImGui::OpenPopup("add_event_popup");

		bool changed = false;
		if (!events.empty()) {
			u32 i = 0;
			InputMemoryStream blob(events);
			while(blob.getPosition() != blob.size()) {
				++i;
				const RuntimeHash type = blob.read<RuntimeHash>();
				const u16 data_size = blob.read<u16>();
				const EventType& type_obj = getEventType(type);
				ASSERT(data_size == type_obj.size);
				u16* rel_time = (u16*)blob.skip(sizeof(u16));
				u8* data = (u8*)blob.skip(type_obj.size);
				
				bool open = ImGui::TreeNodeEx((void*)(uintptr)i, ImGuiTreeNodeFlags_AllowItemOverlap, "%d %s", i, type_obj.label.data);
				ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_FA_MINUS_CIRCLE).x);
				if (ImGuiEx::IconButton(ICON_FA_MINUS_CIRCLE, "Delete")) {
					ImGui::TreePop();
					removeEvent(events, i - 1);
					break;
				}
				
				if (open) {
					ImGuiEx::Label("Time");
					float t = *rel_time / float(0xffff);
					if (ImGui::DragFloat("##t", &t, 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
						*rel_time = u16(t * 0xffFF);
						changed = true;
					}
					changed = type_obj.onGUI(data, *this) || changed;
					ImGui::TreePop();
				}
			}
		}

		if (ImGui::BeginPopup("add_event_popup")) {
			for (const UniquePtr<EventType>& type : m_event_types) {
				if (ImGui::Selectable(type->label)) {
					addEvent(events, *type.get());
					changed = true;
				}
			}
			ImGui::EndPopup();
		}
		ImGui::TreePop();
		return changed;
	}

	bool ui_dispatch(Node& node) {
		char tmp[64];
		copyString(tmp, node.m_name.c_str());
		bool changed = false;

		ImGuiEx::Label("Name");		
		if (ImGui::InputText("##name", tmp, sizeof(tmp))) {
			node.m_name = tmp;
			changed = true;
		}

		changed = child_properties_ui(node) || changed;

		switch(node.type()) {
			case Node::ANIMATION: changed = properties_ui((AnimationNode&)node) || changed; break;
			case Node::GROUP: changed = properties_ui((GroupNode&)node) || changed; break;
			case Node::BLEND1D: changed = properties_ui((Blend1DNode&)node) || changed; break;
			default: ASSERT(false); break;
		}

		changed = editEvents(node.m_events) || changed;
		return changed;
	}

	bool canLoadFromEntity() const {
		const Array<EntityRef>& selected = m_app.getWorldEditor().getSelectedEntities();
		if (selected.size() != 1) return false;
		Universe* universe = m_app.getWorldEditor().getUniverse();
		return universe->hasComponent(selected[0], reflection::getComponentType("animator"));
	}

	void updateSelectedEntity() {
		const Array<EntityRef>& selected = m_app.getWorldEditor().getSelectedEntities();
		if (selected.size() != 1) return;
		Universe* universe = m_app.getWorldEditor().getUniverse();
		ComponentType animator_type = reflection::getComponentType("animator");
		if (!universe->hasComponent(selected[0], animator_type)) return;

		AnimationScene* scene = (AnimationScene*)universe->getScene(animator_type);
		scene->updateAnimator(selected[0], m_app.getEngine().getLastTimeDelta());
	}

	Path getPathFromEntity() const {
		const Array<EntityRef>& selected = m_app.getWorldEditor().getSelectedEntities();
		if (selected.size() != 1) return Path();
		Universe* universe = m_app.getWorldEditor().getUniverse();
		const ComponentType cmp_type = reflection::getComponentType("animator");
		if (!universe->hasComponent(selected[0], cmp_type)) return Path();
		AnimationScene* scene = (AnimationScene*)universe->getScene(cmp_type);
		return scene->getAnimatorSource(selected[0]);
	}

	void load() {
		if (m_dirty) {
			m_confirm_load = true;
			m_confirm_path = "";
			return;
		}

		m_show_open = true;
	}

	void load(const char* path) {
		if (m_dirty) {
			m_confirm_load = true;
			m_confirm_path = path;
			return;
		}

		if (!path[0]) {
			load();
			return;
		}

		FileSystem& fs = m_app.getEngine().getFileSystem();
		IAllocator& allocator = m_app.getAllocator();
		OutputMemoryStream data(allocator);
		if (fs.getContentSync(Path(path), data)) {
			ResourceManager* res_manager = m_app.getEngine().getResourceManager().get(Controller::TYPE);
			InputMemoryStream str(data);
			UniquePtr<Controller> new_controller = UniquePtr<Controller>::create(allocator, Path("anim_editor"), *res_manager, allocator);
			if (new_controller->deserialize(str)) {
				m_controller = new_controller.move();
				m_current_node = m_controller->m_root;
				m_path = path;
				m_undo_stack.clear();
				m_undo_idx = -1;
			}
			pushUndo();
			m_dirty = false;
		}
		else {
			logError("Failed to read ", path);
		}
	}

	static bool conditionInput(const char* label, InputDecl& input, String& condition_str, Condition& condition) {
		char tmp[1024];
		copyString(tmp, condition_str.c_str());
		if (condition.error != Condition::Error::NONE) {
			ImGui::TextUnformatted(Condition::errorToString(condition.error));
		}
		ImGuiEx::Label(label);		
		if (ImGui::InputText(StaticString<64>("##_", label), tmp, sizeof(tmp), ImGuiInputTextFlags_EnterReturnsTrue)) {
			condition_str = tmp;
			condition.compile(tmp, input);
			return true;
		}

		return false;
	}

	static bool nodeInput(const char* label, u32& value, const Array<GroupNode::Child>& children, bool has_any) {
		ImGuiEx::Label(label);		
		bool changed = false;
		if (!ImGui::BeginCombo(StaticString<64>("##_", label), value == 0xffFFffFF ? "*" : children[value].node->m_name.c_str())) return false;

		if (has_any) {
			if (ImGui::Selectable("*")) {
				value = 0xffFFffFF;
				changed = true;
			}
		}

		for (GroupNode::Child& child : children) {
			if (ImGui::Selectable(child.node->m_name.c_str())) {
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
		}
		ASSERT(false);
		return "N/A";
	}

	static bool isContainer(const Node& node) {
		switch (node.type()) {
			case Node::Type::ANIMATION: 
			case Node::Type::BLEND1D: return false;
			case Node::Type::GROUP:
			case Node::Type::LAYERS: return true;
		}
		ASSERT(false);
		return false;
	}

	bool isSelectable(Node& node) {
		if (!node.m_parent) return false;
		for (auto& c : node.m_parent->m_children) {
			if (c.node == &node && (c.flags & GroupNode::Child::SELECTABLE)) return true;
		}
		return false;
	}

	bool hierarchy_ui(Node& node) {
		bool changed = false;
		const bool is_container = isContainer(node);
		const bool is_parent_group = node.m_parent && node.m_parent->type() == Node::Type::GROUP;
		const bool is_layer = node.m_parent && node.m_parent->type() == Node::Type::LAYERS;
		const bool is_group = node.type() == Node::Type::GROUP;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | (&node == m_current_node ? ImGuiTreeNodeFlags_Selected : 0);
		if (!is_container) flags |= ImGuiTreeNodeFlags_Leaf; 

		const char* type_str = toString(node.type());
		const bool is_selectable = isSelectable(node);
		if (!is_selectable) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		const bool open = ImGui::TreeNodeEx(&node, flags, "%s (%s)", node.m_name.c_str(), type_str);
		if (is_parent_group) {
			if (ImGui::BeginDragDropSource()) {
				ImGui::Text("%s", node.m_name.c_str());
				void* ptr = &node;
				ImGui::SetDragDropPayload("anim_node", &ptr, sizeof(ptr));
				ImGui::EndDragDropSource();
			}
		}

		if (is_group) {
			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload("anim_node")) {
					Node* dropped_node = *(Node**)payload->Data;
					OutputMemoryStream blob(m_app.getAllocator());
					dropped_node->serialize(blob);
					Node* new_node = createChild((GroupNode&)node, dropped_node->type(), m_controller->m_allocator);
					InputMemoryStream iblob(blob);
					new_node->deserialize(iblob, *m_controller.get(), (u32)ControllerVersion::LATEST);
					ASSERT(dropped_node->m_parent);
					for (GroupNode::Child& src : dropped_node->m_parent->m_children) {
						if (src.node == dropped_node) {
							for (GroupNode::Child& c : new_node->m_parent->m_children) {
								if (c.node == new_node) {
									c.flags = src.flags;
									c.condition_str = src.condition_str;
									c.condition.compile(src.condition_str.c_str(), m_controller->m_inputs);
									break;
								}
							}
							break;
						}
					}
					if (m_current_node == dropped_node) m_current_node = nullptr;
					dropped_node->m_parent->m_children.eraseItems([dropped_node](GroupNode::Child& c){ return c.node == dropped_node; });
					LUMIX_DELETE(m_controller->m_allocator, dropped_node);
					ImGui::TreePop();
					return true;
				}
			}
		}

		if (open) {
			if (!is_selectable) ImGui::PopStyleColor();
			if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) {
				m_current_node = &node;
			}

			if (is_parent_group && ImGui::IsMouseClicked(1) && ImGui::IsItemHovered()) {
				ImGui::OpenPopup("group_popup");
			}
			else if (is_layer && ImGui::IsMouseClicked(1) && ImGui::IsItemHovered()) {
				ImGui::OpenPopup("layer_popup");
			}
			else if (is_group && ImGui::IsMouseClicked(1) && ImGui::IsItemHovered()) {
				ImGui::OpenPopup("popup");
			}

			auto paste_ui = [&](GroupNode& group){
				if (ImGui::Selectable("Paste", false, m_copy_buffer.data.empty() ? ImGuiSelectableFlags_Disabled : 0)) {
					Node* pasted = createChild(group, m_copy_buffer.node_type, m_controller->m_allocator);
					if (pasted) {
						InputMemoryStream blob(m_copy_buffer.data);
						pasted->deserialize(blob, *m_controller.get(), (u32)ControllerVersion::LATEST);
					}
				}
			};

			if (ImGui::BeginPopup("popup")) {
				paste_ui((GroupNode&)node);
				ImGui::EndPopup();
			}

			if (ImGui::BeginPopup("group_popup")) {
				ImGui::TextUnformatted(node.m_name.c_str());
				ImGui::Separator();
				if (ImGui::Selectable("Copy")) {
					m_copy_buffer.node_type = node.type();
					m_copy_buffer.data.clear();
					node.serialize(m_copy_buffer.data);
				}
				if (node.type() == Node::GROUP) paste_ui((GroupNode&)node);
				if (ImGui::Selectable("Remove")) {
					if (m_current_node == &node) m_current_node = nullptr;
					((GroupNode*)node.m_parent)->m_children.eraseItems([&node](GroupNode::Child& c){ return c.node == &node; });
					LUMIX_DELETE(m_controller->m_allocator, &node);
					ImGui::EndPopup();
					ImGui::TreePop();
					return true;
				}
				ImGui::EndPopup();
			}

			switch (node.type()) {
				case Node::Type::GROUP: {
					GroupNode& group = (GroupNode&)node;
					for (GroupNode::Child& c : group.m_children) {
						changed = hierarchy_ui(*c.node) || changed;
					}
					break;
				}
				case Node::Type::LAYERS: {
					LayersNode& layers = (LayersNode&)node;
					for (LayersNode::Layer& l : layers.m_layers) {
						changed = hierarchy_ui(l.node) || changed;
					}
					break;
				}
				default: break;
			}
			ImGui::TreePop();
		}
		else {
			if (!is_selectable) ImGui::PopStyleColor();
		}
		return changed;
	}

	void debuggerUI() {
		if (ImGui::Begin("Animation debugger", &m_open)) {
			const Array<EntityRef>& selected = m_app.getWorldEditor().getSelectedEntities();
			if (selected.size() != 1) {
				ImGui::TextUnformatted(selected.empty() ? "No entity selected" : "Too many entities selected");
				ImGui::End();
				return;
			}

			Universe* universe = m_app.getWorldEditor().getUniverse();
			const ComponentType cmp_type = reflection::getComponentType("animator");
			if (!universe->hasComponent(selected[0], cmp_type)) {
				ImGui::TextUnformatted("Selected entity does not have animator component");
				ImGui::End();
				return;
			}

			AnimationScene* scene = (AnimationScene*)universe->getScene(cmp_type);
			Controller* ctrl = scene->getAnimatorController(selected[0]);
			if (!ctrl) {
				ImGui::TextUnformatted("Selected entity does not have resource assigned in animator component");
				ImGui::End();
				return;
			}
			
			for (const InputDecl::Input& input : ctrl->m_inputs.inputs) {
				const u32 idx = u32(&input - ctrl->m_inputs.inputs);
				switch (input.type) {
					case InputDecl::Type::EMPTY: break;
					case InputDecl::Type::FLOAT: {
						float val = scene->getAnimatorFloatInput(selected[0], idx);
		
						ImGuiEx::Label(input.name);
						if (ImGui::DragFloat(input.name, &val)) {
							scene->setAnimatorInput(selected[0], idx, val);
						}
						break;
					}
					case InputDecl::Type::BOOL: {
						bool val = scene->getAnimatorBoolInput(selected[0], idx);
						ImGuiEx::Label(input.name);
						if (ImGui::Checkbox(input.name, &val)) {
							scene->setAnimatorInput(selected[0], idx, val);
						}
						break;
					}
					case InputDecl::Type::U32: {
						u32 val = scene->getAnimatorU32Input(selected[0], idx);
						ImGuiEx::Label(input.name);
						if (ImGui::DragInt(input.name, (int*)&val, 1, 0, 0x7ffFFff)) {
							scene->setAnimatorInput(selected[0], idx, val);
						}
						break;
					}
				}
			}
		}
		ImGui::End();
	}

	void save(const char* path) {
		OutputMemoryStream blob(m_controller->m_allocator);
		m_controller->serialize(blob);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.saveContentSync(Path(path), blob)) {
			logError("Failed to save ", path);
			return;
		}
		m_path = path;
		m_dirty = false;
	}

	void newGraph() {
		if (m_dirty) {
			m_confirm_new = true;
			return;
		}
		ResourceManager* res_manager = m_app.getEngine().getResourceManager().get(Controller::TYPE);
		IAllocator& allocator = m_app.getAllocator();
		m_path = "";
		m_controller = UniquePtr<Controller>::create(allocator, Path("anim_editor"), *res_manager, allocator);
		m_controller->initEmpty();
		m_current_node = m_controller->m_root;
		m_undo_stack.clear();
		m_undo_idx = -1;
		pushUndo();
		m_dirty = false;
	}

	void save() {
		if (m_path.empty()) m_show_save_as = true;
		else save(m_path);
	}

	void pushUndo(u64 tag = ~u64(0)) {
		while (m_undo_idx >= m_undo_stack.size()) m_undo_stack.pop();

		if (tag == ~u64(0) || m_undo_stack.back().tag != tag) {
			UndoRecord& r = m_undo_stack.emplace(m_app.getAllocator());
			r.tag = tag;
			m_controller->serialize(r.data);
			m_undo_idx = m_undo_stack.size() - 1;
			return;
		}

		m_undo_stack.back().data.clear();
		m_controller->serialize(m_undo_stack.back().data);
		m_dirty = true;
	}

	void undoRedo() {
		IAllocator& allocator = m_app.getAllocator();
		ResourceManager* res_manager = m_app.getEngine().getResourceManager().get(Controller::TYPE);
		UniquePtr<Controller> c = UniquePtr<Controller>::create(allocator, Path("anim_editor"), *res_manager, allocator);
		InputMemoryStream tmp(m_undo_stack[m_undo_idx].data);
		bool success = c->deserialize(tmp);
		ASSERT(success);
		m_controller = c.move();
		m_current_node = m_controller->m_root;
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

	bool hasFocus() override { return m_has_focus; }

	void onWindowGUI() override {
		m_has_focus = false;
		if (!m_open) return;

		debuggerUI();

		if (m_confirm_new) {
			ImGui::OpenPopup("Confirm##cn");
			m_confirm_new = false;
		}
		if (m_confirm_load) {
			ImGui::OpenPopup("Confirm##cl");
			m_confirm_load = false;
		}

		if (ImGui::BeginPopupModal("Confirm##cn")) {
			ImGui::TextUnformatted("All changes will be lost. Continue anyway?");
			if (ImGui::Selectable("Yes")) {
				m_dirty = false;
				newGraph();
			}
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Confirm##cl")) {
			ImGui::TextUnformatted("All changes will be lost. Continue anyway?");
			if (ImGui::Selectable("Yes")) {
				m_dirty = false;
				load(m_confirm_path);
				m_confirm_path = "";
			}
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Confirm##cn")) {
			ImGui::TextUnformatted("All changes will be lost. Continue anyway?");
			if (ImGui::Selectable("Yes")) {
				m_dirty = false;
				newGraph();
			}
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}

		if (ImGui::Begin("Animation editor", &m_open, ImGuiWindowFlags_MenuBar)) {
			m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			
			if (ImGui::BeginMenuBar()) {
				if (ImGui::BeginMenu("File")) {
					if (ImGui::MenuItem("New")) newGraph();
					if (ImGui::MenuItem("Save")) save();
					if (ImGui::MenuItem("Save As")) m_show_save_as = true;
					if (ImGui::MenuItem("Load")) load();
				
					if (ImGui::MenuItem("Load from entity", nullptr, false, canLoadFromEntity())) {
						load(getPathFromEntity().c_str());
					}
					ImGui::EndMenu();
				}
				
				if (ImGui::BeginMenu("Edit")) {
					menuItem(m_undo_action, canUndo());
					menuItem(m_redo_action, canRedo());
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

				ImGui::Checkbox("Update", &m_update);

				ImGui::EndMenuBar();
			}

			FileSelector& fs = m_app.getFileSelector();
			if (fs.gui("Open", &m_show_open, "act", false)) load(fs.getPath());
			if (fs.gui("Save As", &m_show_save_as, "act", true)) save(fs.getPath());

			if (m_update) updateSelectedEntity();

			if (hierarchy_ui(*m_controller->m_root)) {
				pushUndo();
			}

			if (m_current_node && ImGui::CollapsingHeader("Node")) {
				if (ui_dispatch(*m_current_node)) {
					pushUndo((uintptr)m_current_node);
				}
			}

			if (ImGui::CollapsingHeader("Controller")) {
				ImGuiEx::Label("Root motion bone");
				ImGui::InputText("##rmb", m_controller->m_root_motion_bone.data, sizeof(m_controller->m_root_motion_bone.data));
				bool xz_root_motion = m_controller->m_flags.isSet(Controller::Flags::XZ_ROOT_MOTION);
				ImGuiEx::Label("XZ root motion");
				if (ImGui::Checkbox("##xzrm", &xz_root_motion)) {
					m_controller->m_flags.set(Controller::Flags::XZ_ROOT_MOTION, xz_root_motion);
					pushUndo();
				}
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
						pushUndo();
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
						pushUndo();
					}
				}
				if (ImGui::Button(ICON_FA_PLUS_CIRCLE "##create_slot")) {
					m_controller->m_animation_slots.emplace("", m_controller->m_allocator);
					pushUndo();
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
				for (u32 entry_idx = 0; entry_idx < (u32)m_controller->m_animation_entries.size(); ++entry_idx) {
					ImGui::PushID(entry_idx);
					Controller::AnimationEntry& entry = m_controller->m_animation_entries[entry_idx];
					ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##set", (int*)&entry.set);
					ImGui::NextColumn();
					const char* preview = entry.slot < (u32)m_controller->m_animation_slots.size() ? m_controller->m_animation_slots[entry.slot].c_str() : "N/A";
					ImGui::SetNextItemWidth(-1);
					if (ImGui::BeginCombo("##slot", preview, 0)) {
						for (u32 i = 0, c = m_controller->m_animation_slots.size(); i < c; ++i) {
							if (ImGui::Selectable(m_controller->m_animation_slots[i].c_str())) {
								entry.slot = i;
								pushUndo();
							}
						}
						ImGui::EndCombo();
					}
					ImGui::NextColumn();
					ImGui::PushItemWidth(-1);
					char path[LUMIX_MAX_PATH];
					copyString(path, entry.animation ? entry.animation->getPath().c_str() : "");
					if (m_app.getAssetBrowser().resourceInput("anim", Span(path), Animation::TYPE)) {
						if (entry.animation) entry.animation->decRefCount();
						ResourceManagerHub& res_manager = m_app.getEngine().getResourceManager();
						entry.animation = res_manager.load<Animation>(Path(path));
						pushUndo();
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
					pushUndo();
				}
			}

			if (ImGui::CollapsingHeader("Bone masks")) {
				char model_path[LUMIX_MAX_PATH];
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
							const BoneNameHash bone_name_hash(bone_name);
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
				char model_path[LUMIX_MAX_PATH];
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
										ik.bones[0] = BoneNameHash(bone_name);
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
											ik.bones[0] = BoneNameHash(bone_name);
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
						m_controller->m_ik[m_controller->m_ik_count].bones[0] = BoneNameHash();
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
		load(path);
	}

	void registerEventType(UniquePtr<EventType>&& type) override {
		m_event_types.push(type.move());
	}

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

	StudioApp& m_app;
	Array<UndoRecord> m_undo_stack;
	i32 m_undo_idx;
	UniquePtr<Controller> m_controller;
	Node* m_current_node = nullptr;
	Model* m_model = nullptr;
	bool m_open = false;
	bool m_has_focus = false;
	bool m_dirty = false;
	bool m_confirm_new = false;
	bool m_confirm_load = false;
	StaticString<LUMIX_MAX_PATH> m_confirm_path;
	Action m_toggle_ui;
	Action m_undo_action;
	Action m_redo_action;
	bool m_update = false;
	StaticString<LUMIX_MAX_PATH> m_path;
	bool m_show_save_as = false;
	bool m_show_open = false;
	Array<UniquePtr<EventType>> m_event_types;
}; // ControllerEditorImpl

UniquePtr<ControllerEditor> ControllerEditor::create(StudioApp& app) {
	return UniquePtr<ControllerEditorImpl>::create(app.getAllocator(), app);
}

} // namespace Lumix::Anim
