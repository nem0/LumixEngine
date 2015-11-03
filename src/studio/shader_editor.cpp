#include "shader_editor.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/path_utils.h"
#include "core/string.h"
#include "core/system.h"
#include "utils.h"


enum class NodeTypes
{
	OUTPUT,
	FLOAT_CONST,
	COLOR_CONST,
	SAMPLE,
	ATTRIBUTE,
	LERP,
	UNIFORM,
	VEC4_MERGE,
	MULTIPLY
};


static const struct { const char* name; NodeTypes type; } NODE_TYPES[] = {
	{"LERP",						NodeTypes::LERP},
	{"Sample",					NodeTypes::SAMPLE},
	{"Attribute",				NodeTypes::ATTRIBUTE},
	{"Color constant",	NodeTypes::COLOR_CONST},
	{"Float Const",			NodeTypes::FLOAT_CONST},
	{"Uniform",					NodeTypes::UNIFORM},
	{"Vec4 merge",			NodeTypes::VEC4_MERGE},
	{"Multiply",				NodeTypes::MULTIPLY}
};


struct ShaderEditor::ICommand
{
	ICommand(ShaderEditor& editor)
		: m_editor(editor)
	{
	}

	virtual ~ICommand() {}

	virtual void execute() = 0;
	virtual void undo() = 0;
	virtual bool merge(ICommand& command) { return false; }
	virtual uint32_t getType() const = 0;

	ShaderEditor& m_editor;
};


static void removeConnection(ShaderEditor::Node* node, int pin_index, bool is_input)
{
	if(is_input)
	{
		if(!node->m_inputs[pin_index]) return;

		int index = node->m_inputs[pin_index]->m_outputs.indexOf(node);
		ASSERT(index >= 0);

		node->m_inputs[pin_index]->m_outputs[index] = nullptr;
		node->m_inputs[pin_index] = nullptr;
	}
	else
	{
		if(!node->m_outputs[pin_index]) return;

		int index = node->m_outputs[pin_index]->m_inputs.indexOf(node);
		ASSERT(index >= 0);

		node->m_outputs[pin_index]->m_inputs[index] = nullptr;
		node->m_outputs[pin_index] = nullptr;
	}
}


ShaderEditor::Node::Node(int type, ShaderEditor& editor)
	: m_inputs(editor.m_allocator)
	, m_outputs(editor.m_allocator)
	, m_type(type)
	, m_editor(editor)
{
	m_name[0] = 0;
	m_can_have_name = true;
}


void ShaderEditor::Node::onNodeGUI()
{
	ImGui::PushItemWidth(120);
	onGUI();
	if (m_can_have_name) ImGui::InputText("Name", m_name, Lumix::lengthOf(m_name));
	ImGui::PopItemWidth();
}


struct MultiplyNode : public ShaderEditor::Node
{
	MultiplyNode(ShaderEditor& editor)
		: Node((int)NodeTypes::MULTIPLY, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	void save(Lumix::OutputBlob& blob) override {}
	void load(Lumix::InputBlob& blob) override {}

	void generate(FILE* fp) override
	{
		if(!m_inputs[0]) return;
		if(!m_inputs[1]) return;

		m_inputs[0]->generate(fp);
		m_inputs[1]->generate(fp);

		fprintf(fp, "\tvec4 %s = %s * %s;\n", m_name, m_inputs[0]->m_name, m_inputs[1]->m_name);
	}

	void onGUI() override
	{
		ImGui::Text("A");
		ImGui::Text("B");
	}
};


struct Vec4MergeNode : public ShaderEditor::Node
{
	Vec4MergeNode(ShaderEditor& editor)
		: Node((int)NodeTypes::VEC4_MERGE, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	void save(Lumix::OutputBlob& blob) override {}
	void load(Lumix::InputBlob& blob) override {}

	void generate(FILE* fp) override 
	{
		fprintf(fp, "\tvec4 %s;\n", m_name);

		if (m_inputs[0]) 
		{
			m_inputs[0]->generate(fp);
			fprintf(fp, "\t%s.xyz = %s;\n", m_name, m_inputs[0]->m_name);
		}
		if (m_inputs[1])
		{
			m_inputs[1]->generate(fp);
			fprintf(fp, "\t%s.x = %s;\n", m_name, m_inputs[1]->m_name);
		}
		if (m_inputs[2])
		{
			m_inputs[2]->generate(fp);
			fprintf(fp, "\t%s.y = %s;\n", m_name, m_inputs[2]->m_name);
		}
		if (m_inputs[3])
		{
			m_inputs[3]->generate(fp);
			fprintf(fp, "\t%s.z = %s;\n", m_name, m_inputs[3]->m_name);
		}
		if (m_inputs[4])
		{
			m_inputs[4]->generate(fp);
			fprintf(fp, "\t%s.w = %s;\n", m_name, m_inputs[4]->m_name);
		}
	}

	void onGUI() override 
	{
		ImGui::Text("xyz");
		ImGui::Text("x");
		ImGui::Text("y");
		ImGui::Text("z");
		ImGui::Text("w");
	}
};


struct FloatConstNode : public ShaderEditor::Node
{
	FloatConstNode(ShaderEditor& editor)
		: Node((int)NodeTypes::FLOAT_CONST, editor)
	{
		m_value = 0;
		m_outputs.push(nullptr);
	}

