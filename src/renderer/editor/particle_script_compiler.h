#pragma once

#include "core/arena_allocator.h"
#include "core/defer.h"
#include "core/log.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "renderer/editor/world_viewer.h"
#include "renderer/particle_system.h"
#include "renderer/render_module.h"

// TODO bugs
// errors in imports in compileIR results in crash
// hotreload of common.hlsli does not work

// TODO maybe / low prio
// * autocomplete
// * spline, mesh, terrain
// * saturate, floor, round
// * preview for world space moving ribbons
// * emit_ribbon()
// * kill in ribbons
// * multiply-add
// * while, for cycles
// * debugger
// * global update - runs once per frame on the whole emitter, can prepare some global data
// * create mesh from script
// * JIT https://github.com/asmjit/asmjit

namespace Lumix {

struct ParticleScriptToken {
	enum Type {
		EOF, ERROR, SEMICOLON, COMMA, COLON, DOT, 
		LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
		STAR, SLASH, MINUS, PLUS, EQUAL, PERCENT, GT, LT,
		NUMBER, STRING, IDENTIFIER,

		// keywords
		CONST, GLOBAL, EMITTER, FN, VAR, OUT, IN, LET, RETURN, IMPORT, IF, ELSE, AND, OR, NOT
	};

	Type type;
	StringView value;
};

struct ParticleScriptTokenizer {
	using Token = ParticleScriptToken;
	
	enum class Operators {
		ADD = '+',
		SUB = '-',
		DIV = '/',
		MUL = '*',
		MOD = '%',
		LT = '<',
		GT = '>',
		AND,
		OR,
		NOT
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
			case 'a': return checkKeyword("nd", 1, 2, Token::AND);
			case 'c': return checkKeyword("onst", 1, 4, Token::CONST);
			case 'e': {
				if (m_current - m_start_token < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'm': return checkKeyword("itter", 2, 5, Token::EMITTER);
					case 'l': return checkKeyword("se", 2, 2, Token::ELSE);
				}
			}
			case 'f': return checkKeyword("n", 1, 1, Token::FN);
			case 'g': return checkKeyword("lobal", 1, 5, Token::GLOBAL);
			case 'i': {
				if (m_current - m_start_token < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'f': return checkKeyword("", 2, 0, Token::IF);
					case 'n': return checkKeyword("", 2, 0, Token::IN);
					case 'm': return checkKeyword("port", 2, 4, Token::IMPORT);
				}
				return makeToken(Token::IDENTIFIER);
			}
			case 'l': return checkKeyword("et", 1, 2, Token::LET);
			case 'n': return checkKeyword("ot", 1, 2, Token::NOT);
			case 'o': {
				if (m_current - m_start_token < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'r': return checkKeyword("", 2, 0, Token::OR);
					case 'u': return checkKeyword("t", 2, 1, Token::OUT);
				}
				return makeToken(Token::IDENTIFIER);
			}
			case 'r': return checkKeyword("eturn", 1, 5, Token::RETURN);
			case 'v': return checkKeyword("ar", 1, 2, Token::VAR);
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
		FLOAT2,
        FLOAT3,
        FLOAT4,

		VOID
    };

	enum class EntryPoint {
		EMIT,
		UPDATE,
		OUTPUT,
		GLOBAL
	};

	struct SysCall {
        InstructionType instruction;
        bool returns_value;
		u32 num_args;
		u32 valid_entry_points =
			  (1 << u32(EntryPoint::EMIT)) 
			| (1 << u32(EntryPoint::UPDATE))
			| (1 << u32(EntryPoint::OUTPUT))
			;
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

	struct Local {
		StringView name;
		ValueType type;
		i32 registers[4] = {-1, -1, -1, -1};
		bool is_const[4] = {true, true, true, true};
		float values[4] = {};
	};

	struct BlockNode;

	struct Function {
		Function(IAllocator& allocator) : args(allocator) {}
		StringView name;
		Array<StringView> args;
		BlockNode* block = nullptr;
		bool is_inlining = false;
	};

	struct Variable {
		StringView name;
		ValueType type;
		i32 offset = 0;

