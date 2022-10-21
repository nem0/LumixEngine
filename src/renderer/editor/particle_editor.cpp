#define LUMIX_NO_CUSTOM_CRT
#include "particle_editor.h"
#include "editor/asset_browser.h"
#include "editor/imguicanvas.h"
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
#include "engine/universe.h"
#include "renderer/material.h"
#include "renderer/particle_system.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include <imgui/imgui.h>

namespace Lumix {

static constexpr u32 OUTPUT_FLAG = 1 << 31;
using DataStream = ParticleEmitterResource::DataStream;
using InstructionType = ParticleEmitterResource::InstructionType;

struct ParticleEditorImpl;

struct ParticleEditorResource {
	struct Node;

	struct Header {
		static constexpr u32 MAGIC = '_LPE';
		const u32 magic = MAGIC;
		u32 version = 0;
	};
	
	enum class ValueType : i32 {
		FLOAT,
		VEC3,
		VEC4
	};

	struct NodeInput {
		Node* node;
		u8 output_idx;
		DataStream generate(OutputMemoryStream& instructions, DataStream output, u8 subindex) const; //-V1071
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
			CMP,
			COLOR_MIX,
			GRADIENT,
			GRADIENT_COLOR,
			VEC3,
			DIV
		};

		Node(ParticleEditorResource& res) 
			: m_resource(res)
			, m_id(res.genID())
		{}
		virtual ~Node() {}

		virtual Type getType() const = 0;
		virtual DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) = 0;
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

		void inputSlot(ImGuiEx::PinShape shape = ImGuiEx::PinShape::CIRCLE) {
			ImGuiEx::Pin(m_id | (u32(m_input_counter) << 16), true, shape);
			++m_input_counter;
		}

		void outputSlot(ImGuiEx::PinShape shape = ImGuiEx::PinShape::CIRCLE) {
			ImGuiEx::Pin(m_id | (u32(m_output_counter) << 16) | OUTPUT_FLAG, false, shape);
			++m_output_counter;
		}

		bool onNodeGUI() {
			m_input_counter = 0;
			m_output_counter = 0;
			const ImVec2 old_pos = m_pos;
			ImGuiEx::BeginNode(m_id, m_pos, nullptr);
			bool res = onGUI();
			ImGuiEx::EndNode();
			return res || old_pos.x != m_pos.x || old_pos.y != m_pos.y;
		}

		u16 m_id;
		ImVec2 m_pos = ImVec2(100, 100);
	
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
		
		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			NodeInput input = getInput(0);
			if (!input.node) return output;
			
			DataStream dst = m_resource.streamOrRegister(output);
			DataStream op0 = input.generate(instructions, {}, subindex);
			switch (func) {
				case COS: instructions.write(InstructionType::COS); break;
				case SIN: instructions.write(InstructionType::SIN); break;
				default: ASSERT(false); break;
			}
			instructions.write(dst);
			instructions.write(op0);

			m_resource.freeRegister(op0);