	void save(Lumix::OutputBlob& blob) override { blob.write(m_value); }

	void load(Lumix::InputBlob& blob) override { blob.read(m_value); }

	void generate(FILE* fp) override { fprintf(fp, "\tconst float %s = %f;\n", m_name, m_value); }

	void onGUI() override { ImGui::DragFloat("value", &m_value, 0.1f); }

	float m_value;
};


struct ColorConstNode : public ShaderEditor::Node
{
	ColorConstNode(ShaderEditor& editor)
		: Node((int)NodeTypes::COLOR_CONST, editor)
	{
		m_color[0] = m_color[1] = m_color[2] = m_color[3] = 0;
		m_outputs.push(nullptr);
	}

	void save(Lumix::OutputBlob& blob) override { blob.write(m_color); }

	void load(Lumix::InputBlob& blob) override { blob.read(m_color); }

	void generate(FILE* fp) override
	{
		fprintf(fp,
			"\tconst vec4 %s = vec4(%f, %f, %f, %f);\n",
			m_name,
			m_color[0],
			m_color[1],
			m_color[2],
			m_color[3]);
	}

	void onGUI() override { ImGui::ColorEdit4("value", m_color); }

	float m_color[4];
};


struct SampleNode : public ShaderEditor::Node
{
	SampleNode(ShaderEditor& editor)
		: Node((int)NodeTypes::SAMPLE, editor)
	{
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
		m_texture = 0;
	}

	void save(Lumix::OutputBlob& blob) override { blob.write(m_texture); }

	void load(Lumix::InputBlob& blob) override { blob.read(m_texture); }

	void generate(FILE* fp) override
	{
		if (!m_inputs[0])
		{
			fprintf(fp, "\tvec4 %s = vec4(1, 0, 1, 0);\n", m_name);
			return;
		}

		m_inputs[0]->generate(fp);
		fprintf(fp,
			"\tvec4 %s = texture2D(%s, %s);\n",
			m_name,
			m_editor.getTextureName(m_texture),
			m_inputs[0]->m_name);
	}

	void onGUI() override
	{
		ImGui::Text("UV");
		auto getter = [](void* data, int idx, const char** out) -> bool
		{
			*out = ((SampleNode*)data)->m_editor.getTextureName(idx);
			return true;
		};
		ImGui::Combo("Texture", &m_texture, getter, this, ShaderEditor::MAX_TEXTURES_COUNT);
	}

	int m_texture;
};


struct AttributeNode : public ShaderEditor::Node
{
	AttributeNode(ShaderEditor& editor)
		: Node((int)NodeTypes::ATTRIBUTE, editor)
	{
		m_can_have_name = false;
		m_outputs.push(nullptr);
		m_attribute = 0;
	}

	void save(Lumix::OutputBlob& blob) override { blob.write(m_attribute); }

	void load(Lumix::InputBlob& blob) override { blob.read(m_attribute); }

	void generate(FILE* fp) override {}


	void onGUI() override
	{
		auto getter = [](void* data, int idx, const char** out) -> bool
		{
			*out = ((SampleNode*)data)->m_editor.getVertexOutputName(idx);
			return true;
		};
		if (ImGui::Combo(
				"Attribute", &m_attribute, getter, this, ShaderEditor::MAX_VERTEX_OUTPUTS_COUNT))
		{
			Lumix::copyString(m_name, m_editor.getVertexOutputName(m_attribute));
		}
	}

