#include "shader_editor.h"


struct TextureNode : public ShaderEditor::Node
{
	TextureNode(Lumix::IAllocator& allocator)
		: Node(allocator)
	{
		m_slot = 0;
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	void onGUI() override
	{
		ImGui::PushItemWidth(100);
		ImGui::Text("UV");
		ImGui::InputInt("Texture slot", &m_slot);
		ImGui::PopItemWidth();
	}

	int m_slot;
};


struct SwitchNode : public ShaderEditor::Node
{
	SwitchNode(Lumix::IAllocator& allocator)
		: Node(allocator)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	void onGUI() override
	{
		ImGui::Text("Value");

		for(int i = 0; i < m_inputs.size() - 1; ++i)
		{
			ImGui::Text("case %d", i + 1);
		}
		if(ImGui::Button("Add case"))
		{
			m_inputs.push(nullptr);
		}
	}
};



struct OutputNode : public ShaderEditor::Node
{
	OutputNode(Lumix::IAllocator& allocator)
		: Node(allocator)
	{
		m_inputs.push(nullptr);
	}

	void onGUI() override
	{
		ImGui::Text("OUTPUT");
	}
};


struct LerpNode : public ShaderEditor::Node
{
	LerpNode(Lumix::IAllocator& allocator)
		: Node(allocator)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	void onGUI() override
	{
		ImGui::Text("Input 1");
		ImGui::Text("Input 2");
		ImGui::Text("Weight");
	}
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
	m_last_node_id = 0;
	m_new_link_info.is_active = false;

	m_nodes.push(new OutputNode(allocator));
	m_nodes.back()->pos.x = 200;
	m_nodes.back()->pos.y = 50;
	m_nodes.back()->id = ++m_last_node_id;

	m_nodes.push(new TextureNode(allocator));
	m_nodes.back()->pos.x = 0;
	m_nodes.back()->pos.y = 0;
	m_nodes.back()->id = ++m_last_node_id;

	m_nodes[0]->m_inputs[0] = m_nodes[1];
	m_nodes[1]->m_outputs[0] = m_nodes[0];
}


ShaderEditor::~ShaderEditor()
{
	TODO("todo");
}


void ShaderEditor::addNode(Node* node)
{
	m_nodes.push(node);
	node->pos.x = 0;
	node->pos.y = 0;
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


void ShaderEditor::onGUI()
{
	if(ImGui::Begin("Shader editor"))
	{
		auto cursor_screen_pos = ImGui::GetCursorScreenPos();

		for(auto* node : m_nodes)
		{
			auto node_screen_pos = cursor_screen_pos;
			node_screen_pos.x = cursor_screen_pos.x + node->pos.x;
			node_screen_pos.y = cursor_screen_pos.y + node->pos.y;

			ImGui::BeginNode(node->id, node_screen_pos);
			node->onGUI();
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

		if(ImGui::IsMouseClicked(1))
		{
			ImGui::OpenPopup("context_menu");
		}

		if(ImGui::BeginPopup("context_menu"))
		{
			if(ImGui::BeginMenu("Add"))
			{
				if(ImGui::MenuItem("LERP"))
				{
					addNode(new LerpNode(m_allocator));
				}
				if(ImGui::MenuItem("Texture"))
				{
					addNode(new TextureNode(m_allocator));
				}
				if(ImGui::MenuItem("Switch"))
				{
					addNode(new SwitchNode(m_allocator));
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
	}
	ImGui::End();
}