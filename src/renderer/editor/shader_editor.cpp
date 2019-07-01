#include "shader_editor.h"
#include "editor/utils.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "renderer/model.h"
#include <cstdio>


namespace Lumix
{


enum class NodeType
{
	VERTEX_INPUT,
	VERTEX_OUTPUT,

	FRAGMENT_INPUT,
	FRAGMENT_OUTPUT,

	CONSTANT,
	SAMPLE,
	MIX,
	UNIFORM,
	VEC4_MERGE,
	SWIZZLE,
	OPERATOR,
	BUILTIN_UNIFORM,
	VERTEX_ID,
	PASS,
	INSTANCE_MATRIX,
	FUNCTION_CALL,
	BINARY_FUNCTION_CALL,
	IF,
	VERTEX_PREFAB
};


struct VertexOutput {
	ShaderEditor::ValueType type;
	StaticString<32> name;
};


static constexpr ShaderEditor::ValueType semanticToType(Mesh::AttributeSemantic semantic) {
	switch (semantic) {
		case Mesh::AttributeSemantic::POSITION: return ShaderEditor::ValueType::VEC3;
		case Mesh::AttributeSemantic::COLOR0: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::COLOR1: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::INDICES: return ShaderEditor::ValueType::IVEC4;
		case Mesh::AttributeSemantic::WEIGHTS: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::NORMAL: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::TANGENT: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::BITANGENT: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::TEXCOORD0: return ShaderEditor::ValueType::VEC2;
		case Mesh::AttributeSemantic::TEXCOORD1: return ShaderEditor::ValueType::VEC2;
		case Mesh::AttributeSemantic::INSTANCE0: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::INSTANCE1: return ShaderEditor::ValueType::VEC4;
		case Mesh::AttributeSemantic::INSTANCE2: return ShaderEditor::ValueType::VEC4;
		default: ASSERT(false); return ShaderEditor::ValueType::VEC4;
	}
}


static constexpr const char* toString(Mesh::AttributeSemantic sem) {

	struct {
		Mesh::AttributeSemantic semantic;
		const char* name;
	} table[] = {
		{ Mesh::AttributeSemantic::POSITION, "position" },
		{ Mesh::AttributeSemantic::NORMAL, "normal" },
		{ Mesh::AttributeSemantic::TANGENT, "tangent" },
		{ Mesh::AttributeSemantic::BITANGENT, "bitangent" },
		{ Mesh::AttributeSemantic::COLOR0, "color 0" },
		{ Mesh::AttributeSemantic::COLOR1, "color 1" },
		{ Mesh::AttributeSemantic::INDICES, "indices" },
		{ Mesh::AttributeSemantic::WEIGHTS, "weights" },
		{ Mesh::AttributeSemantic::TEXCOORD0, "tex coord 0" },
		{ Mesh::AttributeSemantic::TEXCOORD1, "tex coord 1" },
		{ Mesh::AttributeSemantic::INSTANCE0, "instance 0" },
		{ Mesh::AttributeSemantic::INSTANCE1, "instance 1" },
		{ Mesh::AttributeSemantic::INSTANCE2, "instance 2" }
	};

	for (const auto& i : table) {
		if (i.semantic == sem) return i.name;
	}
	ASSERT(false);
	return "Unknown";
}


static constexpr const char* toString(ShaderEditor::ValueType type) {
	switch (type) {
		case ShaderEditor::ValueType::NONE: return "error";
		case ShaderEditor::ValueType::BOOL: return "bool";
		case ShaderEditor::ValueType::INT: return "int";
		case ShaderEditor::ValueType::FLOAT: return "float";
		case ShaderEditor::ValueType::VEC2: return "vec2";
		case ShaderEditor::ValueType::VEC3: return "vec3";
		case ShaderEditor::ValueType::VEC4: return "vec4";
		case ShaderEditor::ValueType::IVEC4: return "ivec4";
		case ShaderEditor::ValueType::MATRIX3: return "mat3";
		case ShaderEditor::ValueType::MATRIX4: return "mat4";
		default: ASSERT(false); return "Unknown type";
	}
}


static const struct { const char* name; NodeType type; bool is_frag; bool is_vert; } NODE_TYPES[] = {
	{"Mix",					NodeType::MIX,					true,		true},
	{"Sample",				NodeType::SAMPLE,				true,		true},
	{"Input",				NodeType::VERTEX_INPUT,			false,		true},
	{"Output",				NodeType::VERTEX_OUTPUT,		false,		true},
	{"Input",				NodeType::FRAGMENT_INPUT,		true,		false},
	{"Output",				NodeType::FRAGMENT_OUTPUT,		true,		false},
	{"Constant",			NodeType::CONSTANT,				true,		true},
	{"Uniform",				NodeType::UNIFORM,				true,		true},
	{"Vec4 merge",			NodeType::VEC4_MERGE,			true,		true},
	{"Swizzle",				NodeType::SWIZZLE,				true,		true},
	{"Operator",			NodeType::OPERATOR,				true,		true},
	{"Builtin uniforms",	NodeType::BUILTIN_UNIFORM,		true,		true},
	{"Vertex ID",			NodeType::VERTEX_ID,			false,		true},
	{"Pass",				NodeType::PASS,					true,		true},
	{"Instance matrix",		NodeType::INSTANCE_MATRIX,		false,		true},
	{"Function",			NodeType::FUNCTION_CALL,		true,		true},
	{"Binary function",		NodeType::BINARY_FUNCTION_CALL,	true,		true},
	{"If",					NodeType::IF,					true,		true},
	{"Vertex prefab",		NodeType::VERTEX_PREFAB,		false,		true}
};


static const struct {
	Mesh::AttributeSemantic semantics;
	const char* gui_name;
}
VERTEX_INPUTS[] = {
	{ Mesh::AttributeSemantic::POSITION,	"Position"			},
	{ Mesh::AttributeSemantic::NORMAL,		"Normal"			},
	{ Mesh::AttributeSemantic::COLOR0,		"Color 0"			},
	{ Mesh::AttributeSemantic::COLOR1,		"Color 1"			},
	{ Mesh::AttributeSemantic::TANGENT,		"Tangent"			},
	{ Mesh::AttributeSemantic::BITANGENT,	"Bitangent"			},
	{ Mesh::AttributeSemantic::INDICES,		"Indices"			},
	{ Mesh::AttributeSemantic::WEIGHTS,		"Weights"			},
	{ Mesh::AttributeSemantic::TEXCOORD0,	"Texture coord 0"	},
	{ Mesh::AttributeSemantic::TEXCOORD1,	"Texture coord 1"	},
	{ Mesh::AttributeSemantic::INSTANCE0,	"Instance data 0"	},
	{ Mesh::AttributeSemantic::INSTANCE1,	"Instance data 1"	},
	{ Mesh::AttributeSemantic::INSTANCE2,	"Instance data 2"	},
};


static const struct { const char* gui_name;  const char* name; ShaderEditor::ValueType type; } BUILTIN_UNIFORMS[] =
{
	{ "Model matrix",		"u_model[0]",				ShaderEditor::ValueType::MATRIX4 },
	{ "View & Projection",	"u_pass_view_projection",	ShaderEditor::ValueType::MATRIX4 },
	{ "Time",				"u_time",					ShaderEditor::ValueType::FLOAT },
};


static const struct
{
	const char* name;
	ShaderEditor::ValueType(*output_type)(const ShaderEditor::Node& node);
} BINARY_FUNCTIONS[] = {
	{ "dot",		[](const ShaderEditor::Node&){ return ShaderEditor::ValueType::FLOAT; } },
	{ "cross",		[](const ShaderEditor::Node& node){ return node.getInputType(0); } },
	{ "min",		[](const ShaderEditor::Node& node){ return node.getInputType(0); } },
	{ "max",		[](const ShaderEditor::Node& node){ return node.getInputType(0); } },
	{ "distance",	[](const ShaderEditor::Node&){ return ShaderEditor::ValueType::FLOAT; } }
};


static constexpr char* FUNCTIONS[] = {
	"abs",
	"all",
	"any",
	"ceil",
	"cos",
	"exp",
	"exp2",
	"floor",
	"fract",
	"inverse",
	"log",
	"log2",
	"normalize",
	"not",
	"round",
	"sin",
	"sqrt",
	"tan",
	"transpose",
	"trunc"
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


struct VertexOutputNode : public ShaderEditor::Node
{
	explicit VertexOutputNode(ShaderEditor& editor)
		: Node((int)NodeType::VERTEX_OUTPUT, editor)
		, m_varyings(editor.getAllocator())
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_varyings.push({ StaticString<32>("output"), ShaderEditor::ValueType::VEC4 });
	}

	void save(OutputMemoryStream& blob) override
	{
		blob.write(m_varyings.size());
		for (const Varying& v : m_varyings) {
			blob.write(v.type);
			blob.writeString(v.name);
		}
	}

	void load(InputMemoryStream& blob) override
	{
		int c;
		blob.read(c);
		m_varyings.resize(c);
		m_inputs.clear();
		m_inputs.push(nullptr); // gl_Position
		for (int i = 0; i < c; ++i) {
			blob.read(m_varyings[i].type);
			blob.readString(m_varyings[i].name.data, lengthOf(m_varyings[i].name.data));
			m_inputs.push(nullptr);
		}
	}

	void generateBeforeMain(OutputMemoryStream& blob) override
	{
		// TODO handle ifdefs
		for (const Varying& v : m_varyings) {
			blob << "\tout " << toString(v.type) << " " << v.name << ";\n";
		}
	}

	void generate(OutputMemoryStream& blob) override
	{
		if (m_inputs[0]) {
			blob << "\t\tgl_Position = ";
			m_inputs[0]->printReference(blob, this);
			blob << ";\n";
		}
		for (int i = 0, c = m_varyings.size(); i < c; ++i) {
			if (!m_inputs[i + 1]) continue;
			const Varying& v = m_varyings[i];

			blob << "\t\t" << v.name << " = ";
			m_inputs[i + 1]->printReference(blob, this);
			blob << ";\n";
		}
	}

	void onGUI() override
	{
		ImGui::Text("%s", "Vertex position");
		for (int i = 0, c = m_varyings.size(); i < c; ++i) {
			Varying& v = m_varyings[i];
			auto getter = [](void*, int idx, const char** out){
				*out = toString((ShaderEditor::ValueType)idx);
				return true;
			};
			if (ImGui::Button(StaticString<32>("x##x", i))) {
				removeConnection(this, i + 1, true);
				m_inputs.erase(i + 1);
				m_varyings.erase(i);
				--i;
				--c;
				continue;
			}
			ImGui::SameLine();
			ImGui::Combo(StaticString<32>("##t", i), (int*)&v.type, getter, nullptr, (int)ShaderEditor::ValueType::COUNT);
			ImGui::SameLine();
			ImGui::InputTextWithHint(StaticString<32>("##n", i), "Name", v.name.data, sizeof(v.name.data));
		}
		if (ImGui::Button("Add")) {
			m_inputs.push(nullptr);
			m_varyings.push({ StaticString<32>("output"), ShaderEditor::ValueType::VEC4 });
		}
	}

	struct Varying {
		StaticString<32> name;
		ShaderEditor::ValueType type;
	};
	Array<Varying> m_varyings;
};


struct VertexInputNode : public ShaderEditor::Node
{
	explicit VertexInputNode(ShaderEditor& editor)
		: Node((int)NodeType::VERTEX_INPUT, editor)
		, m_semantics(editor.getAllocator())
	{
		for (int i = 0; i < (int)Mesh::AttributeSemantic::COUNT; ++i) {
			m_outputs.push(nullptr);
			m_semantics.push((Mesh::AttributeSemantic)i);
		}
	}


