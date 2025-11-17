#pragma once

#include "core/arena_allocator.h"
#include "core/defer.h"
#include "core/log.h"
#include "core/string.h"
#include "renderer/editor/world_viewer.h"
#include "renderer/particle_system.h"
#include "renderer/render_module.h"

namespace Lumix {

struct ParticleScriptToken {
	enum Type {
		EOF, ERROR, SEMICOLON, COMMA, COLON, DOT,
		LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
		STAR, SLASH, MINUS, PLUS, EQUAL, PERCENT, GT, LT,
		NUMBER, STRING, IDENTIFIER,

		// keywords
		CONST, GLOBAL, EMITTER, FN, WORLD_SPACE, VAR, OUT, IN, LET, RETURN, IMPORT
	};

	Type type;
	StringView value;
};

struct ParticleScriptEditorWindow : AssetEditorWindow {
	ParticleScriptEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_app(app)
		, m_path(path)
		, m_viewer(app)
	{
		m_editor = createParticleScriptEditor(m_app);
		m_editor->focus();
			
		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			StringView v((const char*)blob.data(), (u32)blob.size());
			m_editor->setText(v);
		}

		World* world = m_viewer.m_world;
		m_preview_entity = world->createEntity({0, 0, 0}, Quat::IDENTITY);
		world->createComponent(types::particle_emitter, m_preview_entity);
		RenderModule* module = (RenderModule*)world->getModule(types::particle_emitter);
		module->setParticleEmitterPath(m_preview_entity, m_path);

		m_viewer.m_viewport.pos = {0, 2, 5};
		m_viewer.m_viewport.rot = {0, 0, 1, 0};
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}

	void fileChangedExternally() override {
		OutputMemoryStream tmp(m_app.getAllocator());
		OutputMemoryStream tmp2(m_app.getAllocator());
		m_editor->serializeText(tmp);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.getContentSync(m_path, tmp2)) return;

		if (tmp.size() == tmp2.size() && memcmp(tmp.data(), tmp2.data(), tmp.size()) == 0) {
			m_dirty = false;
		}
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();

		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			if (ImGuiEx::IconButton(ICON_FA_ANGLE_DOUBLE_RIGHT, "Toggle preview")) m_show_preview = !m_show_preview;
			if (ImGuiEx::IconButton(ICON_FA_BUG, "Debug")) { ASSERT(false); /*TODO*/ };
			ImGui::EndMenuBar();
		}

		float w = ImGui::GetContentRegionAvail().x / 2;
		// TODO hide preview pane -> code_page does not stretch
		if (ImGui::BeginChild("code_pane", ImVec2(m_show_preview ? w : 0, 0), ImGuiChildFlags_ResizeX)) {
			if (m_editor->gui("codeeditor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;
		}
		ImGui::EndChild();
		if (m_show_preview) {
			ImGui::SameLine();
			if (ImGui::BeginChild("preview_pane")) {
				auto* module = (RenderModule*)m_viewer.m_world->getModule(types::particle_emitter);
				ParticleSystem& system = module->getParticleEmitter(m_preview_entity);
				
				if (ImGuiEx::IconButton(ICON_FA_INFO_CIRCLE, "Info")) ImGui::OpenPopup("info");
				if (ImGui::BeginPopup("info")) {
					Span<ParticleSystemResource::Emitter> emitters = system.getResource()->getEmitters();
					for (u32 i = 0, c = emitters.size(); i < c; ++i) {
						const auto& emitter = emitters[i];
						ImGui::Text("Emitter %d", i + 1);
						ImGui::Indent();
						ImGui::LabelText("Emit registers", "%d", emitter.emit_registers_count);
						ImGui::LabelText("Emit instructions", "%d", emitter.emit_instructions_count);
						ImGui::LabelText("Update registers", "%d", emitter.update_registers_count);
						ImGui::LabelText("Update instructions", "%d", emitter.update_instructions_count);
						ImGui::LabelText("Output registers", "%d", emitter.output_registers_count);
						ImGui::LabelText("Output instructions", "%d", emitter.output_instructions_count);
						ImGui::Unindent();
					}
					ImGui::EndPopup();
				}
				ImGui::SameLine();
				if (m_play) {
					if (ImGuiEx::IconButton(ICON_FA_PAUSE, "Pause")) m_play = false;
					float td = m_app.getEngine().getLastTimeDelta();
					module->updateParticleEmitter(m_preview_entity, td);
				}
				else {
					if (ImGuiEx::IconButton(ICON_FA_PLAY, "Play")) m_play = true;
				}
				ImGui::SameLine();
				if (ImGuiEx::IconButton(ICON_FA_STEP_FORWARD, "Next frame")) {
					if (m_play) logError("Particle simulation must be paused.");
					else {
						float td = m_app.getEngine().getLastTimeDelta();
						module->updateParticleEmitter(m_preview_entity, td);
					}
				}
				
				ImGui::SameLine();
				if (ImGuiEx::IconButton(ICON_FA_EYE, "Toggle ground")) {
					m_show_ground = !m_show_ground;
					module->enableModelInstance(m_viewer.m_ground, m_show_ground);
				};

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_UNDO_ALT " Reset")) system.reset();
				u32 num_particles = 0;
				for (ParticleSystem::Emitter& emitter : system.getEmitters()) {
					if (emitter.resource_emitter.max_ribbons > 0) {
						for (const auto& ribbon : emitter.ribbons) num_particles += ribbon.length;
					}
					else {
						num_particles += emitter.particles_count;
					}
				}
				
				if (!system.m_globals.empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Globals")) ImGui::OpenPopup("Globals");
					if (ImGui::BeginPopup("Globals")) {
						u32 offset = 0;
						for (const auto& p : system.getResource()->getGlobals()) {
							ImGui::PushID(&p);
							ImGuiEx::Label(p.name.c_str());
							ImGui::SetNextItemWidth(150);
							float* f = system.m_globals.begin() + offset;
							switch (p.num_floats) {
								case 1: ImGui::InputFloat("##v", f); break;
								case 2: ImGui::InputFloat2("##v", f); break;
								case 3: ImGui::InputFloat3("##v", f); break;
								case 4: ImGui::InputFloat4("##v", f); break;
							}
							offset += p.num_floats;
							ImGui::PopID();
						}
						ImGui::EndPopup();
					}
				}
				ImGui::SameLine();
				ImGui::Text("Particles: %d", num_particles);
				m_viewer.gui();
			}
			ImGui::EndChild();
		}
	}
	
	const Path& getPath() override { return m_path; }
	const char* getName() const override { return "particle script editor"; }

	StudioApp& m_app;
	UniquePtr<CodeEditor> m_editor;
	WorldViewer m_viewer;
	Path m_path;
	EntityRef m_preview_entity;
	bool m_play = true;
	bool m_show_preview = true;
	bool m_show_ground = true;
};

struct ParticleScriptTokenizer {
	using Token = ParticleScriptToken;
	
	enum class Operators : char {
		ADD = '+',
		SUB = '-',
		DIV = '/',
		MUL = '*',
		MOD = '%',
		LT = '<',
		GT = '>',
	};
	
	StringView m_document;
	const char* m_start_token;
	const char* m_current;
	Token m_current_token;

	void skipWhitespaces() {
		while (m_current != m_document.end && isWhitespace(*m_current)) ++m_current;
		if (m_current < m_document.end - 1
			&& m_current[0] == '/'
			&& m_current[1] == '/') 
		{
			m_current += 2;
			while (m_current != m_document.end && *m_current != '\n') {
				++m_current;
			}
			skipWhitespaces();
		}
	}

	Token makeToken(Token::Type type) {
		Token res;
		res.type = type;
		res.value.begin = m_start_token;
		res.value.end = m_current;
		if (type == Token::STRING) {
			++res.value.begin;
			--res.value.end;
		}
		return res;
	}

	char advance() {
		ASSERT(m_current < m_document.end);
		char c = m_current[0];
		++m_current;
		return c;
	}

	static bool isDigit(char c) { return c >= '0' && c <= '9'; }

	char peekChar() {
		if (m_current == m_document.end) return 0;
		return m_current[0];
	}

	char peekNextChar() {
		if (m_current + 1 >= m_document.end) return 0;
		return m_current[1];
	}

	Token numberToken() {
		while (isDigit(peekChar())) advance();
		if (peekChar() == '.') {
			advance();
			if (!isDigit(peekChar())) return makeToken(Token::ERROR);
			advance();
			while (isDigit(peekChar())) advance();
		}
		return makeToken(Token::NUMBER);
	}

	Token stringToken() {
		while (m_current != m_document.end && m_current[0] != '"') {
			++m_current;
		}
		if (m_current == m_document.end) return makeToken(Token::ERROR);
		advance();
		return makeToken(Token::STRING);
	}

	Token checkKeyword(const char* remaining, i32 start, i32 len, Token::Type type) {
		if (m_current - m_start_token != start + len) return makeToken(Token::IDENTIFIER);
		if (memcmp(m_start_token + start, remaining, len) != 0) return makeToken(Token::IDENTIFIER);
		return makeToken(type);
	}

	static bool isIdentifierChar(char c) {
		return isLetter(c) || isDigit(c) || c == '_';
	}

	Token identifierOrKeywordToken() {
		while (isIdentifierChar(peekChar())) advance();

		switch (m_start_token[0]) {
			case 'c': return checkKeyword("onst", 1, 4, Token::CONST);
			case 'e': return checkKeyword("mitter", 1, 6, Token::EMITTER);
			case 'f': return checkKeyword("n", 1, 1, Token::FN);
			case 'g': return checkKeyword("lobal", 1, 5, Token::GLOBAL);
			case 'i': {
				if (m_current - m_start_token < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'n': return checkKeyword("", 2, 0, Token::IN);
					case 'm': return checkKeyword("port", 2, 4, Token::IMPORT);
				}
				return makeToken(Token::IDENTIFIER);
			}
			case 'l': return checkKeyword("et", 1, 2, Token::LET);
			case 'o': return checkKeyword("ut", 1, 2, Token::OUT);
			case 'r': return checkKeyword("eturn", 1, 5, Token::RETURN);
			case 'v': return checkKeyword("ar", 1, 2, Token::VAR);
			case 'w': return checkKeyword("orld_space", 1, 10, Token::WORLD_SPACE);
		}
		return makeToken(Token::IDENTIFIER);
	}

