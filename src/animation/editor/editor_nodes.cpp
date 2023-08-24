#include "animation/animation.h"
#include "animation/controller.h"
#include "engine/log.h"
#include "editor_nodes.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "engine/crt.h"
#include "engine/stack_array.h"

namespace Lumix::anim_editor {

static constexpr u32 OUTPUT_FLAG = 1 << 31;

bool editInput(const char* label, u32* input_index, const Controller& controller) {
	ASSERT(input_index);
	bool changed = false;
	ImGuiEx::Label(label);
	if (controller.m_inputs.empty()) {
		ImGui::Text("No inputs");
		return false;
	}
	const Controller::Input& current_input = controller.m_inputs[*input_index];
	if (ImGui::BeginCombo(StaticString<64>("##input", label), current_input.name)) {
		for (const Controller::Input& input : controller.m_inputs) {
			if (ImGui::Selectable(input.name)) {
				changed = true;
				*input_index = u32(&input - controller.m_inputs.begin());
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

bool editSlot(const Controller& controller, const char* str_id, u32* slot) {
	bool changed = false;
	const char* preview = *slot < (u32)controller.m_animation_slots.size() ? controller.m_animation_slots[*slot].c_str() : "N/A";
	if (ImGui::BeginCombo(str_id, preview, 0)) {
		static char filter[64] = "";
		ImGuiEx::filter("Filter", filter, sizeof(filter), -1, ImGui::IsWindowAppearing());
		bool selected = false;
		for (u32 i = 0, c = controller.m_animation_slots.size(); i < c; ++i) {
			const char* name = controller.m_animation_slots[i].c_str();
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

static PoseNode* castToPoseNode(Node* n) {
	if (!n->isPoseNode()) return nullptr;
	return (PoseNode*)n;
}

static ValueNode* castToValueNode(Node* n) {
	if (!n) return nullptr;
	if (!n->isValueNode()) return nullptr;
	return (ValueNode*)n;
}

Blend2DNode::Blend2DNode(Node* parent, Controller& controller, IAllocator& allocator)
	: PoseNode(parent, controller, allocator) 
	, m_children(allocator)
	, m_triangles(allocator)
	, m_name("blend2d", allocator)
{}

bool Blend2DNode::onGUI() {
	ImGuiEx::NodeTitle(m_name.c_str());
	inputSlot();
	outputSlot();
	ImGui::TextUnformatted("X input");
	inputSlot();
	ImGui::TextUnformatted("Y input");
	return false;
}

anim::Node* Blend2DNode::compile(anim::Controller& controller) {
	if (m_triangles.empty()) return nullptr;

	UniquePtr<anim::Blend2DNode> node = UniquePtr<anim::Blend2DNode>::create(controller.m_allocator, controller.m_allocator);
	m_children.copyTo(node->m_children);
	m_triangles.copyTo(node->m_triangles);
	ValueNode* x = castToValueNode(getInput(0));
	ValueNode* y = castToValueNode(getInput(1));
	if (!x || !y) return nullptr;
	node->m_x_value = (anim::ValueNode*)x->compile(controller);
	node->m_y_value = (anim::ValueNode*)y->compile(controller);
	if (!node->m_x_value) return nullptr;
	if (!node->m_y_value) return nullptr;

	return node.detach();
}

bool Blend2DNode::propertiesGUI() {
	ImGuiEx::Label("Name");
	bool res = inputString("##name", &m_name);
	
	if (ImGui::BeginTable("b2dt", 3, ImGuiTableFlags_Resizable)) {
		for (Blend2DNode::Child& child : m_children) {
			ImGui::PushID(&child);
			ImGui::TableNextRow(ImGuiTableFlags_RowBg);

			if (m_hovered_blend2d_child == i32(&child - m_children.begin())) {
				ImU32 row_bg_color = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_TabHovered]);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_bg_color);
			}
			else {
				ImU32 row_bg_color = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_TableRowBg]);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_bg_color);
			}

			ImGui::TableNextColumn();
			if (ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Remove")) {
				m_children.erase(u32(&child - m_children.begin()));
				ImGui::TableNextColumn();
				ImGui::TableNextColumn();
				ImGui::PopID();
				res = true;
				continue;
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			res = ImGui::DragFloat("##xval", &child.value.x) || res;
		
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			res = ImGui::DragFloat("##yval", &child.value.y) || res;

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			res = editSlot(m_controller, "##anim", &child.slot) || res;

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add")) {
		m_children.emplace();
		if(m_children.size() > 1) {
			m_children.back().value = m_children[m_children.size() - 2].value;
		}
		res = true;
	}

	
	if (!m_triangles.empty()) {
		float w = maximum(ImGui::GetContentRegionAvail().x, 100.f);
		ImGui::InvisibleButton("tmp", ImVec2(w, w));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p = ImGui::GetItemRectMin() + ImVec2(4, 4);
		ImVec2 s = ImGui::GetItemRectSize() - ImVec2(8, 8);
		Vec2 min(FLT_MAX), max(-FLT_MAX);
		for (const Blend2DNode::Child& c : m_children) {
			min = minimum(min, c.value);
			max = maximum(max, c.value);
		}
		swap(min.y, max.y);
		Vec2 inv_range = Vec2(s) / (max - min);

		const ImGuiStyle& style = ImGui::GetStyle();
		ImU32 lines_color = ImGui::GetColorU32(style.Colors[ImGuiCol_PlotLines]);
		ImU32 hovered_color = ImGui::GetColorU32(style.Colors[ImGuiCol_PlotLinesHovered]);
		ImU32 fill_color = ImGui::GetColorU32(style.Colors[ImGuiCol_FrameBgActive]);
		ImU32 bg_color = ImGui::GetColorU32(style.Colors[ImGuiCol_FrameBg]);

		dl->AddRectFilled(p, p + s, bg_color);

		for (const Blend2DNode::Triangle& t : m_triangles) {
			dl->AddTriangleFilled(p + (m_children[t.a].value - min) * inv_range
				, p + (m_children[t.c].value - min) * inv_range
				, p + (m_children[t.b].value - min) * inv_range
				, fill_color);
		}

		auto old_flags = dl->Flags;
		dl->Flags = dl->Flags & ~ImDrawListFlags_AntiAliasedLines;
		for (const Blend2DNode::Triangle& t : m_triangles) {
			dl->AddTriangle(p + (m_children[t.a].value - min) * inv_range
				, p + (m_children[t.c].value - min) * inv_range
				, p + (m_children[t.b].value - min) * inv_range
				, lines_color);
		}
		i32 hovered = -1;
		for (const Blend2DNode::Child& ch : m_children) {
			ImVec2 p0 = p + (ch.value - min) * inv_range - ImVec2(4, 4);
			ImVec2 p1 = p0 + ImVec2(8, 8);
			if (ImGui::IsMouseHoveringRect(p0, p1)) {
				if (ImGui::BeginTooltip()) {
					ImGui::TextUnformatted(m_controller.m_animation_slots[ch.slot].c_str());
					ImGui::Text("X = %f", ch.value.x);
					ImGui::Text("Y = %f", ch.value.y);
					ImGui::EndTooltip();
					hovered = i32(&ch - m_children.begin());
				}
				dl->AddRect(p0, p1, hovered_color);
			}
			else {
				dl->AddRect(p0, p1, lines_color);
			}
		}
		m_hovered_blend2d_child = hovered;
		dl->Flags = old_flags;
	}

	if (res) dataChanged();
	return res;
}

static Vec2 computeCircumcircleCenter(Vec2 a, Vec2 b, Vec2 c) {
	Vec2 dab = b - a;
	Vec2 dac = c - a;
	Vec2 o = (dac * squaredLength(dab) - dab * squaredLength(dac)).ortho() / ((dab.x * dac.y - dab.y * dac.x) * 2.f);
	return o + a;
}

// delaunay triangulation
void Blend2DNode::dataChanged() {
	m_triangles.clear();
	if (m_children.size() < 3) return;

	struct Edge {
		u32 a, b;
		bool valid = true;
		bool operator ==(const Edge& rhs) {
			return a == rhs.a && b == rhs.b || a == rhs.b && b == rhs.a;
		}
	};

	StackArray<Edge, 8> edges(m_allocator);

	auto pushTriangle = [&](u32 a, u32 b, u32 c){
		Triangle& t = m_triangles.emplace();
		t.a = a;
		t.b = b;
		t.c = c;
		t.circumcircle_center = computeCircumcircleCenter(m_children[a].value, m_children[b].value, m_children[c].value);
	};

	Vec2 min = Vec2(FLT_MAX);
	Vec2 max = Vec2(-FLT_MAX);
	for (const Child& i : m_children) {
		min = minimum(min, i.value);
		max = maximum(max, i.value);
	}
	
	{
		// bounding triangle
		Vec2 d = max - min;
		float dmax = maximum(d.x, d.y);
		Vec2 mid = (max + min) * 0.5f;
		m_children.emplace().value = Vec2(mid.x - 20 * dmax, mid.y - dmax);
		m_children.emplace().value = Vec2(mid.x, mid.y + 20 * dmax);
		m_children.emplace().value = Vec2(mid.x + 20 * dmax, mid.y - dmax);
		pushTriangle(m_children.size() - 1, m_children.size() - 2, 0);
		pushTriangle(m_children.size() - 2, m_children.size() - 3, 0);
		pushTriangle(m_children.size() - 3, m_children.size() - 1, 0);
	}

	for (u32 ch = 1, c = m_children.size() - 3; ch < c; ++ch) {
		Vec2 p = m_children[ch].value;
		edges.clear();

		for (i32 ti = m_triangles.size() - 1; ti >= 0; --ti) {
			const Triangle& t = m_triangles[ti];
			Vec2 center = t.circumcircle_center;
			if (squaredLength(p - center) > squaredLength(m_children[t.a].value - center)) continue;

			edges.push({t.a, t.b});
			edges.push({t.b, t.c});
			edges.push({t.c, t.a});

			m_triangles.swapAndPop(ti);
		}

		for (i32 i = edges.size() - 1; i > 0; --i) {
			for (i32 j = i - 1; j >= 0; --j) {
				if (edges[i] == edges[j]) {
					edges[i].valid = false;
					edges[j].valid = false;
				}
			}
		}

		edges.eraseItems([](const Edge& e){ return !e.valid; });

		for (Edge& e : edges) {
			pushTriangle(e.a, e.b, ch);
		}
	}

	// pop bounding triangle's vertices and remove related triangles
	m_children.pop();
	m_children.pop();
	m_children.pop();

	m_triangles.eraseItems([&](const Triangle& t){
		const u32 s = (u32)m_children.size();
		return t.a >= s || t.b >= s || t.c >= s;
	});
}

void Blend2DNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_name);
	stream.writeArray(m_children);
}

void Blend2DNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_name);
	stream.readArray(&m_children);
	dataChanged();
}