	void save(OutputMemoryStream& blob) override
	{
		blob.write(m_outputs.size()); 
		for(Mesh::AttributeSemantic i : m_semantics) {
			blob.write(i);
		}
	}


	void load(InputMemoryStream& blob) override
	{
		int c;
		blob.read(c);
		m_outputs.clear();
		m_semantics.resize(c);
		for (int i = 0; i < c; ++i) {
			m_outputs.push(nullptr);
			blob.read(m_semantics[i]);
		}
	}


	void printReference(OutputMemoryStream& blob, Node* node) override
	{
		for(int i = 0; i < m_outputs.size(); ++i) {
			if (m_outputs[i] == node) {
				blob << "a" << i;
				break;
			}
		}
	}


	ShaderEditor::ValueType getOutputType(int idx) const override
	{
		return semanticToType(m_semantics[idx]);
	}


	void generateBeforeMain(OutputMemoryStream& blob) override
	{
		// TODO handle ifdefs
		for (int i = 0; i < m_outputs.size(); ++i) {
			blob << "\tin " << toString(getOutputType(0)) << " a" << i << ";\n";
		}
	}


	void onGUI() override
	{
		for (int i = 0; i < m_outputs.size(); ++i) {
			if (ImGui::Button(StaticString<32>("x##del", i))) {
				removeConnection(this, i, false);
				m_outputs.erase(i);
				m_semantics.erase(i);
				--i;
				continue;
			}
			ImGui::SameLine();
			auto getter = [](void*, int idx, const char** out){
				*out = toString((Mesh::AttributeSemantic)idx);
				return true;
			};
			int s = (int)m_semantics[i];
			if (ImGui::Combo(StaticString<32>("##cmb", i), &s, getter, nullptr, (int)Mesh::AttributeSemantic::COUNT)) {
				m_semantics[i] = (Mesh::AttributeSemantic)s;
			}
		}
		if (ImGui::Button("Add")) {
			m_outputs.push(nullptr);
			m_semantics.push(Mesh::AttributeSemantic::POSITION);
		}
	}

	Array<Mesh::AttributeSemantic> m_semantics;
};


struct ShaderEditor::ICommand
{
	explicit ICommand(ShaderEditor& editor)
		: m_editor(editor)
	{
	}

	virtual ~ICommand() = default;

	virtual void execute() = 0;
	virtual void undo() = 0;
	virtual bool merge(ICommand& /*command*/) { return false; }
	virtual u32 getType() const = 0;

	ShaderEditor& m_editor;
};


void ShaderEditor::Node::printReference(OutputMemoryStream& blob, Node*)
{
	blob << "v" << m_id;
}


ShaderEditor::Node::Node(int type, ShaderEditor& editor)
	: m_inputs(editor.m_allocator)
	, m_outputs(editor.m_allocator)
	, m_type(type)
	, m_editor(editor)
	, m_id(0xffffFFFF)
{
}


ShaderEditor::ValueType ShaderEditor::Node::getInputType(int index) const
{
	if(!m_inputs[index]) return ShaderEditor::ValueType::NONE;

	int output_idx = m_inputs[index]->m_outputs.indexOf(this);
	return m_inputs[index]->getOutputType(output_idx);
}


void ShaderEditor::Node::generateRecursive(OutputMemoryStream& blob)
{
	for (Node* i : m_inputs) {
		if(i) i->generateRecursive(blob);
	}
	generate(blob);
}


void ShaderEditor::Node::onNodeGUI()
{
	ImGui::PushItemWidth(120);
	onGUI();
	ImGui::PopItemWidth();
}


struct OperatorNode : public ShaderEditor::Node
{
	enum Operation : int
	{
		ADD,
		SUB,
		MUL,
		DIV,
		LT,
		LTE,
		GT,
		GTE,
		EQ,
		NEQ,
		BIT_AND,
		BIT_OR,
		
		COUNT
	};

	explicit OperatorNode(ShaderEditor& editor)
		: Node((int)NodeType::OPERATOR, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
		m_operation = MUL;
	}

	void save(OutputMemoryStream& blob) override { int o = m_operation; blob.write(o); }
	void load(InputMemoryStream& blob) override { int o; blob.read(o); m_operation = (Operation)o; }

