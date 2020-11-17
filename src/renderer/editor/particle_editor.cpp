#define LUMIX_NO_CUSTOM_CRT
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/associative_array.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/reflection.h"
#include "engine/string.h"
#include "engine/universe.h"
#include "imgui/imgui.h"
#include "imgui/imnodes.h"
#include "renderer/material.h"
#include "renderer/particle_system.h"
#include "renderer/render_scene.h"

namespace Lumix {

static constexpr u32 OUTPUT_FLAG = 1 << 31;
using DataStream = ParticleEmitterResource::DataStream;
using Instruction = ParticleEmitterResource::Instruction;
using InstructionType = ParticleEmitterResource::Instruction::Type;

struct ParticleEditor;

struct ParticleEditorResource {
	struct Node;

	struct Header {
		static constexpr u32 MAGIC = '_LPE';
		const u32 magic = MAGIC;
		u32 version = 0;
	};

	struct NodeInput {
		Node* node;
		u8 output_idx;
		DataStream generate(Array<Instruction>& instructions, DataStream output) const;
	};

	struct Node {
		// this is serialized, do not change order
		enum Type {
			OUTPUT,
			INPUT,
			MUL,
			ADD,
			CONST,
			LITERAL,
			EMIT,
			UPDATE,
			RANDOM,
			UNARY_FUNCTION,
			MADD,
			CMP
		};

		Node(ParticleEditorResource& res) 
			: m_resource(res)
			, m_id(res.genID())
		{}
		virtual ~Node() {}

		virtual DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream output) = 0;
		virtual void serialize(OutputMemoryStream& blob) {}
		virtual void deserialize(InputMemoryStream& blob) {}

		NodeInput getInput(u8 input_idx) {
			for (const Link& link : m_resource.m_links) {
				if (link.toNode() != m_id) continue;
				if (link.toPin() != input_idx) continue;

				NodeInput res;
				res.output_idx = link.fromPin();
				res.node = m_resource.getNodeByID(link.fromNode());
				return res;
			}

			return {};
		}

		void beginInput() {
			imnodes::BeginInputAttribute(m_id | (u32(m_input_counter) << 16));
			++m_input_counter;
		}

		static void endInput() { imnodes::EndInputAttribute(); }

		void beginOutput() {
			imnodes::BeginOutputAttribute(m_id | (u32(m_output_counter) << 16) | OUTPUT_FLAG);
			++m_output_counter;
		}

		static void endOutput() { imnodes::EndOutputAttribute(); }

		bool onNodeGUI() {
			m_input_counter = 0;
			m_output_counter = 0;
			imnodes::SetNodeEditorSpacePos(m_id, m_pos);
			imnodes::BeginNode(m_id);
			bool res = onGUI();
			imnodes::EndNode();
			return res;
		}

		u16 m_id;
		ImVec2 m_pos = ImVec2(100, 100);
		virtual Type getType() const = 0;
	
	protected:
		virtual bool onGUI() = 0;
		
		ParticleEditorResource& m_resource;
		u8 m_input_counter;
		u8 m_output_counter;
	};
	
	struct UnaryFunctionNode : Node {
		UnaryFunctionNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::UNARY_FUNCTION; }

