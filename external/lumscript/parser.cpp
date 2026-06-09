#include "compiler.h"
#include "tokenizer.h"
#include "utils.h"

struct NewPlaceholder {};
inline void* operator new(size_t, NewPlaceholder, void* where) { return where; }
inline void operator delete(void*, NewPlaceholder,  void*) { } 

struct Parser {
	Parser(Unit& unit, const ls_host* host)
		: m_unit(unit)
	{
		m_output.host = host;
	}

	template <typename T, typename... Args>
	T* make(Args&&... args) {
		ls_arena& a = *m_unit.arena.arena;
		T* res = static_cast<T*>(a.allocate(a.user_data, sizeof(T), alignof(T)));
		new (NewPlaceholder(), res) T(static_cast<Args&&>(args)...);
		return res;
	}

	static int precedence(Token::Type type) {
		switch (type) {
			case Token::OR: return 1;
			case Token::AND: return 2;
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL: return 3;
			case Token::GT:
			case Token::LT:
			case Token::GT_EQUAL:
			case Token::LT_EQUAL: return 4;
			case Token::PLUS:
			case Token::MINUS: return 5;
			case Token::STAR:
			case Token::SLASH:
			case Token::PERCENT: return 6;
			default: return 0;
		}
	}

	static const char* toString(Token::Type type) {
		switch (type) {
			case Token::END_OF_FILE: return "end of file";
			case Token::ERROR: return "error";
			case Token::IDENTIFIER: return "identifier";
			case Token::NUMBER: return "number";
			case Token::STRING: return "string";
			case Token::LEFT_PAREN: return "(";
			case Token::RIGHT_PAREN: return ")";
			case Token::LEFT_BRACE: return "{";
			case Token::RIGHT_BRACE: return "}";
			case Token::LEFT_BRACKET: return "[";
			case Token::RIGHT_BRACKET: return "]";
			case Token::SEMICOLON: return ";";
			case Token::COLON: return ":";
			case Token::COMMA: return ",";
			case Token::DOT: return ".";
			case Token::RANGE: return "..";
			case Token::QUESTION: return "?";
			case Token::PLUS: return "+";
			case Token::MINUS: return "-";
			case Token::STAR: return "*";
			case Token::SLASH: return "/";
			case Token::PERCENT: return "%";
			case Token::EQUAL: return "=";
			case Token::PLUS_EQUAL: return "+=";
			case Token::MINUS_EQUAL: return "-=";
			case Token::STAR_EQUAL: return "*=";
			case Token::SLASH_EQUAL: return "/=";
			case Token::PLUS_PLUS: return "++";
			case Token::MINUS_MINUS: return "--";
			case Token::EQUAL_EQUAL: return "==";
			case Token::BANG_EQUAL: return "!=";
			case Token::GT: return ">";
			case Token::LT: return "<";
			case Token::GT_EQUAL: return ">=";
			case Token::LT_EQUAL: return "<=";
			case Token::EXTERN: return "extern";
			case Token::OPERATOR: return "operator";
			case Token::STRUCT: return "struct";
			case Token::ENUM: return "enum";
			case Token::FN: return "fn";
			case Token::VAR: return "var";
			case Token::CONST: return "const";
			case Token::DEFER: return "defer";
			case Token::RETURN: return "return";
			case Token::REF: return "ref";
			case Token::WHILE: return "while";
			case Token::FOR: return "for";
			case Token::IF: return "if";
			case Token::ELSE: return "else";
			case Token::IMPORT: return "import";
			case Token::MATCH: return "match";
			case Token::CASE: return "case";
			case Token::BREAK: return "break";
			case Token::CONTINUE: return "continue";
			case Token::AS: return "as";
			case Token::TRUE: return "true";
			case Token::FALSE: return "false";
			case Token::NULL_KW: return "null";
			case Token::AND: return "and";
			case Token::OR: return "or";
			case Token::NOT: return "not";
			case Token::VOID: return "void";
			case Token::STRING_KW: return "string";
			case Token::I8: return "i8";
			case Token::BOOL: return "bool";
			case Token::I16: return "i16";
			case Token::I32: return "i32";
			case Token::I64: return "i64";
			case Token::U8: return "u8";
			case Token::U16: return "u16";
			case Token::U32: return "u32";
			case Token::U64: return "u64";
			case Token::F32: return "f32";
			case Token::F64: return "f64";
			case Token::CPTR: return "cptr";
			case Token::COMPTIME: return "comptime";
			default: ASSERT(false); return "Unknown";
		}
	}

	Token peekToken() { return m_tokenizer.peekToken(); }

	Token consumeToken() { return m_tokenizer.consumeToken(); }