	ShaderEditor::ValueType getOutputType(int) const override
	{
		switch (m_operation) {
			case LT:
			case LTE:
			case GT:
			case GTE:
			case EQ:
			case NEQ:
				// TODO bvec*
				return ShaderEditor::ValueType::BOOL;
				break;
		}
		// TODO float * vec4 and others
		return getInputType(0);
	}

	static const char* toString(Operation op) {
		switch (op) {
			case BIT_AND: return "&";
			case BIT_OR: return "|";
			case ADD: return "+";
			case MUL: return "*";
			case DIV: return "/";
			case SUB: return "-";
			case LT: return "<";
			case LTE: return "<=";
			case GT: return ">";
			case GTE: return ">=";
			case EQ: return "==";
			case NEQ: return "!=";
			default: ASSERT(false); return "Unknown";
		}
	}

	void printReference(OutputMemoryStream& blob, Node*) override
	{
		if (!m_inputs[0] || !m_inputs[1]) return; 
		blob << "(";
		m_inputs[0]->printReference(blob, this);
		blob << ") " << toString(m_operation) << " (";
		m_inputs[1]->printReference(blob, this);
		blob << ")";
	}

	void onGUI() override
	{
		ImGui::Text("A");
		ImGui::Text("B");
		int o = m_operation;
		auto getter = [](void*, int idx, const char** out){
			*out = toString((Operation)idx);
			return true;
		};
		if (ImGui::Combo("Op", &o, getter, nullptr, (int)Operation::COUNT)) {
			m_operation = (Operation)o;
		}
	}

	Operation m_operation;
};


struct SwizzleNode : public ShaderEditor::Node
{
	explicit SwizzleNode(ShaderEditor& editor)
		: Node((int)NodeType::SWIZZLE, editor)
	{
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
		m_swizzle = "xyzw";
	}
	
	void save(OutputMemoryStream& blob) override { blob.write(m_swizzle); }
	void load(InputMemoryStream& blob) override { blob.read(m_swizzle); }
	ShaderEditor::ValueType getOutputType(int idx) const override { 
		// TODO other types, e.g. ivec4...
		switch(stringLength(m_swizzle)) {
			case 0: return ShaderEditor::ValueType::NONE;
			case 1: return ShaderEditor::ValueType::FLOAT;
			case 2: return ShaderEditor::ValueType::VEC2;
			case 3: return ShaderEditor::ValueType::VEC3;
			case 4: return ShaderEditor::ValueType::VEC4;
			default: ASSERT(false); return ShaderEditor::ValueType::NONE;
		}
	}
	
	void printReference(OutputMemoryStream& blob, Node* node) override {
		if (!m_inputs[0]) return;
		
		blob << "(";
		m_inputs[0]->printReference(blob, this);
		blob << ")." << m_swizzle;
	}

	void onGUI() override {
		ImGui::InputTextWithHint("", "swizzle", m_swizzle.data, sizeof(m_swizzle.data));
	}

	StaticString<5> m_swizzle;
};


struct Vec4MergeNode : public ShaderEditor::Node
{
	explicit Vec4MergeNode(ShaderEditor& editor)
		: Node((int)NodeType::VEC4_MERGE, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}


	void save(OutputMemoryStream&) override {}
	void load(InputMemoryStream&) override {}
	ShaderEditor::ValueType getOutputType(int) const override { return ShaderEditor::ValueType::VEC4; }

