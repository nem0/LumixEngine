#pragma once


#include "core/array.h"
#include "ocornut-imgui/imgui.h"


class ShaderEditor
{
	public:
		struct Node
		{
			Node(Lumix::IAllocator& allocator)
				: m_inputs(allocator)
				, m_outputs(allocator)
			{}

			virtual void onGUI() = 0;
			virtual ~Node();

			ImGuiID id;
			ImVec2 pos;

			Lumix::Array<Node*> m_inputs;
			Lumix::Array<Node*> m_outputs;
		};

	public:
		ShaderEditor(Lumix::IAllocator& allocator);
		~ShaderEditor();

		void onGUI();

	private:
		void addNode(Node* node);
		void nodePinMouseDown(Node* node, int pin_index, bool is_input);
		void createConnection(Node* node, int pin_index, bool is_input);
		void removeConnection(Node* node, int pin_index, bool is_input);

	private:
		struct NewLinkInfo
		{
			bool is_active;
			Node* from;
			int from_pin_index;
			bool is_from_input;
			ImVec2 pos_offset;
		} m_new_link_info;

		int m_last_node_id;
		Lumix::Array<Node*> m_nodes;
		Lumix::IAllocator& m_allocator;
};