	bool consume(Token::Type type) {
		Token token = consumeToken();
		if (token.type != type) {
			m_output.errorAt(token, "Expected ", toString(type));
			return false;
		}
		return true;
	}

	bool consume(Token::Type type, ls_string_view& value, const char* error_msg) {
		Token token = consumeToken();
		if (token.type != type) {
			m_output.errorAt(token, makeStringView(error_msg));
			return false;
		}
		value = token.value;
		return true;
	}



	// Parse an atom, i.e. one side of binary expression (even if possibly not in binary expression).
	Expression* atom() {
		Token token = consumeToken();
		switch (token.type) {
			case Token::END_OF_FILE:
				m_output.error("Unexpected end of file");
				return nullptr;
			case Token::FN: return functionExpression();
			case Token::IDENTIFIER: {
				IdentifierExpression* expr = make<IdentifierExpression>();
				expr->name = token.value;
				return expr;
			}
			case Token::NUMBER: {
				if (contains(token.value, '.') || contains(token.value, 'e') || contains(token.value, 'E')) {
					double value = 0.0;
					fromCString(token.value, value);
					FloatLiteralExpression* expr = make<FloatLiteralExpression>();
					expr->value = value;
					return expr;
				}

				i32 value = 0;
				fromCString(token.value, value);
				IntLiteralExpression* expr = make<IntLiteralExpression>();
				expr->value = value;
				return expr;
			}
			case Token::TRUE:
			case Token::FALSE:
				return make<BoolLiteralExpression>(token.type == Token::TRUE);
			case Token::STRING: {
				StringLiteralExpression* expr = make<StringLiteralExpression>();
				expr->value = token.value;
				return expr;
			}
		}
		m_output.errorAt(token, "Expected expression");
		return nullptr;
	}

	ParsedType* type() {
		Token token = consumeToken();
		ParsedType* res = make<ParsedType>();
		if (token.type == Token::QUESTION) {
			res->is_nullable = true;
			token = consumeToken();
		}
		switch (token.type) {
			case Token::VOID: res->kind = ParsedType::VOID; return res;
			case Token::I32: res->kind = ParsedType::I32; return res;
			default: break;
		}
		m_output.errorAt(token, "Expected type");
		return nullptr;
	}

	// Parse a binary expression, e.g. `a + b` or `x * y`. 
	// Recursive, so a * b + c is parsed as two binary expressions and 3 atoms.
	Expression* binaryExpression(int min_precedence) {
		Expression* lhs = atom();
		if (!lhs) return nullptr;

		for (;;) {
			Token op = peekToken();
			int prec = precedence(op.type);
			if (prec < min_precedence) return lhs;

			consumeToken();
			Expression* rhs = binaryExpression(prec + 1);
			if (!rhs) return nullptr;

			ls_arena& a = *m_unit.arena.arena;
			BinaryExpression* bin = make<BinaryExpression>();
			bin->lhs = lhs;
			bin->rhs = rhs;
			bin->op = op.type;
			lhs = bin;
		}
	}

	// Parse an expression, e.g. `a + b * c`.
	Expression* expression() {
		return binaryExpression(1);
	}

	// Parse a comptime block, e.g. `comptime { ... }`.
	bool comptime() {
		Symbol sym;
		sym.storage = Symbol::COMPTIME;

		if (!consume(Token::IDENTIFIER, sym.name, "Expected identifier")) return false;

		if (peekToken().type == Token::COLON) {
			consumeToken();
			sym.parsed_type = type();
			if (!sym.parsed_type) return false;
		}

		if (!consume(Token::EQUAL)) return false;

		sym.expression = expression();
		
		if (!sym.expression) return false;
		if (!consume(Token::SEMICOLON)) return false;

		m_unit.symbols.push_back(sym);
		return true;
	}

	VarDeclStatement* varDecl(Token::Type token) {
		VarDeclStatement* res = make<VarDeclStatement>();
		res->is_immutable = token == Token::CONST;
		if (!consume(Token::IDENTIFIER, res->name, "Expected identifier")) return nullptr;
		if (peekToken().type == Token::COLON) {
			consumeToken();
			res->parsed_type = type();
			if (!res->parsed_type) return nullptr;
		}
		if (!consume(Token::EQUAL)) return nullptr;
		res->expression = expression();
		if (!res->expression) return nullptr;
		if (!consume(Token::SEMICOLON)) return nullptr;
		return res;
	}

	ReturnStatement* returnStatement() {
		ReturnStatement* res = make<ReturnStatement>();
		if (peekToken().type != Token::SEMICOLON) {
			res->expression = expression();
			if (!res->expression) return nullptr;
		}
		if (!consume(Token::SEMICOLON)) return nullptr;
		return res;
	}

