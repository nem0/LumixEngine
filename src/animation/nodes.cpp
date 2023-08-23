#include "animation.h"
#include "controller.h"
#include "engine/log.h"
#include "nodes.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "engine/crt.h"
#include "engine/stack_array.h"

namespace Lumix::anim {

static constexpr u32 OUTPUT_FLAG = 1 << 31;

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotionEx(const Animation* anim, Time t0, Time t1) {
	ASSERT(t0 <= t1);
	LocalRigidTransform old_tr = anim->getRootMotion(t0).inverted();
	LocalRigidTransform new_tr = anim->getRootMotion(t1);
	return old_tr * new_tr;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotion(const RuntimeContext& ctx, const Animation* anim, Time t0_abs, Time t1_abs) {
	const Time t0 = t0_abs % anim->getLength();
	const Time t1 = t1_abs % anim->getLength();
	
	if (t0 <= t1) return getRootMotionEx(anim, t0, t1);
	
	const LocalRigidTransform tr_0 = getRootMotionEx(anim, t0, anim->getLength());
	const LocalRigidTransform tr_1 = getRootMotionEx(anim, Time(0), t1);
	
	return tr_0 * tr_1;
}

RuntimeContext::RuntimeContext(Controller& controller, IAllocator& allocator)
	: data(allocator)
	, inputs(allocator)
	, controller(controller)
	, animations(allocator)
	, events(allocator)
	, input_runtime(nullptr, 0)
{
}

void RuntimeContext::setInput(u32 input_idx, float value) {
	ASSERT(controller.m_inputs[input_idx].type == Controller::Input::FLOAT);
	inputs[input_idx].f = value;
}

void RuntimeContext::setInput(u32 input_idx, bool value) {
	ASSERT(controller.m_inputs[input_idx].type == Controller::Input::BOOL);
	inputs[input_idx].b = value;
}

static float getInputValue(const RuntimeContext& ctx, u32 idx) {
	const Controller::Input& input = ctx.controller.m_inputs[idx];
	ASSERT(input.type == Controller::Input::FLOAT);
	return ctx.inputs[idx].f;
}

struct Blend2DActiveTrio {
	const Blend2DNode::Child* a;
	const Blend2DNode::Child* b;
	const Blend2DNode::Child* c;
	float ta, tb, tc;
};

bool getBarycentric(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, Vec2& uv) {
  const Vec2 ab = b - a, ac = c - a, ap = p - a;

  float d00 = dot(ab, ab);
  float d01 = dot(ab, ac);
  float d11 = dot(ac, ac);
  float d20 = dot(ap, ab);
  float d21 = dot(ap, ac);
  float denom = d00 * d11 - d01 * d01;

  uv.x = (d11 * d20 - d01 * d21) / denom;
  uv.y = (d00 * d21 - d01 * d20) / denom;  
  return uv.x >= 0.f && uv.y >= 0.f && uv.x + uv.y <= 1.f;
}

static Blend2DActiveTrio getActiveTrio(const Blend2DNode& node, Vec2 input_val) {
	const Blend2DNode::Child* children = node.m_children.begin();
	Vec2 uv;
	for (const Blend2DNode::Triangle& t : node.m_triangles) {
		if (!getBarycentric(input_val, children[t.a].value, children[t.b].value, children[t.c].value, uv)) continue;
		
		Blend2DActiveTrio res;
		res.a = &node.m_children[t.a];
		res.b = &node.m_children[t.b];
		res.c = &node.m_children[t.c];
		res.ta = 1 - uv.x - uv.y;
		res.tb = uv.x;
		res.tc = uv.y;
		return res;
	}

	Blend2DActiveTrio res;
	res.a = node.m_children.begin();
	res.b = node.m_children.begin();
	res.c = node.m_children.begin();
	res.ta = 1;
	res.tb = res.tc = 0;
	return res;
}

Blend2DNode::Blend2DNode(Node* parent, Controller& controller, IAllocator& allocator)
	: Node(parent, controller, allocator) 
	, m_children(allocator)
	, m_triangles(allocator)
	, m_name("blend2d", allocator)
{}

bool Blend2DNode::onGUI() {
	outputSlot();
	ImGui::TextUnformatted(m_name.c_str());
	return false;
}

bool Blend2DNode::propertiesGUI() {
	ImGuiEx::Label("Name");
	bool res = inputString("##name", &m_name);
	res = editInput("X input", &m_x_input_index, m_controller) || res;
	res = editInput("Y input", &m_y_input_index, m_controller) || res;
	
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
			res = anim::inputSlot(m_controller, "##anim", &child.slot) || res;

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
					ImGui::Text("%s = %f", m_controller.m_inputs[m_x_input_index].name.data, ch.value.x);
					ImGui::Text("%s = %f", m_controller.m_inputs[m_y_input_index].name.data, ch.value.y);
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

	return res;
}

void Blend2DNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	float relt = ctx.input_runtime.read<float>();
	const float relt0 = relt;
	
	if (m_children.size() > 2) {
		Vec2 input_val;
		input_val.x = getInputValue(ctx, m_x_input_index);
		input_val.y = getInputValue(ctx, m_y_input_index);
		const Blend2DActiveTrio trio = getActiveTrio(*this, input_val);
		const Animation* anim_a = ctx.animations[trio.a->slot];
		const Animation* anim_b = ctx.animations[trio.b->slot];
		const Animation* anim_c = ctx.animations[trio.c->slot];
		if (!anim_a || !anim_b || !anim_c || !anim_a->isReady() || !anim_b->isReady() || !anim_c->isReady()) {
			ctx.data.write(relt);
			return;
		}
	
		const Time wlen = anim_a->getLength() * trio.ta + anim_b->getLength() * trio.tb + anim_c->getLength() * trio.tc;
		relt += ctx.time_delta / wlen;
		relt = fmodf(relt, 1);
		
		{
			const Time len = anim_a->getLength();
			const Time t0 = len * relt0;
			const Time t = len * relt;
			root_motion = getRootMotion(ctx, anim_a, t0, t);
		}
	
		if (trio.tb > 0) {
			const Time len = anim_b->getLength();
			const Time t0 = len * relt0;
			const Time t = len * relt;
			const LocalRigidTransform tr1 = getRootMotion(ctx, anim_b, t0, t);
			root_motion = root_motion.interpolate(tr1, trio.tb / (trio.ta + trio.tb));
		}
	
		if (trio.tc > 0) {
			const Time len = anim_c->getLength();
			const Time t0 = len * relt0;
			const Time t = len * relt;
			const LocalRigidTransform tr1 = getRootMotion(ctx, anim_c, t0, t);
			root_motion = root_motion.interpolate(tr1, trio.tc);
		}
	}

	ctx.data.write(relt);
}

Time Blend2DNode::length(const RuntimeContext& ctx) const {
	if (m_children.size() < 3) return Time(1);

	Vec2 input_val;
	input_val.x = getInputValue(ctx, m_x_input_index);
	input_val.y = getInputValue(ctx, m_y_input_index);
	const Blend2DActiveTrio trio = getActiveTrio(*this, input_val);

	Animation* anim_a = ctx.animations[trio.a->slot];
	Animation* anim_b = ctx.animations[trio.b->slot];
	Animation* anim_c = ctx.animations[trio.c->slot];
	if (!anim_a || !anim_a->isReady()) return Time::fromSeconds(1);
	if (!anim_b || !anim_b->isReady()) return Time::fromSeconds(1);
	if (!anim_c || !anim_c->isReady()) return Time::fromSeconds(1);
	
	return anim_a->getLength() * trio.ta + anim_b->getLength() * trio.tb + anim_c->getLength() * trio.tc;
}

void Blend2DNode::enter(RuntimeContext& ctx) {
	const float t = 0.f;
	ctx.data.write(t);
}

void Blend2DNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(float));
}