	Token nextToken() {
		skipWhitespaces();
		Token res;
		m_start_token = m_current;
		if (m_current == m_document.end) return makeToken(Token::EOF);

		char c = advance();
		if (isDigit(c)) return numberToken();
		if (isLetter(c) || c == '_') return identifierOrKeywordToken();
		
		switch (c) {
			case '"': return stringToken();
			case '.': return makeToken(Token::DOT);
			case ';': return makeToken(Token::SEMICOLON);
			case ':': return makeToken(Token::COLON);
			case '{': return makeToken(Token::LEFT_BRACE);
			case '}': return makeToken(Token::RIGHT_BRACE);
			case '(': return makeToken(Token::LEFT_PAREN);
			case ')': return makeToken(Token::RIGHT_PAREN);
			case '-': return makeToken(Token::MINUS);
			case '+': return makeToken(Token::PLUS);
			case '*': return makeToken(Token::STAR);
			case '/': return makeToken(Token::SLASH);
			case '%': return makeToken(Token::PERCENT);
			case '=': return makeToken(Token::EQUAL);
			case ',': return makeToken(Token::COMMA);
			case '>': return makeToken(Token::GT);
			case '<': return makeToken(Token::LT);
		}
		
		return makeToken(Token::ERROR);
	}
};

struct ParticleScriptCompiler {
	using InstructionType = ParticleSystemResource::InstructionType;
	using DataStream = ParticleSystemResource::DataStream;
	using Token = ParticleScriptToken;
	using Operators = ParticleScriptTokenizer::Operators;

    enum class ValueType {
        FLOAT,
        FLOAT3,
        FLOAT4,

		VOID
    };

	struct BuiltinFunction {
        InstructionType instruction;
        bool returns_value;
		u32 num_args;
    };

    struct Constant {
        StringView name;
        ValueType type;
        float value[4];
    };

	enum class VariableFamily {
		OUTPUT,
		CHANNEL,
		INPUT,
		LOCAL,
		GLOBAL
	};

	struct CompileResult {
		DataStream streams[4];
		u32 num_streams = 0;
		bool success = true;
	};

	struct Local {
		StringView name;
		ValueType type;
		i32 registers[4];
	};

	struct BlockNode;

	struct Function {
		Function(IAllocator& allocator) : args(allocator) {}
		StringView name;
		Array<StringView> args;
		BlockNode* block = nullptr;
	};

	struct Variable {
		StringView name;
		ValueType type;
		i32 offset = 0;

        i32 getOffsetSub(u32 sub) const {
            switch (type) {
                case ValueType::VOID: ASSERT(false); return offset;
                case ValueType::FLOAT: return offset;
                case ValueType::FLOAT3: return offset + minimum(sub, 2);
                case ValueType::FLOAT4: return offset + minimum(sub, 3);
            }
            ASSERT(false);
            return offset;
        }
	};

	struct Emitter {
		Emitter(IAllocator& allocator)
			: m_outputs(allocator)
			, m_update(allocator)
			, m_emit(allocator)
			, m_output(allocator)
			, m_inputs(allocator)
			, m_vars(allocator)
		{}

		StringView m_name;
		Path m_material;
		Path m_mesh;
		OutputMemoryStream m_update;
		OutputMemoryStream m_emit;
		OutputMemoryStream m_output;
		u32 m_num_update_registers = 0;
		u32 m_num_emit_registers = 0;
		u32 m_num_output_registers = 0;
		u32 m_num_update_instructions = 0;
		u32 m_num_emit_instructions = 0;
		u32 m_num_output_instructions = 0;
		Array<Variable> m_vars;
		Array<Variable> m_outputs;
		Array<Variable> m_inputs;
		u32 m_init_emit_count = 0;
		bool m_emit_on_move = false;
		float m_emit_per_second = 0;
		u32 m_max_ribbons = 0;
		u32 m_max_ribbon_length = 0;
		u32 m_init_ribbons_count = 0;
	};

	struct BlockNode;

	struct CompileContext {
		CompileContext(ParticleScriptCompiler& compiler) : compiler(compiler) {} 
		ParticleScriptCompiler& compiler; 
		const Function* function = nullptr;
		Emitter* emitter = nullptr;
		Emitter* emitted = nullptr;
		Span<CompileResult> args = {};
		BlockNode* block = nullptr;

		u32 value_counter = 0;
	};

	struct Node {
		enum Type {
			UNARY_OPERATOR,
			BINARY_OPERATOR,
			LITERAL,
			RETURN,
			FUNCTION_ARG,
			ASSIGN,
			VARIABLE,
			LOCAL_VARIABLE,
			SWIZZLE,
			SYSCALL,
			SYSTEM_VALUE,
			COMPOUND,
			EMITTER_REF,
			BLOCK,
			FUNCTION_CALL
		};

		Node(Type type, Token token) : type(type), token(token) {}
		virtual ~Node() {}
		
		Type type;
		Token token;
	};

	struct SystemValueNode : Node {
		SystemValueNode(Token token) : Node(Node::SYSTEM_VALUE, token) {}
		ParticleSystemValues value;
	};

	struct CompoundNode : Node {
		CompoundNode(Token token, IAllocator& allocator) : Node(Node::COMPOUND, token), elements(allocator) {}
		Array<Node*> elements;
	};

	struct BlockNode : Node {
		BlockNode(Token token, IAllocator& allocator) : Node(Node::BLOCK, token), statements(allocator), locals(allocator) {}
		Array<Node*> statements;
		Array<Local> locals;
		BlockNode* parent = nullptr;
	};

	struct SysCallNode : Node {
		SysCallNode(Token token, IAllocator& allocator) : Node(Node::SYSCALL, token), args(allocator) {}
		BuiltinFunction function;
		Array<Node*> args;
		Node* after_block = nullptr;
	};

	struct FunctionCallNode : Node {
		FunctionCallNode(Token token, IAllocator& allocator) : Node(Node::FUNCTION_CALL, token), args(allocator) {}
		i32 function_index;
		Array<Node*> args;
	};

	struct ReturnNode : Node {
		ReturnNode(Token token) : Node(Node::RETURN, token) {}
		Node* value;
	};

	struct EmitterRefNode : Node {
		EmitterRefNode(Token token) : Node(Node::EMITTER_REF, token) {}
		i32 index;
	};

	struct VariableNode : Node {
		VariableNode(Token token) : Node(Node::VARIABLE, token) {}
		i32 index;
		VariableFamily family;
	};

	struct SwizzleNode : Node {
		SwizzleNode(Token token) : Node(Node::SWIZZLE, token) {}
		Node* left;
	};

	struct FunctionArgNode : Node {
		FunctionArgNode(Token token) : Node(Node::FUNCTION_ARG, token) {}
		i32 index;
	};

	struct LiteralNode : Node {
		LiteralNode(Token token) : Node(Node::LITERAL, token) {}
		float value;
	};

	struct UnaryOperatorNode : Node {
		UnaryOperatorNode(Token token) : Node(Node::UNARY_OPERATOR, token) {}
		Node* right;
		Operators op;
	};

	struct AssignNode : Node {
		AssignNode(Token token) : Node(Node::ASSIGN, token) {}
		Node* left;
		Node* right;
	};

	struct BinaryOperatorNode : Node {
		BinaryOperatorNode(Token token) : Node(Node::BINARY_OPERATOR, token) {}
		Node* left;
		Node* right;
		Operators op;
	};
	
	ParticleScriptCompiler(StudioApp& app, IAllocator& allocator)
		: m_app(app)
		, m_allocator(allocator)
		, m_emitters(allocator)
		, m_constants(allocator)
		, m_globals(allocator)
		, m_functions(allocator)
		, m_arena_allocator(1024 * 1024 * 256, m_allocator, "particle_script_compiler")
		, m_imports(allocator)
	{
	}

	~ParticleScriptCompiler() {
		m_arena_allocator.reset();
	}

	ValueType parseType() {
		StringView type;
		if (!consume(Token::IDENTIFIER, type)) return ValueType::FLOAT;
		if (equalStrings(type, "float")) return ValueType::FLOAT;
		if (equalStrings(type, "float3")) return ValueType::FLOAT3;
		if (equalStrings(type, "float4")) return ValueType::FLOAT4;
		error(type, "Unknown type");
		return ValueType::FLOAT;
	}

	void variableDeclaration(Array<Variable>& vars) {
		u32 offset = 0;
		if (!vars.empty()) {
			const Variable& var = vars.last();
			offset = var.offset;
			switch (var.type) {
				case ValueType::VOID: ASSERT(false); break;
				case ValueType::FLOAT: ++offset; break;
				case ValueType::FLOAT3: offset += 3; break;
				case ValueType::FLOAT4: offset += 4; break;
			}
		}
		Variable& var = vars.emplace();
		if (!consume(Token::IDENTIFIER, var.name)) return;
		consume(Token::COLON);
		var.type = parseType();
		var.offset = offset;
	}

	i32 getFunctionIndex(StringView ident) const {
		for (Function& fn : m_functions) {
			if (equalStrings(fn.name, ident)) return i32(&fn - m_functions.begin());
		}
		return -1;
	}

	static i32 getArgumentIndex(const Function& fn, StringView ident) {
		for (i32 i = 0, c = fn.args.size(); i < c; ++i) {
			if (equalStrings(fn.args[i], ident)) return i;
		}
		return -1;
	}

