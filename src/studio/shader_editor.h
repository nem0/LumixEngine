#pragma once


#include "core/array.h"
#include "core/path.h"
#include "ocornut-imgui/imgui.h"
#include <cstdio>


class ShaderEditor
{
	public:
		struct Node
		{
			Node(int type, ShaderEditor& editor);

			virtual void save(FILE* fp) {}
			virtual void load(FILE* fp) {}
			virtual void generate(FILE* fp) = 0;
			virtual void generateBeforeMain(FILE* fp) {}
			virtual ~Node();

			void onNodeGUI();

			ImGuiID id;
			ImVec2 pos;

			Lumix::Array<Node*> m_inputs;
			Lumix::Array<Node*> m_outputs;
			char m_name[50];
			int m_type;
			ShaderEditor& m_editor;

			protected:
				virtual void onGUI() = 0;
				bool m_can_have_name;
		};

	public:
		ShaderEditor(Lumix::IAllocator& allocator);
		~ShaderEditor();

		void onGUI();
		void generate(const char* path);
		void save(const char* path);
		void load();
		const char* getTextureName(int index) const { return m_textures[index]; }
		const char* getVertexOutputName(int index) const { return m_vertex_outputs[index]; }
		
	private:
		void addNode(Node* node, const ImVec2& pos);
		void nodePinMouseDown(Node* node, int pin_index, bool is_input);
		void createConnection(Node* node, int pin_index, bool is_input);
		void removeConnection(Node* node, int pin_index, bool is_input);
		void getSavePath();
		void clear();
		Node* createNode(int type);
		void onGUILeftColumn();

	private:
		struct NewLinkInfo
		{
			bool is_active;
			Node* from;
			int from_pin_index;
			bool is_from_input;
			ImVec2 pos_offset;
		} m_new_link_info;

		char m_textures[16][50];
		char m_vertex_outputs[16][50];
		Lumix::Path m_path;
		int m_last_node_id;
		Lumix::Array<Node*> m_nodes;
		Lumix::IAllocator& m_allocator;
};