			return dst;
		}

		bool onGUI() override {
			inputSlot();
			ImGui::SetNextItemWidth(60);
			ImGui::Combo("##fn", (int*)&func, "cos\0sin\0");
			ImGui::SameLine();
			outputSlot();
			return false;
		}

		enum Function : int {
			COS,
			SIN
		};

		Function func = COS;
	};
	
	struct GradientColorNode : Node {
		GradientColorNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::GRADIENT_COLOR; }
	
		DataStream generate(OutputMemoryStream& ip, u8 output_idx, DataStream dst, u8 subindex) override {
			const NodeInput input = getInput(0);
			if (!input.node) {
				DataStream res;
				res.type = DataStream::LITERAL;
				res.value = values[0][subindex];
				return res;
			}

			DataStream op0;
			op0 = input.generate(ip, op0, subindex);

			dst = m_resource.streamOrRegister(dst);
			ip.write(InstructionType::GRADIENT);
			ip.write(dst);
			ip.write(op0);
			ip.write(count);
			ip.write(keys, sizeof(keys[0]) * count);
			for (u32 i = 0; i < count; ++i) {
				ip.write(values[i][subindex]);
			}

			m_resource.freeRegister(op0);
			return dst;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(count); blob.write(keys); blob.write(values); }
		void deserialize(InputMemoryStream& blob) override { blob.read(count); blob.read(keys); blob.read(values); }

		bool onGUI() override {
			ImGui::SetNextItemWidth(120);
			inputSlot();
			bool changed = ImGuiEx::Gradient4("test", lengthOf(keys), (int*)&count, keys, &values[0].x);
			ImGui::SameLine();
			outputSlot();
			return changed;
		}

		u32 count = 2;
		float keys[8] = { 0, 1 };
		Vec4 values[8] = { Vec4(0, 0, 0, 1), Vec4(1, 1, 1, 1) };
	};

	struct GradientNode : Node {
		GradientNode(ParticleEditorResource& res) : Node(res) {}
		Type getType() const override { return Type::GRADIENT; }
	
		DataStream generate(OutputMemoryStream& ip, u8 output_idx, DataStream dst, u8 subindex) override {
			const NodeInput input = getInput(0);
			if (!input.node) {
				DataStream res;
				res.type = DataStream::LITERAL;
				res.value = values[0];
				return res;
			}

			DataStream op0;
			op0 = input.generate(ip, op0, 0);

			dst = m_resource.streamOrRegister(dst);
			ip.write(InstructionType::GRADIENT);
			ip.write(dst);
			ip.write(op0);
			ip.write(count);
			ip.write(keys, sizeof(keys[0]) * count);
			ip.write(values, sizeof(values[0]) * count);

			m_resource.freeRegister(op0);
			return dst;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(count); blob.write(keys); blob.write(values); }
		void deserialize(InputMemoryStream& blob) override { blob.read(count); blob.read(keys); blob.read(values); }

		bool onGUI() override {
			ImGui::TextUnformatted("Gradient");
			ImGui::BeginGroup();
			inputSlot(); 

			ImGui::PushItemWidth(60);
			bool changed = false;
			for (u32 i = 0; i < count; ++i) {
				ImGui::PushID(i);
				changed = ImGui::DragFloat("##k", &keys[i]) || changed ;
				ImGui::SameLine();
				changed = ImGui::DragFloat("##v", &values[i]) || changed ;
				ImGui::PopID();
				keys[i] = clamp(keys[i], 0.f, 1.f);
			}
			ImGui::PopItemWidth();
			if (ImGui::Button("Add")) {
				ASSERT(count < lengthOf(values));
				keys[count] = 0;
				values[count] = 0;
				++count;
				changed = true;
			}
			ImGui::EndGroup();

			ImGui::SameLine();
			outputSlot();
			return changed ;
		}

		u32 count = 2;
		float keys[8] = {};
		float values[8] = {};
	};

	struct ConstNode : Node {
		ConstNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::CONST; }

		void serialize(OutputMemoryStream& blob) override { blob.write(idx); }
		void deserialize(InputMemoryStream& blob) override { blob.read(idx); }
		
		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream, u8 subindex) override {
			DataStream r;
			r.type = DataStream::CONST;
			r.index = idx;
			return r;
		}

		bool onGUI() override {
			outputSlot(); ImGui::TextUnformatted(m_resource.m_consts[idx].name);
			return false;
		}

		u8 idx;
	};

	struct RandomNode : Node {
		RandomNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::RANDOM; }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			instructions.write(InstructionType::RAND);
			DataStream op0, op1, dst;
			dst = m_resource.streamOrRegister(output);
			instructions.write(dst);
			instructions.write(from);
			instructions.write(to);
			return dst;
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
			//imnodes::BeginNodeTitleBar();
			ImGui::Text(ICON_FA_DICE " Random");
			//imnodes::EndNodeTitleBar();

			ImGui::BeginGroup();
			ImGui::PushItemWidth(60);
			ImGui::DragFloat("##from", &from);
			ImGui::SameLine(); ImGui::DragFloat("##to", &to);
			ImGui::PopItemWidth();
			ImGui::EndGroup();

			ImGui::SameLine();

			outputSlot();
			return false;
		}

		float from = 0;
		float to = 1;
	};

	struct LiteralNode : Node {
		LiteralNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::LITERAL; }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream, u8 subindex) override {
			DataStream r;
			r.type = DataStream::LITERAL;
			r.value = value;
			return r;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value); }

		bool onGUI() override {
			outputSlot();
			ImGui::SetNextItemWidth(120);
			bool changed = ImGui::DragFloat("##v", &value);
			return changed;
		}

		float value = 0;
	};

	struct Vec3Node : Node {
		Vec3Node(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::VEC3; }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			const NodeInput input = getInput(subindex);
			if (input.node) {
				return input.generate(instructions, output, subindex);
			}

			DataStream r;
			r.type = DataStream::LITERAL;
			r.value = value[subindex];
			return r;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value); }

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
			ImGui::EndGroup();
			
			ImGui::PopItemWidth();
			
			ImGui::SameLine();
			outputSlot();
			
			return changed;
		}

		Vec3 value = Vec3(0);
	};

	struct InputNode : Node {
		InputNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::INPUT; }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream, u8 subindex) override {
			DataStream r;
			r.type = DataStream::CHANNEL;
			r.index = m_resource.getChannelIndex(idx, subindex);
			return r;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(idx); }
		void deserialize(InputMemoryStream& blob) override { blob.read(idx); }

		bool onGUI() override {
			outputSlot();
			if (idx < m_resource.m_streams.size()) {
				ImGui::TextUnformatted(m_resource.m_streams[idx].name);
			}
			else {
				ImGui::TextUnformatted(ICON_FA_EXCLAMATION "Deleted input");
			}
			return false;
		}

		u8 idx;
	};

	struct EmitNode : Node {
		EmitNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::EMIT; }

		bool onGUI() override {
			//imnodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(ICON_FA_PLUS " Emit");
			//imnodes::EndNodeTitleBar();
			for (const Stream& stream : m_resource.m_streams) {
				inputSlot(); ImGui::TextUnformatted(stream.name);
			}
			return false;
		}

		DataStream generate(OutputMemoryStream& instructions, u8, DataStream, u8 subindex) override {
			m_resource.m_register_mask = 0;
			i32 output_idx = 0;
			for (i32 i = 0; i < m_resource.m_streams.size(); ++i) {
				const NodeInput input = getInput(i);
				const u32 si_count = getCount(m_resource.m_streams[i].type);

				for (u32 si = 0; si < si_count; ++si) {
					DataStream s;
					s.type = DataStream::CHANNEL;
					s.index = output_idx;
					if (input.node) {
						DataStream o = input.generate(instructions, s, si);
						if (o.type != s.type || o.index != s.index) {
							instructions.write(InstructionType::MOV);
							instructions.write(s);
							instructions.write(o);
						}
					}
					else {
						instructions.write(InstructionType::MOV);
						instructions.write(s);
						DataStream l;
						l.type = DataStream::LITERAL;
						l.value = 0;
						instructions.write(l);
					}
					++output_idx;
				}
			}
			return {};
		}
	};

	static u32 getCount(ValueType type) {
		switch(type) {
			case ValueType::VEC3: return 3;
			case ValueType::VEC4: return 4;
			case ValueType::FLOAT: return 1;
			default: ASSERT(false); return 1;
		}
	}

	i32 findChannel(const char* name) {
		for (i32 i = 0; i < m_streams.size(); ++i) {
			if (m_streams[i].name == name) return i;
		}
		return -1;
	}

	u32 getChannelIndex(u8 stream, u8 subindex) {
		u32 c = 0;
		for (u8 i = 0; i < stream; ++i) {
			c += getCount(m_streams[i].type);
		}
		switch (m_streams[stream].type) {
			case ValueType::FLOAT: return c;
			case ValueType::VEC3: return c + clamp(subindex, 0, 3);
			case ValueType::VEC4: return c + clamp(subindex, 0, 4);
			default: ASSERT(false); return c;
		}
		
	}

	struct UpdateNode : Node {
		UpdateNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::UPDATE; }

		bool onGUI() override {
			
			//imnodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(ICON_FA_CLOCK " Update");
			//imnodes::EndNodeTitleBar();

			inputSlot(ImGuiEx::PinShape::TRIANGLE); ImGui::TextUnformatted("Kill");

			for (const Stream& stream : m_resource.m_streams) {
				inputSlot(); ImGui::TextUnformatted(stream.name);
			}
			return false;
		}

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream, u8 subindex) override {
			m_resource.m_register_mask = 0;
			const NodeInput kill_input = getInput(0);
			if (kill_input.node) {
				kill_input.generate(instructions, {}, 0);
				instructions.write(InstructionType::KILL);
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
					DataStream o = input.generate(instructions, s, si);
					if (o.type != s.type || o.index != s.index) {
						instructions.write(InstructionType::MOV);
						instructions.write(s);
						instructions.write(o);
					}
					++out_index;
				}
			}
			return {};
		}
	};

	struct CompareNode : Node {
		CompareNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::CMP; }

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

		void serialize(OutputMemoryStream& blob) override { blob.write(op); blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(op); blob.read(value); }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream, u8 subindex) override {
			const NodeInput input0 = getInput(0);
			const NodeInput input1 = getInput(1);
			if (!input0.node) return {};

			DataStream i0 = input0.generate(instructions, {}, subindex);
			DataStream i1 = input1.node ? input1.generate(instructions, {}, subindex) : DataStream{};
			switch (op) {
				case LT: instructions.write(InstructionType::LT); break;
				case GT: instructions.write(InstructionType::GT); break;
				default: ASSERT(false); break;
			}
			
			instructions.write(i0);
			if (input1.node) {
				instructions.write(i1);
			}
			else {
				DataStream op0;
				op0.type = DataStream::LITERAL;
				op0.value = value;
				instructions.write(op0);
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
			//imnodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(ICON_FA_EYE " Output");
			//imnodes::EndNodeTitleBar();
			for (const Output& stream : m_resource.m_outputs) {
				inputSlot(); ImGui::TextUnformatted(stream.name);
			}
			return false;
		}

		DataStream generate(OutputMemoryStream& instructions, u8, DataStream, u8 subindex) override {
			m_resource.m_register_mask = 0;
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
							instructions.write(InstructionType::MOV);
							instructions.write(s);
							instructions.write(o);
							++output_idx;
						}
						continue;
					}
				}

				for (u32 si = 0; si < si_count; ++si) {
					DataStream s;
					s.type = DataStream::OUT;
					s.index = output_idx;
					DataStream o = input.generate(instructions, s, si);
					if (o.type != s.type || o.index != s.index) {
						instructions.write(InstructionType::MOV);
						instructions.write(s);
						instructions.write(o);
					}
					++output_idx;
				}
			}
			return {};
		}
	};

	struct ColorMixNode : Node {
		ColorMixNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::COLOR_MIX; }

		DataStream generate(OutputMemoryStream& instructions, u8, DataStream output, u8 subindex) override {
			const NodeInput input = getInput(0);
			if (!input.node) return {};

			const DataStream w = input.generate(instructions, {}, subindex);

			instructions.write(InstructionType::MIX);
			DataStream dst, op0, op1;
			dst = m_resource.streamOrRegister(DataStream());
			op0.type = DataStream::LITERAL;
			op0.value = *(&color0.x + subindex);
			op1.type = DataStream::LITERAL;
			op1.value = *(&color1.x + subindex);
			instructions.write(dst);
			instructions.write(op0);
			instructions.write(op1);
			instructions.write(w);
			return dst;
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(color0); blob.write(color1); }
		void deserialize(InputMemoryStream& blob) override { blob.read(color0); blob.read(color1); }

		bool onGUI() override {
			ImGui::BeginGroup();
			inputSlot(); ImGui::TextUnformatted("Weight");
			bool changed = ImGui::ColorEdit4("Color A", &color0.x, ImGuiColorEditFlags_NoInputs);
			changed = ImGui::ColorEdit4("Color B", &color1.x, ImGuiColorEditFlags_NoInputs) || changed;
			ImGui::EndGroup();
			
			ImGui::SameLine();
			outputSlot();
			
			return changed;
		}

		Vec4 color0 = Vec4(1);
		Vec4 color1 = Vec4(1);
	};

	struct MaddNode : Node {
		MaddNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::MADD; }

		void serialize(OutputMemoryStream& blob) override { blob.write(value1); blob.write(value2); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value1); blob.read(value2); }

		DataStream generate(OutputMemoryStream& instructions, u8, DataStream output, u8 subindex) override {
			const NodeInput input0 = getInput(0);
			if (!input0.node) return output;
			const NodeInput input1 = getInput(1);
			const NodeInput input2 = getInput(2);

			DataStream dst, op0, op1, op2;
			op0 = input0.generate(instructions, op0, subindex);
			if (input1.node) {
				op1 = input1.generate(instructions, op1, subindex);
			}
			else {
				op1.type = DataStream::LITERAL;
				op1.value = value1;
			}

			if (input2.node) {
				op2 = input2.generate(instructions, op2, subindex);
			}
			else {
				op2.type = DataStream::LITERAL;
				op2.value = value2;
			}

			instructions.write(InstructionType::MULTIPLY_ADD);
			dst = m_resource.streamOrRegister(output);

			instructions.write(dst);
			instructions.write(op0);
			instructions.write(op1);
			instructions.write(op2);
			
			m_resource.freeRegister(op0);
			m_resource.freeRegister(op1);
			m_resource.freeRegister(op2);
			return dst;
		}

		bool onGUI() override {
			ImGui::BeginGroup();
			inputSlot(); ImGui::NewLine();
			
			ImGui::TextUnformatted("X");
			
			inputSlot();
			if (getInput(1).node) {
				ImGui::NewLine();
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("B", &value1);
			}

			ImGui::TextUnformatted(ICON_FA_PLUS);

			inputSlot();
			if (getInput(2).node) {
				ImGui::NewLine();
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("C", &value2);
			}
			ImGui::EndGroup();

			ImGui::SameLine();
			outputSlot();

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
				case InstructionType::DIV: return Type::DIV;
				case InstructionType::MUL: return Type::MUL;
				case InstructionType::ADD: return Type::ADD;
				default: ASSERT(false); return Type::MUL;
			}
		}

		void serialize(OutputMemoryStream& blob) override { blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value); }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			ASSERT(output_idx == 0);
			const NodeInput input0 = getInput(0);
			if (!input0.node) return output;
			const NodeInput input1 = getInput(1);

			DataStream dst, op0, op1;
			op0 = input0.generate(instructions, op0, subindex);
			if (input1.node) {
				op1 = input1.generate(instructions, op1, subindex);
			}
			else {
				op1.type = DataStream::LITERAL;
				op1.value = value;
			}

			instructions.write(OP_TYPE);
			dst = m_resource.streamOrRegister(output);

			instructions.write(dst);
			instructions.write(op0);
			instructions.write(op1);

			m_resource.freeRegister(op0);
			m_resource.freeRegister(op1);
			return dst;
		}

		bool onGUI() override {
			
			ImGui::BeginGroup();
			inputSlot(); ImGui::NewLine();

			inputSlot();
			if (getInput(1).node) {
				ImGui::NewLine();
			}
			else {
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("##b", &value);
			}
			ImGui::EndGroup();

			ImGui::SameLine();
			switch(OP_TYPE) {
				case InstructionType::DIV: ImGui::TextUnformatted(ICON_FA_DIVIDE); break;
				case InstructionType::MUL: ImGui::TextUnformatted("X"); break;
				case InstructionType::ADD: ImGui::TextUnformatted(ICON_FA_PLUS); break;
				default: ASSERT(false); break;
			}

			ImGui::SameLine();
			outputSlot();

			return false;
		}

		float value = 0;
	};

	struct Stream {
		StaticString<32> name;
		ValueType type = ValueType::FLOAT;
	};

	struct Constant {
		StaticString<32> name;
		ValueType type = ValueType::FLOAT;
	};

	struct Output {
		StaticString<32> name;
		ValueType type = ValueType::FLOAT;
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
			case Node::GRADIENT_COLOR: node = UniquePtr<GradientColorNode>::create(m_allocator, *this); break;
			case Node::GRADIENT: node = UniquePtr<GradientNode>::create(m_allocator, *this); break;
			case Node::VEC3: node = UniquePtr<Vec3Node>::create(m_allocator, *this); break;
			case Node::COLOR_MIX: node = UniquePtr<ColorMixNode>::create(m_allocator, *this); break;
			case Node::MADD: node = UniquePtr<MaddNode>::create(m_allocator, *this); break;
			case Node::RANDOM: node = UniquePtr<RandomNode>::create(m_allocator, *this); break;
			case Node::EMIT: node = UniquePtr<EmitNode>::create(m_allocator, *this); break;
			case Node::UPDATE: node = UniquePtr<UpdateNode>::create(m_allocator, *this); break;
			case Node::INPUT: node = UniquePtr<InputNode>::create(m_allocator, *this); break;
			case Node::OUTPUT: node = UniquePtr<OutputNode>::create(m_allocator, *this); break;
			case Node::DIV: node = UniquePtr<BinaryOpNode<InstructionType::DIV>>::create(m_allocator, *this); break;
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
		m_streams.emplace().name = "life";
		m_outputs.emplace().name = "pos_x";
		m_outputs.emplace().name = "pos_y";
		m_outputs.emplace().name = "pos_z";
		m_outputs.emplace().name = "scale";
		m_outputs.emplace().name = "color_r";
		m_outputs.emplace().name = "color_g";
		m_outputs.emplace().name = "color_b";
		m_outputs.emplace().name = "color_a";
		m_outputs.emplace().name = "rotation";
		m_outputs.emplace().name = "frame";

		m_consts.emplace().name = "delta time";

		m_nodes.push(UniquePtr<UpdateNode>::create(m_allocator, *this));
		m_nodes.push(UniquePtr<OutputNode>::create(m_allocator, *this));
		m_nodes.back()->m_pos = ImVec2(200, 100);
		m_nodes.push(UniquePtr<EmitNode>::create(m_allocator, *this));
		m_nodes.back()->m_pos = ImVec2(300, 100);
	}

	void generate() {
		m_update.clear();
		m_output.clear();
		m_emit.clear();

		m_registers_count = 0;
		m_nodes[0]->generate(m_update, 0, {}, 0);
		
		m_update.write(InstructionType::END);
		m_nodes[1]->generate(m_output, 0, {}, 0);
		m_output.write(InstructionType::END);
		m_nodes[2]->generate(m_emit, 0, {}, 0);
		m_emit.write(InstructionType::END);
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
	StaticString<LUMIX_MAX_PATH> m_mat_path;
	Array<Stream> m_streams;
	Array<Output> m_outputs;
	Array<Constant> m_consts;
	Array<UniquePtr<Node>> m_nodes;
	Array<Link> m_links;
	OutputMemoryStream m_update;
	OutputMemoryStream m_emit;
	OutputMemoryStream m_output;
	int m_last_id = 0;
	u8 m_register_mask = 0;
	u8 m_registers_count = 0;
};

