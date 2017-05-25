#include "state_machine_editor.h"
#include "animation/animation.h"
#include "animation/animation_system.h"
#include "animation/controller.h"
#include "animation/editor/animation_editor.h"
#include "animation/events.h"
#include "animation/state_machine.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include <cmath>
#include <cstdlib>


namespace Lumix
{


static const ResourceType CONTROLLER_RESOURCE_TYPE("anim_controller");
static const ResourceType ANIMATION_TYPE("animation");


static ImVec2 operator+(const ImVec2& a, const ImVec2& b)
{
	return ImVec2(a.x + b.x, a.y + b.y);
}


static ImVec2 operator-(const ImVec2& a, const ImVec2& b)
{
	return ImVec2(a.x - b.x, a.y - b.y);
}


static ImVec2 operator*(const ImVec2& a, float b)
{
	return ImVec2(a.x * b, a.y * b);
}


static float dot(const ImVec2& a, const ImVec2& b)
{
	return a.x * b.x + a.y * b.y;
}


namespace AnimEditor
{


static int autocompleteCallback(ImGuiTextEditCallbackData *data)
{
	auto* controller = (AnimEditor::ControllerResource*)data->UserData;
	char tmp[128];
	int start_word = data->CursorPos;
	while (start_word > 0 && data->Buf[start_word - 1] != ' ') --start_word;
	copyNString(tmp, lengthOf(tmp), data->Buf + start_word, data->CursorPos - start_word);

	const auto& input_decl = controller->getEngineResource()->m_input_decl;
	for (const auto& input : input_decl.inputs)
	{
		if (input.type != Anim::InputDecl::EMPTY && startsWith(input.name, tmp))
		{
			data->InsertChars(data->CursorPos, input.name + stringLength(tmp));
			return 0;
		}
	}
	for (const auto& constant : input_decl.constants)
	{
		if (constant.type != Anim::InputDecl::EMPTY && startsWith(constant.name, tmp))
		{
			data->InsertChars(data->CursorPos, constant.name + stringLength(tmp));
			return 0;
		}
	}

	if (startsWith("finishing()", tmp))
	{
		data->InsertChars(data->CursorPos, "finishing()" + stringLength(tmp));
		return 0;
	}

	return 0;
};


static ImVec2 getEdgeStartPoint(const ImVec2 a_pos, const ImVec2 a_size, const ImVec2 b_pos, const ImVec2 b_size, bool is_dir)
{
	ImVec2 center_a = a_pos + a_size * 0.5f;
	ImVec2 center_b = b_pos + b_size * 0.5f;
	ImVec2 dir = center_b - center_a;
	if (fabs(dir.x / dir.y) > fabs(a_size.x / a_size.y))
	{
		dir = dir * fabs(1 / dir.x);
		return center_a + dir * a_size.x * 0.5f + ImVec2(0, center_a.y > center_b.y == is_dir ? 5.0f : -5.0f);
	}

	dir = dir * fabs(1 / dir.y);
	return center_a + dir * a_size.y * 0.5f + ImVec2(center_a.x > center_b.x == is_dir ? 5.0f : -5.0f, 0);
}


static Component* createComponent(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller)
{
	IAllocator& allocator = controller.getAllocator();
	switch (engine_cmp->type)
	{
	case Anim::Component::EDGE: return LUMIX_NEW(allocator, Edge)((Anim::Edge*)engine_cmp, parent, controller);
	case Anim::Component::BLEND1D: return LUMIX_NEW(allocator, Blend1DNode)((Anim::Blend1DNode*)engine_cmp, parent, controller);
	case Anim::Component::SIMPLE_ANIMATION:
		return LUMIX_NEW(allocator, AnimationNode)(engine_cmp, parent, controller);
	case Anim::Component::STATE_MACHINE: return LUMIX_NEW(allocator, StateMachine)(engine_cmp, parent, controller);
	default: ASSERT(false); return nullptr;
	}
}


static ImVec2 getEdgeStartPoint(Node* a, Node* b, bool is_dir)
{
	return getEdgeStartPoint(a->pos, a->size, b->pos, b->size, is_dir);
}


static void drawEdge(ImDrawList* draw, Node* from_node, Node* to_node, u32 color, const ImVec2& canvas_screen_pos)
{
	ImVec2 from = getEdgeStartPoint(from_node, to_node, true) + canvas_screen_pos;
	ImVec2 to = getEdgeStartPoint(to_node, from_node, false) + canvas_screen_pos;
	draw->AddLine(from, to, color);
	ImVec2 dir = to - from;
	dir = dir * (1 / sqrt(dot(dir, dir))) * 5;
	ImVec2 right(dir.y, -dir.x);
	draw->AddLine(to, to - dir + right, color);
	draw->AddLine(to, to - dir - right, color);

}


Component::~Component()
{
	if (getParent())
	{
		getParent()->removeChild(this);
	}
}


Node::Node(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller)
	: Component(engine_cmp, parent, controller)
	, m_edges(controller.getAllocator())
	, m_in_edges(controller.getAllocator())
	, m_allocator(controller.getAllocator())
	, name("")
{
}


Node::~Node()
{
	while (!m_edges.empty())
	{
		LUMIX_DELETE(m_controller.getAllocator(), m_edges.back());
	}
	while (!m_in_edges.empty())
	{
		LUMIX_DELETE(m_controller.getAllocator(), m_in_edges.back());
	}
}


bool Node::hitTest(const ImVec2& on_canvas_pos) const
{
	return on_canvas_pos.x >= pos.x && on_canvas_pos.x < pos.x + size.x
		&& on_canvas_pos.y >= pos.y && on_canvas_pos.y < pos.y + size.y;
}


void Node::removeEvent(int index)
{
	auto* engine_node = ((Anim::Node*)engine_cmp);
	auto& events = engine_node->events;
	Anim::EventHeader header = *(Anim::EventHeader*)&events[sizeof(Anim::EventHeader) * index];
	u8* headers_end = &events[sizeof(Anim::EventHeader) * engine_node->events_count];
	u8* end = &events.back() + 1;
	u8* event_start = headers_end + header.offset;
	u8* event_end = event_start + header.size;
	
	for (int i = index + 1; i < engine_node->events_count; ++i)
	{
		auto& h = *(Anim::EventHeader*)&events[sizeof(Anim::EventHeader) * i];
		h.offset -= header.size;
	}

	u8* header_start = &events[sizeof(Anim::EventHeader) * index];
	u8* header_end = header_start + sizeof(Anim::EventHeader);
	moveMemory(header_start, header_end, event_start - header_end);
	moveMemory(event_start - sizeof(Anim::EventHeader), event_end, end - event_end);
	
	events.resize(events.size() - sizeof(Anim::EventHeader) - header.size);

	--engine_node->events_count;
}


static const char* getEventTypeName(const Anim::EventHeader& event, AnimEditor::IAnimationEditor& editor)
{
	int count = editor.getEventTypesCount();
	for (int i = 0; i < count; ++i)
	{
		if (editor.getEventTypeByIdx(i).type == event.type)
		{
			return editor.getEventTypeByIdx(i).label;
		}
	}
	return "Unknown";
}


void Node::onGUI()
{
	u32 set_input_type = crc32("set_input");
	ImGui::InputText("Name", name.data, lengthOf(name.data));
	if (!engine_cmp) return;
	if (!ImGui::CollapsingHeader("Events")) return;

	auto* engine_node = ((Anim::Node*)engine_cmp);
	auto& events = engine_node->events;
	auto& editor = m_controller.getEditor();
	for(int i = 0; i < engine_node->events_count; ++i)
	{
		Anim::EventHeader& header = *(Anim::EventHeader*)&events[sizeof(Anim::EventHeader) * i];
		const char* event_type_name = getEventTypeName(header, editor);
		if (ImGui::TreeNode((void*)(uintptr)i, "%s - %fs", event_type_name, header.time))
		{
			if (ImGui::Button("Remove"))
			{
				removeEvent(i);
				ImGui::TreePop();
				break;
			}
			ImGui::InputFloat("Time", &header.time);
			int event_offset = header.offset + sizeof(Anim::EventHeader) * engine_node->events_count;
			editor.getEventType(header.type).editor.invoke(&events[event_offset], *this);
			ImGui::TreePop();
		}
	}

	auto getter = [](void* data, int idx, const char** out) -> bool {
		auto* node = (Node*)data;
		*out = node->m_controller.getEditor().getEventTypeByIdx(idx).label;
		return true;
	};
	static int current = 0;
	ImGui::Combo("", &current, getter, this, editor.getEventTypesCount());
	ImGui::SameLine();
	if (ImGui::Button("Add event"))
	{
		auto newEvent = [&](int size, u32 type) {
			int old_payload_size = events.size() - sizeof(Anim::EventHeader) * engine_node->events_count;
			events.resize(events.size() + size + sizeof(Anim::EventHeader));
			u8* headers_end = &events[engine_node->events_count * sizeof(Anim::EventHeader)];
			moveMemory(headers_end + sizeof(Anim::EventHeader), headers_end, old_payload_size);
			Anim::EventHeader& event_header =
				*(Anim::EventHeader*)&events[sizeof(Anim::EventHeader) * engine_node->events_count];
			event_header.type = type;
			event_header.time = 0;
			event_header.size = size;
			event_header.offset = old_payload_size;
			return headers_end + old_payload_size;
		};

		auto& event_type = editor.getEventTypeByIdx(current);
		newEvent(event_type.size, event_type.type);
		++engine_node->events_count;
	}
}


void Node::serialize(OutputBlob& blob)
{
	blob.write(pos);
	blob.write(size);
	blob.write(name);
}


void Node::deserialize(InputBlob& blob)
{
	blob.read(pos);
	blob.read(size);
	blob.read(name);
}


static ImVec2 drawNode(ImDrawList* draw, const char* label, const ImVec2 pos, bool selected)
{
	float text_width = ImGui::CalcTextSize(label).x;
	ImVec2 size;
	size.x = Math::maximum(50.0f, text_width + ImGui::GetStyle().FramePadding.x * 2);
	size.y = ImGui::GetTextLineHeightWithSpacing() * 2;
	ImVec2 from = pos;
	ImVec2 to = from + size;
	ImU32 color = ImGui::ColorConvertFloat4ToU32(
		selected ? ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] : ImGui::GetStyle().Colors[ImGuiCol_Button]);