Blend1DNode::Blend1DNode(Node* parent, Controller& controller, IAllocator& allocator)
	: PoseNode(parent, controller, allocator) 
	, m_children(allocator)
	, m_name("blend1d", allocator)
{}

bool Blend1DNode::propertiesGUI() {
	ImGuiEx::Label("Name");
	bool res = inputString("##name", &m_name);

	if (ImGui::BeginTable("tab", 2, ImGuiTableFlags_Resizable)) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("Value");
		ImGui::TableNextColumn();
		ImGui::Text("Slot");

		for (Blend1DNode::Child& child : m_children) {
			ImGui::TableNextColumn();
			ImGui::PushID(&child);
		
			ImGui::SetNextItemWidth(-1);
			res = ImGui::InputFloat("##val", &child.value) || res;
		
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			res = editSlot(m_controller, "##anim", &child.slot) || res;

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
		m_children.emplace();
		if(m_children.size() > 1) {
			m_children.back().value = m_children[m_children.size() - 2].value;
		}
		res = true;
	}
	return res;
}

bool Blend1DNode::onGUI() {
	inputSlot(ImGuiEx::PinShape::SQUARE);
	outputSlot();
	ImGuiEx::TextUnformatted(m_name.c_str());
	return false;
}

anim::Node* Blend1DNode::compile(anim::Controller& controller) {
	UniquePtr<anim::Blend1DNode> node = UniquePtr<anim::Blend1DNode>::create(controller.m_allocator, controller.m_allocator);
	m_children.copyTo(node->m_children);
	ValueNode* val = castToValueNode(getInput(0));
	if (!val) return nullptr;
	node->m_value = (anim::ValueNode*)val->compile(controller);
	if (!node->m_value) return nullptr;

	return node.detach();	
}