struct ParticleEditorImpl : ParticleEditor {
	ParticleEditorImpl(StudioApp& app, IAllocator& allocator)
		: m_allocator(allocator)
		, m_app(app)
		, m_undo_stack(allocator)
		, m_canvas(app)
	{
		m_toggle_ui.init("Particle editor", "Toggle particle editor", "particle_editor", "", true);
		m_toggle_ui.func.bind<&ParticleEditorImpl::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ParticleEditorImpl::isOpen>(this);

		m_undo_action.init(ICON_FA_UNDO "Undo", "Particle editor undo", "particle_editor_undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL, true);
		m_undo_action.func.bind<&ParticleEditorImpl::undo>(this);
		m_undo_action.plugin = this;

		m_redo_action.init(ICON_FA_REDO "Redo", "Particle editor redo", "particle_editor_redo", ICON_FA_REDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, true);
		m_redo_action.func.bind<&ParticleEditorImpl::redo>(this);
		m_redo_action.plugin = this;

		m_apply_action.init("Apply", "Particle editor apply", "particle_editor_apply", "", os::Keycode::E, Action::Modifiers::CTRL, true);
		m_apply_action.func.bind<&ParticleEditorImpl::apply>(this);
		m_apply_action.plugin = this;

		app.addWindowAction(&m_toggle_ui);
		app.addAction(&m_undo_action);
		app.addAction(&m_redo_action);
		app.addAction(&m_apply_action);
		newGraph();
	}

	~ParticleEditorImpl() {
		m_app.removeAction(&m_toggle_ui);
		m_app.removeAction(&m_undo_action);
		m_app.removeAction(&m_redo_action);
		m_app.removeAction(&m_apply_action);
	}

	bool hasFocus() override { return m_has_focus; }

	void onSettingsLoaded() override {
		m_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_particle_editor_open", false);
	}
	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue(Settings::GLOBAL, "is_particle_editor_open", m_open);
	}

