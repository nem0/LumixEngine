#pragma once


#include "engine/array.h"
#include "engine/path.h"
#include "imgui/imgui.h"


namespace Lumix
{


class InputMemoryStream;
class OutputMemoryStream;


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


	enum class ValueType : int
	{
		BOOL,
		FLOAT,
		INT,
		VEC2,
		VEC3,
		VEC4,
		IVEC4,
		MATRIX3,
		MATRIX4,

		COUNT,
		NONE
	};


	struct Node
	{
		Node(int type, ShaderEditor& editor);

		virtual void save(OutputMemoryStream& /*blob*/) {}
		virtual void load(InputMemoryStream& /*blob*/) {}
		virtual void generate(OutputMemoryStream& /*blob*/) {}
		virtual void printReference(OutputMemoryStream& blob, Node* output);
		virtual void generateBeforeMain(OutputMemoryStream& /*blob*/) {}
		virtual ValueType getOutputType(int /*index*/) const { return ValueType::FLOAT; }
		virtual ~Node();


		ValueType getInputType(int index) const;
		void generateRecursive(OutputMemoryStream& blob);
		void onNodeGUI();

		ImGuiID m_id;
		ImVec2 m_pos;

		Array<Node*> m_inputs;
		Array<Node*> m_outputs;
		int m_type;
		ShaderEditor& m_editor;

	protected:
		virtual void onGUI() = 0;
	};

public:
	explicit ShaderEditor(IAllocator& allocator);
	~ShaderEditor();

	void onGUI();
	const char* getTextureName(int index) const { return m_textures[index]; }
	IAllocator& getAllocator() { return m_allocator; }
	Node* createNode(int type);
	void addNode(Node* node, const ImVec2& pos, ShaderType type);
	void destroyNode(Node* node);
	Node* getNodeByID(int id);
	Node& loadNode(InputMemoryStream& blob, ShaderType type);
	void loadNodeConnections(InputMemoryStream& blob, Node& node);
	void saveNode(OutputMemoryStream& blob, Node& node);
	void saveNodeConnections(OutputMemoryStream& blob, Node& node);
	bool hasFocus() const { return m_is_focused; }
	void undo();
	void redo();

public:
	static const int MAX_TEXTURES_COUNT = 16;

	bool m_is_open;

private:
	void generatePasses(OutputMemoryStream& blob);
	void generate(const char* path, bool save_file);
	void newGraph();
	void save(const char* path);
	void load();
	void execute(ICommand* command);
	bool canUndo() const;
	bool canRedo() const;

	void nodePinMouseDown(Node* node, int pin_index, bool is_input);
	void createConnection(Node* node, int pin_index, bool is_input);
	bool getSavePath();
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
	StaticString<50> m_textures[MAX_TEXTURES_COUNT];
	Path m_path;
	int m_last_node_id;
	int m_undo_stack_idx;
	Array<ICommand*> m_undo_stack;
	Array<Node*> m_fragment_nodes;
	Array<Node*> m_vertex_nodes;
	IAllocator& m_allocator;
	int m_current_node_id;
	ShaderType m_current_shader_type;
	bool m_is_focused;
	ImVec2 m_canvas_pos;
	float m_left_col_width = 100;
	String m_source;
};


} // namespace Lumix