void Blend1DNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_name);
	stream.write((u32)m_children.size());
	stream.write(m_children.begin(), m_children.byte_size());
}

void Blend1DNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_name);
	u32 count;
	stream.read(count);
	m_children.resize(count);
	stream.read(m_children.begin(), m_children.byte_size());
}

anim::Node* AnimationNode::compile(anim::Controller& controller) {
	anim::AnimationNode* node = LUMIX_NEW(controller.m_allocator, anim::AnimationNode);
	node->m_flags = m_flags;
	node->m_slot = m_slot;
	return node;
}

bool AnimationNode::propertiesGUI() {
	ImGuiEx::Label("Slot");
	bool res = editSlot(m_controller, "##slot", &m_slot);
	ImGuiEx::Label("Looping");
	bool loop = m_flags && Flags::LOOPED;
	if (ImGui::Checkbox("##loop", &loop)) {
		if (loop) m_flags = m_flags | Flags::LOOPED;
		else m_flags = m_flags & ~Flags::LOOPED;
		res = true;
	}
	return res;
}

bool AnimationNode::onGUI() {
	outputSlot();
	if (m_slot < (u32)m_controller.m_animation_slots.size()) {
		ImGui::TextUnformatted(ICON_FA_PLAY);
		ImGui::SameLine();
		ImGui::TextUnformatted(m_controller.m_animation_slots[m_slot].c_str());
	}
	else {
		ImGui::TextUnformatted(ICON_FA_PLAY " Animation");
	}
	return false;
}