	int m_attribute;
};


struct OutputNode : public ShaderEditor::Node
{
	OutputNode(ShaderEditor& editor)
		: Node((int)NodeTypes::OUTPUT, editor)
	{
		m_can_have_name = false;
		m_inputs.push(nullptr);
	}


	void generate(FILE* fp) override
	{
		if (!m_inputs[0])
		{
			fputs("\tgl_FragColor = vec4(1, 0, 1, 1);\n", fp);
			return;
		}

		m_inputs[0]->generate(fp);
		fputs("\tgl_FragColor = ", fp);
		fputs(m_inputs[0]->m_name, fp);
		fputs(";\n", fp);
	}


	void onGUI() override { ImGui::Text("OUTPUT"); }
};


struct LerpNode : public ShaderEditor::Node
{
	LerpNode(ShaderEditor& editor)
		: Node((int)NodeTypes::LERP, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	void generate(FILE* fp) override
	{
		if (!m_inputs[0] || !m_inputs[1] || !m_inputs[2])
		{
			fprintf(fp, "\tvec4 %s;", m_name);
			return;
		}

		m_inputs[0]->generate(fp);
		m_inputs[1]->generate(fp);
		m_inputs[2]->generate(fp);

		fprintf(fp,
			"\tvec4 %s = mix(%s, %s, %s);\n",
			m_name,
			m_inputs[0]->m_name,
			m_inputs[1]->m_name,
			m_inputs[2]->m_name);
	}

	void onGUI() override
	{
		ImGui::Text("Input 1");
		ImGui::Text("Input 2");
		ImGui::Text("Weight");
	}
};


struct UniformNode : public ShaderEditor::Node
{
	enum Type
	{
		VEC4
	};

	UniformNode(ShaderEditor& editor)
		: Node((int)NodeTypes::UNIFORM, editor)
	{
		m_outputs.push(nullptr);
		m_type = VEC4;
	}

	const char* getTypeName() const
	{
		switch (m_type)
		{
			case VEC4: return "vec4";
			default: ASSERT(false); return "vec4";
		}
	}


	void save(Lumix::OutputBlob& blob) override { blob.write(m_type); }

	void load(Lumix::InputBlob& blob) override { blob.read(m_type); }


	void generateBeforeMain(FILE* fp) override
	{
		fprintf(fp, "uniform %s %s;\n", getTypeName(), m_name);
	}

	void generate(FILE* fp) override {}

	void onGUI() override { ImGui::Combo("Type", (int*)&m_type, "Vec4\0"); }

	Type m_type;
};


ShaderEditor::Node::~Node()
{
	for (auto* output : m_outputs)
	{
		if (output) output->m_inputs.eraseItem(this);
	}

	for (auto* input : m_inputs)
	{
		if (input) input->m_outputs.eraseItem(this);
	}
}



struct MoveNodeCommand : public ShaderEditor::ICommand
{
	MoveNodeCommand(int node, ImVec2 new_pos, ShaderEditor& editor)
		: ICommand(editor)
		, m_node(node)
		, m_new_pos(new_pos)
	{
		m_old_pos = m_editor.getNodeByID(m_node)->pos;
	}


	virtual uint32_t getType() const override
	{
		static const uint32_t crc = Lumix::crc32("move_node");
		return crc;
	}


	void execute() override
	{
		auto* node = m_editor.getNodeByID(m_node);
		node->pos = m_new_pos;
	}


	void undo() override
	{
		auto* node = m_editor.getNodeByID(m_node);
		node->pos = m_old_pos;
	}


	bool merge(ICommand& command) override
	{
		auto& cmd = static_cast<MoveNodeCommand&>(command);
		if(cmd.m_node == m_node)
		{

			m_new_pos = cmd.m_new_pos;
			return true;
		}
		return false;
	}