	void generate(OutputMemoryStream& blob) override 
	{
		blob << "\t\tvec4 v" << m_id << ";\n";

		if (m_inputs[0]) 
		{
			blob << "\t\tv" << m_id << ".xyz = ";
			m_inputs[0]->printReference(blob, this);
			blob << ";\n";
		}
		if (m_inputs[1])
		{
			blob << "\t\tv" << m_id << ".x = ";
			m_inputs[1]->printReference(blob, this);
			blob << ";\n";
		}
		if (m_inputs[2])
		{
			blob << "\t\tv" << m_id << ".y = ";
			m_inputs[2]->printReference(blob, this);
			blob << ";\n";
		}
		if (m_inputs[3])
		{
			blob << "\t\tv" << m_id << ".z = ";
			m_inputs[3]->printReference(blob, this);
			blob << ";\n";
		}
		if (m_inputs[4])
		{
			blob << "\t\tv" << m_id << ".w = ";
			m_inputs[4]->printReference(blob, this);
			blob << ";\n";
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


struct FunctionCallNode : public ShaderEditor::Node
{
	explicit FunctionCallNode(ShaderEditor& editor)
		: Node((int)NodeType::FUNCTION_CALL, editor)
	{
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
		m_function = 0;
	}


	void save(OutputMemoryStream& blob) override { blob.write(m_function); }
	void load(InputMemoryStream& blob) override { blob.read(m_function); }
	ShaderEditor::ValueType getOutputType(int) const override
	{
		if (m_inputs[0]) return getInputType(0);
		return ShaderEditor::ValueType::NONE;
	}


	void generate(OutputMemoryStream& blob) override
	{
		blob << "\t\t" << toString(getOutputType(0)) << " v" << m_id << " = " << FUNCTIONS[m_function] << "(";
		if (m_inputs[0]) {
			m_inputs[0]->printReference(blob, this);
		}
		else {
			blob << "0";
		}
		blob << ");\n";
	}


	void onGUI() override
	{
		ImGui::Text("value");

		auto getter = [](void* data, int idx, const char** out_text) -> bool {
			*out_text = FUNCTIONS[idx];
			return true;
		};
		ImGui::Combo("Function", &m_function, getter, nullptr, lengthOf(FUNCTIONS));
	}

	int m_function;
};


struct BinaryFunctionCallNode : public ShaderEditor::Node
{
	explicit BinaryFunctionCallNode(ShaderEditor& editor)
		: Node((int)NodeType::BINARY_FUNCTION_CALL, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
		m_function = 0;
	}


	void save(OutputMemoryStream& blob) override { blob.write(m_function); }
	void load(InputMemoryStream& blob) override { blob.read(m_function); }
	ShaderEditor::ValueType getOutputType(int) const override
	{
		return BINARY_FUNCTIONS[m_function].output_type(*this);
	}


	void generate(OutputMemoryStream& blob) override
	{
		blob << "\t\t" << toString(getOutputType(0)) << " v" << m_id << " = " << BINARY_FUNCTIONS[m_function].name << "(";
		if (m_inputs[0]) {
			m_inputs[0]->printReference(blob, this);
		}
		else {
			blob << "0";
		}
		blob << ", ";
		if (m_inputs[1]) {
			m_inputs[1]->printReference(blob, this);
		}
		else {
			blob << "0";
		}
		blob << ");\n";
	}


	void onGUI() override
	{
		ImGui::Text("argument 1");
		ImGui::Text("argument 2");

		auto getter = [](void* data, int idx, const char** out_text) -> bool {
			*out_text = BINARY_FUNCTIONS[idx].name;
			return true;
		};
		ImGui::Combo("Function", &m_function, getter, nullptr, lengthOf(BINARY_FUNCTIONS));
	}

	int m_function;
};


struct InstanceMatrixNode : public ShaderEditor::Node
{
	explicit InstanceMatrixNode(ShaderEditor& editor)
		: Node((int)NodeType::INSTANCE_MATRIX, editor)
	{
		m_outputs.push(nullptr);
	}


	void save(OutputMemoryStream&) override {}
	void load(InputMemoryStream&) override {}
	ShaderEditor::ValueType getOutputType(int) const override { return ShaderEditor::ValueType::MATRIX4; }


	void generate(OutputMemoryStream& blob) override
	{
		blob << "\tmat4 v" << m_id << ";\n";

		blob << "\tv" << m_id << "[0] = i_data0;\n";
		blob << "\tv" << m_id << "[1] = i_data1;\n";
		blob << "\tv" << m_id << "[2] = i_data2;\n";
		blob << "\tv" << m_id << "[3] = i_data3;\n";

		blob << "\tv" << m_id << " = transpose(v" << m_id << ");\n";
	}


	void onGUI() override
	{
		ImGui::Text("Instance matrix");
	}
};


struct ConstNode : public ShaderEditor::Node
{
	explicit ConstNode(ShaderEditor& editor)
		: Node((int)NodeType::CONSTANT, editor)
	{
		m_type = ShaderEditor::ValueType::VEC4;
		m_value[0] = m_value[1] = m_value[2] = m_value[3] = 0;
		m_int_value = 0;
		m_outputs.push(nullptr);
	}

	void save(OutputMemoryStream& blob) override
	{
		blob.write(m_value);
		blob.write(m_is_color);
		blob.write(m_type);
		blob.write(m_int_value);
	}

	void load(InputMemoryStream& blob) override 
	{
		blob.read(m_value);
		blob.read(m_is_color);
		blob.read(m_type);
		blob.read(m_int_value);
	}

	ShaderEditor::ValueType getOutputType(int) const override { return m_type; }

	void printReference(OutputMemoryStream& blob, Node*) override
	{
		switch(m_type) {
			case ShaderEditor::ValueType::VEC4:
				blob << "vec4(" << m_value[0] << ", " << m_value[1] << ", "
					<< m_value[2] << ", " << m_value[3] << ")";
				break;
			case ShaderEditor::ValueType::VEC3:
				blob << "vec3(" << m_value[0] << ", " << m_value[1] << ", "
					<< m_value[2] << ")";
				break;
			case ShaderEditor::ValueType::VEC2:
				blob << "vec2(" << m_value[0] << ", " << m_value[1] << ")";
				break;
			case ShaderEditor::ValueType::INT:
				blob << m_int_value;
				break;
			case ShaderEditor::ValueType::FLOAT:
				blob << m_value[0];
				break;
			default: ASSERT(false); break;
		}
	}

	void onGUI() override { 
		auto getter = [](void*, int idx, const char** out){
			*out = toString((ShaderEditor::ValueType)idx);
			return true;
		};
		ImGui::Combo("Type", (int*)&m_type, getter, nullptr, (int)ShaderEditor::ValueType::COUNT);

		switch(m_type) {
			case ShaderEditor::ValueType::VEC4:
				ImGui::Checkbox("Color", &m_is_color);
				if (m_is_color) {
					ImGui::ColorPicker4("", m_value); 
				}
				else {
					ImGui::InputFloat4("", m_value);
				}
				break;
			case ShaderEditor::ValueType::VEC3:
				ImGui::Checkbox("Color", &m_is_color);
				if (m_is_color) {
					ImGui::ColorPicker3("", m_value); 
				}
				else {
					ImGui::InputFloat3("", m_value);
				}
				break;
			case ShaderEditor::ValueType::VEC2:
				ImGui::InputFloat2("", m_value);
				break;
			case ShaderEditor::ValueType::FLOAT:
				ImGui::InputFloat("", m_value);
				break;
			case ShaderEditor::ValueType::INT:
				ImGui::InputInt("", &m_int_value);
				break;
			default: ASSERT(false); break;
		}
	}

	ShaderEditor::ValueType m_type;
	bool m_is_color = false;
	float m_value[4];
	int m_int_value;
};


struct SampleNode : public ShaderEditor::Node
{
	explicit SampleNode(ShaderEditor& editor)
		: Node((int)NodeType::SAMPLE, editor)
	{
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
		m_texture = 0;
	}

	void save(OutputMemoryStream& blob) override { blob.write(m_texture); }
	void load(InputMemoryStream& blob) override { blob.read(m_texture); }
	ShaderEditor::ValueType getOutputType(int) const override { return ShaderEditor::ValueType::VEC4; }

	void generate(OutputMemoryStream& blob) override
	{
		blob << "\t\tvec4 v" << m_id << " = ";
		if (!m_inputs[0]) {
			blob << "vec4(1, 0, 1, 1);\n";
			return;
		}

		blob << "texture(" << m_editor.getTextureName(m_texture) << ", ";
		m_inputs[0]->printReference(blob, this);
		blob << ");\n";
	}

	void onGUI() override
	{
		ImGui::Text("UV");
		auto getter = [](void* data, int idx, const char** out) -> bool {
			*out = ((SampleNode*)data)->m_editor.getTextureName(idx);
			return true;
		};
		ImGui::Combo("Texture", &m_texture, getter, this, ShaderEditor::MAX_TEXTURES_COUNT);
	}

	int m_texture;
};


struct FragmentInputNode : public ShaderEditor::Node
{
	explicit FragmentInputNode(ShaderEditor& editor)
		: Node((int)NodeType::FRAGMENT_INPUT, editor)
		, m_varyings(editor.getAllocator())
	{
		m_outputs.push(nullptr);
		m_varyings.push({StaticString<32>(), ShaderEditor::ValueType::VEC4});
	}

	void save(OutputMemoryStream& blob) override
	{
		blob.write(m_varyings.size());
		for (const VertexOutputNode::Varying& v : m_varyings) {
			blob.write(v.type);
			blob.writeString(v.name);
		}
	}

	void load(InputMemoryStream& blob) override
	{
		int c;
		blob.read(c);
		m_varyings.resize(c);
		m_outputs.clear();
		for (int i = 0; i < c; ++i) {
			blob.read(m_varyings[i].type);
			blob.readString(m_varyings[i].name.data, lengthOf(m_varyings[i].name.data));
			m_outputs.push(nullptr);
		}
	}

	void generateBeforeMain(OutputMemoryStream& blob) override
	{
		// TODO handle ifdefs
		for (const VertexOutputNode::Varying& v : m_varyings) {
			blob << "\tin " << toString(v.type) << " " << v.name << ";\n";
		}
	}

	void printReference(OutputMemoryStream& blob, Node* node) override
	{
		for (int i = 0; i < m_outputs.size(); ++i) {
			if (node == m_outputs[i]) {
				blob << m_varyings[i].name;
				return;
			}
		}
		blob << "vec4(0) // N/A\n";
	}

	void onGUI() override
	{
		for (int i = 0, c = m_varyings.size(); i < c; ++i) {
			VertexOutputNode::Varying& v = m_varyings[i];
			auto getter = [](void*, int idx, const char** out){
				*out = toString((ShaderEditor::ValueType)idx);
				return true;
			};
			if (ImGui::Button(StaticString<32>("x##x", i))) {
				removeConnection(this, i, true);
				m_inputs.erase(i);
				m_varyings.erase(i);
				--i;
				continue;
			}
			ImGui::SameLine();
			ImGui::Combo(StaticString<32>("##t", i), (int*)&v.type, getter, nullptr, (int)ShaderEditor::ValueType::COUNT);
			ImGui::SameLine();
			ImGui::InputTextWithHint(StaticString<32>("##n", i), "Name", v.name.data, sizeof(v.name.data));
		}
	}

	Array<VertexOutputNode::Varying> m_varyings;
};


struct FragmentOutputNode : public ShaderEditor::Node
{
	explicit FragmentOutputNode(ShaderEditor& editor)
		: Node((int)NodeType::FRAGMENT_OUTPUT, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
	}

	void save(OutputMemoryStream& blob) { blob.write(m_inputs.size()); }
	
	void load(InputMemoryStream& blob) {
		int c;
		blob.read(c);
		m_inputs.clear();
		m_inputs.reserve(c);
		for (int i = 0; i < c; ++i) m_inputs.push(nullptr);
	}

	void generateBeforeMain(OutputMemoryStream& blob) override
	{
		for (int i = 1; i < m_inputs.size(); ++i) {
			blob << "\tlayout(location = " << i - 1 << ") out vec4 out" << i - 1 << ";\n";
		}
	}

	void generate(OutputMemoryStream& blob) override
	{
		if (m_inputs[0]) {
			blob << "\t\tif(";
			m_inputs[0]->printReference(blob, this);
			blob << ") discard;\n";
		}

		for(int i = 1; i < m_inputs.size(); ++i) {
			if (m_inputs[i]) {
				blob << "\t\tout" << i - 1 << " = ";
				m_inputs[i]->printReference(blob, this);
				blob << ";\n";
			}
			else {
				blob << "\t\tout" << i - 1 << " = vec4(0, 0, 0, 1);\n";
			}
		}
	}

	void onGUI() override {
		ImGui::Text("Discard");
		for (int i = 1; i < m_inputs.size(); ++i) {
			ImGui::Text("output %d ", i - 1);
		}
		if (ImGui::Button("Add")) {
			m_inputs.push(nullptr);
		}
		if (m_inputs.size() > 1) {
			ImGui::SameLine();
			if (ImGui::Button("Remove")) {
				removeConnection(this, m_inputs.size() - 1, true);
				m_inputs.pop();
			}
		}
	}
};


struct MixNode : public ShaderEditor::Node
{
	explicit MixNode(ShaderEditor& editor)
		: Node((int)NodeType::MIX, editor)
	{
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_outputs.push(nullptr);
	}

	ShaderEditor::ValueType getOutputType(int) const override 
	{
		return getInputType(1);
	}

	void printReference(OutputMemoryStream& blob, Node*) override
	{
		if (!m_inputs[0] || !m_inputs[1] || !m_inputs[2]) return;

		blob << "mix(";
		m_inputs[0]->printReference(blob, this);
		blob << ", ";
		m_inputs[1]->printReference(blob, this);
		blob << ", ";
		m_inputs[2]->printReference(blob, this);
		blob << ")";
	}

	void onGUI() override
	{
		ImGui::Text("Input 1");
		ImGui::Text("Input 2");
		ImGui::Text("Weight");
	}
};


struct PassNode : public ShaderEditor::Node
{
	explicit PassNode(ShaderEditor& editor)
		: Node((int)NodeType::PASS, editor)
	{
		m_outputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_pass[0] = 0;
	}

	void save(OutputMemoryStream& blob) override { blob.writeString(m_pass); }
	void load(InputMemoryStream& blob) override { blob.readString(m_pass, lengthOf(m_pass)); }
	ShaderEditor::ValueType getOutputType(int) const override { return getInputType(0); }

	void generate(OutputMemoryStream& blob) override 
	{
		const char* defs[] = { "\t\t#ifdef ", "\t\t#ifndef " };
		for (int i = 0; i < 2; ++i)
		{
			if (!m_inputs[i]) continue;

			blob << defs[i] << m_pass << "\n";
			blob << "\t\t" << toString(getOutputType(0)) << " v" << m_id << " = ";
			m_inputs[i]->printReference(blob, this);
			blob << ";\n";
			blob << "\t\t#endif // " << m_pass << "\n\n";
		}
	}

	void onGUI() override
	{
		ImGui::Text("if defined");
		ImGui::Text("if not defined");
		ImGui::InputText("Pass", m_pass, sizeof(m_pass));
	}

	char m_pass[50];
};


struct IfNode : public ShaderEditor::Node
{
	explicit IfNode(ShaderEditor& editor)
		: Node((int)NodeType::IF, editor)
	{
		m_outputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
		m_inputs.push(nullptr);
	}

	void save(OutputMemoryStream& blob) override {}
	void load(InputMemoryStream& blob) override {}

	ShaderEditor::ValueType getOutputType(int) const override
	{
		if (!m_inputs[0]) return ShaderEditor::ValueType::NONE;
		return getInputType(1);
	}

	void generate(OutputMemoryStream& blob) override 
	{
		blob << "\t\t" << toString(getOutputType(0)) << " v" << m_id << ";\n";
		if(!m_inputs[0]) return;
		if (m_inputs[1]) {
			blob << "\t\tif(";
			m_inputs[0]->printReference(blob, this);
			blob << ") {\n";
			blob << "\t\t\tv" << m_id << " = ";
			m_inputs[1]->printReference(blob, this);
			blob << ";\n";
			blob << "\t\t}\n";

			if(m_inputs[2]) {
				blob << "else {\n";
				blob << "\t\t\tv" << m_id << " = ";
				m_inputs[1]->printReference(blob, this);
				blob << ";\n";
				blob << "\t\t}\n";
			}
		}
		else if(m_inputs[2]) {
			blob << "\t\tif(!(";
			m_inputs[0]->printReference(blob, this);
			blob << ")) {\n";
			blob << "\t\t\tv" << m_id << " = ";
			m_inputs[1]->printReference(blob, this);
			blob << ";\n";
			blob << "\t\t}\n";
		}
	}

	void onGUI() override
	{
		ImGui::Text("condition");
		ImGui::Text("if");
		ImGui::Text("else");
	}
};


struct VertexPrefabNode : ShaderEditor::Node
{
	enum class Type : int {
		FULLSCREEN_POSITION,

		COUNT
	};

	explicit VertexPrefabNode(ShaderEditor& editor)
		: Node((int)NodeType::VERTEX_PREFAB, editor)
	{
		m_outputs.push(nullptr);
	}

	void save(OutputMemoryStream& blob) override {}
	void load(InputMemoryStream& blob) override {}

	void printReference(OutputMemoryStream& blob, Node*) override
	{
		switch(m_type) {
			case Type::FULLSCREEN_POSITION: blob << "vec4((gl_VertexID & 1) * 2 - 1, (gl_VertexID & 2) - 1, 0, 1)"; break;
			default: ASSERT(false); break;
		}
	}

	ShaderEditor::ValueType getOutputType(int) const override
	{
		return ShaderEditor::ValueType::VEC4;
	}

	static const char* toString(Type type) {
		switch(type) {
			case Type::FULLSCREEN_POSITION: return "fullscreen position"; 
			default: ASSERT(false); return "Unknown";
		}
	}

	void onGUI() override
	{
		auto getter = [](void*, int idx, const char** out){
			*out = toString((Type)idx);
			return true;
		};
		ImGui::Combo("", (int*)&m_type, getter, nullptr, (int)Type::COUNT);
	}

	Type m_type = Type::FULLSCREEN_POSITION;
};


struct VertexIDNode : ShaderEditor::Node
{
	explicit VertexIDNode(ShaderEditor& editor)
		: Node((int)NodeType::VERTEX_ID, editor)
	{
		m_outputs.push(nullptr);
	}

	void save(OutputMemoryStream& blob) override {}
	void load(InputMemoryStream& blob) override {}

	void printReference(OutputMemoryStream& blob, Node*) override
	{
		blob << "gl_VertexID";
	}

	ShaderEditor::ValueType getOutputType(int) const override
	{
		return ShaderEditor::ValueType::INT;
	}

	void onGUI() override
	{
		ImGui::Text("Vertex ID");
	}
};


struct BuiltinUniformNode : ShaderEditor::Node
{
	explicit BuiltinUniformNode(ShaderEditor& editor)
		: Node((int)NodeType::BUILTIN_UNIFORM, editor)
	{
		m_outputs.push(nullptr);
		m_uniform = 0;
	}


	void save(OutputMemoryStream& blob) override { blob.write(m_uniform); }
	void load(InputMemoryStream& blob) override { blob.read(m_uniform); }


	void printReference(OutputMemoryStream& blob, Node*) override
	{
		blob << BUILTIN_UNIFORMS[m_uniform].name;
	}

	ShaderEditor::ValueType getOutputType(int) const override
	{
		return BUILTIN_UNIFORMS[m_uniform].type;
	}

	void onGUI() override
	{
		auto getter = [](void* data, int index, const char** out_text) -> bool {
			*out_text = BUILTIN_UNIFORMS[index].gui_name;
			return true;
		};
		ImGui::Combo("Uniform", (int*)&m_uniform, getter, nullptr, lengthOf(BUILTIN_UNIFORMS));
	}

	int m_uniform;
};


struct UniformNode : public ShaderEditor::Node
{
	explicit UniformNode(ShaderEditor& editor)
		: Node((int)NodeType::UNIFORM, editor)
	{
		m_outputs.push(nullptr);
		m_value_type = ShaderEditor::ValueType::VEC4;
	}

	void save(OutputMemoryStream& blob) override { blob.write(m_type); blob.writeString(m_name); }
	void load(InputMemoryStream& blob) override { blob.read(m_type); blob.readString(m_name.data, lengthOf(m_name.data)); }
	ShaderEditor::ValueType getOutputType(int) const override { return m_value_type; }


	void printReference(OutputMemoryStream& blob, Node*) override
	{
		blob << m_name;
	}


	void generateBeforeMain(OutputMemoryStream& blob) override
	{
		blob << "\tuniform " << toString(m_value_type) << " " << m_name << ";\n";
	}


	void onGUI() override
	{
		auto getter = [](void*, int idx, const char** out){
			*out = toString((ShaderEditor::ValueType)idx);
			return true;
		};
		ImGui::Combo("Type", (int*)&m_value_type, getter, nullptr, (int)ShaderEditor::ValueType::COUNT);
		ImGui::InputText("Name", m_name.data, sizeof(m_name.data));
	}

	StaticString<50> m_name;
	ShaderEditor::ValueType m_value_type;
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
		m_old_pos = m_editor.getNodeByID(m_node)->m_pos;
	}


	u32 getType() const override
	{
		static const u32 crc = crc32("move_node");
		return crc;
	}


	void execute() override
	{
		auto* node = m_editor.getNodeByID(m_node);
		node->m_pos = m_new_pos;
	}


	void undo() override
	{
		auto* node = m_editor.getNodeByID(m_node);
		node->m_pos = m_old_pos;
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
			m_before_to = before_to->m_id;
		}
		else
		{
			m_before_to = m_before_to_pin = -1;
		}

		auto* before_from = to_node->m_inputs[m_from_pin];
		if(before_from)
		{
			m_before_from_pin = before_from->m_outputs.indexOf(to_node);
			m_before_from = before_from->m_id;
		}
		else
		{
			m_before_from = m_before_from_pin = -1;
		}
	}


	u32 getType() const override
	{
		static const u32 crc = crc32("create_connection");
		return crc;
	}


	void execute() override
	{
		auto* from_node = m_editor.getNodeByID(m_from);
		auto* to_node = m_editor.getNodeByID(m_to);

		removeConnection(from_node, m_from_pin, false);
		removeConnection(to_node, m_to_pin, true);

		from_node->m_outputs[m_from_pin] = to_node;
		to_node->m_inputs[m_to_pin] = from_node;
	}


	void undo() override
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
	RemoveNodeCommand(int node_id, ShaderEditor::ShaderType shader_type, ShaderEditor& editor)
		: ICommand(editor)
		, m_node_id(node_id)
		, m_blob(editor.getAllocator())
		, m_shader_type(shader_type)
	{
	}


	u32 getType() const override
	{
		static const u32 crc = crc32("remove_node");
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
		InputMemoryStream blob(m_blob);
		auto& node = m_editor.loadNode(blob, m_shader_type);
		m_editor.loadNodeConnections(blob, node);
	}


	ShaderEditor::ShaderType m_shader_type;
	OutputMemoryStream m_blob;
	int m_node_id;
};


struct CreateNodeCommand : public ShaderEditor::ICommand
{
	CreateNodeCommand(int id, NodeType type, ShaderEditor::ShaderType shader_type, ImVec2 pos, ShaderEditor& editor)
		: m_type(type)
		, m_pos(pos)
		, m_id(id)
		, m_shader_type(shader_type)
		, m_node(nullptr)
		, ICommand(editor)
	{
	}