	i32 getParamIndex(StringView name) const {
		for (const Variable& var : m_globals) {
			if (equalStrings(var.name, name)) return i32(&var - m_globals.begin());
		}
		return -1;
	}

	static i32 find(Span<const Variable> vars, StringView name) {
		for (const Variable& var : vars) {
			if (equalStrings(var.name, name)) return i32(&var - vars.begin());
		}
		return -1;
	}

	Constant* getConstant(StringView name) const {
        for (Constant& c : m_constants) {
            if (equalStrings(c.name, name)) return &c;
        }
        return nullptr;
    }

	i32 getLine(StringView location) {
        ASSERT(location.begin <= m_tokenizer.m_document.end);
        const char* c = m_tokenizer.m_document.begin;
        i32 line = 1;
        while(c < location.begin) {
            if (*c == '\n') ++line;
            ++c;
        }
        return line;
    }

	template <typename... Args>
	void error(StringView location, Args&&... args) {
		if(!m_is_error) logError(m_path, "(", getLine(location), "): ", args...);
		m_is_error = true;
	}

	template <typename... Args>
	void errorAtCurrent(Args&&... args) {
		if(!m_is_error) logError(m_path, "(", getLine(m_tokenizer.m_current_token.value), "): ", args...);
		m_is_error = true;
	}

	Token peekToken() { return m_tokenizer.m_current_token; }

	Token consumeToken() {
		Token t = m_tokenizer.m_current_token;
		m_tokenizer.m_current_token = m_tokenizer.nextToken();
		return t;
	}

const char* toString(Token::Type type) {
		switch (type) {
			case Token::Type::COLON: return ";";
			case Token::Type::COMMA: return ",";
			case Token::Type::CONST: return "const";
			case Token::Type::DOT: return ".";
			case Token::Type::EMITTER: return "emitter";
			case Token::Type::EOF: return "end of file";
			case Token::Type::ERROR: return "error";
			case Token::Type::SEMICOLON: return ";";
			case Token::Type::LEFT_PAREN: return "(";
			case Token::Type::RIGHT_PAREN: return ")";
			case Token::Type::LEFT_BRACE: return "{";
			case Token::Type::RIGHT_BRACE: return "}";
			case Token::Type::STAR: return "*";
			case Token::Type::SLASH: return "/";
			case Token::Type::MINUS: return "-";
			case Token::Type::PLUS: return "+";
			case Token::Type::EQUAL: return "=";
			case Token::Type::PERCENT: return "%";
			case Token::Type::GT: return ">";
			case Token::Type::LT: return "<";
			case Token::Type::NUMBER: return "number";
			case Token::Type::STRING: return "string";
			case Token::Type::IDENTIFIER: return "identifier";
			case Token::Type::GLOBAL: return "global";
			case Token::Type::FN: return "fn";
			case Token::Type::WORLD_SPACE: return "world_space";
			case Token::Type::VAR: return "var";
			case Token::Type::OUT: return "out";
			case Token::Type::IN: return "in";
			case Token::Type::LET: return "let";
			case Token::Type::RETURN: return "return";
			case Token::Type::IMPORT: return "import";
		}
		ASSERT(false);
		return "N//A";
	}

	bool consume(Token::Type type) {
		Token t = consumeToken();
		if (t.type != type) {
			error(t.value, "Missing ", toString(type), " before ", t.value);
			return false;
		}
		return true;
	}

	[[nodiscard]] bool consume(Token::Type type, StringView& value) {
		Token t = consumeToken();
		if (t.type != type) {
			error(t.value, "Missing ", toString(type), " before ", t.value);
			return false;
		}
		value = t.value;
		return true;
	}	

	float asFloat(Token token) {
		ASSERT(token.type == Token::NUMBER);
		float v;
		fromCString(token.value, v);
		return v;
	}

	static u32 getPriority(const Token& token) {
		switch (token.type) {
			case Token::GT: case Token::LT: return 1;
			case Token::PLUS: case Token::MINUS: return 2;
			case Token::PERCENT: case Token::STAR: case Token::SLASH: return 3;
			default: ASSERT(false);
		}
		return 0;
	}

	// original nodes are left in undefined state
	Node* collapseConstants(Node* node) {
		switch (node->type) {
			case Node::RETURN: {
				auto* r = (ReturnNode*)node;
				r->value = collapseConstants(r->value);
				return r;
			}
			case Node::ASSIGN: {
				auto* n = (AssignNode*)node;
				n->right = collapseConstants(n->right);
				return node;
			}
			case Node::SWIZZLE: {
				auto* n = (SwizzleNode*)node;
				n->left = collapseConstants(n->left);
				// TODO compound literal
				return node;
			}
			case Node::COMPOUND: {
				auto* n = (CompoundNode*)node;
				for (Node*& element : n->elements) {
					element = collapseConstants(element);
				}
				return node;
			}
			case Node::BLOCK: {
				auto* n = (BlockNode*)node;
				for (Node*& statement : n->statements) {
					statement = collapseConstants(statement);
				}
				return node;
			}
			case Node::SYSCALL: {
				auto* n = (SysCallNode*)node;
				bool all_args_const = true;
				for (Node*& arg : n->args) {
					arg = collapseConstants(arg);
					if (arg->type != Node::LITERAL) all_args_const = false;
				}
				if (all_args_const) {
					auto* arg0 = ((LiteralNode*)n->args[0]);
					auto* arg1 = n->args.size() > 1 ? ((LiteralNode*)n->args[1]) : nullptr;
					switch (n->function.instruction) {
						case InstructionType::COS: {
							auto* res = LUMIX_NEW(m_arena_allocator, LiteralNode)(n->token);
							res->value = cosf(arg0->value);
							return res;
						}
						case InstructionType::SIN: {
							auto* res = LUMIX_NEW(m_arena_allocator, LiteralNode)(n->token);
							res->value = sinf(arg0->value);
							return res;
						}
						case InstructionType::SQRT: {
							auto* res = LUMIX_NEW(m_arena_allocator, LiteralNode)(n->token);
							res->value = sqrtf(arg0->value);
							return res;
						}
						case InstructionType::MIN: {
							auto* res = LUMIX_NEW(m_arena_allocator, LiteralNode)(n->token);
							res->value = minimum(arg0->value, arg1->value);
							return res;
						}
						case InstructionType::MAX: {
							auto* res = LUMIX_NEW(m_arena_allocator, LiteralNode)(n->token);
							res->value = maximum(arg0->value, arg1->value);
							return res;
						}
						default: return node;
					}
				}
				return node;
			}
			case Node::FUNCTION_CALL: {
				auto* n = (FunctionCallNode*)node;
				for (Node*& arg : n->args) arg = collapseConstants(arg);
				return node;
			}
			case Node::FUNCTION_ARG: return node;
			case Node::EMITTER_REF: return node;
			case Node::SYSTEM_VALUE: return node;
			case Node::LOCAL_VARIABLE: return node;
			case Node::VARIABLE: return node;
			case Node::LITERAL: return node;
			case Node::BINARY_OPERATOR: {
				auto* n = (BinaryOperatorNode*)node;
				n->left = collapseConstants(n->left); 
				n->right = collapseConstants(n->right); 
				if (n->left->type != Node::LITERAL || n->right->type != Node::LITERAL) return node;

				auto* r = (LiteralNode*)n->right;
				auto* l = (LiteralNode*)n->left;
				switch (n->op) {
					case Operators::ADD: l->value = l->value + r->value; return l;
					case Operators::SUB: l->value = l->value - r->value; return l;
					case Operators::MUL: l->value = l->value * r->value; return l;
					case Operators::DIV: l->value = l->value / r->value; return l;
					default: return node;
				}
			}
			case Node::UNARY_OPERATOR: {
				auto* n = (UnaryOperatorNode*)node;
				if (n->op != Operators::SUB) return node;
				
				Node* r = collapseConstants(n->right);
				if (r->type == Node::LITERAL) {
					auto* literal = (LiteralNode*)r;
					literal->value = -literal->value;
					return literal;
				}
				return node;
			}
		}
		return node;
	}

	Node* atom(CompileContext& ctx) {
		Node* left = atomInternal(ctx);
		if (peekToken().type != Token::DOT) return left;
		// swizzle
		consumeToken();
		Token swizzle = consumeToken();

		if (swizzle.type != Token::IDENTIFIER) {
			error(swizzle.value, "Invalid swizzle ", swizzle.value);
			return nullptr;
		}
		auto* node = LUMIX_NEW(m_arena_allocator, SwizzleNode)(swizzle);
		node->left = left;
		return node;
	}
	
	ParticleSystemValues getSystemValue(StringView name) {
		if (equalStrings(name, "time_delta")) return ParticleSystemValues::TIME_DELTA;
		if (equalStrings(name, "total_time")) return ParticleSystemValues::TOTAL_TIME;
		if (equalStrings(name, "emit_index")) return ParticleSystemValues::EMIT_INDEX;
		if (equalStrings(name, "ribbon_index")) return ParticleSystemValues::RIBBON_INDEX;
		return ParticleSystemValues::NONE;
	}

	BuiltinFunction checkBuiltinFunction(StringView name, const char* remaining, i32 start, i32 len, BuiltinFunction if_matching) {
		if (name.size() != start + len) return {InstructionType::END};
		name.removePrefix(start);
		if (memcmp(name.begin, remaining, len) == 0) return if_matching;
		return {InstructionType::END};
	}