static void getPose(const RuntimeContext& ctx, float rel_time, float weight, u32 slot, Pose& pose, u32 mask_idx, bool looped) {
	Animation* anim = ctx.animations[slot];
	if (!anim) return;
	if (!ctx.model->isReady()) return;
	if (!anim->isReady()) return;

	Time time = anim->getLength() * rel_time;
	const Time anim_time = looped ? time % anim->getLength() : minimum(time, anim->getLength());

	const BoneMask* mask = mask_idx < (u32)ctx.controller.m_bone_masks.size() ? &ctx.controller.m_bone_masks[mask_idx] : nullptr;

	Animation::SampleContext sample_ctx;
	sample_ctx.pose = &pose;
	sample_ctx.time = anim_time;
	sample_ctx.model = ctx.model;
	sample_ctx.weight = weight;
	sample_ctx.mask = mask;
	anim->setRootMotionBone(ctx.root_bone_hash);
	anim->getRelativePose(sample_ctx);
}

static void getPose(const RuntimeContext& ctx, Time time, float weight, u32 slot, Pose& pose, u32 mask_idx, bool looped) {
	Animation* anim = ctx.animations[slot];
	if (!anim) return;
	if (!ctx.model->isReady()) return;
	if (!anim->isReady()) return;

	const Time anim_time = looped ? time % anim->getLength() : minimum(time, anim->getLength());

	Animation::SampleContext sample_ctx;
	sample_ctx.pose = &pose;
	sample_ctx.time = anim_time;
	sample_ctx.model = ctx.model;
	sample_ctx.weight = weight;
	sample_ctx.mask = mask_idx < (u32)ctx.controller.m_bone_masks.size() ? &ctx.controller.m_bone_masks[mask_idx] : nullptr;
	anim->setRootMotionBone(ctx.root_bone_hash);
	anim->getRelativePose(sample_ctx);
}