AnimationNode::AnimationNode(Node* parent, Controller& controller, IAllocator& allocator) 
	: PoseNode(parent, controller, allocator) 
{}

void AnimationNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_slot);
	stream.write(m_flags);
}

void AnimationNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_slot);
	stream.read(m_flags);
}

LayersNode::Layer::Layer(IAllocator& allocator) 
	: name(allocator)
{
}

LayersNode::LayersNode(Node* parent, Controller& controller, IAllocator& allocator) 
	: PoseNode(parent, controller, allocator)
	, m_layers(allocator)
	, m_allocator(allocator)
{
}

LayersNode::~LayersNode() {
	for (Layer& l : m_layers) {
		LUMIX_DELETE(m_allocator, l.node);
	}
}

void LayersNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write((u32)m_layers.size());
	for (const Layer& layer : m_layers) {
		stream.writeString(layer.name);
		stream.write(layer.mask);
		stream.write(layer.node->type());
		layer.node->serialize(stream);
	}
}

anim::Node* LayersNode::compile(anim::Controller& controller) {
	ASSERT(false);
	return false;
}

void LayersNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	u32 c;
	stream.read(c);
	for (u32 i = 0; i < c; ++i) {
		Layer& layer = m_layers.emplace(m_allocator);
		layer.name = stream.readString();
		stream.read(layer.mask);
		anim::NodeType type;
		stream.read(type);
		layer.node = (PoseNode*)Node::create(this, type, m_controller, m_allocator);
		layer.node->deserialize(stream, ctrl, version);
	}
}

bool InputNode::onGUI() {
	outputSlot(ImGuiEx::PinShape::SQUARE);
	ImGui::TextUnformatted(ICON_FA_SIGN_IN_ALT);
	ImGui::SameLine();
	ImGui::TextUnformatted(m_controller.m_inputs[m_input_index].name);
	return false;
}

bool InputNode::propertiesGUI() {
	return editInput("Input", &m_input_index, m_controller);
}

anim::Node* InputNode::compile(anim::Controller& controller) {
	if (m_input_index >= (u32)m_controller.m_inputs.size()) return nullptr;
	anim::InputNode* node = LUMIX_NEW(controller.m_allocator, anim::InputNode);
	node->m_input_index = m_input_index;
	return node;
}

