#include "particle_editor.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/associative_array.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/string.h"
#include "engine/world.h"
#include "renderer/material.h"
#include "renderer/particle_system.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include <imgui/imgui.h>

namespace Lumix {

namespace {

static constexpr u32 OUTPUT_FLAG = 1 << 31;
using DataStream = ParticleSystemResource::DataStream;
using InstructionType = ParticleSystemResource::InstructionType;

enum class Version {
	LINK_ID_REMOVED,
	EMIT_RATE,
	MULTIEMITTER,
	EMIT_NODE,
	WORLD_SPACE,

	LAST
};

enum class ValueType : i32 {
	FLOAT,
	VEC3,
	VEC4
};

struct Node;

struct GenerateContext {
	enum Context {
		INIT,
		UPDATE,
		OUTPUT
	};

	GenerateContext(OutputMemoryStream& instructions, Context context) 
		: ip(instructions)
		, context(context)
	{}
	
	void write(const void* data, u64 size) { ip.write(data, size); }
	template <typename T> void write(const T& value) { ip.write(value); }

	void freeRegister(DataStream v) {
		if (v.type != DataStream::REGISTER) return;
		m_register_mask &= ~(1 << v.index);
	}

	DataStream streamOrRegister(DataStream v) {
		if (v.type == DataStream::NONE) { 
			DataStream r;
			r.type = DataStream::REGISTER;
			r.index = 0xff;
			for (u32 i = 0; i < sizeof(m_register_mask) * 8; ++i) {
				if ((m_register_mask & (1 << i)) == 0) {
					r.index = i;
					break;
				}
			}
			ASSERT(r.index != 0xFF);
			m_register_mask |= 1 << r.index;
			m_registers_count = maximum(m_registers_count, r.index + 1);
			return r;
		}

		return v;
	}

	OutputMemoryStream& ip;
	Context context;
	u16 m_register_mask = 0;
	u8 m_registers_count = 0;
};

struct NodeInput {
	Node* node;
	u16 output_idx;
	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) const;
};

struct Node : NodeEditorNode {
	// this is serialized, do not change order
	enum Type {
		OUTPUT,
		STREAM,
		MUL,
		ADD,
		CONST,
		NUMBER,
		INIT,
		UPDATE,
		RANDOM,
		SIN,
		MADD,
		CMP,
		COLOR_MIX,
		CURVE,
		GRADIENT_COLOR,
		VEC3,
		DIV,
		PIN,
		COS,
		SWITCH,
		VEC4,
		SPLINE,
		MESH,
		MOD,
		NOISE,
		SUB,
		CACHE,
		EMIT_INPUT,
		EMIT,
		CHANNEL_MASK
	};

	Node(struct ParticleEmitterEditorResource& res);
	virtual ~Node() {}

	virtual Type getType() const = 0;
	virtual DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) = 0;
	virtual void beforeGenerate() {};
	virtual void serialize(OutputMemoryStream& blob) const {}
	virtual void deserialize(InputMemoryStream& blob) {}

	NodeInput getInput(u8 input_idx);

	void inputSlot(ImGuiEx::PinShape shape = ImGuiEx::PinShape::CIRCLE) {
		ImGuiEx::Pin(m_id | (u32(m_input_counter) << 16), true, shape);
		++m_input_counter;
	}

	void outputSlot(ImGuiEx::PinShape shape = ImGuiEx::PinShape::CIRCLE) {
		ImGuiEx::Pin(m_id | (u32(m_output_counter) << 16) | OUTPUT_FLAG, false, shape);
		++m_output_counter;
	}

	void clearError() {
		m_error = "";
	}

	DataStream error(const char* msg) {
		m_error = msg;
		DataStream res;
		res.type = DataStream::ERROR;
		return res;
	}

	bool nodeGUI() override {
		m_input_counter = 0;
		m_output_counter = 0;
		const ImVec2 old_pos = m_pos;
		ImGuiEx::BeginNode(m_id, m_pos, &m_selected);
		bool res = onGUI();
		if (m_error.length() > 0) {
			ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0xff, 0, 0, 0xff));
		}
		ImGuiEx::EndNode();
		if (m_error.length() > 0) {
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_error.c_str());
		}
		return res || old_pos.x != m_pos.x || old_pos.y != m_pos.y;
	}

	bool m_selected = false;
	
protected:
	virtual bool onGUI() = 0;
		
	const ParticleEmitterEditorResource& m_resource;
	u8 m_input_counter;
	u8 m_output_counter;
	String m_error;
};

static u32 getCount(ValueType type) {
	switch(type) {
		case ValueType::VEC3: return 3;
		case ValueType::VEC4: return 4;
		case ValueType::FLOAT: return 1;
	}
	ASSERT(false);
	return 1;
}

struct ParticleEmitterEditorResource {
	struct EmitInput {
		StaticString<32> name;
		ValueType type = ValueType::FLOAT;
	};

	struct Stream {
		StaticString<32> name;
		ValueType type = ValueType::FLOAT;
	};

	struct Output {
		StaticString<32> name;
		ValueType type = ValueType::FLOAT;
	};

	ParticleEmitterEditorResource(struct ParticleSystemEditorResource& system, IAllocator& allocator)
		: m_allocator(allocator)
		, m_system(system)
		, m_nodes(allocator)
		, m_links(allocator)
		, m_streams(allocator)
		, m_outputs(allocator)
		, m_update(allocator)
		, m_emit(allocator)
		, m_output(allocator)
		, m_emit_inputs(allocator)
		, m_name("default", allocator)
	{}
	
	~ParticleEmitterEditorResource() {
		for (Node* node : m_nodes) LUMIX_DELETE(m_allocator, node);
	}

	i32 findChannel(const char* name) const {
		for (i32 i = 0; i < m_streams.size(); ++i) {
			if (m_streams[i].name == name) return i;
		}
		return -1;
	}

	u32 getEmitInputIndex(u8 emit_input, u8 subindex) const {
		u32 c = 0;
		for (u8 i = 0; i < emit_input; ++i) {
			c += getCount(m_emit_inputs[i].type);
		}
		switch (m_emit_inputs[emit_input].type) {
			case ValueType::FLOAT: return c;
			case ValueType::VEC3: return c + clamp(subindex, 0, 3);
			case ValueType::VEC4: return c + clamp(subindex, 0, 4);
		}
		ASSERT(false);
		return c;
	}

	u32 getChannelIndex(u8 stream, u8 subindex) const {
		u32 c = 0;
		for (u8 i = 0; i < stream; ++i) {
			c += getCount(m_streams[i].type);
		}
		switch (m_streams[stream].type) {
			case ValueType::FLOAT: return c;
			case ValueType::VEC3: return c + clamp(subindex, 0, 3);
			case ValueType::VEC4: return c + clamp(subindex, 0, 4);
		}
		ASSERT(false);
		return c;
	}
	
	void colorLinks(ImU32 color, u32 link_idx) {
		m_links[link_idx].color = color;
		const u16 from_node_id = m_links[link_idx].getFromNode();
		for (u32 i = 0, c = m_links.size(); i < c; ++i) {
			if (m_links[i].getToNode() == from_node_id) colorLinks(color, i);
		}
	}
	
	const Node* getNode(u16 id) const {
		for(const auto& n : m_nodes) {
			if (n->m_id == id) return n;
		}
		return nullptr;
	}

	void colorLinks() {
		const ImU32 colors[] = {
			IM_COL32(0x20, 0x20, 0xA0, 255),
			IM_COL32(0x20, 0xA0, 0x20, 255),
			IM_COL32(0x20, 0xA0, 0xA0, 255),
			IM_COL32(0xA0, 0x20, 0x20, 255),
			IM_COL32(0xA0, 0x20, 0xA0, 255),
			IM_COL32(0xA0, 0xA0, 0x20, 255),
			IM_COL32(0xA0, 0xA0, 0xA0, 255),
		};
	
		for (NodeEditorLink& l : m_links) {
			l.color = IM_COL32(0xA0, 0xA0, 0xA0, 0xFF);
		}

		for (u32 i = 0, c = m_links.size(); i < c; ++i) {
			const NodeEditorLink& link = m_links[i];
			const Node* node = getNode(link.getToNode());
			switch(node->getType()) {
				case Node::UPDATE:
				case Node::INIT:
				case Node::OUTPUT:
					colorLinks(colors[link.getToPin() % lengthOf(colors)], i);
					break;
				default: break;
			}

		}		
	}

	u16 genID() { return ++m_last_id; }

	Node* getNodeByID(u16 id) const {
		for (Node* node : m_nodes) {
			if (node->m_id == id) return node;
		}
		return nullptr;
	}

	Node* addNode(Node::Type type);
	
	bool deserialize(InputMemoryStream& blob, const char* path, Version version) {
		blob.read(m_last_id);
		if (version > Version::MULTIEMITTER) m_name = blob.readString();
		m_mat_path = blob.readString();

		if (version > Version::EMIT_RATE) {
			blob.read(m_init_emit_count);
			blob.read(m_emit_per_second);
		}

		i32 count;

		blob.read(count);
		m_streams.resize(count);
		blob.read(m_streams.begin(), m_streams.byte_size());

		blob.read(count);
		m_outputs.resize(count);
		blob.read(m_outputs.begin(), m_outputs.byte_size());

		if (version > Version::EMIT_NODE) {
			blob.read(count);
			m_emit_inputs.resize(count);
			blob.read(m_emit_inputs.begin(), m_emit_inputs.byte_size());
		}

		if (version <= Version::MULTIEMITTER) {
			blob.read(count);
			blob.skip(count * 36);
		}

		blob.read(count);
		m_links.resize(count);
		if (version > Version::LINK_ID_REMOVED) {
			for (i32 i = 0; i < count; ++i) {
				blob.read(m_links[i].from);
				blob.read(m_links[i].to);
			}
		}
		else {
			for (i32 i = 0; i < count; ++i) {
				blob.read<i32>();
				blob.read(m_links[i].from);
				blob.read(m_links[i].to);
			}
		}

		blob.read(count);
		for (i32 i = 0; i < count; ++i) {
			Node::Type type;
			blob.read(type);
			Node* n = addNode(type);
			blob.read(n->m_id);
			blob.read(n->m_pos);
			n->deserialize(blob);
		}
		colorLinks();
		return true;
	}

