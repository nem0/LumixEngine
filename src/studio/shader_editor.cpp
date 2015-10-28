#include "shader_editor.h"
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
	UNIFORM
};


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


struct FloatConstNode : public ShaderEditor::Node
{
	FloatConstNode(ShaderEditor& editor)
		: Node((int)NodeTypes::FLOAT_CONST, editor)
	{
		m_value = 0;
		m_outputs.push(nullptr);
	}

	void save(FILE* fp) override
	{
		fwrite(&m_value, sizeof(m_value), 1, fp);
	}

	void load(FILE* fp) override
	{
		fread(&m_value, sizeof(m_value), 1, fp);
	}

	void generate(FILE* fp) override
	{
		fprintf(fp, "\tconst float %s = %f;\n", m_name, m_value);
	}

	void onGUI() override
	{
		ImGui::DragFloat("value", &m_value, 0.1f);
	}

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

	void save(FILE* fp) override
	{
		fwrite(&m_color, sizeof(m_color), 1, fp);
	}

	void load(FILE* fp) override
	{
		fread(&m_color, sizeof(m_color), 1, fp);
	}

	void generate(FILE* fp) override
	{
		fprintf(fp, "\tconst vec4 %s = vec4(%f, %f, %f, %f);\n", m_name, m_color[0], m_color[1], m_color[2], m_color[3]);
	}

	void onGUI() override
	{
		ImGui::ColorEdit4("value", m_color);
	}

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

	void save(FILE* fp) override
	{
		fwrite(&m_texture, sizeof(m_texture), 1, fp);
	}

	void load(FILE* fp) override
	{
		fread(&m_texture, sizeof(m_texture), 1, fp);
	}

	void generate(FILE* fp) override
	{
		if (!m_inputs[0])
		{
			fprintf(fp, "\tvec4 %s = vec4(1, 0, 1, 0);\n", m_name);
			return;
		}

		m_inputs[0]->generate(fp);
		fprintf(fp, "\tvec4 %s = texture2D(%s, %s);\n", m_name, m_editor.getTextureName(m_texture), m_inputs[0]->m_name);
	}

	void onGUI() override
	{
		ImGui::Text("UV");
		auto getter = [](void* data, int idx, const char** out) -> bool
		{
			*out = ((SampleNode*)data)->m_editor.getTextureName(idx);
			return true;
		};
		ImGui::Combo("Texture", &m_texture, getter, this, 16); TODO("16");
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

	void save(FILE* fp) override
	{
		fwrite(&m_attribute, sizeof(m_attribute), 1, fp);
	}

	void load(FILE* fp) override
	{
		fread(&m_attribute, sizeof(m_attribute), 1, fp);
	}

	void generate(FILE* fp) override
	{
	}


	void onGUI() override
	{
		auto getter = [](void* data, int idx, const char** out) -> bool
		{
			*out = ((SampleNode*)data)->m_editor.getVertexOutputName(idx);
			return true;
		};
		TODO("16");
		if (ImGui::Combo("Attribute", &m_attribute, getter, this, 16))
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


	void onGUI() override
	{
		ImGui::Text("OUTPUT");
	}
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
			case VEC4:
				return "vec4";
			default:
				ASSERT(false);
				return "vec4";
		}
	}


	void save(FILE* fp) override
	{
		fwrite(&m_type, sizeof(m_type), 1, fp);
	}

	void load(FILE* fp) override
	{
		fread(&m_type, sizeof(m_type), 1, fp);
	}


	void generateBeforeMain(FILE* fp) override
	{
		fprintf(fp, "uniform %s %s;\n", getTypeName(), m_name);
	}

	void generate(FILE* fp) override
	{
		
	}

	void onGUI() override
	{
		ImGui::Combo("Type", (int*)&m_type, "Vec4\0");
	}

	Type m_type;
};


ShaderEditor::Node::~Node()
{
	for(auto* output : m_outputs)
	{
		if(output) output->m_inputs.eraseItem(this);
	}

	for(auto* input : m_inputs)
	{
		if(input) input->m_outputs.eraseItem(this);
	}
}


ShaderEditor::ShaderEditor(Lumix::IAllocator& allocator)
	: m_nodes(allocator)
	, m_allocator(allocator)
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

	m_nodes.push(LUMIX_NEW(allocator, OutputNode)(*this));
	m_nodes.back()->pos.x = 250;
	m_nodes.back()->pos.y = 50;
	m_nodes.back()->id = ++m_last_node_id;
}


ShaderEditor::~ShaderEditor()
{
	clear();
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
	
	for (auto* node : m_nodes)
	{
		node->generateBeforeMain(fp);
	}

	fputs("void main() {\n", fp);
	m_nodes[0]->generate(fp);
	fputs("}\n", fp);

	fclose(fp);

}