void Blend2DNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const {
	const float t = ctx.input_runtime.read<float>();

	if (m_children.empty()) return;
	if (m_children.size() < 3) {
		anim::getPose(ctx, t, weight, m_children[0].slot, pose, mask, true);
		return;
	}

	Vec2 input_val;
	input_val.x = getInputValue(ctx, m_x_input_index);
	input_val.y = getInputValue(ctx, m_y_input_index);
	const Blend2DActiveTrio trio = getActiveTrio(*this, input_val);
	
	anim::getPose(ctx, t, weight, trio.a->slot, pose, mask, true);
	if (trio.tb > 0) anim::getPose(ctx, t, weight * trio.tb, trio.b->slot, pose, mask, true);
	if (trio.tc > 0) anim::getPose(ctx, t, weight * trio.tc, trio.c->slot, pose, mask, true);
}

static Vec2 computeCircumcircleCenter(Vec2 a, Vec2 b, Vec2 c) {
	Vec2 dab = b - a;
	Vec2 dac = c - a;
	Vec2 o = (dac * squaredLength(dab) - dab * squaredLength(dac)).ortho() / ((dab.x * dac.y - dab.y * dac.x) * 2.f);
	return o + a;
}

// delaunay triangulation
void Blend2DNode::dataChanged(IAllocator& allocator) {
	m_triangles.clear();
	if (m_children.size() < 3) return;

	struct Edge {
		u32 a, b;
		bool valid = true;
		bool operator ==(const Edge& rhs) {
			return a == rhs.a && b == rhs.b || a == rhs.b && b == rhs.a;
		}
	};

	StackArray<Edge, 8> edges(allocator);

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

Time Blend2DNode::time(const RuntimeContext& ctx) const {
	return length(ctx) * ctx.input_runtime.getAs<float>();
}

void Blend2DNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_name);
	stream.write(m_x_input_index);
	stream.write(m_y_input_index);
	stream.writeArray(m_children);
}

void Blend2DNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_name);
	stream.read(m_x_input_index);
	stream.read(m_y_input_index);
	stream.readArray(&m_children);
	dataChanged(ctrl.m_allocator);
}

Blend1DNode::Blend1DNode(Node* parent, Controller& controller, IAllocator& allocator)
	: Node(parent, controller, allocator) 
	, m_children(allocator)
	, m_name("blend1d", allocator)
{}

struct Blend1DActivePair {
	const Blend1DNode::Child* a;
	const Blend1DNode::Child* b;
	float t;
};