	draw->AddRectFilled(from, to, color, 5);
	draw->AddRect(from + ImVec2(1, 1), to + ImVec2(1, 1), ImGui::GetColorU32(ImGuiCol_BorderShadow), 5);
	draw->AddRect(from, to, ImGui::GetColorU32(ImGuiCol_Border), 5);

	ImGui::SetCursorScreenPos(from + ImVec2((size.x - text_width) * 0.5f, size.y * 0.25f));
	ImGui::Text("%s", label);

	ImGui::SetCursorScreenPos(from);
	ImGui::InvisibleButton("bg", size);
	return size;

}


bool Node::draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected)
{
	ImGui::PushID(engine_cmp);
	size = drawNode(draw, name, canvas_screen_pos + pos, selected);
	ImGui::PopID();
	return ImGui::IsItemActive();
}


Container::Container(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller)
	: Node(engine_cmp, parent, controller)
	, m_editor_cmps(controller.getAllocator())
	, m_selected_component(nullptr)
{
}


Container::~Container()
{
	while (!m_editor_cmps.empty())
	{
		LUMIX_DELETE(m_controller.getAllocator(), m_editor_cmps.back());
	}
}


void Container::removeChild(Component* component)
{
	auto* engine_container = ((Anim::Container*)engine_cmp);
	engine_container->children.eraseItem(component->engine_cmp);
	m_editor_cmps.eraseItem(component);
	if (component == m_selected_component) m_selected_component = nullptr;
}


void Container::createEdge(int from_uid, int to_uid, int edge_uid)
{
	auto* engine_parent = ((Anim::Container*)engine_cmp);
	// TODO different kind of edges
	auto* engine_edge = LUMIX_NEW(m_allocator, Anim::Edge)(m_allocator);
	engine_edge->uid = edge_uid;
	engine_edge->from = (Anim::Node*)getByUID(from_uid)->engine_cmp;
	engine_edge->to = (Anim::Node*)getByUID(to_uid)->engine_cmp;
	engine_parent->children.push(engine_edge);

	auto* edge = LUMIX_NEW(m_allocator, Edge)(engine_edge, this, m_controller);
	m_editor_cmps.push(edge);
	m_selected_component = edge;
}