	BuiltinFunction getBuiltinFunction(StringView name) {
		switch (name[0]) {
			case 'c': 
				if (name.size() < 2) return {InstructionType::END};
				switch (name[1]) {
					case 'o': return checkBuiltinFunction(name, "s", 2, 1, { InstructionType::COS, true, 1 });
					case 'u': return checkBuiltinFunction(name, "rve", 2, 3, { InstructionType::GRADIENT, true, 0xff });
				}
				return { InstructionType::END };
				
			case 'e': return checkBuiltinFunction(name, "mit", 1, 3, { InstructionType::EMIT, false, 2 });
			case 'k': return checkBuiltinFunction(name, "ill", 1, 3, { InstructionType::KILL, false, 1 });
			case 'm':
				if (name.size() < 2) return {InstructionType::END};
				switch (name[1]) {
					case 'a': return checkBuiltinFunction(name, "x", 2, 1, { InstructionType::MAX, true, 2 });
					case 'e': return checkBuiltinFunction(name, "sh", 2, 2, { InstructionType::MESH, true, 0 });
					case 'i': return checkBuiltinFunction(name, "n", 2, 1, { InstructionType::MIN, true, 2 });
				}
				return { InstructionType::END };

			case 'n': return checkBuiltinFunction(name, "oise", 1, 4, { InstructionType::NOISE, true, 1 });
			case 'r': return checkBuiltinFunction(name, "andom", 1, 5, { InstructionType::RAND, true, 2 });
			case 's': 
				if (name.size() < 2) return {InstructionType::END};
				switch (name[1]) {
					case 'i': return checkBuiltinFunction(name, "n", 2, 1, { InstructionType::SIN, true, 1 });
					case 'q': return checkBuiltinFunction(name, "rt", 2, 2, { InstructionType::SQRT, true, 1 });
				}
				return { InstructionType::END };
				
		}
		return { InstructionType::END };
	}

	BlockNode* block(CompileContext& ctx) {
		auto* node = LUMIX_NEW(m_arena_allocator, BlockNode)(peekToken(), m_arena_allocator);
		node->parent = ctx.block;
		ctx.block = node;
		defer { ctx.block = node->parent; };
		if (!consume(Token::LEFT_BRACE)) return nullptr;
		node->statements.reserve(8);
		for (;;) {
			Token token = peekToken();
			switch (token.type) {
				case Token::ERROR: return nullptr;
				case Token::EOF:
					errorAtCurrent("Unexpected end of file.");
					return nullptr;
				case Token::LEFT_BRACE: {
					Node* s = block(ctx);
					if (!s) return nullptr;
					node->statements.push(s);
				}
				case Token::LET:
					declareLocal(ctx);
					break;
				case Token::RIGHT_BRACE:
					consumeToken();
					return node;
				default: {
					Node* s = statement(ctx);
					if (!s) return nullptr;
					if (!consume(Token::SEMICOLON)) return nullptr;
					node->statements.push(s);
					break;
				}
			}
		}
	}

	Node* atomInternal(CompileContext& ctx) {
		Token token = consumeToken();
		switch (token.type) {
			case Token::EOF:
				error(token.value, "Unexpected end of file.");
				return nullptr;
			case Token::ERROR: return nullptr;
			case Token::LEFT_BRACE: {
				auto* node = LUMIX_NEW(m_arena_allocator, CompoundNode)(token, m_arena_allocator);
				node->elements.reserve(4);
				for (;;) {
					Token t = peekToken();
					switch (t.type) {
						case Token::ERROR: return nullptr;
						case Token::EOF:
							errorAtCurrent("Unexpected end of file.");
							return nullptr;
						case Token::RIGHT_BRACE:
							consumeToken();
							return node;
						default: {
							if (!node->elements.empty() && !consume(Token::COMMA)) return nullptr;
							Node* element = expression(ctx, 0);
							if (!element) return nullptr;
							node->elements.push(element);
							break;
						}
					}
				}
			}
			case Token::LEFT_PAREN: {
				Node* res = expression(ctx, 0);
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				return res;
			}
			case Token::IDENTIFIER: {
				i32 param_index = getParamIndex(token.value);
				if (param_index >= 0) {
					auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
					node->family = VariableFamily::GLOBAL;
					node->index = param_index;
					return node;
				}

				for (Emitter& e : m_emitters) {
					if (equalStrings(e.m_name, token.value)) {
						auto* node = LUMIX_NEW(m_arena_allocator, EmitterRefNode)(token);
						node->index = i32(&e - m_emitters.begin());
						return node;
					}
				}

				ParticleSystemValues system_value = getSystemValue(token.value);
				if (system_value != ParticleSystemValues::NONE) {
					auto* node = LUMIX_NEW(m_arena_allocator, SystemValueNode)(token);
					node->value = system_value;
					return node;
				}

				BuiltinFunction bfn = getBuiltinFunction(token.value);
				if (bfn.instruction != InstructionType::END) {
					auto* node = LUMIX_NEW(m_arena_allocator, SysCallNode)(token, m_arena_allocator);
					if (!consume(Token::LEFT_PAREN)) return nullptr;
					node->args.reserve(bfn.num_args);
					for (u32 i = 0; i < bfn.num_args; ++i) {
						if (i > 0 && !consume(Token::COMMA)) return nullptr;
						Node* arg = expression(ctx, 0);
						if (!arg) return nullptr;
						node->args.push(arg);
					}
					if (!consume(Token::RIGHT_PAREN)) return nullptr;
		
					if (peekToken().type == Token::LEFT_BRACE) {
						CompileContext inner_ctx = ctx;
						if (bfn.instruction == InstructionType::EMIT) {
							if (node->args[0]->type != Node::EMITTER_REF) {
								error(node->args[0]->token.value, "First parameter must an emitter.");
								return nullptr;
							}
							u32 emitter_index = ((EmitterRefNode*)node->args[0])->index;
							inner_ctx.emitted = &m_emitters[emitter_index];
						}
						node->after_block = block(inner_ctx);
						if (!node->after_block) return nullptr;
					}

					node->function = bfn;
					return node;
				}

				if (ctx.emitted) {
					i32 input_index = find(ctx.emitted->m_inputs, token.value);
					if (input_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
						node->family = VariableFamily::INPUT;
						node->index = input_index;
						return node;
					}
				}

				if (ctx.emitter) {
					i32 output_index = find(ctx.emitter->m_outputs, token.value);
					if (output_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
						node->family = VariableFamily::OUTPUT;
						node->index = output_index;
						return node;
					}

					i32 input_index = find(ctx.emitter->m_inputs, token.value);
					if (input_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
						node->family = VariableFamily::INPUT;
						node->index = input_index;
						return node;
					}

					i32 var_index = find(ctx.emitter->m_vars, token.value);
					if (var_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
						node->family = VariableFamily::CHANNEL;
						node->index = var_index;
						return node;
					}

					i32 fn_index = getFunctionIndex(token.value);
					if (fn_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, FunctionCallNode)(token, m_arena_allocator);
						if (!consume(Token::LEFT_PAREN)) return nullptr;
						node->function_index = fn_index;
						const Function& fn = m_functions[fn_index];
						for (u32 i = 0, c = fn.args.size(); i < c; ++i) {
							if (i > 0 && !consume(Token::COMMA)) return nullptr;
							Node* arg = expression(ctx, 0);
							if (!arg) return nullptr;
							node->args.push(arg);
						}
						if (!consume(Token::RIGHT_PAREN)) return nullptr;
						return node;
					}
				}

				if (ctx.function) {
					i32 arg_index = getArgumentIndex(*ctx.function, token.value);
					if (arg_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, FunctionArgNode)(token);
						node->index = arg_index;
						return node;
					}
				}

				Constant* c = getConstant(token.value);
				if (c) {
					auto* node = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
					// TODO floatN
					node->value = c->value[0];
					return node;
				}
				
				BlockNode* block = ctx.block;
				if (block) { // TODO parent scope
					for (const Local& local : ctx.block->locals) {
						if (equalStrings(local.name, token.value)) {
							auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
							node->family = VariableFamily::LOCAL;
							node->index = i32(&local - ctx.block->locals.begin());
							return node;
						}
					}
					block = block->parent;
				}

				error(token.value, "Unexpected token ", token.value);
				return nullptr;
			}