        i32 getOffsetSub(u32 sub) const {
            switch (type) {
                case ValueType::VOID: ASSERT(false); return offset;
                case ValueType::FLOAT: return offset;
				case ValueType::FLOAT2: return offset + minimum(sub, 1);
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
		float m_emit_move_distance = -1;
		float m_emit_per_second = 0;
		u32 m_max_ribbons = 0;
		u32 m_max_ribbon_length = 0;
		u32 m_init_ribbons_count = 0;
		u32 m_tube_segments = 0;
	};

	struct CompileContext {
		CompileContext(ParticleScriptCompiler& compiler) : compiler(compiler) {} 
		ParticleScriptCompiler& compiler; 
		const Function* function = nullptr;
		Emitter* emitter = nullptr;
		Emitter* emitted = nullptr;
		BlockNode* block = nullptr;
		EntryPoint entry_point;


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
			SWIZZLE,
			SYSCALL,
			SYSTEM_VALUE,
			COMPOUND,
			EMITTER_REF,
			BLOCK,
			FUNCTION_CALL,
			IF
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

	struct IfNode : Node {
		IfNode(Token token) : Node(Node::IF, token) {}
		Node* condition = nullptr;
		BlockNode* true_block = nullptr;
		BlockNode* false_block = nullptr;
	};\

	struct SysCallNode : Node {
		SysCallNode(Token token, IAllocator& allocator) : Node(Node::SYSCALL, token), args(allocator) {}
		SysCall function;
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
		BlockNode* block; // for locals
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
	
	ParticleScriptCompiler(FileSystem& fs, IAllocator& allocator)
		: m_filesystem(fs)
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
		if (equalStrings(type, "float2")) return ValueType::FLOAT2;
		if (equalStrings(type, "float3")) return ValueType::FLOAT3;
		if (equalStrings(type, "float4")) return ValueType::FLOAT4;
		error(type, "Unknown type");
		return ValueType::FLOAT;
	}

	void variableDeclaration(Array<Variable>& vars) {
		StringView name;
		if (!consume(Token::IDENTIFIER, name)) return;
		for (const Variable& existing : vars) {
			if (equalStrings(existing.name, name)) {
				error(name, "Variable '", name, "' already exists.");
				return;
			}
		}
		u32 offset = 0;
		if (!vars.empty()) {
			const Variable& var = vars.last();
			offset = var.offset;
			switch (var.type) {
				case ValueType::VOID: ASSERT(false); break;
				case ValueType::FLOAT: ++offset; break;
				case ValueType::FLOAT2: offset += 2; break;
				case ValueType::FLOAT3: offset += 3; break;
				case ValueType::FLOAT4: offset += 4; break;
			}
		}
		Variable& var = vars.emplace();
		var.name = name;
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
		if(!m_is_error && !m_suppress_logging) logError(m_path, "(", getLine(location), "): ", args...);
		m_is_error = true;
	}

	template <typename... Args>
	void errorAtCurrent(Args&&... args) {
		if(!m_is_error && !m_suppress_logging) logError(m_path, "(", getLine(m_tokenizer.m_current_token.value), "): ", args...);
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
			case Token::Type::AND: return "and";
			case Token::Type::OR: return "or";
			case Token::Type::NOT: return "not";
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
			case Token::Type::VAR: return "var";
			case Token::Type::OUT: return "out";
			case Token::Type::IN: return "in";
			case Token::Type::LET: return "let";
			case Token::Type::RETURN: return "return";
			case Token::Type::IMPORT: return "import";
			case Token::Type::IF: return "if";
			case Token::Type::ELSE: return "else";
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
			case Token::OR: case Token::AND: return 1;
			case Token::GT: case Token::LT: return 2;
			case Token::PLUS: case Token::MINUS: return 3;
			case Token::PERCENT: case Token::STAR: case Token::SLASH: return 4;
			default: ASSERT(false);
		}
		return 0;
	}

	i32 toCount(ValueType type) {
		switch (type) {
			case ValueType::VOID: return 0;
			case ValueType::FLOAT: return 1;
			case ValueType::FLOAT2: return 2;
			case ValueType::FLOAT3: return 3;
			case ValueType::FLOAT4: return 4;
		}
		ASSERT(false);
		return -1;
	}

	template <typename F>
	void forEachSwizzleToIndex(StringView swizzle, F&& f) {
		for (u32 i = 0, c = swizzle.size(); i < c; ++i) {	
			switch (swizzle[i]) {
				case 'x': case 'r': f(0); break;
				case 'y': case 'g': f(1); break;
				case 'z': case 'b': f(2); break;
				case 'w': case 'a': f(3); break;
			}
		}
	}

	// Can evaluate AST tree. It's used to get the value of compile-time constants.
	struct ASTEvaluator {
		ASTEvaluator(ParticleScriptCompiler& compiler, IAllocator& allocator)
			: stack(allocator)
			, compiler(compiler)
		{}

		StackArray<float, 16> stack;
		ParticleScriptCompiler& compiler;
		i32 arg_offset = -9000;

		// returns false on failure
		bool eval(Node* node) {
			if (compiler.m_is_error) return false;

			switch (node->type) {
				case Node::FUNCTION_ARG: {
					auto* n = (FunctionArgNode*)node;
					stack.push(stack[arg_offset + n->index]);
					return true;
				}
				case Node::RETURN: {
					auto* n = (ReturnNode*)node;
					return eval(n->value);
				}
				case Node::BLOCK: {
					auto* n = (BlockNode*)node;
					for (Node* statement : n->statements) {
						if (!eval(statement)) return false;
					}
					return true;
				}
				case Node::IF: {
					auto* n = (IfNode*)node;
					if (!eval(n->condition)) return false;
					float cond = stack.back();
					stack.pop();
					if (cond != 0.0f) {
						return eval(n->true_block);
					} else if (n->false_block) {
						return eval(n->false_block);
					}
					return true;
				}
				case Node::FUNCTION_CALL: {
					// TODO we assume all args and result is float
					auto* n = (FunctionCallNode*)node;
					const Function& fn = compiler.m_functions[n->function_index];
					u32 prev_arg_offset = arg_offset;
					arg_offset = stack.size();
					for (Node* arg : n->args) {
						if (!eval(arg)) return false;
					}
					u32 args_end = stack.size();
					if (!eval(fn.block)) return false;
					ASSERT(stack.size() == args_end + 1); // TODO
					float result = stack.back();
					stack.pop();
					for (u32 i = 0; i < (u32)n->args.size(); ++i) {
						stack.pop();
					}
					stack.push(result);
					arg_offset = prev_arg_offset;
					return true;
				}
				case Node::UNARY_OPERATOR: {
					auto* n = (UnaryOperatorNode*)node;
					i32 prev = stack.size();
					if (!eval(n->right)) return false;
					i32 count = stack.size() - prev;
					if (count == 0) {
						compiler.errorAtCurrent("Invalid unary operation.");
						return false;
					}
					float vals[4];
					for (i32 i = count - 1; i >= 0; --i) {
						vals[i] = stack.back();
						stack.pop();
					}
					for (i32 i = 0; i < count; ++i) {
						float res;
						switch (n->op) {
							case Operators::SUB: res = -vals[i]; break;
							case Operators::NOT: res = vals[i] == 0.0f ? 1.0f : 0.0f; break;
							default: ASSERT(false); return false;
						}
						stack.push(res);
					}
					return true;
				}
				case Node::SYSCALL: {
					auto* n = (SysCallNode*)node;
					for (Node* arg : n->args) {
						if (!eval(arg)) return false;
					}
					switch (n->function.instruction) {
						case InstructionType::COS: {
							ASSERT(n->args.size() == 1);
							float& v = stack.back();
							v = cosf(v);
							return true;
						}
						case InstructionType::SIN: {
							ASSERT(n->args.size() == 1);
							float& v = stack.back();
							v = sinf(v);
							return true;
						}
						case InstructionType::SQRT: {
							ASSERT(n->args.size() == 1);
							float& v = stack.back();
							v = sqrtf(v);
							return true;
						}
						case InstructionType::MIN: {
							ASSERT(n->args.size() == 2);
							float v1 = stack.back(); stack.pop();
							float& v0 = stack.back();
							v0 = minimum(v0, v1);
							return true;
						}
						case InstructionType::RAND: {
							compiler.errorAtCurrent("Random called when trying to evaluate a compile-time constant.");
							return true;
						}
						case InstructionType::MAX: {
							ASSERT(n->args.size() == 2);
							float v1 = stack.back(); stack.pop();
							float& v0 = stack.back();
							v0 = maximum(v0, v1);
							return true;
						}
						default: break;
					}
					ASSERT(false); 
					return false;
				}
				case Node::BINARY_OPERATOR: {
					auto* n = (BinaryOperatorNode*)node;
					i32 l = stack.size();
					if (!eval(n->left)) return false;
					i32 r = stack.size();
					if (!eval(n->right)) return false;
					i32 left_count = r - l;
					i32 right_count = stack.size() - r;
					if (left_count != right_count) {
						compiler.errorAtCurrent("Vector sizes don't match in binary operation.");
						return false;
					}
					i32 count = left_count;
					if (count == 0) {
						compiler.errorAtCurrent("Invalid binary operation.");
						return false;
					}
					float left_vals[4];
					float right_vals[4];
					for (i32 i = count - 1; i >= 0; --i) {
						right_vals[i] = stack.back();
						stack.pop();
					}
					for (i32 i = count - 1; i >= 0; --i) {
						left_vals[i] = stack.back();
						stack.pop();
					}
					for (i32 i = 0; i < count; ++i) {
						float lv = left_vals[i];
						float rv = right_vals[i];
						float res;
						switch (n->op) {
							case Operators::ADD: res = lv + rv; break;
							case Operators::SUB: res = lv - rv; break;
							case Operators::MUL: res = lv * rv; break;
							case Operators::DIV:
								if (rv == 0) {
									compiler.errorAtCurrent("Division by zero.");
									return false;
								}
								res = lv / rv;
								break;
							case Operators::MOD: res = fmodf(lv, rv); break;
							case Operators::LT: res = lv < rv ? 1.0f : 0.0f; break;
							case Operators::GT: res = lv > rv ? 1.0f : 0.0f; break;
							case Operators::AND: res = (lv != 0.0f && rv != 0.0f) ? 1.0f : 0.0f; break;
							case Operators::OR: res = (lv != 0.0f || rv != 0.0f) ? 1.0f : 0.0f; break;
							default: ASSERT(false); return false;
						}
						stack.push(res);
					}
					return true;
				}
				case Node::LITERAL: {
					auto* n = (LiteralNode*)node;
					stack.push(n->value);
					return true;
				}
				case Node::COMPOUND: {
					auto* n = (CompoundNode*)node;
					for (Node* elem : n->elements) {
						if (!eval(elem)) return false;
					}
					return true;
				}
				default: break;
			} // switch

			compiler.errorAtCurrent("Operation not supported at compile-time.");
			return false;
		}
	};

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
	
	static bool tokenMatchRemaining(StringView name, const char* remaining, i32 start, i32 len) {
		if (name.size() != start + len) return false;
		if (memcmp(name.begin + start, remaining, len) == 0) return true;
		return false;
	}

	ParticleSystemValues getSystemValue(StringView name) {
		switch (name[0]) {
			case 'e':
				if (tokenMatchRemaining(name, "mit_index", 1, 9)) return ParticleSystemValues::EMIT_INDEX;
				break; 
			case 'r':
				if (tokenMatchRemaining(name, "ibbon_index", 1, 11)) return ParticleSystemValues::RIBBON_INDEX;
				break; 
			case 't': 
				if (name.size() < 2) return ParticleSystemValues::NONE;
				switch (name[1]) {
					case 'i':
						if (tokenMatchRemaining(name, "me_delta", 2, 8)) return ParticleSystemValues::TIME_DELTA;
						break; 
					case 'o':
						if (tokenMatchRemaining(name, "tal_time", 2, 8)) return ParticleSystemValues::TOTAL_TIME;
						break; 
				}
				break;
		}
		return ParticleSystemValues::NONE;
	}

	SysCall checkBuiltinFunction(StringView name, const char* remaining, i32 start, i32 len, SysCall if_matching) {
		if (name.size() != start + len) return {InstructionType::END};
		name.removePrefix(start);
		if (memcmp(name.begin, remaining, len) == 0) return if_matching;
		return {InstructionType::END};
	}

	SysCall getSysCall(StringView name) {
		switch (name[0]) {
			case 'c': 
				if (name.size() < 2) return {InstructionType::END};
				switch (name[1]) {
					case 'o': return checkBuiltinFunction(name, "s", 2, 1, { InstructionType::COS, true, 1 });
					case 'u': return checkBuiltinFunction(name, "rve", 2, 3, { InstructionType::GRADIENT, true, 0xff });
				}
				return { InstructionType::END };
				
			case 'e': return checkBuiltinFunction(name, "mit", 1, 3, { InstructionType::EMIT, false, 1, 1 << u32(EntryPoint::UPDATE) });
			case 'k': return checkBuiltinFunction(name, "ill", 1, 3, { InstructionType::KILL, false, 0, 1 << u32(EntryPoint::UPDATE) });
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
					break;
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

				if (equalStrings(token.value, "entity_position")) {
					auto* node = LUMIX_NEW(m_arena_allocator, CompoundNode)(token, m_arena_allocator);
					node->elements.reserve(3);
					auto* x = LUMIX_NEW(m_arena_allocator, SystemValueNode)(token);
					x->value = ParticleSystemValues::ENTITY_POSITION_X;
					node->elements.push(x);
					
					auto* y = LUMIX_NEW(m_arena_allocator, SystemValueNode)(token);
					y->value = ParticleSystemValues::ENTITY_POSITION_Y;
					node->elements.push(y);
					
					auto* z = LUMIX_NEW(m_arena_allocator, SystemValueNode)(token);
					z->value = ParticleSystemValues::ENTITY_POSITION_Z;
					node->elements.push(z);
					return node;
				}

				ParticleSystemValues system_value = getSystemValue(token.value);
				if (system_value != ParticleSystemValues::NONE) {
					auto* node = LUMIX_NEW(m_arena_allocator, SystemValueNode)(token);
					node->value = system_value;
					return node;
				}

				SysCall syscall = getSysCall(token.value);
				if (syscall.instruction != InstructionType::END) {
					auto* node = LUMIX_NEW(m_arena_allocator, SysCallNode)(token, m_arena_allocator);
					if (!consume(Token::LEFT_PAREN)) return nullptr;
					node->args.reserve(syscall.num_args);
					for (u32 i = 0; i < syscall.num_args; ++i) {
						if (i > 0 && !consume(Token::COMMA)) return nullptr;
						Node* arg = expression(ctx, 0);
						if (!arg) return nullptr;
						node->args.push(arg);
					}
					if (!consume(Token::RIGHT_PAREN)) return nullptr;
		
					if (peekToken().type == Token::LEFT_BRACE) {
						CompileContext inner_ctx = ctx;
						if (syscall.instruction == InstructionType::EMIT) {
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

					node->function = syscall;
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
					switch (c->type) {
						case ValueType::VOID: {
							ASSERT(false);
							return nullptr;
						}
						case ValueType::FLOAT: {
							auto* node = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							node->value = c->value[0];
							return node;
						}
						case ValueType::FLOAT2: {
							auto* node = LUMIX_NEW(m_arena_allocator, CompoundNode)(token, m_arena_allocator);
							auto* x = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							x->value = c->value[0];
							auto* y = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							y->value = c->value[1];
							node->elements.reserve(2);
							node->elements.push(x);
							node->elements.push(y);
							return node;
						}
						case ValueType::FLOAT3: {
							auto* node = LUMIX_NEW(m_arena_allocator, CompoundNode)(token, m_arena_allocator);
							auto* x = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							x->value = c->value[0];
							auto* y = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							y->value = c->value[1];
							auto* z = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							z->value = c->value[2];
							node->elements.reserve(3);
							node->elements.push(x);
							node->elements.push(y);
							node->elements.push(z);
							return node;
						}
						case ValueType::FLOAT4: {
							auto* node = LUMIX_NEW(m_arena_allocator, CompoundNode)(token, m_arena_allocator);
							auto* x = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							x->value = c->value[0];
							auto* y = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							y->value = c->value[1];
							auto* z = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							z->value = c->value[2];
							auto* w = LUMIX_NEW(m_arena_allocator, LiteralNode)(token);
							w->value = c->value[3];
							node->elements.reserve(4);
							node->elements.push(x);
							node->elements.push(y);
							node->elements.push(z);
							node->elements.push(w);
							return node;
						}
					}
					ASSERT(false);
					return nullptr;
				}
				
				BlockNode* block = ctx.block;
				while (block) {
					for (const Local& local : block->locals) {
						if (equalStrings(local.name, token.value)) {
							auto* node = LUMIX_NEW(m_arena_allocator, VariableNode)(token);
							node->family = VariableFamily::LOCAL;
							node->block = block;
							node->index = i32(&local - block->locals.begin());
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
			case Token::NOT: {
				UnaryOperatorNode* node = LUMIX_NEW(m_arena_allocator, UnaryOperatorNode)(token);
				node->op = Operators::NOT;
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

		bool infer_type = false;
		if (peekToken().type == Token::COLON) {
			// let a : type ...
			if (!consume(Token::COLON)) return;
			local.type = parseType();
		}
		else if (peekToken().type == Token::EQUAL) {
			// let a = ...;
			infer_type = true;
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
		Node* value = expression(ctx, 0);
		if (!value) return;

		if (value->type == Node::COMPOUND && infer_type) {
			switch (((CompoundNode*)value)->elements.size()) {
				case 1: local.type = ValueType::FLOAT; break;
				case 2: local.type = ValueType::FLOAT2; break;
				case 3: local.type = ValueType::FLOAT3; break;
				case 4: local.type = ValueType::FLOAT4; break;
				default: ASSERT(false); break;
			}
		}

		auto* assign = LUMIX_NEW(m_arena_allocator, AssignNode)(equal_token);
		assign->right = value;
		auto* var_node = LUMIX_NEW(m_arena_allocator, VariableNode)(equal_token);
		var_node->family = VariableFamily::LOCAL;
		var_node->block = ctx.block;
		var_node->index = ctx.block->locals.size() - 1;
		assign->left = var_node;
		ctx.block->statements.push(assign);

		consume(Token::SEMICOLON);
	}

	Node* ifStatement(CompileContext& ctx) {
		IfNode* result = LUMIX_NEW(m_arena_allocator, IfNode)(peekToken());
		result->condition = expression(ctx, 0);
		if (!result->condition) return nullptr;

		result->true_block = block(ctx);
		if (!result->true_block) return nullptr;

		if (peekToken().type == Token::ELSE) {
			consumeToken();
			if (peekToken().type == Token::IF) {
				consumeToken();
				Node* nested_if = ifStatement(ctx);
				if (!nested_if) return nullptr;
				auto* false_blk = LUMIX_NEW(m_arena_allocator, BlockNode)(peekToken(), m_arena_allocator);
				false_blk->statements.push(nested_if);
				result->false_block = false_blk;
			}
			else {
				result->false_block = block(ctx);
				if (!result->false_block) return nullptr;
			}
		}
		return result;
	}

	static bool canMutate(Node* node) {
		if (node->type == Node::SWIZZLE) return canMutate(((SwizzleNode*)node)->left);
		if (node->type != Node::VARIABLE) return false;

		VariableNode* var_node = static_cast<VariableNode*>(node);
		if (var_node->family != VariableFamily::LOCAL && var_node->family != VariableFamily::OUTPUT && var_node->family != VariableFamily::CHANNEL) {
			return false;
		}
		return true;
	}

	Node* statement(CompileContext& ctx) {
		Token token = peekToken();
		switch (token.type) {
			case Token::IF: 
				consumeToken();
				return ifStatement(ctx);
			case Token::IDENTIFIER: {
				Node* lhs = atom(ctx);
				if (!lhs) return nullptr;

				Token op = peekToken();
				switch (op.type) {
					case Token::SEMICOLON: 
						consumeToken();
						return lhs;
					case Token::EQUAL: {
						if (!canMutate(lhs)) {
							error(lhs->token.value, "Cannot assign to this expression.");
							return nullptr;
						}
						consumeToken();
						Node* value = expression(ctx, 0);
						if (!value) return nullptr;
						auto* node = LUMIX_NEW(m_arena_allocator, AssignNode)(op);
						node->left = lhs;
						node->right = value;
						if (!consume(Token::SEMICOLON)) return nullptr;
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
				if (!consume(Token::SEMICOLON)) return nullptr;
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
				case Token::AND: case Token::OR:
				case Token::LT: case Token::GT:
				case Token::SLASH: case Token::STAR:
				case Token::MINUS: case Token::PLUS: {
					u32 prio = getPriority(op);
					if (prio <= min_priority) return lhs;
					consumeToken();
					Node* rhs = expression(ctx, prio);
					if (!rhs) return nullptr;
					BinaryOperatorNode* opnode = LUMIX_NEW(m_arena_allocator, BinaryOperatorNode)(op);
					if (op.type == Token::AND) opnode->op = Operators::AND;
					else if (op.type == Token::OR) opnode->op = Operators::OR;
					else opnode->op = (Operators)op.value[0];
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

	void parseImport() {
		StringView path;
		if (!consume(Token::STRING, path)) return;
		
		OutputMemoryStream& import_content = m_imports.emplace(m_allocator);
		if (!m_filesystem.getContentSync(Path(path), import_content)) {
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
				case Token::IMPORT: parseImport(); break;
				case Token::CONST: parseConst(); break;
				case Token::FN: parseFunction(); break;
				case Token::GLOBAL: variableDeclaration(m_globals); break;
				case Token::EMITTER: compileEmitter(); break;
				default: error(token.value, "Unexpected token ", token.value); return;
			}
		}
		end_import:
		
		m_tokenizer = tokenizer;
	}

	void parseConst() {
        Constant& c = m_constants.emplace();
		if (!consume(Token::IDENTIFIER, c.name)) return;
        if (!consume(Token::EQUAL)) return;
        
		CompileContext ctx(*this);
		ctx.entry_point = EntryPoint::GLOBAL;
		Node* n = expression(ctx, 0);
		if (!n) return;
		
		ASTEvaluator evaluator(*this, m_arena_allocator);
		evaluator.eval(n);

		if (evaluator.stack.empty()) {
			errorAtCurrent("Expected a constant.");
			return;
		}

		switch (evaluator.stack.size()) {
			case 0: 
				errorAtCurrent("Expected a constant.");
				return;
			case 4:
				c.value[0] = evaluator.stack[0];
				c.value[1] = evaluator.stack[1];
				c.value[2] = evaluator.stack[2];
				c.value[3] = evaluator.stack[3];
				c.type = ValueType::FLOAT4;
				break;
			case 3:
				c.value[0] = evaluator.stack[0];
				c.value[1] = evaluator.stack[1];
				c.value[2] = evaluator.stack[2];
				c.value[3] = evaluator.stack[2];
				c.type = ValueType::FLOAT3;
				break;
			case 2:
				c.value[0] = evaluator.stack[0];
				c.value[1] = evaluator.stack[1];
				c.value[2] = evaluator.stack[1];
				c.value[3] = evaluator.stack[1];
				c.type = ValueType::FLOAT2;
				break;
			case 1:
				c.value[0] = evaluator.stack[0];
				c.value[1] = evaluator.stack[0];
				c.value[2] = evaluator.stack[0];
				c.value[3] = evaluator.stack[0];
				c.type = ValueType::FLOAT;
				break;
			default:
				errorAtCurrent("Expected a constant.");
				return;
		}

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
					for (StringView a : fn.args) {
						if (a == t.value) {
							error(t.value, "Argument '", t.value, "' already exists.");
							break;
						}
					}
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
				case InstructionType::CMP_ELSE:
					forNumStreams(1, itype, ioffset);
					ip.skip(sizeof(u16) * 2); // blocks' sizes
					instruction_index = forEachDataStreamInBytecode(ip, f, instruction_index); // if subblock
					instruction_index = forEachDataStreamInBytecode(ip, f, instruction_index); // else subblock
					break;
				case InstructionType::CMP: 
					forNumStreams(1, itype, ioffset);
					ip.skip(sizeof(u16)); // block size
					instruction_index = forEachDataStreamInBytecode(ip, f, instruction_index); // if subblock
					break;
				case InstructionType::NOISE: forNumStreams(2, itype, ioffset); break;
				case InstructionType::MOV: forNumStreams(2, itype, ioffset); break;
				case InstructionType::SIN: forNumStreams(2, itype, ioffset); break;
				case InstructionType::COS: forNumStreams(2, itype, ioffset); break;
				case InstructionType::SQRT: forNumStreams(2, itype, ioffset); break;
				case InstructionType::NOT: forNumStreams(2, itype, ioffset); break;
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
				case InstructionType::KILL: break;
				case InstructionType::RAND: forNumStreams(1, itype, ioffset); ip.skip(sizeof(float) * 2); break;
				case InstructionType::EMIT:
						ip.skip(sizeof(u32)); // emitter index
						instruction_index = forEachDataStreamInBytecode(ip, f, instruction_index); // emit subroutine
						break;
				default:
					ASSERT(false);
					break;
			}
		}
	}

	// some instructions do jumps relative to them, let's compute the jump size after all optimizations are done
	void patchBlockSizes(OutputMemoryStream& bytecode) {
		InputMemoryStream ip(bytecode);
		forEachDataStreamInBytecode(ip, [&](const DataStream& s, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){
			if (arg_index != 0) return;
			switch (itype) {
				case InstructionType::CMP_ELSE: {
					u64 offset = position + sizeof(DataStream);
					
					InputMemoryStream inner((const u8*)ip.getData() + offset + sizeof(u16) * 2, ip.size() - offset - sizeof(u16) * 2);
					forEachDataStreamInBytecode(inner, [](const DataStream& s, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){});
					u64 true_size = inner.getPosition();
					forEachDataStreamInBytecode(inner, [](const DataStream& s, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){});
					u64 false_size = inner.getPosition() - true_size;
					*(u16*)(bytecode.getMutableData() + offset) = u16(true_size);
					*(u16*)(bytecode.getMutableData() + offset + 2) = u16(false_size);

					break;
				}
				case InstructionType::CMP: {
					u64 offset = position + sizeof(DataStream);
					
					InputMemoryStream inner((const u8*)ip.getData() + offset + sizeof(u16), ip.size() - offset - sizeof(u16));
					forEachDataStreamInBytecode(inner, [](const DataStream& s, i32 position, i32 arg_index, InstructionType itype, i32 ioffset, i32){});
					u16 size = (u16)inner.getPosition();
					
					memcpy(bytecode.getMutableData() + offset, &size, sizeof(size));
					break;
				}
				default: break;
			}
		});
	}

	struct IRNode {
		#ifdef LUMIX_DEBUG
			// so VS debugger can show use actual type
			virtual ~IRNode() {}
		#endif
		enum Type {
			OP, // mov, binary op or syscall
			IF,
			END,
		};

		IRNode(Type type, Node* ast) : type(type), ast(ast) {}

		Type type;
		IRNode* next = nullptr;
		IRNode* prev = nullptr;
		Node* ast = nullptr; 
	};

	struct IRValue {
		DataStream::Type type;
		u32 index;
		float value;

		bool operator==(const IRValue& rhs) const {
			if (type != rhs.type) return false;
			if (type == DataStream::LITERAL) return value == rhs.value;
			return index == rhs.index;
		}
	};

	struct IROp : IRNode {
		IROp(Node* ast, IAllocator& allocator) : IRNode(IRNode::OP, ast), args(allocator) {}
		InstructionType instruction;
		IRValue dst;
		StackArray<IRValue, 2> args;
	};

	struct IREnd : IRNode {
		IREnd(Node* ast) : IRNode(IRNode::END, ast) {}
		bool is_conditional = false;
	};

	struct IRIf : IRNode {
		IRIf(Node* ast) : IRNode(IRNode::IF, ast) {}
		IRValue condition;
		IREnd* true_end = nullptr;
		IREnd* false_end = nullptr;
	};

	struct IRContext {
		IRContext(Emitter& emitter, IAllocator& allocator)
			: stack(allocator)
			, emitter(emitter)
		{}

		StackArray<IRValue, 16> stack;
		i32 emitted_index = -1;
		IRNode* tail = nullptr;
		IRNode* head = nullptr;
		Emitter& emitter;
		struct Arg {
			i32 num;
			i32 offset;
		};
		Span<Arg> args;
		u32 register_allocator = 0;
		u32 num_immutables = 0;
		EntryPoint entry_point;
		
		IRValue& stackValue(i32 idx) {
			return stack[stack.size() + idx];
		}

		void push(IRNode* node) {
			if (tail) tail->next = node;
			node->prev = tail;
			if (!head) head = node;
			tail = node;
		}

		void popStack(u32 num) {
			for (u32 i = 0; i < num; ++i) stack.pop();
		}
	};
	
	IRNode* compileBytecode(CompileContext& ctx, IRNode* ir, OutputMemoryStream& compiled) {
		IRNode* node = ir;
		auto writeValue = [&](IRValue& val) {
			DataStream tmp;
			tmp.type = val.type;
			tmp.index = val.index;
			tmp.value = val.value;
			compiled.write(tmp);
		};
		while (node) {
			switch (node->type) {
				case IRNode::IF: {
					auto* n = (IRIf*)node;
					compiled.write(n->false_end ? InstructionType::CMP_ELSE : InstructionType::CMP);
					writeValue(n->condition);
					compiled.write(u16(0));
					if (n->false_end) compiled.write(u16(0));

					compileBytecode(ctx, n->next, compiled);
					node = n->true_end;
					if (n->false_end) {
						compileBytecode(ctx, node->next, compiled);
						node = n->false_end;
					}
					break;
				}
				case IRNode::END: {
					compiled.write(InstructionType::END);
					return node;
				}
				case IRNode::OP: {
					auto* n = (IROp*)node;
					compiled.write(n->instruction);
					if (n->dst.type != DataStream::NONE) writeValue(n->dst);
					if (n->instruction == InstructionType::RAND) { // TODO make this not special case
						for (IRValue& arg : n->args) {
							compiled.write(arg.value);
						}
					}
					else if (n->instruction == InstructionType::EMIT) { // TODO make this not special case
						compiled.write(u32(n->args[0].index));
						node = compileBytecode(ctx, n->next, compiled);
					}
					else {
						for (IRValue& arg : n->args) {
							writeValue(arg);
						}
					}
					break;
				}
			}
			node = node->next;
		}
		return nullptr;
	}

	void optimizeIR(IRContext& ctx) {
		reorderIR(ctx);
		fold(ctx);
		fold(ctx); // It's possible more passes would fold even more, but we do only 2 passes.
	}

	void printIR(StringView path, IRContext& ctx) {
		StaticString<4096> tmp;
		auto append = [&](const IRValue& val) {
			switch (val.type) {
				case DataStream::NONE:
				case DataStream::ERROR: tmp.append("##ERROR##"); break;
				case DataStream::LITERAL: tmp.append(val.value); break;
				case DataStream::CHANNEL: tmp.append("CH", val.index); break;
				case DataStream::GLOBAL: tmp.append("GLOB", val.index); break;
				case DataStream::OUT: tmp.append("OUT", val.index); break;
				case DataStream::REGISTER: tmp.append("R", val.index); break;
				case DataStream::SYSTEM_VALUE: tmp.append("SYS", val.index); break;
			}
			};
		IRNode* node = ctx.head;
		while (node) {
			switch (node->type) {
				case IRNode::OP: {
					auto* n = (IROp*)node;
					if (n->dst.type != DataStream::NONE) {
						append(n->dst);
						tmp.append(" = ");
					}

					auto appendArgs = [&](){
						for (IRValue& arg : n->args) {
							if (&arg != n->args.begin()) tmp.append(",");
							append(arg);
						}
						tmp.append(")");
					};
					
					switch (n->instruction) {
						case InstructionType::MUL: append(n->args[0]); tmp.append(" * "); append(n->args[1]); break;
						case InstructionType::ADD: append(n->args[0]); tmp.append(" + "); append(n->args[1]); break;
						case InstructionType::SUB: append(n->args[0]); tmp.append(" - "); append(n->args[1]); break;
						case InstructionType::DIV: append(n->args[0]); tmp.append(" / "); append(n->args[1]); break;
						case InstructionType::LT: append(n->args[0]); tmp.append(" < "); append(n->args[1]); break;
						case InstructionType::GT: append(n->args[0]); tmp.append(" > "); append(n->args[1]); break;
						case InstructionType::MOD: append(n->args[0]); tmp.append(" % "); append(n->args[1]); break;
						case InstructionType::COS: tmp.append("cos("); appendArgs(); break;
						case InstructionType::SIN: tmp.append("sin("); appendArgs(); break;
						case InstructionType::KILL: tmp.append("kill("); appendArgs(); break;
						case InstructionType::EMIT: tmp.append("emit(", n->args[0].index, ")"); break;
						case InstructionType::RAND: tmp.append("random("); appendArgs(); break;
						case InstructionType::MIN: tmp.append("min("); appendArgs(); break;
						case InstructionType::MAX: tmp.append("max("); appendArgs(); break;
						case InstructionType::NOISE: tmp.append("noise("); appendArgs(); break;
						case InstructionType::SQRT: tmp.append("sqrt("); appendArgs(); break;
						case InstructionType::MOV: append(n->args[0]); break;
						default: ASSERT(false); break;
					}
					break;
				}
				case IRNode::END:
					tmp.append("END");
					break;
				case IRNode::IF: {
					auto* n = (IRIf*)node;
					tmp.append("CMP ");
					append(n->condition);
					break;
				}
			}
			tmp.append("\n");
			node = node->next;
		}	
		logError(path, "\n\n");
		logError(tmp.data);
	}

	template <typename F>
	void forEachValue(IRNode* node, const F& fn) {
		u32 instruction_index = 0;
		while (node) {
			switch (node->type) {
				case IRNode::OP: {
					auto* n = (IROp*)node;
					if (n->dst.type != DataStream::NONE) fn(n->dst, true, instruction_index);
					for (IRValue& arg : n->args) {
						fn(arg, false, instruction_index);
					}
					break;
				}
				case IRNode::END: break;
				case IRNode::IF: {
					auto* n = (IRIf*)node;
					fn(n->condition, false, instruction_index);
					break;
				}
			}
			node = node->next;
			++instruction_index;
		}
	}

	// Optimizes register allocation by reusing registers whose lifetimes don't overlap.
	// Before this pass, each operation creates a unique register index. This pass remaps
	// those indices to minimize the total number of registers needed at runtime.
	u32 allocateRegisters(IRContext& ctx) {
		struct IRLifetime {
			u32 original_index;
			i32 from = -1;
			i32 to = -1;
			i32 remapped = -1;
		};

		StackArray<IRLifetime, 16> lifetimes(m_arena_allocator);
		HashMap<u32, i32> register_to_lifetime(m_allocator);

		// gather reads/writes
		forEachValue(ctx.head, [&](const IRValue& val, bool is_write, i32 instruction_index) {
			if (val.type == DataStream::REGISTER) {
				if (val.index < ctx.num_immutables) return;
				auto iter = register_to_lifetime.find(val.index);
				if (iter == register_to_lifetime.end()) {
					IRLifetime& lt = lifetimes.emplace();
					lt.original_index = val.index;
					lt.from = instruction_index;
					lt.to = instruction_index;
					register_to_lifetime.insert(val.index, lifetimes.size() - 1);
				} else {
					IRLifetime& lt = lifetimes[iter.value()];
					lt.to = maximum(lt.to, instruction_index);
				}
			}
		});

		u32 num_used_registers = 0;
		// assign remapped
		if (!lifetimes.empty()) {
			lifetimes[0].remapped = ctx.num_immutables;
			num_used_registers = 1;
		}
		for (i32 i = 1; i < lifetimes.size(); ++i) {
			IRLifetime& lt = lifetimes[i];
			lt.remapped = ctx.num_immutables;
			for (i32 j = 0; j < i; ++j) {
				const IRLifetime& prev = lifetimes[j];
				if (prev.remapped == lt.remapped && !(lt.to <= prev.from || prev.to <= lt.from)) {
					++lt.remapped;
					if (lt.remapped > 0xfe) {
						logError(m_path, ": Too many registers.");
						m_is_error = true;
						return 0;
					}
					j = -1; // restart check
					num_used_registers = maximum(num_used_registers, lt.remapped + 1);
				}
			}
		}

		// update indices
		forEachValue(ctx.head, [&](IRValue& val, bool, i32) {
			if (val.type != DataStream::REGISTER) return;
			if (val.index < ctx.num_immutables) return;
			auto iter = register_to_lifetime.find(val.index);
			val.index = lifetimes[iter.value()].remapped;
		});
		return num_used_registers;
	}

	static u32 getValues(IRNode& node, Span<IRValue> srcs, IRValue*& dst) {
		dst = nullptr;
		switch (node.type) {
			case IRNode::OP: {
				auto& n = (IROp&)node;
				dst = &n.dst;
				u32 i = 0;
				for (const IRValue& arg : n.args) {
					srcs[i] = arg;
					++i;
				}
				return i;
			}
			case IRNode::IF: {
				auto& n = (IRIf&)node;
				srcs[0] = n.condition;
				return 1;
			}
			case IRNode::END: return 0;
		}
		ASSERT(false);
		return 0;
	}

	enum class IRSwapResult {
		POSSIBLE,
		BLOCK,
		COLLISION
	};

	static IRSwapResult canSwap(IRNode& node, IRNode& node_dst) {
		// we can't reorder across blocks (e.g. from inside an if statement)
		switch (node_dst.type) {
			case IRNode::END: return IRSwapResult::BLOCK;
			case IRNode::IF: return IRSwapResult::BLOCK;
			case IRNode::OP: {
				auto& n = (IROp&)node_dst;
				if (n.instruction == InstructionType::EMIT) 
					return IRSwapResult::BLOCK;
				break;
			}
			default: break;
		}

		switch (node.type) {
			case IRNode::END: return IRSwapResult::BLOCK;
			case IRNode::IF: return IRSwapResult::BLOCK;
			case IRNode::OP: {
				auto& n = (IROp&)node;
				if (n.instruction == InstructionType::EMIT) 
					return IRSwapResult::BLOCK;
				break;
			}
			default: break;
		}

		IRValue values[9];
		IRValue* dst;
		IRValue prev_values[9];
		IRValue* prev_dst;
		u32 num_values = getValues(node, values, dst);
		u32 num_prev_values = getValues(node_dst, prev_values, prev_dst);

		// we can't reorder
		// a = x
		// a = y
		if (*dst == *prev_dst) return IRSwapResult::COLLISION;

		// we can't reorder
		// a = x
		// b = a
		if (dst) {
			for (u32 i = 0; i < num_prev_values; ++i) {
				if (*dst == prev_values[i]) return IRSwapResult::COLLISION;
			}
		}
		if (prev_dst) {
			for (u32 i = 0; i < num_values; ++i) {
				if (*prev_dst == values[i]) return IRSwapResult::COLLISION;
			}
		}
		return IRSwapResult::POSSIBLE;
	}

	// Reorders IR instructions to improve execution efficiency by moving instructions
	// earlier in the sequence when possible. This optimization reduces register pressure
	// and can enable better instruction scheduling by placing instructions closer to
	// where their results are first used.
	void reorderIR(IRContext& ctx) {
		if (!ctx.head) return;

		// we try to move nodes forward, otherwise conditions are moved far from ::CMP
		IRNode* node = ctx.tail;
		while (node) {
			IRNode* prev = node->prev;
			IRNode* dst = node->next;
			// to keep instructions roughly in the same order, we don't swap unless there's collision
			// this helps if we need to debug the compiler
			while (dst) {
				switch (canSwap(*node, *dst)) {
					case IRSwapResult::BLOCK: goto next;
					case IRSwapResult::POSSIBLE: break;
					case IRSwapResult::COLLISION: {
						if (dst->prev != node) {
							// Move node before dst (which is where it should stay)
							// Unlink node from its current position
							node->next->prev = node->prev;
							if (node->prev) node->prev->next = node->next;
							else ctx.head = node->next;

							// Insert node before dst
							node->next = dst;
							node->prev = dst->prev;
							
							dst->prev->next = node;
							dst->prev = node;
						}
						goto next;
					}
				}
				dst = dst->next;
			}
			next:
			node = prev;
		}
	}

	static void unlinkNode(IRContext& ctx, IRNode& node) {
		if (ctx.head == &node) ctx.head = ctx.head->next;
		if (ctx.tail == &node) ctx.tail = ctx.tail->prev;
		if (node.prev) node.prev->next = node.next;
		if (node.next) node.next->prev = node.prev;
	}

	struct RegisterAccess {
		u32 reads = 0;
		u32 writes = 0;
		IRNode* prev_writer = nullptr;
		IRValue alias;
		bool is_aliased = false;
		IREnd* alias_branch = nullptr;
	};

	void fold(IRContext& ctx) {
		if (!ctx.head) return;

		StackArray<RegisterAccess, 16> register_access(m_arena_allocator);
		register_access.reserve(ctx.register_allocator);
		
		auto set = [&register_access](bool is_write, const IRValue& val) {
			if (val.type != DataStream::REGISTER) return;
			if (val.index >= (u32)register_access.size()) register_access.resize(val.index + 1);
			if (is_write) {
				++register_access[val.index].writes;
			}
			else {
				++register_access[val.index].reads;
			}
		};

		IRNode* node = ctx.head;
		while (node) {
			IRValue srcs[9];
			IRValue* dst;
			u32 num_srcs = getValues(*node, srcs, dst); 
			if (dst) {
				set(true, *dst);
			}
			for (u32 i = 0; i < num_srcs; ++i) {
				set(false, srcs[i]);
			}
			node = node->next;
		}

		auto foldNode = [&](IRNode* node, const IRValue& dst, const IRValue& src) {
			if (dst.type == DataStream::REGISTER 
				&& register_access[dst.index].writes == 1 
				&& register_access[dst.index].reads == 1
			) {
				register_access[dst.index].alias = src;
				register_access[dst.index].is_aliased = true;

				unlinkNode(ctx, *node);
				return;
			}

			// replace with mov
			auto* mov = LUMIX_NEW(m_arena_allocator, IROp)(node->ast, m_arena_allocator);
			mov->instruction = InstructionType::MOV;
			mov->dst = dst;
			mov->args.push(src);
			mov->prev = node->prev;
			mov->next = node->next;
			unlinkNode(ctx, *node);
		};

		node = ctx.head;
		StackArray<IREnd*, 4> branch_stack(m_arena_allocator);
		while (node) {
			switch (node->type) {
				case IRNode::OP: {
					auto* n = (IROp*)node;

					// fold all arguments first
					bool all_args_literals = true;
					for (IRValue& arg : n->args) {
						if (arg.type == DataStream::REGISTER && register_access[arg.index].is_aliased) {
							arg = register_access[arg.index].alias;
						}
						if (arg.type != DataStream::LITERAL) all_args_literals = false;
					}

					// fold operations like
					// a = b * 1
					// a = b * 0
					// a = b - 0
					if (n->args.size() == 2) {
						if (n->args[0].type == DataStream::LITERAL) {
							if ((n->instruction == InstructionType::ADD && n->args[0].value == 0)
								|| (n->instruction == InstructionType::MUL && n->args[0].value == 1)
								)
							{
								foldNode(n, n->dst, n->args[1]);
								break;
							}
							if (n->instruction == InstructionType::MUL && n->args[0].value == 0) {
								foldNode(n, n->dst, n->args[0]);
								break;
							}
						}

						if (n->args[1].type == DataStream::LITERAL) {
							if ((n->instruction == InstructionType::ADD && n->args[1].value == 0)
								|| (n->instruction == InstructionType::SUB && n->args[1].value == 0)
								|| (n->instruction == InstructionType::MUL && n->args[1].value == 1)
								|| (n->instruction == InstructionType::DIV && n->args[1].value == 1)
								)
							{
								foldNode(n, n->dst, n->args[0]);
								break;
							}
							if (n->instruction == InstructionType::MUL && n->args[1].value == 0) {
								foldNode(n, n->dst, n->args[1]);
								break;
							}
						}
					}

					// we write into a register thah is never read, remove the operation
					if (n->dst.type == DataStream::REGISTER && register_access[n->dst.index].reads == 0) {
						unlinkNode(ctx, *n);
						break;
					}

					// fold 
					// op regN a b
					// mov dst regN
					// into
					// op dst a b
					// if it's safe to do
					if (n->instruction == InstructionType::MOV
						&& n->args[0].type == DataStream::REGISTER 
						&& n->dst.type == DataStream::REGISTER
						&& register_access[n->args[0].index].reads == 1
						&& register_access[n->args[0].index].writes == 1
						&& register_access[n->args[0].index].prev_writer
					) {
						IRNode* prev_writer = register_access[n->args[0].index].prev_writer;
						if (prev_writer->type == IRNode::OP) {
							auto* src = (IROp*)prev_writer;
							src->dst = n->dst;
							if (n->dst.type == DataStream::REGISTER) register_access[n->dst.index].prev_writer = prev_writer;
							if (n->prev) n->prev->next = n->next;
							if (n->next) n->next->prev = n->prev;
							if (ctx.tail == n) ctx.tail = n->prev;
							break;
						}
					}

					// fold the operation if all arguments are runtime constant
					// some operations can not be fold like this, e.g. random(...)
					if (n->dst.type != DataStream::NONE
						&& n->args.size() > 0
						&& all_args_literals
						&& n->dst.type == DataStream::REGISTER
					) {
						float first = n->args[0].value;
						switch (n->instruction) {
							case InstructionType::SQRT:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = sqrtf(n->args[0].value);
								break;
							case InstructionType::COS:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = cosf(n->args[0].value);
								break;
							case InstructionType::SIN:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = sinf(n->args[0].value);
								break;
							case InstructionType::ADD:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first + n->args[1].value;
								break;
							case InstructionType::MUL:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first * n->args[1].value;
								break;
							case InstructionType::DIV:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first / n->args[1].value;
								break;
							case InstructionType::SUB:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first - n->args[1].value;
								break;
							case InstructionType::MOD:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = fmodf(first, n->args[1].value);
								break;
							case InstructionType::LT:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first < n->args[1].value ? 1.f : 0.f;
								break;
							case InstructionType::GT:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first > n->args[1].value ? 1.f : 0.f;
								break;
							case InstructionType::MOV:
								register_access[n->dst.index].is_aliased = true;
								register_access[n->dst.index].alias.value = first;
								break;
							default: break;
						}

						if (register_access[n->dst.index].is_aliased) {
							register_access[n->dst.index].alias.type = DataStream::LITERAL;
							register_access[n->dst.index].alias_branch = branch_stack.empty() ? nullptr : branch_stack.last();
							register_access[n->dst.index].prev_writer = n;
							if (register_access[n->dst.index].writes == 1) {
								unlinkNode(ctx, *n);
							}
							break;
						}
					}

					if (n->instruction == InstructionType::MOV && n->prev && n->prev->type == IRNode::OP) {
						IROp* prev_op = (IROp*)n->prev;
						if (prev_op->instruction == InstructionType::MOV && n->dst == prev_op->dst) {
							// consecutive movs to the same destination, remove the previous mov
							unlinkNode(ctx, *prev_op);
						}
					}

					if (n->dst.type == DataStream::REGISTER) register_access[n->dst.index].prev_writer = n;
					break;
				}
				case IRNode::END: {
					auto* n = (IREnd*)node;
					if (n->is_conditional) {
						for (auto& r : register_access) {
							if (r.alias_branch == n) {
								r.is_aliased = false;
							}
						}
						ASSERT(branch_stack.last() == n);
						branch_stack.pop();
					}
					break;
				}
				case IRNode::IF: {
					auto* n = (IRIf*)node;
					if (n->condition.type == DataStream::REGISTER && register_access[n->condition.index].is_aliased) {
						n->condition = register_access[n->condition.index].alias;
					}
					if (n->condition.type == DataStream::LITERAL) {
						if (n->condition.value == 0.f) {
							// take false branch
							n->next = n->true_end->next;
							if (n->next) n->next->prev = n;
							unlinkNode(ctx, *n);
							if (n->false_end) unlinkNode(ctx, *n->false_end);
							node = n->true_end;
						}
						else {
							// take true branch
							unlinkNode(ctx, *n);
							if (n->false_end) {
								if (ctx.tail == n->false_end) ctx.tail = n->true_end;
								n->true_end->next = n->false_end->next;
								if (n->true_end->next) n->true_end->next->prev = n->true_end;
							}
							unlinkNode(ctx, *n->true_end);
						}
					}
					else {
						if (n->false_end) branch_stack.push(n->false_end);
						branch_stack.push(n->true_end);
					}

					break;
				}
			}
			node = node->next;
		}
	}

	static const char* toString(EntryPoint ep) {
		switch (ep) {
			case EntryPoint::EMIT: return "emit";
			case EntryPoint::OUTPUT: return "output";
			case EntryPoint::UPDATE: return "update";
			case EntryPoint::GLOBAL: return "global";
		}
		ASSERT(false);
		return "unknown";
	}

	i32 compileIR(IRContext& ctx, Node* node) {
		switch (node->type) {
			case Node::EMITTER_REF: {
				auto* n = (EmitterRefNode*)node;
				ctx.stack.emplace().index = n->index;
				return 1;
			}
			case Node::COMPOUND: {
				auto* n = (CompoundNode*)node;
				i32 num = 0;
				for (u32 i = 0, c = n->elements.size(); i < c; ++i) {
					i32 r = compileIR(ctx, n->elements[i]);
					if (r < 0) return -1;
					ASSERT(r != 0); // TODO can this happen?
					num += r;
				}
				ASSERT(num <= 4); // TODO make sure this is handled in parser 
				return num;
			}
			case Node::SYSCALL: {
				auto* n = (SysCallNode*)node;
				if ((n->function.valid_entry_points & (1 << (u32)ctx.entry_point)) == 0) {
					error(node->token.value, n->token.value, " can not be called in context of ", toString(ctx.entry_point));
					return -1;
				}
				auto* res = LUMIX_NEW(m_arena_allocator, IROp)(node, m_arena_allocator);
				res->instruction = n->function.instruction;
				res->args.reserve(n->args.size());
				for (Node* arg : n->args) {
					i32 a = compileIR(ctx, arg);
					switch (a) {
						case -1: return -1;
						case 1: break;
						default:
							error(node->token.value, "Arguments must be scalars.");
							return -1;
					}
					res->args.push(ctx.stack.last());
					ctx.stack.pop();
				}
				if (n->function.returns_value) {
					res->dst.type = DataStream::REGISTER;
					res->dst.index = ++ctx.register_allocator;
					ctx.stack.push(res->dst);
				}
				ctx.push(res);
				if (n->after_block) {
					ASSERT(n->function.instruction == InstructionType::EMIT);
					ctx.emitted_index = res->args[0].index;
					i32 a = compileIR(ctx, n->after_block);
					if (a < 0) return -1;
					// TODO a > 0

					ctx.emitted_index = -1;
					auto* end = LUMIX_NEW(m_arena_allocator, IREnd)(n->after_block);
					ctx.push(end);
				}
				else if (n->function.instruction == InstructionType::EMIT) {
					auto* end = LUMIX_NEW(m_arena_allocator, IREnd)(n);
					ctx.push(end);
				}
				return n->function.returns_value ? 1 : 0;
			}
			case Node::SWIZZLE: {
				auto* n = (SwizzleNode*)node;
				i32 left = compileIR(ctx, n->left);
				if (left < 0) return -1;

				StringView swizzle = n->token.value;
				IRValue swizzled[4];
				for (u32 i = 0; i < swizzle.size(); ++i) {
					i32 idx = -1;
					switch (swizzle[i]) {
						case 'x': case 'r': idx = 0; break;
						case 'y': case 'g': idx = 1; break;
						case 'z': case 'b': idx = 2; break;
						case 'w': case 'a': idx = 3; break;
						default: ASSERT(false); return -1;
					}
					if (idx >= left) {
						error(n->token.value, "Invalid swizzle component.");
						return -1;
					}
					swizzled[i] = ctx.stackValue(-left + idx);
				}
				ctx.popStack(left);
				for (u32 i = 0; i < swizzle.size(); ++i) ctx.stack.push(swizzled[i]);
				return swizzle.size();
			}
			case Node::SYSTEM_VALUE: {
				auto* n = (SystemValueNode*)node;
				IRValue& val = ctx.stack.emplace();
				val.type = DataStream::SYSTEM_VALUE;
				val.index = (u8)n->value;
				return 1;
			}
			case Node::FUNCTION_ARG: {
				auto* n = (FunctionArgNode*)node;
				IRContext::Arg arg = ctx.args[n->index];
				for (i32 i = 0; i < arg.num; ++i) {
					IRValue val = ctx.stack[ctx.args[n->index].offset + i];
					ctx.stack.push(val);
				}
				return arg.num;
			}
			case Node::FUNCTION_CALL: {
				auto* n = (FunctionCallNode*)node;
				Function& fn = m_functions[n->function_index];
				if (fn.is_inlining) {
					error(n->token.value, fn.name, " is called recursively. Recursion is not supported.");
					return -1;
				}
				IRContext::Arg args[8];
				if ((u32)n->args.size() > lengthOf(args)) {
					error(n->token.value, "Too many arguments.");
					return -1;
				}
				u32 arg_offset = ctx.stack.size();
				u32 args_size = 0;
				for (i32 i = 0; i < n->args.size(); ++i) {
					args[i].offset = arg_offset;
					args[i].num = compileIR(ctx, n->args[i]);
					args_size += args[i].num;
					if (args[i].num < 0) return -1;
					arg_offset = ctx.stack.size();
				}
				Span<IRContext::Arg> prev_args = ctx.args;
				ctx.args = { args, &args[n->args.size()] };
				fn.is_inlining = true;
				i32 ret = compileIR(ctx, fn.block);
				if (ret < 0) return ret;
				fn.is_inlining = false;
				ctx.args = prev_args;
				IRValue ret_vals[4];
				for (i32 i = 0; i < ret; ++i) {
					ret_vals[i] = ctx.stack.last();
					ctx.stack.pop();
				}
				ctx.popStack(args_size);
				for (i32 i = 0; i < ret; ++i) {
					ctx.stack.push(ret_vals[ret - i - 1]);
				}
				return ret;
			}
			case Node::VARIABLE: {
				auto* n = (VariableNode*)node;
				switch (n->family) {
					case VariableFamily::LOCAL: {
						Local& l = n->block->locals[n->index];
						u32 num = toCount(l.type);
						for (u32 i = 0; i < num; ++i) {
							IRValue& val = ctx.stack.emplace();
							val.type = DataStream::REGISTER;
							if (l.registers[i] < 0) {
								l.registers[i] = ++ctx.register_allocator;
							}
							val.index = l.registers[i];
						}
						return num;
					}
					case VariableFamily::INPUT: {
						if (ctx.entry_point != EntryPoint::EMIT) {
							error(node->token.value, "Can not access input variables outside of emit()");
							return -1;
						}
						u32 num;
						if (ctx.emitted_index >= 0) {
							Variable& v = m_emitters[ctx.emitted_index].m_inputs[n->index];
							num = toCount(v.type);
							for (u32 i = 0; i < num; ++i) {
								IRValue& val = ctx.stack.emplace();
								val.type = DataStream::OUT;
								val.index = v.getOffsetSub(i);
							}
						}
						else {
							Variable& v = ctx.emitter.m_inputs[n->index];
							num = toCount(v.type);
							for (u32 i = 0; i < num; ++i) {
								IRValue& val = ctx.stack.emplace();
								val.type = DataStream::REGISTER;
								val.index = v.getOffsetSub(i);
							}
						}
						return num;
					}
					case VariableFamily::CHANNEL: {
						Variable& v = ctx.emitter.m_vars[n->index];
						u32 num = toCount(v.type);
						for (u32 i = 0; i < num; ++i) {
							IRValue& val = ctx.stack.emplace();
							val.type = DataStream::CHANNEL;
							val.index = v.getOffsetSub(i);
						}
						return num;
					}
					case VariableFamily::GLOBAL: {
						const Variable& var = m_globals[n->index];
						u32 num = toCount(var.type);
						for (u32 i = 0; i < num; ++i) {
							IRValue& val = ctx.stack.emplace();
							val.type = DataStream::GLOBAL;
							val.index = var.getOffsetSub(i);
						}
						return num;
					}
					case VariableFamily::OUTPUT: {
						if (ctx.entry_point != EntryPoint::OUTPUT) {
							error(node->token.value, "Can not access output variables outside of output()");
							return -1;
						}
						Variable& v = ctx.emitter.m_outputs[n->index];
						u32 num = toCount(v.type);
						for (u32 i = 0; i < num; ++i) {
							IRValue& val = ctx.stack.emplace();
							val.type = DataStream::OUT;
							val.index = v.getOffsetSub(i);
						}
						return num;
					}
					default: break;
				}
				ASSERT(false);
				return -1;
			}
			case Node::UNARY_OPERATOR: {
				auto* n = (UnaryOperatorNode*)node;
				if (n->op == Operators::SUB) {
					i32 right = compileIR(ctx, n->right);
					if (right < 0) return -1;
					i32 num = right;
					IROp* ir_ops[4];
					for (i32 i = 0; i < num; ++i) {
						auto* res = LUMIX_NEW(m_arena_allocator, IROp)(node, m_arena_allocator);
						res->instruction = InstructionType::MUL;
						res->dst.type = DataStream::REGISTER;
						res->dst.index = ++ctx.register_allocator;
						res->args.push(ctx.stackValue(-num + i));
						IRValue minus_one;
						minus_one.type = DataStream::LITERAL;
						minus_one.value = -1.0f;
						res->args.push(minus_one);
						ir_ops[i] = res;
						ctx.push(res);
					}
					for (i32 i = 0; i < num; ++i) {
						ctx.stackValue(-num + i) = ir_ops[i]->dst;
					}
					return num;
				} else if (n->op == Operators::NOT) {
					i32 right = compileIR(ctx, n->right);
					if (right < 0) return -1;
					i32 num = right;
					IROp* ir_ops[4];
					for (i32 i = 0; i < num; ++i) {
						auto* res = LUMIX_NEW(m_arena_allocator, IROp)(node, m_arena_allocator);
						res->instruction = InstructionType::NOT;
						res->dst.type = DataStream::REGISTER;
						res->dst.index = ++ctx.register_allocator;
						res->args.push(ctx.stackValue(-num + i));
						ir_ops[i] = res;
						ctx.push(res);
					}
					for (i32 i = 0; i < num; ++i) {
						ctx.stackValue(-num + i) = ir_ops[i]->dst;
					}
					return num;
				} else {
					ASSERT(false);
					return -1;
				}
			}
			case Node::BINARY_OPERATOR: {
				auto* n = (BinaryOperatorNode*)node;
				i32 left = compileIR(ctx, n->left);
				i32 right = compileIR(ctx, n->right);
				if (left < 0 || right < 0) return -1;
				if (right != left && right != 1 && left != 1) {
					error(node->token.value, "Type mismatch.");
					return -1;
				}
				i32 num = maximum(left, right);
				IROp* ir_ops[4];
				for (i32 i = 0; i < num; ++i) {
					auto* res = LUMIX_NEW(m_arena_allocator, IROp)(n, m_arena_allocator);
					res->instruction = toInstruction(n->op);
					res->dst.type = DataStream::REGISTER;
					res->args.push(ctx.stackValue(-right - left + (left == 1 ? 0 : i)));
					res->args.push(ctx.stackValue(-right + (right == 1 ? 0 : i)));
					ir_ops[i] = res;
				}
				ctx.popStack(left + right);
				for (i32 i = 0; i < left; ++i) {
					ir_ops[i]->dst.index = ++ctx.register_allocator;
					ctx.stack.push(ir_ops[i]->dst);
					ctx.push(ir_ops[i]);
				}
				return num;
			}
			case Node::IF: {
				auto* n = (IfNode*)node;
				i32 cond = compileIR(ctx, n->condition);
				if (cond < 0) return -1;
				auto* res = LUMIX_NEW(m_arena_allocator, IRIf)(n);
				ctx.push(res);
				if (cond < 0) return -1;
				if (cond > 1) {
					error(n->token.value, "Condition must be scalar.");
					return -1;
				}
				res->condition = ctx.stack.last();
				ctx.stack.pop();
				
				i32 t = compileIR(ctx, n->true_block);
				if (t < 0) return -1; // TODO t > 0

				res->true_end = LUMIX_NEW(m_arena_allocator, IREnd)(n->true_block);
				res->true_end->is_conditional = true;
				ctx.push(res->true_end);

				if (n->false_block) {
					i32 f = compileIR(ctx, n->false_block);
					if (f < 0) return -1; // TODO f > 0

					res->false_end = LUMIX_NEW(m_arena_allocator, IREnd)(n->false_block);
					res->false_end->is_conditional = true;
					ctx.push(res->false_end);
				}
				return 0;
			}

			case Node::RETURN: {
				auto* n = (ReturnNode*)node;
				return compileIR(ctx, n->value);
			}
			case Node::LITERAL: {
				IRValue& val = ctx.stack.emplace();
				val.type = DataStream::LITERAL;
				val.value = ((LiteralNode*)node)->value;
				return 1;
			}
			case Node::BLOCK: {
				auto* n = (BlockNode*)node;
				u32 size_locals = 0;
				for (Node* statement : n->statements) {
					i32 s = compileIR(ctx, statement);
					if (s < 0) return -1;
					if (s > 0) {
						ctx.popStack(size_locals);
						for (auto& local : n->locals) {
							local.registers[0] = -1;
							local.registers[1] = -1;
							local.registers[2] = -1;
							local.registers[3] = -1;
						}
						return s; // TODO make sure this is from "return ...;"
					}
				}
				return 0;
			}
			case Node::ASSIGN: {
				auto* n = (AssignNode*)node;
				i32 left = compileIR(ctx, n->left);
				i32 right = compileIR(ctx, n->right);
				if (left < 0 || right < 0) return -1;
				if ((right < left && right != 1) || right > left) {
					error(node->token.value, "Type mismatch.");
					return -1;
				}

				IROp* movs[4];
				for (i32 i = 0; i < left; ++i) {
					auto* res = LUMIX_NEW(m_arena_allocator, IROp)(n, m_arena_allocator);
					res->instruction = InstructionType::MOV;
					res->dst = ctx.stackValue(-left - right + i);
					res->args.push(ctx.stackValue(-right + (right == 1 ? 0 : i)));
					movs[i] = res;
				}
				ctx.popStack(left + right);
				for (i32 i = 0; i < left; ++i) ctx.push(movs[i]);
				return 0;
			}
			default: break;
		}
		ASSERT(false);
		return -1;
	}

	void compileFunction(Emitter& emitter) {
		StringView fn_name;
		if (!consume(Token::IDENTIFIER, fn_name)) return;
		if (!consume(Token::LEFT_PAREN)) return;
		if (!consume(Token::RIGHT_PAREN)) return;
	
		CompileContext ctx(*this);
		ctx.emitter = &emitter;
		IRContext irctx(emitter, m_arena_allocator);

		u32 num_immutables = 0;
		OutputMemoryStream& compiled = [&]() -> OutputMemoryStream& {
			if (equalStrings(fn_name, "update")) {
				ctx.entry_point = EntryPoint::UPDATE;
				return emitter.m_update;
			}
			if (equalStrings(fn_name, "emit")) {
				ctx.entry_point = EntryPoint::EMIT;
				for (const Variable& v : emitter.m_inputs) {
					switch (v.type) {
						case ValueType::VOID: ASSERT(false); break;
						case ValueType::FLOAT: ++num_immutables; break;
						case ValueType::FLOAT2: num_immutables += 2; break;
						case ValueType::FLOAT3: num_immutables += 3; break;
						case ValueType::FLOAT4: num_immutables += 4; break;
					}
				}
				return emitter.m_emit;
			}
			if (equalStrings(fn_name, "output")) {
				ctx.entry_point = EntryPoint::OUTPUT;
				return emitter.m_output;
			}
			error(fn_name, "Unknown function");
			return emitter.m_output;
		}();
		if (m_is_error) return;

		irctx.entry_point = ctx.entry_point;
		irctx.register_allocator = num_immutables;
		irctx.num_immutables = num_immutables;

		BlockNode* b = block(ctx);
		if (!b || m_is_error) return;
		compileIR(irctx, b);
		if (m_is_error) return;
		optimizeIR(irctx);
		u32 num_used_registers = allocateRegisters(irctx);
		//printIR(fn_name, irctx);
		compileBytecode(ctx, irctx.head, compiled);
		compiled.write(InstructionType::END);
		patchBlockSizes(compiled);
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
		else {
			emitter.m_num_output_registers = num_used_registers;
			emitter.m_num_output_instructions = max_instruction_index + 1;
		}
	}

	DataStream pushStack(CompileContext& ctx) {
		DataStream res;
		res.type = DataStream::REGISTER;
		res.index = ctx.value_counter;
		++ctx.value_counter;
		ASSERT(ctx.value_counter < 255);
		return res;
	}

	InstructionType toInstruction(Operators op) {
		switch (op) {
			case Operators::MOD: return InstructionType::MOD;
			case Operators::ADD: return InstructionType::ADD;
			case Operators::SUB: return InstructionType::SUB;
			case Operators::MUL: return InstructionType::MUL;
			case Operators::DIV: return InstructionType::DIV;
			case Operators::LT: return InstructionType::LT;
			case Operators::GT: return InstructionType::GT;
			case Operators::AND: return InstructionType::AND;
			case Operators::OR: return InstructionType::OR;
			case Operators::NOT: return InstructionType::NOT;
		}
		ASSERT(false);
		return InstructionType::END;
	}

	void parseFunction() {
		Function& fn = m_functions.emplace(m_allocator);
		if (!consume(Token::IDENTIFIER, fn.name)) return;
		parseArgs(fn);
		
		CompileContext ctx(*this);
		ctx.entry_point = EntryPoint::GLOBAL;
		ctx.function = &fn;
		fn.block = block(ctx);
		if (!fn.block) return;

		u32 count = 0;
		for (const Function& f : m_functions) {
			if (f.name == fn.name) ++count;
		}
		if (count > 1) {
			error(fn.name, "Function '", fn.name, "' already exists.");
		}
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
					if (emitter.m_material.isEmpty() && emitter.m_mesh.isEmpty()) {
						error(emitter.m_name, "Either material or mesh must be provided.");
					}
					return;
				case Token::IDENTIFIER:
					if (equalStrings(token.value, "material")) compileMaterial(emitter);
					else if (equalStrings(token.value, "mesh")) compileMesh(emitter);
					else if (equalStrings(token.value, "emit_move_distance")) emitter.m_emit_move_distance = consumeFloat();
					else if (equalStrings(token.value, "init_emit_count")) emitter.m_init_emit_count = consumeU32();
					else if (equalStrings(token.value, "emit_per_second")) emitter.m_emit_per_second = consumeFloat();
					else if (equalStrings(token.value, "max_ribbons")) emitter.m_max_ribbons = consumeU32();
					else if (equalStrings(token.value, "max_ribbon_length")) emitter.m_max_ribbon_length = consumeU32();
					else if (equalStrings(token.value, "init_ribbons_count")) emitter.m_init_ribbons_count = consumeU32();
					else if (equalStrings(token.value, "tube_segments")) emitter.m_tube_segments = consumeU32();
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
				case ValueType::FLOAT2: 
					decl.addAttribute(offset, 2, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec2);
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
				case Token::IMPORT: parseImport(); break;
				case Token::CONST: parseConst(); break;
				case Token::FN: parseFunction(); break;
				case Token::GLOBAL: variableDeclaration(m_globals); break;
				case Token::EMITTER: compileEmitter(); break;
				default: error(token.value, "Unexpected token ", token.value); return false;
			}
		}

		write_label:
		ParticleSystemResource::Header header;
		output.write(header);

		output.write(m_emitters.size());
		auto getCount = [](const auto& x){
			u32 c = 0;
			for (const auto& i : x) {
				switch (i.type) {
					case ValueType::FLOAT4: c+= 4; break;
					case ValueType::FLOAT3: c+= 3; break;
					case ValueType::FLOAT2: c+= 2; break;
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
			output.write(emitter.m_tube_segments);
			output.write(emitter.m_emit_move_distance);
		}
		output.write(m_globals.size());
		for (const Variable& p : m_globals) {
			output.writeString(p.name);
			switch (p.type) {
				case ValueType::VOID: ASSERT(false); break;
				case ValueType::FLOAT: output.write(u32(1)); break;
				case ValueType::FLOAT2: output.write(u32(2)); break;
				case ValueType::FLOAT3: output.write(u32(3)); break;
				case ValueType::FLOAT4: output.write(u32(4)); break;
			}
		}
		
		return !m_is_error;
	}

	FileSystem& m_filesystem;
	IAllocator& m_allocator;
	ArenaAllocator m_arena_allocator;
	Path m_path;
	bool m_is_error = false;
	bool m_suppress_logging = false;
	ParticleScriptTokenizer m_tokenizer;
	Array<Constant> m_constants;
	Array<Function> m_functions;
	Array<Variable> m_globals;
	Array<Emitter> m_emitters;
};


}