void Container::createNode(Anim::Node::Type type, int uid, const ImVec2& pos)
{

}


void Container::destroyChild(int child_uid)
{
	auto* child = getByUID(child_uid);
	LUMIX_DELETE(m_allocator, child);
}


Component* Container::childrenHitTest(const ImVec2& pos)
{
	for (auto* i : m_editor_cmps)
	{
		if (i->hitTest(pos)) return i;
	}
	return nullptr;
}


Component* Container::getChildByUID(int uid)
{
	for (auto* i : m_editor_cmps)
	{
		if (i->engine_cmp && i->engine_cmp->uid == uid) return i;
	}
	return nullptr;
}


Edge::Edge(Anim::Edge* engine_cmp, Container* parent, ControllerResource& controller)
	: Component(engine_cmp, parent, controller)
{
	m_from = (Node*)parent->getChildByUID(engine_cmp->from->uid);
	m_to = (Node*)parent->getChildByUID(engine_cmp->to->uid);
	ASSERT(m_from);
	ASSERT(m_to);
	m_from->addEdge(this);
	m_to->addInEdge(this);
	m_expression = "finishing()";
}


Edge::~Edge()
{
	m_from->removeEdge(this);
	m_to->removeInEdge(this);
}


void Edge::debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime)
{
	if (runtime->source.type != engine_cmp->type) return;
	
	ImVec2 from = getEdgeStartPoint(m_from, m_to, true) + canvas_screen_pos;
	ImVec2 to = getEdgeStartPoint(m_to, m_from, false) + canvas_screen_pos;

	float t = runtime->getTime() / runtime->getLength();
	ImVec2 p = from + (to - from) * t;
	ImVec2 dir = to - from;
	dir = dir * (1 / sqrt(dot(dir, dir))) * 2;
	draw->AddLine(p - dir, p + dir, 0xfff00FFF, 3);
}


void Edge::compile()
{
	auto* engine_edge = (Anim::Edge*)engine_cmp;
	engine_edge->condition.compile(m_expression, m_controller.getEngineResource()->m_input_decl);
}


void Edge::onGUI()
{
	auto* engine_edge = (Anim::Edge*)engine_cmp;
	ImGui::DragFloat("Length", &engine_edge->length);
	
	if (ImGui::InputText("Expression",
			m_expression.data,
			lengthOf(m_expression.data),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion,
			autocompleteCallback,
			&getController()))
	{
		if (!engine_edge->condition.compile(m_expression, m_controller.getEngineResource()->m_input_decl))
		{
			g_log_error.log("Animation") << "Failed to compile condition " << m_expression;
		}
	}
}