		void serialize(OutputMemoryStream& blob) override { blob.write(func); }
		void deserialize(InputMemoryStream& blob) override { blob.read(func); }
		
		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream output) override {
			NodeInput input = getInput(0);
			if (!input.node) return output;
			
			Instruction& i = instructions.emplace();
			switch (func) {
				case COS: i.type = InstructionType::COS; break;
				case SIN: i.type = InstructionType::SIN; break;
				default: ASSERT(false); break;
			}
			i.dst = m_resource.streamOrRegister(output);
			i.op0 = input.generate(instructions, i.op0);

			m_resource.freeRegister(i.op0);

			return i.dst;
		}

		bool onGUI() override {
			beginInput();
			endInput();
			beginOutput();
			ImGui::SetNextItemWidth(60);
			ImGui::Combo("##fn", (int*)&func, "cos\0sin\0");
			endOutput();
			return false;
		}

		enum Function : int {
			COS,
			SIN
		};

		Function func = COS;
	};

	struct ConstNode : Node {
		ConstNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::CONST; }

		void serialize(OutputMemoryStream& blob) override { blob.write(idx); }
		void deserialize(InputMemoryStream& blob) override { blob.read(idx); }
		
		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			DataStream r;
			r.type = DataStream::CONST;
			r.index = idx;
			return r;
		}

		bool onGUI() override {
			beginOutput();
			ImGui::TextUnformatted(m_resource.m_consts[idx].name);
			endOutput();
			return false;
		}

		u8 idx;
	};

	struct RandomNode : Node {
		RandomNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::RANDOM; }

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream output) override {
			Instruction& i = instructions.emplace();
			i.type = InstructionType::RAND;
			i.dst = m_resource.streamOrRegister(output);
			i.op0.type = DataStream::LITERAL;
			i.op0.value = from;
			i.op1.type = DataStream::LITERAL;
			i.op1.value = to;
			return i.dst;
		}

		void serialize(OutputMemoryStream& blob) override {
			blob.write(from);
			blob.write(to);
		}

		void deserialize(InputMemoryStream& blob) override {
			blob.read(from);
			blob.read(to);
		}

		bool onGUI() override {
			beginOutput();
			imnodes::BeginNodeTitleBar();
			ImGui::Text("Random");
			imnodes::EndNodeTitleBar();
			endOutput();
			ImGui::PushItemWidth(60);
			ImGui::DragFloat("From", &from);
			ImGui::DragFloat("To", &to);
			ImGui::PopItemWidth();
			return false;
		}

		float from = 0;
		float to = 1;
	};

	struct LiteralNode : Node {
		LiteralNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::LITERAL; }

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			DataStream r;
			r.type = DataStream::LITERAL;
			r.value = value;
			return r;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value); }

		bool onGUI() override {
			beginOutput();
			ImGui::SetNextItemWidth(120);
			ImGui::DragFloat("##v", &value);
			endOutput();
			return false;
		}

		float value = 0;
	};

	struct InputNode : Node {
		InputNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::INPUT; }

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			DataStream r;
			r.type = DataStream::CHANNEL;
			r.index = idx;
			return r;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(idx); }
		void deserialize(InputMemoryStream& blob) override { blob.read(idx); }

		bool onGUI() override {
			beginOutput();
			if (idx < m_resource.m_streams.size()) {
				ImGui::TextUnformatted(m_resource.m_streams[idx].name);
			}
			else {
				ImGui::TextUnformatted(ICON_FA_EXCLAMATION "Deleted input");
			}
			endOutput();
			return false;
		}

		u8 idx;
	};

	struct EmitNode : Node {
		EmitNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::EMIT; }

		bool onGUI() override {
			imnodes::BeginNodeTitleBar();
			ImGui::TextUnformatted("Emit");
			imnodes::EndNodeTitleBar();
			for (const Stream& stream : m_resource.m_streams) {
				beginInput();
				ImGui::TextUnformatted(stream.name);
				endInput();
			}
			return false;
		}

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			m_resource.m_register_mask = 0;
			for (i32 i = 0; i < m_resource.m_streams.size(); ++i) {
				const NodeInput input = getInput(i);
				if (!input.node) continue;

				DataStream s;
				s.type = DataStream::CHANNEL;
				s.index = i;
				DataStream o = input.generate(instructions, s);
				if (o.type != DataStream::CHANNEL || o.index != i) {
					Instruction& instr = instructions.emplace();
					instr.type = InstructionType::MOV;
					instr.dst = s;
					instr.op0 = o;
				}
			}
			return {};
		}
	};

	struct UpdateNode : Node {
		UpdateNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::UPDATE; }

		bool onGUI() override {
			imnodes::BeginNodeTitleBar();
			ImGui::TextUnformatted("Update");
			imnodes::EndNodeTitleBar();

			beginInput();
			ImGui::TextUnformatted("Kill");
			endInput();

			for (const Stream& stream : m_resource.m_streams) {
				beginInput();
				ImGui::TextUnformatted(stream.name);
				endInput();
			}
			return false;
		}

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			m_resource.m_register_mask = 0;
			const NodeInput kill_input = getInput(0);
			if (kill_input.node) {
				kill_input.generate(instructions, {});
				instructions.emplace().type = InstructionType::KILL;
			}

			for (i32 i = 0; i < m_resource.m_streams.size(); ++i) {
				const NodeInput input = getInput(i + 1);
				if (!input.node) continue;

				DataStream s;
				s.type = DataStream::CHANNEL;
				s.index = i;
				DataStream o = input.generate(instructions, s);
				if (o.type != DataStream::CHANNEL || o.index != i) {
					Instruction& instr = instructions.emplace();
					instr.type = InstructionType::MOV;
					instr.dst = s;
					instr.op0 = o;
				}
			}
			return {};
		}
	};

	struct CompareNode : Node {
		CompareNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::CMP; }

		bool onGUI() override {
			beginInput();
			ImGui::TextUnformatted("A");
			endInput();

			beginOutput();
			ImGui::SetNextItemWidth(60);
			ImGui::Combo("##op", (int*)&op, "A < B\0A > B\0");
			endOutput();

			beginInput();
			if (getInput(1).node) {
				ImGui::TextUnformatted("B");
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("B", &value);
			}
			endInput();

			return false;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(op); blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(op); blob.read(value); }

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			const NodeInput input0 = getInput(0);
			const NodeInput input1 = getInput(1);
			if (!input0.node) return {};

			DataStream i0 = input0.generate(instructions, {});
			DataStream i1 = input1.node ? input1.generate(instructions, {}) : DataStream{};
			Instruction& i = instructions.emplace();
			switch (op) {
				case LT: i.type = InstructionType::LT; break;
				case GT: i.type = InstructionType::GT; break;
				default: ASSERT(false); break;
			}
			
			i.dst = i0;
			if (input1.node) {
				i.op0 = i1;
			}
			else {
				i.op0.type = DataStream::LITERAL;
				i.op0.value = value;
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

	struct OutputNode : Node {
		OutputNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::OUTPUT; }

		bool onGUI() override {
			imnodes::BeginNodeTitleBar();
			ImGui::TextUnformatted("Output");
			imnodes::EndNodeTitleBar();
			for (const Output& stream : m_resource.m_outputs) {
				beginInput();
				ImGui::TextUnformatted(stream.name);
				endInput();
			}
			return false;
		}

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream) override {
			m_resource.m_register_mask = 0;
			for (i32 i = 0; i < m_resource.m_outputs.size(); ++i) {
				const NodeInput input = getInput(i);
				if (!input.node) continue;

				DataStream s;
				s.type = DataStream::OUT;
				s.index = i;
				DataStream o = input.generate(instructions, s);
				if (o.type != DataStream::OUT || o.index != i) {
					Instruction& instr = instructions.emplace();
					instr.type = InstructionType::MOV;
					instr.dst = s;
					instr.op0 = o;
				}
			}
			return {};
		}
	};

	struct MaddNode : Node {
		MaddNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::MADD; }

		void serialize(OutputMemoryStream& blob) override { blob.write(value1); blob.write(value2); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value1); blob.read(value2); }

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream output) override {
			ASSERT(output_idx == 0);
			const NodeInput input0 = getInput(0);
			if (!input0.node) return output;
			const NodeInput input1 = getInput(1);
			const NodeInput input2 = getInput(2);

			Instruction i;
			i.op0 = input0.generate(instructions, i.op0);
			if (input1.node) {
				i.op1 = input1.generate(instructions, i.op1);
			}
			else {
				i.op1.type = DataStream::LITERAL;
				i.op1.value = value1;
			}

			if (input2.node) {
				i.op2 = input2.generate(instructions, i.op2);
			}
			else {
				i.op2.type = DataStream::LITERAL;
				i.op2.value = value2;
			}

			i.type = InstructionType::MULTIPLY_ADD;
			i.dst = m_resource.streamOrRegister(output);

			instructions.push(i);
			
			m_resource.freeRegister(i.op0);
			m_resource.freeRegister(i.op1);
			m_resource.freeRegister(i.op2);
			return i.dst;
		}

		bool onGUI() override {
			imnodes::BeginNodeTitleBar();
			beginOutput();
			ImGui::TextUnformatted("Multiply add (A * B + C)");
			endOutput();
			imnodes::EndNodeTitleBar();

			beginInput();
			ImGui::TextUnformatted("A");
			endInput();

			beginInput();
			if (getInput(1).node) {
				ImGui::TextUnformatted("B");
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("B", &value1);
			}
			endInput();

			beginInput();
			if (getInput(2).node) {
				ImGui::TextUnformatted("C");
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("C", &value2);
			}
			endInput();

			return false;
		}

		float value1 = 0;
		float value2 = 0;
	};

	template <InstructionType OP_TYPE>
	struct BinaryOpNode : Node {
		BinaryOpNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { 
			switch(OP_TYPE) {
				case InstructionType::MUL: return Type::MUL;
				case InstructionType::ADD: return Type::ADD;
				default: ASSERT(false); return Type::MUL;
			}
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value); }

		DataStream generate(Array<Instruction>& instructions, u8 output_idx, DataStream output) override {
			ASSERT(output_idx == 0);
			const NodeInput input0 = getInput(0);
			if (!input0.node) return output;
			const NodeInput input1 = getInput(1);

			Instruction i;
			i.op0 = input0.generate(instructions, i.op0);
			if (input1.node) {
				i.op1 = input1.generate(instructions, i.op1);
			}
			else {
				i.op1.type = DataStream::LITERAL;
				i.op1.value = value;
			}

			i.type = OP_TYPE;
			i.dst = m_resource.streamOrRegister(output);

			instructions.push(i);
			
			m_resource.freeRegister(i.op0);
			m_resource.freeRegister(i.op1);
			return i.dst;
		}

		bool onGUI() override {
			imnodes::BeginNodeTitleBar();
			beginOutput();
			switch(OP_TYPE) {
				case InstructionType::MUL: ImGui::TextUnformatted("Multiply"); break;
				case InstructionType::ADD: ImGui::TextUnformatted("Add"); break;
				default: ASSERT(false); break;
			}
			endOutput();
			imnodes::EndNodeTitleBar();

			beginInput();
			ImGui::TextUnformatted("A");
			endInput();

			beginInput();
			if (getInput(1).node) {
				ImGui::TextUnformatted("B");
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("B", &value);
			}
			endInput();

			return false;
		}

		float value = 0;
	};

	struct Stream {
		StaticString<32> name;
	};

	struct Constant {
		StaticString<32> name;
	};

	struct Output {
		StaticString<32> name;
	};
	
	struct Link {
		int id;
		int from;
		int to;

		u16 toNode() const { return to & 0xffFF; }
		u16 fromNode() const { return from & 0xffFF; }
		
		u8 toPin() const { return (to >> 16) & 0xff; }
		u8 fromPin() const { return (from >> 16) & 0xff; }
	};

	ParticleEditorResource(IAllocator& allocator)
		: m_allocator(allocator)
		, m_nodes(allocator)
		, m_links(allocator)
		, m_streams(allocator)
		, m_outputs(allocator)
		, m_consts(allocator)
		, m_update(allocator)
		, m_emit(allocator)
		, m_output(allocator)
	{}
	
	u16 genID() { return ++m_last_id; }

	Node* getNodeByID(u16 id) const {
		for (UniquePtr<Node>& node : m_nodes) {
			if (node->m_id == id) return node.get();
		}
		return nullptr;
	}

	Node* addNode(Node::Type type) {
		UniquePtr<Node> node;
		switch(type) {
			case Node::CMP: node = UniquePtr<CompareNode>::create(m_allocator, *this); break;
			case Node::MADD: node = UniquePtr<MaddNode>::create(m_allocator, *this); break;
			case Node::RANDOM: node = UniquePtr<RandomNode>::create(m_allocator, *this); break;
			case Node::EMIT: node = UniquePtr<EmitNode>::create(m_allocator, *this); break;
			case Node::UPDATE: node = UniquePtr<UpdateNode>::create(m_allocator, *this); break;
			case Node::INPUT: node = UniquePtr<InputNode>::create(m_allocator, *this); break;
			case Node::OUTPUT: node = UniquePtr<OutputNode>::create(m_allocator, *this); break;
			case Node::MUL: node = UniquePtr<BinaryOpNode<InstructionType::MUL>>::create(m_allocator, *this); break;
			case Node::ADD: node = UniquePtr<BinaryOpNode<InstructionType::ADD>>::create(m_allocator, *this); break;
			case Node::CONST: node = UniquePtr<ConstNode>::create(m_allocator, *this); break;
			case Node::UNARY_FUNCTION: node = UniquePtr<UnaryFunctionNode>::create(m_allocator, *this); break;
			case Node::LITERAL: node = UniquePtr<LiteralNode>::create(m_allocator, *this); break;
			default: ASSERT(false);
		}
		m_nodes.push(node.move());
		return m_nodes.back().get();
	}

	bool deserialize(InputMemoryStream& blob, const char* path) {
		Header header;
		blob.read(header);
		if (header.magic != Header::MAGIC) {
			logError("Invalid file ", path);
			return false;
		}
		if (header.version != 0) {
			logError("Invalid file version ", path);
			return false;
		}

		blob.read(m_last_id);
		m_mat_path = blob.readString();
		
		i32 count;

		blob.read(count);
		m_streams.resize(count);
		blob.read(m_streams.begin(), m_streams.byte_size());

		blob.read(count);
		m_outputs.resize(count);
		blob.read(m_outputs.begin(), m_outputs.byte_size());

		blob.read(count);
		m_consts.resize(count);
		blob.read(m_consts.begin(), m_consts.byte_size());

		blob.read(count);
		m_links.resize(count);
		blob.read(m_links.begin(), m_links.byte_size());

		blob.read(count);
		for (i32 i = 0; i < count; ++i) {
			Node::Type type;
			blob.read(type);
			Node* n = addNode(type);
			blob.read(n->m_id);
			blob.read(n->m_pos);
			n->deserialize(blob);
		}
		return true;
	}

	void serialize(OutputMemoryStream& blob) {
		Header header;
		blob.write(header);
		blob.write(m_last_id);
		blob.writeString(m_mat_path.data);
		
		blob.write((i32)m_streams.size());
		blob.write(m_streams.begin(), m_streams.byte_size());

		blob.write((i32)m_outputs.size());
		blob.write(m_outputs.begin(), m_outputs.byte_size());

		blob.write((i32)m_consts.size());
		blob.write(m_consts.begin(), m_consts.byte_size());

		blob.write((i32)m_links.size());
		blob.write(m_links.begin(), m_links.byte_size());

		blob.write((i32)m_nodes.size());
		for (const UniquePtr<Node>& n : m_nodes) {
			blob.write(n->getType());
			blob.write(n->m_id);
			blob.write(n->m_pos);
			n->serialize(blob);
		}
	}

	void initDefault() {
		m_streams.emplace().name = "pos_x";
		m_streams.emplace().name = "pos_y";
		m_streams.emplace().name = "pos_z";
		m_outputs.emplace().name = "pos_x";
		m_outputs.emplace().name = "pos_y";
		m_outputs.emplace().name = "pos_z";

		m_consts.emplace().name = "delta time";

		m_nodes.push(UniquePtr<UpdateNode>::create(m_allocator, *this));
		m_nodes.push(UniquePtr<OutputNode>::create(m_allocator, *this));
		m_nodes.push(UniquePtr<EmitNode>::create(m_allocator, *this));
	}

	void generate() {
		m_update.clear();
		m_output.clear();
		m_emit.clear();

		m_registers_count = 0;
		m_nodes[0]->generate(m_update, 0, {});
		m_update.emplace().type = InstructionType::END;
		m_nodes[1]->generate(m_output, 0, {});
		m_output.emplace().type = InstructionType::END;
		m_nodes[2]->generate(m_emit, 0, {});
		m_emit.emplace().type = InstructionType::END;
	}

	void toString(OutputMemoryStream& text, DataStream r) const {
		switch(r.type) {
			case DataStream::CHANNEL: text << m_streams[r.index].name; break;
			case DataStream::REGISTER: text << "r" << r.index; break;
			case DataStream::OUT: text << "OUT." << m_outputs[r.index].name; break;
			case DataStream::CONST: text << m_consts[r.index].name; break;
			case DataStream::LITERAL: text << r.value; break;
			default: ASSERT(false); break;
		}
	}

	void toString(OutputMemoryStream& text, const Array<Instruction>& instructions) const {
		for (const Instruction& i : instructions) {
			if (i.type == InstructionType::END) continue;
			switch (i.type) {
				case InstructionType::GT: text << "\tgt("; break;
				case InstructionType::LT: text << "\tlt("; break;
				case InstructionType::KILL: text << "\tkill("; break;
				case InstructionType::COS: text << "\tcos("; break;
				case InstructionType::SIN: text << "\tsin("; break;
				case InstructionType::MOV: text << "\tmov("; break;
				case InstructionType::ADD: text << "\tadd("; break;
				case InstructionType::MUL: text << "\tmul("; break;
				case InstructionType::MULTIPLY_ADD: text << "\tmadd("; break;
				case InstructionType::RAND: text << "\trand("; break;
				default: ASSERT(false); break;
			}

			if (i.dst.type != DataStream::NONE) {
				toString(text, i.dst);
			}
			if (i.op0.type != DataStream::NONE) {
				text << ", ";
				toString(text, i.op0);
			}
			if (i.op1.type != DataStream::NONE) {
				text << ", ";
				toString(text, i.op1);
			}
			if (i.op2.type != DataStream::NONE) {
				text << ", ";
				toString(text, i.op2);
			}
			text << ")\n";
		}
	}
	
	void freeRegister(DataStream v) {
		if (v.type != DataStream::REGISTER) return;
		m_register_mask &= ~(1 << v.index);
	}

	DataStream streamOrRegister(DataStream v) {
		if (v.type == DataStream::NONE) { 
			DataStream r;
			r.type = DataStream::REGISTER;
			r.index = 0xff;
			for (u32 i = 0; i < 8; ++i) {
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

	IAllocator& m_allocator;
	StaticString<MAX_PATH_LENGTH> m_mat_path;
	Array<Stream> m_streams;
	Array<Output> m_outputs;
	Array<Constant> m_consts;
	Array<UniquePtr<Node>> m_nodes;
	Array<Link> m_links;
	Array<Instruction> m_update;
	Array<Instruction> m_emit;
	Array<Instruction> m_output;
	int m_last_id = 0;
	u8 m_register_mask = 0;
	u8 m_registers_count = 0;
};

struct ParticleEditor : StudioApp::GUIPlugin {
	ParticleEditor(StudioApp& app, IAllocator& allocator)
		: m_allocator(allocator)
		, m_app(app)
		, m_code(m_allocator)
	{
		m_toggle_ui.init("Particle editor", "Toggle particle editor", "particle_editor", "", true);
		m_toggle_ui.func.bind<&ParticleEditor::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ParticleEditor::isOpen>(this);
		app.addWindowAction(&m_toggle_ui);
		newGraph();
	}

	~ParticleEditor() {
		m_app.removeAction(&m_toggle_ui);
	}

	void onSettingsLoaded() override {
		m_open = m_app.getSettings().getValue("is_particle_editor_open", false);
	}
	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue("is_particle_editor_open", m_open);
	}

	bool isOpen() const { return m_open; }
	void toggleOpen() { m_open = !m_open; }

	void leftColumnGUI() {
		ImGuiEx::Label("Material");
		m_app.getAssetBrowser().resourceInput("material", Span(m_resource->m_mat_path.data), Material::TYPE);
		if (ImGui::CollapsingHeader("Streams", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Stream& s : m_resource->m_streams) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					m_resource->m_streams.erase(u32(&s - m_resource->m_streams.begin()));
					ImGui::PopID();
					pushUndo();
					break;
				}
				ImGui::SameLine();				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##v", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button("Add##add_stream")) {
				m_resource->m_streams.emplace();
			}
		}
		if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Output& s : m_resource->m_outputs) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					m_resource->m_outputs.erase(u32(&s - m_resource->m_outputs.begin()));
					ImGui::PopID();
					pushUndo();
					break;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##o", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button("Add##add_output")) {
				m_resource->m_outputs.emplace();
			}
		}
		if (ImGui::CollapsingHeader("Constants", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Constant& s : m_resource->m_consts) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					m_resource->m_consts.erase(u32(&s - m_resource->m_consts.begin()));
					ImGui::PopID();
					pushUndo();
					break;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##v", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button("Add##add_const")) {
				m_resource->m_consts.emplace();
			}
		}
		if (m_code.size() > 0 && ImGui::CollapsingHeader("Source", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextMultiline("##Code", (char*)m_code.data(), m_code.size());
		}
	}

	ParticleEmitter* getSelectedEmitter() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.size() != 1) return nullptr;

		Universe* universe = editor.getUniverse();
		ComponentType emitter_type = Reflection::getComponentType("particle_emitter");
		RenderScene* scene = (RenderScene*)universe->getScene(emitter_type);
		const bool has = universe->hasComponent(selected[0], emitter_type);
		EntityRef e = selected[0];
		return has ? scene->getParticleEmitters()[e] : nullptr;
	}

	void onWindowGUI() override {
		if (!m_open) return;
		if (!ImGui::Begin("Particle editor", &m_open, ImGuiWindowFlags_MenuBar)) {
			ImGui::End();
			return;
		}
		
		ParticleEmitter* emitter = getSelectedEmitter();
		
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New")) newGraph();
				if (ImGui::MenuItem("Load")) load();
				if (!m_path.empty() && ImGui::MenuItem("Save")) save(m_path);
				if (ImGui::MenuItem("Save as")) saveAs();
				ImGui::Separator();
			
				if (ImGui::MenuItem("Apply", 0, false, emitter && emitter->getResource())) {
					Array<Instruction> instructions(m_allocator);
					instructions.resize(m_resource->m_update.size() + m_resource->m_emit.size() + m_resource->m_output.size());
					memcpy(instructions.begin(), m_resource->m_update.begin(), m_resource->m_update.byte_size());
					memcpy(instructions.begin() + m_resource->m_update.size(), m_resource->m_emit.begin(), m_resource->m_emit.byte_size());
					memcpy(instructions.begin() + m_resource->m_update.size() + m_resource->m_emit.size(), m_resource->m_output.begin(), m_resource->m_output.byte_size());
					emitter->getResource()->overrideData(instructions.move()
						, m_resource->m_update.size()
						, m_resource->m_update.size() + m_resource->m_emit.size()
						, m_resource->m_streams.size()
						, 0
						, m_resource->m_outputs.size());
					emitter->getResource()->setMaterial(Path(m_resource->m_mat_path));
				}

				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		ImGui::Columns(2);

		if (emitter) {
			ImGui::LabelText("Particle count", "%d", emitter->m_particles_count);
		}
		leftColumnGUI();
		
		ImGui::NextColumn();
		bool context_open = false;
		imnodes::BeginNodeEditor();

		if (imnodes::IsEditorHovered() && ImGui::IsMouseClicked(1)) {
			ImGui::OpenPopup("context_menu");
			context_open = true;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
		if (ImGui::BeginPopup("context_menu")) {
			if (ImGui::BeginMenu("Add")) {
				if (ImGui::Selectable("Add")) addNode(ParticleEditorResource::Node::ADD);
				if (ImGui::Selectable("Multiply")) addNode(ParticleEditorResource::Node::MUL);
				if (ImGui::Selectable("Multiply add")) addNode(ParticleEditorResource::Node::MADD);
				if (ImGui::Selectable("Literal")) addNode(ParticleEditorResource::Node::LITERAL);
				if (ImGui::Selectable("Compare")) addNode(ParticleEditorResource::Node::CMP);
				if (ImGui::Selectable("Random")) addNode(ParticleEditorResource::Node::RANDOM);
				if (ImGui::Selectable("Sin")) {
					ParticleEditorResource::Node* n = addNode(ParticleEditorResource::Node::UNARY_FUNCTION);
					((ParticleEditorResource::UnaryFunctionNode*)n)->func = ParticleEditorResource::UnaryFunctionNode::SIN;
				}
				if (ImGui::Selectable("Cos")) {
					ParticleEditorResource::Node* n = addNode(ParticleEditorResource::Node::UNARY_FUNCTION);
					((ParticleEditorResource::UnaryFunctionNode*)n)->func = ParticleEditorResource::UnaryFunctionNode::COS;
				}
				if (ImGui::BeginMenu("Input")) {
					for (u8 i = 0; i < m_resource->m_streams.size(); ++i) {
						if (ImGui::Selectable(m_resource->m_streams[i].name)) {
							UniquePtr<ParticleEditorResource::InputNode> n = UniquePtr<ParticleEditorResource::InputNode>::create(m_allocator, *m_resource.get());
							n->idx = i;
							m_resource->m_nodes.push(n.move());
							pushUndo();
						}
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Constant")) {
					for (u8 i = 0; i < m_resource->m_consts.size(); ++i) {
						if (ImGui::Selectable(m_resource->m_consts[i].name)) {
							UniquePtr<ParticleEditorResource::ConstNode> n = UniquePtr<ParticleEditorResource::ConstNode>::create(m_allocator, *m_resource.get());
							n->idx = i;
							m_resource->m_nodes.push(n.move());
							pushUndo();
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			if (m_context_node != -1 && ImGui::Selectable("Remove node")) {
				m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
					return link.fromNode() == m_context_node || link.toNode() == m_context_node;
				});

				m_resource->m_nodes.eraseItems([&](const UniquePtr<ParticleEditorResource::Node>& node){
					return node->m_id == m_context_node;
				});
			}

			if (m_context_link != -1 && ImGui::Selectable("Remove link")) {
				m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
					return link.id == m_context_link;
				});
				pushUndo();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		for (UniquePtr<ParticleEditorResource::Node>& n : m_resource->m_nodes) {
			if (n->onNodeGUI()) {
				pushUndo();
			}
		}

		for (const ParticleEditorResource::Link& link : m_resource->m_links) {
			imnodes::Link(link.id, link.from, link.to);
		}

		imnodes::EndNodeEditor();

		for (UniquePtr<ParticleEditorResource::Node>& n : m_resource->m_nodes) {
			n->m_pos = imnodes::GetNodeEditorSpacePos(n->m_id);
		}

		if (context_open) {
			m_context_link = -1;
			imnodes::IsLinkHovered(&m_context_link);
			m_context_node = -1;
			imnodes::IsNodeHovered(&m_context_node);
		}

		{
			int from, to;
			if (imnodes::IsLinkCreated(&from, &to)) {
				ParticleEditorResource::Link& link = m_resource->m_links.emplace();
				link.id = m_resource->genID();
				link.from = from;
				link.to = to;
				pushUndo();
			}
		}
		ImGui::Columns();

		ImGui::End();
	}

	ParticleEditorResource::Node* addNode(ParticleEditorResource::Node::Type type) {
		ParticleEditorResource::Node* n = m_resource->addNode(type);
		pushUndo();
		return n;
	}

	void pushUndo() {
		m_resource->generate();
		m_code.clear();

		m_code << "function update()\n";
		m_resource->toString(m_code, m_resource->m_update);
		m_code << "end\n\n";

		m_code << "function emit()\n";
		m_resource->toString(m_code, m_resource->m_emit);
		m_code << "end\n\n";

		m_code << "function output()\n";
		m_resource->toString(m_code, m_resource->m_output);
		m_code << "end\n";
		m_code.write('\0');	
		// TODO push in undo queue
	}

	void load() {
		char path[MAX_PATH_LENGTH];
		if (!OS::getOpenFilename(Span(path), "Particles\0*.par\0", nullptr)) return;
	
		OS::InputFile file;
		if (file.open(path)) {
			const u64 size = file.size();
			OutputMemoryStream blob(m_allocator);
			blob.resize(size);
			if (!file.read(blob.getMutableData(), blob.size())) {
				logError("Failed to read ", path);
				file.close();
				return;
			}
			file.close();

			m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
			InputMemoryStream iblob(blob);
			m_resource->deserialize(iblob, path);
			m_path = path;
			m_resource->generate();
			pushUndo();
		}
		else {
			logError("Failed to open ", path);
		}
	}

	void saveAs() {
		char path[MAX_PATH_LENGTH];
		if (!OS::getSaveFilename(Span(path), "Particles\0*.par\0", "par")) return;

		save(path);
	}

	void save(const char* path) {
		OutputMemoryStream blob(m_allocator);
		m_resource->serialize(blob);

		OS::OutputFile file;
		if (file.open(path)) {
			if (!file.write(blob.data(), blob.size())) {
				logError("Failed to write ", path);
			}
			else {
				m_path = path;
			}
			file.close();
		}
		else {
			logError("Failed to open ", path);
		}
	}

	void newGraph() {
		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		m_resource->initDefault();
		m_path = "";
	}

	const char* getName() const override { return "Particle editor"; }

	IAllocator& m_allocator;
	StudioApp& m_app;
	StaticString<MAX_PATH_LENGTH> m_path;
	UniquePtr<ParticleEditorResource> m_resource;
	OutputMemoryStream m_code;
	bool m_open = false;
	int m_context_link;
	int m_context_node;
	Action m_toggle_ui;
};

DataStream ParticleEditorResource::NodeInput::generate(Array<Instruction>& instructions, DataStream output) const {
	return node ? node->generate(instructions, output_idx, output) : DataStream();
}

UniquePtr<StudioApp::GUIPlugin> createParticleEditor(StudioApp& app) {
	return UniquePtr<ParticleEditor>::create(app.getAllocator(), app, app.getAllocator());
}

bool compileParticleEmitter(InputMemoryStream& input, OutputMemoryStream& output, const char* path, IAllocator& allocator) {
	ParticleEditorResource res(allocator);
	if (!res.deserialize(input, path)) return false;

	res.generate();

	ParticleEmitterResource::Header header;
	output.write(header);
	output.writeString(res.m_mat_path); // material
	const i32 count = res.m_update.size() + res.m_emit.size() + res.m_output.size();
	output.write(count);
	output.write(res.m_update.begin(), res.m_update.byte_size());
	output.write(res.m_emit.begin(), res.m_emit.byte_size());
	output.write(res.m_output.begin(), res.m_output.byte_size());
	output.write((i32)res.m_update.size());
	output.write(i32(res.m_update.size() + res.m_emit.size()));
	output.write((i32)res.m_streams.size());
	output.write((i32)res.m_registers_count);
	output.write((i32)res.m_outputs.size());
	return true;
}

} // namespace Lumix