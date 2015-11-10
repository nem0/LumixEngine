#pragma once


#include "core/array.h"
#include "core/path.h"
#include "ocornut-imgui/imgui.h"


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


	enum class ValueType
	{
		FLOAT,
		VEC2,
		VEC3,
		VEC4,
		MATRIX3,
		MATRIX4,

		COUNT,
		NONE
	};


	struct Node
	{
		Node(int type, ShaderEditor& editor);

		virtual void save(Lumix::OutputBlob& blob) {}
		virtual void load(Lumix::InputBlob& blob) {}
		virtual void generate(Lumix::OutputBlob& blob) = 0;
		virtual void printReference(Lumix::OutputBlob& blob);
		virtual void generateBeforeMain(Lumix::OutputBlob& blob) {}
		virtual ValueType getOutputType(int index) const { return ValueType::FLOAT; }
		virtual ~Node();

		ValueType getInputType(int index) const;
		void onNodeGUI();

		ImGuiID m_id;
		ImVec2 m_pos;

		Lumix::Array<Node*> m_inputs;
		Lumix::Array<Node*> m_outputs;
		int m_type;
		ShaderEditor& m_editor;

	protected:
		virtual void onGUI() = 0;
	};

public:
	ShaderEditor(Lumix::IAllocator& allocator);
	~ShaderEditor();

	void onGUI();
	const char* getTextureName(int index) const { return m_textures[index]; }
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

	bool m_is_opened;

private:
	void generateMain(const char* path);
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