bool Edge::draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected)
{
	u32 color = ImGui::ColorConvertFloat4ToU32(
		selected ? ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
	ImVec2 from = getEdgeStartPoint(m_from, m_to, true) + canvas_screen_pos;
	ImVec2 to = getEdgeStartPoint(m_to, m_from, false) + canvas_screen_pos;
	draw->AddLine(from, to, color);
	ImVec2 dir = to - from;
	dir = dir * (1 / sqrt(dot(dir, dir))) * 5;
	ImVec2 right(dir.y, -dir.x);
	draw->AddLine(to, to - dir + right, color);
	draw->AddLine(to, to - dir - right, color);
	if (ImGui::IsMouseClicked(0) && hitTest(ImGui::GetMousePos() - canvas_screen_pos))
	{
		return true;
	}
	return false;
}


void Edge::serialize(OutputBlob& blob)
{
	blob.write(m_from->engine_cmp->uid);
	blob.write(m_to->engine_cmp->uid);
	blob.write(m_expression);
}


void Edge::deserialize(InputBlob& blob)
{
	int uid;
	blob.read(uid);
	m_from = (Node*)m_parent->getChildByUID(uid);
	blob.read(uid);
	m_to = (Node*)m_parent->getChildByUID(uid);
	blob.read(m_expression);
}


bool Edge::hitTest(const ImVec2& on_canvas_pos) const
{
	ImVec2 a = getEdgeStartPoint(m_from, m_to, true);
	ImVec2 b = getEdgeStartPoint(m_to, m_from, false);

	ImVec2 dif = a - b;
	float len_squared = dif.x * dif.x + dif.y * dif.y;
	float t = Math::clamp(dot(on_canvas_pos - a, b - a) / len_squared, 0.0f, 1.0f);
	const ImVec2 projection = a + (b - a) * t;
	ImVec2 dist_vec = on_canvas_pos - projection;

	return dot(dist_vec, dist_vec) < 100;
}


struct Blend1DNode::RootEdge : public Component
{
	RootEdge(Blend1DNode* parent, Node* to, ControllerResource& controller)
		: Component(nullptr, parent, controller)
		, m_parent(parent)
		, m_to(to)
	{
		parent->getRootNode()->edges.push(this);
	}


	~RootEdge()
	{
		m_parent->removeChild(this);
	}

	void serialize(OutputBlob& blob) override {}
	void deserialize(InputBlob& blob) override {}
	bool hitTest(const ImVec2& on_canvas_pos) const
	{
		ImVec2 a = getEdgeStartPoint(m_parent->getRootNode(), m_to, true);
		ImVec2 b = getEdgeStartPoint(m_to, m_parent->getRootNode(), false);

		ImVec2 dif = a - b;
		float len_squared = dif.x * dif.x + dif.y * dif.y;
		float t = Math::clamp(dot(on_canvas_pos - a, b - a) / len_squared, 0.0f, 1.0f);
		const ImVec2 projection = a + (b - a) * t;
		ImVec2 dist_vec = on_canvas_pos - projection;

		return dot(dist_vec, dist_vec) < 100;
	}


	void onGUI() override
	{
		auto* engine_node = (Anim::Blend1DNode*)m_parent->engine_cmp;
		bool changed = false;
		for (auto& item : engine_node->items)
		{
			if (item.node == m_to->engine_cmp)
			{
				changed = ImGui::InputFloat("Value", &item.value) || changed;
				break;
			}
		}

		if (changed)
		{
			auto comparator = [](const void* a, const void* b) -> int {
				float v0 = ((Anim::Blend1DNode::Item*)a)->value;
				float v1 = ((Anim::Blend1DNode::Item*)b)->value;
				if (v0 < v1) return -1;
				if (v0 > v1) return 1;
				return 0;
			};

			qsort(&engine_node->items[0], engine_node->items.size(), sizeof(engine_node->items[0]), comparator);
		}
	}


	bool isNode() const override { return false; }


	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override
	{
		u32 color = ImGui::ColorConvertFloat4ToU32(
			selected ? ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
		drawEdge(draw, m_parent->getRootNode(), m_to, color, canvas_screen_pos);
		if (ImGui::IsMouseClicked(0) && hitTest(ImGui::GetMousePos() - canvas_screen_pos))
		{
			return true;
		}
		return false;
	}

	Node* getTo() const { return m_to; }

private:
	Blend1DNode* m_parent;
	Node* m_to;
};


Blend1DNode::Blend1DNode(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller)
	: Container(engine_cmp, parent, controller)
{
	m_root_node = LUMIX_NEW(controller.getAllocator(), RootNode)(this, controller);
	m_editor_cmps.push(m_root_node);
}


void Blend1DNode::debugInside(ImDrawList* draw,
	const ImVec2& canvas_screen_pos,
	Anim::ComponentInstance* runtime,
	Container* current)
{
	if (runtime->source.type != Anim::Component::BLEND1D) return;
	auto* runtime_b1 = (Anim::Blend1DNodeInstance*)runtime;
	auto& children_runtime = runtime_b1->instances;
	auto& source = (Anim::Blend1DNode&)runtime->source;
	for (int i = 0; i < source.children.size() && i < lengthOf(children_runtime); ++i)
	{
		auto* child_runtime = (Anim::NodeInstance*)children_runtime[i];
		auto* child = getChildByUID(child_runtime->source.uid);
		if (!child) continue;

		if (current == this)
		{
			if (runtime_b1->a0 == child_runtime || runtime_b1->a1 == child_runtime)
			{
				child->debug(draw, canvas_screen_pos, child_runtime);
				float t = runtime_b1->current_weight;
				if (runtime_b1->a0 == child_runtime) t = 1 - t;
				ImVec2 to = getEdgeStartPoint((Node*)child, m_root_node, false);
				ImVec2 from = getEdgeStartPoint(m_root_node, (Node*)child, true);
				ImVec2 dir = to - from;
				to = from + dir*t;
				draw->AddLine(from + canvas_screen_pos, to + canvas_screen_pos, 0xfff00fff);
			}
		}
		else
		{
			child->debugInside(draw, canvas_screen_pos, child_runtime, current);
		}
	}
}


void Blend1DNode::dropSlot(const char* name, u32 slot, const ImVec2& canvas_screen_pos)
{
	createNode(Anim::Component::SIMPLE_ANIMATION, m_controller.createUID(), ImGui::GetMousePos() - canvas_screen_pos);
	auto* node = (AnimationNode*)m_selected_component;
	node->name = name;
	auto* engine_node = (Anim::AnimationNode*)node->engine_cmp;
	engine_node->animations_hashes.emplace(slot);
}


void Blend1DNode::removeChild(Component* component)
{
	Container::removeChild(component);
	auto* engine_b1 = (Anim::Blend1DNode*)engine_cmp;
	for (int i = 0; i < engine_b1->items.size(); ++i)
	{
		if (engine_b1->items[i].node == component->engine_cmp)
		{
			engine_b1->items.erase(i);
			LUMIX_DELETE(m_controller.getAllocator(), m_root_node->edges[i]);
			break;
		}
	}
}


void Blend1DNode::serialize(OutputBlob& blob)
{
	Container::serialize(blob);
	m_root_node->serialize(blob);
	blob.write(m_root_node->edges.size());
	for (RootEdge* edge : m_root_node->edges)
	{
		blob.write(edge->getTo()->engine_cmp->uid);
	}
}


void Blend1DNode::deserialize(InputBlob& blob)
{
	Container::deserialize(blob);

	m_root_node->deserialize(blob);
	int count;
	blob.read(count);
	for (int i = 0; i < count; ++i)
	{
		int uid;
		blob.read(uid);
		Node* node = (Node*)getChildByUID(uid);
		auto* edge = LUMIX_NEW(m_allocator, RootEdge)(this, node, m_controller);
		m_editor_cmps.push(edge);
	}

	auto& input_decl = m_controller.getEngineResource()->m_input_decl;
	m_input = -1;
	int offset = ((Anim::Blend1DNode*)engine_cmp)->input_offset;
	for (int i = 0; i < lengthOf(input_decl.inputs); ++i)
	{
		if (input_decl.inputs[i].type != Anim::InputDecl::EMPTY && input_decl.inputs[i].offset == offset)
		{
			m_input = i;
			break;
		}
	}
}


void Blend1DNode::createNode(Anim::Component::Type type, int uid, const ImVec2& pos)
{
	auto* cmp = (Node*)createComponent(Anim::createComponent(type, m_allocator), this, m_controller);
	cmp->pos = pos;
	cmp->size.x = 100;
	cmp->size.y = 30;
	cmp->engine_cmp->uid = uid;
	m_editor_cmps.push(cmp);
	((Anim::Blend1DNode*)engine_cmp)->children.push(cmp->engine_cmp);
	m_selected_component = cmp;
}


Blend1DNode::RootEdge* Blend1DNode::createRootEdge(Node* node)
{
	auto* edge = LUMIX_NEW(m_allocator, RootEdge)(this, node, m_controller);
	m_editor_cmps.push(edge);

	auto* engine_b1 = (Anim::Blend1DNode*)engine_cmp;
	auto& engine_edge = engine_b1->items.emplace();
	engine_edge.node = (Anim::Node*)node->engine_cmp;
	return edge;
}



void Blend1DNode::drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos)
{
	if (ImGui::IsWindowHovered())
	{
		if (ImGui::IsMouseClicked(0)) m_selected_component = nullptr;
		if (ImGui::IsMouseReleased(1) && m_mouse_status == NONE)
		{
			m_context_cmp = nullptr;
			ImGui::OpenPopup("context_menu");
		}
	}

	for (int i = 0; i < m_editor_cmps.size(); ++i)
	{
		Component* cmp = m_editor_cmps[i];
		if (cmp->draw(draw, canvas_screen_pos, m_selected_component == cmp))
		{
			m_selected_component = cmp;
		}

		if (cmp->isNode() && ImGui::IsItemHovered())
		{
			if (ImGui::IsMouseClicked(0))
			{
				m_drag_source = (Node*)cmp;
				m_mouse_status = DOWN_LEFT;
			}
			if (ImGui::IsMouseClicked(1))
			{
				m_drag_source = (Node*)cmp;
				m_mouse_status = DOWN_RIGHT;
			}
		}

		if (m_mouse_status == DOWN_RIGHT && ImGui::IsMouseDragging(1)) m_mouse_status = NEW_EDGE;
		if (m_mouse_status == DOWN_LEFT && ImGui::IsMouseDragging(0) && m_drag_source) m_mouse_status = DRAG_NODE;
	}

	if (m_mouse_status == DRAG_NODE && !m_drag_source) m_mouse_status = NONE;

	if (ImGui::IsMouseReleased(1))
	{
		Component* hit_cmp = childrenHitTest(ImGui::GetMousePos() - canvas_screen_pos);
		if (hit_cmp)
		{
			if (m_mouse_status == NEW_EDGE)
			{
				if (hit_cmp != m_drag_source && hit_cmp->isNode())
				{
					if (hit_cmp == m_root_node)
					{
						createRootEdge(m_drag_source);
					}
					else if (m_drag_source == m_root_node)
					{
						createRootEdge((Node*)hit_cmp);
					}
					else
					{
						auto* engine_parent = ((Anim::Container*)engine_cmp);
						auto* engine_edge = LUMIX_NEW(m_allocator, Anim::Edge)(m_allocator);
						engine_edge->uid = m_controller.createUID();
						engine_edge->from = (Anim::Node*)m_drag_source->engine_cmp;
						engine_edge->to = (Anim::Node*)hit_cmp->engine_cmp;
						engine_parent->children.push(engine_edge);

						auto* edge = LUMIX_NEW(m_allocator, Edge)(engine_edge, this, m_controller);
						m_editor_cmps.push(edge);
						m_selected_component = edge;
					}
				}
			}
			else
			{
				m_context_cmp = hit_cmp;
				m_selected_component = hit_cmp;
				ImGui::OpenPopup("context_menu");
			}
		}
	}

	if (m_mouse_status == DRAG_NODE)
	{
		m_drag_source->pos = m_drag_source->pos + ImGui::GetIO().MouseDelta;
	}

	if (ImGui::IsMouseReleased(0) || ImGui::IsMouseReleased(1)) m_mouse_status = NONE;

	if (m_mouse_status == NEW_EDGE)
	{
		draw->AddLine(canvas_screen_pos + m_drag_source->pos + m_drag_source->size * 0.5f, ImGui::GetMousePos(), 0xfff00FFF);
	}

	auto& editor = m_controller.getEditor();
	if (ImGui::BeginPopup("context_menu"))
	{
		ImVec2 pos_on_canvas = ImGui::GetMousePos() - canvas_screen_pos;
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Simple")) editor.createNode(m_controller, this, Anim::Component::SIMPLE_ANIMATION, pos_on_canvas);
			if (ImGui::MenuItem("State machine")) editor.createNode(m_controller, this, Anim::Component::STATE_MACHINE, pos_on_canvas);
			if (ImGui::MenuItem("Blend 1D")) editor.createNode(m_controller, this, Anim::Component::BLEND1D, pos_on_canvas);
			ImGui::EndMenu();
		}
		if (m_context_cmp && m_context_cmp != m_root_node)
		{
			if (ImGui::MenuItem("Remove"))
			{
				LUMIX_DELETE(m_controller.getAllocator(), m_context_cmp);
				if (m_selected_component == m_context_cmp) m_selected_component = nullptr;
				if (m_drag_source == m_context_cmp) m_drag_source = nullptr;
				m_context_cmp = nullptr;
			}
		}
		ImGui::EndPopup();
	}
}