	int m_node;
	ImVec2 m_new_pos;
	ImVec2 m_old_pos;
};


struct CreateConnectionCommand : public ShaderEditor::ICommand
{
	CreateConnectionCommand(int from, int from_pin, int to, int to_pin, ShaderEditor& editor)
		: ICommand(editor)
		, m_from(from)
		, m_to(to)
		, m_from_pin(from_pin)
		, m_to_pin(to_pin)
	{
		auto* from_node = m_editor.getNodeByID(m_from);
		auto* to_node = m_editor.getNodeByID(m_to);

		auto* before_to = from_node->m_outputs[m_from_pin];
		if(before_to)
		{
			m_before_to_pin = before_to->m_inputs.indexOf(from_node);
			m_before_to = before_to->id;
		}
		else
		{
			m_before_to = m_before_to_pin = -1;
		}

		auto* before_from = to_node->m_inputs[m_from_pin];
		if(before_from)
		{
			m_before_from_pin = before_from->m_outputs.indexOf(to_node);
			m_before_from = before_from->id;
		}
		else
		{
			m_before_from = m_before_from_pin = -1;
		}
	}


	virtual uint32_t getType() const override
	{
		static const uint32_t crc = Lumix::crc32("create_connection");
		return crc;
	}


	virtual void execute() override
	{
		auto* from_node = m_editor.getNodeByID(m_from);
		auto* to_node = m_editor.getNodeByID(m_to);

		removeConnection(from_node, m_from_pin, false);
		removeConnection(to_node, m_to_pin, true);

		from_node->m_outputs[m_from_pin] = to_node;
		to_node->m_inputs[m_to_pin] = from_node;
	}


	virtual void undo() override
	{
		auto* node = m_editor.getNodeByID(m_from);
		removeConnection(node, m_from_pin, false);

		if(m_before_to >= 0)
		{
			auto* before_to = m_editor.getNodeByID(m_before_to);

			node->m_outputs[m_from_pin] = before_to;
			before_to->m_inputs[m_before_to_pin] = node;
		}

		if(m_before_from >= 0)
		{
			auto* before_from = m_editor.getNodeByID(m_before_from);

			auto* to_node = m_editor.getNodeByID(m_to);

			to_node->m_inputs[m_to_pin] = before_from;
			before_from->m_outputs[m_before_from_pin] = to_node;
		}
	}

	int m_before_to;
	int m_before_to_pin;
	int m_before_from;
	int m_before_from_pin;
	int m_from_pin;
	int m_to_pin;
	int m_from;
	int m_to;
};


struct RemoveNodeCommand : public ShaderEditor::ICommand
{
	RemoveNodeCommand(int node_id, ShaderEditor& editor)
		: ICommand(editor)
		, m_node_id(node_id)
		, m_blob(editor.getAllocator())
	{
	}


	virtual uint32_t getType() const override
	{
		static const uint32_t crc = Lumix::crc32("remove_node");
		return crc;
	}


	void execute() override
	{
		auto* node = m_editor.getNodeByID(m_node_id);
		m_editor.saveNode(m_blob, *node);
		m_editor.saveNodeConnections(m_blob, *node);
		m_editor.destroyNode(node);
	}


	void undo() override
	{
		Lumix::InputBlob blob(m_blob);
		auto& node = m_editor.loadNode(blob);
		m_editor.loadNodeConnections(blob, node);
	}


	Lumix::OutputBlob m_blob;
	int m_node_id;
};


struct CreateNodeCommand : public ShaderEditor::ICommand
{
	CreateNodeCommand(int id, NodeTypes type, ImVec2 pos, ShaderEditor& editor)
		: m_type(type)
		, m_pos(pos)
		, m_id(id)
		, ICommand(editor)
	{
	}


	virtual uint32_t getType() const override
	{
		static const uint32_t crc = Lumix::crc32("create_node");
		return crc;
	}


	void execute() override
	{
		m_node = m_editor.createNode((int)m_type);
		m_editor.addNode(m_node, m_pos);
		if(m_id >= 0) m_node->id = m_id;
	}


	void undo() override
	{
		m_id = m_node->id;
		m_editor.destroyNode(m_node);
	}


	int m_id;
	ShaderEditor::Node* m_node;
	NodeTypes m_type;
	ImVec2 m_pos;
};


ShaderEditor::ShaderEditor(Lumix::IAllocator& allocator)
	: m_fragment_nodes(allocator)
	, m_allocator(allocator)
	, m_undo_stack(allocator)
	, m_undo_stack_idx(-1)
	, m_current_node_id(-1)
	, m_is_focused(false)
{
	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		m_textures[i][0] = 0;
	}
	for (int i = 0; i < Lumix::lengthOf(m_vertex_outputs); ++i)
	{
		m_vertex_outputs[i][0] = 0;
	}
	m_last_node_id = 0;
	m_new_link_info.is_active = false;

