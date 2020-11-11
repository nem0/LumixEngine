#pragma once

#define LUMIX_NO_CUSTOM_CRT
#include "editor/studio_app.h"
#include "engine/string.h"
#include "editor/settings.h"
#include "editor/utils.h"
#include "imgui/imgui.h"
#include "imgui/imnodes.h"

namespace Lumix {

struct Header {
	const u32 magic = '_LPE';
	u32 version = 0;
};

struct Register {
	enum Type : u8 {
		STREAM,
		REGISTER,
		CONST
	};
	Type type;
	u8 idx;
};

struct Instr {
	enum Type : u8 {
		MUL,
		ADD
	};
	Type type;
	Register dst;
	Register op0;
	Register op1;
};

struct ParticleEditor : StudioApp::GUIPlugin {
	static constexpr u32 OUTPUT_FLAG = 1 << 31;
	
	struct Node;

	struct NodeInput {
		Node* node;
		u8 output_idx;
		Register generate(Array<Instr>& instructions, Register output) const;
	};

	struct Node {
		enum Type {
			OUTPUT,
			INPUT,
			MUL,
			ADD,
			CONST
		};

		Node(ParticleEditor& editor) 
			: m_editor(editor)
			, m_id(editor.genID())
		{}
		virtual ~Node() {}

		virtual Register generate(Array<Instr>& instructions, u8 output_idx, Register output) = 0;
		virtual void serialize(OutputMemoryStream& blob) {}
		virtual void deserialize(InputMemoryStream& blob) {}

		NodeInput getInput(u8 input_idx) {
			for (const ParticleEditor::Link& link : m_editor.m_links) {
				if (link.toNode() != m_id) continue;
				if (link.toPin() != input_idx) continue;

				NodeInput res;
				res.output_idx = link.fromPin();
				res.node = m_editor.getNodeByID(link.fromNode());
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
			imnodes::BeginNode(m_id);
			bool res = onGUI();
			imnodes::EndNode();
			return res;
		}

		u16 id() const { return m_id; }
	
	protected:
		virtual bool onGUI() = 0;
		
		u16 m_id;
		ParticleEditor& m_editor;
		u8 m_input_counter;
		u8 m_output_counter;
	};

	struct ConstNode : Node {
		ConstNode(ParticleEditor& editor) : Node(editor) {}
		
		Register generate(Array<Instr>& instructions, u8 output_idx, Register) override {
			Register r;
			r.type = Register::CONST;
			r.idx = idx;
			return r;
		}

		bool onGUI() override {
			beginOutput();
			ImGui::TextUnformatted(m_editor.m_consts[idx].name);
			endOutput();
			return false;
		}

		u8 idx;
	};

	struct InputNode : Node {
		InputNode(ParticleEditor& editor) 
			: Node(editor)
		{}

		Register generate(Array<Instr>& instructions, u8 output_idx, Register) override {
			Register r;
			r.type = Register::STREAM;
			r.idx = idx;
			return r;
		}

		bool onGUI() override {
			beginOutput();
			if (idx < m_editor.m_streams.size()) {
				ImGui::TextUnformatted(m_editor.m_streams[idx].name);
			}
			else {
				ImGui::TextUnformatted(ICON_FA_EXCLAMATION "Deleted input");
			}
			endOutput();
			return false;
		}

		u8 idx;
	};

	struct OutputNode : Node {
		OutputNode(ParticleEditor& editor) : Node(editor) {}

		bool onGUI() override {
			for (const Stream& stream : m_editor.m_streams) {
				beginInput();
				ImGui::TextUnformatted(stream.name);
				endInput();
			}
			return false;
		}

		Register generate(Array<Instr>& instructions, u8 output_idx, Register) override {
			for (i32 i = 0; i < m_editor.m_streams.size(); ++i) {
				const NodeInput input = getInput(i);
				Register s;
				s.type = Register::STREAM;
				s.idx = i;
				input.generate(instructions, s);
			}
			return {};
		}
	};