	bool isOpen() const { return m_open; }
	void toggleOpen() { m_open = !m_open; }

	void redo() {
		if (m_undo_idx >= m_undo_stack.size() - 1) return;

		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		++m_undo_idx;
		InputMemoryStream tmp(m_undo_stack[m_undo_idx].data);
		m_resource->deserialize(tmp, "undo");
	}

	void undo() {
		if (m_undo_idx <= 0) return;

		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		--m_undo_idx;
		InputMemoryStream tmp(m_undo_stack[m_undo_idx].data);
		m_resource->deserialize(tmp, "undo");
	}

	void leftColumnGUI() {
		ImGuiEx::Label("Material");
		m_app.getAssetBrowser().resourceInput("material", Span(m_resource->m_mat_path.data), Material::TYPE);
		if (ImGui::CollapsingHeader("Streams", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Stream& s : m_resource->m_streams) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					m_resource->m_streams.erase(u32(&s - m_resource->m_streams.begin()));
					ImGui::PopID();
					pushUndo(0xffFFffFF);
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
				m_resource->m_streams.emplace();
			}
		}
		if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Output& s : m_resource->m_outputs) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					m_resource->m_outputs.erase(u32(&s - m_resource->m_outputs.begin()));
					ImGui::PopID();
					pushUndo(0xffFFffFF);
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
				m_resource->m_outputs.emplace();
			}
		}
		if (ImGui::CollapsingHeader("Constants", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Constant& s : m_resource->m_consts) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					m_resource->m_consts.erase(u32(&s - m_resource->m_consts.begin()));
					ImGui::PopID();
					pushUndo(0xffFFffFF);
					break;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##v", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button(ICON_FA_PLUS "##add_const")) {
				m_resource->m_consts.emplace();
			}
		}
	}

	const ParticleEmitter* getSelectedEmitter() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.size() != 1) return nullptr;

		Universe* universe = editor.getUniverse();
		ComponentType emitter_type = reflection::getComponentType("particle_emitter");
		RenderScene* scene = (RenderScene*)universe->getScene(emitter_type);
		const bool has = universe->hasComponent(selected[0], emitter_type);
		EntityRef e = selected[0];
		return has ? &scene->getParticleEmitter(e) : nullptr;
	}

	void apply() {
		const ParticleEmitter* emitter = getSelectedEmitter();
		if (!emitter) return;

		OutputMemoryStream instructions(m_allocator);
		instructions.resize(m_resource->m_update.size() + m_resource->m_emit.size() + m_resource->m_output.size());
		memcpy(instructions.getMutableData(), m_resource->m_update.data(), m_resource->m_update.size());
		memcpy(instructions.getMutableData() + m_resource->m_update.size(), m_resource->m_emit.data(), m_resource->m_emit.size());
		memcpy(instructions.getMutableData() + m_resource->m_update.size() + m_resource->m_emit.size(), m_resource->m_output.data(), m_resource->m_output.size());
		auto getCount = [](const auto& x){
			u32 c = 0;
			for (const auto& i : x) c += ParticleEditorResource::getCount(i.type);
			return c;
		};
		emitter->getResource()->overrideData(static_cast<OutputMemoryStream&&>(instructions)
			, u32(m_resource->m_update.size())
			, u32(m_resource->m_update.size() + m_resource->m_emit.size())
			, getCount(m_resource->m_streams)
			, m_resource->m_registers_count
			, getCount(m_resource->m_outputs));
		emitter->getResource()->setMaterial(Path(m_resource->m_mat_path));
	}

	void onWindowGUI() override {
		m_has_focus = false;
		if (!m_open) return;
		if (m_is_focus_requested) ImGui::SetNextWindowFocus();
		m_is_focus_requested = false;

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

		if (!ImGui::Begin("Particle editor", &m_open, ImGuiWindowFlags_MenuBar)) {
			ImGui::End();
			return;
		}
		
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				const ParticleEmitter* emitter = getSelectedEmitter();
				if (ImGui::MenuItem("New")) newGraph();
				if (ImGui::MenuItem("Load")) load();
				if (ImGui::MenuItem("Load from entity", nullptr, false, emitter)) loadFromEntity();
				if (ImGui::MenuItem("Save", nullptr, false, !m_path.empty())) save(m_path);
				if (ImGui::MenuItem("Save as")) saveAs();
				ImGui::Separator();
			
				menuItem(m_apply_action, emitter && emitter->getResource());
				ImGui::MenuItem("Autoapply", nullptr, &m_autoapply, emitter && emitter->getResource());

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				menuItem(m_undo_action, m_undo_idx > 0);
				menuItem(m_redo_action, m_undo_idx < m_undo_stack.size() - 1);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		ImGui::Columns(2);

		leftColumnGUI();

		ImGui::NextColumn();

		ImVec2 canvas_size = ImGui::GetContentRegionAvail();
		
		if (canvas_size.x > 0 && canvas_size.y > 0) {

			m_canvas.begin();

			ImGuiEx::BeginNodeEditor("particle_editor", &m_offset);

			i32 hovered_node = -1;
			i32 hovered_link = -1;
			for (UniquePtr<ParticleEditorResource::Node>& n : m_resource->m_nodes) {
				if (n->onNodeGUI()) {
					pushUndo(n->m_id);
				}
				if (ImGui::IsItemHovered()) {
					hovered_node = n->m_id;
				}
			}

			for (const ParticleEditorResource::Link& link : m_resource->m_links) {
				ImGuiEx::NodeLink(link.from, link.to);
				if (ImGuiEx::IsLinkHovered()) {
					hovered_link = link.id;
				}
			}

			ImGuiID nlf, nlt;
			if (ImGuiEx::GetNewLink(&nlf, &nlt)) {
				ParticleEditorResource::Link& link = m_resource->m_links.emplace();
				link.from = nlf;
				link.to = nlt;
				pushUndo(0xffFFffFF);
			}

			ImGuiEx::EndNodeEditor();
			const ImVec2 editor_pos = ImGui::GetItemRectMin();
			bool context_open = false;

			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
				ImGui::OpenPopup("context_menu");
				context_open = true;
			}
		
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
			if (ImGui::BeginPopup("context_menu")) {
				ImVec2 cp = ImGui::GetItemRectMin();
				if (ImGui::BeginMenu("Add")) {
					ParticleEditorResource::Node* n = nullptr;
					if (ImGui::Selectable("Add")) n = addNode(ParticleEditorResource::Node::ADD);
					if (ImGui::Selectable("Color mix")) n = addNode(ParticleEditorResource::Node::COLOR_MIX);
					if (ImGui::Selectable("Compare")) n = addNode(ParticleEditorResource::Node::CMP);
					if (ImGui::BeginMenu("Constant")) {
						for (u8 i = 0; i < m_resource->m_consts.size(); ++i) {
							if (ImGui::Selectable(m_resource->m_consts[i].name)) {
								UniquePtr<ParticleEditorResource::ConstNode> n = UniquePtr<ParticleEditorResource::ConstNode>::create(m_allocator, *m_resource.get());
								n->idx = i;
								m_resource->m_nodes.push(n.move());
								pushUndo(0xffFFffFF);
							}
						}
						ImGui::EndMenu();
					}
					if (ImGui::Selectable("Cos")) {
						n = addNode(ParticleEditorResource::Node::UNARY_FUNCTION);
						((ParticleEditorResource::UnaryFunctionNode*)n)->func = ParticleEditorResource::UnaryFunctionNode::COS;
					}
					if (ImGui::Selectable("Gradient")) n = addNode(ParticleEditorResource::Node::GRADIENT);
					if (ImGui::Selectable("Gradient color")) n = addNode(ParticleEditorResource::Node::GRADIENT_COLOR);
					if (ImGui::BeginMenu("Input")) {
						for (u8 i = 0; i < m_resource->m_streams.size(); ++i) {
							if (ImGui::Selectable(m_resource->m_streams[i].name)) {
								UniquePtr<ParticleEditorResource::InputNode> n = UniquePtr<ParticleEditorResource::InputNode>::create(m_allocator, *m_resource.get());
								n->idx = i;
								m_resource->m_nodes.push(n.move());
								pushUndo(0xffFFffFF);
							}
						}
						ImGui::EndMenu();
					}
					if (ImGui::Selectable("Literal")) n = addNode(ParticleEditorResource::Node::LITERAL);
					if (ImGui::Selectable("Divide")) n = addNode(ParticleEditorResource::Node::DIV);
					if (ImGui::Selectable("Multiply")) n = addNode(ParticleEditorResource::Node::MUL);
					if (ImGui::Selectable("Multiply add")) n = addNode(ParticleEditorResource::Node::MADD);
					if (ImGui::Selectable("Random")) n = addNode(ParticleEditorResource::Node::RANDOM);
					if (ImGui::Selectable("Sin")) {
						n = addNode(ParticleEditorResource::Node::UNARY_FUNCTION);
						((ParticleEditorResource::UnaryFunctionNode*)n)->func = ParticleEditorResource::UnaryFunctionNode::SIN;
					}
					if (ImGui::Selectable("Vec3")) n = addNode(ParticleEditorResource::Node::VEC3);
					if (n) {
						n->m_pos = cp - editor_pos - ImGuiEx::GetNodeEditorOffset();
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
					pushUndo(0xffFFffFF);
				}

				if (m_context_link != -1 && ImGui::Selectable("Remove link")) {
					m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
						return link.id == m_context_link;
					});
					pushUndo(0xffFFffFF);
				}
				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();

			if (context_open) {
				m_context_link = hovered_link;
				m_context_node = hovered_node;
			}

			m_canvas.end();
		}

		ImGui::Columns();

		ImGui::End();
	}

	ParticleEditorResource::Node* addNode(ParticleEditorResource::Node::Type type) {
		ParticleEditorResource::Node* n = m_resource->addNode(type);
		pushUndo(0xffFFffFF);
		return n;
	}

	void pushUndo(u32 tag) {
		m_resource->generate();
		if (m_autoapply) apply();
		m_dirty = true;
		
		while (m_undo_stack.size() > (i32)m_undo_idx + 1) {
			m_undo_stack.pop();
		}

		if (tag == 0xffFFffFF || tag != m_undo_stack.back().tag) {
			UndoRecord& rec = m_undo_stack.emplace(m_allocator);
			m_resource->serialize(rec.data);
			rec.tag = tag;
		}
		else {
			m_undo_stack.back().data.clear();
			m_resource->serialize(m_undo_stack.back().data);
		}
		m_undo_idx = m_undo_stack.size() - 1;
	}

	void loadFromEntity() {
		const ParticleEmitter* emitter = getSelectedEmitter();
		ASSERT(emitter);

		const Path& path = emitter->getResource()->getPath();
		FileSystem& fs = m_app.getEngine().getFileSystem();
		load(StaticString<LUMIX_MAX_PATH>(fs.getBasePath(), path.c_str()));
	}

	void load(const char* path) {
		if (!path || path[0] == '\0') {
			load();
			return;
		}
		os::InputFile file;
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
			m_undo_stack.clear();
			m_undo_idx = m_undo_stack.size() - 1;
			pushUndo(0xffFFffFF);
			m_dirty = false;
		}
		else {
			logError("Failed to open ", path);
		}
	}

	void load() {
		if (m_dirty) {
			m_confirm_load = true;
			m_confirm_load_path = "";
			return;
		}
		char path[LUMIX_MAX_PATH];
		if (!os::getOpenFilename(Span(path), "Particles\0*.par\0", nullptr)) return;
		load(path);
	}

	void saveAs() {
		char path[LUMIX_MAX_PATH];
		if (!os::getSaveFilename(Span(path), "Particles\0*.par\0", "par")) return;

		save(path);
	}

	void save(const char* path) {
		OutputMemoryStream blob(m_allocator);
		m_resource->serialize(blob);

		os::OutputFile file;
		if (file.open(path)) {
			if (!file.write(blob.data(), blob.size())) {
				logError("Failed to write ", path);
			}
			else {
				m_path = path;
				m_dirty = false;
			}
			file.close();
		}
		else {
			logError("Failed to open ", path);
		}
	}

	void newGraph() {
		if (m_dirty) {
			m_confirm_new = true;
			return;
		}

		m_undo_stack.clear();
		m_undo_idx = -1;
		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		m_resource->initDefault();
		m_path = "";
		pushUndo(0xffFFffFF);
		m_dirty = false;
	}

	const char* getName() const override { return "Particle editor"; }

	void open(const char* path) override {
		m_is_focus_requested = true;
		m_open = true;
		if (m_dirty) {
			m_confirm_load = true;
			m_confirm_load_path = path;
			return;
		}
		
		FileSystem& fs = m_app.getEngine().getFileSystem();
		load(StaticString<LUMIX_MAX_PATH>(fs.getBasePath(), path));
	}

	bool compile(InputMemoryStream& input, OutputMemoryStream& output, const char* path) override {
		ParticleEditorResource res(m_allocator);
		if (!res.deserialize(input, path)) return false;

		res.generate();

		ParticleEmitterResource::Header header;
		output.write(header);
		output.writeString(res.m_mat_path); // material
		const u32 count = u32(res.m_update.size() + res.m_emit.size() + res.m_output.size());
		output.write(count);
		output.write(res.m_update.data(), res.m_update.size());
		output.write(res.m_emit.data(), res.m_emit.size());
		output.write(res.m_output.data(), res.m_output.size());
		output.write((u32)res.m_update.size());
		output.write(u32(res.m_update.size() + res.m_emit.size()));

		auto getCount = [](const auto& x){
			u32 c = 0;
			for (const auto& i : x) c += ParticleEditorResource::getCount(i.type);
			return c;
		};

		output.write(getCount(res.m_streams));
		output.write((u32)res.m_registers_count);
		output.write(getCount(res.m_outputs));
		return true;
	}

	struct UndoRecord {
		UndoRecord(IAllocator& allocator) : data(allocator) {}
		OutputMemoryStream data;
		u32 tag;
	};

	IAllocator& m_allocator;
	StudioApp& m_app;
	StaticString<LUMIX_MAX_PATH> m_path;
	Array<UndoRecord> m_undo_stack;
	bool m_dirty = false;
	bool m_confirm_new = false;
	bool m_confirm_load = false;
	StaticString<LUMIX_MAX_PATH> m_confirm_load_path;
	i32 m_undo_idx = 0;
	UniquePtr<ParticleEditorResource> m_resource;
	bool m_open = false;
	bool m_autoapply = false;
	int m_context_link;
	int m_context_node;
	bool m_is_focus_requested = false; 
	Action m_toggle_ui;
	Action m_undo_action;
	Action m_redo_action;
	Action m_apply_action;
	bool m_has_focus = false;
	ImGuiCanvas m_canvas;
	ImVec2 m_offset = ImVec2(0, 0);
};


DataStream ParticleEditorResource::NodeInput::generate(OutputMemoryStream& instructions, DataStream output, u8 subindex) const {
	return node ? node->generate(instructions, output_idx, output, subindex) : DataStream();
}

UniquePtr<ParticleEditor> ParticleEditor::create(StudioApp& app) {
	IAllocator& allocator = app.getAllocator();
	return UniquePtr<ParticleEditorImpl>::create(allocator, app, allocator);
}


} // namespace Lumix