static Blend1DActivePair getActivePair(const Blend1DNode& node, float input_val) {
	const auto& children = node.m_children;
	if (input_val > children[0].value) {
		if (input_val >= children.back().value) {
			return { &children.back(), nullptr, 0 };
		}
		else {
			for (u32 i = 1, c = children.size(); i < c; ++i) {
				if (input_val < children[i].value) {
					const float w = (input_val - children[i - 1].value) / (children[i].value - children[i - 1].value);
					return { &children[i - 1], &children[i], w };
				}
			}
		}
	}
	return { &children[0], nullptr, 0 };
}

bool Blend1DNode::propertiesGUI() {
	ImGuiEx::Label("Name");
	bool res = inputString("##name", &m_name);
	const Controller::Input& current_input = m_controller.m_inputs[m_input_index];
	ImGuiEx::Label("Input");
	if (ImGui::BeginCombo("##input", current_input.name)) {
		bool selected = false;
		for (const Controller::Input& input : m_controller.m_inputs) {
			if (ImGui::Selectable(input.name.empty() ? "##tmp" : input.name)) {
				selected = true;
				m_input_index = u32(&input - m_controller.m_inputs.begin());
			}
		}
		ImGui::EndCombo();
		res = true;
	}

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
			res = anim::inputSlot(m_controller, "##anim", &child.slot) || res;

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
	outputSlot();
	ImGuiEx::TextUnformatted(m_name.c_str());
	return false;
}

void Blend1DNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	float relt = ctx.input_runtime.read<float>();
	const float relt0 = relt;
	
	const float input_val = getInputValue(ctx, m_input_index);
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	const Animation* anim_a = pair.a ? ctx.animations[pair.a->slot] : nullptr;
	const Animation* anim_b = pair.b ? ctx.animations[pair.b->slot] : nullptr;
	const Time wlen = anim_a ? lerp(anim_a->getLength(), anim_b ? anim_b->getLength() : anim_a->getLength(), pair.t) : Time::fromSeconds(1);
	relt += ctx.time_delta / wlen;
	relt = fmodf(relt, 1);
	
	if (anim_a) {
		const Time len = anim_a->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		root_motion = getRootMotion(ctx, ctx.animations[pair.a->slot], t0, t);
	}
	else {
		root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	}
	if (anim_b && anim_b->isReady()) {
		const Time len = anim_b->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		const LocalRigidTransform tr1 = getRootMotion(ctx, ctx.animations[pair.b->slot], t0, t);
		root_motion = root_motion.interpolate(tr1, pair.t);
	}

	ctx.data.write(relt);
}

Time Blend1DNode::length(const RuntimeContext& ctx) const {
	const float input_val = getInputValue(ctx, m_input_index);
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	Animation* anim_a = ctx.animations[pair.a->slot];
	if (!anim_a) return Time::fromSeconds(1);
	if (!anim_a->isReady()) return Time::fromSeconds(1);
	
	Animation* anim_b = pair.b ? ctx.animations[pair.b->slot] : nullptr;
	if (!anim_b) return anim_a->getLength();
	if (!anim_b->isReady()) return anim_a->getLength();

	return lerp(anim_a->getLength(), anim_b->getLength(), pair.t);
}

Time Blend1DNode::time(const RuntimeContext& ctx) const {
	return length(ctx) * ctx.input_runtime.getAs<float>();
}

void Blend1DNode::enter(RuntimeContext& ctx) {
	const float t = 0.f;
	ctx.data.write(t);
}

void Blend1DNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(float));
}

void Blend1DNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const {
	const float t = ctx.input_runtime.read<float>();

	if (m_children.empty()) return;
	if (m_children.size() == 1) {
		anim::getPose(ctx, t, weight, m_children[0].slot, pose, mask, true);
		return;
	}

	const float input_val = getInputValue(ctx, m_input_index);
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	
	anim::getPose(ctx, t, weight, pair.a->slot, pose, mask, true);
	if (pair.b) {
		anim::getPose(ctx, t, weight * pair.t, pair.b->slot, pose, mask, true);
	}
}

void Blend1DNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_name);
	stream.write(m_input_index);
	stream.write((u32)m_children.size());
	stream.write(m_children.begin(), m_children.byte_size());
}

void Blend1DNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_name);
	stream.read(m_input_index);
	u32 count;
	stream.read(count);
	m_children.resize(count);
	stream.read(m_children.begin(), m_children.byte_size());
}