void ShaderEditor::addNode(Node* node, const ImVec2& pos)
{
	m_nodes.push(node);
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


void ShaderEditor::removeConnection(Node* node, int pin_index, bool is_input)
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


void ShaderEditor::createConnection(Node* node, int pin_index, bool is_input)
{
	if(!m_new_link_info.is_active) return;
	if(m_new_link_info.is_from_input == is_input) return;

	removeConnection(node, pin_index, is_input);
	removeConnection(m_new_link_info.from, m_new_link_info.from_pin_index, m_new_link_info.is_from_input);
	if(is_input)
	{
		node->m_inputs[pin_index] = m_new_link_info.from;
		m_new_link_info.from->m_outputs[m_new_link_info.from_pin_index] = node;
	}
	else
	{
		node->m_outputs[pin_index] = m_new_link_info.from;
		m_new_link_info.from->m_inputs[m_new_link_info.from_pin_index] = node;
	}
}


void ShaderEditor::save(const char* path)
{
	FILE* fp = fopen(path, "wb");
	if (!fp) return; TODO("todo");

	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		int tmp = (int)strlen(m_textures[i]);
		fwrite(&tmp, sizeof(tmp), 1, fp);
		fwrite(m_textures[i], 1, tmp, fp);
	}

	for (int i = 0; i < Lumix::lengthOf(m_vertex_outputs); ++i)
	{
		int tmp = (int)strlen(m_vertex_outputs[i]);
		fwrite(&tmp, sizeof(tmp), 1, fp);
		fwrite(m_vertex_outputs[i], 1, tmp, fp);
	}

	int nodes_count = m_nodes.size();
	fwrite(&nodes_count, sizeof(nodes_count), 1, fp);
	for (auto* node : m_nodes)
	{
		int type = (int)node->m_type;
		fwrite(&type, sizeof(type), 1, fp);
		fwrite(&node->pos, sizeof(node->pos), 1, fp);
		int tmp = (int)strlen(node->m_name);
		fwrite(&tmp, sizeof(tmp), 1, fp);
		fwrite(node->m_name, 1, tmp, fp);

		node->save(fp);
	}


	for (auto* node : m_nodes)
	{
		int inputs_count = node->m_inputs.size();
		fwrite(&inputs_count, sizeof(inputs_count), 1, fp);
		for (int i = 0; i < inputs_count; ++i)
		{
			int tmp = node->m_inputs[i] ? m_nodes.indexOf(node->m_inputs[i]) : -1;
			fwrite(&tmp, sizeof(tmp), 1, fp);
		}

		int outputs_count = node->m_outputs.size();
		fwrite(&outputs_count, sizeof(outputs_count), 1, fp);
		for (int i = 0; i < outputs_count; ++i)
		{
			int tmp = node->m_outputs[i] ? m_nodes.indexOf(node->m_outputs[i]) : -1;
			fwrite(&tmp, sizeof(tmp), 1, fp);
		}
	}

	fclose(fp);
}


void ShaderEditor::clear()
{
	for (auto* node : m_nodes)
	{
		m_allocator.deleteObject(node);
	}
	m_nodes.clear();
	m_last_node_id = 0;
}


ShaderEditor::Node* ShaderEditor::createNode(int type)
{
	switch ((NodeTypes)type)
	{
		case NodeTypes::OUTPUT:
			return LUMIX_NEW(m_allocator, OutputNode)(*this);
		case NodeTypes::ATTRIBUTE:
			return LUMIX_NEW(m_allocator, AttributeNode)(*this);
		case NodeTypes::COLOR_CONST:
			return LUMIX_NEW(m_allocator, ColorConstNode)(*this);
		case NodeTypes::FLOAT_CONST:
			return LUMIX_NEW(m_allocator, FloatConstNode)(*this);
		case NodeTypes::LERP:
			return LUMIX_NEW(m_allocator, LerpNode)(*this);
		case NodeTypes::SAMPLE:
			return LUMIX_NEW(m_allocator, SampleNode)(*this);
		case NodeTypes::UNIFORM:
			return LUMIX_NEW(m_allocator, UniformNode)(*this);
	}

	return nullptr;
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
	if (!fp) return; TODO("todo");

	for (int i = 0; i < Lumix::lengthOf(m_textures); ++i)
	{
		int tmp;
		fread(&tmp, sizeof(tmp), 1, fp);
		fread(m_textures[i], 1, tmp, fp);
		m_textures[i][tmp] = 0;
	}

	for (int i = 0; i < Lumix::lengthOf(m_vertex_outputs); ++i)
	{
		int tmp;
		fread(&tmp, sizeof(tmp), 1, fp);
		fread(m_vertex_outputs[i], 1, tmp, fp);
		m_vertex_outputs[i][tmp] = 0;
	}

	int size;
	fread(&size, sizeof(size), 1, fp);
	for (int i = 0; i < size; ++i)
	{
		int type;
		fread(&type, sizeof(type), 1, fp);
		Node* node = createNode(type);
		node->id = ++m_last_node_id;
		m_nodes.push(node);
		fread(&node->pos, sizeof(node->pos), 1, fp);
		int tmp;
		fread(&tmp, sizeof(tmp), 1, fp);
		fread(node->m_name, 1, tmp, fp);
		node->m_name[tmp] = 0;

		node->load(fp);
	}

	for (auto* node : m_nodes)
	{
		int size;
		fread(&size, sizeof(size), 1, fp);
		for (int i = 0; i < size; ++i)
		{
			int tmp;
			fread(&tmp, sizeof(tmp), 1, fp);
			node->m_inputs[i] = tmp < 0 ? nullptr : m_nodes[tmp];
		}

		fread(&size, sizeof(size), 1, fp);
		for (int i = 0; i < size; ++i)
		{
			int tmp;
			fread(&tmp, sizeof(tmp), 1, fp);
			node->m_outputs[i] = tmp < 0 ? nullptr : m_nodes[tmp];
		}
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
		ImGui::InputText(StringBuilder<10>("###vout", i), m_vertex_outputs[i], sizeof(m_vertex_outputs[i]));
	}


	ImGui::PopItemWidth();
	ImGui::EndChild();
}


