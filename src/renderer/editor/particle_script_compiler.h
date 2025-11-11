#pragma once

#include "core/log.h"
#include "core/arena_allocator.h"
#include "core/string.h"
#include "renderer/particle_system.h"

namespace Lumix {

struct ParticleScriptToken {
	enum Type {
		EOF, ERROR, SEMICOLON, COMMA, COLON, DOT,
		LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
		STAR, SLASH, MINUS, PLUS, EQUAL, PERCENT, GT, LT,
		NUMBER, STRING, IDENTIFIER,

		// keywords
		CONST, PARAM, EMITTER, FN, WORLD_SPACE, VAR, OUT, IN, LET, RETURN,
	};

	Type type;
	StringView value;
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
			case 'i': return checkKeyword("n", 1, 1, Token::IN);
			case 'l': return checkKeyword("et", 1, 2, Token::LET);
			case 'o': return checkKeyword("ut", 1, 2, Token::OUT);
			case 'p': return checkKeyword("aram", 1, 4, Token::PARAM);
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

    enum class Type {
        FLOAT,
        FLOAT3,
        FLOAT4
    };

	struct BuiltinFunction {
        InstructionType instruction;
        bool returns_value;
		u32 num_args;
    };

    struct Constant {
        StringView name;
        Type type;
        float value[4];
    };

	struct Node {
		enum Type {
			UNARY_OPERATOR,
			BINARY_OPERATOR,
			LITERAL,
			RETURN,
			PARAM,
			FUNCTION_ARG,
			ASSIGN,
			OUTPUT_VAR,
			INPUT_VAR,
			VAR,
			SWIZZLE,
			SYSCALL,
			SYSTEM_VALUE,
			COMPOUND,
			EMITTER_REF,
			BLOCK
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
		BlockNode(Token token, IAllocator& allocator) : Node(Node::BLOCK, token), statements(allocator) {}
		Array<Node*> statements;
	};

	struct SysCallNode : Node {
		SysCallNode(Token token, IAllocator& allocator) : Node(Node::SYSCALL, token), args(allocator) {}
		BuiltinFunction function;
		Array<Node*> args;
		Node* after_block = nullptr;
	};

	struct ReturnNode : Node {
		ReturnNode(Token token) : Node(Node::RETURN, token) {}
		Node* value;
	};

	struct EmitterRefNode : Node {
		EmitterRefNode(Token token) : Node(Node::EMITTER_REF, token) {}
		i32 index;
	};

	struct ParamNode : Node {
		ParamNode(Token token) : Node(Node::PARAM, token) {}
		i32 index;
	};

	struct OutputVarNode : Node {
		OutputVarNode(Token token) : Node(Node::OUTPUT_VAR, token) {}
		i32 index;
	};

	struct InputVarNode : Node {
		InputVarNode(Token token) : Node(Node::INPUT_VAR, token) {}
		i32 index;
	};

	struct VarNode : Node {
		VarNode(Token token) : Node(Node::VAR, token) {}
		i32 index;
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

	struct Function {
		Function(IAllocator& allocator) : args(allocator), statements(allocator) {}
		StringView name;
		Array<StringView> args;
		Array<Node*> statements;
	};

	struct Variable {
		StringView name;
		Type type;
		i32 offset = 0;