bool AnimationNode::compile() {
	return m_slot < (u32)m_controller.m_animation_slots.size();
}

bool AnimationNode::propertiesGUI() {
	ImGuiEx::Label("Slot");
	bool res = Lumix::anim::inputSlot(m_controller, "##slot", &m_slot);
	ImGuiEx::Label("Looping");
	bool loop = m_flags && LOOPED;
	if (ImGui::Checkbox("##loop", &loop)) {
		if (loop) m_flags = m_flags | LOOPED;
		else m_flags = m_flags & ~LOOPED;
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
	: Node(parent, controller, allocator) 
{}


void AnimationNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	Time t = ctx.input_runtime.read<Time>();
	Time prev_t = t;
	t += ctx.time_delta;

	Animation* anim = ctx.animations[m_slot];
	if (anim && anim->isReady()) {
		if ((m_flags & LOOPED) == 0) {
			const u32 len = anim->getLength().raw();
			t = Time(minimum(t.raw(), len));
			prev_t = Time(minimum(prev_t.raw(), len));
		}

		root_motion = getRootMotion(ctx, anim, prev_t, t);
	}
	else {
		root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	}
	ctx.data.write(t);
}

Time AnimationNode::length(const RuntimeContext& ctx) const {
	Animation* anim = ctx.animations[m_slot];
	if (!anim) return Time(0);
	return anim->getLength();
}

Time AnimationNode::time(const RuntimeContext& ctx) const {
	return ctx.input_runtime.getAs<Time>();
}

void AnimationNode::enter(RuntimeContext& ctx) {
	Time t = Time(0); 
	ctx.data.write(t);	
}

void AnimationNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(Time));
}
	
void AnimationNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const {
	const Time t = ctx.input_runtime.read<Time>();
	anim::getPose(ctx, t, weight, m_slot, pose, mask, m_flags & LOOPED);
}

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
	: Node(parent, controller, allocator)
	, m_layers(allocator)
	, m_allocator(allocator)
{
}

LayersNode::~LayersNode() {
	for (Layer& l : m_layers) {
		LUMIX_DELETE(m_allocator, l.node);
	}
}

void LayersNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	for (const Layer& layer : m_layers) {
		LocalRigidTransform tmp_rm;
		layer.node->update(ctx, tmp_rm);
		if (&layer == m_layers.begin()) {
			root_motion = tmp_rm;
		}
	}
}

Time LayersNode::length(const RuntimeContext& ctx) const {
	return Time::fromSeconds(1);
}

Time LayersNode::time(const RuntimeContext& ctx) const {
	return Time(0);
}

void LayersNode::enter(RuntimeContext& ctx) {
	for (const Layer& layer : m_layers) {
		layer.node->enter(ctx);
	}
}

void LayersNode::skip(RuntimeContext& ctx) const {
	for (const Layer& layer : m_layers) {
		layer.node->skip(ctx);
	}
}

void LayersNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const {
	for (const Layer& layer : m_layers) {
		layer.node->getPose(ctx, weight, pose, layer.mask);
	}
}

void LayersNode::serialize(OutputMemoryStream& stream) const {
	stream.write((u32)m_layers.size());
	for (const Layer& layer : m_layers) {
		stream.writeString(layer.name);
		stream.write(layer.mask);
		stream.write(layer.node->type());
		layer.node->serialize(stream);
	}
}

void LayersNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	u32 c;
	stream.read(c);
	for (u32 i = 0; i < c; ++i) {
		Layer& layer = m_layers.emplace(m_allocator);
		layer.name = stream.readString();
		stream.read(layer.mask);
		Node::Type type;
		stream.read(type);
		layer.node = Node::create(this, type, m_controller, m_allocator);
		layer.node->deserialize(stream, ctrl, version);
	}
}

OutputNode::OutputNode(Node* parent, Controller& controller, IAllocator& allocator)
	: Node(parent, controller, allocator)
{}

bool OutputNode::onGUI() {
	inputSlot();
	ImGuiEx::TextUnformatted(ICON_FA_SIGN_OUT_ALT " Output");
	return false;
}

void OutputNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const { if (input) input->update(ctx, root_motion); }