	u32 getType() const override
	{
		static const u32 crc = crc32("create_node");
		return crc;
	}


	void execute() override
	{
		m_node = m_editor.createNode((int)m_type);
		m_editor.addNode(m_node, m_pos, m_shader_type);
		if(m_id >= 0) m_node->m_id = m_id;
	}


	void undo() override
	{
		m_id = m_node->m_id;
		m_editor.destroyNode(m_node);
	}


	int m_id;
	ShaderEditor::ShaderType m_shader_type;
	ShaderEditor::Node* m_node;
	NodeType m_type;
	ImVec2 m_pos;
};


ShaderEditor::ShaderEditor(IAllocator& allocator)
	: m_fragment_nodes(allocator)
	, m_vertex_nodes(allocator)
	, m_allocator(allocator)
	, m_undo_stack(allocator)
	, m_source(allocator)
	, m_undo_stack_idx(-1)
	, m_current_node_id(-1)
	, m_is_focused(false)
	, m_is_open(false)
	, m_current_shader_type(ShaderType::VERTEX)
{
	newGraph();
}


ShaderEditor::~ShaderEditor()
{
	clear();
}


ShaderEditor::Node* ShaderEditor::getNodeByID(int id)
{
	for(auto* node : m_fragment_nodes)
	{
		if(node->m_id == id) return node;
	}

	for(auto* node : m_vertex_nodes)
	{
		if(node->m_id == id) return node;
	}

	return nullptr;
}


void ShaderEditor::generate(const char* sed_path, bool save_file)
{
	OutputMemoryStream blob(m_allocator);
	blob.reserve(8192);

	for (int i = 0; i < lengthOf(m_textures); ++i) {
		if (!m_textures[i][0]) continue;

		blob << "texture_slot {\n";
		blob << "\tname = \"" << m_textures[i] << "\",\n";
		blob << "\tdefault_texture = \"textures/common/white.tga\"\n";
		blob << "}\n\n";
	}

	// TODO
	/*
	for (const Attribute& attr : m_attributes) {
		blob << "attribute { name = \"" << attr.name << "\", semantic = " << toString((Mesh::AttributeSemantic)attr.semantic) << " }\n";
	}
	*/

	blob << "include \"pipelines/common.glsl\"\n\n";


	auto writeShader = [&](const char* shader_type, const Array<Node*>& nodes){
		blob << shader_type << "_shader [[\n";

		for (int i = 0; i < lengthOf(m_textures); ++i) {
			if (!m_textures[i][0]) continue;

			blob << "\tlayout (binding=" << i << ") uniform sampler2D " << m_textures[i] << ";\n";
		}

		for (auto* node : nodes) {
			node->generateBeforeMain(blob);
		}

		blob << "\tvoid main() {\n";
		for(auto& node : nodes)
		{
			if (node->m_type == (int)NodeType::FRAGMENT_OUTPUT ||
				node->m_type == (int)NodeType::VERTEX_OUTPUT)
			{
				node->generateRecursive(blob);
			}
		}
		blob << "\t}\n";

		blob << "]]\n\n";
	};

	writeShader("fragment", m_fragment_nodes);
	writeShader("vertex", m_vertex_nodes);

	if (save_file) {
		PathUtils::FileInfo fi(sed_path);
		StaticString<MAX_PATH_LENGTH> path(fi.m_dir, fi.m_basename, ".shd");
		OS::OutputFile file;
		if (!file.open(path)) {
			logError("Editor") << "Could not create file " << path;
			return;
		}

		file.write(blob.getData(), blob.getPos());
		file.close();
	}

	m_source.resize((int)blob.getPos());
	copyMemory(m_source.getData(), blob.getData(), m_source.length() + 1);
}


void ShaderEditor::addNode(Node* node, const ImVec2& pos, ShaderType type)
{
	if(type == ShaderType::FRAGMENT)
	{
		m_fragment_nodes.push(node);
	}
	else
	{
		m_vertex_nodes.push(node);
	}

	node->m_pos = pos;
	node->m_id = ++m_last_node_id;
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
			m_new_link_info.from->m_id, m_new_link_info.from_pin_index, node->m_id, pin_index, *this));
	}
	else
	{
		execute(LUMIX_NEW(m_allocator, CreateConnectionCommand)(
			node->m_id, pin_index, m_new_link_info.from->m_id, m_new_link_info.from_pin_index, *this));
	}
}