void Blend1DNode::compile()
{
	auto* engine_node = (Anim::Blend1DNode*)engine_cmp;
	Anim::InputDecl& decl = m_controller.getEngineResource()->m_input_decl;
	if (m_input >= 0)
	{
		engine_node->input_offset = decl.inputs[m_input].offset;
	}
	else
	{
		engine_node->input_offset = -1;
	}
}


void Blend1DNode::debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime)
{
	if (runtime->source.type != engine_cmp->type) return;

	ImVec2 p = canvas_screen_pos + pos;
	p = p + ImVec2(5, ImGui::GetTextLineHeightWithSpacing() * 1.5f);
	draw->AddRect(p, p + ImVec2(size.x - 10, 5), 0xfff00fff);
	float t = Math::clamp(runtime->getTime() / runtime->getLength(), 0.0f, 1.0f);
	draw->AddRectFilled(p, p + ImVec2((size.x - 10) * t, 5), 0xfff00fff);
}


void Blend1DNode::onGUI()
{
	Container::onGUI();
	if (ImGui::Button("Show Children"))
	{
		m_controller.getEditor().setContainer(this);
	}

	auto* node = (Anim::Blend1DNode*)engine_cmp;
	auto getter = [](void* data, int idx, const char** out) -> bool {
		auto* node = (Blend1DNode*)data;
		auto& slots = node->m_controller.getAnimationSlots();
		*out = slots[idx].c_str();
		return true;
	};

	Anim::InputDecl& decl = m_controller.getEngineResource()->m_input_decl;
	auto input_getter = [](void* data, int idx, const char** out) -> bool {
		auto& decl = *(Anim::InputDecl*)data;
		int input_idx = decl.inputFromLinearIdx(idx);
		const auto& input = decl.inputs[input_idx];
		*out = input.name;
		return true;
	};
	int linear = decl.inputToLinearIdx(m_input);
	ImGui::Combo("Input", &linear, input_getter, &decl, decl.inputs_count);
	m_input = decl.inputFromLinearIdx(linear);
}


AnimationNode::AnimationNode(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller)
	: Node(engine_cmp, parent, controller)
{
}