	Statement* statement() {
		Token token = consumeToken();
		switch (token.type) {
			case Token::END_OF_FILE:
				m_output.error("Unexpected end of file");
				return nullptr;
			case Token::CONST:
			case Token::VAR: return varDecl(token.type);
			case Token::RETURN: return returnStatement();
			case Token::IDENTIFIER: {
				Token op = peekToken();
				switch (op.type) {
					case Token::EQUAL:
					case Token::PLUS_EQUAL:
					case Token::MINUS_EQUAL:
					case Token::STAR_EQUAL:
					case Token::SLASH_EQUAL:
						break;
					default:
						m_output.errorAt(op, "Expected assignment operator");
						return nullptr;
				}

				consumeToken();

				AssignStatement* res = make<AssignStatement>();
				IdentifierExpression* lhs = make<IdentifierExpression>();
				lhs->name = token.value;
				res->lhs = lhs;
				res->op = op.type;
				res->rhs = expression();
				if (!res->rhs) return nullptr;
				if (!consume(Token::SEMICOLON)) return nullptr;
				return res;
			}
			default:
				m_output.errorAt(token, "Unexpected ", toString(token.type));
				return nullptr;
		}
		return nullptr;
	}

	BlockStatement* blockStatement() {
		if (!consume(Token::LEFT_BRACE)) return nullptr;
		BlockStatement* res = make<BlockStatement>(m_unit.arena);
		for (;;) {
			switch (peekToken().type) {
				case Token::END_OF_FILE:
					m_output.error("Unexpected end of file");
					return nullptr;
				case Token::RIGHT_BRACE: consume(Token::RIGHT_BRACE); return res;
				default: {
					Statement* child = statement();
					if (!child) return nullptr;
					res->statements.push(child);
					break;
				}
			}
		}
		return res;
	}

	FunctionExpression* functionExpression() {
		if (!consume(Token::LEFT_PAREN)) return nullptr;
		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		if (!consume(Token::RIGHT_PAREN)) return nullptr;
		if (!consume(Token::COLON)) return nullptr;
		fn->return_type = type();
		if (!fn->return_type) return nullptr;
		fn->body = blockStatement();
		if (!fn->body) return nullptr;
		return fn;
	}

	// Parse a function declaration, e.g. `fn foo(a) { }`.
	bool functionDecl() {
		ls_string_view name;
		if (!consume(Token::IDENTIFIER, name, "Expected function name")) return false;
		
		FunctionExpression* fn = functionExpression();
		if (!fn) return false;

		Symbol& s = m_unit.symbols.emplace_back();
		s.name = name;
		s.expression = fn;
		s.storage = Symbol::COMPTIME;

		return true;
	}

	// Parse a global declaration, e.g. `var x = 1;`.
	bool globalDecl(Token::Type token) {
		Symbol sym;
		sym.storage = token == Token::CONST ? Symbol::CONST : Symbol::VARIABLE;

		if (!consume(Token::IDENTIFIER, sym.name, "Expected identifier")) return false;

		if (peekToken().type == Token::COLON) {
			consumeToken();
			sym.parsed_type = type();
			if (!sym.parsed_type) return false;
		}

		if (!consume(Token::EQUAL)) return false;

		sym.expression = expression();
		
		if (!sym.expression) return false;
		if (!consume(Token::SEMICOLON)) return false;

		m_unit.symbols.push_back(sym);
		return true;
	}

	// Parse a source file, e.g. a whole `.ls` script.
	ls_result parse(ls_string_view source, ls_string_view source_name) {
		m_tokenizer.init(source, source_name);

		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::END_OF_FILE: return LS_RESULT_OK;
				case Token::CONST:
				case Token::VAR: if (!globalDecl(token.type)) return LS_RESULT_FAILURE;
				case Token::FN: functionDecl(); break;
				//case Token::STRUCT: structDecl(); break;
				//case Token::ENUM: enumDecl(); break;
				case Token::COMPTIME: if (!comptime()) return LS_RESULT_FAILURE; break;
				default: 
					m_output.errorAt(token, "Unexpected ", toString(token.type));
					return LS_RESULT_FAILURE;
			}
		}

	}

	Unit& m_unit;
	Tokenizer m_tokenizer;
	OutputFormatter m_output;
};

// Parse a module, e.g. `ls_module_parse(module, source, name)`.
ls_result ls_module_parse(ls_module* module, ls_string_view source, ls_string_view source_name) {
	Unit& unit = module->units.emplace_back(source_name, module->host);
	Parser parser(unit, module->host);
	if (parser.parse(source, source_name) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;

	return LS_RESULT_OK;
}


void OutputFormatter::print(int v) {
	char tmp[32];
	toCString(v, tmp, sizeof(tmp));
	print(tmp);
}