	void serialize(OutputMemoryStream& blob) {
		blob.write(m_last_id);
		blob.writeString(m_name.c_str());
		blob.writeString(m_mat_path.data);
		blob.write(m_init_emit_count);
		blob.write(m_emit_per_second);
		
		blob.write((i32)m_streams.size());
		blob.write(m_streams.begin(), m_streams.byte_size());

		blob.write((i32)m_outputs.size());
		blob.write(m_outputs.begin(), m_outputs.byte_size());

		blob.write((i32)m_emit_inputs.size());
		blob.write(m_emit_inputs.begin(), m_emit_inputs.byte_size());

		blob.write((i32)m_links.size());
		for (const NodeEditorLink& link : m_links) {
			blob.write(link.from);
			blob.write(link.to);
		}

		blob.write((i32)m_nodes.size());
		for (const Node* n : m_nodes) {
			blob.write(n->getType());
			blob.write(n->m_id);
			blob.write(n->m_pos);
			n->serialize(blob);
		}
	}

	void initDefault() {
		m_streams.emplace().name = "pos";
		m_streams.back().type = ValueType::VEC3;
		m_streams.emplace().name = "life";

		m_outputs.emplace().name = "pos";
		m_outputs.back().type = ValueType::VEC3;
		m_outputs.emplace().name = "scale";
		m_outputs.emplace().name = "color";
		m_outputs.back().type = ValueType::VEC4;
		m_outputs.emplace().name = "rotation";
		m_outputs.emplace().name = "frame";

		addNode(Node::Type::UPDATE);
		addNode(Node::Type::OUTPUT)->m_pos = ImVec2(100, 300);
		addNode(Node::Type::INIT)->m_pos = ImVec2(100, 200);
	}

	bool generate() {
		m_update.clear();
		m_output.clear();
		m_emit.clear();

		for (Node* n : m_nodes) {
			n->clearError();
			n->beforeGenerate();
		}

		m_registers_count = 0;

		bool emit_fail = false;
		for (Node* n : m_nodes) {
			if (n->getType() == Node::Type::EMIT) {
				GenerateContext ctx(m_update, GenerateContext::UPDATE);
				if (n->generate(ctx, {}, 0).isError()) {
					emit_fail = true;
				}
				m_registers_count = maximum(ctx.m_registers_count, m_registers_count);
			}
		}

		bool success = !emit_fail;
		{
			GenerateContext ctx(m_update, GenerateContext::UPDATE);
			if (emit_fail || m_nodes[0]->generate(ctx, {}, 0).isError()) {
				m_update.clear();
				success = false;
			}
			else {
				m_update.write(InstructionType::END);
				m_registers_count = maximum(ctx.m_registers_count, m_registers_count);
			}
		}

		{
			GenerateContext ctx(m_output, GenerateContext::OUTPUT);
			if (m_nodes[1]->generate(ctx, {}, 0).isError()) {
				m_output.clear();
				success = false;
			}
			else {
				m_output.write(InstructionType::END);
				m_registers_count = maximum(ctx.m_registers_count, m_registers_count);
			}
		}

		{
			GenerateContext ctx(m_emit, GenerateContext::INIT);
			if (m_nodes[2]->generate(ctx, {}, 0).isError()) {
				m_emit.clear();
				success = false;
			}
			else {
				m_emit.write(InstructionType::END);
				m_registers_count = maximum(ctx.m_registers_count, m_registers_count);
			}
		}

		return success;
	}
	
	void fillVertexDecl(gpu::VertexDecl& decl, Array<String>* attribute_names, IAllocator& allocator) const {
		u32 idx = 0;
		u32 offset = 0;
		for (const ParticleEmitterEditorResource::Output& o : m_outputs) {
			switch(o.type) {
				case ValueType::FLOAT: {
					if (attribute_names) attribute_names->emplace(o.name, allocator);
					decl.addAttribute(idx, offset, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(float);
					break;
				}
				case ValueType::VEC3: {
					if (attribute_names) attribute_names->emplace(o.name, allocator);
					decl.addAttribute(idx, offset, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec3);
					break;
				}
				case ValueType::VEC4: {
					if (attribute_names) attribute_names->emplace(o.name, allocator);
					decl.addAttribute(idx, offset, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec4);
					break;
				}
			}
			++idx;
		}
	}

	ParticleSystemEditorResource& m_system;
	IAllocator& m_allocator;
	String m_name;
	StaticString<LUMIX_MAX_PATH> m_mat_path;
	Array<EmitInput> m_emit_inputs;
	Array<Stream> m_streams;
	Array<Output> m_outputs;
	Array<Node*> m_nodes;
	u32 m_registers_count;
	Array<NodeEditorLink> m_links;
	OutputMemoryStream m_update;
	OutputMemoryStream m_emit;
	OutputMemoryStream m_output;
	int m_last_id = 0;
	u32 m_init_emit_count = 0;
	float m_emit_per_second = 100;
};

Node::Node(struct ParticleEmitterEditorResource& res) 
	: m_resource(res)
	, m_error(res.m_allocator)
{
	m_id = res.genID();
}

NodeInput Node::getInput(u8 input_idx) {
	for (const NodeEditorLink& link : m_resource.m_links) {
		if (link.getToNode() != m_id) continue;
		if (link.getToPin() != input_idx) continue;

		NodeInput res;
		res.output_idx = link.getFromPin();
		res.node = m_resource.getNodeByID(link.getFromNode());
		return res;
	}

	return {};
}

struct ParticleSystemEditorResource {
	struct Header {
		static constexpr u32 MAGIC = '_LPE';
		const u32 magic = MAGIC;
		Version version = Version::LAST;
	};
	
	ParticleSystemEditorResource(IAllocator& allocator) 
		: m_emitters(allocator)
		, m_allocator(allocator)
	{
		addEmitter();
	}

	void serialize(OutputMemoryStream& blob) {
		Header header;
		blob.write(header);
		blob.write(m_world_space);
		blob.write(m_emitters.size());
		for (const UniquePtr<ParticleEmitterEditorResource>& e : m_emitters) {
			e->serialize(blob);
		}
	}

	bool deserialize(InputMemoryStream& blob, const char* path) {
		Header header;
		blob.read(header);
		if (header.magic != Header::MAGIC) {
			logError("Invalid file ", path);
			return false;
		}
		if (header.version > Version::LAST) {
			logError("Unsupported file version ", path);
			return false;
		}

		if (header.version > Version::WORLD_SPACE) blob.read(m_world_space);

		m_emitters.clear();
		u32 count = 1;
		if (header.version > Version::MULTIEMITTER) count = blob.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			UniquePtr<ParticleEmitterEditorResource> emitter = UniquePtr<ParticleEmitterEditorResource>::create(m_allocator, *this, m_allocator);
			if (!emitter->deserialize(blob, path, header.version)) return false;
			m_emitters.push(emitter.move());
		}

		for (UniquePtr<ParticleEmitterEditorResource>& emitter : m_emitters) {
			if (!emitter->generate()) return false;
		}
		return true;
	}

	void removeEmitter(ParticleEmitterEditorResource* to_remove) {
		m_emitters.eraseItems([&](UniquePtr<ParticleEmitterEditorResource>& e){ return e.get() == to_remove; });
	}

	ParticleEmitterEditorResource* addEmitter() {
		UniquePtr<ParticleEmitterEditorResource> emitter = UniquePtr<ParticleEmitterEditorResource>::create(m_allocator, *this, m_allocator);
		ParticleEmitterEditorResource* res = emitter.get();
		u32 iter = 0;
		for (;;) {
			i32 idx = m_emitters.find([&](UniquePtr<ParticleEmitterEditorResource>& e){ return e->m_name == emitter->m_name; });
			if (idx < 0) break;

			++iter;
			emitter->m_name = "default";
			emitter->m_name.cat(iter);
		}
		m_emitters.push(emitter.move());
		return res;
	}

	Array<UniquePtr<ParticleEmitterEditorResource>> m_emitters;
	IAllocator& m_allocator;
	bool m_world_space = false;
};

template <Node::Type T>
struct UnaryFunctionNode : Node {
	UnaryFunctionNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return T; }

	void serialize(OutputMemoryStream& blob) const override {}
	void deserialize(InputMemoryStream& blob) override {}
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
		
	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		NodeInput input = getInput(0);
		if (!input.node) return error("Invalid input");
			
		DataStream dst = ctx.streamOrRegister(output);
		DataStream op0 = input.generate(ctx, {}, subindex);
		if (op0.isError()) return op0;
		switch (T) {
			case COS: ctx.write(InstructionType::COS); break;
			case SIN: ctx.write(InstructionType::SIN); break;
			default: ASSERT(false); break;
		}
		ctx.write(dst);
		ctx.write(op0);

		ctx.freeRegister(op0);

		return dst;
	}

	bool onGUI() override {
		inputSlot();
		switch (T) {
			case COS: ImGui::TextUnformatted("cos"); break;
			case SIN: ImGui::TextUnformatted("sin"); break;
			default: ASSERT(false); break;
		}
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};
	
struct MeshNode : Node {
	MeshNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::MESH; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
		
	void beforeGenerate() override {
		index = DataStream();
	}

	DataStream generate(GenerateContext& ctx, DataStream dst, u8 subindex) override {
		if (index.type == DataStream::NONE) {
			index = ctx.streamOrRegister(index);
			ctx.write(InstructionType::MOV);
			ctx.write(index);
			DataStream init_value;
			init_value.type = DataStream::LITERAL;
			init_value.value = -1;
			ctx.write(init_value);
		}

		dst = ctx.streamOrRegister(dst);
		ctx.write(InstructionType::MESH);
		ctx.write(dst);
		ctx.write(index);
		ctx.write(subindex);
		return dst;
	}

	bool onGUI() override {
		ImGuiEx::NodeTitle("Mesh");
		outputSlot();
		ImGui::TextUnformatted("Position");
		return false;
	}

	DataStream index;
};

struct CacheNode : Node {
	CacheNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::CACHE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
		
	void beforeGenerate() override { m_cached = {}; }