void AnimationNode::deserialize(InputBlob& blob)
{
	Node::deserialize(blob);
	auto& input_decl = m_controller.getEngineResource()->m_input_decl;
	root_rotation_input = -1;
	int offset = ((Anim::AnimationNode*)engine_cmp)->root_rotation_input_offset;
	for (int i = 0; i < lengthOf(input_decl.inputs); ++i)
	{
		if (input_decl.inputs[i].type != Anim::InputDecl::EMPTY && input_decl.inputs[i].offset == offset)
		{
			root_rotation_input = i;
			break;
		}
	}
}


void AnimationNode::compile()
{
	auto* engine_node = (Anim::AnimationNode*)engine_cmp;
	Anim::InputDecl& decl = m_controller.getEngineResource()->m_input_decl;
	if (root_rotation_input >= 0)
	{
		engine_node->root_rotation_input_offset = decl.inputs[root_rotation_input].offset;
	}
	else
	{
		engine_node->root_rotation_input_offset = -1;
	}
}


void AnimationNode::debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime)
{
	if (runtime->source.type != engine_cmp->type) return;

	ImVec2 p = canvas_screen_pos + pos;
	p = p + ImVec2(5, ImGui::GetTextLineHeightWithSpacing() * 1.5f);
	draw->AddRect(p, p + ImVec2(size.x - 10, 5), 0xfff00fff);
	float t = Math::clamp(runtime->getTime() / runtime->getLength(), 0.0f, 1.0f);
	draw->AddRectFilled(p, p + ImVec2((size.x - 10) * t, 5), 0xfff00fff);
}