	m_fragment_nodes.push(LUMIX_NEW(allocator, OutputNode)(*this));
	m_fragment_nodes.back()->pos.x = 250;
	m_fragment_nodes.back()->pos.y = 50;
	m_fragment_nodes.back()->id = ++m_last_node_id;
}


ShaderEditor::~ShaderEditor()
{
	clear();
}


ShaderEditor::Node* ShaderEditor::getNodeByID(int id)
{
	for(auto* node : m_fragment_nodes)
	{
		if(node->id == id) return node;
	}

	return nullptr;
}


void ShaderEditor::generate(const char* path)
{
	char sc_path[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::FileInfo info(path);
	Lumix::copyString(sc_path, info.m_dir);
	Lumix::catString(sc_path, info.m_basename);
	Lumix::catString(sc_path, ".sc");


	FILE* fp = fopen(sc_path, "wb");
	if (!fp) return;

	fputs("$input ", fp);
	bool first = true;
	for (auto* vertex_output : m_vertex_outputs)
	{
		if (!vertex_output[0]) continue;
		if (!first) fputs(", ", fp);
		fputs(vertex_output, fp);
		first = false;
	}
	fputs("\n", fp);

	fputs("#include \"common.sh\"\n", fp);

	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		if (!m_textures[i][0]) continue;

		fprintf(fp, "SAMPLER2D(%s, %d);\n", m_textures[i], i);
	}

	for (auto* node : m_fragment_nodes)
	{
		node->generateBeforeMain(fp);
	}

	fputs("void main() {\n", fp);
	m_fragment_nodes[0]->generate(fp);
	fputs("}\n", fp);

	fclose(fp);
}


void ShaderEditor::addNode(Node* node, const ImVec2& pos)
{
	m_fragment_nodes.push(node);
	node->pos = pos;
	node->id = ++m_last_node_id;
}


void ShaderEditor::nodePinMouseDown(Node* node, int pin_index, bool is_input)
{
	m_new_link_info.from = node;
	m_new_link_info.from_pin_index = pin_index;
	m_new_link_info.is_from_input = is_input;
	m_new_link_info.is_active = true;
}


void ShaderEditor::createConnection(Node* node, int pin_index, bool is_input)
{
	if (!m_new_link_info.is_active) return;
	if (m_new_link_info.is_from_input == is_input) return;

	if (is_input)
	{
		execute(LUMIX_NEW(m_allocator, CreateConnectionCommand)(
			m_new_link_info.from->id, m_new_link_info.from_pin_index, node->id, pin_index, *this));
	}
	else
	{
		execute(LUMIX_NEW(m_allocator, CreateConnectionCommand)(
			node->id, pin_index, m_new_link_info.from->id, m_new_link_info.from_pin_index, *this));
	}
}


void ShaderEditor::saveNode(Lumix::OutputBlob& blob, Node& node)
{
	int type = (int)node.m_type;
	blob.write(node.id);
	blob.write(type);
	blob.write(node.pos);
	int tmp = (int)strlen(node.m_name);
	blob.writeString(node.m_name);

	node.save(blob);
}


void ShaderEditor::saveNodeConnections(Lumix::OutputBlob& blob, Node& node)
{
	int inputs_count = node.m_inputs.size();
	blob.write(inputs_count);
	for(int i = 0; i < inputs_count; ++i)
	{
		int tmp = node.m_inputs[i] ? node.m_inputs[i]->id : -1;
		blob.write(tmp);
		tmp = node.m_inputs[i] ? node.m_inputs[i]->m_outputs.indexOf(&node) : -1;
		blob.write(tmp);
	}

	int outputs_count = node.m_outputs.size();
	blob.write(outputs_count);
	for(int i = 0; i < outputs_count; ++i)
	{
		int tmp = node.m_outputs[i] ? node.m_outputs[i]->id : -1;
		blob.write(tmp);
		tmp = node.m_outputs[i] ? node.m_outputs[i]->m_inputs.indexOf(&node) : -1;
		blob.write(tmp);
	}
}


void ShaderEditor::save(const char* path)
{
	FILE* fp = fopen(path, "wb");
	if (!fp)
	{
		Lumix::g_log_error.log("Shader editor") << "Could not save shader " << path;
		return;
	}

	Lumix::OutputBlob blob(m_allocator);
	blob.reserve(4096);
	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		blob.writeString(m_textures[i]);
	}