void ShaderEditor::saveNode(OutputMemoryStream& blob, Node& node)
{
	int type = (int)node.m_type;
	blob.write(node.m_id);
	blob.write(type);
	blob.write(node.m_pos);

	node.save(blob);
}


void ShaderEditor::saveNodeConnections(OutputMemoryStream& blob, Node& node)
{
	int inputs_count = node.m_inputs.size();
	blob.write(inputs_count);
	for(int i = 0; i < inputs_count; ++i)
	{
		int tmp = node.m_inputs[i] ? node.m_inputs[i]->m_id : -1;
		blob.write(tmp);
		tmp = node.m_inputs[i] ? node.m_inputs[i]->m_outputs.indexOf(&node) : -1;
		blob.write(tmp);
	}

	int outputs_count = node.m_outputs.size();
	blob.write(outputs_count);
	for(int i = 0; i < outputs_count; ++i)
	{
		int tmp = node.m_outputs[i] ? node.m_outputs[i]->m_id : -1;
		blob.write(tmp);
		tmp = node.m_outputs[i] ? node.m_outputs[i]->m_inputs.indexOf(&node) : -1;
		blob.write(tmp);
	}
}


void ShaderEditor::save(const char* path)
{
	OS::OutputFile file;
	if(!file.open(path)) 
	{
		logError("Editor") << "Could not save shader " << path;
		return;
	}

	OutputMemoryStream blob(m_allocator);
	blob.reserve(4096);
	for (int i = 0; i < lengthOf(m_textures); ++i)
	{
		blob.writeString(m_textures[i]);
	}

	int nodes_count = m_vertex_nodes.size();
	blob.write(nodes_count);
	for(auto* node : m_vertex_nodes)
	{
		saveNode(blob, *node);
	}

	for(auto* node : m_vertex_nodes)
	{
		saveNodeConnections(blob, *node);
	}

	nodes_count = m_fragment_nodes.size();
	blob.write(nodes_count);
	for (auto* node : m_fragment_nodes)
	{
		saveNode(blob, *node);
	}

	for (auto* node : m_fragment_nodes)
	{
		saveNodeConnections(blob, *node);
	}

	bool success = file.write(blob.getData(), blob.getPos());
	file.close();
	if (!success)
	{
		logError("Editor") << "Could not save shader " << path;
	}
}