void AnimationNode::onGUI()
{
	Node::onGUI();
	
	auto* node = (Anim::AnimationNode*)engine_cmp;
	auto getter = [](void* data, int idx, const char** out) -> bool {
		auto* node = (AnimationNode*)data;
		auto& slots = node->m_controller.getAnimationSlots();
		*out = slots[idx].c_str();
		return true;
	};
	
	auto& slots = m_controller.getAnimationSlots();
	int current = 0;

	for (int i = 0; i < node->animations_hashes.size(); ++i)
	{
		for (current = 0; current < slots.size() && crc32(slots[current].c_str()) != node->animations_hashes[i]; ++current);
		ImGui::PushID(i);
		if (ImGui::Combo("Animation", &current, getter, this, slots.size()))
		{
			node->animations_hashes[i] = crc32(slots[current].c_str());
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove"))
		{
			node->animations_hashes.erase(i);
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Add animation"))
	{
		node->animations_hashes.emplace(0);
	}
	ImGui::Checkbox("Looped", &node->looped);
	ImGui::Checkbox("New selection on loop", &node->new_on_loop);

	Anim::InputDecl& decl = m_controller.getEngineResource()->m_input_decl;
	auto input_getter = [](void* data, int idx, const char** out) -> bool {
		auto& decl = *(Anim::InputDecl*)data;
		if (idx >= decl.inputs_count)
		{
			*out = "No root motion rotation";
			return true;
		}
		int input_idx = decl.inputFromLinearIdx(idx);
		const auto& input = decl.inputs[input_idx];
		*out = input.name;
		return true;
	};
	int linear = decl.inputToLinearIdx(root_rotation_input);
	if (ImGui::Combo("Root rotation input", &linear, input_getter, &decl, decl.inputs_count + 1))
	{
		if (linear >= decl.inputs_count)
		{
			root_rotation_input = -1;
		}
		else
		{
			root_rotation_input = decl.inputFromLinearIdx(linear);
		}
	}
	if (root_rotation_input != -1)
	{
		float deg = Math::radiansToDegrees(node->max_root_rotation_speed);
		if (ImGui::DragFloat("Max root rotation speed (deg/s)", &deg))
		{
			node->max_root_rotation_speed = Math::degreesToRadians(deg);
		}
	}
}


struct EntryEdge : public Component
{
	EntryEdge(StateMachine* parent, Node* to, ControllerResource& controller)
		: Component(nullptr, parent, controller)
		, m_parent(parent)
		, m_to(to)
	{
		parent->getEntryNode()->entries.push(this);
		expression = "";
	}


	~EntryEdge()
	{
		m_parent->removeEntry(*this);
	}

	void serialize(OutputBlob& blob) override {}
	void deserialize(InputBlob& blob) override {}
	bool hitTest(const ImVec2& on_canvas_pos) const
	{
		ImVec2 a = getEdgeStartPoint(m_parent->getEntryNode(), m_to, true);
		ImVec2 b = getEdgeStartPoint(m_to, m_parent->getEntryNode(), false);

		ImVec2 dif = a - b;
		float len_squared = dif.x * dif.x + dif.y * dif.y;
		float t = Math::clamp(dot(on_canvas_pos - a, b - a) / len_squared, 0.0f, 1.0f);
		const ImVec2 projection = a + (b - a) * t;
		ImVec2 dist_vec = on_canvas_pos - projection;

		return dot(dist_vec, dist_vec) < 100;
	}


	void compile() override
	{
		// compiled in StateMachine::compile
	}


	void onGUI() override
	{
		ImGui::InputText("Condition",
			expression.data,
			lengthOf(expression.data),
			ImGuiInputTextFlags_CallbackCompletion,
			autocompleteCallback,
			&getController());
	}


	bool isNode() const override { return false; }


	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override
	{
		u32 color = ImGui::ColorConvertFloat4ToU32(
			selected ? ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
		drawEdge(draw, m_parent->getEntryNode(), m_to, color, canvas_screen_pos);;
		if (ImGui::IsMouseClicked(0) && hitTest(ImGui::GetMousePos() - canvas_screen_pos))
		{
			return true;
		}
		return false;
	}

	Node* getTo() const { return m_to; }

	StaticString<128> expression;

private:
	StateMachine* m_parent;
	Node* m_to;
};


Blend1DNode::RootNode::RootNode(Container* parent, ControllerResource& controller)
	: Node(nullptr, parent, controller)
	, edges(controller.getAllocator())
{
	name = "Root";
}


EntryNode::EntryNode(Container* parent, ControllerResource& controller)
	: Node(nullptr, parent, controller)
	, entries(controller.getAllocator())
{
	name = "Entry";
}


StateMachine::StateMachine(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller)
	: Container(engine_cmp, parent, controller)
	, m_drag_source(nullptr)
	, m_context_cmp(nullptr)
{
	m_entry_node = LUMIX_NEW(controller.getAllocator(), EntryNode)(this, controller);
	m_editor_cmps.push(m_entry_node);
}


void StateMachine::removeEntry(EntryEdge& entry)
{
	auto* sm = (Anim::StateMachine*)engine_cmp;
	for(int i = 0; i < sm->entries.size(); ++i)
	{
		if (sm->entries[i].node == entry.getTo()->engine_cmp)
		{
			sm->entries.erase(i);
			m_entry_node->entries.eraseItemFast(&entry);
			break;
		}
	}
}


void StateMachine::onGUI()
{
	Container::onGUI();
	if (ImGui::Button("Show Children"))
	{
		m_controller.getEditor().setContainer(this);
	}
}


void StateMachine::compile()
{
	Container::compile();
	int i = 0;
	for (auto* entry : m_entry_node->entries)
	{
		auto* sm = (Anim::StateMachine*)engine_cmp;
		sm->entries[i].condition.compile(entry->expression, m_controller.getEngineResource()->m_input_decl);
		++i;
	}
}


void Container::deserialize(InputBlob& blob)
{
	Node::deserialize(blob);
	int size;
	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		int uid;
		blob.read(uid);
		if (uid >= 0)
		{
			auto* engine_sm = (Anim::StateMachine*)engine_cmp;
			Component* cmp = createComponent(engine_sm->getChildByUID(uid), this, m_controller);
			cmp->deserialize(blob);
			m_editor_cmps.push(cmp);
		}
	}
}


Component* Container::getByUID(int uid)
{
	if (uid == engine_cmp->uid) return this;
	for (Component* cmp : m_editor_cmps)
	{
		Component* x = cmp->getByUID(uid);
		if (x) return x;
	}
	return nullptr;
}


void Container::compile()
{
	Node::compile();
	for (auto* cmp : m_editor_cmps)
	{
		cmp->compile();
	}
}


void Container::serialize(OutputBlob& blob)
{
	Node::serialize(blob);
	blob.write(m_editor_cmps.size());
	for (auto* cmp : m_editor_cmps)
	{
		blob.write(cmp->engine_cmp ? cmp->engine_cmp->uid : -1);
		if(cmp->engine_cmp) cmp->serialize(blob);
	}
}


void StateMachine::createNode(Anim::Component::Type type, int uid, const ImVec2& pos)
{
	auto* cmp = (Node*)createComponent(Anim::createComponent(type, m_allocator), this, m_controller);
	cmp->pos = pos;
	cmp->size.x = 100;
	cmp->size.y = 30;
	cmp->engine_cmp->uid = uid;
	m_editor_cmps.push(cmp);
	((Anim::StateMachine*)engine_cmp)->children.push(cmp->engine_cmp);
	m_selected_component = cmp;
}


void StateMachine::deserialize(InputBlob& blob)
{
	Container::deserialize(blob);
	m_entry_node->deserialize(blob);
	int count;
	blob.read(count);
	for (int i = 0; i < count; ++i)
	{
		int uid;
		blob.read(uid);
		Node* node = (Node*)getChildByUID(uid);
		auto* edge = LUMIX_NEW(m_allocator, EntryEdge)(this, node, m_controller);
		m_editor_cmps.push(edge);
		blob.read(edge->expression);
	}
}


void StateMachine::serialize(OutputBlob& blob)
{
	Container::serialize(blob);
	m_entry_node->serialize(blob);
	blob.write(m_entry_node->entries.size());
	for (EntryEdge* edge : m_entry_node->entries)
	{
		blob.write(edge->getTo()->engine_cmp->uid);
		blob.write(edge->expression);
	}
}


void StateMachine::debugInside(ImDrawList* draw,
	const ImVec2& canvas_screen_pos,
	Anim::ComponentInstance* runtime,
	Container* current)
{
	if (runtime->source.type != Anim::Component::STATE_MACHINE) return;
	
	auto* child_runtime = ((Anim::StateMachineInstance*)runtime)->current;
	if (!child_runtime) return;
	auto* child = getChildByUID(child_runtime->source.uid);
	if (child)
	{
		if(current == this)
			child->debug(draw, canvas_screen_pos, child_runtime);
		else
			child->debugInside(draw, canvas_screen_pos, child_runtime, current);
	}
}


void StateMachine::debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime)
{
	if (runtime->source.type != engine_cmp->type) return;

	ImVec2 p = canvas_screen_pos + pos;
	p = p + ImVec2(size.x * 0.5f - 3, ImGui::GetTextLineHeightWithSpacing() * 1.5f);
	draw->AddRectFilled(p, p + ImVec2(6, 6), 0xfff00FFF);
}


EntryEdge* StateMachine::createEntryEdge(Node* node)
{
	auto* edge = LUMIX_NEW(m_allocator, EntryEdge)(this, node, m_controller);
	m_editor_cmps.push(edge);

	auto* engine_sm = (Anim::StateMachine*)engine_cmp;
	auto& entry = engine_sm->entries.emplace(engine_sm->allocator);
	entry.node = (Anim::Node*)node->engine_cmp;
	return edge;
}


void StateMachine::drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos)
{
	if (ImGui::IsWindowHovered())
	{
		if (ImGui::IsMouseClicked(0)) m_selected_component = nullptr;
		if (ImGui::IsMouseReleased(1) && m_mouse_status == NONE)
		{
			m_context_cmp = nullptr;
			ImGui::OpenPopup("context_menu");
		}
	}

	for (int i = 0; i < m_editor_cmps.size(); ++i)
	{
		Component* cmp = m_editor_cmps[i];
		if (cmp->draw(draw, canvas_screen_pos, m_selected_component == cmp))
		{
			m_selected_component = cmp;
		}

		if (cmp->isNode() && ImGui::IsItemHovered())
		{
			if (ImGui::IsMouseClicked(0))
			{
				m_drag_source = (Node*)cmp;
				m_mouse_status = DOWN_LEFT;
			}
			if (ImGui::IsMouseClicked(1))
			{
				m_drag_source = (Node*)cmp;
				m_mouse_status = DOWN_RIGHT;
			}
		}

		if (m_mouse_status == DOWN_RIGHT && ImGui::IsMouseDragging(1)) m_mouse_status = NEW_EDGE;
		if (m_mouse_status == DOWN_LEFT && ImGui::IsMouseDragging(0)) m_mouse_status = DRAG_NODE;
	}

	if (m_mouse_status == DRAG_NODE && !m_drag_source) m_mouse_status = NONE;

	auto& editor = m_controller.getEditor();
	if (ImGui::IsMouseReleased(1))
	{
		Component* hit_cmp = childrenHitTest(ImGui::GetMousePos() - canvas_screen_pos);
		if (hit_cmp)
		{
			if (m_mouse_status == NEW_EDGE)
			{
				if (hit_cmp != m_drag_source && hit_cmp->isNode())
				{
					if (hit_cmp == m_entry_node)
					{
						createEntryEdge(m_drag_source);
					}
					else if (m_drag_source == m_entry_node)
					{
						createEntryEdge((Node*)hit_cmp);
					}
					else
					{
						editor.createEdge(m_controller, this, m_drag_source, (Node*)hit_cmp);
					}
				}
			}
			else
			{
				m_context_cmp = hit_cmp;
				m_selected_component = hit_cmp;
				ImGui::OpenPopup("context_menu");
			}
		}
	}

	if (m_mouse_status == DRAG_NODE)
	{
		ImVec2 new_pos = m_drag_source->pos + ImGui::GetIO().MouseDelta;
		editor.moveNode(m_controller, m_drag_source, new_pos);
	}

	if (ImGui::IsMouseReleased(0) || ImGui::IsMouseReleased(1)) m_mouse_status = NONE;

	if (m_mouse_status == NEW_EDGE)
	{
		draw->AddLine(canvas_screen_pos + m_drag_source->pos + m_drag_source->size * 0.5f, ImGui::GetMousePos(), 0xfff00FFF);
	}

	if (ImGui::BeginPopup("context_menu"))
	{
		ImVec2 pos_on_canvas = ImGui::GetMousePos() - canvas_screen_pos;
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Simple")) editor.createNode(m_controller, this, Anim::Component::SIMPLE_ANIMATION, pos_on_canvas);
			if (ImGui::MenuItem("State machine")) editor.createNode(m_controller, this, Anim::Component::STATE_MACHINE, pos_on_canvas);
			if (ImGui::MenuItem("Blend 1D")) editor.createNode(m_controller, this, Anim::Component::BLEND1D, pos_on_canvas);
			ImGui::EndMenu();
		}
		if (m_context_cmp && m_context_cmp != m_entry_node)
		{
			if (ImGui::MenuItem("Remove"))
			{
				LUMIX_DELETE(m_controller.getAllocator(), m_context_cmp);
				if (m_selected_component == m_context_cmp) m_selected_component = nullptr;
				if (m_drag_source == m_context_cmp) m_drag_source = nullptr;
				m_context_cmp = nullptr;
			}
		}
		ImGui::EndPopup();
	}
}