	DataStream generate(GenerateContext& ctx, DataStream dst, u8 subindex) override {
		if (m_cached.type == DataStream::NONE) {
			const NodeInput input = getInput(0);
			if (!input.node) return error("Invalid input");

			DataStream op0;
			op0 = input.generate(ctx, op0, subindex);
			if (op0.isError()) return op0;

			m_cached = ctx.streamOrRegister({});
			ctx.write(InstructionType::MOV);
			ctx.write(m_cached);
			ctx.write(op0);
			ctx.freeRegister(op0);

			ctx.write(InstructionType::MOV);
			dst = ctx.streamOrRegister(dst);
			ctx.write(dst);
			ctx.write(m_cached);
			return dst;
		}

		return m_cached;
	}

	bool onGUI() override {
		ImGuiEx::NodeTitle("Cache");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted(" ");
		return false;
	}

	DataStream m_cached;
};

struct SplineNode : Node {
	SplineNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::SPLINE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
		
	DataStream generate(GenerateContext& ctx, DataStream dst, u8 subindex) override {
		const NodeInput input = getInput(0);
		if (!input.node) return error("Invalid input");

		DataStream op0 = input.generate(ctx, {}, 0);
		if (op0.isError()) return op0;

		dst = ctx.streamOrRegister(dst);
		ctx.write(InstructionType::SPLINE);
		ctx.write(dst);
		ctx.write(op0);
		ctx.write(subindex);
		ctx.freeRegister(op0);
		return dst;
	}

	bool onGUI() override {
		ImGuiEx::NodeTitle("Spline");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted("Position");
		return false;
	}
};

struct GradientColorNode : Node {
	GradientColorNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::GRADIENT_COLOR; }
	
	DataStream generate(GenerateContext& ctx, DataStream dst, u8 subindex) override {
		const NodeInput input = getInput(0);
		if (!input.node) return error("Invalid input");

		DataStream op0 = input.generate(ctx, {}, subindex);
		if (op0.isError()) return op0;

		dst = ctx.streamOrRegister(dst);
		ctx.write(InstructionType::GRADIENT);
		ctx.write(dst);
		ctx.write(op0);
		ctx.write(count);
		ctx.write(keys, sizeof(keys[0]) * count);
		for (u32 i = 0; i < count; ++i) {
			ctx.write(values[i][subindex]);
		}

		ctx.freeRegister(op0);
		return dst;
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(count); blob.write(keys); blob.write(values); }
	void deserialize(InputMemoryStream& blob) override { blob.read(count); blob.read(keys); blob.read(values); }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
		ImGui::BeginGroup();
		inputSlot();
		ImGui::SetNextItemWidth(200);
		bool changed = ImGuiEx::Gradient4("test", lengthOf(keys), (int*)&count, keys, &values[0].x);
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		ASSERT(sentinel == 0xDEADBEAF);
		return changed;
	}

	u32 count = 2;
	float keys[8] = { 0, 1 };
	Vec4 values[8] = { Vec4(0, 0, 0, 1), Vec4(1, 1, 1, 1) };

	u32 sentinel = 0xDEADBEAF;
};

struct CurveNode : Node {
	CurveNode(ParticleEmitterEditorResource& res) : Node(res) {}
	Type getType() const override { return Type::CURVE; }
	
	DataStream generate(GenerateContext& ctx, DataStream dst, u8 subindex) override {
		const NodeInput input = getInput(0);
		if (!input.node) return error("Invalid input");

		DataStream op0 = input.generate(ctx, {}, 0);
		if (op0.isError()) return op0;

		dst = ctx.streamOrRegister(dst);
		ctx.write(InstructionType::GRADIENT);
		ctx.write(dst);
		ctx.write(op0);
		ctx.write(count);
		ctx.write(keys, sizeof(keys[0]) * count);
		ctx.write(values, sizeof(values[0]) * count);

		ctx.freeRegister(op0);
		return dst;
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(count); blob.write(keys); blob.write(values); }
	void deserialize(InputMemoryStream& blob) override { blob.read(count); blob.read(keys); blob.read(values); }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
		ImGuiEx::NodeTitle("Curve");

		inputSlot(); 
		outputSlot();

		int new_count;
		float tmp[16];
		for (u32 i = 0; i < count; ++i) {
			tmp[i * 2] = keys[i];
			tmp[i * 2 + 1] = values[i];
		}

		int flags = (int)ImGuiEx::CurveEditorFlags::NO_TANGENTS;
		if (ImGuiEx::CurveEditor("##curve", tmp, count, lengthOf(tmp) / 2, ImVec2(150, 150), flags, &new_count) >= 0 || new_count != count) {
			for (i32 i = 0; i < new_count; ++i) {
				keys[i] = tmp[i * 2];
				values[i] = tmp[i * 2 + 1];
				count = new_count;
			}
			return true;
		}

		return false;
	}

	u32 count = 2;
	float keys[8] = {0, 1};
	float values[8] = {0, 1};
};

struct ChannelMaskNode : Node {
	ChannelMaskNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::CHANNEL_MASK; }

	void serialize(OutputMemoryStream& blob) const override { blob.write(channel); }
	void deserialize(InputMemoryStream& blob) override { blob.read(channel); }
		
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream dst, u8 subindex) override {
		if (subindex > 0) return error("Invalid subindex");

		const NodeInput input = getInput(0);
		if (!input.node) return error("Invalid input");

		return input.generate(ctx, dst, channel);
	}

	bool onGUI() override {
		ImGuiEx::NodeTitle("Channel mask");
		inputSlot();
		outputSlot(); 
		ImGui::SetNextItemWidth(60);
		return ImGui::Combo("##ch", (int*)&channel, "X\0Y\0Z\0W\0");
	}

	u32 channel = 0;
};

struct EmitInputNode : Node {
	EmitInputNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::EMIT_INPUT; }

	void serialize(OutputMemoryStream& blob) const override { blob.write(idx); }
	void deserialize(InputMemoryStream& blob) override { blob.read(idx); }
		
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (subindex >= getCount(m_resource.m_emit_inputs[idx].type)) return error("Invalid subindex");
		if (ctx.context != GenerateContext::INIT) return error("Invalid context");
		DataStream res;
		res.type = DataStream::REGISTER;
		res.index = m_resource.getEmitInputIndex(idx, subindex);
		return res;
	}

	bool onGUI() override {
		outputSlot(); 
		if (idx >= m_resource.m_emit_inputs.size()) {
			ImGui::TextUnformatted("INVALID INPUT");
			return false;
		}
		ImGui::TextUnformatted(m_resource.m_emit_inputs[idx].name);
		return false;
	}

	u8 idx;
};

struct NoiseNode : Node {
	NoiseNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::NOISE; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		if (subindex > 0) return error("Invalid subindex");

		const NodeInput input0 = getInput(0);
		if (!input0.node) return error("Invalid input");

		DataStream op0 = input0.generate(ctx, {}, subindex);
		if (op0.isError()) return op0;

		ctx.write(InstructionType::NOISE);
		DataStream dst = ctx.streamOrRegister(output);
		ctx.write(dst);
		ctx.write(op0);
		return dst;
	}

	void serialize(OutputMemoryStream& blob) const override {}
	void deserialize(InputMemoryStream& blob) override {}

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
		ImGuiEx::NodeTitle("Noise");

		outputSlot();
		inputSlot();
		
		ImGui::TextUnformatted(" ");
		return false;
	}
};

struct RandomNode : Node {
	RandomNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::RANDOM; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		if (subindex > 0) return error("Invalid subindex");

		ctx.write(InstructionType::RAND);
		DataStream dst;
		dst = ctx.streamOrRegister(output);
		ctx.write(dst);
		ctx.write(from);
		ctx.write(to);
		return dst;
	}

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(from);
		blob.write(to);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(from);
		blob.read(to);
	}

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
		ImGuiEx::NodeTitle(ICON_FA_DICE " Random");

		ImGui::BeginGroup();
		ImGui::PushItemWidth(60);
		bool res = ImGui::DragFloat("##from", &from);
		ImGui::SameLine(); 
		res = ImGui::DragFloat("##to", &to) || res;
		ImGui::PopItemWidth();
		ImGui::EndGroup();

		ImGui::SameLine();

		outputSlot();
		return res;
	}

	float from = 0;
	float to = 1;
};

struct LiteralNode : Node {
	LiteralNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::NUMBER; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (subindex > 0) return error("Invalid subindex");

		DataStream r;
		r.type = DataStream::LITERAL;
		r.value = value;
		return r;
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(value); }
	void deserialize(InputMemoryStream& blob) override { blob.read(value); }

	bool onGUI() override {
		outputSlot();
		ImGui::SetNextItemWidth(60);
		bool changed = ImGui::DragFloat("##v", &value);
		return changed;
	}

	float value = 0;
};

template <Node::Type T>
struct VectorNode : Node {
	VectorNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return T; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		if constexpr (T == Node::Type::VEC3) {
			if (subindex > 2) return error("Invalid subindex");
		}

		const NodeInput input = getInput(subindex);
		if (input.node) {
			return input.generate(ctx, output, 0);
		}

		DataStream r;
		r.type = DataStream::LITERAL;
		r.value = value[subindex];
		return r;
	}

	void serialize(OutputMemoryStream& blob) const override { 
		if constexpr (T == Node::VEC3) blob.write(value.xyz());
		else blob.write(value);
	}

	void deserialize(InputMemoryStream& blob) override { 
		if constexpr (T == Node::VEC3) {
			Vec3 v = blob.read<Vec3>();
			value = Vec4(v, 0);
		}
		else blob.read(value);
	}
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
			
		ImGui::PushItemWidth(60);
			
		bool changed = false;
		ImGui::BeginGroup();
		inputSlot();
		if (getInput(0).node) {
			ImGui::TextUnformatted("X");
		}
		else {
			changed = ImGui::DragFloat("X", &value.x);
		}

		inputSlot();
		if (getInput(1).node) {
			ImGui::TextUnformatted("Y");
		}
		else {
			changed = ImGui::DragFloat("Y", &value.y) || changed;
		}

		inputSlot();
		if (getInput(2).node) {
			ImGui::TextUnformatted("Z");
		}
		else {
			changed = ImGui::DragFloat("Z", &value.z) || changed;
		}

		if constexpr (T == Node::Type::VEC4) {
			inputSlot();
			if (getInput(3).node) {
				ImGui::TextUnformatted("W");
			}
			else {
				changed = ImGui::DragFloat("W", &value.w) || changed;
			}

			changed = ImGui::ColorEdit4("##color", &value.x, ImGuiColorEditFlags_NoInputs) || changed;
		}
		else {
			changed = ImGui::ColorEdit3("##color", &value.x, ImGuiColorEditFlags_NoInputs) || changed;
		}

		ImGui::EndGroup();
			
		ImGui::PopItemWidth();
			
		ImGui::SameLine();
		outputSlot();
			
		return changed;
	}

	Vec4 value = Vec4(0);
};