			case Token::MINUS: {
				UnaryOperatorNode* node = LUMIX_NEW(m_arena_allocator, UnaryOperatorNode)(token);
				node->op = (Operators)token.value[0];
				node->right = atom(ctx);
				if (!node->right) return nullptr;
				return node;
			}
			case Token::NUMBER: {
				LiteralNode* node = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
				node->value = asFloat(token);
				return node;
			}
			default:
				error(token.value, "Unexpected token ", token.value);
				return nullptr;
		}
		return nullptr;
	} 

	// let a = ...;
	// let a : type;
	// let a : type = ...;
	void declareLocal(CompileContext& ctx) {
		if (!consume(Token::LET)) return;
		BlockNode* block = ctx.block;
		Local& local = block->locals.emplace();
		if (!consume(Token::IDENTIFIER, local.name)) return;

		if (peekToken().type == Token::COLON) {
			// let a : type ...
			if (!consume(Token::COLON)) return;
			local.type = parseType();
		}
		else if (peekToken().type == Token::EQUAL) {
			// let a = ...; 
			local.type = ValueType::FLOAT;
		}
		else {
			error(peekToken().value, "Unexpected token ", peekToken().value);
			return;
		}

		if (peekToken().type == Token::SEMICOLON) {
			// let a : type;
			consumeToken();
			return;
		}
		Token equal_token = peekToken();
		if (!consume(Token::EQUAL)) return;

		// let a : type = ...
		// let a = ...
		u32 var_len;
		switch (local.type) {
			case ValueType::FLOAT4: var_len = 4; break;
			case ValueType::FLOAT3: var_len = 3; break;
			case ValueType::FLOAT: var_len = 1; break;
			case ValueType::VOID: ASSERT(false); break;
		}

		Node* value = expression(ctx, 0);
		if (!value) return;

		auto* assign = LUMIX_NEW(m_arena_allocator, AssignNode)(equal_token);
		assign->right = value;
		auto* var_node = LUMIX_NEW(m_arena_allocator, VariableNode)(equal_token);
		var_node->family = VariableFamily::LOCAL;
		var_node->index = ctx.block->locals.size() - 1;
		assign->left = var_node;
		ctx.block->statements.push(assign);

		consume(Token::SEMICOLON);
	}

	Node* statement(CompileContext& ctx) {
		Token token = peekToken();
		switch (token.type) {
			case Token::IDENTIFIER: {
				Node* lhs = atom(ctx);
				if (!lhs) return nullptr;

				Token op = peekToken();
				switch (op.type) {
					case Token::SEMICOLON: return lhs;
					case Token::EQUAL: {
						consumeToken();
						Node* value = expression(ctx, 0);
						if (!value) return nullptr;
						auto* node = LUMIX_NEW(m_arena_allocator, AssignNode)(op);
						node->left = lhs;
						node->right = value;
						return node;
					}
					default:
						error(op.value, "Unexpected token ", op.value);
						return nullptr;
				}
			}
			case Token::RETURN: {
				consumeToken();
				auto* node = LUMIX_NEW(m_arena_allocator, ReturnNode)(token);
				node->value = expression(ctx, 0);
				if (!node->value) return nullptr;
				return node;
			}
			default:
				errorAtCurrent("Unexpected token ", token.value);
				return nullptr;
		}
	}

	// does not consume terminating token
	Node* expression(CompileContext& ctx, u32 min_priority) {
		Node* lhs = atom(ctx);
		if (!lhs) return nullptr;
		
		for (;;) {
			Token op = peekToken();
			switch (op.type) {
				case Token::EOF: return lhs;
				case Token::ERROR: return nullptr;

				case Token::PERCENT:
				case Token::LT: case Token::GT:
				case Token::SLASH: case Token::STAR:
				case Token::MINUS: case Token::PLUS: {
					u32 prio = getPriority(op);
					if (prio <= min_priority) return lhs;
					consumeToken();
					Node* rhs = expression(ctx, prio);
					BinaryOperatorNode* opnode = LUMIX_NEW(m_arena_allocator, BinaryOperatorNode)(op);
					opnode->op = (Operators)op.value[0];
					opnode->left = lhs;
					opnode->right = rhs;
					lhs = opnode;
					break;
				}

				default: return lhs;
			}
		}
	}

	Array<OutputMemoryStream> m_imports;

	void compileImport() {
		StringView path;
		if (!consume(Token::STRING, path)) return;
		
		OutputMemoryStream& import_content = m_imports.emplace(m_allocator);
		if (!m_app.getEngine().getFileSystem().getContentSync(Path(path), import_content)) {
			error(path, "Failed to load import ", path);
			return;
		}
		
		ParticleScriptTokenizer tokenizer = m_tokenizer;
		m_tokenizer.m_current = (const char*)import_content.data();
		m_tokenizer.m_document = StringView((const char*)import_content.data(), (u32)import_content.size());
		m_tokenizer.m_current_token = m_tokenizer.nextToken();
		
		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::EOF: goto end_import;
				case Token::ERROR: return;
				case Token::IMPORT: compileImport(); break;
				case Token::CONST: compileConst(); break;
				case Token::FN: compileFunction(); break;
				case Token::GLOBAL: variableDeclaration(m_globals); break;
				case Token::EMITTER: compileEmitter(); break;
				case Token::WORLD_SPACE: m_is_world_space = true; break;
				default: error(token.value, "Unexpected token ", token.value); return;
			}
		}
		end_import:
		
		m_tokenizer = tokenizer;
	}

	void compileConst() {
        Constant& c = m_constants.emplace();
		if (!consume(Token::IDENTIFIER, c.name)) return;
        if (!consume(Token::EQUAL)) return;
        
		CompileContext ctx(*this);
		Node* n = expression(ctx, 0);
		if (!n) return;
		n = collapseConstants(n);

		if (n->type != Node::LITERAL) {
			// TODO floatN constants
			errorAtCurrent("Expected a constant.");
			return;
		}

        float f = ((LiteralNode*)n)->value;
        c.value[0] = f;
        c.value[1] = f;
        c.value[2] = f;
        c.value[3] = f;
        c.type = ValueType::FLOAT;

		consume(Token::SEMICOLON);
	}

	void parseArgs(Function& fn) {
		consume(Token::LEFT_PAREN);
		bool comma = false;
		for (;;) {
			Token t = consumeToken();
			switch (t.type) {
				case Token::ERROR: return;
				case Token::EOF: error(t.value, "Unexpected end of file.");
				case Token::RIGHT_PAREN:
					if (!comma) return;
					error(t.value, "Unexpected ).'");
					return;
				case Token::COMMA: 
					if (fn.args.empty() || comma) {
						error(t.value, "Unexpected ,.");
						return;
					}
					comma = true;
					break;
				case Token::IDENTIFIER: {
					StringView& arg = fn.args.emplace();
					arg = t.value;
					comma = false;
					break;
				}
				default:
					error(t.value, "Unexpected token ", t.value);
					return;
			}
		}
	}

	u32 forEachDataStreamInBytecode(InputMemoryStream& ip, auto f, u32 instruction_index_offset = 0) {
		u32 instruction_index = instruction_index_offset;
		auto forNumStreams = [&](u32 num, InstructionType itype, i32 ioffset) {
			for (u32 i = 0; i < num; ++i) {
				i32 pos = (i32)ip.getPosition();
				DataStream dst = ip.read<DataStream>();
				f(dst, pos, i, itype, ioffset, instruction_index);
			}
			++instruction_index;
		};
		for(;;) {
			i32 ioffset = (i32)ip.getPosition();
			InstructionType itype = ip.read<InstructionType>();
			switch (itype) {
				case InstructionType::END: return instruction_index;
				case InstructionType::NOISE: forNumStreams(2, itype, ioffset); break;
				case InstructionType::MOV: forNumStreams(2, itype, ioffset); break;
				case InstructionType::SIN: forNumStreams(2, itype, ioffset); break;
				case InstructionType::COS: forNumStreams(2, itype, ioffset); break;
				case InstructionType::SQRT: forNumStreams(2, itype, ioffset); break;
				case InstructionType::GT: forNumStreams(3, itype, ioffset); break;
				case InstructionType::LT: forNumStreams(3, itype, ioffset); break;
				case InstructionType::SUB: forNumStreams(3, itype, ioffset); break;
				case InstructionType::ADD: forNumStreams(3, itype, ioffset); break;
				case InstructionType::MUL: forNumStreams(3, itype, ioffset); break;
				case InstructionType::DIV: forNumStreams(3, itype, ioffset); break;
				case InstructionType::MOD: forNumStreams(3, itype, ioffset); break;
				case InstructionType::AND: forNumStreams(3, itype, ioffset); break;
				case InstructionType::OR: forNumStreams(3, itype, ioffset); break;
				case InstructionType::MIN: forNumStreams(3, itype, ioffset); break;
				case InstructionType::MAX: forNumStreams(3, itype, ioffset); break;
				case InstructionType::KILL: forNumStreams(1, itype, ioffset); break;
				case InstructionType::RAND: forNumStreams(1, itype, ioffset); ip.skip(sizeof(float) * 2); break;
				case InstructionType::EMIT:
						forNumStreams(1, itype, ioffset);
						ip.skip(sizeof(u32)); // emitter index
						instruction_index = forEachDataStreamInBytecode(ip, f, instruction_index); // emit subroutine
						break;
				default:
					ASSERT(false);
					break;
			}
		}
	}

	u32 optimizeBytecode(OutputMemoryStream& bytecode, u32 num_immutables) {
		// register allocation
		struct Lifetime {
			i32 register_index = -1;
			i32 from = -1;
			i32 to = -1;
			i32 remapped = -1;
			bool is_dst = false;
			i32 num_reads = 0;
		};
		InputMemoryStream ip(bytecode.data(), bytecode.size());
		StackArray<Lifetime, 16> lifetimes(m_arena_allocator);
		auto computeLifetimes = [&](){
			lifetimes.clear();
			ip.setPosition(0);
			forEachDataStreamInBytecode(ip, [&](const DataStream& s, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){
				if (s.type != DataStream::REGISTER) return;
				if (s.index < num_immutables) return;
			
				if (arg_index == 0 && itype != InstructionType::EMIT && itype != InstructionType::KILL) {
					Lifetime& lt = lifetimes.emplace();
					lt.register_index = s.index;
					lt.from = position;
					lt.to = position;
					lt.is_dst = true; 
					lt.num_reads = 0;
					return;
				}

				for (i32 i = lifetimes.size() - 1; i >= 0; --i) {
					if (lifetimes[i].register_index == s.index) {
						lifetimes[i].to = position;
						++lifetimes[i].num_reads;
						return;
					}
				}

				Lifetime& lt = lifetimes.emplace();
				lt.register_index = s.index;
				lt.from = position;
				lt.to = position;
				lt.is_dst = false;
				lt.num_reads = 1;
			});
		};
		computeLifetimes();

		// collapse unnecessary MOVs
		StackArray<i32, 16> movs_to_remove(m_arena_allocator);
		ip.setPosition(0);
		forEachDataStreamInBytecode(ip, [&](const DataStream& src, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){
			if (src.type != DataStream::REGISTER) return;
			if (src.index < num_immutables) return;
			if (itype != InstructionType::MOV) return;
			if (arg_index == 0) return;

			// we are mov-ing from register, it's also the only read from that value
			// collapse the mov instruction with the instruction which produced the value
			// e.g. `out = t * 5;` is collapsed into just a single instruction `mul out, t, 5`
			for (const Lifetime& lt : lifetimes) {
				if (lt.register_index == src.index && position == lt.to && lt.num_reads == 1 && lt.is_dst) {
					movs_to_remove.push(ioffset);
					DataStream mov_dst;
					i32 offset = ioffset + sizeof(InstructionType);
					memcpy(&mov_dst, bytecode.data() + offset, sizeof(mov_dst));
					memcpy(bytecode.getMutableData() + lt.from, &mov_dst, sizeof(mov_dst));
					
					// dst register starts to exist when the orignal src started
					if (mov_dst.type == DataStream::REGISTER) {
						for (Lifetime& dst_lifetime : lifetimes) {
							if (dst_lifetime.register_index == mov_dst.index) {
								ASSERT(lt.from < dst_lifetime.from);
								dst_lifetime.from = lt.from;
								break;
							}
						}
					}
					
					// original src register does not exist anymore
					lifetimes.eraseItems([&src](const Lifetime& l){ return l.register_index == src.index; });
					break;
				}
			}
		});

		for (i32 i = movs_to_remove.size() - 1; i >= 0; --i) {
			u8* data = bytecode.getMutableData();
			i32 offset = movs_to_remove[i];
			static constexpr u32 SIZE = sizeof(DataStream) * 2 + sizeof(InstructionType);
			memmove(data + offset, data + offset + SIZE, bytecode.size() - offset - SIZE);
			bytecode.resize(bytecode.size() - SIZE);
		}
		ip = InputMemoryStream(bytecode.data(), bytecode.size());
		computeLifetimes();

		// allocate registers
		if (!lifetimes.empty()) lifetimes[0].remapped = num_immutables;
		for (i32 i = 1, c = lifetimes.size(); i < c; ++i) {
			Lifetime& lt = lifetimes[i];
			// assign .remapped values so lifetimes with the same remapped value do no overlap
			lt.remapped = num_immutables;
			for (i32 j = 0; j < i; ++j) {
				const Lifetime& prev = lifetimes[j];
				if (prev.remapped == lt.remapped && !(lt.to < prev.from || prev.to < lt.from)) {
					++lt.remapped;
					j = -1; // restart check
				}
			}
		}

		ip.setPosition(0);
		forEachDataStreamInBytecode(ip, [&](const DataStream& s, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){
			if (s.type != DataStream::REGISTER) return;
			if (s.index < num_immutables) return;

			DataStream* ds = const_cast<DataStream*>((const DataStream*)(bytecode.data() + position));
			for (const Lifetime& lt : lifetimes) {
				if (lt.register_index == s.index && position >= lt.from && position <= lt.to) {
					ds->index = lt.remapped;
				}
			}
		});

		i32 max_used_register = (i32)num_immutables - 1;
		for (const Lifetime& lt : lifetimes) {
			max_used_register = maximum(max_used_register, lt.remapped);
		}
		return max_used_register + 1;
	}


	void compileFunction(Emitter& emitter) {
		StringView fn_name;
		if (!consume(Token::IDENTIFIER, fn_name)) return;
		if (!consume(Token::LEFT_PAREN)) return;
		if (!consume(Token::RIGHT_PAREN)) return;
	
		CompileContext ctx(*this);
		ctx.emitter = &emitter;

		OutputMemoryStream& compiled = [&]() -> OutputMemoryStream& {
			if (equalStrings(fn_name, "update")) return emitter.m_update;
			if (equalStrings(fn_name, "emit")) {
				for (const Variable& v : emitter.m_inputs) {
					switch (v.type) {
						case ValueType::VOID: ASSERT(false); break;
						case ValueType::FLOAT: ++ctx.value_counter; break;
						case ValueType::FLOAT3: ctx.value_counter += 3; break;
						case ValueType::FLOAT4: ctx.value_counter += 4; break;
					}
				}
				return emitter.m_emit;
			}
			if (equalStrings(fn_name, "output")) return emitter.m_output;
			error(fn_name, "Unknown function");
			return emitter.m_output;
		}();
		u32 stack_offset = ctx.value_counter;

		BlockNode* b = block(ctx);
		if (!b) return;
		compile(ctx, b, compiled);
		compiled.write(InstructionType::END);
		u32 num_used_registers = optimizeBytecode(compiled, stack_offset);
		InputMemoryStream ip(compiled.data(), compiled.size());
		u32 max_instruction_index = 0;
		forEachDataStreamInBytecode(ip, [&](const DataStream&, i32, i32, InstructionType, i32, i32 instruction_index){
			max_instruction_index = instruction_index;
		});

		if (equalStrings(fn_name, "update")) {
			emitter.m_num_update_registers = num_used_registers;
			emitter.m_num_update_instructions = max_instruction_index + 1;
		}
		else if (equalStrings(fn_name, "emit")) {
			emitter.m_num_emit_registers = num_used_registers;
			emitter.m_num_emit_instructions = max_instruction_index + 1;
		}
		else if (equalStrings(fn_name, "output")) {
			emitter.m_num_output_registers = num_used_registers;
			emitter.m_num_output_instructions = max_instruction_index + 1;
		}
	}

	CompileResult toCompileResult(const Variable& var, DataStream::Type type) {
		CompileResult res;
		switch (var.type) {
			case ValueType::FLOAT4: 
				res.streams[3].type = type;
				res.streams[3].index = var.offset + 3;
				++res.num_streams;
				// fallthrough
			case ValueType::FLOAT3: 
				res.streams[2].type = type;
				res.streams[2].index = var.offset + 2;
				res.streams[1].type = type;
				res.streams[1].index = var.offset + 1;
				res.num_streams += 2;
				// fallthrough
			case ValueType::FLOAT:
				res.streams[0].type = type;
				res.streams[0].index = var.offset;
				++res.num_streams;
				return res;
			case ValueType::VOID: return res;
		}
		ASSERT(false);
		return res;
	}

	DataStream pushStack(CompileContext& ctx) {
		DataStream res;
		res.type = DataStream::REGISTER;
		res.index = ctx.value_counter;
		++ctx.value_counter;
		ASSERT(ctx.value_counter < 255);
		return res;
	}

	CompileResult compile(CompileContext& ctx, Node* node, OutputMemoryStream& compiled) {
		CompileResult res;
		node = collapseConstants(node);
		switch (node->type) {
			case Node::UNARY_OPERATOR: {
				auto* n = (UnaryOperatorNode*)node;
				DataStream dst = pushStack(ctx);
				CompileResult right = compile(ctx, n->right, compiled);
				if (!right.success) return right;
				if (right.num_streams != 1) {
					error(n->right->token.value, "Only scalar is supported");
					return {.success = false};
				}
				//popStack(right.streams[0]);
				compiled.write(InstructionType::MUL);
				DataStream neg = {.type = DataStream::LITERAL, .value = -1};
				compiled.write(dst);
				compiled.write(neg);
				compiled.write(right.streams[0]);
				res.num_streams = 1;
				res.streams[0] = dst;
				return res;
			}
			case Node::BINARY_OPERATOR: {
				auto* n = (BinaryOperatorNode*)node;
				res.streams[0] = pushStack(ctx);
				res.streams[1] = pushStack(ctx);
				res.streams[2] = pushStack(ctx);
				res.streams[3] = pushStack(ctx);
				CompileResult left = compile(ctx, n->left, compiled);
				CompileResult right = compile(ctx, n->right, compiled);
				
				if (!left.success) return left;
				if (!right.success) return right;
				if (left.num_streams != right.num_streams && left.num_streams != 1 && right.num_streams != 1) {
					error(n->token.value, "Mismatched operands in binary operation.");
					return {.success = false };
				}
				res.num_streams = maximum(left.num_streams, right.num_streams);
				for (u32 i = 0; i < res.num_streams; ++i) {
					switch (n->op) {
						case Operators::MOD: compiled.write(InstructionType::MOD); break;
						case Operators::ADD: compiled.write(InstructionType::ADD); break;
						case Operators::SUB: compiled.write(InstructionType::SUB); break;
						case Operators::MUL: compiled.write(InstructionType::MUL); break;
						case Operators::DIV: compiled.write(InstructionType::DIV); break;
						case Operators::LT: compiled.write(InstructionType::LT); break;
						case Operators::GT: compiled.write(InstructionType::GT); break;
					}
					compiled.write(res.streams[i]);
					compiled.write(left.streams[i < left.num_streams ? i : 0]);
					compiled.write(right.streams[i < right.num_streams ? i : 0]);
				}
				return res;
			}
			case Node::COMPOUND: {
				auto* n = (CompoundNode*)node;
				if ((u32)n->elements.size() > lengthOf(res.streams)) {
					error(n->token.value, "Too many elements.");
					return {.success = false};
				}

				for (u32 i = 0; i < (u32)n->elements.size(); ++i) {
					CompileResult el = compile(ctx, n->elements[i], compiled);
					if (!el.success) return el;
					if (el.num_streams != 1) {
						error(n->elements[i]->token.value, "Compound elements must be scalars.");
						return {.success = false};
					}
					res.streams[i] = el.streams[0];
					res.num_streams = i + 1;
				}
				return res;
			}
			case Node::VARIABLE: {
				auto* n = (VariableNode*)node;
				switch (n->family) {
					case VariableFamily::GLOBAL:
						return toCompileResult(m_globals[n->index], DataStream::GLOBAL);
					case VariableFamily::LOCAL: {
						const Local& local = ctx.block->locals[n->index];
						// fallthroughs intentional
						switch (local.type) {
							case ValueType::VOID: ASSERT(false); return {.success=false};
							case ValueType::FLOAT4:
								res.streams[3].type = DataStream::REGISTER;
								res.streams[3].index = local.registers[3];
								++res.num_streams;
							case ValueType::FLOAT3:
								res.streams[2].type = DataStream::REGISTER;
								res.streams[2].index = local.registers[2];
								res.streams[1].type = DataStream::REGISTER;
								res.streams[1].index = local.registers[1];
								res.num_streams += 2;
							case ValueType::FLOAT:
								res.streams[0].type = DataStream::REGISTER;
								res.streams[0].index = local.registers[0];
								++res.num_streams;
								return res;
						}
					}
					case VariableFamily::CHANNEL:
						return toCompileResult(ctx.emitter->m_vars[n->index], DataStream::CHANNEL);
					case VariableFamily::OUTPUT:
						return toCompileResult(ctx.emitter->m_outputs[n->index], DataStream::OUT);
					case VariableFamily::INPUT:
						if (ctx.emitted) {
							return toCompileResult(ctx.emitted->m_inputs[n->index], DataStream::OUT);
						}
						return toCompileResult(ctx.emitter->m_inputs[n->index], DataStream::REGISTER);
				}
				ASSERT(false);
				return {.success = false};
			}
			case Node::SWIZZLE: {
				auto* n = (SwizzleNode*)node;
				CompileResult left = compile(ctx, n->left, compiled);
				if (!left.success) return left;
				StringView swizzle = n->token.value;
				res.num_streams = 0;
				if (swizzle.size() > lengthOf(res.streams)) {
					error(swizzle, "Invalid swizzle ", swizzle);
					return {.success = false};
				}
				for (const char* c = swizzle.begin; c != swizzle.end; ++c) {
					#define SET(i) do {\
						if (left.num_streams <= i) { \
							error(swizzle, "Accessing invalid element ", i); \
							return {.success = false}; \
						} \
						res.streams[res.num_streams] = left.streams[i]; \
					} while(false) \

					switch (*c) {
						case 'x': case 'r': SET(0); break;
						case 'y': case 'g': SET(1); break;
						case 'z': case 'b': SET(2); break;
						case 'w': case 'a': SET(3); break;
					}

					#undef SET
					++res.num_streams;
				}
				return res;
			}
			case Node::SYSTEM_VALUE: {
				auto* n = (SystemValueNode*)node;
				res.streams[0].type = DataStream::SYSTEM_VALUE;
				res.streams[0].index = (u8)n->value;
				res.num_streams = 1;
				return res;
			}
			case Node::RETURN: {
				auto* n = (ReturnNode*)node;
				return compile(ctx, n->value, compiled);
			}
			case Node::FUNCTION_ARG: {
				auto* n = (FunctionArgNode*)node;
				ASSERT((u32)n->index < ctx.args.size());
				res = ctx.args[n->index];
				return res;
			}
			case Node::FUNCTION_CALL: {
				auto* n = (FunctionCallNode*)node;
				CompileResult return_val;
				const Function& fn = m_functions[n->function_index];
				CompileResult args[8];
				if ((u32)n->args.size() > lengthOf(args)) {
					error(n->token.value, "Too many arguments.");
					return { .success = false };
				}
				for (i32 i = 0; i < n->args.size(); ++i) {
					args[i] = compile(ctx, n->args[i], compiled);
					if (!args[i].success) return args[i];
				}
				ctx.args = {args, &args[n->args.size()]};
				return compile(ctx, fn.block, compiled);
			}
			case Node::SYSCALL: {
				auto* n = (SysCallNode*)node;
				CompileResult args[8];
				DataStream dst = n->function.returns_value ? pushStack(ctx) : DataStream{};
				if ((u32)n->args.size() > lengthOf(args)) {
					error(n->token.value, "Too many arguments.");
					return {.success = false};
				}
				for (i32 i = 0; i < n->args.size(); ++i) {
					args[i] = compile(ctx, n->args[i], compiled);
					if (!args[i].success) return args[i];
					if (args[i].num_streams != 1) {
						error(n->args[i]->token.value, "Only scalar arguments are supported.");
						return {.success = false};
						// TODO vector args
					}
				}
				compiled.write(n->function.instruction);
				if (n->function.instruction == InstructionType::EMIT) {
					compiled.write(args[1].streams[0]);
					compiled.write(u32(args[0].streams[0].value));
					//popStack(args[0].streams[0]);
					if (n->after_block) {
						CompileContext inner_ctx = ctx;
						if (n->function.instruction == InstructionType::EMIT) {
							inner_ctx.emitted = &m_emitters[u32(args[0].streams[0].value)];
						}
						if (!compile(inner_ctx, n->after_block, compiled).success) return {.success = false};
						compiled.write(InstructionType::END);
					}
				}
				else {
					if (n->function.returns_value) {
						compiled.write(dst);
						res.streams[0] = dst;
						res.num_streams = 1;
					}
					for (i32 i = 0; i < n->args.size(); ++i) {
						if (n->function.instruction == InstructionType::RAND) { 
							compiled.write(args[i].streams[0].value);
						}
						else {
							compiled.write(args[i].streams[0]);
						}
						//popStack(args[i].streams[0]);
					}
				}
				return res;
			}
			case Node::BLOCK: {
				BlockNode* prev_block = ctx.block;
				auto* n = (BlockNode*)node;
				ctx.block = n;
				res.streams[0] = pushStack(ctx);
				res.streams[1] = pushStack(ctx);
				res.streams[2] = pushStack(ctx);
				res.streams[3] = pushStack(ctx);
				// alloc space for locals
				for (Local& l : n->locals) {
					// fallthrough intentional
					switch (l.type) {
						case ValueType::VOID: ASSERT(false);
						case ValueType::FLOAT4:
							l.registers[3] = pushStack(ctx).index;
						case ValueType::FLOAT3:
							l.registers[2] = pushStack(ctx).index;
							l.registers[1] = pushStack(ctx).index;
						case ValueType::FLOAT:
							l.registers[0] = pushStack(ctx).index;
							break;
					}
				}
				// execute statements
				for (Node* statement : n->statements) {
					if (statement->type == Node::RETURN) {
						res = compile(ctx, statement, compiled);
						ctx.block = prev_block;
						return res;
					}
					else {
						if (!compile(ctx, statement, compiled).success) return {.success=false};
					}
				}
				ctx.block = prev_block;
				return res;
			}
			case Node::EMITTER_REF:
				res.streams[0].type = DataStream::LITERAL;
				res.streams[0].value = float(((EmitterRefNode*)node)->index);
				res.num_streams = 1;
				return res;
			case Node::LITERAL:
				res.streams[0].type = DataStream::LITERAL;
				res.streams[0].value = ((LiteralNode*)node)->value;
				res.num_streams = 1;
				return res;
			case Node::ASSIGN: {
				auto* n = (AssignNode*)node;
				CompileResult left = compile(ctx, n->left, compiled);
				CompileResult right = compile(ctx, n->right, compiled);
				if (!right.success) return right;
				if (!left.success) return left;
				if (left.num_streams > right.num_streams && right.num_streams != 1) {
					error(node->token.value, "Trying to assign ", right.num_streams, " values into ", left.num_streams);
					return {.success = false};
				}
				for (u32 i = 0; i < left.num_streams; ++i) {
					compiled.write(InstructionType::MOV);
					compiled.write(left.streams[i]);
					compiled.write(right.streams[right.num_streams == 1 ? 0 : i]);
				}
				return {.success = true };
			}
			default: break;
		}
		
		error(node->token.value, "Unknown error compiling ", node->token.value);
		return {.success = false};
	}


	void compileFunction() {
		Function& fn = m_functions.emplace(m_allocator);
		if (!consume(Token::IDENTIFIER, fn.name)) return;
		parseArgs(fn);
		
		CompileContext ctx(*this);
		ctx.function = &fn;
		fn.block = block(ctx);
	}

	void compileMesh(Emitter& emitter) {
		StringView value;
		if (!consume(Token::STRING, value)) return;
		emitter.m_mesh = value;
	}

	void compileMaterial(Emitter& emitter) {
		StringView value;
		if (!consume(Token::STRING, value)) return;
		emitter.m_material = value;
	}

	float consumeFloat() {
		return asFloat(consumeToken());
	}

	u32 consumeU32() {
		Token t = consumeToken();
		if (t.type != Token::NUMBER) {
			error(t.value, "Expected number.");
			return 0;
		}
		u32 res;
		const char* end = fromCString(t.value, res);
		if (end != t.value.end) {
			error(t.value, "Expected u32.");
			return res;
		}
		return res;
	}

	void compileEmitter() {
		Emitter& emitter = m_emitters.emplace(m_allocator);
		if (!consume(Token::IDENTIFIER, emitter.m_name)) return;
		if (!consume(Token::LEFT_BRACE)) return;

		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::ERROR: return;
				case Token::FN: compileFunction(emitter); break;
				case Token::VAR: variableDeclaration(emitter.m_vars); break;
				case Token::OUT: variableDeclaration(emitter.m_outputs); break;
				case Token::IN: variableDeclaration(emitter.m_inputs); break;
				case Token::EOF:
					error(token.value, "Unexpected end of file.");
					return;
				case Token::RIGHT_BRACE:
					if (emitter.m_max_ribbons > 0 && emitter.m_max_ribbon_length == 0) {
						error(token.value, "max_ribbon_length must be > 0 if max_ribbons is > 0");
					}
					return;
				case Token::IDENTIFIER:
					if (equalStrings(token.value, "material")) compileMaterial(emitter);
					else if (equalStrings(token.value, "mesh")) compileMesh(emitter);
					else if (equalStrings(token.value, "emit_on_move")) emitter.m_emit_on_move = true;
					else if (equalStrings(token.value, "init_emit_count")) emitter.m_init_emit_count = consumeU32();
					else if (equalStrings(token.value, "emit_per_second")) emitter.m_emit_per_second = consumeFloat();
					else if (equalStrings(token.value, "max_ribbons")) emitter.m_max_ribbons = consumeU32();
					else if (equalStrings(token.value, "max_ribbon_length")) emitter.m_max_ribbon_length = consumeU32();
					else if (equalStrings(token.value, "init_ribbons_count")) emitter.m_init_ribbons_count = consumeU32();
					else {
						error(token.value, "Unexpected identifier ", token.value);
						return;
					}
					break;
				default:
					error(token.value, "Unexpected token ", token.value);
					return;
			}
		}
	}

	void fillVertexDecl(const Emitter& emitter, gpu::VertexDecl& decl) {
		u32 offset = 0;
		for (const Variable& o : emitter.m_outputs) {
			switch (o.type) {
				case ValueType::VOID: ASSERT(false); break; 
				case ValueType::FLOAT: 
					decl.addAttribute(offset, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(float);
					break;
				case ValueType::FLOAT3: 
					decl.addAttribute(offset, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec3);
					break;
				case ValueType::FLOAT4: 
					decl.addAttribute(offset, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec4);
					break;
			}
		}
	}

	bool compile(const Path& path, StringView code, OutputMemoryStream& output) {
		m_path = path;
		m_tokenizer.m_current = code.begin;
		m_tokenizer.m_document = code;
		m_tokenizer.m_current_token = m_tokenizer.nextToken();

		// TODO error reporting
		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::EOF: goto write_label;
				case Token::ERROR: return false;
				case Token::IMPORT: compileImport(); break;
				case Token::CONST: compileConst(); break;
				case Token::FN: compileFunction(); break;
				case Token::GLOBAL: variableDeclaration(m_globals); break;
				case Token::EMITTER: compileEmitter(); break;
				case Token::WORLD_SPACE: m_is_world_space = true; break;
				default: error(token.value, "Unexpected token ", token.value); return false;
			}
		}

		write_label:
		ParticleSystemResource::Header header;
		output.write(header);

		ParticleSystemResource::Flags flags = m_is_world_space ? ParticleSystemResource::Flags::WORLD_SPACE : ParticleSystemResource::Flags::NONE;
		output.write(flags);
		
		output.write(m_emitters.size());
		auto getCount = [](const auto& x){
			u32 c = 0;
			for (const auto& i : x) {
				switch (i.type) {
					case ValueType::FLOAT4: c+= 4; break;
					case ValueType::FLOAT3: c+= 3; break;
					case ValueType::FLOAT: c+= 1; break;
					case ValueType::VOID: ASSERT(false); break;
				}
			}
			return c;
		};

		for (const Emitter& emitter : m_emitters) {
			gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLE_STRIP);
			fillVertexDecl(emitter, decl);
			output.write(decl);
			output.writeString(emitter.m_material);
			output.writeString(emitter.m_mesh);
			const u32 count = u32(emitter.m_update.size() + emitter.m_emit.size() + emitter.m_output.size());
			output.write(count);
			output.write(emitter.m_update.data(), emitter.m_update.size());
			output.write(emitter.m_emit.data(), emitter.m_emit.size());
			output.write(emitter.m_output.data(), emitter.m_output.size());
			output.write((u32)emitter.m_update.size());
			output.write(u32(emitter.m_update.size() + emitter.m_emit.size()));

			output.write(getCount(emitter.m_vars));
			output.write(emitter.m_num_update_registers);
			output.write(emitter.m_num_emit_registers);
			output.write(emitter.m_num_output_registers);
			output.write(emitter.m_num_update_instructions);
			output.write(emitter.m_num_emit_instructions);
			output.write(emitter.m_num_output_instructions);
			output.write(getCount(emitter.m_outputs));
			output.write(emitter.m_init_emit_count);
			output.write(emitter.m_emit_per_second);
			output.write(getCount(emitter.m_inputs));
			output.write(emitter.m_max_ribbons);
			output.write(emitter.m_max_ribbon_length);
			output.write(emitter.m_init_ribbons_count);
			output.write(emitter.m_emit_on_move);
		}
		output.write(m_globals.size());
		for (const Variable& p : m_globals) {
			output.writeString(p.name);
			switch (p.type) {
				case ValueType::VOID: ASSERT(false); break;
				case ValueType::FLOAT: output.write(u32(1)); break;
				case ValueType::FLOAT3: output.write(u32(3)); break;
				case ValueType::FLOAT4: output.write(u32(4)); break;
			}
		}
		
		return !m_is_error;
	}

	StudioApp& m_app;
	IAllocator& m_allocator;
	ArenaAllocator m_arena_allocator;
	Path m_path;
	bool m_is_error = false;
	ParticleScriptTokenizer m_tokenizer;
	bool m_is_world_space = false;
	Array<Constant> m_constants;
	Array<Function> m_functions;
	Array<Variable> m_globals;
	Array<Emitter> m_emitters;
};


