#pragma once


#include "engine/array.h"
#include "engine/path.h"
#include "imgui/imgui.h"


namespace Lumix
{


class InputBlob;
class OutputBlob;
class ShaderCompiler;


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

		virtual void save(OutputBlob& /*blob*/) {}
		virtual void load(InputBlob& /*blob*/) {}
		virtual void generate(OutputBlob& blob) = 0;
		virtual void printReference(OutputBlob& blob);
		virtual void generateBeforeMain(OutputBlob& /*blob*/) {}
		virtual ValueType getOutputType(int /*index*/) const { return ValueType::FLOAT; }
		virtual ~Node();

		ValueType getInputType(int index) const;
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

	void onGUI(ShaderCompiler& compiler);
	const char* getTextureName(int index) const { return m_textures[index]; }
	IAllocator& getAllocator() { return m_allocator; }
	Node* createNode(int type);
	void addNode(Node* node, const ImVec2& pos, ShaderType type);
	void destroyNode(Node* node);
	Node* getNodeByID(int id);
	Node& loadNode(InputBlob& blob, ShaderType type);
	void loadNodeConnections(InputBlob& blob, Node& node);
	void saveNode(OutputBlob& blob, Node& node);
	void saveNodeConnections(OutputBlob& blob, Node& node);
	bool isFocused() const { return m_is_focused; }
	void undo();
	void redo();

public:
	static const int MAX_TEXTURES_COUNT = 16;

	bool m_is_opened;

private:
	void generateMain(const char* path);
	void generatePasses(OutputBlob& blob);
	void generate(const char* path, ShaderType shader_type);
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
	char m_textures[MAX_TEXTURES_COUNT][50];
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
};


} // namespace Lumix