struct StreamNode : Node {
	StreamNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::STREAM; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (ctx.context == GenerateContext::INIT) return error("Invalid context");

		DataStream r;
		r.type = DataStream::CHANNEL;
		r.index = m_resource.getChannelIndex(idx, subindex);
		return r;
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(idx); }
	void deserialize(InputMemoryStream& blob) override { blob.read(idx); }

	bool onGUI() override {
		outputSlot();
		if (idx < m_resource.m_streams.size()) {
			ImGui::TextUnformatted(m_resource.m_streams[idx].name);
		}
		else {
			ImGui::TextUnformatted(ICON_FA_EXCLAMATION "Deleted stream");
		}
		return false;
	}

	u8 idx;
};

struct InitNode : Node {
	InitNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::INIT; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }

	bool onGUI() override {
		ImGuiEx::NodeTitle(ICON_FA_PLUS " Init", ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
		for (const ParticleEmitterEditorResource::Stream& stream : m_resource.m_streams) {
			inputSlot(); ImGui::TextUnformatted(stream.name);
		}
		return false;
	}

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (ctx.context != GenerateContext::INIT) return error("Invalid context");

		ASSERT(ctx.m_register_mask == 0);
		ASSERT(ctx.m_registers_count == 0);
		for (ParticleEmitterEditorResource::EmitInput& emit_input : m_resource.m_emit_inputs) {
			for (u32 i = 0; i < getCount(emit_input.type); ++i) {
				ctx.m_register_mask |= 1 << ctx.m_registers_count;
				++ctx.m_registers_count;
			}
		} 
		i32 output_idx = 0;
		for (i32 i = 0; i < m_resource.m_streams.size(); ++i) {
			const NodeInput input = getInput(i);
			const u32 si_count = getCount(m_resource.m_streams[i].type);

			for (u32 si = 0; si < si_count; ++si) {
				DataStream s;
				s.type = DataStream::CHANNEL;
				s.index = output_idx;
				if (input.node) {
					DataStream o = input.generate(ctx, s, si);
					if (o.isError()) return o;
					if (o.type != s.type || o.index != s.index) {
						ctx.write(InstructionType::MOV);
						ctx.write(s);
						ctx.write(o);
					}
				}
				else {
					ctx.write(InstructionType::MOV);
					ctx.write(s);
					DataStream l;
					l.type = DataStream::LITERAL;
					l.value = 0;
					ctx.write(l);
				}
				++output_idx;
			}
		}
		return {};
	}
};

struct UpdateNode : Node {
	UpdateNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::UPDATE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }

	bool onGUI() override {
		ImGuiEx::NodeTitle(ICON_FA_CLOCK " Update", ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));

		inputSlot(ImGuiEx::PinShape::TRIANGLE); ImGui::TextUnformatted("Kill");

		for (const ParticleEmitterEditorResource::Stream& stream : m_resource.m_streams) {
			inputSlot(); ImGui::TextUnformatted(stream.name);
		}
		return false;
	}

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (ctx.context != GenerateContext::UPDATE) return error("Invalid context");

		const NodeInput kill_input = getInput(0);
		if (kill_input.node) {
			DataStream res = kill_input.generate(ctx, {}, 0);
			if (res.isError()) return res;
			ctx.write(InstructionType::KILL);
		}

		i32 out_index = 0 ;
		for (i32 i = 0; i < m_resource.m_streams.size(); ++i) {
			const NodeInput input = getInput(i + 1);
			const u32 si_count = getCount(m_resource.m_streams[i].type);
			if (!input.node) {
				out_index += si_count;
				continue;
			}

			for (u32 si = 0; si < si_count; ++si) {
				DataStream s;
				s.type = DataStream::CHANNEL;
				s.index = out_index;
				DataStream o = input.generate(ctx, s, si);
				if (o.isError()) return o;
				if (o.type != s.type || o.index != s.index) {
					ctx.write(InstructionType::MOV);
					ctx.write(s);
					ctx.write(o);
				}
				++out_index;
			}
		}
		return {};
	}
};

struct CompareNode : Node {
	CompareNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::CMP; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
		ImGui::BeginGroup();
		inputSlot(); ImGui::NewLine();

		ImGui::SetNextItemWidth(45);
		bool changed = ImGui::Combo("##op", (int*)&op, "<\0>\0");
		inputSlot();

		if (getInput(1).node) {
			ImGui::NewLine();
		}
		else {
			ImGui::SetNextItemWidth(60);
			changed = ImGui::DragFloat("##b", &value) || changed;
		}
		ImGui::EndGroup();

		ImGui::SameLine();
		outputSlot(ImGuiEx::PinShape::TRIANGLE); 
		return changed;
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(op); blob.write(value); }
	void deserialize(InputMemoryStream& blob) override { blob.read(op); blob.read(value); }

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		const NodeInput input0 = getInput(0);
		const NodeInput input1 = getInput(1);
		if (!input0.node) return error("Invalid input");

		DataStream i0 = input0.generate(ctx, {}, subindex);
		if (i0.isError()) return i0;

		DataStream i1 = input1.node ? input1.generate(ctx, {}, subindex) : DataStream{};
		if (i1.isError()) return i1;

		switch (op) {
			case LT: ctx.write(InstructionType::LT); break;
			case GT: ctx.write(InstructionType::GT); break;
		}
			
		ctx.write(i0);
		if (input1.node) {
			ctx.write(i1);
		}
		else {
			DataStream op0;
			op0.type = DataStream::LITERAL;
			op0.value = value;
			ctx.write(op0);
		}

		return {};
	}

	enum Op : int {
		LT,
		GT
	};

	Op op = LT;
	float value = 0;
};

struct SwitchNode : Node {
	SwitchNode(ParticleEmitterEditorResource& res) : Node(res) {}
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	Type getType() const override { return Type::SWITCH; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		const NodeInput input = getInput(m_is_on ? 0 : 1);
		if (!input.node) return error("Invalid input");
		return input.generate(ctx, output, subindex);
	}

	bool onGUI() override {
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("True");
		inputSlot(); ImGui::TextUnformatted("False");
		ImGui::EndGroup();
		ImGui::SameLine();
		bool res = ImGui::Checkbox("##on", &m_is_on);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	bool m_is_on = true;
};

struct PinNode : Node {
	PinNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		const NodeInput input = getInput(0);
		if (!input.node) {
			ASSERT(false);
			return error("Invalid input");
		}
		return input.generate(ctx, output, subindex);
	}
		
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	Type getType() const override { return Type::PIN; }
		
	bool onGUI() override {
		inputSlot();
		ImGui::TextUnformatted(" ");
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct OutputNode : Node {
	OutputNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::OUTPUT; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }

	bool onGUI() override {
		ImGuiEx::NodeTitle(ICON_FA_EYE " Output", ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
		for (const ParticleEmitterEditorResource::Output& stream : m_resource.m_outputs) {
			inputSlot(); ImGui::TextUnformatted(stream.name);
		}
		return false;
	}

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (ctx.context != GenerateContext::OUTPUT) return error("Invalid context");

		u32 output_idx = 0;
		for (i32 i = 0; i < m_resource.m_outputs.size(); ++i) {
			const NodeInput input = getInput(i);
			const u32 si_count = getCount(m_resource.m_outputs[i].type);
			if (!input.node) {
				const i32 ch_idx = m_resource.findChannel(m_resource.m_outputs[i].name);
				if (ch_idx < 0) {
					output_idx += si_count;
					continue;
				}
				else {
					for (u32 si = 0; si < si_count; ++si) {
						DataStream s;
						s.type = DataStream::OUT;
						s.index = output_idx;
						DataStream o;
						o.type = DataStream::CHANNEL;
						o.index = m_resource.getChannelIndex(ch_idx, si);
						ctx.write(InstructionType::MOV);
						ctx.write(s);
						ctx.write(o);
						++output_idx;
					}
					continue;
				}
			}

			for (u32 si = 0; si < si_count; ++si) {
				DataStream s;
				s.type = DataStream::OUT;
				s.index = output_idx;
				DataStream o = input.generate(ctx, s, si);
				if (o.isError()) return o;
				if (o.type != s.type || o.index != s.index) {
					ctx.write(InstructionType::MOV);
					ctx.write(s);
					ctx.write(o);
				}
				++output_idx;
			}
		}
		return {};
	}
};

struct ColorMixNode : Node {
	ColorMixNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::COLOR_MIX; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		const NodeInput input = getInput(0);
		if (!input.node) return error("Invalid input");

		const DataStream w = input.generate(ctx, {}, subindex);
		if (w.isError()) return w;

		ctx.write(InstructionType::MIX);
		DataStream dst, op0, op1;
		dst = ctx.streamOrRegister(DataStream());
		op0.type = DataStream::LITERAL;
		op0.value = *(&color0.x + subindex);
		op1.type = DataStream::LITERAL;
		op1.value = *(&color1.x + subindex);
		ctx.write(dst);
		ctx.write(op0);
		ctx.write(op1);
		ctx.write(w);
		return dst;
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(color0); blob.write(color1); }
	void deserialize(InputMemoryStream& blob) override { blob.read(color0); blob.read(color1); }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool onGUI() override {
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("Weight");
		bool changed = ImGui::ColorEdit4("A", &color0.x, ImGuiColorEditFlags_NoInputs);
		changed = ImGui::ColorEdit4("B", &color1.x, ImGuiColorEditFlags_NoInputs) || changed;
		ImGui::EndGroup();
			
		ImGui::SameLine();
		outputSlot();
			
		return changed;
	}

	Vec4 color0 = Vec4(1);
	Vec4 color1 = Vec4(1);
};

struct MaddNode : Node {
	MaddNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { return Type::MADD; }

	void serialize(OutputMemoryStream& blob) const override { blob.write(value1); blob.write(value2); }
	void deserialize(InputMemoryStream& blob) override { blob.read(value1); blob.read(value2); }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		const NodeInput input0 = getInput(0);
		if (!input0.node) return error("Invalid input");
		const NodeInput input1 = getInput(1);
		const NodeInput input2 = getInput(2);