struct ParticleScriptImportEditorWindow : AssetEditorWindow {
	ParticleScriptImportEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_app(app)
		, m_path(path)
	{
		m_editor = createParticleScriptEditor(m_app);
		m_editor->focus();
			
		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			StringView v((const char*)blob.data(), (u32)blob.size());
			m_editor->setText(v);
		}
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}

	void fileChangedExternally() override {
		OutputMemoryStream tmp(m_app.getAllocator());
		OutputMemoryStream tmp2(m_app.getAllocator());
		m_editor->serializeText(tmp);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.getContentSync(m_path, tmp2)) return;

		if (tmp.size() == tmp2.size() && memcmp(tmp.data(), tmp2.data(), tmp.size()) == 0) {
			m_dirty = false;
		}
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();

		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			ImGui::EndMenuBar();
		}

		if (m_editor->gui("codeeditor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;
	}
	
	const Path& getPath() override { return m_path; }
	const char* getName() const override { return "particle script import editor"; }

	StudioApp& m_app;
	UniquePtr<CodeEditor> m_editor;
	Path m_path;
};

struct ParticleScriptImportPlugin : EditorAssetPlugin {
	ParticleScriptImportPlugin(StudioApp& app, IAllocator& allocator)
		: EditorAssetPlugin("Particle script import", "pai", TYPE, app, allocator)
		, m_app(app)
	{}