void ShaderEditor::clear()
{
	for (auto* node : m_fragment_nodes)
	{
		LUMIX_DELETE(m_allocator, node);
	}
	m_fragment_nodes.clear();

	for(auto* node : m_vertex_nodes)
	{
		LUMIX_DELETE(m_allocator, node);
	}
	m_vertex_nodes.clear();

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
	switch ((NodeType)type) {
		case NodeType::FRAGMENT_OUTPUT:				return LUMIX_NEW(m_allocator, FragmentOutputNode)(*this);
		case NodeType::VERTEX_OUTPUT:				return LUMIX_NEW(m_allocator, VertexOutputNode)(*this);
		case NodeType::FRAGMENT_INPUT:				return LUMIX_NEW(m_allocator, FragmentInputNode)(*this);
		case NodeType::VERTEX_INPUT:				return LUMIX_NEW(m_allocator, VertexInputNode)(*this);
		case NodeType::CONSTANT:					return LUMIX_NEW(m_allocator, ConstNode)(*this);
		case NodeType::MIX:							return LUMIX_NEW(m_allocator, MixNode)(*this);
		case NodeType::SAMPLE:						return LUMIX_NEW(m_allocator, SampleNode)(*this);
		case NodeType::UNIFORM:						return LUMIX_NEW(m_allocator, UniformNode)(*this);
		case NodeType::SWIZZLE:						return LUMIX_NEW(m_allocator, SwizzleNode)(*this);
		case NodeType::VEC4_MERGE:					return LUMIX_NEW(m_allocator, Vec4MergeNode)(*this);
		case NodeType::OPERATOR:					return LUMIX_NEW(m_allocator, OperatorNode)(*this);
		case NodeType::BUILTIN_UNIFORM:				return LUMIX_NEW(m_allocator, BuiltinUniformNode)(*this);
		case NodeType::VERTEX_ID	:				return LUMIX_NEW(m_allocator, VertexIDNode)(*this);
		case NodeType::PASS:						return LUMIX_NEW(m_allocator, PassNode)(*this);
		case NodeType::IF:							return LUMIX_NEW(m_allocator, IfNode)(*this);
		case NodeType::INSTANCE_MATRIX:				return LUMIX_NEW(m_allocator, InstanceMatrixNode)(*this);
		case NodeType::FUNCTION_CALL:				return LUMIX_NEW(m_allocator, FunctionCallNode)(*this);
		case NodeType::BINARY_FUNCTION_CALL:		return LUMIX_NEW(m_allocator, BinaryFunctionCallNode)(*this);
		case NodeType::VERTEX_PREFAB:				return LUMIX_NEW(m_allocator, VertexPrefabNode)(*this);
	}

	ASSERT(false);
	return nullptr;
}


ShaderEditor::Node& ShaderEditor::loadNode(InputMemoryStream& blob, ShaderType shader_type)
{
	int type;
	int id;
	blob.read(id);
	blob.read(type);
	Node* node = createNode(type);
	node->m_id = id;
	if(shader_type == ShaderType::FRAGMENT) {
		m_fragment_nodes.push(node);
	}
	else {
		m_vertex_nodes.push(node);
	}
	blob.read(node->m_pos);

	node->load(blob);
	return *node;
}


void ShaderEditor::loadNodeConnections(InputMemoryStream& blob, Node& node)
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
	char path[MAX_PATH_LENGTH];
	if (!OS::getOpenFilename(path, lengthOf(path), "Shader edit data\0*.sed\0", nullptr))
	{
		return;
	}
	m_path = path;

	clear();

	OS::InputFile file;
	if (!file.open(path))
	{
		logError("Editor") << "Failed to load shader " << path;
		return;
	}

	int data_size = (int)file.size();
	Array<u8> data(m_allocator);
	data.resize(data_size);
	if (!file.read(&data[0], data_size))
	{
		logError("Editor") << "Failed to load shader " << path;
		file.close();
		return;
	}
	file.close();

	InputMemoryStream blob(&data[0], data_size);
	for (int i = 0; i < lengthOf(m_textures); ++i)
	{
		blob.readString(m_textures[i].data, lengthOf(m_textures[i].data));
	}

	int size;
	blob.read(size);
	for(int i = 0; i < size; ++i)
	{
		loadNode(blob, ShaderType::VERTEX);
	}

	for(auto* node : m_vertex_nodes)
	{
		loadNodeConnections(blob, *node);
		m_last_node_id = maximum(int(node->m_id + 1), int(m_last_node_id));
	}

	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		loadNode(blob, ShaderType::FRAGMENT);
	}

	for (auto* node : m_fragment_nodes)
	{
		loadNodeConnections(blob, *node);
		m_last_node_id = maximum(int(node->m_id + 1), int(m_last_node_id));
	}
}


bool ShaderEditor::getSavePath()
{
	char path[MAX_PATH_LENGTH];
	if (OS::getSaveFilename(path, lengthOf(path), "Shader edit data\0*.sed\0", "sed"))
	{
		m_path = path;
		return true;
	}
	return false;
}


static ImVec2 operator+(const ImVec2& a, const ImVec2& b)
{
	return ImVec2(a.x + b.x, a.y + b.y);
}


static ImVec2 operator-(const ImVec2& a, const ImVec2& b)
{
	return ImVec2(a.x - b.x, a.y - b.y);
}