		DataStream dst, op0, op1, op2;
		op0 = input0.generate(ctx, op0, subindex);
		if (op0.isError()) return op0;
		if (input1.node) {
			op1 = input1.generate(ctx, op1, subindex);
			if (op1.isError()) return op1;
		}
		else {
			op1.type = DataStream::LITERAL;
			op1.value = value1;
		}

		if (input2.node) {
			op2 = input2.generate(ctx, op2, subindex);
			if (op2.isError()) return op2;
		}
		else {
			op2.type = DataStream::LITERAL;
			op2.value = value2;
		}

		ctx.write(InstructionType::MULTIPLY_ADD);
		dst = ctx.streamOrRegister(output);

		ctx.write(dst);
		ctx.write(op0);
		ctx.write(op1);
		ctx.write(op2);
			
		ctx.freeRegister(op0);
		ctx.freeRegister(op1);
		ctx.freeRegister(op2);
		return dst;
	}

	bool onGUI() override {
		bool changed = false;
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("A");
			
		ImGui::TextUnformatted("X");
			
		inputSlot();
		if (getInput(1).node) {
			ImGui::NewLine();
		}
		else {
			ImGui::SetNextItemWidth(60);
			changed = ImGui::DragFloat("B", &value1) || changed;
		}

		ImGui::TextUnformatted(ICON_FA_PLUS);

		inputSlot();
		if (getInput(2).node) {
			ImGui::NewLine();
		}
		else {
			ImGui::SetNextItemWidth(60);
			changed = ImGui::DragFloat("C", &value2) || changed;
		}
		ImGui::EndGroup();

		ImGui::SameLine();
		outputSlot();

		return changed;
	}

	float value1 = 0;
	float value2 = 0;
};

template <InstructionType OP_TYPE>
struct BinaryOpNode : Node {
	BinaryOpNode(ParticleEmitterEditorResource& res) : Node(res) {}
		
	Type getType() const override { 
		switch(OP_TYPE) {
			case InstructionType::DIV: return Type::DIV;
			case InstructionType::MUL: return Type::MUL;
			case InstructionType::ADD: return Type::ADD;
			case InstructionType::SUB: return Type::SUB;
			case InstructionType::MOD: return Type::MOD;
			default: ASSERT(false); return Type::MUL;
		}
	}

	void serialize(OutputMemoryStream& blob) const override { blob.write(value); }
	void deserialize(InputMemoryStream& blob) override { blob.read(value); }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream output, u8 subindex) override {
		const NodeInput input0 = getInput(0);
		if (!input0.node) return error("Invalid input");
		const NodeInput input1 = getInput(1);

		DataStream dst, op0, op1;
		op0 = input0.generate(ctx, op0, subindex);
		if (op0.isError()) return op0;

		if (input1.node) {
			op1 = input1.generate(ctx, op1, subindex);
			if (op1.isError()) return op0;
		}
		else {
			op1.type = DataStream::LITERAL;
			op1.value = value;
		}

		ctx.write(OP_TYPE);
		dst = ctx.streamOrRegister(output);

		ctx.write(dst);
		ctx.write(op0);
		ctx.write(op1);

		ctx.freeRegister(op0);
		ctx.freeRegister(op1);
		return dst;
	}

	static const char* getName() {
		switch(OP_TYPE) {
			case InstructionType::DIV: return "Divide";
			case InstructionType::SUB: return "Subtract";
			case InstructionType::MUL: return "Multiply";
			case InstructionType::ADD: return "Add";
			case InstructionType::MOD: return "Modulo";
			default: ASSERT(false); return "Error";
		}
	}

	bool onGUI() override {
		ImGuiEx::NodeTitle(getName());

		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("A");

		bool changed = false;
		inputSlot();
		if (getInput(1).node) {
			ImGui::TextUnformatted("B");
		}
		else {
			ImGui::SetNextItemWidth(60);
			changed = ImGui::DragFloat("##b", &value);
		}
		ImGui::EndGroup();

		ImGui::SameLine();
		outputSlot();

		return changed;
	}

	float value = 0;
};

struct ConstNode : Node {
	enum class Const : u8 {
		TIME_DELTA,
		TOTAL_TIME,
		EMIT_INDEX
	};

	ConstNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::CONST; }

	void serialize(OutputMemoryStream& blob) const override { blob.write(constant); }
	void deserialize(InputMemoryStream& blob) override { blob.read(constant); }
		
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (subindex > 0) return error("Invalid subindex");
		if (ctx.context != GenerateContext::INIT && constant == Const::EMIT_INDEX) return error("Invalid context");
		
		DataStream r;
		r.type = DataStream::CONST;
		r.index = (u8)constant;
		return r;
	}

	bool onGUI() override {
		outputSlot(); 
		switch (constant) {
			case Const::TIME_DELTA:
				ImGui::TextUnformatted("Time delta");
				break;
			case Const::TOTAL_TIME:
				ImGui::TextUnformatted("Total time");
				break;
			case Const::EMIT_INDEX:
				ImGui::TextUnformatted("Emit index");
				break;
			default:
				ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " INVALID CONSTANT");
				break;
		}
		return false;
	}

	Const constant;
};

struct EmitNode : Node {
	EmitNode(ParticleEmitterEditorResource& res) : Node(res) {}

	Type getType() const override { return Type::EMIT; }

	void serialize(OutputMemoryStream& blob) const override { blob.write(emitter_idx); }
	void deserialize(InputMemoryStream& blob) override { blob.read(emitter_idx); }
		
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }

	DataStream generate(GenerateContext& ctx, DataStream, u8 subindex) override {
		if (ctx.context != GenerateContext::UPDATE) return error("Invalid context");

		const NodeInput cond_input = getInput(0);
		if (!cond_input.node) return {};
			
		DataStream res = cond_input.generate(ctx, {}, 0);
		if (res.isError()) return res;

		ctx.write(InstructionType::EMIT);
		ctx.write(emitter_idx);
		
		ParticleEmitterEditorResource& emitter = *m_resource.m_system.m_emitters[emitter_idx].get();
		u32 out_idx = 0;
		for (i32 i = 0, c = emitter.m_emit_inputs.size(); i < c; ++i) {
			NodeInput input = getInput(i + 1);
			if (!input.node) return error("Invalid input");

			for (u8 subindex = 0; subindex < getCount(emitter.m_emit_inputs[i].type); ++subindex) {
				DataStream dst;
				dst.type = DataStream::OUT;
				dst.index = out_idx;
				++out_idx;
				DataStream o = input.generate(ctx, dst, subindex);
				if (o.isError()) return o;
				if (o.type != dst.type || o.index != dst.index) {
					ctx.write(InstructionType::MOV);
					ctx.write(dst);
					ctx.write(o);
				}
			}
		}
		ctx.write(InstructionType::END);
		return {};
	}

	bool onGUI() override {
		ParticleEmitterEditorResource& emitter = *m_resource.m_system.m_emitters[emitter_idx].get();

		ImGuiEx::BeginNodeTitleBar(ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
		ImGui::Text("Emit %s", emitter.m_name.c_str());
		ImGuiEx::EndNodeTitleBar();

		inputSlot(ImGuiEx::PinShape::TRIANGLE);
		ImGui::TextUnformatted("Condition");

		for (const ParticleEmitterEditorResource::EmitInput& input : emitter.m_emit_inputs) {
			inputSlot();
			ImGui::TextUnformatted(input.name);
		}
		return false;
	}

	// TODO update emitter_idx when emitter is deleted
	u32 emitter_idx;
};

Node* ParticleEmitterEditorResource::addNode(Node::Type type) {
		Node* node;
		switch(type) {
			case Node::CMP: node = LUMIX_NEW(m_allocator, CompareNode)(*this); break;
			case Node::MESH: node = LUMIX_NEW(m_allocator, MeshNode)(*this); break;
			case Node::SPLINE: node = LUMIX_NEW(m_allocator, SplineNode)(*this); break;
			case Node::NOISE: node = LUMIX_NEW(m_allocator, NoiseNode)(*this); break;
			case Node::CACHE: node = LUMIX_NEW(m_allocator, CacheNode)(*this); break;
			case Node::EMIT_INPUT: node = LUMIX_NEW(m_allocator, EmitInputNode)(*this); break;
			case Node::EMIT: node = LUMIX_NEW(m_allocator, EmitNode)(*this); break;
			case Node::GRADIENT_COLOR: node = LUMIX_NEW(m_allocator, GradientColorNode)(*this); break;
			case Node::CHANNEL_MASK: node = LUMIX_NEW(m_allocator, ChannelMaskNode)(*this); break;
			case Node::CURVE: node = LUMIX_NEW(m_allocator, CurveNode)(*this); break;
			case Node::VEC3: node = LUMIX_NEW(m_allocator, VectorNode<Node::VEC3>)(*this); break;
			case Node::VEC4: node = LUMIX_NEW(m_allocator, VectorNode<Node::VEC4>)(*this); break;
			case Node::COLOR_MIX: node = LUMIX_NEW(m_allocator, ColorMixNode)(*this); break;
			case Node::MADD: node = LUMIX_NEW(m_allocator, MaddNode)(*this); break;
			case Node::SWITCH: node = LUMIX_NEW(m_allocator, SwitchNode)(*this); break;
			case Node::RANDOM: node = LUMIX_NEW(m_allocator, RandomNode)(*this); break;
			case Node::INIT: node = LUMIX_NEW(m_allocator, InitNode)(*this); break;
			case Node::UPDATE: node = LUMIX_NEW(m_allocator, UpdateNode)(*this); break;
			case Node::STREAM: node = LUMIX_NEW(m_allocator, StreamNode)(*this); break;
			case Node::OUTPUT: node = LUMIX_NEW(m_allocator, OutputNode)(*this); break;
			case Node::PIN: node = LUMIX_NEW(m_allocator, PinNode)(*this); break;
			case Node::DIV: node = LUMIX_NEW(m_allocator, BinaryOpNode<InstructionType::DIV>)(*this); break;
			case Node::MOD: node = LUMIX_NEW(m_allocator, BinaryOpNode<InstructionType::MOD>)(*this); break;
			case Node::MUL: node = LUMIX_NEW(m_allocator, BinaryOpNode<InstructionType::MUL>)(*this); break;
			case Node::ADD: node = LUMIX_NEW(m_allocator, BinaryOpNode<InstructionType::ADD>)(*this); break;
			case Node::SUB: node = LUMIX_NEW(m_allocator, BinaryOpNode<InstructionType::SUB>)(*this); break;
			case Node::CONST: node = LUMIX_NEW(m_allocator, ConstNode)(*this); break;
			case Node::COS: node = LUMIX_NEW(m_allocator, UnaryFunctionNode<Node::COS>)(*this); break;
			case Node::SIN: node = LUMIX_NEW(m_allocator, UnaryFunctionNode<Node::SIN>)(*this); break;
			case Node::NUMBER: node = LUMIX_NEW(m_allocator, LiteralNode)(*this); break;
		}
		m_nodes.push(node);
		return node;
	}

} // anonymous namespace