	for (int i = 0; i < Lumix::lengthOf(m_vertex_outputs); ++i)
	{
		blob.writeString(m_vertex_outputs[i]);
	}

	int nodes_count = m_fragment_nodes.size();
	blob.write(nodes_count);
	for (auto* node : m_fragment_nodes)
	{
		saveNode(blob, *node);
	}


	for (auto* node : m_fragment_nodes)
	{
		saveNodeConnections(blob, *node);
	}

	fwrite(blob.getData(), blob.getSize(), 1, fp);
	fclose(fp);
}


void ShaderEditor::clear()
{
	for (auto* node : m_fragment_nodes)
	{
		m_allocator.deleteObject(node);
	}
	m_fragment_nodes.clear();

	for(auto* command : m_undo_stack)
	{
		LUMIX_DELETE(m_allocator, command);
	}
	m_undo_stack.clear();
	m_undo_stack_idx = -1;

	m_last_node_id = 0;
}


ShaderEditor::Node* ShaderEditor::createNode(int type)
{
	switch ((NodeTypes)type)
	{
		case NodeTypes::OUTPUT: return LUMIX_NEW(m_allocator, OutputNode)(*this);
		case NodeTypes::ATTRIBUTE: return LUMIX_NEW(m_allocator, AttributeNode)(*this);
		case NodeTypes::COLOR_CONST: return LUMIX_NEW(m_allocator, ColorConstNode)(*this);
		case NodeTypes::FLOAT_CONST: return LUMIX_NEW(m_allocator, FloatConstNode)(*this);
		case NodeTypes::LERP: return LUMIX_NEW(m_allocator, LerpNode)(*this);
		case NodeTypes::SAMPLE: return LUMIX_NEW(m_allocator, SampleNode)(*this);
		case NodeTypes::UNIFORM: return LUMIX_NEW(m_allocator, UniformNode)(*this);
		case NodeTypes::VEC4_MERGE: return LUMIX_NEW(m_allocator, Vec4MergeNode)(*this);
		case NodeTypes::MULTIPLY: return LUMIX_NEW(m_allocator, MultiplyNode)(*this);
	}

	return nullptr;
}


ShaderEditor::Node& ShaderEditor::loadNode(Lumix::InputBlob& blob)
{
	int type;
	int id;
	blob.read(id);
	blob.read(type);
	Node* node = createNode(type);
	node->id = id;
	m_fragment_nodes.push(node);
	blob.read(node->pos);
	blob.readString(node->m_name, Lumix::lengthOf(node->m_name));

	node->load(blob);
	return *node;
}


void ShaderEditor::loadNodeConnections(Lumix::InputBlob& blob, Node& node)
{
	int size;
	blob.read(size);
	for(int i = 0; i < size; ++i)
	{
		int tmp;
		blob.read(tmp);
		node.m_inputs[i] = tmp < 0 ? nullptr : getNodeByID(tmp);
		blob.read(tmp);
		if(node.m_inputs[i]) node.m_inputs[i]->m_outputs[tmp] = &node;
	}

	blob.read(size);
	for(int i = 0; i < size; ++i)
	{
		int tmp;
		blob.read(tmp);
		node.m_outputs[i] = tmp < 0 ? nullptr : getNodeByID(tmp);
		blob.read(tmp);
		if(node.m_outputs[i]) node.m_outputs[i]->m_inputs[tmp] = &node;
	}
}


void ShaderEditor::load()
{
	char path[Lumix::MAX_PATH_LENGTH];
	if (!Lumix::getOpenFilename(path, Lumix::lengthOf(path), "Shader edit data\0*.sed\0"))
	{
		return;
	}
	m_path = path;

	clear();

	FILE* fp = fopen(path, "rb");
	if (!fp)
	{
		Lumix::g_log_error.log("Shader editor") << "Failed to load shader " << path;
		return;
	}

	fseek(fp, 0, SEEK_END);
	int data_size = (int)ftell(fp);
	Lumix::Array<uint8_t> data(m_allocator);
	data.resize(data_size);
	fseek(fp, 0, SEEK_SET);
	fread(&data[0], 1, data_size, fp);

	Lumix::InputBlob blob(&data[0], data_size);
	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		blob.readString(m_textures[i], Lumix::lengthOf(m_textures[i]));
	}