void ShaderEditor::onGUI()
{
	if(ImGui::Begin("Shader editor", nullptr, ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				ImGui::MenuItem("New");
				if (ImGui::MenuItem("Load"))
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

		for(auto* node : m_nodes)
		{
			auto node_screen_pos = cursor_screen_pos;
			node_screen_pos.x = cursor_screen_pos.x + node->pos.x;
			node_screen_pos.y = cursor_screen_pos.y + node->pos.y;

			ImGui::BeginNode(node->id, node_screen_pos);
			node->onNodeGUI();
			ImGui::EndNode(node_screen_pos);

			for(int i = 0; i < node->m_outputs.size(); ++i)
			{
				Node* output = node->m_outputs[i];
				if(!output) continue;

				auto output_screen_pos = cursor_screen_pos;
				output_screen_pos.x = cursor_screen_pos.x + output->pos.x;
				output_screen_pos.y = cursor_screen_pos.y + output->pos.y;

				auto output_pos = ImGui::GetNodeOutputPos(node->id, i);
				auto input_pos = ImGui::GetNodeInputPos(output->id, output->m_inputs.indexOf(node));
				ImGui::NodeLink(output_pos, input_pos);
			}

			for(int i = 0; i < node->m_outputs.size(); ++i)
			{
				auto pin_pos = ImGui::GetNodeOutputPos(node->id, i);
				if(ImGui::NodePin(i, pin_pos))
				{
					if(ImGui::IsMouseReleased(0) && m_new_link_info.is_active) createConnection(node, i, false);
					if(ImGui::IsMouseClicked(0)) nodePinMouseDown(node, i ,false);
				}
			}

			for(int i = 0; i < node->m_inputs.size(); ++i)
			{
				auto pin_pos = ImGui::GetNodeInputPos(node->id, i);
				if(ImGui::NodePin(i + node->m_outputs.size(), pin_pos))
				{
					if(ImGui::IsMouseReleased(0) && m_new_link_info.is_active) createConnection(node, i, true);
					if(ImGui::IsMouseClicked(0)) nodePinMouseDown(node, i, true);
				}
			}

			node->pos.x = node_screen_pos.x - cursor_screen_pos.x;
			node->pos.y = node_screen_pos.y - cursor_screen_pos.y;
		}

		if(m_new_link_info.is_active && ImGui::IsMouseDown(0))
		{
			if(m_new_link_info.is_from_input)
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

		if(ImGui::BeginPopup("context_menu"))
		{
			ImVec2 add_pos(ImGui::GetMousePos().x - cursor_screen_pos.x,
				ImGui::GetMousePos().y - cursor_screen_pos.y);
			if (ImGui::BeginMenu("Add"))
			{
				if(ImGui::MenuItem("LERP"))
				{
					addNode(new LerpNode(*this), add_pos);
				}
				if (ImGui::MenuItem("Sample"))
				{
					addNode(new SampleNode(*this), add_pos);
				}
				if (ImGui::MenuItem("Attribute"))
				{
					addNode(new AttributeNode(*this), add_pos);
				}
				if (ImGui::MenuItem("Color constant"))
				{
					addNode(new ColorConstNode(*this), add_pos);
				}
				if (ImGui::MenuItem("Float constant"))
				{
					addNode(new FloatConstNode(*this), add_pos);
				}
				if (ImGui::MenuItem("Uniform"))
				{
					addNode(new UniformNode(*this), add_pos);
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
		ImGui::EndChild();

	}
	ImGui::End();
}