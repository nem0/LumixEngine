#pragma once

#include "core/log.h"
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
		CONST, PARAM, EMITTER, FN, WORLD_SPACE, VAR, OUT, IN
	};

	Type type;
	StringView value;
};

struct ParticleScriptTokenizer {
	using Token = ParticleScriptToken;

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
			case 'o': return checkKeyword("ut", 1, 2, Token::OUT);
			case 'p': return checkKeyword("aram", 1, 4, Token::PARAM);
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


    enum class Type {
        FLOAT,
        FLOAT3,
        FLOAT4
    };

    struct Constant {
        StringView name;
        Type type;
        float value[4];
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
	
	struct BuiltinFunction {
        InstructionType instruction;
        bool returns_value;
		u32 num_args;
    };

	enum class Operators : char {
		ADD = '+',
		SUB = '-',
		DIV = '/',
		MUL = '*',
		MOD = '%',
		LT = '<',
		GT = '>',
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
			i32 variable_index;
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

	struct Function {
		Function(IAllocator& allocator) : args(allocator), expressions(allocator) {}
		StringView name;
		Array<StringView> args;
		Array<ExpressionStackElement> expressions;
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
		u32 m_register_allocator = 0;
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
	};

	ParticleScriptCompiler(StringView content, const Path& path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_emitters(allocator)
		, m_constants(allocator)
		, m_params(allocator)
		, m_stack(allocator)
		, m_postfix(allocator)
		, m_functions(allocator)
		, m_path(path)
        , m_text_begin(content.begin)
	{
		m_tokenizer.m_current = content.begin;
		m_tokenizer.m_document = content;
		m_tokenizer.m_current_token = m_tokenizer.nextToken();
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

	void error(StringView location, const char* error_msg) {
		if(!m_is_error) logError(m_path, "(", getLine(location), "): ", error_msg);
		m_is_error = true;
	}

	void error(StringView location, const char* error_msg, const char* error_msg2) {
		if(!m_is_error) logError(m_path, "(", getLine(location), "): ", error_msg2);
		m_is_error = true;
	}

	Token peekToken() { return m_tokenizer.m_current_token; }

	Token consumeToken() {
		Token t = m_tokenizer.m_current_token;
		m_tokenizer.m_current_token = m_tokenizer.nextToken();
		return t;
	}

	bool consume(Token::Type type) {
		Token t = consumeToken();
		if (t.type != type) {
			error(t.value, "Unexpected token.");
			return false;
		}
		return true;
	}

	[[nodiscard]] bool consume(Token::Type type, StringView& value) {
		Token t = consumeToken();
		if (t.type != type) {
			error(t.value, "Unexpected token.");
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

	void compileConst() {
        Constant& c = m_constants.emplace();
		if (!consume(Token::IDENTIFIER, c.name)) return;
        if (!consume(Token::EQUAL)) return;
        
		Token value = consumeToken();
		if (value.type != Token::NUMBER) {
			// TODO floatN constants
			error(value.value, "Expected a number.");
			return;
		}

        float f = asFloat(value);
        c.value[0] = f;
        c.value[1] = f;
        c.value[2] = f;
        c.value[3] = f;
        c.type = Type::FLOAT;

		consume(Token::SEMICOLON);
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

	BuiltinFunction getBuiltinFunction(StringView name) {
		if (equalStrings(name, "cos")) return { InstructionType::COS, true, 1 };
		else if (equalStrings(name, "sin")) return { InstructionType::SIN, true, 1 };
		else if (equalStrings(name, "kill")) return { InstructionType::KILL, false, 1 };
		else if (equalStrings(name, "random")) return { InstructionType::RAND, true, 2 };
		else if (equalStrings(name, "curve")) return { InstructionType::GRADIENT, true, 0xff };
		else if (equalStrings(name, "emit")) return { InstructionType::EMIT, false, 2 };
		else if (equalStrings(name, "mesh")) return { InstructionType::MESH, true, 0 };
		else if (equalStrings(name, "noise")) return { InstructionType::NOISE, true, 1 };
		else if (equalStrings(name, "min")) return { InstructionType::MIN, true, 2 };
		else if (equalStrings(name, "max")) return { InstructionType::MAX, true, 2 };
		else if (equalStrings(name, "sqrt")) return { InstructionType::SQRT, true, 1 };
		else return { InstructionType::END };
	}

	static i32 getArgumentIndex(const Function& fn, StringView ident) {
		for (i32 i = 0, c = fn.args.size(); i < c; ++i) {
			if (equalStrings(fn.args[i], ident)) return i;
		}
		return -1;
	}

	static i32 getInputIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_inputs) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_inputs.begin());
		}
		return -1;
	}

	static i32 getOutputIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_outputs) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_outputs.begin());
		}
		return -1;
	}

	static i32 getVariableIndex(const Emitter& emitter, StringView name) {
		for (const Variable& var : emitter.m_vars) {
			if (equalStrings(var.name, name)) return i32(&var - emitter.m_vars.begin());
		}
		return -1;
	}

	enum class VariableFamily {
		OUTPUT,
		CHANNEL,
		INPUT
	};

	bool compileVariable(const CompileContext& ctx, i32 var_index, VariableFamily type, u32& parenthesis_depth, u32 sub, bool can_assign) {
		ExpressionStackElement el;
		el.parenthesis_depth = parenthesis_depth;

		bool has_subscript = false;

		if (peekToken().type == Token::DOT) {
			consume(Token::DOT);
			StringView subtoken;
			if (!consume(Token::IDENTIFIER, subtoken)) return false;
			if (subtoken.size() != 1) {
				error(subtoken, "Unknown subscript.");
				return false;
			}
			switch (subtoken[0]) {
				case 'x': case 'r': sub = 0; break;
				case 'y': case 'g': sub = 1; break;
				case 'z': case 'b': sub = 2; break;
				case 'w': case 'a': sub = 3; break;
				default: error(subtoken, "Unknown subscript"); return false;
			}
			has_subscript = true;
			el.sub = sub;
		}

		switch (type) {
			case VariableFamily::OUTPUT:
				el.output_offset = ctx.emitter->m_outputs[var_index].getOffsetSub(sub);
				el.type = ExpressionStackElement::OUTPUT;
				break;
			case VariableFamily::CHANNEL:
				el.variable_index = var_index;
				el.type = ExpressionStackElement::VARIABLE;
				break;
			case VariableFamily::INPUT:
				el.register_offset = ctx.emitted->m_inputs[var_index].getOffsetSub(sub);
				el.type = ExpressionStackElement::OUTPUT;
				break;
		}

		if (peekToken().type != Token::EQUAL) {
			el.sub = sub;
			m_postfix.push(el);
			return true;
		}
			
		consumeToken();
		if (!can_assign) {
			error(m_tokenizer.m_current_token.value, "Unexpected =.");
			return false;
		}

		if (has_subscript) {
			compileExpression(ctx, sub, parenthesis_depth);
			ASSERT(m_stack.empty());
			m_postfix.push(el);
			m_postfix.emplace().type = ExpressionStackElement::ASSIGN;
			return true;
		}

		Type var_type;
		
		switch (type) {
			case VariableFamily::OUTPUT: var_type = ctx.emitter->m_outputs[var_index].type; break;
			case VariableFamily::CHANNEL: var_type = ctx.emitter->m_vars[var_index].type; break;
			case VariableFamily::INPUT: var_type = ctx.emitted->m_inputs[var_index].type; break;
		}

		u32 num = 0;
		switch (var_type) {
			case Type::FLOAT: num = 1; break;
			case Type::FLOAT3: num = 3; break;
			case Type::FLOAT4: num = 4; break;
		}

		for (u32 i = 0; i < num; ++i) {
			ParticleScriptTokenizer tokenizer = m_tokenizer;
			compileExpression(ctx, i, parenthesis_depth);
			if (i != num - 1) m_tokenizer = tokenizer;
			ASSERT(m_stack.empty());
			
			switch (type) {
				case VariableFamily::OUTPUT: el.output_offset = ctx.emitter->m_outputs[var_index].getOffsetSub(i); break;
				case VariableFamily::CHANNEL: el.variable_index = var_index; break;
				case VariableFamily::INPUT: el.register_offset = ctx.emitted->m_inputs[var_index].getOffsetSub(i); break;
			}
			
			el.sub = i;
			m_postfix.push(el);
			m_postfix.emplace().type = ExpressionStackElement::ASSIGN;
		}
		return true;
	}

	float compileCompoundLiteral(u32 sub) {
		float res = 0;
		for (u32 i = 0;; ++i) {
			Token t = peekToken();
			if (t.type == Token::RIGHT_BRACE) {
				if (i <= sub) error(t.value, "Compound literal is too small.");
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

	ParticleSystemValues getSystemValue(StringView name) {
		if (equalStrings(name, "time_delta")) return ParticleSystemValues::TIME_DELTA;
		if (equalStrings(name, "total_time")) return ParticleSystemValues::TOTAL_TIME;
		// TODO error if emit_index is used outside emit
		if (equalStrings(name, "emit_index")) return ParticleSystemValues::EMIT_INDEX;
		if (equalStrings(name, "ribbon_index")) return ParticleSystemValues::RIBBON_INDEX;
		return ParticleSystemValues::NONE;
	}
	
	Constant* getConstant(StringView name) const {
        for (Constant& c : m_constants) {
            if (equalStrings(c.name, name)) return &c;
        }
        return nullptr;
    }

	i32 getParamIndex(StringView name) const {
		for (const Variable& var : m_params) {
			if (equalStrings(var.name, name)) return i32(&var - m_params.begin());
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


	void compileExpression(const CompileContext& ctx, u32 sub, u32& parenthesis_depth) {
		bool can_be_unary = true;
		bool can_assign = true;

		const i32 start_postfix_size = m_postfix.size();
		const i32 start_stack_size = m_stack.size();
		u32 start_depth = parenthesis_depth;
		for (;;) {
			Token token = peekToken();
			switch (token.type) {
				case Token::ERROR: return;
				case Token::EOF:
					consumeToken();
					error(token.value, "Unexpected end of file");
					return;
				case Token::COMMA:
				case Token::SEMICOLON:
					while (m_stack.size() > start_stack_size) {
						m_postfix.push(m_stack.last());
						m_stack.pop();
					}
					if (m_postfix.size() == start_postfix_size) {
						error(token.value, "Empty expression");
						return;
					}
					return;
				case Token::RIGHT_PAREN:
					if (parenthesis_depth == start_depth) {
						while (m_stack.size() > start_stack_size) {
							m_postfix.push(m_stack.last());
							m_stack.pop();
						}
						if (m_postfix.size() == start_postfix_size) {
							error(token.value, "Empty expression");
							return;
						}
						return;
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
					el.literal_value = compileCompoundLiteral(sub);
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
						if (!consume(Token::LEFT_PAREN)) return;

						if (bfn.instruction == InstructionType::EMIT) {
							StringView emitter_name;
							if (!consume(Token::IDENTIFIER, emitter_name)) return;
							i32 emitter_index = getEmitterIndex(emitter_name);
							if (emitter_index < 0) {
								error(emitter_name, "Unknown emitter");
								return;
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
								if (!compileEmitBlock(ctx, m_emitters[emitter_index])) return;
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
							return;
						}
						
						ExpressionStackElement el;
						el.type = ExpressionStackElement::FUNCTION;
						el.function_desc = bfn;
						el.parenthesis_depth = parenthesis_depth - 1;
						el.sub = sub;
						pushStack(el);
						for (u32 i = 0; i < bfn.num_args; ++i) {
							if (i > 0 && !consume(Token::COMMA)) return;
							compileExpression(ctx, sub, parenthesis_depth);
						}
						if (!consume(Token::RIGHT_PAREN)) return;
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
							if (!compileVariable(ctx, index, VariableFamily::INPUT, parenthesis_depth, sub, can_assign)) return;
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
							if (!compileVariable(ctx, out_index, VariableFamily::OUTPUT, parenthesis_depth, sub, can_assign)) return;
							can_be_unary = false;
							break;
						}

						i32 var_index = getVariableIndex(*ctx.emitter, token.value);
						if (var_index >= 0) {
							if (!compileVariable(ctx, var_index, VariableFamily::CHANNEL, parenthesis_depth, sub, can_assign)) return;
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
						if (!consume(Token::IDENTIFIER, subtoken)) return;
						if (subtoken.size() != 1) {
							error(subtoken, "Unknown subscript.");
							return;
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
						if (equalStrings(token.value, "return")) {
							ExpressionStackElement& el = m_postfix.emplace();
							el.type = ExpressionStackElement::RETURN;
							can_be_unary = true;
							break;
						}

						i32 arg_index = getArgumentIndex(*ctx.function, token.value);
						if (arg_index >= 0) {
							ExpressionStackElement& el = m_postfix.emplace();
							el.type = ExpressionStackElement::FUNCTION_ARGUMENT;
							el.arg_index = arg_index;
							can_be_unary = false;
							break;
						}					
					}
					ASSERT(false);
					break;
				}
				default: ASSERT(false); break;
			}
			can_assign = false;
		}
	}

	static void deallocRegister(Emitter& emitter, const DataStream& stream) {
		if (stream.type == DataStream::REGISTER) {
			emitter.m_register_allocator = emitter.m_register_allocator & ~(1 << stream.index);
		}
	}

	DataStream allocRegister(Emitter& emitter) {
		DataStream res;
		res.type = DataStream::REGISTER;
		for (u32 i = 0; i < 32; ++i) {
			if ((emitter.m_register_allocator & (1 << i)) == 0) {
				res.index = i;
				emitter.m_num_used_registers = maximum(emitter.m_num_used_registers, i + 1);
				emitter.m_register_allocator |= 1 << i;
				return res;
			}
		}
		error(StringView(m_tokenizer.m_current, m_tokenizer.m_document.end), "Run out of allocators");
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
					deallocRegister(*ctx.emitter, arg);
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
						dst.index = ctx.emitter->m_vars[dst_el.variable_index].getOffsetSub(dst_el.sub);
						break;
					case ExpressionStackElement::REGISTER:
						dst.type = DataStream::REGISTER;
						dst.index = dst_el.register_offset;
						break;
					default: ASSERT(false); break;
				}
				compiled.write(dst);
				compiled.write(src);
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
				res.index = ctx.emitter->m_vars[el.variable_index].getOffsetSub(el.sub);
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

				DataStream res = allocRegister(*ctx.emitter);
				compiled.write(res);
				compiled.write(op1);
				compiled.write(op2);

				deallocRegister(*ctx.emitter, op1);
				deallocRegister(*ctx.emitter, op2);
				return res;
			}

			case ExpressionStackElement::NEGATIVE: {
				DataStream cont = compilePostfix(ctx, compiled, postfix);
				if (cont.type == DataStream::LITERAL) {
					cont.value = -cont.value;
					return cont;
				}
				DataStream res = allocRegister(*ctx.emitter);
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
					compiled.write(u32(emitter_index.index));
					return {};
				}

				if (el.function_desc.instruction == InstructionType::RAND) {
					DataStream res = allocRegister(*ctx.emitter);
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
					DataStream res = allocRegister(*ctx.emitter);
					if (m_mesh_stream.type == DataStream::NONE) {
						m_mesh_stream = allocRegister(*ctx.emitter);
					}
					compiled.write(el.function_desc.instruction);
					compiled.write(res);
					compiled.write(m_mesh_stream);
					compiled.write(el.sub);
					return res;
				}
			
				if (el.function_desc.instruction == InstructionType::GRADIENT) {
					DataStream res = allocRegister(*ctx.emitter);
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
					deallocRegister(*ctx.emitter, t);
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
					res = allocRegister(*ctx.emitter);
					compiled.write(res);
				}
				for (u32 i = 0; i < el.function_desc.num_args; ++i) {
					compiled.write(ops[i]);
					deallocRegister(*ctx.emitter, ops[i]);
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

		ASSERT(m_postfix.empty());

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
				default: {
					u32 depth = 0;
					compileExpression({.emitter = &emitter}, 0, depth);
					if (!consume(Token::SEMICOLON)) return;
					flushCompile(ctx, compiled);
					break;
				}
			}
		}
	}

	void compileFunction() {
		ASSERT(m_stack.empty());
		ASSERT(m_postfix.empty());

		Function& fn = m_functions.emplace(m_allocator);
		if (!consume(Token::IDENTIFIER, fn.name)) return;
		parseArgs(fn);
		consume(Token::LEFT_BRACE);

		for (;;) {
			Token t = consumeToken();
			switch (t.type) {
				case Token::ERROR: return;
				case Token::EOF: error(t.value, "Unexpected end of file."); return;
				case Token::RIGHT_BRACE:
					m_postfix.copyTo(fn.expressions);
					m_postfix.clear();
					return;
				default:
					u32 parenthesis_depth = 0;
					compileExpression({.function = &fn}, 0, parenthesis_depth);
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

	Type parseType() {
		StringView type;
		if (!consume(Token::IDENTIFIER, type)) return Type::FLOAT;
		if (equalStrings(type, "float")) return Type::FLOAT;
		if (equalStrings(type, "float3")) return Type::FLOAT3;
		if (equalStrings(type, "float4")) return Type::FLOAT4;
		error(type, "Unknown type");
		return Type::FLOAT;
	}


	void parseVariableDeclaration(Array<Variable>& vars) {
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

	void compileEmitter() {
		Emitter& emitter = m_emitters.emplace(m_allocator);
		if (!consume(Token::IDENTIFIER, emitter.m_name)) return;
		if (!consume(Token::LEFT_BRACE)) return;

		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::ERROR: return;
				case Token::FN: compileFunction(emitter); break;
				case Token::VAR: parseVariableDeclaration(emitter.m_vars); break;
				case Token::OUT: parseVariableDeclaration(emitter.m_outputs); break;
				case Token::IN: parseVariableDeclaration(emitter.m_inputs); break;
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
						error(token.value, "Unknown identifier");
						return;
					}
					break;
				default:
					error(token.value, "Unexpected token.");
					return;
			}
		}
		
	}

	bool compile(OutputMemoryStream& output) {
		// TODO error reporting
		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::EOF: goto write_label;
				case Token::ERROR: return false;
				case Token::CONST: compileConst(); break;
				case Token::FN: compileFunction(); break;
				case Token::PARAM: parseVariableDeclaration(m_params); break;
				case Token::EMITTER: compileEmitter(); break;
				case Token::WORLD_SPACE: m_is_world_space = true; break;
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
	bool expectNotEOF() {
		skipWhitespaces();
		if (m_content.size() == 0) {
			error(m_content, "Unexpected end of file");
			return false;
		}
		return true;
	}

	StringView tryReadWord() {
		StringView res = m_content;
		res.end = res.begin;
		if (res.end != m_content.end) {
			++res.end;
			if (!isDigit(*res.begin) && !isLetter(*res.begin) && *res.begin != '_') {
				m_content.begin = res.end;
				return res;
			}
		}
		while (res.end != m_content.end && (isDigit(*res.end) || isLetter(*res.end) || *res.end == '_')) ++res.end;
		m_content.begin = res.end;
		return res;
	}

	StringView readWord() {
		expectNotEOF();
		StringView w = tryReadWord();
		if (w.size() == 0) error(w, "Expected a word"); 
		return w;
	}

	void expect(const char* v) {
		StringView w = readWord();
		if (!equalStrings(w, v)) error(w, "Expected ", v);
	}

	StringView readIdentifier() {
		expectNotEOF();
		StringView res = m_content;
		res.end = res.begin;
		if (!isLetter(*res.begin) && *res.begin != '_') error(res, "Identifier must start with a letter or _"); 
		++res.end;
		while (res.end != m_content.end && (isDigit(*res.end) || isLetter(*res.end) || *res.end == '_')) {
			 ++res.end;
		}
		m_content.begin = res.end;
		return res;
	}

	float readNumberOrSub(u32 sub) {
		expectNotEOF();
        if (peekChar() != '{') return readNumber();
        ++m_content.begin;
        skipWhitespaces();
        float vals[4] = {};
        vals[0] = readNumber();
        expect(",");
        vals[1] = readNumber();
		expectNotEOF();
        if (peekChar() == '}') {
            if (sub > 1) error (m_content, "Expected ,");
        }
        else {
            expect(",");
            vals[2] = readNumber();
            if (peekChar() == '}') {
                if (sub > 2) error (m_content, "Expected ,");
            }
            else {
                expect(",");
                vals[3] = readNumber();
            }
        }
        expect("}");

        return vals[sub];
    }

	float readNumber() {
		expectNotEOF();
		
        bool is_negative = false;
		if (*m_content.begin == '-') {
            is_negative = true;
            ++m_content.begin;
            if (m_content.size() == 0) {
                error(m_content, "Expected a number");
                return 0;
            }
        }
        if (!isDigit(*m_content.begin)) {
            StringView ident = readIdentifier();
            Constant* c = getConstant(ident);
            if (c) {
                return c->value[0] * (is_negative ? -1 : 1);
            }
            else {
                error(ident, "Expected a number");
            }
            return 0;
        }
		
		StringView s;
		s.begin = m_content.begin;
		s.end = s.begin + 1;
		while (s.end != m_content.end && isDigit(*s.end)) ++s.end;
		if (s.end == m_content.end || *s.end != '.') {
			m_content.begin = s.end;
			float v;
			fromCString(s, v);
            if (is_negative) v = -v;
	 		return v;
		}
		++s.end;
		while (s.end != m_content.end && isDigit(*s.end)) ++s.end;
		m_content.begin = s.end;
		float v;
		fromCString(s, v);
        if (is_negative) v = -v;
		return v;
	}

	bool isOperator(char c) { 
		switch ((Operators)c) {
			case Operators::ADD: case Operators::SUB:
			case Operators::MUL: case Operators::DIV:
			case Operators::LT: case Operators::GT:
			case Operators::MOD:
				return true;
		}
		return false;
	}

	void compileAssignment(Emitter& emitter, Variable& variable, DataStream::Type stream_type, i32 index) {
		u32 parenthesis_depth = 0;
		switch (variable.type) {
			case Type::FLOAT: {
				expect("=");
				compileExpression({.emitter = &emitter}, 0, parenthesis_depth);
				ExpressionStackElement& dst = m_postfix.emplace();
				switch (stream_type) {
					case DataStream::CHANNEL:
						dst.type = ExpressionStackElement::VARIABLE;
						dst.variable_index = index;
						break;
					case DataStream::REGISTER:
						dst.type = ExpressionStackElement::REGISTER;
						ASSERT(false);
						//dst.register_offset = index;
						break;
					case DataStream::OUT:
						dst.type = ExpressionStackElement::OUTPUT;
						dst.output_offset = variable.getOffsetSub(0);
						break;
					default:
						ASSERT(false);
						break;
				}
				ExpressionStackElement& el = m_postfix.emplace();
				el.type = ExpressionStackElement::ASSIGN;
				break;
			}
			case Type::FLOAT4:
			case Type::FLOAT3: {
				const bool is_float4 = variable.type == Type::FLOAT4;
				expectNotEOF();
				if (peekChar() == '.') {
					++m_content.begin;
					u32 sub = 0;
					StringView sub_word = readWord();
					if (equalStrings(sub_word, "y")) sub = 1;
					else if (equalStrings(sub_word, "z")) sub = 2; 
					else if (equalStrings(sub_word, "g")) sub = 1; 
					else if (equalStrings(sub_word, "b")) sub = 2; 
					else if (is_float4) {
						if (equalStrings(sub_word, "w")) sub = 3; 
						else if (equalStrings(sub_word, "a")) sub = 3; 
					} 

					expect("=");
					compileExpression({.emitter = &emitter}, 0, parenthesis_depth);
					ExpressionStackElement& dst = m_postfix.emplace();
					switch (stream_type) {
						case DataStream::CHANNEL:
							dst.type = ExpressionStackElement::VARIABLE;
							dst.variable_index = index;
							break;
						case DataStream::REGISTER:
							dst.type = ExpressionStackElement::REGISTER;
							ASSERT(false);
							//dst.register_offset = index;
							break;
						case DataStream::OUT:
							dst.type = ExpressionStackElement::OUTPUT;
							dst.output_offset = variable.getOffsetSub(sub);
							break;
						default:
							ASSERT(false);
							break;
					}
					dst.sub = sub;
					ExpressionStackElement& el = m_postfix.emplace();
					el.type = ExpressionStackElement::ASSIGN;
				}
				else {
					expect("=");
					for (i32 i = 0; i < (is_float4 ? 4 : 3); ++i) {
						StringView content = m_content;
						/*DataStream expr_result = */compileExpression({.emitter = &emitter}, i, parenthesis_depth);
						ExpressionStackElement& dst = m_postfix.emplace();
						switch (stream_type) {
							case DataStream::CHANNEL:
								dst.type = ExpressionStackElement::VARIABLE;
								dst.variable_index = index;
								break;
							case DataStream::REGISTER:
								dst.type = ExpressionStackElement::REGISTER;
								ASSERT(false);
								//dst.register_offset = index;
								break;
							case DataStream::OUT:
								dst.type = ExpressionStackElement::OUTPUT;
								dst.output_offset = variable.getOffsetSub(i);
								break;
							default:
								ASSERT(false);
								break;
						}
						ExpressionStackElement& el = m_postfix.emplace();
						el.type = ExpressionStackElement::ASSIGN;
						dst.sub = i;
						if (i < (is_float4 ? 3 : 2)) m_content = content;
					}
				}
				break;
			}
		}
	}

	// TODO statements are executed in reverse order
	bool compileEmitBlock(Emitter& emitter, Emitter& emitted) {
		for (;;) {
			StringView word = readWord();
			if (equalStrings(word, "}")) break;

			i32 out_index = getInputIndex(emitted, word);
			if (out_index >= 0) {
				compileAssignment(emitter, emitted.m_inputs[out_index], DataStream::OUT, out_index);
				continue;
			}

			error(word, "Syntax error");
			return false;
		}
		return true;
	}

#endif
	IAllocator& m_allocator;
	Path m_path;
	Array<Emitter> m_emitters;
	Array<Constant> m_constants;
	Array<Variable> m_params;
	Array<Function> m_functions;
	StackArray<ExpressionStackElement, 16> m_stack;
	StackArray<ExpressionStackElement, 16> m_postfix;
	DataStream m_mesh_stream;
	
	ParticleScriptTokenizer m_tokenizer;
    const char* m_text_begin;
	bool m_is_error = false;
	bool m_is_world_space = false;
};

}