	bool compile(const Path& src) override { return true; }
	void openEditor(const Path& path) override {
		UniquePtr<ParticleScriptImportEditorWindow> win = UniquePtr<ParticleScriptImportEditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}
	void createResource(OutputMemoryStream& blob) override {}

	StudioApp& m_app;
	static inline ResourceType TYPE = ResourceType("particle_script_import");
};

struct ParticleScriptPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	ParticleScriptPlugin(StudioApp& app, IAllocator& allocator)
		: m_app(app)
		, m_allocator(allocator)
	{
		AssetCompiler& compiler = app.getAssetCompiler();
		compiler.registerExtension("pat", ParticleSystemResource::TYPE);
		const char* particle_emitter_exts[] = { "pat" };
		compiler.addPlugin(*this, Span(particle_emitter_exts));
		m_app.getAssetBrowser().addPlugin(*this, Span(particle_emitter_exts));
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;

		StringView content = { (const char*)src_data.data(), (const char*)src_data.data() + src_data.size() };
		ParticleScriptCompiler compiler(m_app, m_allocator);
		OutputMemoryStream output(m_app.getAllocator());
		if (!compiler.compile(src, content, output)) return false;

		return m_app.getAssetCompiler().writeCompiledResource(src, Span(output.data(), (i32)output.size()));
	}

	const char* getIcon() const override { return ICON_FA_FIRE; }

	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32&) override {
		compiler.addResource(ParticleSystemResource::TYPE, path);
		
		OutputMemoryStream content(m_allocator);
		if (!m_app.getEngine().getFileSystem().getContentSync(path, content)) {
			logError("Failed to read ", path);
			return;
		}

		ParticleScriptTokenizer tokenizer;
		tokenizer.m_document.begin = (const char*)content.data();
		tokenizer.m_document.end = tokenizer.m_document.begin + content.size();
		tokenizer.m_current = tokenizer.m_document.begin;
		tokenizer.m_current_token = tokenizer.nextToken();
		
		for (;;) {
			ParticleScriptTokenizer::Token token = tokenizer.m_current_token;
			tokenizer.m_current_token = tokenizer.nextToken();
			
			switch (token.type) {
				case ParticleScriptTokenizer::Token::EOF: return;
				case ParticleScriptTokenizer::Token::ERROR: return;
				case ParticleScriptTokenizer::Token::IMPORT: {
					ParticleScriptTokenizer::Token t = tokenizer.m_current_token;
					tokenizer.m_current_token = tokenizer.nextToken();
					if (t.type == ParticleScriptTokenizer::Token::STRING) {
						m_app.getAssetCompiler().registerDependency(path, Path(t.value));
					}
					break;
				}
				default: break;
			}
		}	
	}

	bool canCreateResource() const override { return true; }
	bool canMultiEdit() override { return false; }
	void createResource(struct OutputMemoryStream& content) override {
		content << R"#(
emitter Emitter0 {
	material "/engine/materials/particle.mat"
	init_emit_count 0
	emit_per_second 10
	
	out i_position : float3
	out i_scale : float
	out i_color : float4
	out i_rot : float
	out i_frame : float
	out i_emission : float

	var pos : float3
	var t : float

	fn update() {
		t = t + time_delta;
		kill(t > 1);
	}
	fn emit() {
		pos.x = random(-1, 1);
		pos.y = random(0, 2);
		pos.z = random(-1, 1);
		t = 0;
	}
	fn output() {
		i_position = pos;
		i_scale = 0.1;
		i_color = {1, 0, 0, 1};
		i_rot = 0;
		i_frame = 0;
		i_emission = 10;
	}
}
		)#";
	}
	const char* getDefaultExtension() const override { return "pat"; }

	const char* getLabel() const override { return "Particle script"; }
	ResourceType getResourceType() const override { return ParticleSystemResource::TYPE; }
	
	void openEditor(const struct Path& path) override {
		UniquePtr<ParticleScriptEditorWindow> win = UniquePtr<ParticleScriptEditorWindow>::create(m_allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	IAllocator& m_allocator;
	StudioApp& m_app;
};

}