struct ParticleEditorImpl : ParticleEditor, NodeEditor {
	ParticleEditorImpl(StudioApp& app, IAllocator& allocator)
		: m_allocator(allocator)
		, m_app(app)
		, NodeEditor(allocator)
		, m_recent_paths("particle_editor_recent_", 10, app)
	{
		m_toggle_ui.init("Particle editor", "Toggle particle editor", "particle_editor", "", true);
		m_toggle_ui.func.bind<&ParticleEditorImpl::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ParticleEditorImpl::isOpen>(this);

		m_save_action.init(ICON_FA_SAVE "Save", "Particle editor save", "particle_editor_save", ICON_FA_SAVE, os::Keycode::S, Action::Modifiers::CTRL, true);
		m_save_action.func.bind<&ParticleEditorImpl::save>(this);
		m_save_action.plugin = this;

		m_undo_action.init(ICON_FA_UNDO "Undo", "Particle editor undo", "particle_editor_undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL, true);
		m_undo_action.func.bind<&ParticleEditorImpl::undo>((SimpleUndoRedo*)this);
		m_undo_action.plugin = this;

		m_redo_action.init(ICON_FA_REDO "Redo", "Particle editor redo", "particle_editor_redo", ICON_FA_REDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, true);
		m_redo_action.func.bind<&ParticleEditorImpl::redo>((SimpleUndoRedo*)this);
		m_redo_action.plugin = this;

		m_apply_action.init("Apply", "Particle editor apply", "particle_editor_apply", "", os::Keycode::E, Action::Modifiers::CTRL, true);
		m_apply_action.func.bind<&ParticleEditorImpl::apply>(this);
		m_apply_action.plugin = this;

		m_delete_action.init(ICON_FA_TRASH "Delete", "Particle editor delete", "particle_editor_delete", ICON_FA_TRASH, os::Keycode::DEL, Action::Modifiers::NONE, true);
		m_delete_action.func.bind<&ParticleEditorImpl::deleteSelectedNodes>(this);
		m_delete_action.plugin = this;

		app.addWindowAction(&m_toggle_ui);
		app.addAction(&m_save_action);
		app.addAction(&m_undo_action);
		app.addAction(&m_redo_action);
		app.addAction(&m_apply_action);
		app.addAction(&m_delete_action);
		newGraph();
	}

	~ParticleEditorImpl() {
		m_app.removeAction(&m_toggle_ui);
		m_app.removeAction(&m_save_action);
		m_app.removeAction(&m_undo_action);
		m_app.removeAction(&m_redo_action);
		m_app.removeAction(&m_delete_action);
		m_app.removeAction(&m_apply_action);
	}

	struct ICategoryVisitor {
		struct INodeCreator {
			virtual Node* create(ParticleEmitterEditorResource&) const = 0;
		};

		virtual bool beginCategory(const char* category) { return true; }
		virtual void endCategory() {}
		virtual ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut = 0) = 0;

		ICategoryVisitor& visitType(Node::Type type, const char* label, char shortcut = 0) {
			struct : INodeCreator {
				Node* create(ParticleEmitterEditorResource& res) const override {
					return res.addNode(type);
				}
				Node::Type type;
			} creator;
			creator.type = type;
			return visitType(label, creator, shortcut);
		}
	};

	void visitCategories(ICategoryVisitor& visitor) {
		if (visitor.beginCategory("Constants")) {
			struct : ICategoryVisitor::INodeCreator {
				Node* create(ParticleEmitterEditorResource& res) const override {
					auto* n = (ConstNode*)res.addNode(Node::CONST);
					n->constant = (ConstNode::Const)i;
					return n;
				}
				u8 i;
			} creator;
			creator.i = (u8)ConstNode::Const::TIME_DELTA;
			visitor.visitType("Time delta", creator);
			creator.i = (u8)ConstNode::Const::TOTAL_TIME;
			visitor.visitType("Total time", creator);
			creator.i = (u8)ConstNode::Const::EMIT_INDEX;
			visitor.visitType("Emit index", creator);
			visitor.endCategory();
		}

		if (visitor.beginCategory("Emit inputs")) {
			for (u8 i = 0; i < m_active_emitter->m_emit_inputs.size(); ++i) {
				struct : ICategoryVisitor::INodeCreator {
					Node* create(ParticleEmitterEditorResource& res) const override {
						auto* n = (EmitInputNode*)res.addNode(Node::EMIT_INPUT);
						n->idx = i;
						return n;
					}
					u8 i;
				} creator;
				creator.i = i;
				visitor.visitType(m_active_emitter->m_emit_inputs[i].name, creator);
			}
			visitor.endCategory();
		}

		if (visitor.beginCategory("Emit")) {
			for (i32 i = 0; i < m_resource->m_emitters.size(); ++i) {
				ParticleEmitterEditorResource& emitter = *m_resource->m_emitters[i].get();
				struct : ICategoryVisitor::INodeCreator {
					Node* create(ParticleEmitterEditorResource& res) const override {
						auto* n = (EmitNode*)res.addNode(Node::EMIT);
						n->emitter_idx = i;
						return n;
					}
					u32 i;
				} creator;
				creator.i = i;
				visitor.visitType(emitter.m_name.c_str(), creator);
			}
			visitor.endCategory();
		}

		if (visitor.beginCategory("Streams")) {
			for (u8 i = 0; i < m_active_emitter->m_streams.size(); ++i) {
				struct : ICategoryVisitor::INodeCreator {
					Node* create(ParticleEmitterEditorResource& res) const override {
						auto* n = (StreamNode*)res.addNode(Node::STREAM);
						n->idx = i;
						return n;
					}
					u8 i;
				} creator;
				creator.i = i;
				visitor.visitType(m_active_emitter->m_streams[i].name, creator);
			}
			visitor.endCategory();
		}

		if (visitor.beginCategory("Math")) {
			visitor.visitType(Node::ADD, "Add", 'A')
			.visitType(Node::COLOR_MIX, "Color mix")
			.visitType(Node::COS, "Cos")
			.visitType(Node::DIV, "Divide", 'D')
			.visitType(Node::MUL, "Multiply", 'M')
			.visitType(Node::MADD, "Multiply add")
			.visitType(Node::SIN, "Sin")
			.visitType(Node::SUB, "Subtract", 'S')
			.endCategory();
		}

		visitor
			.visitType(Node::CACHE, "Cache")
			.visitType(Node::CHANNEL_MASK, "Channel mask")
			.visitType(Node::CMP, "Compare")
			.visitType(Node::CURVE, "Curve", 'C')
			.visitType(Node::GRADIENT_COLOR, "Gradient color")
			.visitType(Node::MESH, "Mesh")
			.visitType(Node::NOISE, "Noise", 'N')
			.visitType(Node::NUMBER, "Number", '1')
			.visitType(Node::RANDOM, "Random", 'R')
			.visitType(Node::SPLINE, "Spline")
			.visitType(Node::SWITCH, "Switch")
			.visitType(Node::VEC3, "Vec3", '3')
			.visitType(Node::VEC4, "Vec4", '4');
	}