bool OutputNode::compile() {
	input = getInput(0);
	if (!input) return false;
	return input->compile();
}

void OutputNode::enter(RuntimeContext& ctx) { if (input) input->enter(ctx); }
void OutputNode::skip(RuntimeContext& ctx) const { if (input) input->skip(ctx); }
void OutputNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const { if (input) input->getPose(ctx, weight, pose, mask); }
void OutputNode::serialize(OutputMemoryStream& stream) const { Node::serialize(stream); }
void OutputNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) { Node::deserialize(stream, ctrl, version); }
Time OutputNode::length(const RuntimeContext& ctx) const { return  input ? input->length(ctx) : Time(1); }
Time OutputNode::time(const RuntimeContext& ctx) const { return  input ? input->time(ctx) : Time(0); }


TreeNode::TreeNode(Node* parent, Controller& controller, IAllocator& allocator)
	: Node(parent, controller, allocator)
	, m_name("new tree", allocator)
{
	LUMIX_NEW(m_allocator, OutputNode)(this, controller, m_allocator);
}

void TreeNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const { m_nodes[0]->update(ctx, root_motion); }
void TreeNode::enter(RuntimeContext& ctx) { m_nodes[0]->enter(ctx); }
void TreeNode::skip(RuntimeContext& ctx) const { m_nodes[0]->skip(ctx); }
void TreeNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const { m_nodes[0]->getPose(ctx, weight, pose, mask); }
void TreeNode::serialize(OutputMemoryStream& stream) const { Node::serialize(stream); stream.write(m_name); }
bool TreeNode::compile() { return m_nodes[0]->compile(); }

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

Time TreeNode::length(const RuntimeContext& ctx) const { return  m_nodes[0]->length(ctx); }
Time TreeNode::time(const RuntimeContext& ctx) const { return  m_nodes[0]->time(ctx); }

bool SelectNode::propertiesGUI() { 
	bool res = editInput("Input", &m_input_index, m_controller);
	float node_blend_length = m_blend_length.seconds();
	ImGuiEx::Label("Blend length");
	if (ImGui::DragFloat("##bl", &node_blend_length)) {
		m_blend_length = Time::fromSeconds(node_blend_length);
		res = true;
	}
	return res;
}

bool SelectNode::onGUI() {
	ImGuiEx::NodeTitle("Select");
	outputSlot();
	bool res = false;
	for (u32 i = 0; i < (u32)m_max_values.size(); ++i) {
		float& f = m_max_values[i];
		inputSlot();
		ImGui::PushID(&f);
		if (ImGuiEx::IconButton(ICON_FA_TIMES_CIRCLE, "Remove")) {
			m_max_values.erase(i);
			for (i32 j = m_parent->m_links.size() - 1; j >= 0; --j) {
				NodeEditorLink& link = m_parent->m_links[j];
				if (link.getToNode() == m_id) {
					if (link.getToPin() == i) {
						m_parent->m_links.swapAndPop(j);
					}
					else if (link.getToPin() > i) {
						link.to = link.getToNode() | ((link.getToPin() - 1) << 16);
					}
				}
			}
			--i;
			ImGui::PopID();
			res = true;
			continue;
		}

		ImGui::SameLine();
		res = ImGui::DragFloat("##f", &f) || res;
		ImGui::PopID();
	}
	if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, "Add")) {
		m_max_values.push(m_max_values.back() + 1);
		return true;
	}
	return res;
}

SelectNode::SelectNode(Node* parent, Controller& controller, IAllocator& allocator)
	: Node(parent, controller, allocator)
	, m_max_values(allocator)
	, m_inputs(allocator)
{
	m_max_values.push(0);
	m_max_values.push(1);
}

u32 SelectNode::getChildIndex(float input_val) const {
	ASSERT(m_max_values.size() > 0);
	for (u32 i = 0, c = m_max_values.size(); i < c; ++i) {
		if (input_val <= m_max_values[i]) {
			return i;
		}
	}
	return m_max_values.size() - 1;
}

void SelectNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	if (m_max_values.empty()) return;

	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	
	const float input_val = getInputValue(ctx, m_input_index);
	const u32 child_idx = getChildIndex(input_val);

	if (data.from != data.to) {
		data.t += ctx.time_delta;

		if (m_blend_length < data.t) {
			// TODO root motion in data.from
			m_inputs[data.from]->skip(ctx);
			data.from = data.to;
			data.t = Time(0);
			ctx.data.write(data);
			m_inputs[data.to]->update(ctx, root_motion);
			return;
		}

		ctx.data.write(data);

		m_inputs[data.from]->update(ctx, root_motion);
		LocalRigidTransform tmp;
		m_inputs[data.to]->update(ctx, tmp);
		root_motion = root_motion.interpolate(tmp, data.t.seconds() / m_blend_length.seconds());
		return;
	}

	if (child_idx != data.from) {
		data.to = child_idx;
		data.t = Time(0);
		ctx.data.write(data);
		m_inputs[data.from]->update(ctx, root_motion);
		m_inputs[data.to]->enter(ctx);
		return;
	}

	data.t += ctx.time_delta;
	ctx.data.write(data);
	m_inputs[data.from]->update(ctx, root_motion);
}

bool SelectNode::compile() {
	m_inputs.resize(m_max_values.size());
	for (u32 i = 0; i < (u32)m_inputs.size(); ++i) {
		Node* n = getInput(i);
		if (!n) return false;
		m_inputs[i] = n;
		if (!n->compile()) return false;
	}
	if (m_controller.m_inputs[m_input_index].type != Controller::Input::FLOAT) return false;
	return true;
}

void SelectNode::enter(RuntimeContext& ctx) {
	if (m_inputs.empty()) return;

	RuntimeData runtime_data = { 0, 0, Time(0) };
	const float input_val = getInputValue(ctx, m_input_index);
	runtime_data.from = getChildIndex(input_val);
	runtime_data.to = runtime_data.from;
	ctx.data.write(runtime_data);
	if (runtime_data.from < (u32)m_inputs.size()) {
		m_inputs[runtime_data.from]->enter(ctx);
	}
}

void SelectNode::skip(RuntimeContext& ctx) const {
	if (m_inputs.empty()) return;

	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	m_inputs[data.from]->skip(ctx);
	if (data.from != data.to) {
		m_inputs[data.to]->skip(ctx);
	}
}

void SelectNode::getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const {
	if (m_inputs.empty()) return;

	const RuntimeData data = ctx.input_runtime.read<RuntimeData>();

	m_inputs[data.from]->getPose(ctx, weight, pose, mask);
	if(data.from != data.to) {
		const float t = clamp(data.t.seconds() / m_blend_length.seconds(), 0.f, 1.f);
		m_inputs[data.to]->getPose(ctx, weight * t, pose, mask);
	}
}

Time SelectNode::length(const RuntimeContext& ctx) const {	return Time::fromSeconds(1); }

Time SelectNode::time(const RuntimeContext& ctx) const { return Time(0); }

void SelectNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	Node::deserialize(stream, ctrl, version);
	stream.read(m_blend_length);
	stream.read(m_input_index);
	stream.readArray(&m_max_values);
}

void SelectNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_blend_length);
	stream.write(m_input_index);
	stream.writeArray(m_max_values);
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
		Node::Type type;
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
		case Node::ANIMATION: return LUMIX_NEW(allocator, AnimationNode)(parent, controller, allocator);
		case Node::BLEND1D: return LUMIX_NEW(allocator, Blend1DNode)(parent, controller, allocator);
		case Node::BLEND2D: return LUMIX_NEW(allocator, Blend2DNode)(parent, controller, allocator);
		case Node::LAYERS: return LUMIX_NEW(allocator, LayersNode)(parent, controller, allocator);
		case Node::SELECT: return LUMIX_NEW(allocator, SelectNode)(parent, controller, allocator);
		case Node::TREE: return LUMIX_NEW(allocator, TreeNode)(parent, controller, allocator);
		case Node::OUTPUT: return LUMIX_NEW(allocator, OutputNode)(parent, controller, allocator);
		case Node::NONE: ASSERT(false); return nullptr;
	}
	ASSERT(false);
	return nullptr;
}

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

bool inputSlot(const Controller& controller, const char* str_id, u32* slot) {
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

} // namespace Lumix::anim