InputNode::InputNode(Node* parent, Controller& controller, IAllocator& allocator)
	: ValueNode(parent, controller, allocator)
{}

void InputNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_input_index);
}

void InputNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_input_index);
}

OutputNode::OutputNode(Node* parent, Controller& controller, IAllocator& allocator)
	: PoseNode(parent, controller, allocator)
{}

bool OutputNode::onGUI() {
	inputSlot();
	ImGuiEx::TextUnformatted(ICON_FA_SIGN_OUT_ALT " Output");
	return false;
}

anim::Node* OutputNode::compile(anim::Controller& controller) {
	Node* n = getInput(0);
	if (!n) return false;
	if (!n->isPoseNode()) return false;
	return n->compile(controller);
}

void OutputNode::serialize(OutputMemoryStream& stream) const { Node::serialize(stream); }
void OutputNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) { Node::deserialize(stream, ctrl, version); }

TreeNode::TreeNode(Node* parent, Controller& controller, IAllocator& allocator)
	: PoseNode(parent, controller, allocator)
	, m_name("new tree", allocator)
{
	LUMIX_NEW(m_allocator, OutputNode)(this, controller, m_allocator);
}

anim::Node* TreeNode::compile(anim::Controller& controller) {
	if (m_nodes.empty() || m_nodes[0]->type() != anim::NodeType::OUTPUT) return false;
	return m_nodes[0]->compile(controller);
}

bool TreeNode::propertiesGUI() {
	ImGuiEx::Label("Name");
	return inputString("##name", &m_name);
}

bool TreeNode::onGUI() {
	outputSlot();
	ImGui::TextUnformatted(ICON_FA_TREE);
	ImGui::SameLine();
	ImGui::TextUnformatted(m_name.c_str());
	return false;
}

void TreeNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	LUMIX_DELETE(m_allocator, m_nodes[0]);
	m_nodes.clear();
	Node::deserialize(stream, ctrl, version);
	stream.read(m_name);
}

void TreeNode::serialize(OutputMemoryStream& stream) const { Node::serialize(stream); stream.write(m_name); }

bool SelectNode::propertiesGUI() { 
	float node_blend_length = m_blend_length.seconds();
	ImGuiEx::Label("Blend length");
	if (ImGui::DragFloat("##bl", &node_blend_length)) {
		m_blend_length = Time::fromSeconds(node_blend_length);
		return true;
	}
	return false;
}

bool SelectNode::onGUI() {
	ImGuiEx::NodeTitle("Select");
	outputSlot();
	inputSlot(); ImGui::TextUnformatted("Value");
	
	bool changed = false;
	for (u32 i = 0; i < m_options_count; ++i) {
		inputSlot();
		ImGui::PushID(i);
		if (ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Remove")) {
			--m_options_count;
			for (i32 link_idx = m_parent->m_links.size() - 1; link_idx >= 0; --link_idx) {
				NodeEditorLink& link = m_parent->m_links[link_idx];
				if (link.getToNode() != m_id) continue;

				if (link.getToPin() == i + 1) {
					m_parent->m_links.erase(link_idx);
				}
				else if (link.getToPin() > i + 1) {
					link.to = link.getToNode() | (link.getToPin() << 16);
				}
			}
			changed = true;
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text("Option %d", i);
	}

	inputSlot(); ImGui::TextUnformatted("New option");
	if (getInput(1 + m_options_count)) {
		++m_options_count;
		changed = true;
	}
	return changed;
}

SelectNode::SelectNode(Node* parent, Controller& controller, IAllocator& allocator)
	: PoseNode(parent, controller, allocator)
{}

anim::Node* SelectNode::compile(anim::Controller& controller) {
	if (m_options_count == 0) return nullptr;
	ValueNode* value_node = castToValueNode(getInput(0));
	// TODO make sure value_node returns i32
	if (!value_node) return nullptr;

	UniquePtr<anim::SelectNode> node = UniquePtr<anim::SelectNode>::create(controller.m_allocator, controller.m_allocator);
	node->m_blend_length = m_blend_length;
	node->m_value = (anim::ValueNode*)value_node->compile(controller);
	if (!node->m_value) return nullptr;

	node->m_children.resize(m_options_count);
	for (u32 i = 0; i < m_options_count; ++i) {
		PoseNode* n = castToPoseNode(getInput(i + 1));
		if (!n) return nullptr;

		node->m_children[i] = (anim::PoseNode*)n->compile(controller);
		if (!node->m_children[i]) return nullptr;
	}
	return node.detach();
}

void SelectNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_blend_length);
	stream.read(m_options_count);
}

void SelectNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_blend_length);
	stream.write(m_options_count);
}

void Node::serialize(OutputMemoryStream& stream) const {
	stream.write(m_id);
	stream.write(m_pos);
	stream.writeArray(m_links);
	stream.write(m_nodes.size());
	for (Node* node : m_nodes) {
		stream.write(node->type());
		node->serialize(stream);
	}
}

void Node::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.read(m_id);
	stream.read(m_pos);
	stream.readArray(&m_links);
	u32 count;
	stream.read(count);
	m_nodes.reserve(count);
	for (u32 i = 0; i < count; ++i) {
		anim::NodeType type;
		stream.read(type);
		Node* child = Node::create(this, type, m_controller, m_allocator);
		child->deserialize(stream, ctrl, version);
	}
}

void Node::inputSlot(ImGuiEx::PinShape shape) {
	ImGuiEx::Pin(m_id | (u32(m_input_counter) << 16), true, shape);
	++m_input_counter;
}

Node* Node::getInput(u32 idx) const {
	if (!m_parent) return nullptr;
	for (const NodeEditorLink& link : m_parent->m_links) {
		if (link.getToNode() == m_id && link.getToPin() == idx) {
			for (Node* n : m_parent->m_nodes) {
				if (link.getFromNode() == n->m_id) return n;
			}
			ASSERT(false);
		}
	}
	return nullptr;
}

void Node::outputSlot(ImGuiEx::PinShape shape) {
	ImGuiEx::Pin(m_id | (u32(m_output_counter) << 16) | OUTPUT_FLAG, false, shape);
	++m_output_counter;
}

bool Node::nodeGUI() {
	m_input_counter = 0;
	m_output_counter = 0;
	const ImVec2 old_pos = m_pos;
	ImGuiEx::BeginNode(m_id, m_pos, &m_selected);
	bool res = onGUI();
	if (m_error.length() > 0) {
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0xff, 0, 0, 0xff));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 4);
	}
	else if (!m_reachable) {
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_TableBorderLight));
	}
	ImGuiEx::EndNode();
	if (m_error.length() > 0) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetItemRectMax() - ImGui::GetStyle().FramePadding;
		dl->AddText(p, IM_COL32(0xff, 0, 0, 0xff), ICON_FA_EXCLAMATION_TRIANGLE);
			
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_error.c_str());
	}
	else if (!m_reachable) {
		ImGui::PopStyleColor();
	}
	return res;
}

Node* Node::create(Node* parent, Type type, Controller& controller, IAllocator& allocator) {
	switch (type) {
		case anim::NodeType::ANIMATION: return LUMIX_NEW(allocator, AnimationNode)(parent, controller, allocator);
		case anim::NodeType::BLEND1D: return LUMIX_NEW(allocator, Blend1DNode)(parent, controller, allocator);
		case anim::NodeType::BLEND2D: return LUMIX_NEW(allocator, Blend2DNode)(parent, controller, allocator);
		case anim::NodeType::LAYERS: return LUMIX_NEW(allocator, LayersNode)(parent, controller, allocator);
		case anim::NodeType::SELECT: return LUMIX_NEW(allocator, SelectNode)(parent, controller, allocator);
		case anim::NodeType::TREE: return LUMIX_NEW(allocator, TreeNode)(parent, controller, allocator);
		case anim::NodeType::OUTPUT: return LUMIX_NEW(allocator, OutputNode)(parent, controller, allocator);
		case anim::NodeType::INPUT: return LUMIX_NEW(allocator, InputNode)(parent, controller, allocator);
		case anim::NodeType::NONE: ASSERT(false); return nullptr;
	}
	ASSERT(false);
	return nullptr;
}

} // namespace Lumix::anim
