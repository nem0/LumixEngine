#include "compiler.h"
#include "tokenizer.h"
#include "utils.h"

enum class ExprMode {
	FULL,
	HEAD,
};

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
		new (res) T(static_cast<Args&&>(args)...);
		return res;
	}

	template <typename T, typename... Args>
	T* makeExpr(Token token, Args&&... args) {
		T* res = make<T>(static_cast<Args&&>(args)...);
		res->token = token;
		return res;
	}

	template <typename T, typename... Args>
	T* makeParsedType(Token token, Args&&... args) {
		T* res = make<T>(static_cast<Args&&>(args)...);
		res->token = token;
		return res;
	}

	template <typename T, typename... Args>
	T* makeStmt(Token token, Args&&... args) {
		T* res = make<T>(static_cast<Args&&>(args)...);
		res->token = token;
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
			case Token::DOLLAR: return "$";
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
			case Token::UNDEFINED: return "undefined";
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
			case Token::ISIZE: return "isize";
			case Token::F32: return "f32";
			case Token::F64: return "f64";
			case Token::CPTR: return "cptr";
			case Token::BYTE: return "byte";
			case Token::SIZEOF: return "sizeof";
			case Token::ALIGNOF: return "alignof";
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

	// Push a top-level symbol. Operator overloads may share a name with other
	// overloads of the same operator; all other symbols reject redeclarations.
	bool addSymbol(const Symbol& sym) {
		ASSERT(sym.expression || sym.storage == Symbol::IMPORT);
		if (!isOperatorSymbol(sym.name)) {
			for (const Symbol& other : m_unit.symbols) {
				if (!equalStrings(other.name, sym.name)) continue;
				m_output.errorAt(sym.token, "Duplicate declaration: ", sym.name);
				return false;
			}
		}
		m_unit.symbols.push_back(sym);
		return true;
	}

	Expression* numberLiteral(Token token) {
		if (contains(token.value, '.') || contains(token.value, 'e') || contains(token.value, 'E')) {
			double value = 0.0;
			fromCString(token.value, value);
			FloatLiteralExpression* expr = makeExpr<FloatLiteralExpression>(token);
			expr->value = value;
			return expr;
		}
		u64 value = 0;
		if (!fromCString(token.value, value)) {
			m_output.errorAt(token, "Integer literal does not fit in u64");
			return nullptr;
		}
		IntLiteralExpression* expr = makeExpr<IntLiteralExpression>(token);
		expr->value = value;
		return expr;
	}

	bool symbolDecl(Symbol::Storage storage) {
		Symbol sym;
		sym.storage = storage;

		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) {
			m_output.errorAt(name_token, "Expected identifier");
			return false;
		}
		sym.name = name_token.value;
		sym.token = name_token;

		if (peekToken().type == Token::COLON) {
			consumeToken();
			sym.parsed_type = type();
			if (!sym.parsed_type) return false;
		}

		if (!consume(Token::EQUAL)) return false;

		sym.expression = expression();
		if (!sym.expression) return false;
		if (storage != Symbol::COMPTIME) {
			if (!consume(Token::SEMICOLON)) return false;
		}
		else if (peekToken().type == Token::SEMICOLON) {
			consumeToken();
		}

		return addSymbol(sym);
	}

	bool namedComptimeDecl(Token name_token, Expression* expression) {
		Symbol sym;
		sym.name = name_token.value;
		sym.token = name_token;
		sym.expression = expression;
		sym.storage = Symbol::COMPTIME;
		return addSymbol(sym);
	}

	// Parse a primary expression, i.e. a syntactic starting point before postfix chaining.
	Expression* primaryExpression(ExprMode mode) {
		Token token = consumeToken();
		switch (token.type) {
			case Token::END_OF_FILE:
				m_output.error("Unexpected end of file");
				return nullptr;
			case Token::DOT: {
				Token name = consumeToken();
				if (name.type != Token::IDENTIFIER) {
					m_output.errorAt(name, "Expected identifier");
					return nullptr;
				}
				MemberExpression* expr = makeExpr<MemberExpression>(token);
				expr->name = name.value;
				return expr;
			}
			case Token::FN: {
				FunctionExpression* expr = functionExpression();
				if (expr) expr->token = token;
				return expr;
			}
			case Token::STRUCT: {
				StructExpression* expr = structExpression();
				if (expr) expr->token = token;
				return expr;
			}
			case Token::ENUM: {
				EnumExpression* expr = enumExpression();
				if (expr) expr->token = token;
				return expr;
			}
			case Token::NULL_KW: return makeExpr<NullLiteralExpression>(token);
			case Token::UNDEFINED: return makeExpr<UndefinedExpression>(token);
			case Token::VOID:
			case Token::BOOL:
			case Token::I8:
			case Token::I16:
			case Token::I32:
			case Token::I64:
			case Token::U8:
			case Token::U16:
			case Token::U32:
			case Token::U64:
			case Token::ISIZE:
			case Token::F32:
			case Token::F64:
			case Token::STRING_KW:
			case Token::CPTR:
			case Token::BYTE:
			case Token::TYPE_KW:
				return makeExpr<TypeLiteralExpression>(token, typeFromToken(token)->kind);
			case Token::SIZEOF:
			case Token::ALIGNOF: {
				SizeofExpression* expr = makeExpr<SizeofExpression>(token);
				expr->is_align = token.type == Token::ALIGNOF;
				if (!consume(Token::LEFT_PAREN)) return nullptr;
				expr->type = type();
				if (!expr->type) return nullptr;
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				return expr;
			}
			case Token::DOLLAR: {
				GenericIdentifierExpression* expr = makeExpr<GenericIdentifierExpression>(token);
				if (!consume(Token::IDENTIFIER, expr->name, "Expected identifier")) return nullptr;;
				return expr;
			}
			case Token::IDENTIFIER: {
				IdentifierExpression* expr = makeExpr<IdentifierExpression>(token);
				expr->name = token.value;
				return expr;
			}
			case Token::LEFT_BRACE:
				if (mode == ExprMode::HEAD) {
					m_output.errorAt(token, "Expected expression");
					return nullptr;
				}
				return structLiteralBody(nullptr, token);
			case Token::LEFT_PAREN: {
				Expression* expr = expression();
				if (!expr) return nullptr;
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				return expr;
			}
			case Token::NUMBER: {
				return numberLiteral(token);
			}
			case Token::TRUE:
			case Token::FALSE:
				return makeExpr<BoolLiteralExpression>(token, token.type == Token::TRUE);
			case Token::STRING: {
				StringLiteralExpression* expr = makeExpr<StringLiteralExpression>(token);
				expr->value = token.value;
				return expr;
			}
		}
		m_output.errorAt(token, "Expected expression");
		return nullptr;
	}

	Expression* postfixSuffixes(Expression* expr, ExprMode mode) {
		for (;;) {
			switch (peekToken().type) {
				case Token::DOT: {
					Token dot = consumeToken();
					Token name = consumeToken();
					if (name.type != Token::IDENTIFIER) {
						m_output.errorAt(name, "Expected identifier");
						return nullptr;
					}

					MemberExpression* member = makeExpr<MemberExpression>(dot);
					member->expression = expr;
					member->name = name.value;
					expr = member;
					break;
				}
				case Token::LEFT_BRACKET: {
					Token bracket = consumeToken();
					Expression* first = nullptr;
					if (peekToken().type != Token::COLON && peekToken().type != Token::RIGHT_BRACKET) {
						first = expression();
						if (!first) return nullptr;
					}

					// A colon makes it a slice; either bound may be omitted.
					if (peekToken().type == Token::COLON) {
						consumeToken();
						SliceExpression* slice = makeExpr<SliceExpression>(bracket);
						slice->base = expr;
						slice->begin = first;
						if (peekToken().type != Token::RIGHT_BRACKET) {
							slice->end = expression();
							if (!slice->end) return nullptr;
						}
						if (!consume(Token::RIGHT_BRACKET)) return nullptr;
						expr = slice;
						break;
					}

					// Indexing or template instantiation; disambiguated at check time.
					BracketExpression* br = makeExpr<BracketExpression>(bracket, m_unit.arena);
					br->base = expr;
					if (first) {
						br->args.push(first);
						while (peekToken().type == Token::COMMA) {
							consumeToken();
							Expression* arg = expression();
							if (!arg) return nullptr;
							br->args.push(arg);
						}
					}

					if (!consume(Token::RIGHT_BRACKET)) return nullptr;
					expr = br;
					break;
				}
				case Token::LEFT_PAREN: {
					Token paren = consumeToken();
					CallExpression* call = makeExpr<CallExpression>(paren, m_unit.arena);
					call->callee = expr;
					while (peekToken().type != Token::RIGHT_PAREN) {
						if (peekToken().type == Token::END_OF_FILE) {
							m_output.error("Unexpected end of file");
							return nullptr;
						}

						Expression* arg = expression();
						if (!arg) return nullptr;
						call->args.push(arg);
						if (peekToken().type != Token::COMMA) break;
						consumeToken();
					}
					if (!consume(Token::RIGHT_PAREN)) return nullptr;
					expr = call;
					break;
				}
				case Token::LEFT_BRACE:
					if (mode == ExprMode::HEAD) return expr;
					expr = structLiteral(expr);
					if (!expr) return nullptr;
					break;
				default:
					return expr;
			}
		}
	}

	StructLiteralExpression* structLiteralBody(Expression* type, Token start_token = {}) {
		StructLiteralExpression* res = makeExpr<StructLiteralExpression>(start_token, m_unit.arena);
		res->type = type;
		while (peekToken().type != Token::RIGHT_BRACE) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				return nullptr;
			}

			Expression* value = expression();
			if (!value) return nullptr;
			res->values.push(value);
			if (peekToken().type != Token::COMMA) break;
			consumeToken();
		}
		if (!consume(Token::RIGHT_BRACE)) return nullptr;
		return res;
	}

	StructLiteralExpression* structLiteral(Expression* type) {
		Token brace = consumeToken();
		return structLiteralBody(type, brace);
	}

	Expression* unaryExpression(ExprMode mode) {
		Token token = peekToken();
		switch (token.type) {
			case Token::MINUS:
			case Token::NOT:
			case Token::REF: {
				consumeToken();
				UnaryExpression* expr = makeExpr<UnaryExpression>(token);
				expr->op = token.type;
				expr->expression = unaryExpression(mode);
				if (!expr->expression) return nullptr;
				return expr;
			}
			default:
				Expression* expr = primaryExpression(mode);
				if (!expr) return nullptr;
				return postfixSuffixes(expr, mode);
		}
	}

	Expression* castExpression(ExprMode mode) {
		Expression* expr = unaryExpression(mode);
		if (!expr) return nullptr;

		while (peekToken().type == Token::AS) {
			Token as_token = consumeToken();
			CastExpression* cast = makeExpr<CastExpression>(as_token);
			cast->expression = expr;
			cast->parsed_type = type();
			if (!cast->parsed_type) return nullptr;
			expr = cast;
		}

		return expr;
	}

	bool templateParams(ExpArray<NamedDecl>& params) {
		if (peekToken().type != Token::LEFT_BRACKET) return true;
		consumeToken();
		if (peekToken().type == Token::RIGHT_BRACKET) {
			m_output.error("Template parameter list can not be empty");
			return false;
		}
		while (peekToken().type != Token::RIGHT_BRACKET) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				return false;
			}

			NamedDecl& param = params.emplace_back();
			if (!consume(Token::IDENTIFIER, param.name, "Expected parameter name")) return false;
			for (i32 i = 0; i < params.size() - 1; ++i) {
				if (!equalStrings(params[i].name, param.name)) continue;
				m_output.error("Duplicate template parameter: ", param.name);
				return false;
			}
			if (peekToken().type == Token::COLON) {
				consumeToken();
				param.parsed_type = type();
				if (!param.parsed_type) return false;
			}
			if (peekToken().type != Token::COMMA) break;
			consumeToken();
		}
		if (!consume(Token::RIGHT_BRACKET)) return false;
		return true;
	}

	ParsedType* typeFromToken(Token token) {
		ParsedType* res = nullptr;
		switch (token.type) {
			case Token::STRING_KW: res = makeParsedType<ParsedType>(token, ParsedType::STRING); break;
			case Token::VOID: res = makeParsedType<ParsedType>(token, ParsedType::VOID); break;
			case Token::BOOL: res = makeParsedType<ParsedType>(token, ParsedType::BOOL); break;
			case Token::I8: res = makeParsedType<ParsedType>(token, ParsedType::I8); break;
			case Token::I16: res = makeParsedType<ParsedType>(token, ParsedType::I16); break;
			case Token::I32: res = makeParsedType<ParsedType>(token, ParsedType::I32); break;
			case Token::I64: res = makeParsedType<ParsedType>(token, ParsedType::I64); break;
			case Token::U8: res = makeParsedType<ParsedType>(token, ParsedType::U8); break;
			case Token::U16: res = makeParsedType<ParsedType>(token, ParsedType::U16); break;
			case Token::U32: res = makeParsedType<ParsedType>(token, ParsedType::U32); break;
			case Token::U64: res = makeParsedType<ParsedType>(token, ParsedType::U64); break;
			case Token::ISIZE: res = makeParsedType<ParsedType>(token, ParsedType::ISIZE); break;
			case Token::F32: res = makeParsedType<ParsedType>(token, ParsedType::F32); break;
			case Token::F64: res = makeParsedType<ParsedType>(token, ParsedType::F64); break;
			case Token::CPTR: res = makeParsedType<ParsedType>(token, ParsedType::CPTR); break;
			case Token::BYTE: res = makeParsedType<ParsedType>(token, ParsedType::BYTE); break;
			case Token::TYPE_KW: res = makeParsedType<ParsedType>(token, ParsedType::TYPE); break;
			case Token::IDENTIFIER: {
				QualifiedParsedType* qualified = makeParsedType<QualifiedParsedType>(token);
				qualified->name = token.value;
				res = qualified;
				if (peekToken().type == Token::DOT) {
					consumeToken();
					Token name = consumeToken();
					if (name.type != Token::IDENTIFIER) {
						m_output.errorAt(name, "Expected identifier");
						return nullptr;
					}
					qualified->qualifier = qualified->name;
					qualified->name = name.value;
				}
				break;
			}
			case Token::FN: {
				FunctionParsedType* fn = makeParsedType<FunctionParsedType>(token, m_unit.arena);
				if (!consume(Token::LEFT_PAREN)) return nullptr;
				while (peekToken().type != Token::RIGHT_PAREN) {
					ParsedType* param = type();
					if (!param) return nullptr;
					fn->params.push(param);
					if (peekToken().type != Token::COMMA) break;
					consumeToken();
				}
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				if (!consume(Token::COLON)) return nullptr;
				fn->return_type = type();
				if (!fn->return_type) return nullptr;
				res = fn;
				break;
			}
			case Token::LEFT_PAREN: {
				res = type();
				if (!res) return nullptr;
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				break;
			}
			default:
				return nullptr;
		}

		// Brackets after a qualified name in type position are template instantiation;
		// array/slice syntax is prefix ([N]T, []T), so there is no ambiguity.
		if (res->kind == ParsedType::QUALIFIED) {
			if (peekToken().type == Token::LEFT_BRACKET) {
				Token bracket = consumeToken();
				TemplateInstantiationParsedType* call = makeParsedType<TemplateInstantiationParsedType>(bracket, m_unit.arena);
				call->base = static_cast<QualifiedParsedType*>(res);
				while (peekToken().type != Token::RIGHT_BRACKET) {
					if (peekToken().type == Token::END_OF_FILE) {
						m_output.error("Unexpected end of file");
						return nullptr;
					}

					Expression* arg = expression(ExprMode::HEAD);
					if (!arg) return nullptr;
					call->args.push(arg);
					if (peekToken().type != Token::COMMA) break;
					consumeToken();
				}
				if (!consume(Token::RIGHT_BRACKET)) return nullptr;
				res = call;
			}
		}

		return res;
	}

	ParsedType* type() {
		// Parse prefix brackets and nullable syntax recursively.
		// Examples:
		// - [4]i32: array of 4 ints
		// - []i32: slice of ints
		// - [4]?i32: array of 4 nullable ints
		// - ?[4]i32: nullable array of 4 ints
		// - [2][3]i32: array of 2 arrays of 3 ints
		if (peekToken().type == Token::LEFT_BRACKET) {
			Token bracket = consumeToken();
			if (peekToken().type == Token::RIGHT_BRACKET) {
				consumeToken();
				SliceParsedType* slice = makeParsedType<SliceParsedType>(bracket);
				slice->element_type = type();
				return slice;
			} else {
				Expression* size = expression();
				if (!size) return nullptr;
				if (!consume(Token::RIGHT_BRACKET)) return nullptr;
				ArrayParsedType* array = makeParsedType<ArrayParsedType>(bracket);
				array->size = size;
				array->element_type = type();
				return array;
			}
		}

		// Handle nullable
		Token first = peekToken();
		if (first.type == Token::QUESTION) {
			consumeToken();
			ParsedType* inner = type();
			if (!inner) return nullptr;
			NullableParsedType* nullable = makeParsedType<NullableParsedType>(first);
			nullable->inner = inner;
			return nullable;
		}

		if (first.type == Token::DOLLAR) {
			consumeToken();
			Token name = consumeToken();
			if (name.type != Token::IDENTIFIER) {
				m_output.errorAt(name, "Expected inferred generic type name");
				return nullptr;
			}
			GenericParsedType* generic = makeParsedType<GenericParsedType>(first);
			generic->name = name.value;
			return generic;
		}

		Token base_token = consumeToken();
		ParsedType* res = typeFromToken(base_token);
		if (!res) {
			m_output.errorAt(base_token, "Expected type");
		}
		return res;
	}

	// Parse a binary expression, e.g. `a + b` or `x * y`. 
	// Recursive, so a * b + c is parsed as two binary expressions and 3 atoms.
	Expression* binaryExpression(int min_precedence, ExprMode mode) {
		Expression* lhs = castExpression(mode);
		if (!lhs) return nullptr;

		for (;;) {
			Token op = peekToken();
			int prec = precedence(op.type);
			if (prec < min_precedence) return lhs;

			consumeToken();
			Expression* rhs = binaryExpression(prec + 1, mode);
			if (!rhs) return nullptr;

			BinaryExpression* bin = makeExpr<BinaryExpression>(op);
			bin->lhs = lhs;
			bin->rhs = rhs;
			bin->op = op.type;
			lhs = bin;
		}
	}

	// Parse an expression, e.g. `a + b * c`.
	Expression* expression(ExprMode mode = ExprMode::FULL) {
		return binaryExpression(1, mode);
	}

	// Parse a comptime declaration, e.g. `comptime x = expr;`.
	bool comptime() {
		return symbolDecl(Symbol::COMPTIME);
	}

	VarDeclStatement* varDecl() {
		Token type_token = consumeToken();
		if (type_token.type != Token::CONST && type_token.type != Token::VAR) return nullptr;

		VarDeclStatement* res = makeStmt<VarDeclStatement>(type_token);
		res->is_immutable = type_token.type == Token::CONST;
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

	ForStatement* forStatement() {
		Token for_token = consumeToken();
		if (for_token.type != Token::FOR) return nullptr;

		ForStatement* res = makeStmt<ForStatement>(for_token);
		if (!consume(Token::IDENTIFIER, res->loop_var, "Expected identifier")) return nullptr;
		if (!consume(Token::EQUAL)) return nullptr;

		res->begin = expression(ExprMode::HEAD);
		if (!res->begin) return nullptr;
		if (!consume(Token::RANGE)) return nullptr;

		res->end = expression(ExprMode::HEAD);
		if (!res->end) return nullptr;

		res->body = blockStatement();
		if (!res->body) return nullptr;
		return res;
	}

	WhileStatement* whileStatement() {
		Token while_token = consumeToken();
		if (while_token.type != Token::WHILE) return nullptr;
		WhileStatement* res = makeStmt<WhileStatement>(while_token);
		res->condition = expression(ExprMode::HEAD);
		if (!res->condition) return nullptr;
		res->body = blockStatement();
		if (!res->body) return nullptr;
		return res;
	}

	DeferStatement* deferStatement() {
		Token defer_token = consumeToken();
		if (defer_token.type != Token::DEFER) return nullptr;
		DeferStatement* res = makeStmt<DeferStatement>(defer_token);
		res->statement = statement();
		if (!res->statement) return nullptr;
		return res;
	}

	IfStatement* ifStatement() {
		Token if_token = consumeToken();
		if (if_token.type != Token::IF) return nullptr;
		IfStatement* res = makeStmt<IfStatement>(if_token);
		res->condition = expression(ExprMode::HEAD);
		if (!res->condition) return nullptr;
		res->body = blockStatement();
		if (!res->body) return nullptr;
		if (peekToken().type == Token::ELSE) {
			consumeToken();
			res->else_branch = peekToken().type == Token::IF ? static_cast<Statement*>(ifStatement()) : static_cast<Statement*>(blockStatement());
			if (!res->else_branch) return nullptr;
		}
		return res;
	}

	ReturnStatement* returnStatement() {
		Token return_token = consumeToken();
		if (return_token.type != Token::RETURN) return nullptr;
		ReturnStatement* res = makeStmt<ReturnStatement>(return_token);
		if (peekToken().type != Token::SEMICOLON) {
			res->expression = expression();
			if (!res->expression) return nullptr;
		}
		if (!consume(Token::SEMICOLON)) return nullptr;
		return res;
	}

	bool functionParam(FunctionParam& param) {
		if (!consume(Token::IDENTIFIER, param.name, "Expected parameter name")) return false;
		if (!consume(Token::COLON)) return false;
		if (peekToken().type == Token::COMPTIME) {
			consumeToken();
			param.is_comptime = true;
		}
		if (peekToken().type == Token::REF) {
			consumeToken();
			param.is_ref = true;
		}
		param.parsed_type = type();
		if (!param.parsed_type) return false;
		return true;
	}

	ContinueStatement* continueStatement() {
		Token continue_token = consumeToken();
		if (continue_token.type != Token::CONTINUE) return nullptr;
		ContinueStatement* res = makeStmt<ContinueStatement>(continue_token);
		if (peekToken().type == Token::IDENTIFIER) {
			res->label = consumeToken().value;
		}
		if (!consume(Token::SEMICOLON)) return nullptr;
		return res;
	}

	BreakStatement* breakStatement() {
		Token break_token = consumeToken();
		if (break_token.type != Token::BREAK) return nullptr;
		BreakStatement* res = makeStmt<BreakStatement>(break_token);
		if (peekToken().type == Token::IDENTIFIER) {
			res->label = consumeToken().value;
		}
		if (!consume(Token::SEMICOLON)) return nullptr;
		return res;
	}

	Expression* matchPatternExpression() {
		if (peekToken().type == Token::DOT) {
			Token dot = consumeToken();
			Token name = consumeToken();
			if (name.type != Token::IDENTIFIER) {
				m_output.errorAt(name, "Expected identifier");
				return nullptr;
			}
			MemberExpression* expr = makeExpr<MemberExpression>(dot);
			expr->name = name.value;
			return expr;
		}
		return expression();
	}

	bool matchPattern(MatchPattern& pattern) {
		pattern.begin = matchPatternExpression();
		if (!pattern.begin) return false;

		if (peekToken().type == Token::RANGE) {
			consumeToken();
			pattern.end = matchPatternExpression();
			if (!pattern.end) return false;
		}
		return true;
	}

	BlockStatement* matchArmBody() {
		BlockStatement* body = makeStmt<BlockStatement>(peekToken(), m_unit.arena);
		for (;;) {
			switch (peekToken().type) {
				case Token::END_OF_FILE:
					m_output.error("Unexpected end of file");
					return nullptr;
				case Token::CASE:
				case Token::ELSE:
				case Token::RIGHT_BRACE:
					return body;
				default: {
					Statement* child = statement();
					if (!child) return nullptr;
					body->statements.push(child);
					break;
				}
			}
		}
	}

	MatchStatement* matchStatement() {
		Token match_token = consumeToken();
		if (match_token.type != Token::MATCH) return nullptr;

		MatchStatement* res = makeStmt<MatchStatement>(match_token, m_unit.arena);
		res->subject = expression(ExprMode::HEAD);
		if (!res->subject) return nullptr;
		if (!consume(Token::LEFT_BRACE)) return nullptr;

		bool has_fallback = false;
		while (peekToken().type != Token::RIGHT_BRACE) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				return nullptr;
			}

			MatchArm& arm = res->arms.emplace_back(m_unit.arena);
			if (peekToken().type == Token::ELSE) {
				consumeToken();
				if (has_fallback) {
					m_output.errorAt(peekToken(), "Duplicate match else");
					return nullptr;
				}
				has_fallback = true;
				arm.is_fallback = true;
			}
			else if (peekToken().type == Token::CASE) {
				consumeToken();
				for (;;) {
					MatchPattern& pattern = arm.patterns.emplace_back();
					if (!matchPattern(pattern)) return nullptr;
					if (peekToken().type != Token::COMMA) break;
					consumeToken();
				}
			}
			else {
				m_output.errorAt(peekToken(), "Expected case or else");
				return nullptr;
			}

			if (!consume(Token::COLON)) return nullptr;
			arm.body = matchArmBody();
			if (!arm.body) return nullptr;
		}

		if (!consume(Token::RIGHT_BRACE)) return nullptr;
		return res;
	}

	Statement* statement() {
		switch (peekToken().type) {
			case Token::END_OF_FILE:
				m_output.error("Unexpected end of file");
				return nullptr;
			case Token::CONTINUE: return continueStatement();
			case Token::BREAK: return breakStatement();
			case Token::MATCH: return matchStatement();
			case Token::LEFT_BRACE: return blockStatement();
			case Token::WHILE: return whileStatement();
			case Token::FOR: return forStatement();
			case Token::CONST:
			case Token::VAR: return varDecl();
			case Token::RETURN: return returnStatement();
			case Token::IF: return ifStatement();
			case Token::DEFER: return deferStatement();
			case Token::IDENTIFIER: {
				Token token = consumeToken();
				if (peekToken().type == Token::COLON) {
					consumeToken();
					LabelStatement* res = makeStmt<LabelStatement>(token);
					res->name = token.value;
					res->statement = statement();
					if (!res->statement) return nullptr;
					return res;
				}

				IdentifierExpression* lhs_id = makeExpr<IdentifierExpression>(token);
				lhs_id->name = token.value;
				Expression* lhs = postfixSuffixes(lhs_id, ExprMode::FULL);
				if (!lhs) return nullptr;

				Token op = peekToken();
				switch (op.type) {
					case Token::EQUAL:
					case Token::PLUS_EQUAL:
					case Token::MINUS_EQUAL:
					case Token::STAR_EQUAL:
					case Token::SLASH_EQUAL:
					{
						consumeToken();
						AssignStatement* res = makeStmt<AssignStatement>(op);
						res->lhs = lhs;
						res->op = op.type;
						res->rhs = expression();
						if (!res->rhs) return nullptr;
						if (!consume(Token::SEMICOLON)) return nullptr;
						return res;
					}
					// `i++;` is a postfix-only statement form equivalent to `i += 1;`,
					// so it desugars to a compound assignment here.
					case Token::PLUS_PLUS:
					case Token::MINUS_MINUS: {
						consumeToken();
						AssignStatement* res = makeStmt<AssignStatement>(op);
						res->lhs = lhs;
						res->op = op.type == Token::PLUS_PLUS ? Token::PLUS_EQUAL : Token::MINUS_EQUAL;
						IntLiteralExpression* one = make<IntLiteralExpression>();
						one->value = 1;
						res->rhs = one;
						if (!consume(Token::SEMICOLON)) return nullptr;
						return res;
					}
					case Token::SEMICOLON: {
						Token semi = consumeToken();
						ExpressionStatement* res = makeStmt<ExpressionStatement>(semi);
						res->expression = lhs;
						return res;
					}
					default:
						m_output.errorAt(op, "Expected assignment operator or semicolon");
						return nullptr;
				}
			}
			default: {
				Expression* expr = expression();
				if (!expr) return nullptr;
				if (!consume(Token::SEMICOLON)) return nullptr;
				ExpressionStatement* res = makeStmt<ExpressionStatement>(expr->token);
				res->expression = expr;
				return res;
			}
		}
		return nullptr;
	}

	BlockStatement* blockStatement() {
		Token brace = consumeToken();
		if (brace.type != Token::LEFT_BRACE) { m_output.errorAt(brace, "Expected {"); return nullptr; }
		BlockStatement* res = makeStmt<BlockStatement>(brace, m_unit.arena);
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

	// Parse a runtime parameter list and return type, e.g. `(a : i32) : i32`.
	bool functionSignature(FunctionExpression* fn) {
		if (!consume(Token::LEFT_PAREN)) return false;
		while (peekToken().type != Token::RIGHT_PAREN) {
			FunctionParam& arg = fn->params.emplace_back();
			if (!functionParam(arg)) return false;
			for (i32 i = 0; i < fn->params.size() - 1; ++i) {
				if (!equalStrings(fn->params[i].name, arg.name)) continue;
				m_output.error("Duplicate parameter: ", arg.name);
				return false;
			}
			if (peekToken().type != Token::COMMA) break;
			consumeToken();
		}
		if (!consume(Token::RIGHT_PAREN)) return false;
		if (!consume(Token::COLON)) return false;
		fn->return_type = type();
		return fn->return_type != nullptr;
	}

	static bool isGeneric(const Expression& expr) {
		switch (expr.kind) {
			case Expression::GENERIC_IDENTIFIER: return true;
			case Expression::BRACKET: {
				const auto& br = static_cast<const BracketExpression&>(expr);
				if (isGeneric(*br.base)) return true;
				for (Expression* arg : br.args) {
					if (isGeneric(*arg)) return true;
				}
				return false;
			}
			case Expression::CALL: {
				const auto& call = static_cast<const CallExpression&>(expr);
				if (isGeneric(*call.callee)) return true;
				for (Expression* arg : call.args) {
					if (isGeneric(*arg)) return true;
				}
				return false;
			}
			case Expression::BINARY: {
				const auto& bin = static_cast<const BinaryExpression&>(expr);
				return isGeneric(*bin.lhs) || isGeneric(*bin.rhs);
			}
			case Expression::UNARY: return isGeneric(*static_cast<const UnaryExpression&>(expr).expression);
			case Expression::CAST: return isGeneric(*static_cast<const CastExpression&>(expr).expression);
			case Expression::MEMBER: return isGeneric(*static_cast<const MemberExpression&>(expr).expression);
			case Expression::SLICE: {
				const auto& slice = static_cast<const SliceExpression&>(expr);
				if (isGeneric(*slice.base)) return true;
				if (slice.begin && isGeneric(*slice.begin)) return true;
				if (slice.end && isGeneric(*slice.end)) return true;
				return false;
			}
			case Expression::STRUCT_LITERAL: {
				const auto& lit = static_cast<const StructLiteralExpression&>(expr);
				if (lit.type && isGeneric(*lit.type)) return true;
				for (Expression* val : lit.values) {
					if (isGeneric(*val)) return true;
				}
				return false;
			}
			default: return false;
		}
	}

	static bool isGeneric(const ParsedType& type) {
		switch (type.kind) {
			case ParsedType::GENERIC: return true;
			case ParsedType::ARRAY: return isGeneric(*static_cast<const ArrayParsedType&>(type).element_type);
			case ParsedType::NULLABLE: return isGeneric(*static_cast<const NullableParsedType&>(type).inner);
			case ParsedType::SLICE: return isGeneric(*static_cast<const SliceParsedType&>(type).element_type);
			case ParsedType::TEMPLATE_INSTANTIATION: {
				const TemplateInstantiationParsedType& tpl = static_cast<const TemplateInstantiationParsedType&>(type);
				if (isGeneric(*tpl.base)) return true;
				for (Expression* arg : tpl.args) {
					if (isGeneric(*arg)) return true;
				}
				return false;
			}
			case ParsedType::FUNCTION: {
				const FunctionParsedType& fn = static_cast<const FunctionParsedType&>(type);
				for (const auto param : fn.params) {
					if (isGeneric(*param)) return true;
				}
				return isGeneric(*fn.return_type);
			}

			default: return false;
		}
	}

	FunctionExpression* functionExpression() {
		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		bool ok = functionSignature(fn);
		if (!ok) return nullptr;

		fn->is_template = false;
		for (FunctionParam& param : fn->params) {
			if (param.is_comptime || isGeneric(*param.parsed_type)) {
				param.is_generic = true;
				fn->is_template = true;
			}
		}

		fn->body = blockStatement();
		if (!fn->body) return nullptr;
		return fn;
	}

	StructExpression* structExpression() {
		StructExpression* st = make<StructExpression>(m_unit.arena);
		if (!templateParams(st->comptime_params)) return nullptr;
		m_comptime_params = st->comptime_params.empty() ? nullptr : &st->comptime_params;
		if (!consume(Token::LEFT_BRACE)) { m_comptime_params = nullptr; return nullptr; }
		while (peekToken().type != Token::RIGHT_BRACE) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				m_comptime_params = nullptr;
				return nullptr;
			}

			NamedDecl& field = st->fields.emplace_back();
			if (!consume(Token::IDENTIFIER, field.name, "Expected field name")) { m_comptime_params = nullptr; return nullptr; }
			for (i32 i = 0; i < st->fields.size() - 1; ++i) {
				if (!equalStrings(st->fields[i].name, field.name)) continue;
				m_output.error("Duplicate field: ", field.name);
				m_comptime_params = nullptr;
				return nullptr;
			}
			if (!consume(Token::COLON)) { m_comptime_params = nullptr; return nullptr; }
			field.parsed_type = type();
			if (!field.parsed_type) { m_comptime_params = nullptr; return nullptr; }
			if (!consume(Token::SEMICOLON)) { m_comptime_params = nullptr; return nullptr; }
		}
		m_comptime_params = nullptr;
		if (!consume(Token::RIGHT_BRACE)) return nullptr;
		return st;
	}

	EnumExpression* enumExpression() {
		EnumExpression* en = make<EnumExpression>(m_unit.arena);
		if (!consume(Token::LEFT_BRACE)) return nullptr;
		while (peekToken().type != Token::RIGHT_BRACE) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				return nullptr;
			}

			EnumMember& member = en->members.emplace_back();
			if (!consume(Token::IDENTIFIER, member.name, "Expected enum member name")) return nullptr;
			for (i32 i = 0; i < en->members.size() - 1; ++i) {
				if (!equalStrings(en->members[i].name, member.name)) continue;
				m_output.error("Duplicate enum member: ", member.name);
				return nullptr;
			}
			if (peekToken().type == Token::EQUAL) {
				consumeToken();
				member.value = expression();
				if (!member.value) return nullptr;
			}

			if (peekToken().type != Token::COMMA) break;
			consumeToken();
		}
		if (!consume(Token::RIGHT_BRACE)) return nullptr;
		return en;
	}

	bool enumDecl() {
		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected enum name"); return false; }
		EnumExpression* en = enumExpression();
		if (!en) return false;
		return namedComptimeDecl(name_token, en);
	}

	bool structDecl() {
		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected struct name"); return false; }
		StructExpression* st = structExpression();
		if (!st) return false;
		return namedComptimeDecl(name_token, st);
	}

	// Parse an import declaration, e.g. `import "core:vec3" as vec`.
	bool importDecl() {
		Token path = consumeToken();
		if (path.type != Token::STRING) {
			m_output.errorAt(path, "Expected import path string");
			return false;
		}

		Import import;
		import.path = path.value;
		Token alias_token = {};
		if (peekToken().type == Token::AS) {
			consumeToken();
			alias_token = consumeToken();
			if (alias_token.type != Token::IDENTIFIER) { m_output.errorAt(alias_token, "Expected import alias"); return false; }
			import.alias = alias_token.value;
		}

		if (!empty(import.alias)) {
			Symbol sym;
			sym.storage = Symbol::IMPORT;
			sym.name = import.alias;
			sym.token = alias_token;
			if (!addSymbol(sym)) return false;
		}

		m_unit.imports.push_back(import);
		if (peekToken().type == Token::SEMICOLON) consumeToken();
		return true;
	}

	// Parse an extern function declaration, e.g. `extern fn foo(a : i32) : i32;`.
	bool externDecl() {
		if (!consume(Token::FN)) return false;
		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected function name"); return false; }

		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		fn->is_extern = true;
		if (!functionSignature(fn)) return false;
		if (!consume(Token::SEMICOLON)) return false;

		// Per the language reference, `extern fn foo() : T;` is sugar for a
		// module-level `var` bound to a body-less function value.
		Symbol s;
		s.name = name_token.value;
		s.token = name_token;
		s.expression = fn;
		s.storage = Symbol::VARIABLE;
		return addSymbol(s);
	}

	// Parse an operator declaration, e.g. `operator +(a : Vec3, b : Vec3) : Vec3 { }`.
	bool operatorDecl() {
		Token op = consumeToken();
		const char* sym_name = operatorSymbolName(op.type);
		if (!sym_name) {
			m_output.errorAt(op, "Operator ", toString(op.type), " can not be overloaded");
			return false;
		}

		if (peekToken().type == Token::LEFT_BRACKET) {
			m_output.errorAt(peekToken(), "Operator overloads can not be templated");
			return false;
		}

		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		if (!functionSignature(fn)) return false;
		fn->body = blockStatement();
		if (!fn->body) return false;

		Symbol sym;
		sym.name = makeStringView(sym_name);
		sym.token = op;
		sym.expression = fn;
		sym.storage = Symbol::COMPTIME;
		return addSymbol(sym);
	}

	// Parse a function declaration, e.g. `fn foo(a) { }`.
	bool functionDecl() {
		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected function name"); return false; }

		FunctionExpression* fn = functionExpression();
		if (!fn) return false;
		return namedComptimeDecl(name_token, fn);
	}

	// Parse a source file, e.g. a whole `.ls` script.
	ls_result parse(ls_string_view source, ls_string_view source_name) {
		m_tokenizer.init(source, source_name);

		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::END_OF_FILE: return LS_RESULT_OK;
				case Token::CONST:
				case Token::VAR:
					if (!symbolDecl(token.type == Token::CONST ? Symbol::CONST : Symbol::VARIABLE)) return LS_RESULT_FAILURE;
					break;
				case Token::FN: if (!functionDecl()) return LS_RESULT_FAILURE; break;
				case Token::STRUCT: if (!structDecl()) return LS_RESULT_FAILURE; break;
				case Token::ENUM: if (!enumDecl()) return LS_RESULT_FAILURE; break;
				case Token::COMPTIME: if (!comptime()) return LS_RESULT_FAILURE; break;
				case Token::IMPORT: if (!importDecl()) return LS_RESULT_FAILURE; break;
				case Token::EXTERN: if (!externDecl()) return LS_RESULT_FAILURE; break;
				case Token::OPERATOR: if (!operatorDecl()) return LS_RESULT_FAILURE; break;
				default: 
					m_output.errorAt(token, "Unexpected ", toString(token.type));
					return LS_RESULT_FAILURE;
			}
		}

	}

	Unit& m_unit;
	Tokenizer m_tokenizer;
	OutputFormatter m_output;
	const ExpArray<NamedDecl>* m_comptime_params = nullptr;
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