        i32 getOffsetSub(u32 sub) const {
            switch (type) {
                case Type::FLOAT: return offset;
                case Type::FLOAT3: return offset + minimum(sub, 2);
                case Type::FLOAT4: return offset + minimum(sub, 3);
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
		Array<Variable> m_vars;
		Array<Variable> m_outputs;
		Array<Variable> m_inputs;
		u32 m_num_used_registers = 0;
		u32 m_init_emit_count = 0;
		float m_emit_per_second = 0;
		u32 m_max_ribbons = 0;
		u32 m_max_ribbon_length = 0;
		u32 m_init_ribbons_count = 0;
	};
	
	struct CompileContext {
		const Function* function = nullptr;
		Emitter* emitter = nullptr;
		Emitter* emitted = nullptr;
		Span<DataStream> args = {};

		u32 register_allocator = 0;
	};

	ParticleScriptCompiler(IAllocator& allocator)
		: m_allocator(allocator)
		, m_emitters(allocator)
		, m_constants(allocator)
		, m_params(allocator)
		, m_functions(allocator)
		//, m_locals(allocator)
		, m_arena_allocator(1024 * 1024 * 256, m_allocator, "particle_script_compiler")
	{
	}

	~ParticleScriptCompiler() {
		m_arena_allocator.reset();
	}

	Type parseType() {
		StringView type;
		if (!consume(Token::IDENTIFIER, type)) return Type::FLOAT;
		if (equalStrings(type, "float")) return Type::FLOAT;
		if (equalStrings(type, "float3")) return Type::FLOAT3;
		if (equalStrings(type, "float4")) return Type::FLOAT4;
		error(type, "Unknown type");
		return Type::FLOAT;
	}

	void variableDeclaration(Array<Variable>& vars) {
		u32 offset = 0;
		if (!vars.empty()) {
			const Variable& var = vars.last();
			offset = var.offset;
			switch (var.type) {
				case Type::FLOAT: ++offset; break;
				case Type::FLOAT3: offset += 3; break;
				case Type::FLOAT4: offset += 4; break;
			}
		}
		Variable& var = vars.emplace();
		if (!consume(Token::IDENTIFIER, var.name)) return;
		consume(Token::COLON);
		var.type = parseType();
		var.offset = offset;
	}

	static i32 getArgumentIndex(const Function& fn, StringView ident) {
		for (i32 i = 0, c = fn.args.size(); i < c; ++i) {
			if (equalStrings(fn.args[i], ident)) return i;
		}
		return -1;
	}

	i32 getParamIndex(StringView name) const {
		for (const Variable& var : m_params) {
			if (equalStrings(var.name, name)) return i32(&var - m_params.begin());
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
			case Token::Type::PARAM: return "param";
			case Token::Type::FN: return "fn";
			case Token::Type::WORLD_SPACE: return "world_space";
			case Token::Type::VAR: return "var";
			case Token::Type::OUT: return "out";
			case Token::Type::IN: return "in";
			case Token::Type::LET: return "let";
			case Token::Type::RETURN: return "return";
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
				for (Node*& arg : n->args) {
					arg = collapseConstants(arg);
				}
				// TODO collapse the function itself if all args are literals
				return node;
			}
			case Node::FUNCTION_ARG: return node;
			case Node::OUTPUT_VAR: return node;
			case Node::EMITTER_REF: return node;
			case Node::INPUT_VAR: return node;
			case Node::SYSTEM_VALUE: return node;
			case Node::VAR: return node;
			case Node::PARAM: return node;
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

	Node* atom(const CompileContext& ctx) {
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

	Node* block(const CompileContext& ctx) {
		auto* node = LUMIX_NEW(m_arena_allocator, BlockNode)(peekToken(), m_arena_allocator);
		if (!consume(Token::LEFT_BRACE)) return nullptr;
		node->statements.reserve(8);
		for (;;) {
			Token token = peekToken();
			switch (token.type) {
				case Token::ERROR: return nullptr;
				case Token::EOF:
					errorAtCurrent("Unexpected end of file.");
					return nullptr;
				case Token::RIGHT_BRACE:
					consumeToken();
					return node;
				default: {
					Node* s = statement(ctx);
					if (!s) return nullptr;
					if (!consume(Token::SEMICOLON)) return nullptr;
					node->statements.push(s);
				}
			}
		}
	}

	Node* atomInternal(const CompileContext& ctx) {
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
					auto* node = LUMIX_NEW(m_arena_allocator, ParamNode)(token);
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
						auto* node = LUMIX_NEW(m_arena_allocator, InputVarNode)(token);
						node->index = input_index;
						return node;
					}
				}

				if (ctx.emitter) {
					i32 output_index = find(ctx.emitter->m_outputs, token.value);
					if (output_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, OutputVarNode)(token);
						node->index = output_index;
						return node;
					}

					i32 input_index = find(ctx.emitter->m_inputs, token.value);
					if (input_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, InputVarNode)(token);
						node->index = input_index;
						return node;
					}

					i32 var_index = find(ctx.emitter->m_vars, token.value);
					if (var_index >= 0) {
						auto* node = LUMIX_NEW(m_arena_allocator, VarNode)(token);
						node->index = var_index;
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

	Node* statement(const CompileContext& ctx) {
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
	Node* expression(const CompileContext& ctx, u32 min_priority) {
		Node* lhs = atom(ctx);
		if (!lhs) return nullptr;
		
		for (;;) {
			Token op = peekToken();
			switch (op.type) {
				case Token::EOF: return lhs;
				case Token::ERROR: return nullptr;

				case Token::LT: case Token::GT:
				case Token::SLASH: case Token::STAR:
				case Token::MINUS: case Token::PLUS: {
					u32 prio = getPriority(op);
					if (prio < min_priority) return lhs;
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

	void compileConst() {
        Constant& c = m_constants.emplace();
		if (!consume(Token::IDENTIFIER, c.name)) return;
        if (!consume(Token::EQUAL)) return;
        
		Node* n = expression({}, 0);
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
        c.type = Type::FLOAT;

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
					error(t.value, "Unexpected token.");
					return;
			}
		}
	}

	void compileFunction(Emitter& emitter) {
		StringView fn_name;
		if (!consume(Token::IDENTIFIER, fn_name)) return;
		if (!consume(Token::LEFT_BRACE)) return;
		
		OutputMemoryStream& compiled = [&]() -> OutputMemoryStream& {
			if (equalStrings(fn_name, "update")) return emitter.m_update;
			if (equalStrings(fn_name, "emit")) return emitter.m_emit;
			if (equalStrings(fn_name, "output")) return emitter.m_output;
			error(fn_name, "Unknown function");
			return emitter.m_output;
		}();

		CompileContext ctx;
		ctx.emitter = &emitter;
		for (;;) {
			Token token = peekToken();
			switch (token.type) {
				case Token::ERROR: return;
				case Token::EOF:
					error(token.value, "Unexpected end of file.");
					return;
				case Token::RIGHT_BRACE:
					consumeToken();
					compiled.write(InstructionType::END);
					return;
				case Token::LET:
					ASSERT(false);
					//declareLocal(ctx);
					break;
				default: {
					Node* s = statement({.emitter = &emitter});
					if (!s) return;
					if (!consume(Token::SEMICOLON)) return;
					if (!compile(ctx, s, compiled).success) return;
					// TODO compile
					break;
				}
			}
		}
	}

	struct CompileResult {
		DataStream streams[4];
		u32 num_streams = 0;
		bool success = true;
	};

	CompileResult toCompileResult(const Variable& var, DataStream::Type type) {
		CompileResult res;
		res.streams[0].type = type;
		res.streams[0].index = var.offset;
		res.num_streams = 1;
		switch (var.type) {
			case Type::FLOAT4: 
				res.streams[3].type = type;
				res.streams[3].index = var.offset + 3;
				++res.num_streams;
				// fallthrough
			case Type::FLOAT3: 
				res.streams[2].type = type;
				res.streams[2].index = var.offset + 2;
				res.streams[1].type = type;
				res.streams[1].index = var.offset + 1;
				res.num_streams += 2;
				// fallthrough
			case Type::FLOAT: return res;
		}
		ASSERT(false);
		return res;
	}

	DataStream allocRegister(CompileContext& ctx) {
		DataStream res;
		res.type = DataStream::REGISTER;
		for (u32 i = 0; i < 32; ++i) {
			if ((ctx.register_allocator & (1 << i)) == 0) {
				res.index = i;
				ctx.emitter->m_num_used_registers = maximum(ctx.emitter->m_num_used_registers, i + 1);
				ctx.register_allocator |= 1 << i;
				return res;
			}
		}
		error(StringView(m_tokenizer.m_current, m_tokenizer.m_document.end), "Run out of registers");
		return res;
	}

	CompileResult compile(CompileContext& ctx, Node* node, OutputMemoryStream& compiled) {
		CompileResult res;
		node = collapseConstants(node);
		switch (node->type) {
			case Node::BINARY_OPERATOR: {
				auto* n = (BinaryOperatorNode*)node;
				CompileResult left = compile(ctx, n->left, compiled);
				CompileResult right = compile(ctx, n->right, compiled);
				if (!left.success) return left;
				if (!right.success) return right;
				if (left.num_streams != right.num_streams && left.num_streams != 1 && right.num_streams != 1) {
					error(n->token.value, "Mismatched operands in binary operation.");
					return {.success = false };
				}
				for (u32 i = 0; i < maximum(left.num_streams, right.num_streams); ++i) {
					switch (n->op) {
						case Operators::MOD: compiled.write(InstructionType::MOD); break;
						case Operators::ADD: compiled.write(InstructionType::ADD); break;
						case Operators::SUB: compiled.write(InstructionType::SUB); break;
						case Operators::MUL: compiled.write(InstructionType::MUL); break;
						case Operators::DIV: compiled.write(InstructionType::DIV); break;
						case Operators::LT: compiled.write(InstructionType::LT); break;
						case Operators::GT: compiled.write(InstructionType::GT); break;
					}
					DataStream dst = allocRegister(ctx);
					res.streams[i] = dst;
					++res.num_streams;
					compiled.write(dst);
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
			case Node::OUTPUT_VAR: {
				u32 index = ((OutputVarNode*)node)->index;
				res = toCompileResult(ctx.emitter->m_outputs[index], DataStream::OUT);
				return res;
			}
			case Node::VAR: {
				u32 index = ((VarNode*)node)->index;
				res = toCompileResult(ctx.emitter->m_vars[index], DataStream::CHANNEL);
				return res;
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
			case Node::SYSCALL: {
				auto* n = (SysCallNode*)node;
				CompileResult args[8];
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
				DataStream dst = allocRegister(ctx);
				compiled.write(dst); res.streams[0] = dst;
				res.num_streams = 1;
				for (i32 i = 0; i < n->args.size(); ++i) {
					compiled.write(args[i].streams[0]);
				}
				// TODO n->after_block
				return res;
			}
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
		consume(Token::LEFT_BRACE);

		for (;;) {
			Token t = peekToken();
			switch (t.type) {
				case Token::ERROR: return;
				case Token::EOF: error(t.value, "Unexpected end of file."); return;
				case Token::RIGHT_BRACE:
					consumeToken();	
					return;
				case Token::LET:
					ASSERT(false);
					//declareLocal({.function = &fn});
					break;
				default:
					Node* s = statement({.function = &fn});
					if (!s) return;
					fn.statements.push(s);
					if (!consume(Token::SEMICOLON)) return;
					break;
			}
		
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
					return;
				case Token::IDENTIFIER:
					if (equalStrings(token.value, "material")) compileMaterial(emitter);
					else if (equalStrings(token.value, "mesh")) compileMesh(emitter);
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
				case Type::FLOAT: 
					decl.addAttribute(offset, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(float);
					break;
				case Type::FLOAT3: 
					decl.addAttribute(offset, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
					offset += sizeof(Vec3);
					break;
				case Type::FLOAT4: 
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
				case Token::CONST: compileConst(); break;
				case Token::FN: compileFunction(); break;
				case Token::PARAM: variableDeclaration(m_params); break;
				case Token::EMITTER: compileEmitter(); break;
				case Token::WORLD_SPACE: ASSERT(false); break; // m_is_world_space = true; break;
				default: error(token.value, "Unexpected token."); return false;
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
					case Type::FLOAT4: c+= 4; break;
					case Type::FLOAT3: c+= 3; break;
					case Type::FLOAT: c+= 1; break;
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
			output.write(emitter.m_num_used_registers);
			output.write(getCount(emitter.m_outputs));
			output.write(emitter.m_init_emit_count);
			output.write(emitter.m_emit_per_second);
			output.write(getCount(emitter.m_inputs));
			output.write(emitter.m_max_ribbons);
			output.write(emitter.m_max_ribbon_length);
			output.write(emitter.m_init_ribbons_count);
		}
		output.write(m_params.size());
		for (const Variable& p : m_params) {
			output.writeString(p.name);
			switch (p.type) {
				case Type::FLOAT: output.write(u32(1)); break;
				case Type::FLOAT3: output.write(u32(3)); break;
				case Type::FLOAT4: output.write(u32(4)); break;
			}
		}
		
		return !m_is_error;
	}


#if 0

	struct Local {
		StringView name;
		Type type;
		i32 registers[4];
	};

	struct ExpressionStackElement {
		enum Type {
			NONE,
			LITERAL,
			OPERATOR,
			FUNCTION,
			VARIABLE,
			SYSTEM_VALUE,
			REGISTER,
			PARAM,
			ASSIGN,
			FUNCTION_CALL,
			FUNCTION_ARGUMENT,
			RETURN,
			OUTPUT,
			EMITTER_INDEX,
			BLOCK_END,
			NEGATIVE
		};

		Type type;
		float literal_value;
		Operators operator_char;
		BuiltinFunction function_desc;
		union {
			i32 variable_offset;
			i32 param_index;
			i32 arg_index;
			i32 register_offset;
			i32 output_offset;
			i32 function_index;
			i32 emitter_index;
		};
		ParticleSystemValues system_value;
		u8 sub;
		u32 parenthesis_depth = 0xffFFffFF;
	};

	enum class VariableFamily {
		OUTPUT,
		CHANNEL,
		INPUT,
		LOCAL
	};

	static u32 getPriority(const ExpressionStackElement& el) {
		u32 prio = el.parenthesis_depth * 10;
		switch (el.type) {
			case ExpressionStackElement::FUNCTION_CALL: prio += 9; break;
			case ExpressionStackElement::NEGATIVE: prio += 9; break;
			case ExpressionStackElement::FUNCTION: prio += 8; break;
			case ExpressionStackElement::OPERATOR: 
				switch(el.operator_char) {
					case Operators::GT: case Operators::LT: prio += 0; break;
					case Operators::ADD: case Operators::SUB: prio += 1; break;
					case Operators::MOD: case Operators::MUL: case Operators::DIV: prio += 2; break;
				}
				break;
			default: ASSERT(false);
		}
		return prio;
	}

	void pushStack(const ExpressionStackElement& el) {
		u32 priority = getPriority(el);
		while (!m_stack.empty()) {
			if (getPriority(m_stack.last()) < priority) break;

			m_postfix.push(m_stack.last());
			m_stack.pop();
		}
		m_stack.push(el);
	}

	// b.xy = a.y;
	bool compileVariable(const CompileContext& ctx, i32 var_index, VariableFamily type, u32& parenthesis_depth, u32 sub, bool can_assign) {
		ExpressionStackElement el;
		el.parenthesis_depth = parenthesis_depth;

		u8 swizzle[4] = {0, 1, 2, 3};
		u32 swizzle_len = 0;
		StringView swizzle_str;
		// if swizzle
		if (peekToken().type == Token::DOT) {
			if (!consume(Token::DOT)) return false;
			if (!consume(Token::IDENTIFIER, swizzle_str)) return false;
			if (swizzle_str.size() > 4) {
				error(swizzle_str, "Invalid swizzle.");
				return false;
			}
			swizzle_len = swizzle_str.size();
			for (u32 i = 0; i < swizzle_len; ++i) {
				switch (swizzle_str[i]) {
					case 'x': case 'r': swizzle[i] = 0; break;
					case 'y': case 'g': swizzle[i] = 1; break;
					case 'z': case 'b': swizzle[i] = 2; break;
					case 'w': case 'a': swizzle[i] = 3; break;
					default: error(swizzle_str, "Invalid swizzle"); return false;
				}
			}
			// a.xy = b.y; means a.x = b.y; a.y = b.y;
			for (u32 i = swizzle_len; i < 4; ++i) {
				swizzle[i] = swizzle[swizzle_len - 1];
			}
		}
		el.sub = swizzle[sub];

		switch (type) {
			case VariableFamily::OUTPUT:
				el.output_offset = ctx.emitter->m_outputs[var_index].getOffsetSub(el.sub);
				el.type = ExpressionStackElement::OUTPUT;
				break;
			case VariableFamily::CHANNEL:
				el.variable_offset = ctx.emitter->m_vars[var_index].getOffsetSub(el.sub);
				el.type = ExpressionStackElement::VARIABLE;
				break;
			case VariableFamily::INPUT:
				el.register_offset = ctx.emitted->m_inputs[var_index].getOffsetSub(el.sub);
				el.type = ExpressionStackElement::OUTPUT;
				break;
			case VariableFamily::LOCAL:
				el.register_offset = m_locals[var_index].registers[el.sub];
				el.type = ExpressionStackElement::REGISTER;
				break;
		}

		if (peekToken().type != Token::EQUAL) {
			m_postfix.push(el);
			return true;
		}
			
		consumeToken();
		if (!can_assign) {
			error(m_tokenizer.m_current_token.value, "Unexpected =.");
			return false;
		}

		Type var_type;
		switch (type) {
			case VariableFamily::OUTPUT: var_type = ctx.emitter->m_outputs[var_index].type; break;
			case VariableFamily::CHANNEL: var_type = ctx.emitter->m_vars[var_index].type; break;
			case VariableFamily::INPUT: var_type = ctx.emitted->m_inputs[var_index].type; break;
			case VariableFamily::LOCAL: var_type = m_locals[var_index].type; break;
		}

		u32 var_len = 0;
		switch (var_type) {
			case Type::FLOAT: var_len = 1; break;
			case Type::FLOAT3: var_len = 3; break;
			case Type::FLOAT4: var_len = 4; break;
		}

		if (swizzle_len == 0) swizzle_len = var_len;
		for (u32 i = 0; i < swizzle_len; ++i) {
			if (swizzle[i] >= var_len) {
				error(swizzle_str, "Out of bounds swizzle.");
				return false;
			} 
			ParticleScriptTokenizer tokenizer = m_tokenizer;
			compileExpression(ctx, i, parenthesis_depth);
			if (i != var_len - 1) m_tokenizer = tokenizer;
			ASSERT(m_stack.empty());
			
			el.sub = swizzle[i];
			switch (type) {
				case VariableFamily::OUTPUT: el.output_offset = ctx.emitter->m_outputs[var_index].getOffsetSub(el.sub); break;
				case VariableFamily::CHANNEL: el.variable_offset = ctx.emitter->m_vars[var_index].getOffsetSub(el.sub); break;
				case VariableFamily::INPUT: el.register_offset = ctx.emitted->m_inputs[var_index].getOffsetSub(el.sub); break;
				case VariableFamily::LOCAL: el.register_offset = m_locals[var_index].registers[el.sub]; break;
			}
			
			m_postfix.push(el);
			m_postfix.emplace().type = ExpressionStackElement::ASSIGN;
		}
		return true;
	}

	float compileCompound(u32 sub) {
		float res = 0;
		for (u32 i = 0;; ++i) {
			Token t = peekToken();
			if (t.type == Token::RIGHT_BRACE) {
				if (i <= sub) error(t.value, "Out of bounds access.");
				return res;
			}
			if (i > 0 && !consume(Token::COMMA)) return 0;
			StringView val;
			bool neg = false;
			if (peekToken().type == Token::MINUS) {
				neg = true;
				consumeToken();
			}
			if (!consume(Token::NUMBER, val)) return 0;
			if (i == sub) {
				fromCString(val, res);
				if (neg) res = -res;
			}
		}
	}

	i32 getFunctionIndex(StringView ident) const {
		for (Function& fn : m_functions) {
			if (equalStrings(fn.name, ident)) return i32(&fn - m_functions.begin());
		}
		return -1;
	}

	i32 getLocalIndex(StringView ident) const {
		for (Local& v : m_locals) {
			if (equalStrings(v.name, ident)) return i32(&v - m_locals.begin());
		}
		return -1;
	}

	i32 getEmitterIndex(StringView name) const {
		for (const Emitter& emitter : m_emitters) {
			if (equalStrings(emitter.m_name, name)) return i32(&emitter - m_emitters.begin());
		}
		return -1;
	}

	bool compileEmitBlock(const CompileContext& ctx, Emitter& emitted) {
		for (;;) {
			Token token = peekToken();
			switch (token.type) {
				case Token::ERROR: return false;
				case Token::EOF:
					error(token.value, "Unexpected end of file.");
					return false;
				case Token::RIGHT_BRACE:
					return true;
				default: {
					u32 depth = 0;
					CompileContext ctx2 = ctx;
					ctx2.emitted = &emitted;
					compileExpression(ctx2, 0, depth);
					if (!consume(Token::SEMICOLON)) return false;
					break;
				}
			}
		}
		return true;
	}

	Node* compileExpression(const CompileContext& ctx, u32 sub, u32& parenthesis_depth) {
		bool can_be_unary = true;
		bool can_assign = true;

		const i32 start_postfix_size = m_postfix.size();
		const i32 start_stack_size = m_stack.size();
		u32 start_depth = parenthesis_depth;
		for (;;) {
			Token token = peekToken();
			switch (token.type) {
				case Token::ERROR: return nullptr;
				case Token::EOF:
					consumeToken();
					error(token.value, "Unexpected end of file");
					return nullptr;
				case Token::COMMA:
				case Token::SEMICOLON:
					while (m_stack.size() > start_stack_size) {
						m_postfix.push(m_stack.last());
						m_stack.pop();
					}
					if (m_postfix.size() == start_postfix_size) {
						error(token.value, "Empty expression");
						return nullptr;
					}
					return nullptr;
				case Token::RIGHT_PAREN:
					if (parenthesis_depth == start_depth) {
						while (m_stack.size() > start_stack_size) {
							m_postfix.push(m_stack.last());
							m_stack.pop();
						}
						if (m_postfix.size() == start_postfix_size) {
							error(token.value, "Empty expression");
							return nullptr;
						}
						return nullptr;
					}
					consumeToken();
					--parenthesis_depth;
					can_be_unary = false;
					break;
				case Token::LEFT_PAREN:
					consumeToken();
					can_be_unary = true;
					++parenthesis_depth;
					break;
				case Token::LEFT_BRACE: {
					consumeToken();
					ExpressionStackElement& el = m_postfix.emplace();
					el.type = ExpressionStackElement::LITERAL;
					el.literal_value = compileCompound(sub);
					el.parenthesis_depth = parenthesis_depth;
					consume(Token::RIGHT_BRACE);
					break;
				}
				case Token::NUMBER: {
					consumeToken();
					ExpressionStackElement& el = m_postfix.emplace();
					el.type = ExpressionStackElement::LITERAL;
					el.literal_value = asFloat(token);
					el.parenthesis_depth = parenthesis_depth;
					can_be_unary = false;
					break;
				}
				case Token::MINUS: {
					consumeToken();
					if (can_be_unary) {
						if (peekToken().type == Token::NUMBER) {
							consumeToken();
							ExpressionStackElement& el = m_postfix.emplace();
							el.type = ExpressionStackElement::LITERAL;
							el.literal_value = -asFloat(token);
							el.parenthesis_depth = parenthesis_depth;
							can_be_unary = false;
							break;
						}
						// TODO this does not work with abs(-a + b);
						compileExpression(ctx, sub, parenthesis_depth);
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::NEGATIVE;
						break;
					}
					ExpressionStackElement el;
					el.type = ExpressionStackElement::OPERATOR;
					el.operator_char = (Operators)token.value[0];
					el.parenthesis_depth = parenthesis_depth;
					pushStack(el);
					can_be_unary = true;
					break;
				}
				case Token::RETURN: {
					consumeToken();
					compileExpression(ctx, sub, parenthesis_depth);
					ExpressionStackElement& el = m_postfix.emplace();
					el.type = ExpressionStackElement::RETURN;
					can_be_unary = true;
					break;
				}
				case Token::GT:
				case Token::LT:
				case Token::PERCENT:
				case Token::PLUS:
				case Token::STAR:
				case Token::SLASH: {
					consumeToken();
					ExpressionStackElement el;
					el.type = ExpressionStackElement::OPERATOR;
					el.operator_char = (Operators)token.value[0];
					el.parenthesis_depth = parenthesis_depth;
					pushStack(el);
					can_be_unary = true;
					break;
				}
				case Token::IDENTIFIER: {
					consumeToken();
					BuiltinFunction bfn = getBuiltinFunction(token.value);
					if (bfn.instruction != InstructionType::END) {
						++parenthesis_depth;
						if (!consume(Token::LEFT_PAREN)) return nullptr;

						if (bfn.instruction == InstructionType::EMIT) {
							StringView emitter_name;
							if (!consume(Token::IDENTIFIER, emitter_name)) return nullptr;
							i32 emitter_index = getEmitterIndex(emitter_name);
							if (emitter_index < 0) {
								error(emitter_name, "Unknown emitter");
								return nullptr;
							}
							consume(Token::COMMA);
					
							const i32 condition_postfix_offset = m_postfix.size();
							compileExpression(ctx, 0, parenthesis_depth);
							StackArray<ExpressionStackElement, 16> cond_exprs(m_allocator);
							while (m_postfix.size() > condition_postfix_offset) {
								cond_exprs.push(m_postfix.last());
								m_postfix.pop();
							}

							consume(Token::RIGHT_PAREN);
							--parenthesis_depth;

							ExpressionStackElement& end_el = m_postfix.emplace();
							end_el.type = ExpressionStackElement::BLOCK_END;
							if (peekToken().type == Token::LEFT_BRACE) {
								consumeToken();
								if (!compileEmitBlock(ctx, m_emitters[emitter_index])) return nullptr;
								consume(Token::RIGHT_BRACE);
							}
							else {
								consume(Token::SEMICOLON);
							}

							ExpressionStackElement& emit_el = m_postfix.emplace();
							emit_el.type = ExpressionStackElement::EMITTER_INDEX;
							emit_el.emitter_index = emitter_index;

							while (!cond_exprs.empty()) {
								m_postfix.push(cond_exprs.last());
								cond_exprs.pop();
							}

							ExpressionStackElement& dst = m_postfix.emplace();
							dst.type = ExpressionStackElement::FUNCTION;
							dst.function_desc = bfn;

							//flushCompile(ctx, compiled);
							return nullptr;
						}
						
						ExpressionStackElement el;
						el.type = ExpressionStackElement::FUNCTION;
						el.function_desc = bfn;
						el.parenthesis_depth = parenthesis_depth - 1;
						el.sub = sub;
						pushStack(el);
						for (u32 i = 0; i < bfn.num_args; ++i) {
							if (i > 0 && !consume(Token::COMMA)) return nullptr;
							compileExpression(ctx, sub, parenthesis_depth);
						}
						if (!consume(Token::RIGHT_PAREN)) return nullptr;
						--parenthesis_depth;
						can_be_unary = true;
						break;
					}

					ParticleSystemValues system_value = getSystemValue(token.value);
					if (system_value != ParticleSystemValues::NONE) {
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::SYSTEM_VALUE;
						el.system_value = system_value;
						el.parenthesis_depth = parenthesis_depth;
						can_be_unary = false;
						break;
					}

					i32 local_index = getLocalIndex(token.value);
					if (local_index >= 0) {
						if (!compileVariable(ctx, local_index, VariableFamily::LOCAL, parenthesis_depth, sub, can_assign)) return nullptr;
						can_be_unary = false;
						break;
					}

					i32 fn_index = getFunctionIndex(token.value);
					if (fn_index >= 0) {
						ExpressionStackElement el;
						el.type = ExpressionStackElement::FUNCTION_CALL;
						el.function_index = fn_index;
						el.parenthesis_depth = parenthesis_depth;
						el.sub = sub;
						pushStack(el);
						++parenthesis_depth;
						consume(Token::LEFT_PAREN);
						for (u32 i = 0, c = m_functions[fn_index].args.size(); i < c; ++i) {
							compileExpression(ctx, sub, parenthesis_depth);
						}
						consume(Token::RIGHT_PAREN);
						--parenthesis_depth;
						can_be_unary = true;
						break;;
					}

					if (ctx.emitted) {
						i32 index = getInputIndex(*ctx.emitted, token.value);
						if (index >= 0) {
							if (!compileVariable(ctx, index, VariableFamily::INPUT, parenthesis_depth, sub, can_assign)) return nullptr;
							can_be_unary = false;
							break;
						}
					}

					if (ctx.emitter) {
						i32 input_index = getInputIndex(*ctx.emitter, token.value);
						if (input_index >= 0) {
							ExpressionStackElement& el = m_postfix.emplace();
							el.type = ExpressionStackElement::REGISTER;
							el.register_offset = ctx.emitter->m_inputs[input_index].getOffsetSub(sub);
							el.parenthesis_depth = parenthesis_depth;
							can_be_unary = false;
							break;
						}

						i32 out_index = getOutputIndex(*ctx.emitter, token.value);
						if (out_index >= 0) {
							if (!compileVariable(ctx, out_index, VariableFamily::OUTPUT, parenthesis_depth, sub, can_assign)) return nullptr;
							can_be_unary = false;
							break;
						}

						i32 var_index = getVariableIndex(*ctx.emitter, token.value);
						if (var_index >= 0) {
							if (!compileVariable(ctx, var_index, VariableFamily::CHANNEL, parenthesis_depth, sub, can_assign)) return nullptr;
							can_be_unary = false;
							break;
						}
					}

					if (Constant* c = getConstant(token.value)) {
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::LITERAL;
						el.literal_value = c->value[sub];
						el.parenthesis_depth = parenthesis_depth;
						can_be_unary = false;
						break;
					}

					i32 param_index = getParamIndex(token.value);
					if (param_index >= 0) {
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::PARAM;
						el.param_index = param_index;
						el.parenthesis_depth = parenthesis_depth;
						can_be_unary = false;

						if (peekToken().type != Token::DOT) {
							el.sub = sub;
							continue;
						}

						consumeToken();
						StringView subtoken;
						if (!consume(Token::IDENTIFIER, subtoken)) return nullptr;
						if (subtoken.size() != 1) {
							error(subtoken, "Unknown subscript.");
							return nullptr;
						}

						switch(subtoken[0]) {
							case 'x': case 'r': el.sub = 0; break;
							case 'y': case 'g': el.sub = 1; break;
							case 'z': case 'b': el.sub = 2; break;
							case 'w': case 'a': el.sub = 3; break;
							default: error(subtoken, "Unknown subscript"); break;
						}
						break;
					}

					if (ctx.function) {
						i32 arg_index = getArgumentIndex(*ctx.function, token.value);
						if (arg_index >= 0) {
							ExpressionStackElement& el = m_postfix.emplace();
							el.type = ExpressionStackElement::FUNCTION_ARGUMENT;
							el.arg_index = arg_index;
							can_be_unary = false;
							break;
						}					
					}
					error(token.value, "Unexpected token ", token.value);
					return nullptr;
				}
				default:
					error(token.value, "Unexpected token ", token.value);
					return nullptr;
			}
			can_assign = false;
		}
	}

	void deallocRegister(const DataStream& stream) {
		if (stream.type == DataStream::REGISTER && (m_local_allocator & (1 << stream.index)) == 0) {
			m_register_allocator = m_register_allocator & ~(1 << stream.index);
		}
	}

	DataStream allocRegister() {
		DataStream res;
		res.type = DataStream::REGISTER;
		for (u32 i = 0; i < 32; ++i) {
			if ((m_register_allocator & (1 << i)) == 0) {
				res.index = i;
				m_num_used_registers = maximum(m_num_used_registers, i + 1);
				m_register_allocator |= 1 << i;
				return res;
			}
		}
		error(StringView(m_tokenizer.m_current, m_tokenizer.m_document.end), "Run out of registers");
		return res;
	}

	DataStream compilePostfix(const CompileContext& ctx, OutputMemoryStream& compiled, Span<ExpressionStackElement>& postfix) {
		if (postfix.size() == 0) return {};
		ExpressionStackElement el = postfix.last();
		postfix.removeSuffix(1);
		
		switch (el.type) {
			case ExpressionStackElement::OUTPUT:
			case ExpressionStackElement::RETURN:
			case ExpressionStackElement::NONE: ASSERT(false); return {};

			case ExpressionStackElement::BLOCK_END: 
				compiled.write(InstructionType::END);
				return {};
			
			case ExpressionStackElement::FUNCTION_ARGUMENT:
				return ctx.args[el.arg_index];
		
			case ExpressionStackElement::FUNCTION_CALL: {
				const Function& fn = m_functions[el.function_index];
				StackArray<DataStream, 16> args(m_allocator);
				for (StringView a : fn.args) {
					DataStream arg_val = compilePostfix(ctx, compiled, postfix);
					args.push(arg_val);
				}

				Span<ExpressionStackElement> fn_exprs = fn.expressions;
				CompileContext ctx2 = ctx;
				ctx2.args = args;
				DataStream res = compilePostfix(ctx2, compiled, fn_exprs);
				for (const DataStream& arg : args) {
					deallocRegister(arg);
				}
				return res;
			}

			case ExpressionStackElement::ASSIGN: {
				ExpressionStackElement dst_el = postfix.last();
				postfix.removeSuffix(1);
				DataStream src = compilePostfix(ctx, compiled, postfix);
				compiled.write(InstructionType::MOV);
				DataStream dst;
				switch (dst_el.type) {
					case ExpressionStackElement::OUTPUT:
						dst.type = DataStream::OUT;
						dst.index = dst_el.output_offset;
						break;
					case ExpressionStackElement::VARIABLE:
						dst.type = DataStream::CHANNEL;
						dst.index = dst_el.variable_offset;
						break;
					case ExpressionStackElement::REGISTER:
						dst.type = DataStream::REGISTER;
						dst.index = dst_el.register_offset;
						break;
					default: ASSERT(false); break;
				}
				compiled.write(dst);
				compiled.write(src);
				deallocRegister(src);
				return {};
			}

			case ExpressionStackElement::LITERAL: {
				DataStream res;
				res.type = DataStream::LITERAL;
				res.value = el.literal_value;
				return res;
			}

			case ExpressionStackElement::SYSTEM_VALUE: {
				DataStream res;
				res.type = DataStream::SYSTEM_VALUE;
				res.index = (u8)el.system_value;
				return res;
			}

			case ExpressionStackElement::PARAM: {
				DataStream res;
				res.type = DataStream::PARAM;
				res.index = m_params[el.param_index].getOffsetSub(el.sub);
				return res;
			}

			case ExpressionStackElement::EMITTER_INDEX: {
				DataStream res;
				res.type = DataStream::LITERAL;
				res.index = el.emitter_index;
				return res;
			}

			case ExpressionStackElement::VARIABLE: {
				DataStream res;
				res.type = DataStream::CHANNEL;
				res.index = el.variable_offset;
				return res;
			}

			case ExpressionStackElement::REGISTER: {
				DataStream res;
				res.type = DataStream::REGISTER;
				res.index = el.register_offset;
				return res;
			}

			case ExpressionStackElement::OPERATOR: {
				if (postfix.size() < 2) {
					// TODO error location
					error(m_tokenizer.m_document, "Operator must have left and right side");
					return {};
				}
				DataStream op2 = compilePostfix(ctx, compiled, postfix);
				DataStream op1 = compilePostfix(ctx, compiled, postfix);

				if (op1.type == DataStream::LITERAL && op2.type == DataStream::LITERAL) {
					DataStream res;
					res.type = DataStream::LITERAL;
					switch (el.operator_char) {
						case Operators::MOD: res.value = fmodf(op1.value, op2.value); break;
						case Operators::ADD: res.value = op1.value + op2.value; break;
						case Operators::SUB: res.value = op1.value - op2.value; break;
						case Operators::MUL: res.value = op1.value * op2.value; break;
						case Operators::DIV: res.value = op1.value / op2.value; break;
						case Operators::GT:
						case Operators::LT: goto nonliteral;
					}
					return res;
				}

				nonliteral:
				switch (el.operator_char) {
					case Operators::MOD: compiled.write(InstructionType::MOD); break;
					case Operators::ADD: compiled.write(InstructionType::ADD); break;
					case Operators::SUB: compiled.write(InstructionType::SUB); break;
					case Operators::MUL: compiled.write(InstructionType::MUL); break;
					case Operators::DIV: compiled.write(InstructionType::DIV); break;
					case Operators::LT: compiled.write(InstructionType::LT); break;
					case Operators::GT: compiled.write(InstructionType::GT); break;
				}

				DataStream res = allocRegister();
				compiled.write(res);
				compiled.write(op1);
				compiled.write(op2);

				deallocRegister(op1);
				deallocRegister(op2);
				return res;
			}

			case ExpressionStackElement::NEGATIVE: {
				DataStream cont = compilePostfix(ctx, compiled, postfix);
				if (cont.type == DataStream::LITERAL) {
					cont.value = -cont.value;
					return cont;
				}
				DataStream res = allocRegister();
				compiled.write(InstructionType::MUL);
				compiled.write(res);
				compiled.write(cont);
				DataStream neg;
				neg.type = DataStream::LITERAL;
				neg.value = -1;
				compiled.write(neg);
				return res;
			}

			case ExpressionStackElement::FUNCTION: {
				if (el.function_desc.instruction == InstructionType::EMIT) {
					DataStream cond_stream = compilePostfix(ctx, compiled, postfix);
					DataStream emitter_index = compilePostfix(ctx, compiled, postfix);
					compiled.write(el.function_desc.instruction);
					ASSERT(emitter_index.type == DataStream::LITERAL);
					compiled.write(cond_stream);
					deallocRegister(cond_stream);
					compiled.write(u32(emitter_index.index));
					return {};
				}

				if (el.function_desc.instruction == InstructionType::RAND) {
					DataStream res = allocRegister();
					compiled.write(el.function_desc.instruction);
					compiled.write(res);
					if (postfix.size() < 2) {
						// TODO error location
						error(m_tokenizer.m_document, "rand expects 2 arguments");
						return {};
					}
					DataStream arg1 = compilePostfix(ctx, compiled, postfix);
					DataStream arg0 = compilePostfix(ctx, compiled, postfix);
					if (arg0.type != DataStream::LITERAL || arg0.type != DataStream::LITERAL) {
						// TODO error location
						error(m_tokenizer.m_document, "rand expects 2 literals");
						return {};
					}

					compiled.write(arg0.value);
					compiled.write(arg1.value);
					return res;
				}

				if (el.function_desc.instruction == InstructionType::MESH) {
					DataStream res = allocRegister();
					if (m_mesh_stream.type == DataStream::NONE) {
						m_mesh_stream = allocRegister();
					}
					compiled.write(el.function_desc.instruction);
					compiled.write(res);
					compiled.write(m_mesh_stream);
					compiled.write(el.sub);
					return res;
				}
			
				if (el.function_desc.instruction == InstructionType::GRADIENT) {
					DataStream res = allocRegister();
					compiled.write(el.function_desc.instruction);
					compiled.write(res);

					float keys[8];
					float values[8];
					u32 num_frames = 0;
					DataStream t;
					for (;;) {
						DataStream stream = compilePostfix(ctx, compiled, postfix);
						if (stream.type != DataStream::LITERAL) {
							t = stream;
							break;
						}
						keys[num_frames] = stream.value;

						stream = compilePostfix(ctx, compiled, postfix);
						if (stream.type != DataStream::LITERAL) {
							// TODO error location
							error(m_tokenizer.m_document, "Literal expected");
							return {};
						}
						values[num_frames] = stream.value;
						++num_frames;
					}

					compiled.write(t);
					deallocRegister(t);
					compiled.write(num_frames);
					compiled.write(keys, sizeof(keys[0]) * num_frames);
					compiled.write(values, sizeof(values[0]) * num_frames);
					return res;
				}

				DataStream ops[2];
				ASSERT(lengthOf(ops) >= el.function_desc.num_args);
				for (u32 i = 0; i < el.function_desc.num_args; ++i) {
					ops[i] = compilePostfix(ctx, compiled, postfix);
				}
				compiled.write(el.function_desc.instruction);
				DataStream res = {};
				if (el.function_desc.returns_value) {
					res = allocRegister();
					compiled.write(res);
				}
				for (u32 i = 0; i < el.function_desc.num_args; ++i) {
					compiled.write(ops[i]);
					deallocRegister(ops[i]);
				}

				return res;
			}
		}
		ASSERT(false);
		return {};
	}

	void flushCompile(const CompileContext& ctx, OutputMemoryStream& compiled) {
		Span<ExpressionStackElement> exprs = m_postfix; 
		while (exprs.size() > 0) {
			compilePostfix(ctx, compiled, exprs);
		}
		m_postfix.clear();
	}

	// let a = ...;
	// let a : type;
	// let a : type = ...;
	void declareLocal(const CompileContext& ctx) {
		if (!consume(Token::LET)) return;
		Local& local = m_locals.emplace();
		if (!consume(Token::IDENTIFIER, local.name)) return;
		
		if (peekToken().type == Token::COLON) { 
			// let a : type ...
			if (!consume(Token::COLON)) return;
			local.type = parseType();
		}
		else if(peekToken().type == Token::EQUAL) {
			// let a = ...; 
			local.type = Type::FLOAT;
		}
		else {
			error(peekToken().value, "Unexpected token.");
			return;
		}

		switch (local.type) {
			case Type::FLOAT4: {
				DataStream reg3 = allocRegister();
				m_local_allocator |= 1 << reg3.index;
				local.registers[3] = reg3.index;
				// fallthrough
			}
			case Type::FLOAT3: {
				DataStream reg2 = allocRegister();
				DataStream reg1 = allocRegister();
				m_local_allocator |= 1 << reg2.index;
				m_local_allocator |= 1 << reg1.index;
				local.registers[2] = reg2.index;
				local.registers[1] = reg1.index;
				// fallthrough
			}
			case Type::FLOAT: {
				DataStream reg0 = allocRegister();
				m_local_allocator |= 1 << reg0.index;
				local.registers[0] = reg0.index;
				break;
			}
		}

		if (peekToken().type == Token::SEMICOLON) {
			// let a : type;
			consumeToken();
			return;
		}
		if (!consume(Token::EQUAL)) return;

		// let a : type = ...
		// let a = ...
		u32 var_len;
		switch (local.type) {
			case Type::FLOAT4: var_len = 4; break;
			case Type::FLOAT3: var_len = 3; break;
			case Type::FLOAT: var_len = 1; break;
		}

		for (u32 i = 0; i < var_len; ++i) {
			ParticleScriptTokenizer tokenizer = m_tokenizer;
			u32 depth = 0;
			compileExpression(ctx, i, depth);
			if (i != var_len - 1) m_tokenizer = tokenizer;
			ExpressionStackElement el;
			el.type = ExpressionStackElement::REGISTER;
			el.register_offset = local.registers[i];
			m_postfix.push(el);
			m_postfix.emplace().type = ExpressionStackElement::ASSIGN;
		}

		consume(Token::SEMICOLON);
	}

	Array<Local> m_locals;
	StackArray<ExpressionStackElement, 16> m_stack;
	StackArray<ExpressionStackElement, 16> m_postfix;
	DataStream m_mesh_stream;
	u32 m_register_allocator = 0;
	u32 m_local_allocator = 0;
	u32 m_num_used_registers = 0;
	
	
	#endif
	IAllocator& m_allocator;
	ArenaAllocator m_arena_allocator;
	Path m_path;
	bool m_is_error = false;
	ParticleScriptTokenizer m_tokenizer;
	bool m_is_world_space = false;
	Array<Constant> m_constants;
	Array<Function> m_functions;
	Array<Variable> m_params;
	Array<Emitter> m_emitters;
};


}