	for (int i = 0; i < Lumix::lengthOf(m_vertex_outputs); ++i)
	{
		blob.readString(m_vertex_outputs[i], Lumix::lengthOf(m_vertex_outputs[i]));
	}

	int size;
	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		loadNode(blob);
	}

	for (auto* node : m_fragment_nodes)
	{
		loadNodeConnections(blob, *node);
		m_last_node_id = Lumix::Math::maxValue(int(node->id + 1), int(m_last_node_id));
	}

	fclose(fp);
}


void ShaderEditor::getSavePath()
{
	char path[Lumix::MAX_PATH_LENGTH];
	Lumix::getSaveFilename(path, Lumix::lengthOf(path), "Shader edit data\0*.sed\0", "sed");
	m_path = path;
}


void ShaderEditor::onGUILeftColumn()
{
	ImGui::BeginChild("left_col", ImVec2(120, 0));
	ImGui::PushItemWidth(120);

	ImGui::Text("Textures");
	ImGui::Separator();
	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		ImGui::InputText(StringBuilder<10>("###tex", i), m_textures[i], sizeof(m_textures[i]));
	}

	ImGui::Text("Vertex outputs");
	ImGui::Separator();
	for (int i = 0; i < Lumix::lengthOf(m_vertex_outputs); ++i)
	{
		ImGui::InputText(
			StringBuilder<10>("###vout", i), m_vertex_outputs[i], sizeof(m_vertex_outputs[i]));
	}


	ImGui::PopItemWidth();
	ImGui::EndChild();
}


void ShaderEditor::execute(ICommand* command)
{
	for (int i = m_undo_stack.size() - 1; i > m_undo_stack_idx; --i)
	{
		LUMIX_DELETE(m_allocator, m_undo_stack[i]);
		m_undo_stack.pop();
	}

	if(m_undo_stack_idx >= 0)
	{
		if(m_undo_stack[m_undo_stack_idx]->merge(*command))
		{
			m_undo_stack[m_undo_stack_idx]->execute();
			LUMIX_DELETE(m_allocator, command);
			return;
		}
	}

	m_undo_stack_idx = m_undo_stack.size();
	m_undo_stack.push(command);

	command->execute();
}


bool ShaderEditor::canUndo() const
{
	return m_undo_stack_idx >= 0;
}


bool ShaderEditor::canRedo() const
{
	return m_undo_stack_idx < m_undo_stack.size() - 1;
}


void ShaderEditor::undo()
{
	if (m_undo_stack_idx < 0) return;

	m_undo_stack[m_undo_stack_idx]->undo();
	--m_undo_stack_idx;
}


void ShaderEditor::redo()
{
	if (m_undo_stack_idx + 1 >= m_undo_stack.size()) return;

	m_undo_stack[m_undo_stack_idx + 1]->execute();
	++m_undo_stack_idx;
}


void ShaderEditor::destroyNode(Node* node)
{
	for(auto* input : node->m_inputs)
	{
		if(!input) continue;
		input->m_outputs[input->m_outputs.indexOf(node)] = nullptr;
	}

	for(auto* output : node->m_outputs)
	{
		if(!output) continue;
		output->m_inputs[output->m_inputs.indexOf(node)] = nullptr;
	}

	LUMIX_DELETE(m_allocator, node);
	m_fragment_nodes.eraseItem(node);
}