void ShaderEditor::onGUIRightColumn()
{
	ImGui::BeginChild("right_col");

	if(ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
	{
		m_canvas_pos = m_canvas_pos + ImGui::GetIO().MouseDelta;
	}

	int current_shader = (int)m_current_shader_type;
	if(ImGui::Combo("Shader", &current_shader, "Vertex\0Fragment\0"))
	{
		m_current_shader_type = (ShaderType)current_shader;
	}

	auto cursor_screen_pos = ImGui::GetCursorScreenPos();

	auto& nodes = m_current_shader_type == ShaderType::FRAGMENT ? m_fragment_nodes : m_vertex_nodes;
	for(auto* node : nodes)
	{
		auto node_screen_pos = cursor_screen_pos + node->m_pos + m_canvas_pos;

		ImGui::BeginNode(node->m_id, node_screen_pos);
		node->onNodeGUI();
		ImGui::EndNode(node_screen_pos);
		if(ImGui::IsItemHovered() && ImGui::IsMouseDown(1))
		{
			m_current_node_id = node->m_id;
		}

		for(int i = 0; i < node->m_outputs.size(); ++i)
		{
			Node* output = node->m_outputs[i];
			if(!output) continue;

			auto output_pos = ImGui::GetNodeOutputPos(node->m_id, i);
			auto input_pos = ImGui::GetNodeInputPos(output->m_id, output->m_inputs.indexOf(node));
			ImGui::NodeLink(output_pos, input_pos);
		}

		for(int i = 0; i < node->m_outputs.size(); ++i)
		{
			auto pin_pos = ImGui::GetNodeOutputPos(node->m_id, i);
			if(ImGui::NodePin(i, pin_pos))
			{
				if(ImGui::IsMouseReleased(0) && m_new_link_info.is_active)
				{
					createConnection(node, i, false);
				}
				if(ImGui::IsMouseClicked(0)) nodePinMouseDown(node, i, false);
			}
		}

		for(int i = 0; i < node->m_inputs.size(); ++i)
		{
			auto pin_pos = ImGui::GetNodeInputPos(node->m_id, i);
			if(ImGui::NodePin(i + node->m_outputs.size(), pin_pos))
			{
				if(ImGui::IsMouseReleased(0) && m_new_link_info.is_active)
				{
					createConnection(node, i, true);
				}
				if(ImGui::IsMouseClicked(0)) nodePinMouseDown(node, i, true);
			}
		}

		ImVec2 new_pos = node_screen_pos - cursor_screen_pos - m_canvas_pos;
		if(new_pos.x != node->m_pos.x || new_pos.y != node->m_pos.y)
		{
			execute(LUMIX_NEW(m_allocator, MoveNodeCommand)(node->m_id, new_pos, *this));
		}
	}

	if(m_new_link_info.is_active && ImGui::IsMouseDown(0))
	{
		if(m_new_link_info.is_from_input)
		{
			auto pos = ImGui::GetNodeInputPos(
				m_new_link_info.from->m_id, m_new_link_info.from_pin_index);
			ImGui::NodeLink(ImGui::GetMousePos(), pos);
		}
		else
		{
			auto pos = ImGui::GetNodeOutputPos(
				m_new_link_info.from->m_id, m_new_link_info.from_pin_index);
			ImGui::NodeLink(pos, ImGui::GetMousePos());
		}
	}
	else
	{
		m_new_link_info.is_active = false;
	}

	if(ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered())
	{
		ImGui::OpenPopup("context_menu");
	}

	if(ImGui::BeginPopup("context_menu"))
	{
		ImVec2 add_pos(ImGui::GetMousePos() - cursor_screen_pos - m_canvas_pos);
		if(m_current_node_id >= 0)
		{
			if(ImGui::MenuItem("Remove"))
			{
				execute(LUMIX_NEW(m_allocator, RemoveNodeCommand)(m_current_node_id, m_current_shader_type, *this));
				m_current_node_id = -1;
			}
		}

		if (ImGui::BeginMenu("Add"))
		{
			for (auto node_type : NODE_TYPES)
			{
				if (!node_type.is_frag && m_current_shader_type == ShaderType::FRAGMENT) continue;
				if (!node_type.is_vert && m_current_shader_type == ShaderType::VERTEX) continue;

				if (ImGui::MenuItem(node_type.name))
				{
					execute(LUMIX_NEW(m_allocator, CreateNodeCommand)(
						-1, node_type.type, m_current_shader_type, add_pos, *this));
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}
	ImGui::EndChild();
}


void ShaderEditor::onGUILeftColumn()
{
	ImGui::BeginChild("left_col", ImVec2(m_left_col_width, 0));
	ImGui::PushItemWidth(m_left_col_width);

	if (ImGui::CollapsingHeader("Textures")) {
		for (int i = 0; i < lengthOf(m_textures); ++i) {
			ImGui::InputText(StaticString<10>("###tex", i), m_textures[i].data, sizeof(m_textures[i]));
		}
	}

	if (ImGui::CollapsingHeader("Source")) {
		if (m_source.length() == 0) {
			ImGui::Text("Empty");
		}
		else {
			ImGui::InputTextMultiline("", m_source.getData(), m_source.length(), ImVec2(0, 300), ImGuiInputTextFlags_ReadOnly);
		}
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

	generate("", false);
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
	m_vertex_nodes.eraseItem(node);
}


void ShaderEditor::newGraph()
{
	clear();

	for (auto& t : m_textures) t = "";
	m_last_node_id = 0;
	m_new_link_info.is_active = false;
	m_path = "";

	m_fragment_nodes.push(LUMIX_NEW(m_allocator, FragmentOutputNode)(*this));
	m_fragment_nodes.back()->m_pos.x = 50;
	m_fragment_nodes.back()->m_pos.y = 50;
	m_fragment_nodes.back()->m_id = ++m_last_node_id;

	m_fragment_nodes.push(LUMIX_NEW(m_allocator, FragmentInputNode)(*this));
	m_fragment_nodes.back()->m_pos.x = 50;
	m_fragment_nodes.back()->m_pos.y = 150;
	m_fragment_nodes.back()->m_id = ++m_last_node_id;

	m_vertex_nodes.push(LUMIX_NEW(m_allocator, VertexOutputNode)(*this));
	m_vertex_nodes.back()->m_pos.x = 50;
	m_vertex_nodes.back()->m_pos.y = 50;
	m_vertex_nodes.back()->m_id = ++m_last_node_id;

	m_vertex_nodes.push(LUMIX_NEW(m_allocator, VertexInputNode)(*this));
	m_vertex_nodes.back()->m_pos.x = 50;
	m_vertex_nodes.back()->m_pos.y = 150;
	m_vertex_nodes.back()->m_id = ++m_last_node_id;
}


void ShaderEditor::generatePasses(OutputMemoryStream& blob)
{
	const char* passes[32];
	int pass = 0;

	auto process = [&](Array<Node*>& nodes){
		for (auto* node : nodes)
		{
			if (node->m_type != (int)NodeType::PASS) continue;

			auto* pass_node = static_cast<PassNode*>(node);
			passes[pass] = pass_node->m_pass;
			++pass;
		}
	};

	process(m_vertex_nodes);
	process(m_fragment_nodes);

	if (pass == 0)
	{
		passes[0] = "MAIN";
		++pass;
	}

	for (int i = 0; i < pass; ++i)
	{
		blob << "pass \"" << passes[i] << "\"\n";
	}
}


void ShaderEditor::onGUIMenu()
{
	if(ImGui::BeginMenuBar()) {
		if(ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New")) newGraph();
			if (ImGui::MenuItem("Open")) load();
			if (ImGui::MenuItem("Save", nullptr, false, m_path.isValid())) save(m_path.c_str());
			if (ImGui::MenuItem("Save as")) {
				if(getSavePath() && m_path.isValid()) save(m_path.c_str());
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", nullptr, false, canUndo())) undo();
			if (ImGui::MenuItem("Redo", nullptr, false, canRedo())) redo();
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Generate & save", nullptr, false, m_path.isValid())) {
			generate(m_path.c_str(), true);
		}

		ImGui::EndMenuBar();
	}
}


void ShaderEditor::onGUI()
{
	if (!m_is_open) return;
	StaticString<MAX_PATH_LENGTH + 25> title("Shader Editor");
	if (m_path.isValid()) title << " - " << m_path.c_str();
	title << "###Shader Editor";
	if (ImGui::Begin(title, &m_is_open, ImGuiWindowFlags_MenuBar))
	{
		m_is_focused = ImGui::IsFocusedHierarchy();

		onGUIMenu();
		onGUILeftColumn();
		ImVec2 size(m_left_col_width, 0);
		ImGui::SameLine();
		ImGui::VSplitter("vsplit", &size);
		m_left_col_width = size.x;
		ImGui::SameLine();
		onGUIRightColumn();
	}
	else
	{
		m_is_focused = false;
	}
	ImGui::End();
}


} // namespace Lumix