	template <Instr::Type OP_TYPE>
	struct BinaryOpNode : Node {
		BinaryOpNode(ParticleEditor& editor) : Node(editor) {}

		Register generate(Array<Instr>& instructions, u8 output_idx, Register output) override {
			ASSERT(output_idx == 0);
			const NodeInput input0 = getInput(0);
			const NodeInput input1 = getInput(1);
			
			Instr i;
			i.op0.type = Register::REGISTER;
			i.op0.idx = 0;

			i.op0 = input0.generate(instructions, i.op0);

			i.op1.idx = 1;
			i.op1 = input1.generate(instructions, i.op1);

			i.type = OP_TYPE;
			i.dst = output;

			instructions.push(i);
			return output;
		}

		bool onGUI() override {
			imnodes::BeginNodeTitleBar();
			beginOutput();
			switch(OP_TYPE) {
				case Instr::Type::MUL: ImGui::TextUnformatted("Multiply"); break;
				case Instr::Type::ADD: ImGui::TextUnformatted("Add"); break;
				default: ASSERT(false); break;
			}
			endOutput();
			imnodes::EndNodeTitleBar();

			beginInput();
			ImGui::TextUnformatted("A");
			endInput();

			beginInput();
			ImGui::TextUnformatted("B");
			endInput();

			return false;
		}
	};

	ParticleEditor(StudioApp& app, IAllocator& allocator)
		: m_allocator(allocator)
		, m_app(app)
		, m_nodes(m_allocator)
		, m_links(m_allocator)
		, m_streams(m_allocator)
		, m_instructions(m_allocator)
		, m_consts(m_allocator)
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

	Node* getNodeByID(u16 id) const {
		for (UniquePtr<Node>& node : m_nodes) {
			if (node->id() == id) return node.get();
		}
		return nullptr;
	}

