#define LUMIX_NO_CUSTOM_CRT
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

	enum class Version {
		LINK_ID_REMOVED,
		LAST
	};

	struct Header {
		static constexpr u32 MAGIC = '_LPE';
		const u32 magic = MAGIC;
		Version version = Version::LAST;
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
			NUMBER,
			EMIT,
			UPDATE,
			RANDOM,
			SIN,
			MADD,
			CMP,
			COLOR_MIX,
			GRADIENT,
			GRADIENT_COLOR,
			VEC3,
			DIV,
			PIN,
			COS,
			SWITCH,
			VEC4
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

		virtual bool hasInputPins() const = 0;
		virtual bool hasOutputPins() const = 0;

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
			ImGuiEx::BeginNode(m_id, m_pos, &m_selected);
			bool res = onGUI();
			ImGuiEx::EndNode();
			return res || old_pos.x != m_pos.x || old_pos.y != m_pos.y;
		}

		u16 m_id;
		ImVec2 m_pos = ImVec2(100, 100);
		bool m_selected = false;
	
	protected:
		virtual bool onGUI() = 0;
		
		ParticleEditorResource& m_resource;
		u8 m_input_counter;
		u8 m_output_counter;
	};
	
	template <Node::Type T>
	struct UnaryFunctionNode : Node {
		UnaryFunctionNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return T; }

		void serialize(OutputMemoryStream& blob) override {}
		void deserialize(InputMemoryStream& blob) override {}
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }
		
		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			NodeInput input = getInput(0);
			if (!input.node) return output;
			
			DataStream dst = m_resource.streamOrRegister(output);
			DataStream op0 = input.generate(instructions, {}, subindex);
			switch (T) {
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

		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

		bool onGUI() override {
			ImGui::SetNextItemWidth(120);
			ImGui::BeginGroup();
			inputSlot();
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

		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

		bool onGUI() override {
			ImGuiEx::NodeTitle("Gradient");

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
		
		bool hasInputPins() const override { return false; }
		bool hasOutputPins() const override { return true; }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream, u8 subindex) override {
			DataStream r;
			r.type = DataStream::CONST;
			r.index = idx;
			return r;
		}

		bool onGUI() override {
			outputSlot(); 
			if (m_resource.m_consts.size() <= idx) {
				ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " INVALID CONSTANT");
			}
			else {
				ImGui::TextUnformatted(m_resource.m_consts[idx].name);
			}
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
		LiteralNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::NUMBER; }
		bool hasInputPins() const override { return false; }
		bool hasOutputPins() const override { return true; }

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
			ImGui::SetNextItemWidth(60);
			bool changed = ImGui::DragFloat("##v", &value);
			return changed;
		}

		float value = 0;
	};

	template <Node::Type T>
	struct VectorNode : Node {
		VectorNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return T; }

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

		void serialize(OutputMemoryStream& blob) override { 
			if (T == Node::VEC3) blob.write(value.xyz());
			else blob.write(value);
		}
		void deserialize(InputMemoryStream& blob) override { 
			if (T == Node::VEC3) {
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

			if (T == Node::Type::VEC4) {
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

	struct InputNode : Node {
		InputNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::INPUT; }
		bool hasInputPins() const override { return false; }
		bool hasOutputPins() const override { return true; }

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
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return false; }

		bool onGUI() override {
			ImGuiEx::NodeTitle(ICON_FA_PLUS " Emit", ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
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
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return false; }

		bool onGUI() override {
			ImGuiEx::NodeTitle(ICON_FA_CLOCK " Update", ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));

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

	struct SwitchNode : Node {
		SwitchNode(ParticleEditorResource& res) : Node(res) {}
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }
		Type getType() const override { return Type::SWITCH; }

		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			const NodeInput input = getInput(m_is_on ? 0 : 1);
			if (!input.node) return {};
			return input.generate(instructions, output, subindex);
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
		PinNode(ParticleEditorResource& res) : Node(res) {}
		
		DataStream generate(OutputMemoryStream& instructions, u8 output_idx, DataStream output, u8 subindex) override {
			const NodeInput input = getInput(0);
			if (!input.node) return {};
			return input.generate(instructions, output, subindex);
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
		OutputNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::OUTPUT; }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return false; }

		bool onGUI() override {
			ImGuiEx::NodeTitle(ICON_FA_EYE " Output", ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
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
		MaddNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::MADD; }

		void serialize(OutputMemoryStream& blob) override { blob.write(value1); blob.write(value2); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value1); blob.read(value2); }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

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
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

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

		static const char* getName() {
			switch(OP_TYPE) {
				case InstructionType::DIV: return "Divide";
				case InstructionType::MUL: return "Multiply";
				case InstructionType::ADD: return "Add";
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
		int from;
		int to;
		ImU32 color;

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
			case Node::VEC3: node = UniquePtr<VectorNode<Node::VEC3>>::create(m_allocator, *this); break;
			case Node::VEC4: node = UniquePtr<VectorNode<Node::VEC4>>::create(m_allocator, *this); break;
			case Node::COLOR_MIX: node = UniquePtr<ColorMixNode>::create(m_allocator, *this); break;
			case Node::MADD: node = UniquePtr<MaddNode>::create(m_allocator, *this); break;
			case Node::SWITCH: node = UniquePtr<SwitchNode>::create(m_allocator, *this); break;
			case Node::RANDOM: node = UniquePtr<RandomNode>::create(m_allocator, *this); break;
			case Node::EMIT: node = UniquePtr<EmitNode>::create(m_allocator, *this); break;
			case Node::UPDATE: node = UniquePtr<UpdateNode>::create(m_allocator, *this); break;
			case Node::INPUT: node = UniquePtr<InputNode>::create(m_allocator, *this); break;
			case Node::OUTPUT: node = UniquePtr<OutputNode>::create(m_allocator, *this); break;
			case Node::PIN: node = UniquePtr<PinNode>::create(m_allocator, *this); break;
			case Node::DIV: node = UniquePtr<BinaryOpNode<InstructionType::DIV>>::create(m_allocator, *this); break;
			case Node::MUL: node = UniquePtr<BinaryOpNode<InstructionType::MUL>>::create(m_allocator, *this); break;
			case Node::ADD: node = UniquePtr<BinaryOpNode<InstructionType::ADD>>::create(m_allocator, *this); break;
			case Node::CONST: node = UniquePtr<ConstNode>::create(m_allocator, *this); break;
			case Node::COS: node = UniquePtr<UnaryFunctionNode<Node::COS>>::create(m_allocator, *this); break;
			case Node::SIN: node = UniquePtr<UnaryFunctionNode<Node::SIN>>::create(m_allocator, *this); break;
			case Node::NUMBER: node = UniquePtr<LiteralNode>::create(m_allocator, *this); break;
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
		if (header.version > Version::LAST) {
			logError("Unsupported file version ", path);
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
		if (header.version > Version::LINK_ID_REMOVED) {
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
		for (const Link& link : m_links) {
			blob.write(link.from);
			blob.write(link.to);
		}

		blob.write((i32)m_nodes.size());
		for (const UniquePtr<Node>& n : m_nodes) {
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
		m_outputs.emplace().name = "scale";
		m_outputs.emplace().name = "color";
		m_outputs.back().type = ValueType::VEC4;
		m_outputs.emplace().name = "rotation";
		m_outputs.emplace().name = "frame";

		m_consts.emplace().name = "delta time";

		m_nodes.push(UniquePtr<UpdateNode>::create(m_allocator, *this));
		m_nodes.push(UniquePtr<OutputNode>::create(m_allocator, *this));
		m_nodes.back()->m_pos = ImVec2(100, 300);
		m_nodes.push(UniquePtr<EmitNode>::create(m_allocator, *this));
		m_nodes.back()->m_pos = ImVec2(100, 200);
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
	using Node = ParticleEditorResource::Node;
	ParticleEditorImpl(StudioApp& app, IAllocator& allocator)
		: m_allocator(allocator)
		, m_app(app)
		, m_undo_stack(allocator)
	{
		m_toggle_ui.init("Particle editor", "Toggle particle editor", "particle_editor", "", true);
		m_toggle_ui.func.bind<&ParticleEditorImpl::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ParticleEditorImpl::isOpen>(this);

		m_save_action.init(ICON_FA_SAVE "Save", "Particle editor save", "particle_editor_save", ICON_FA_SAVE, os::Keycode::S, Action::Modifiers::CTRL, true);
		m_save_action.func.bind<&ParticleEditorImpl::save>(this);
		m_save_action.plugin = this;

		m_undo_action.init(ICON_FA_UNDO "Undo", "Particle editor undo", "particle_editor_undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL, true);
		m_undo_action.func.bind<&ParticleEditorImpl::undo>(this);
		m_undo_action.plugin = this;

		m_redo_action.init(ICON_FA_REDO "Redo", "Particle editor redo", "particle_editor_redo", ICON_FA_REDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, true);
		m_redo_action.func.bind<&ParticleEditorImpl::redo>(this);
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

	const ParticleEditorResource::Node* getNode(u16 id) const {
		for(const auto& n : m_resource->m_nodes) {
			if (n->m_id == id) return n.get();
		}
		return nullptr;
	}

	void colorLinks(ImU32 color, u32 link_idx) {
		m_resource->m_links[link_idx].color = color;
		const u16 from_node_id = m_resource->m_links[link_idx].fromNode();
		for (u32 i = 0, c = m_resource->m_links.size(); i < c; ++i) {
			if (m_resource->m_links[i].toNode() == from_node_id) colorLinks(color, i);
		}
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
	
		for (ParticleEditorResource::Link& l : m_resource->m_links) {
			l.color = IM_COL32(0xA0, 0xA0, 0xA0, 0xFF);
		}

		for (u32 i = 0, c = m_resource->m_links.size(); i < c; ++i) {
			const ParticleEditorResource::Link& link = m_resource->m_links[i];
			const ParticleEditorResource::Node* node = getNode(link.toNode());
			switch(node->getType()) {
				case ParticleEditorResource::Node::UPDATE:
				case ParticleEditorResource::Node::EMIT:
				case ParticleEditorResource::Node::OUTPUT:
					colorLinks(colors[link.toPin() % lengthOf(colors)], i);
					break;
			}

		}		
	}

	void deleteSelectedNodes() {

		for (i32 i = m_resource->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_resource->m_nodes[i].get();
			if (n->m_selected) {
				m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
					return link.fromNode() == n->m_id || link.toNode() == n->m_id;
				});
				m_resource->m_nodes.swapAndPop(i);
			}
		}
		pushUndo(0xffFFffFF);
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
		colorLinks();
	}

	void deleteOutput(u32 output_idx) {
		for (i32 i = m_resource->m_nodes.size() - 1; i >= 0; --i) {
			UniquePtr<Node>& n = m_resource->m_nodes[i];
			switch (n->getType()) {
				case Node::OUTPUT: {
					for (i32 j = m_resource->m_links.size() - 1; j >= 0; --j) {
						ParticleEditorResource::Link& link = m_resource->m_links[j];
						if (link.toNode() == n->m_id) {
							if (link.toPin() == output_idx) {
								m_resource->m_links.swapAndPop(j);
							}
							else if (link.toPin() > output_idx) {
								link.to = link.toNode() | (u32(link.toPin() - 1) << 16);
							}
						}
					}
				}
			}
		}
		m_resource->m_outputs.erase(output_idx);
		pushUndo(0xffFFffFF);
	}

	void deleteStream(u32 stream_idx) {
		for (i32 i = m_resource->m_nodes.size() - 1; i >= 0; --i) {
			UniquePtr<Node>& n = m_resource->m_nodes[i];
			switch (n->getType()) {
				case Node::UPDATE: {
					auto* node = (ParticleEditorResource::UpdateNode*)n.get();
					for (i32 j = m_resource->m_links.size() - 1; j >= 0; --j) {
						ParticleEditorResource::Link& link = m_resource->m_links[j];
						if (link.toNode() == node->m_id) {
							// `stream_idx + 1` because of the "kill" input pin
							if (link.toPin() == stream_idx + 1) {
								m_resource->m_links.swapAndPop(j);
							}
							else if (link.toPin() > stream_idx + 1) {
								link.to = link.toNode() | (u32(link.toPin() - 1) << 16);
							}
						}
					}
					break;
				}
				case Node::EMIT: {
					auto* node = (ParticleEditorResource::EmitNode*)n.get();
					for (i32 j = m_resource->m_links.size() - 1; j >= 0; --j) {
						ParticleEditorResource::Link& link = m_resource->m_links[j];
						if (link.toNode() == node->m_id) {
							if (link.toPin() == stream_idx) {
								m_resource->m_links.swapAndPop(j);
							}
							else if (link.toPin() > stream_idx) {
								link.to = link.toNode() | (u32(link.toPin() - 1) << 16);
							}
						}
					}
					break;
				}
				case Node::INPUT: {
					auto* node = (ParticleEditorResource::InputNode*)n.get();
					if (node->idx == stream_idx) {
						m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
							return link.fromNode() == n->m_id || link.toNode() == n->m_id;
						});
						m_resource->m_nodes.swapAndPop(i);
					}
					break;
				}
			}
		}
		m_resource->m_streams.erase(stream_idx);
		pushUndo(0xffFFffFF);
	}

	void leftColumnGUI() {
		ImGuiEx::Label("Material");
		m_app.getAssetBrowser().resourceInput("material", Span(m_resource->m_mat_path.data), Material::TYPE);
		if (ImGui::CollapsingHeader("Streams", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Stream& s : m_resource->m_streams) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					deleteStream(u32(&s - m_resource->m_streams.begin()));
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
				m_resource->m_streams.emplace();
			}
		}
		if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (ParticleEditorResource::Output& s : m_resource->m_outputs) {
				ImGui::PushID(&s);
				if (ImGui::Button(ICON_FA_TRASH)) {
					deleteOutput(u32(&s - m_resource->m_outputs.begin()));
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
				menuItem(m_save_action, true);
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
			const ImVec2 origin = ImGui::GetCursorScreenPos();

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

			for (i32 i = 0, c = m_resource->m_links.size(); i < c; ++i) {
				ParticleEditorResource::Link& link = m_resource->m_links[i];
				ImGuiEx::NodeLinkEx(link.from, link.to, link.color, ImGui::GetColorU32(ImGuiCol_TabActive));
				if (ImGuiEx::IsLinkHovered()) {
					if (ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyCtrl) {
						if (ImGuiEx::IsLinkStartHovered()) {
							ImGuiEx::StartNewLink(link.to, true);
						}
						else {
							ImGuiEx::StartNewLink(link.from | OUTPUT_FLAG, false);
						}
						m_resource->m_links.erase(i);
						--c;
					}
					const ImVec2 mp = ImGui::GetMousePos() - origin - m_offset;
					if (ImGui::IsMouseDoubleClicked(0)) {
						Node* n = addNode(ParticleEditorResource::Node::Type::PIN);
						ParticleEditorResource::Link new_link;
						new_link.from = n->m_id | OUTPUT_FLAG; 
						new_link.to = link.to;
						link.to = n->m_id;
						m_resource->m_links.push(new_link);
						n->m_pos = mp;
						pushUndo(0xffFF);
					}
					hovered_link = i32(&link - m_resource->m_links.begin());
				}
			}

			bool open_context = false;
			ImGuiID nlf, nlt;
			if (ImGuiEx::GetHalfLink(&nlf)) {
				open_context = true;
				m_half_link_start = nlf;
			}
			else if (ImGuiEx::GetNewLink(&nlf, &nlt)) {
				m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){ return link.to == nlt; });
				ParticleEditorResource::Link& link = m_resource->m_links.emplace();
				link.from = nlf;
				link.to = nlt;
				pushUndo(0xffFFffFF);
			}

			ImGuiEx::EndNodeEditor();
			
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
				if (ImGui::GetIO().KeyAlt && hovered_link != -1) {
					m_resource->m_links.erase(hovered_link);
					pushUndo(0xffFF);
				}
				else {
					static const struct {
						char key;
						Node::Type type;
					} types[] = {
						{ 'A', Node::Type::ADD },
						{ 'D', Node::Type::DIV },
						{ 'G', Node::Type::GRADIENT },
						{ 'M', Node::Type::MUL },
						{ 'R', Node::Type::RANDOM },
						{ '1', Node::Type::NUMBER},
						{ '3', Node::Type::VEC3 },
						{ '4', Node::Type::VEC4 }
					};
					Node* n = nullptr;
					for (const auto& t : types) {
						if (os::isKeyDown((os::Keycode)t.key)) {
							n = addNode(t.type);
							break;
						}
					}
					if (n) {
						n->m_pos = ImGui::GetMousePos() - ImGui::GetItemRectMin() - ImGuiEx::GetNodeEditorOffset();
						pushUndo(0xffFFffFF);
					}
				}
			}

			const ImVec2 editor_pos = ImGui::GetItemRectMin();

			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
				open_context = true;
				m_half_link_start = 0;
			}
			if (open_context) ImGui::OpenPopup("context_menu");
		
			if (ImGui::BeginPopup("context_menu")) {
				ImVec2 cp = ImGui::GetItemRectMin();
				ParticleEditorResource::Node* n = nullptr;
				if (ImGui::Selectable("Add")) n = addNode(ParticleEditorResource::Node::ADD);
				if (ImGui::Selectable("Color mix")) n = addNode(ParticleEditorResource::Node::COLOR_MIX);
				if (ImGui::Selectable("Compare")) n = addNode(ParticleEditorResource::Node::CMP);
				if (ImGui::BeginMenu("Constant")) {
					for (u8 i = 0; i < m_resource->m_consts.size(); ++i) {
						if (ImGui::Selectable(m_resource->m_consts[i].name)) {
							n = addNode(ParticleEditorResource::Node::CONST);
							((ParticleEditorResource::ConstNode*)n)->idx = i;
						}
					}
					ImGui::EndMenu();
				}
				if (ImGui::Selectable("Cos")) n = addNode(ParticleEditorResource::Node::COS);
				if (ImGui::Selectable("Gradient")) n = addNode(ParticleEditorResource::Node::GRADIENT);
				if (ImGui::Selectable("Gradient color")) n = addNode(ParticleEditorResource::Node::GRADIENT_COLOR);
				if (ImGui::BeginMenu("Input")) {
					for (u8 i = 0; i < m_resource->m_streams.size(); ++i) {
						if (ImGui::Selectable(m_resource->m_streams[i].name)) {
							n = addNode(ParticleEditorResource::Node::INPUT);
							((ParticleEditorResource::InputNode*)n)->idx = i;
						}
					}
					ImGui::EndMenu();
				}
				if (ImGui::Selectable("Number")) n = addNode(ParticleEditorResource::Node::NUMBER);
				if (ImGui::Selectable("Divide")) n = addNode(ParticleEditorResource::Node::DIV);
				if (ImGui::Selectable("Multiply")) n = addNode(ParticleEditorResource::Node::MUL);
				if (ImGui::Selectable("Multiply add")) n = addNode(ParticleEditorResource::Node::MADD);
				if (ImGui::Selectable("Random")) n = addNode(ParticleEditorResource::Node::RANDOM);
				if (ImGui::Selectable("Sin")) n = addNode(ParticleEditorResource::Node::SIN);
				if (ImGui::Selectable("Switch")) n = addNode(ParticleEditorResource::Node::SWITCH);
				if (ImGui::Selectable("Vec3")) n = addNode(ParticleEditorResource::Node::VEC3);
				if (ImGui::Selectable("Vec4")) n = addNode(ParticleEditorResource::Node::VEC4);
				if (n) {
					n->m_pos = cp - editor_pos - ImGuiEx::GetNodeEditorOffset();
						
					if (m_half_link_start) {
						if (m_half_link_start & OUTPUT_FLAG) {
							if (n->hasInputPins()) m_resource->m_links.push({i32(m_half_link_start), i32(n->m_id)});
						}
						else {
							if (n->hasOutputPins()) m_resource->m_links.push({i32(n->m_id | OUTPUT_FLAG) , i32(m_half_link_start)});
						}
						m_half_link_start = 0;
					}
					pushUndo(0xffFFffFF);
				}

				ImGui::EndPopup();
			}
		
			m_canvas.end();
		}

		ImGui::Columns();

		ImGui::End();
	}

	ParticleEditorResource::Node* addNode(ParticleEditorResource::Node::Type type) {
		return m_resource->addNode(type);
	}

	void pushUndo(u32 tag) {
		colorLinks();
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

	void save() {
		if (!m_path.isEmpty()) {
			saveAs(m_path.c_str());
			return;
		}
		char path[LUMIX_MAX_PATH];
		if (!os::getSaveFilename(Span(path), "Particles\0*.par\0", "par")) return;
		saveAs(path);
	}

	void saveAs() {
		char path[LUMIX_MAX_PATH];
		if (!os::getSaveFilename(Span(path), "Particles\0*.par\0", "par")) return;

		saveAs(path);
	}

	void saveAs(const char* path) {
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
	Path m_path;
	Array<UndoRecord> m_undo_stack;
	bool m_dirty = false;
	bool m_confirm_new = false;
	bool m_confirm_load = false;
	StaticString<LUMIX_MAX_PATH> m_confirm_load_path;
	i32 m_undo_idx = 0;
	UniquePtr<ParticleEditorResource> m_resource;
	bool m_open = false;
	bool m_autoapply = false;
	bool m_is_focus_requested = false; 
	Action m_toggle_ui;
	Action m_save_action;
	Action m_undo_action;
	Action m_redo_action;
	Action m_apply_action;
	Action m_delete_action;
	bool m_has_focus = false;
	ImGuiEx::Canvas m_canvas;
	ImVec2 m_offset = ImVec2(0, 0);
	ImGuiID m_half_link_start = 0;
};


DataStream ParticleEditorResource::NodeInput::generate(OutputMemoryStream& instructions, DataStream output, u8 subindex) const {
	return node ? node->generate(instructions, output_idx, output, subindex) : DataStream();
}

UniquePtr<ParticleEditor> ParticleEditor::create(StudioApp& app) {
	IAllocator& allocator = app.getAllocator();
	return UniquePtr<ParticleEditorImpl>::create(allocator, app, allocator);
}


} // namespace Lumix