	void onCanvasClicked(ImVec2 pos, i32 hovered_link) override {
		struct : ICategoryVisitor {
			ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut = 0) override {
				if (shortcut && os::isKeyDown((os::Keycode)shortcut)) {
					ASSERT(!n);
					n = creator.create(*editor->m_active_emitter);
					ASSERT(n);
				}
				return *this;
			}
			
			ParticleEditorImpl* editor;
			Node* n = nullptr;
		} visitor;
		visitor.editor = this;
		visitCategories(visitor);
		if (visitor.n) {
			visitor.n->m_pos = pos;
			if (hovered_link >= 0) splitLink(m_active_emitter->m_nodes.back(), m_active_emitter->m_links, hovered_link);
			pushUndo(NO_MERGE_UNDO);
		}
	}
	
	void onLinkDoubleClicked(NodeEditorLink& link, ImVec2 pos) override {
		Node* n = addNode(Node::Type::PIN);
		NodeEditorLink new_link;
		new_link.from = n->m_id | OUTPUT_FLAG; 
		new_link.to = link.to;
		link.to = n->m_id;
		m_active_emitter->m_links.push(new_link);
		n->m_pos = pos;
		pushUndo(0xffFF);
	}

	void onContextMenu(ImVec2 pos) override {
		Node* n = nullptr;
		ImGui::SetNextItemWidth(150);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
		if (m_filter[0]) {
			struct : ICategoryVisitor {
				ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut) override {
					if (n) return *this;
					if (stristr(label, editor->m_filter)) {
						if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::MenuItem(label)) {
							n = creator.create(*editor->m_active_emitter);
							ImGui::CloseCurrentPopup();
						}
					}
					return *this;
				}
			
				ParticleEditorImpl* editor;
				Node* n = nullptr;
			} visitor;
			visitor.editor = this;
			visitCategories(visitor);
			n = visitor.n;
		}
		else {
			struct : ICategoryVisitor {
				void endCategory() override { ImGui::EndMenu(); }
				bool beginCategory(const char* category) override { return ImGui::BeginMenu(category); }

				ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut) override {
					if (ImGui::Selectable(label)) n = creator.create(*editor->m_active_emitter);
					return *this;
				}
			
				ParticleEditorImpl* editor;
				Node* n = nullptr;
			} visitor;
			visitor.editor = this;
			visitCategories(visitor);
			n = visitor.n;
		}

		if (n) {
			m_filter[0] = '\0';
			n->m_pos = pos;
				
			if (m_half_link_start) {
				if (m_half_link_start & OUTPUT_FLAG) {
					if (n->hasInputPins()) m_active_emitter->m_links.push({u32(m_half_link_start), u32(n->m_id)});
				}
				else {
					if (n->hasOutputPins()) m_active_emitter->m_links.push({u32(n->m_id | OUTPUT_FLAG) , u32(m_half_link_start)});
				}
				m_half_link_start = 0;
			}
			pushUndo(NO_MERGE_UNDO);
		}	
	}

	void deleteSelectedNodes() {
		for (i32 i = m_active_emitter->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_active_emitter->m_nodes[i];
			if (n->m_selected && i > 2) {
				m_active_emitter->m_links.eraseItems([&](const NodeEditorLink& link){
					return link.getFromNode() == n->m_id || link.getToNode() == n->m_id;
				});
				LUMIX_DELETE(m_allocator, n);
				m_active_emitter->m_nodes.swapAndPop(i);
			}
		}
		pushUndo(NO_MERGE_UNDO);
	}

	bool hasFocus() override { return m_has_focus; }

	void onSettingsLoaded() override {
		m_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_particle_editor_open", false);
		m_recent_paths.onSettingsLoaded();
	}

	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue(Settings::GLOBAL, "is_particle_editor_open", m_open);
		m_recent_paths.onBeforeSettingsSaved();
	}

	bool isOpen() const { return m_open; }
	void toggleOpen() { m_open = !m_open; }

	void deleteEmitInput(u32 input_idx) {
		for (i32 i = m_active_emitter->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_active_emitter->m_nodes[i];
			if (n->getType() == Node::EMIT_INPUT && ((EmitInputNode*)n)->idx == input_idx) {
				for (i32 j = m_active_emitter->m_links.size() - 1; j >= 0; --j) {
					NodeEditorLink& link = m_active_emitter->m_links[j];
					if (link.getFromNode() == n->m_id) {
						m_active_emitter->m_links.swapAndPop(j);
					}
				}
				LUMIX_DELETE(m_allocator, n);
				m_active_emitter->m_nodes.swapAndPop(i);
				break;
			}
		}
		m_active_emitter->m_emit_inputs.erase(input_idx);
		pushUndo(NO_MERGE_UNDO);
	}

	void deleteOutput(u32 output_idx) {
		for (i32 i = m_active_emitter->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_active_emitter->m_nodes[i];
			if (n->getType() == Node::OUTPUT) {
				for (i32 j = m_active_emitter->m_links.size() - 1; j >= 0; --j) {
					NodeEditorLink& link = m_active_emitter->m_links[j];
					if (link.getToNode() == n->m_id) {
						if (link.getToPin() == output_idx) {
							m_active_emitter->m_links.swapAndPop(j);
						}
						else if (link.getToPin() > output_idx) {
							link.to = link.getToNode() | (u32(link.getToPin() - 1) << 16);
						}
					}
				}
			}
		}
		m_active_emitter->m_outputs.erase(output_idx);
		pushUndo(NO_MERGE_UNDO);
	}

	void deleteStream(u32 stream_idx) {
		for (i32 i = m_active_emitter->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_active_emitter->m_nodes[i];
			switch (n->getType()) {
				case Node::UPDATE: {
					auto* node = (UpdateNode*)n;
					for (i32 j = m_active_emitter->m_links.size() - 1; j >= 0; --j) {
						NodeEditorLink& link = m_active_emitter->m_links[j];
						if (link.getToNode() == node->m_id) {
							// `stream_idx + 1` because of the "kill" input pin
							if (link.getToPin() == stream_idx + 1) {
								m_active_emitter->m_links.swapAndPop(j);
							}
							else if (link.getToPin() > stream_idx + 1) {
								link.to = link.getToNode() | (u32(link.getToPin() - 1) << 16);
							}
						}
					}
					break;
				}
				case Node::INIT: {
					auto* node = (InitNode*)n;
					for (i32 j = m_active_emitter->m_links.size() - 1; j >= 0; --j) {
						NodeEditorLink& link = m_active_emitter->m_links[j];
						if (link.getToNode() == node->m_id) {
							if (link.getToPin() == stream_idx) {
								m_active_emitter->m_links.swapAndPop(j);
							}
							else if (link.getToPin() > stream_idx) {
								link.to = link.getToNode() | (u32(link.getToPin() - 1) << 16);
							}
						}
					}
					break;
				}
				case Node::STREAM: {
					auto* node = (StreamNode*)n;
					if (node->idx == stream_idx) {
						m_active_emitter->m_links.eraseItems([&](const NodeEditorLink& link){
							return link.getFromNode() == n->m_id || link.getToNode() == n->m_id;
						});
						LUMIX_DELETE(m_allocator, n)
						m_active_emitter->m_nodes.swapAndPop(i);
					}
					break;
				}
				default: break;
			}
		}
		m_active_emitter->m_streams.erase(stream_idx);
		pushUndo(NO_MERGE_UNDO);
	}

	void leftColumnGUI() {
		inputString("##name", "Emitter name", &m_active_emitter->m_name);
		ImGuiEx::Label("Material");
		m_app.getAssetBrowser().resourceInput("material", Span(m_active_emitter->m_mat_path.data), Material::TYPE);
		ImGuiEx::Label("Emit per second");
		ImGui::DragFloat("##eps", &m_active_emitter->m_emit_per_second);
		ImGuiEx::Label("Emit at start");
		ImGui::DragInt("##eas", (i32*)&m_active_emitter->m_init_emit_count);
		if (ImGui::CollapsingHeader("Streams", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEmitterEditorResource::Stream& s : m_active_emitter->m_streams) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					deleteStream(u32(&s - m_active_emitter->m_streams.begin()));
					ImGui::PopID();
					break;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				ImGui::Combo("##t", (i32*)&s.type, "float\0vec3\0vec4\0");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##v", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button(ICON_FA_PLUS "##add_stream")) {
				m_active_emitter->m_streams.emplace();
			}
		}
		if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEmitterEditorResource::Output& s : m_active_emitter->m_outputs) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					deleteOutput(u32(&s - m_active_emitter->m_outputs.begin()));
					ImGui::PopID();
					break;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				ImGui::Combo("##t", (i32*)&s.type, "float\0vec3\0vec4\0");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##o", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button(ICON_FA_PLUS "##add_output")) {
				m_active_emitter->m_outputs.emplace();
			}
		}

		if (ImGui::CollapsingHeader("Emit inputs", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEmitterEditorResource::EmitInput& input : m_active_emitter->m_emit_inputs) {
				ImGui::PushID(&input);
				if (ImGui::Button(ICON_FA_TRASH)) {
					deleteEmitInput(u32(&input - m_active_emitter->m_emit_inputs.begin()));
					ImGui::PopID();
					break;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				ImGui::Combo("##t", (i32*)&input.type, "float\0vec3\0vec4\0");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##o", input.name.data, sizeof(input.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button(ICON_FA_PLUS "##add_input")) {
				m_active_emitter->m_emit_inputs.emplace();
			}
		}
	}

	const ParticleSystem* getSelectedParticleSystem() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.size() != 1) return nullptr;

		World* world = editor.getWorld();
		ComponentType emitter_type = reflection::getComponentType("particle_emitter");
		RenderModule* module = (RenderModule*)world->getModule(emitter_type);
		const bool has = world->hasComponent(selected[0], emitter_type);
		return has ? &module->getParticleSystem(selected[0]) : nullptr;
	}

	void apply() {
		const ParticleSystem* system = getSelectedParticleSystem();
		if (!system) return;

		for (u32 emitter_idx = 0, c = system->getEmitters().size(); emitter_idx < c; ++emitter_idx) {
			ASSERT(c == m_resource->m_emitters.size());
			const ParticleSystem::Emitter& emitter = system->getEmitters()[emitter_idx];
			const UniquePtr<ParticleEmitterEditorResource>& editor_emitter = m_resource->m_emitters[emitter_idx];
			OutputMemoryStream instructions(m_allocator);
			instructions.resize(editor_emitter->m_update.size() + editor_emitter->m_emit.size() + editor_emitter->m_output.size());
			memcpy(instructions.getMutableData(), editor_emitter->m_update.data(), editor_emitter->m_update.size());
			memcpy(instructions.getMutableData() + editor_emitter->m_update.size(), editor_emitter->m_emit.data(), editor_emitter->m_emit.size());
			memcpy(instructions.getMutableData() + editor_emitter->m_update.size() + editor_emitter->m_emit.size(), editor_emitter->m_output.data(), editor_emitter->m_output.size());
			auto getCount = [](const auto& x){
				u32 c = 0;
				for (const auto& i : x) c += Lumix::getCount(i.type);
				return c;
			};
			system->getResource()->overrideData(emitter_idx
				, static_cast<OutputMemoryStream&&>(instructions)
				, u32(editor_emitter->m_update.size())
				, u32(editor_emitter->m_update.size() + editor_emitter->m_emit.size())
				, getCount(editor_emitter->m_streams)
				, editor_emitter->m_registers_count
				, getCount(editor_emitter->m_outputs)
				, editor_emitter->m_init_emit_count
				, getCount(editor_emitter->m_emit_inputs)
				, editor_emitter->m_emit_per_second
				, Path(editor_emitter->m_mat_path));
		}
	}

	static constexpr const char* WINDOW_NAME = "Particle editor";

	void debugUI() {
		if (!m_show_debug) return;
		if (ImGui::Begin("Debug particles", &m_show_debug)) {
			const ParticleSystem* system = getSelectedParticleSystem();
			if (system) {
				const Array<ParticleSystem::Emitter>& emitters = system->getEmitters();
				const Array<ParticleSystemResource::Emitter>& res_emitters =  system->getResource()->getEmitters();
				if (ImGui::BeginTabBar("tb")) { 
					for (i32 emitter_idx = 0; emitter_idx < emitters.size(); ++emitter_idx) {
						const ParticleSystem::Emitter& emitter = emitters[emitter_idx];
						const ParticleSystemResource::Emitter& res_emitter = res_emitters[emitter_idx];
						if (ImGui::BeginTabItem(StaticString<64>(emitter_idx + 1))) {
							if (ImGui::BeginTable("tab", res_emitter.channels_count + 1)) {
								for (u32 j = 0; j < emitter.particles_count; ++j) {
									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::Text("%d", j);
									for (u32 i = 0; i < res_emitter.channels_count; ++i) {
										ImGui::TableNextColumn();
										ImGui::Text("%f", emitter.channels[i].data[j]);
									}
								}
								ImGui::EndTable();
							}
							ImGui::EndTabItem();
						}
					}
					ImGui::EndTabBar();
				}
			}
		}
		ImGui::End();
	}


	void onWindowGUI() override {
		m_has_focus = false;
		if (!m_open) return;

		if (m_confirm_new) ImGui::OpenPopup("Confirm##cn");
		if (m_confirm_load) ImGui::OpenPopup("Confirm##cl");

		m_confirm_new = false;
		m_confirm_load = false;

		if (ImGui::BeginPopupModal("Confirm##cn")) {
			ImGui::TextUnformatted("Graph not saved, all changes will be lost. Are you sure?");
			if (ImGui::Selectable("Yes")) {
				m_dirty = false;
				newGraph();
			}
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Confirm##cl")) {
			ImGui::TextUnformatted("Graph not saved, all changes will be lost. Are you sure?");
			if (ImGui::Selectable("Yes")) {
				m_dirty = false;
				load(m_confirm_load_path);
			}
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}

		debugUI();

		ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(WINDOW_NAME, &m_open, ImGuiWindowFlags_MenuBar)) {
			ImGui::End();
			return;
		}
		
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				const ParticleSystem* emitter = getSelectedParticleSystem();
				if (ImGui::MenuItem("New")) newGraph();
				if (ImGui::BeginMenu("Open")) {
					char buf[LUMIX_MAX_PATH] = "";
					FilePathHash dummy;
					if (m_app.getAssetBrowser().resourceList(Span(buf), dummy, ParticleSystemResource::TYPE, false, false)) {
						open(buf);
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Load from entity", nullptr, false, emitter)) loadFromEntity();
				if (ImGui::MenuItem("Debug entity", nullptr, false, emitter)) m_show_debug = true;
				menuItem(m_save_action, true);
				if (ImGui::MenuItem("Save as")) m_show_save_as = true;
				if (const char* path = m_recent_paths.menu(); path) open(path);
				ImGui::Separator();
			
				menuItem(m_apply_action, emitter && emitter->getResource());
				ImGui::MenuItem("Autoapply", nullptr, &m_autoapply, emitter && emitter->getResource());

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				ImGui::MenuItem("World space", 0, &m_resource->m_world_space);
				menuItem(m_undo_action, canUndo());
				menuItem(m_redo_action, canRedo());
				if (ImGui::MenuItem("Add emitter")) {
					ParticleEmitterEditorResource* emitter = m_resource->addEmitter();
					emitter->initDefault();
					pushUndo(NO_MERGE_UNDO);
				}
				if (ImGui::MenuItem("Remove emitter")) {
					if (m_resource->m_emitters.size() <= 1) {
						logError("Can not remove the last emitter");
					}
					else {
						m_resource->removeEmitter(m_active_emitter);
						m_active_emitter = m_resource->m_emitters[0].get();
						pushUndo(NO_MERGE_UNDO);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		FileSelector& fs = m_app.getFileSelector();
		if (fs.gui("Save As", &m_show_save_as, "par", true)) saveAs(fs.getPath());

		if (ImGui::BeginTabBar("tabbar")) {
			for (const UniquePtr<ParticleEmitterEditorResource>& emitter : m_resource->m_emitters) {
				u32 idx = u32(&emitter - m_resource->m_emitters.begin());
				if (ImGui::BeginTabItem(StaticString<256>(emitter->m_name.c_str(), "###", idx))) {
					ImGui::Columns(2);
					leftColumnGUI();
					ImGui::NextColumn();
					m_active_emitter = emitter.get();
					nodeEditorGUI(m_active_emitter->m_nodes, m_active_emitter->m_links);
					ImGui::Columns();
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	Node* addNode(Node::Type type) {
		return m_active_emitter->addNode(type);
	}

	void pushUndo(u32 tag) override {
		m_active_emitter->colorLinks();
		for (const UniquePtr<ParticleEmitterEditorResource>& emitter : m_resource->m_emitters) {
			// TODO generate only active emitter and make sure other emitters are up-to-date
			emitter->generate();
		}
		if (m_autoapply) apply();
		m_dirty = true;
		
		SimpleUndoRedo::pushUndo(tag);
	}

	void loadFromEntity() {
		const ParticleSystem* emitter = getSelectedParticleSystem();
		ASSERT(emitter);

		const Path& path = emitter->getResource()->getPath();
		load(path.c_str());
	}

	void serialize(OutputMemoryStream& blob) override { m_resource->serialize(blob); }
	
	void deserialize(InputMemoryStream& blob) override { 
		m_resource = UniquePtr<ParticleSystemEditorResource>::create(m_allocator, m_allocator);
		m_resource->deserialize(blob, "");
		m_active_emitter = m_resource->m_emitters[0].get();
	}

	void load(const char* path) {
		ASSERT(path && path[0] != '\0');

		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream blob(m_allocator);
		if (!fs.getContentSync(Path(path), blob)) {
			logError("Failed to read ", path);
			return;
		}
		m_resource = UniquePtr<ParticleSystemEditorResource>::create(m_allocator, m_allocator);
		InputMemoryStream iblob(blob);
		m_resource->deserialize(iblob, path);
		m_active_emitter = m_resource->m_emitters[0].get();
		m_path = path;
		clearUndoStack();
		m_recent_paths.push(path);
		pushUndo(NO_MERGE_UNDO);
		m_dirty = false;
	}

	void save() {
		if (!m_path.isEmpty()) {
			saveAs(m_path.c_str());
			return;
		}
		m_show_save_as = true;
	}

	void saveAs(const char* path) {
		OutputMemoryStream blob(m_allocator);
		m_resource->serialize(blob);

		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.saveContentSync(Path(path), blob)) {
			logError("Failed to save ", path);
			return;
		}
		
		m_path = path;
		m_recent_paths.push(path);
		m_dirty = false;
	}

	void newGraph() {
		if (m_dirty) {
			m_confirm_new = true;
			return;
		}

		clearUndoStack();
		m_resource = UniquePtr<ParticleSystemEditorResource>::create(m_allocator, m_allocator);
		m_active_emitter = m_resource->m_emitters[0].get();
		m_active_emitter->initDefault();

		m_path = "";
		pushUndo(NO_MERGE_UNDO);
		m_dirty = false;
	}

	const char* getName() const override { return "Particle editor"; }

	void open(const char* path) override {
		ImGui::SetWindowFocus(WINDOW_NAME);
		m_open = true;
		if (m_dirty) {
			m_confirm_load = true;
			m_confirm_load_path = path;
			return;
		}
		
		load(path);
	}

	bool compile(InputMemoryStream& input, OutputMemoryStream& output, const char* path) override {
		ParticleSystemEditorResource res(m_allocator);
		if (!res.deserialize(input, path)) return false;

		ParticleSystemResource::Header header;
		output.write(header);
		
		ParticleSystemResource::Flags flags = ParticleSystemResource::Flags::NONE;
		if (res.m_world_space) flags = ParticleSystemResource::Flags::WORLD_SPACE;
		output.write(flags);
		
		output.write(res.m_emitters.size());
		for (const UniquePtr<ParticleEmitterEditorResource>& emitter : res.m_emitters) {
			if (!emitter->generate()) return false;

			gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
			emitter->fillVertexDecl(decl, nullptr, m_allocator);
			output.write(decl);
			output.writeString(emitter->m_mat_path);
			const u32 count = u32(emitter->m_update.size() + emitter->m_emit.size() + emitter->m_output.size());
			output.write(count);
			output.write(emitter->m_update.data(), emitter->m_update.size());
			output.write(emitter->m_emit.data(), emitter->m_emit.size());
			output.write(emitter->m_output.data(), emitter->m_output.size());
			output.write((u32)emitter->m_update.size());
			output.write(u32(emitter->m_update.size() + emitter->m_emit.size()));

			auto getCount = [](const auto& x){
				u32 c = 0;
				for (const auto& i : x) c += Lumix::getCount(i.type);
				return c;
			};

			output.write(getCount(emitter->m_streams));
			output.write((u32)emitter->m_registers_count);
			output.write(getCount(emitter->m_outputs));
			output.write(emitter->m_init_emit_count);
			output.write(emitter->m_emit_per_second);
			output.write(getCount(emitter->m_emit_inputs));
		}
		return true;
	}

	IAllocator& m_allocator;
	StudioApp& m_app;
	Path m_path;
	bool m_show_debug = false;
	bool m_show_save_as = false;
	bool m_dirty = false;
	bool m_confirm_new = false;
	bool m_confirm_load = false;
	StaticString<LUMIX_MAX_PATH> m_confirm_load_path;
	UniquePtr<ParticleSystemEditorResource> m_resource;
	ParticleEmitterEditorResource* m_active_emitter = nullptr;
	bool m_open = false;
	bool m_autoapply = false;
	Action m_toggle_ui;
	Action m_save_action;
	Action m_undo_action;
	Action m_redo_action;
	Action m_apply_action;
	Action m_delete_action;
	bool m_has_focus = false;
	ImGuiEx::Canvas m_canvas;
	ImVec2 m_offset = ImVec2(0, 0);
	RecentPaths m_recent_paths;
	char m_filter[64] = "";
};

DataStream NodeInput::generate(GenerateContext& ctx, DataStream output, u8 subindex) const {
	return node ? node->generate(ctx, output, subindex) : DataStream();
}

gpu::VertexDecl ParticleEditor::getVertexDecl(const char* path, Array<String>& attribute_names, StudioApp& app) {
	attribute_names.clear();
	gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
#if 0
	ParticleSystemEditorResource system(app.getAllocator());
	OutputMemoryStream blob(app.getAllocator());
	if (app.getEngine().getFileSystem().getContentSync(Path(path), blob)) {
		InputMemoryStream tmp(blob);
		if (system.deserialize(tmp, path)) {
			system.fillVertexDecl(decl, &attribute_names, app.getAllocator());
		}
		else {
			logError("Failed to parse ", path);
		}
	}
	else {
		logError("Failed to load ", path);
	}
	return decl;
#endif
	ASSERT(false);
	// TODO
	return decl;
}

UniquePtr<ParticleEditor> ParticleEditor::create(StudioApp& app) {
	IAllocator& allocator = app.getAllocator();
	return UniquePtr<ParticleEditorImpl>::create(allocator, app, allocator);
}


} // namespace Lumix