	void leftColumnGUI() {
		if (ImGui::CollapsingHeader("Streams", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (Stream& s : m_streams) {
				ImGui::PushID(&s);
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##v", s.name.data, sizeof(s.name.data));
				ImGui::PopID();
			}
			if (ImGui::Button("Add##add_stream")) {
				m_streams.emplace();
			}
		}
		if (m_code.size() > 0 && ImGui::CollapsingHeader("Source", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::InputTextMultiline("Code", (char*)m_code.data(), m_code.size());
		}
	}

	void onWindowGUI() override {
		if (!m_open) return;
		if (!ImGui::Begin("Particle editor", &m_open)) {
			ImGui::End();
			return;
		}

		ImGui::Columns(2);
		
		leftColumnGUI();
		
		ImGui::NextColumn();
		bool context_open = false;
		imnodes::BeginNodeEditor();

		if (imnodes::IsEditorHovered() && ImGui::IsMouseClicked(1)) {
			ImGui::OpenPopup("context_menu");
			context_open = true;
		}

		if (ImGui::BeginPopup("context_menu")) {
			if (ImGui::BeginMenu("Add")) {
				if (ImGui::Selectable("Add")) addNode(Node::ADD);
				if (ImGui::Selectable("Multiply")) addNode(Node::MUL);
				if (ImGui::BeginMenu("Input")) {
					for (u8 i = 0; i < m_streams.size(); ++i) {
						if (ImGui::Selectable(m_streams[i].name)) {
							UniquePtr<InputNode> n = UniquePtr<InputNode>::create(m_allocator, *this);
							n->idx = i;
							m_nodes.push(n.move());
							pushUndo();
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (m_context_link && ImGui::Selectable("Remove link")) {
				m_links.eraseItems([&](const Link& link){
					return link.id == m_context_link;
				});
				pushUndo();
			}
			ImGui::EndPopup();
		}

		for (UniquePtr<Node>& n : m_nodes) {
			if (n->onNodeGUI()) {
				pushUndo();
			}
		}

		for (const Link& link : m_links) {
			imnodes::Link(link.id, link.from, link.to);
		}

		imnodes::EndNodeEditor();

		if (context_open) {
			m_context_link = -1;
			imnodes::IsLinkHovered(&m_context_link);
			m_context_node = -1;
			imnodes::IsLinkHovered(&m_context_node);
		}

		{
			int from, to;
			if (imnodes::IsLinkCreated(&from, &to)) {
				Link& link = m_links.emplace();
				link.id = genID();
				link.from = from;
				link.to = to;
				pushUndo();
			}
		}
		ImGui::Columns();

		ImGui::End();
	}

	void addNode(Node::Type type) {
		UniquePtr<Node> node;
		switch(type) {
			case Node::INPUT: node = UniquePtr<InputNode>::create(m_allocator, *this); break;
			case Node::OUTPUT: node = UniquePtr<OutputNode>::create(m_allocator, *this); break;
			case Node::MUL: node = UniquePtr<BinaryOpNode<Instr::MUL>>::create(m_allocator, *this); break;
			case Node::ADD: node = UniquePtr<BinaryOpNode<Instr::ADD>>::create(m_allocator, *this); break;
			case Node::CONST: node = UniquePtr<ConstNode>::create(m_allocator, *this); break;
			default: ASSERT(false);
		}
		m_nodes.push(node.move());
		pushUndo();
	}

	void pushUndo() {
		generate();
		toString(m_code);
	} // TODO

	void save(OutputMemoryStream& blob) {
		Header header;
		blob.write(header);
		blob.write(m_last_id);
		
		blob.write((i32)m_links.size());
		blob.write(m_links.begin(), m_links.byte_size());

		blob.write((i32)m_nodes.size());
		for (const UniquePtr<Node>& n : m_nodes) {
			n->serialize(blob);
		}
	}

	void generate() {
		m_instructions.clear();
		m_nodes[0]->generate(m_instructions, 0, {});
	}

	void newGraph() {
		m_links.clear();
		m_nodes.clear();
		m_last_id = 0;
		m_streams.clear();
		m_consts.clear();
		m_instructions.clear();
		m_streams.emplace().name = "pos_x";
		m_streams.emplace().name = "pos_y";
		m_streams.emplace().name = "pos_z";

		UniquePtr<OutputNode> n = UniquePtr<OutputNode>::create(m_allocator, *this);
		m_nodes.push(n.move());
	}

	void toString(OutputMemoryStream& text, Register r) const {
		switch(r.type) {
			case Register::STREAM: text << m_streams[r.idx].name; break;
			case Register::REGISTER: text << "r" << r.idx; break;
			case Register::CONST: text << m_consts[r.idx].name; break;
			default: ASSERT(false); break;
		}
	}

	void toString(OutputMemoryStream& text) const {
		text.clear();
		for (const Instr& i : m_instructions) {
			switch (i.type) {
				case Instr::ADD: text << "add("; break;
				case Instr::MUL: text << "mul("; break;
				default: ASSERT(false); break;
			}

			toString(text, i.dst);
			text << ", ";
			toString(text, i.op0);
			text << ", ";
			toString(text, i.op1);
			text << ")\n";
		}
		text.write((char)0);
	}

	u16 genID() { return ++m_last_id; }

	const char* getName() const override { return "Particle editor"; }

	struct Stream {
		StaticString<32> name;
	};

	struct Constant {
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


	IAllocator& m_allocator;
	StudioApp& m_app;
	Array<Stream> m_streams;
	Array<Instr> m_instructions;
	Array<Constant> m_consts;
	Array<UniquePtr<Node>> m_nodes;
	Array<Link> m_links;
	OutputMemoryStream m_code;
	int m_last_id = 1;
	bool m_open = false;
	int m_context_link;
	int m_context_node;
	Action m_toggle_ui;
};

Register ParticleEditor::NodeInput::generate(Array<Instr>& instructions, Register output) const {
	return node ? node->generate(instructions, output_idx, output) : Register();
}

UniquePtr<StudioApp::GUIPlugin> createParticleEditor(StudioApp& app) {
	return UniquePtr<ParticleEditor>::create(app.getAllocator(), app, app.getAllocator());
}

} // namespace Lumix