void StateMachine::dropSlot(const char* name, u32 slot, const ImVec2& canvas_screen_pos)
{
	createNode(Anim::Component::SIMPLE_ANIMATION, m_controller.createUID(), ImGui::GetMousePos() - canvas_screen_pos);
	auto* node = (AnimationNode*)m_selected_component;
	node->name = name;
	auto* engine_node = (Anim::AnimationNode*)node->engine_cmp;
	engine_node->animations_hashes.emplace(slot);
}


ControllerResource::ControllerResource(IAnimationEditor& editor,
	ResourceManagerBase& manager,
	IAllocator& allocator)
	: m_animation_slots(allocator)
	, m_allocator(allocator)
	, m_editor(editor)
{
	m_engine_resource = LUMIX_NEW(allocator, Anim::ControllerResource)(Path("editor"), manager, allocator);
	auto* engine_root = LUMIX_NEW(allocator, Anim::StateMachine)(allocator);
	m_engine_resource->m_root = engine_root;
	m_root = LUMIX_NEW(allocator, StateMachine)(engine_root, nullptr, *this);
}


ControllerResource::~ControllerResource()
{
	LUMIX_DELETE(m_allocator, m_engine_resource);
	LUMIX_DELETE(m_allocator, m_root);
}


void ControllerResource::serialize(OutputBlob& blob)
{
	m_root->compile();

	m_engine_resource->serialize(blob);

	blob.write(m_last_uid);
	m_root->serialize(blob);
	blob.write(m_animation_slots.size());
	for (auto& slot : m_animation_slots)
	{
		blob.writeString(slot.c_str());
	}
}


bool ControllerResource::deserialize(InputBlob& blob, Engine& engine, IAllocator& allocator)
{
	LUMIX_DELETE(m_allocator, m_engine_resource);
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
	auto* manager = engine.getResourceManager().get(CONTROLLER_RESOURCE_TYPE);
	m_engine_resource =
		LUMIX_NEW(allocator, Anim::ControllerResource)(Path("editor"), *manager, allocator);
	m_engine_resource->create();
	if (!m_engine_resource->deserialize(blob)) return false;

	blob.read(m_last_uid);
	m_root = createComponent(m_engine_resource->m_root, nullptr, *this);
	m_root->deserialize(blob);

	int count;
	blob.read(count);
	m_animation_slots.clear();
	for (int i = 0; i < count; ++i)
	{
		auto& slot = m_animation_slots.emplace(allocator);
		char tmp[64];
		blob.readString(tmp, lengthOf(tmp));
		slot = tmp;
	}

	return true;
}


const char* ControllerResource::getAnimationSlot(u32 slot_hash) const
{
	for (auto& slot : m_animation_slots)
	{
		if (crc32(slot.c_str()) == slot_hash) return slot.c_str();
	}
	return "";
}


void ControllerResource::createAnimSlot(const char* name, const char* path)
{
	m_animation_slots.emplace(m_allocator) = name;
	auto* manager = m_engine_resource->getResourceManager().getOwner().get(ANIMATION_TYPE);
	auto* anim = (Animation*)manager->load(Path(path));
	m_engine_resource->addAnimation(0, crc32(name), anim);
}



Component* ControllerResource::getByUID(int uid)
{
	if (m_root->engine_cmp->uid == uid) return m_root;
	return m_root->getByUID(uid);
}


} // namespace AnimEditor


} // namespace Lumix