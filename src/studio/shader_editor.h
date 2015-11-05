#pragma once


#include "core/array.h"
#include "core/path.h"
#include "ocornut-imgui/imgui.h"
#include <cstdio>


namespace Lumix
{
	class InputBlob;
	class OutputBlob;
}


class ShaderEditor
{
public:
	struct ICommand;

	enum class ShaderType
	{
		VERTEX,
		FRAGMENT,

		COUNT
	};

	enum class VertexInput
	{
		POSITION,
		COLOR,
		NORMAL,
		TANGENT,
		TEXCOORD0,
		INSTANCE_DATA0,
		INSTANCE_DATA1,
		INSTANCE_DATA2,
		INSTANCE_DATA3,

		COUNT
	};

	struct Node
	{
		Node(int type, ShaderEditor& editor);

		virtual void save(Lumix::OutputBlob& blob) {}
		virtual void load(Lumix::InputBlob& blob) {}
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
	const char* getTextureName(int index) const { return m_textures[index]; }
	const char* getVertexOutputName(int index) const { return m_vertex_outputs[index]; }
	Lumix::IAllocator& getAllocator() { return m_allocator; }
	Node* createNode(int type);
	void addNode(Node* node, const ImVec2& pos, ShaderType type);
	void destroyNode(Node* node);
	Node* getNodeByID(int id);
	Node& loadNode(Lumix::InputBlob& blob, ShaderType type);
	void loadNodeConnections(Lumix::InputBlob& blob, Node& node);
	void saveNode(Lumix::OutputBlob& blob, Node& node);
	void saveNodeConnections(Lumix::OutputBlob& blob, Node& node);
	bool isFocused() const { return m_is_focused; }
	void undo();
	void redo();

public:
	static const int MAX_TEXTURES_COUNT = 16;
	static const int MAX_VERTEX_OUTPUTS_COUNT = 16;

private:
	void generate(const char* path, ShaderType shader_type);
	void newGraph();
	void save(const char* path);
	void load();
	void execute(ICommand* command);
	bool canUndo() const;
	bool canRedo() const;

	void nodePinMouseDown(Node* node, int pin_index, bool is_input);
	void createConnection(Node* node, int pin_index, bool is_input);
	void getSavePath();
	void clear();
	void onGUILeftColumn();
	void onGUIRightColumn();
	void onGUIMenu();

private:
	struct NewLinkInfo
	{
		bool is_active;
		Node* from;
		int from_pin_index;
		bool is_from_input;
		ImVec2 pos_offset;
	} m_new_link_info;

private:
	char m_textures[MAX_TEXTURES_COUNT][50];
	char m_vertex_outputs[MAX_VERTEX_OUTPUTS_COUNT][50];
	bool m_vertex_inputs[(int)VertexInput::COUNT];
	Lumix::Path m_path;
	int m_last_node_id;
	int m_undo_stack_idx;
	Lumix::Array<ICommand*> m_undo_stack;
	Lumix::Array<Node*> m_fragment_nodes;
	Lumix::Array<Node*> m_vertex_nodes;
	Lumix::IAllocator& m_allocator;
	int m_current_node_id;
	ShaderType m_current_shader_type;
	bool m_is_focused;
	ImVec2 m_canvas_pos;
};