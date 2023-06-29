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

static constexpr u32 OUTPUT_FLAG = 1 << 31;
using DataStream = ParticleEmitterResource::DataStream;
using InstructionType = ParticleEmitterResource::InstructionType;

struct ParticleEditorImpl;

struct ParticleEditorResource {
	struct Node;

	enum class Version {
		LINK_ID_REMOVED,
		EMIT_RATE,
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
		u16 output_idx;
		DataStream generate(OutputMemoryStream& instructions, DataStream output, u8 subindex) const;
	};

	struct Node : NodeEditorNode {
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
			CACHE
		};

		Node(ParticleEditorResource& res) 
			: m_resource(res)
		{
			m_id = res.genID();
		}
		virtual ~Node() {}

		virtual Type getType() const = 0;
		virtual DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) = 0;
		virtual void beforeGenerate() {};
		virtual void serialize(OutputMemoryStream& blob) const {}
		virtual void deserialize(InputMemoryStream& blob) {}

		NodeInput getInput(u8 input_idx) {
			for (const Link& link : m_resource.m_links) {
				if (link.getToNode() != m_id) continue;
				if (link.getToPin() != input_idx) continue;

				NodeInput res;
				res.output_idx = link.getFromPin();
				res.node = m_resource.getNodeByID(link.getFromNode());
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

		bool nodeGUI() override {
			m_input_counter = 0;
			m_output_counter = 0;
			const ImVec2 old_pos = m_pos;
			ImGuiEx::BeginNode(m_id, m_pos, &m_selected);
			bool res = onGUI();
			ImGuiEx::EndNode();
			return res || old_pos.x != m_pos.x || old_pos.y != m_pos.y;
		}

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

		void serialize(OutputMemoryStream& blob) const override {}
		void deserialize(InputMemoryStream& blob) override {}
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }
		
		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
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
	
	struct MeshNode : Node {
		MeshNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::MESH; }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }
		
		DataStream generate(OutputMemoryStream& ip, u16 output_idx, DataStream dst, u8 subindex) override {
			dst = m_resource.streamOrRegister(dst);
			ip.write(InstructionType::MESH);
			ip.write(dst);
			ip.write(subindex);
			return dst;
		}

		bool onGUI() override {
			ImGuiEx::NodeTitle("Mesh");
			outputSlot();
			ImGui::TextUnformatted("Position");
			return false;
		}
	};

	struct CacheNode : Node {
		CacheNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::CACHE; }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }
		
		void beforeGenerate() override { m_cached = {}; }

		DataStream generate(OutputMemoryStream& ip, u16 output_idx, DataStream dst, u8 subindex) override {
			if (m_cached.type == DataStream::NONE) {
				const NodeInput input = getInput(0);
				if (!input.node) return {};

				DataStream op0;
				op0 = input.generate(ip, op0, subindex);

				m_cached = m_resource.streamOrRegister({});
				ip.write(InstructionType::MOV);
				ip.write(m_cached);
				ip.write(op0);
				m_resource.freeRegister(op0);

				ip.write(InstructionType::MOV);
				dst = m_resource.streamOrRegister(dst);
				ip.write(dst);
				ip.write(m_cached);
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
		SplineNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::SPLINE; }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }
		
		DataStream generate(OutputMemoryStream& ip, u16 output_idx, DataStream dst, u8 subindex) override {
			const NodeInput input = getInput(0);
			if (!input.node) return {};

			DataStream op0;
			op0 = input.generate(ip, op0, 0);

			dst = m_resource.streamOrRegister(dst);
			ip.write(InstructionType::SPLINE);
			ip.write(dst);
			ip.write(op0);
			ip.write(subindex);
			m_resource.freeRegister(op0);
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
		GradientColorNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::GRADIENT_COLOR; }
	
		DataStream generate(OutputMemoryStream& ip, u16 output_idx, DataStream dst, u8 subindex) override {
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
		CurveNode(ParticleEditorResource& res) : Node(res) {}
		Type getType() const override { return Type::CURVE; }
	
		DataStream generate(OutputMemoryStream& ip, u16 output_idx, DataStream dst, u8 subindex) override {
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

		void serialize(OutputMemoryStream& blob) const override { blob.write(count); blob.write(keys); blob.write(values); }
		void deserialize(InputMemoryStream& blob) override { blob.read(count); blob.read(keys); blob.read(values); }

		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

		bool onGUI() override {
			ImGuiEx::NodeTitle("Gradient");

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
		float keys[8] = {};
		float values[8] = {};
	};

	struct ConstNode : Node {
		ConstNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::CONST; }

		void serialize(OutputMemoryStream& blob) const override { blob.write(idx); }
		void deserialize(InputMemoryStream& blob) override { blob.read(idx); }
		
		bool hasInputPins() const override { return false; }
		bool hasOutputPins() const override { return true; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream, u8 subindex) override {
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

	struct NoiseNode : Node {
		NoiseNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::NOISE; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
			const NodeInput input0 = getInput(0);
			if (!input0.node) return output;
			DataStream op0;
			op0 = input0.generate(instructions, op0, subindex);

			instructions.write(InstructionType::NOISE);
			DataStream dst = m_resource.streamOrRegister(output);
			instructions.write(dst);
			instructions.write(op0);
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
		RandomNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::RANDOM; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
			instructions.write(InstructionType::RAND);
			DataStream dst;
			dst = m_resource.streamOrRegister(output);
			instructions.write(dst);
			instructions.write(from);
			instructions.write(to);
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
		LiteralNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::NUMBER; }
		bool hasInputPins() const override { return false; }
		bool hasOutputPins() const override { return true; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream, u8 subindex) override {
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
		VectorNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return T; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
			const NodeInput input = getInput(subindex);
			if (input.node) {
				return input.generate(instructions, output, subindex);
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

	struct InputNode : Node {
		InputNode(ParticleEditorResource& res) : Node(res) {}

		Type getType() const override { return Type::INPUT; }
		bool hasInputPins() const override { return false; }
		bool hasOutputPins() const override { return true; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream, u8 subindex) override {
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

		DataStream generate(OutputMemoryStream& instructions, u16, DataStream, u8 subindex) override {
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
		}
		ASSERT(false);
		return 1;
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
		}
		ASSERT(false);
		return c;
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

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream, u8 subindex) override {
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

		void serialize(OutputMemoryStream& blob) const override { blob.write(op); blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(op); blob.read(value); }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream, u8 subindex) override {
			const NodeInput input0 = getInput(0);
			const NodeInput input1 = getInput(1);
			if (!input0.node) return {};

			DataStream i0 = input0.generate(instructions, {}, subindex);
			DataStream i1 = input1.node ? input1.generate(instructions, {}, subindex) : DataStream{};
			switch (op) {
				case LT: instructions.write(InstructionType::LT); break;
				case GT: instructions.write(InstructionType::GT); break;
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

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
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
		
		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
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

		DataStream generate(OutputMemoryStream& instructions, u16, DataStream, u8 subindex) override {
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

		DataStream generate(OutputMemoryStream& instructions, u16, DataStream output, u8 subindex) override {
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
		MaddNode(ParticleEditorResource& res) : Node(res) {}
		
		Type getType() const override { return Type::MADD; }

		void serialize(OutputMemoryStream& blob) const override { blob.write(value1); blob.write(value2); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value1); blob.read(value2); }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

		DataStream generate(OutputMemoryStream& instructions, u16, DataStream output, u8 subindex) override {
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
				case InstructionType::SUB: return Type::SUB;
				case InstructionType::MOD: return Type::MOD;
				default: ASSERT(false); return Type::MUL;
			}
		}

		void serialize(OutputMemoryStream& blob) const override { blob.write(value); }
		void deserialize(InputMemoryStream& blob) override { blob.read(value); }
		bool hasInputPins() const override { return true; }
		bool hasOutputPins() const override { return true; }

		DataStream generate(OutputMemoryStream& instructions, u16 output_idx, DataStream output, u8 subindex) override {
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

	using Link = NodeEditorLink;

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
	
	~ParticleEditorResource() {
		for (Node* node : m_nodes) LUMIX_DELETE(m_allocator, node);
	}
	
	void colorLinks(ImU32 color, u32 link_idx) {
		m_links[link_idx].color = color;
		const u16 from_node_id = m_links[link_idx].getFromNode();
		for (u32 i = 0, c = m_links.size(); i < c; ++i) {
			if (m_links[i].getToNode() == from_node_id) colorLinks(color, i);
		}
	}
	
	const ParticleEditorResource::Node* getNode(u16 id) const {
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
	
		for (ParticleEditorResource::Link& l : m_links) {
			l.color = IM_COL32(0xA0, 0xA0, 0xA0, 0xFF);
		}

		for (u32 i = 0, c = m_links.size(); i < c; ++i) {
			const ParticleEditorResource::Link& link = m_links[i];
			const ParticleEditorResource::Node* node = getNode(link.getToNode());
			switch(node->getType()) {
				case ParticleEditorResource::Node::UPDATE:
				case ParticleEditorResource::Node::EMIT:
				case ParticleEditorResource::Node::OUTPUT:
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

	Node* addNode(Node::Type type) {
		Node* node;
		switch(type) {
			case Node::CMP: node = LUMIX_NEW(m_allocator, CompareNode)(*this); break;
			case Node::MESH: node = LUMIX_NEW(m_allocator, MeshNode)(*this); break;
			case Node::SPLINE: node = LUMIX_NEW(m_allocator, SplineNode)(*this); break;
			case Node::NOISE: node = LUMIX_NEW(m_allocator, NoiseNode)(*this); break;
			case Node::CACHE: node = LUMIX_NEW(m_allocator, CacheNode)(*this); break;
			case Node::GRADIENT_COLOR: node = LUMIX_NEW(m_allocator, GradientColorNode)(*this); break;
			case Node::CURVE: node = LUMIX_NEW(m_allocator, CurveNode)(*this); break;
			case Node::VEC3: node = LUMIX_NEW(m_allocator, VectorNode<Node::VEC3>)(*this); break;
			case Node::VEC4: node = LUMIX_NEW(m_allocator, VectorNode<Node::VEC4>)(*this); break;
			case Node::COLOR_MIX: node = LUMIX_NEW(m_allocator, ColorMixNode)(*this); break;
			case Node::MADD: node = LUMIX_NEW(m_allocator, MaddNode)(*this); break;
			case Node::SWITCH: node = LUMIX_NEW(m_allocator, SwitchNode)(*this); break;
			case Node::RANDOM: node = LUMIX_NEW(m_allocator, RandomNode)(*this); break;
			case Node::EMIT: node = LUMIX_NEW(m_allocator, EmitNode)(*this); break;
			case Node::UPDATE: node = LUMIX_NEW(m_allocator, UpdateNode)(*this); break;
			case Node::INPUT: node = LUMIX_NEW(m_allocator, InputNode)(*this); break;
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

		if (header.version > Version::EMIT_RATE) {
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
		generate();
		colorLinks();
		return true;
	}

	void serialize(OutputMemoryStream& blob) {
		Header header;
		blob.write(header);
		blob.write(m_last_id);
		blob.writeString(m_mat_path.data);
		blob.write(m_init_emit_count);
		blob.write(m_emit_per_second);
		
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

		m_consts.emplace().name = "delta time";
		m_consts.emplace().name = "emitter time";

		m_nodes.push(LUMIX_NEW(m_allocator, UpdateNode)(*this));
		m_nodes.push(LUMIX_NEW(m_allocator, OutputNode)(*this));
		m_nodes.back()->m_pos = ImVec2(100, 300);
		m_nodes.push(LUMIX_NEW(m_allocator, EmitNode)(*this));
		m_nodes.back()->m_pos = ImVec2(100, 200);
	}

	void generate() {
		m_update.clear();
		m_output.clear();
		m_emit.clear();

		for (Node* n : m_nodes) n->beforeGenerate();

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
	
	void fillVertexDecl(gpu::VertexDecl& decl, Array<String>* attribute_names, IAllocator& allocator) const {
		u32 idx = 0;
		u32 offset = 0;
		for (const ParticleEditorResource::Output& o : m_outputs) {
			switch(o.type) {
				case ParticleEditorResource::ValueType::FLOAT: {
					if (attribute_names) attribute_names->emplace(o.name, allocator);
					decl.addAttribute(idx, offset, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(float);
					break;
				}
				case ParticleEditorResource::ValueType::VEC3: {
					if (attribute_names) attribute_names->emplace(o.name, allocator);
					decl.addAttribute(idx, offset, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec3);
					break;
				}
				case ParticleEditorResource::ValueType::VEC4: {
					if (attribute_names) attribute_names->emplace(o.name, allocator);
					decl.addAttribute(idx, offset, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec4);
					break;
				}
			}
			++idx;
		}
	}

	IAllocator& m_allocator;
	StaticString<LUMIX_MAX_PATH> m_mat_path;
	Array<Stream> m_streams;
	Array<Output> m_outputs;
	Array<Constant> m_consts;
	Array<Node*> m_nodes;
	Array<Link> m_links;
	OutputMemoryStream m_update;
	OutputMemoryStream m_emit;
	OutputMemoryStream m_output;
	int m_last_id = 0;
	u8 m_register_mask = 0;
	u8 m_registers_count = 0;
	u32 m_init_emit_count = 0;
	float m_emit_per_second = 100;
};

struct ParticleEditorImpl : ParticleEditor, NodeEditor {
	using Node = ParticleEditorResource::Node;
	
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
			virtual ParticleEditorResource::Node* create(ParticleEditorResource&) const = 0;
		};

		virtual bool beginCategory(const char* category) { return true; }
		virtual void endCategory() {}
		virtual ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut = 0) = 0;

		ICategoryVisitor& visitType(ParticleEditorResource::Node::Type type, const char* label, char shortcut = 0) {
			struct : INodeCreator {
				ParticleEditorResource::Node* create(ParticleEditorResource& res) const override {
					return res.addNode(type);
				}
				ParticleEditorResource::Node::Type type;
			} creator;
			creator.type = type;
			return visitType(label, creator, shortcut);
		}
	};

	void visitCategories(ICategoryVisitor& visitor) {
		if (visitor.beginCategory("Constants")) {
			for (u8 i = 0; i < m_resource->m_consts.size(); ++i) {
				struct : ICategoryVisitor::INodeCreator {
					ParticleEditorResource::Node* create(ParticleEditorResource& res) const override {
						auto* n = (ParticleEditorResource::ConstNode*)res.addNode(ParticleEditorResource::Node::CONST);
						n->idx = i;
						return n;
					}
					u8 i;
				} creator;
				creator.i = i;
				visitor.visitType(m_resource->m_consts[i].name, creator);
			}
			visitor.endCategory();
		}

		if (visitor.beginCategory("Input")) {
			for (u8 i = 0; i < m_resource->m_streams.size(); ++i) {
				struct : ICategoryVisitor::INodeCreator {
					ParticleEditorResource::Node* create(ParticleEditorResource& res) const override {
						auto* n = (ParticleEditorResource::ConstNode*)res.addNode(ParticleEditorResource::Node::INPUT);
						n->idx = i;
						return n;
					}
					u8 i;
				} creator;
				creator.i = i;
				visitor.visitType(m_resource->m_streams[i].name, creator);
			}
			visitor.endCategory();
		}

		if (visitor.beginCategory("Math")) {
			visitor.visitType(ParticleEditorResource::Node::ADD, "Add", 'A')
			.visitType(ParticleEditorResource::Node::COLOR_MIX, "Color mix")
			.visitType(ParticleEditorResource::Node::COS, "Cos")
			.visitType(ParticleEditorResource::Node::DIV, "Divide", 'D')
			.visitType(ParticleEditorResource::Node::MUL, "Multiply", 'M')
			.visitType(ParticleEditorResource::Node::MADD, "Multiply add")
			.visitType(ParticleEditorResource::Node::SIN, "Sin")
			.visitType(ParticleEditorResource::Node::SUB, "Subtract", 'S')
			.endCategory();
		}

		visitor
			.visitType(ParticleEditorResource::Node::CACHE, "Cache")
			.visitType(ParticleEditorResource::Node::CMP, "Compare")
			.visitType(ParticleEditorResource::Node::CURVE, "Curve", 'C')
			.visitType(ParticleEditorResource::Node::GRADIENT_COLOR, "Gradient color")
			.visitType(ParticleEditorResource::Node::MESH, "Mesh")
			.visitType(ParticleEditorResource::Node::NOISE, "Noise", 'N')
			.visitType(ParticleEditorResource::Node::NUMBER, "Number", '1')
			.visitType(ParticleEditorResource::Node::RANDOM, "Random", 'R')
			.visitType(ParticleEditorResource::Node::SPLINE, "Spline")
			.visitType(ParticleEditorResource::Node::SWITCH, "Switch")
			.visitType(ParticleEditorResource::Node::VEC3, "Vec3", '3')
			.visitType(ParticleEditorResource::Node::VEC4, "Vec4", '4');
	}

	void onCanvasClicked(ImVec2 pos, i32 hovered_link) override {
		struct : ICategoryVisitor {
			ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut = 0) override {
				if (shortcut && os::isKeyDown((os::Keycode)shortcut)) {
					ASSERT(!n);
					n = creator.create(*editor->m_resource);
					ASSERT(n);
				}
				return *this;
			}
			
			ParticleEditorImpl* editor;
			ParticleEditorResource::Node* n = nullptr;
		} visitor;
		visitor.editor = this;
		visitCategories(visitor);
		if (visitor.n) {
			visitor.n->m_pos = pos;
			if (hovered_link >= 0) splitLink(m_resource->m_nodes.back(), m_resource->m_links, hovered_link);
			pushUndo(NO_MERGE_UNDO);
		}
	}
	
	void onLinkDoubleClicked(ParticleEditorResource::Link& link, ImVec2 pos) override {
		Node* n = addNode(ParticleEditorResource::Node::Type::PIN);
		ParticleEditorResource::Link new_link;
		new_link.from = n->m_id | OUTPUT_FLAG; 
		new_link.to = link.to;
		link.to = n->m_id;
		m_resource->m_links.push(new_link);
		n->m_pos = pos;
		pushUndo(0xffFF);
	}

	void onContextMenu(ImVec2 pos) override {
		ParticleEditorResource::Node* n = nullptr;
		ImGui::SetNextItemWidth(150);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
		if (m_filter[0]) {
			struct : ICategoryVisitor {
				ICategoryVisitor& visitType(const char* label, const INodeCreator& creator, char shortcut) override {
					if (n) return *this;
					if (stristr(label, editor->m_filter)) {
						if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::MenuItem(label)) {
							n = creator.create(*editor->m_resource);
							ImGui::CloseCurrentPopup();
						}
					}
					return *this;
				}
			
				ParticleEditorImpl* editor;
				ParticleEditorResource::Node* n = nullptr;
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
					if (ImGui::Selectable(label)) n = creator.create(*editor->m_resource);
					return *this;
				}
			
				ParticleEditorImpl* editor;
				ParticleEditorResource::Node* n = nullptr;
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
					if (n->hasInputPins()) m_resource->m_links.push({u32(m_half_link_start), u32(n->m_id)});
				}
				else {
					if (n->hasOutputPins()) m_resource->m_links.push({u32(n->m_id | OUTPUT_FLAG) , u32(m_half_link_start)});
				}
				m_half_link_start = 0;
			}
			pushUndo(NO_MERGE_UNDO);
		}	
	}

	void deleteSelectedNodes() {
		for (i32 i = m_resource->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_resource->m_nodes[i];
			if (n->m_selected && i > 2) {
				m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
					return link.getFromNode() == n->m_id || link.getToNode() == n->m_id;
				});
				LUMIX_DELETE(m_allocator, n);
				m_resource->m_nodes.swapAndPop(i);
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

	void deleteOutput(u32 output_idx) {
		for (i32 i = m_resource->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_resource->m_nodes[i];
			if (n->getType() == Node::OUTPUT) {
				for (i32 j = m_resource->m_links.size() - 1; j >= 0; --j) {
					ParticleEditorResource::Link& link = m_resource->m_links[j];
					if (link.getToNode() == n->m_id) {
						if (link.getToPin() == output_idx) {
							m_resource->m_links.swapAndPop(j);
						}
						else if (link.getToPin() > output_idx) {
							link.to = link.getToNode() | (u32(link.getToPin() - 1) << 16);
						}
					}
				}
			}
		}
		m_resource->m_outputs.erase(output_idx);
		pushUndo(NO_MERGE_UNDO);
	}

	void deleteStream(u32 stream_idx) {
		for (i32 i = m_resource->m_nodes.size() - 1; i >= 0; --i) {
			Node* n = m_resource->m_nodes[i];
			switch (n->getType()) {
				case Node::UPDATE: {
					auto* node = (ParticleEditorResource::UpdateNode*)n;
					for (i32 j = m_resource->m_links.size() - 1; j >= 0; --j) {
						ParticleEditorResource::Link& link = m_resource->m_links[j];
						if (link.getToNode() == node->m_id) {
							// `stream_idx + 1` because of the "kill" input pin
							if (link.getToPin() == stream_idx + 1) {
								m_resource->m_links.swapAndPop(j);
							}
							else if (link.getToPin() > stream_idx + 1) {
								link.to = link.getToNode() | (u32(link.getToPin() - 1) << 16);
							}
						}
					}
					break;
				}
				case Node::EMIT: {
					auto* node = (ParticleEditorResource::EmitNode*)n;
					for (i32 j = m_resource->m_links.size() - 1; j >= 0; --j) {
						ParticleEditorResource::Link& link = m_resource->m_links[j];
						if (link.getToNode() == node->m_id) {
							if (link.getToPin() == stream_idx) {
								m_resource->m_links.swapAndPop(j);
							}
							else if (link.getToPin() > stream_idx) {
								link.to = link.getToNode() | (u32(link.getToPin() - 1) << 16);
							}
						}
					}
					break;
				}
				case Node::INPUT: {
					auto* node = (ParticleEditorResource::InputNode*)n;
					if (node->idx == stream_idx) {
						m_resource->m_links.eraseItems([&](const ParticleEditorResource::Link& link){
							return link.getFromNode() == n->m_id || link.getToNode() == n->m_id;
						});
						LUMIX_DELETE(m_allocator, n)
						m_resource->m_nodes.swapAndPop(i);
					}
					break;
				}
				default: break;
			}
		}
		m_resource->m_streams.erase(stream_idx);
		pushUndo(NO_MERGE_UNDO);
	}

	void leftColumnGUI() {
		ImGuiEx::Label("Material");
		m_app.getAssetBrowser().resourceInput("material", Span(m_resource->m_mat_path.data), Material::TYPE);
		ImGuiEx::Label("Emit per second");
		ImGui::DragFloat("##eps", &m_resource->m_emit_per_second);
		ImGuiEx::Label("Emit at start");
		ImGui::DragInt("##eas", (i32*)&m_resource->m_init_emit_count);
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
					pushUndo(NO_MERGE_UNDO);
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

		World* world = editor.getWorld();
		ComponentType emitter_type = reflection::getComponentType("particle_emitter");
		RenderModule* module = (RenderModule*)world->getModule(emitter_type);
		const bool has = world->hasComponent(selected[0], emitter_type);
		EntityRef e = selected[0];
		return has ? &module->getParticleEmitter(e) : nullptr;
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
			, getCount(m_resource->m_outputs)
			, m_resource->m_init_emit_count
			, m_resource->m_emit_per_second);
		emitter->getResource()->setMaterial(Path(m_resource->m_mat_path));
	}

	static constexpr const char* WINDOW_NAME = "Particle editor";

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

		ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(WINDOW_NAME, &m_open, ImGuiWindowFlags_MenuBar)) {
			ImGui::End();
			return;
		}
		
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				const ParticleEmitter* emitter = getSelectedEmitter();
				if (ImGui::MenuItem("New")) newGraph();
				if (ImGui::BeginMenu("Open")) {
					char buf[LUMIX_MAX_PATH] = "";
					FilePathHash dummy;
					if (m_app.getAssetBrowser().resourceList(Span(buf), dummy, ParticleEmitterResource::TYPE, false, false)) {
						open(buf);
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Load from entity", nullptr, false, emitter)) loadFromEntity();
				menuItem(m_save_action, true);
				if (ImGui::MenuItem("Save as")) m_show_save_as = true;
				if (const char* path = m_recent_paths.menu(); path) open(path);
				ImGui::Separator();
			
				menuItem(m_apply_action, emitter && emitter->getResource());
				ImGui::MenuItem("Autoapply", nullptr, &m_autoapply, emitter && emitter->getResource());

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				menuItem(m_undo_action, canUndo());
				menuItem(m_redo_action, canRedo());
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		FileSelector& fs = m_app.getFileSelector();
		if (fs.gui("Save As", &m_show_save_as, "par", true)) saveAs(fs.getPath());

		ImGui::Columns(2);
		leftColumnGUI();
		ImGui::NextColumn();
		nodeEditorGUI(m_resource->m_nodes, m_resource->m_links);
		ImGui::Columns();

		ImGui::End();
	}

	ParticleEditorResource::Node* addNode(ParticleEditorResource::Node::Type type) {
		return m_resource->addNode(type);
	}

	void pushUndo(u32 tag) override {
		m_resource->colorLinks();
		m_resource->generate();
		if (m_autoapply) apply();
		m_dirty = true;
		
		SimpleUndoRedo::pushUndo(tag);
	}

	void loadFromEntity() {
		const ParticleEmitter* emitter = getSelectedEmitter();
		ASSERT(emitter);

		const Path& path = emitter->getResource()->getPath();
		load(path.c_str());
	}

	void serialize(OutputMemoryStream& blob) override { m_resource->serialize(blob); }
	
	void deserialize(InputMemoryStream& blob) override { 
		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		m_resource->deserialize(blob, "");
	}

	void load(const char* path) {
		ASSERT(path && path[0] != '\0');

		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream blob(m_allocator);
		if (!fs.getContentSync(Path(path), blob)) {
			logError("Failed to read ", path);
			return;
		}
		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		InputMemoryStream iblob(blob);
		m_resource->deserialize(iblob, path);
		m_path = path;
		clearUndoStack();
		m_recent_paths.push(path);
		pushUndo(NO_MERGE_UNDO);
		m_dirty = false;
	}

	void load() {
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
		m_resource = UniquePtr<ParticleEditorResource>::create(m_allocator, m_allocator);
		m_resource->initDefault();
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
		ParticleEditorResource res(m_allocator);
		if (!res.deserialize(input, path)) return false;

		res.generate();

		ParticleEmitterResource::Header header;
		output.write(header);
		gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
		m_resource->fillVertexDecl(decl, nullptr, m_allocator);
		output.write(decl);
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
		output.write(res.m_init_emit_count);
		output.write(res.m_emit_per_second);
		return true;
	}

	IAllocator& m_allocator;
	StudioApp& m_app;
	Path m_path;
	bool m_show_save_as = false;
	bool m_dirty = false;
	bool m_confirm_new = false;
	bool m_confirm_load = false;
	StaticString<LUMIX_MAX_PATH> m_confirm_load_path;
	UniquePtr<ParticleEditorResource> m_resource;
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

DataStream ParticleEditorResource::NodeInput::generate(OutputMemoryStream& instructions, DataStream output, u8 subindex) const {
	return node ? node->generate(instructions, output_idx, output, subindex) : DataStream();
}

gpu::VertexDecl ParticleEditor::getVertexDecl(const char* path, Array<String>& attribute_names, StudioApp& app) {
	attribute_names.clear();
	gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
	ParticleEditorResource res(app.getAllocator());
	OutputMemoryStream blob(app.getAllocator());
	if (app.getEngine().getFileSystem().getContentSync(Path(path), blob)) {
		InputMemoryStream tmp(blob);
		if (res.deserialize(tmp, path)) {
			res.fillVertexDecl(decl, &attribute_names, app.getAllocator());
		}
		else {
			logError("Failed to parse ", path);
		}
	}
	else {
		logError("Failed to load ", path);
	}
	return decl;
}

UniquePtr<ParticleEditor> ParticleEditor::create(StudioApp& app) {
	IAllocator& allocator = app.getAllocator();
	return UniquePtr<ParticleEditorImpl>::create(allocator, app, allocator);
}


} // namespace Lumix