void ShaderEditor::onGUI()
{
	if (ImGui::Begin("Shader editor", nullptr, ImGuiWindowFlags_MenuBar))
	{
		m_is_focused = ImGui::IsRootWindowOrAnyChildFocused();
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				ImGui::MenuItem("New");
				if (ImGui::MenuItem("Open"))
				{
					load();
				}
				if (ImGui::MenuItem("Save", nullptr, false, m_path.isValid()))
				{
					save(m_path.c_str());
				}
				if (ImGui::MenuItem("Save as"))
				{
					getSavePath();
					if (m_path.isValid()) save(m_path.c_str());
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo", nullptr, false, canUndo()))
				{
					undo();
				}
				if (ImGui::MenuItem("Redo", nullptr, false, canRedo()))
				{
					redo();
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Generate", nullptr, false, m_path.isValid()))
			{
				generate(m_path.c_str());
			}

			ImGui::EndMenuBar();
		}

		onGUILeftColumn();
		ImGui::SameLine();

		ImGui::BeginChild("right_col");
		auto cursor_screen_pos = ImGui::GetCursorScreenPos();

		for (auto* node : m_fragment_nodes)
		{
			auto node_screen_pos = cursor_screen_pos;
			node_screen_pos.x = cursor_screen_pos.x + node->pos.x;
			node_screen_pos.y = cursor_screen_pos.y + node->pos.y;

			ImGui::BeginNode(node->id, node_screen_pos);
			node->onNodeGUI();
			ImGui::EndNode(node_screen_pos);
			if(ImGui::IsItemHovered() && ImGui::IsMouseDown(1))
			{
				m_current_node_id = node->id;
			}

			for (int i = 0; i < node->m_outputs.size(); ++i)
			{
				Node* output = node->m_outputs[i];
				if (!output) continue;

				auto output_screen_pos = cursor_screen_pos;
				output_screen_pos.x = cursor_screen_pos.x + output->pos.x;
				output_screen_pos.y = cursor_screen_pos.y + output->pos.y;

				auto output_pos = ImGui::GetNodeOutputPos(node->id, i);
				auto input_pos = ImGui::GetNodeInputPos(output->id, output->m_inputs.indexOf(node));
				ImGui::NodeLink(output_pos, input_pos);
			}

			for (int i = 0; i < node->m_outputs.size(); ++i)
			{
				auto pin_pos = ImGui::GetNodeOutputPos(node->id, i);
				if (ImGui::NodePin(i, pin_pos))
				{
					if (ImGui::IsMouseReleased(0) && m_new_link_info.is_active)
					{
						createConnection(node, i, false);
					}
					if (ImGui::IsMouseClicked(0)) nodePinMouseDown(node, i, false);
				}
			}

			for (int i = 0; i < node->m_inputs.size(); ++i)
			{
				auto pin_pos = ImGui::GetNodeInputPos(node->id, i);
				if (ImGui::NodePin(i + node->m_outputs.size(), pin_pos))
				{
					if (ImGui::IsMouseReleased(0) && m_new_link_info.is_active)
					{
						createConnection(node, i, true);
					}
					if (ImGui::IsMouseClicked(0)) nodePinMouseDown(node, i, true);
				}
			}

			ImVec2 new_pos;
			new_pos.x = node_screen_pos.x - cursor_screen_pos.x;
			new_pos.y = node_screen_pos.y - cursor_screen_pos.y;

			if(new_pos.x != node->pos.x || new_pos.y != node->pos.y)
			{
				execute(LUMIX_NEW(m_allocator, MoveNodeCommand)(node->id, new_pos, *this));
			}
		}

		if (m_new_link_info.is_active && ImGui::IsMouseDown(0))
		{
			if (m_new_link_info.is_from_input)
			{
				auto pos = ImGui::GetNodeInputPos(
					m_new_link_info.from->id, m_new_link_info.from_pin_index);
				ImGui::NodeLink(ImGui::GetMousePos(), pos);
			}
			else
			{
				auto pos = ImGui::GetNodeOutputPos(
					m_new_link_info.from->id, m_new_link_info.from_pin_index);
				ImGui::NodeLink(pos, ImGui::GetMousePos());
			}
		}
		else
		{
			m_new_link_info.is_active = false;
		}

		if (ImGui::IsMouseClicked(1))
		{
			ImGui::OpenPopup("context_menu");
		}

		if (ImGui::BeginPopup("context_menu"))
		{
			ImVec2 add_pos(ImGui::GetMousePos().x - cursor_screen_pos.x,
				ImGui::GetMousePos().y - cursor_screen_pos.y);
			if(m_current_node_id >= 0)
			{
				if(ImGui::MenuItem("Remove"))
				{
					execute(LUMIX_NEW(m_allocator, RemoveNodeCommand)(m_current_node_id, *this));
				}
			}

			if (ImGui::BeginMenu("Add"))
			{
				for(auto node_type : NODE_TYPES)
				{
					if (ImGui::MenuItem(node_type.name))
					{
						execute(LUMIX_NEW(m_allocator, CreateNodeCommand)(-1, node_type.type, add_pos, *this));
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
		ImGui::EndChild();
	}
	ImGui::End();
}