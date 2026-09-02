#include "compiler.h"
#include "tokenizer.h"
#include "utils.h"

enum class ExprMode {
	FULL,
	HEAD,
};

struct Parser {
	Parser(Unit& unit, const ex_host* host, SourceLocTable& src_locs)
		: m_unit(unit)
		, m_src_locs(src_locs)
		, m_tokenizer(src_locs)
	{
		m_output.host = host;
		m_output.src_locs = &src_locs;
	}

	// Counter for synthesizing unique hidden index names for `for v in arr` (no
	// explicit index binding), so nested such loops never shadow one another.
	i32 m_for_index_counter = 0;

	ex_string_view makeForIndexName() {
		char digits[32];
		toCString(m_for_index_counter++, digits, sizeof(digits));
		i64 digits_len = 0;
		while (digits[digits_len] != '\0') ++digits_len;

		static const char prefix[] = "$for_index";
		const i64 prefix_len = sizeof(prefix) - 1;
		ex_arena& a = m_unit.arena;
		char* buffer = static_cast<char*>(a.allocate(a.user_data, prefix_len + digits_len, 1));
		copyMemory(buffer, prefix, prefix_len);
		copyMemory(buffer + prefix_len, digits, digits_len);
		return ex_string_view{buffer, prefix_len + digits_len};
	}

	template <typename T, typename... Args>
	T* make(Args&&... args) {
		ex_arena& a = m_unit.arena;
		T* res = static_cast<T*>(a.allocate(a.user_data, sizeof(T), alignof(T)));
		new (NewPlaceholder{}, res) T(static_cast<Args&&>(args)...);
		return res;
	}

	template <typename T, typename... Args>
	T* makeExpr(Token token, Args&&... args) {
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
			case Token::PIPE: return 1;
			case Token::OR: return 1;
			case Token::AND: return 2;
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL: return 3;
			case Token::GT:
			case Token::LT:
			case Token::GT_EQUAL:
			case Token::LT_EQUAL:
			case Token::IS: return 4;
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
			case Token::RUNE: return "rune";
			case Token::LEFT_PAREN: return "(";
			case Token::RIGHT_PAREN: return ")";
			case Token::LEFT_BRACE: return "{";
			case Token::RIGHT_BRACE: return "}";
			case Token::LEFT_BRACKET: return "[";
			case Token::RIGHT_BRACKET: return "]";
			case Token::SEMICOLON: return ";";
			case Token::COLON: return ":";
			case Token::DOUBLE_COLON: return "::";
			case Token::COMMA: return ",";
			case Token::DOT: return ".";
			case Token::RANGE: return "..";
			case Token::RANGE_INCLUSIVE: return "..=";
			case Token::QUESTION: return "?";
			case Token::DOLLAR: return "$";
			case Token::HASH: return "#";
			case Token::PIPE: return "|";
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
			case Token::WHILE: return "while";
			case Token::FOR: return "for";
			case Token::UNROLL: return "unroll";
			case Token::IN_KW: return "in";
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
			case Token::IS: return "is";
			case Token::F32: return "f32";
			case Token::F64: return "f64";
			case Token::CPTR: return "cptr";
			case Token::CSTR: return "cstr";
			case Token::BYTE: return "byte";
			case Token::SIZEOF: return "sizeof";
			case Token::ALIGNOF: return "alignof";
			case Token::TYPEOF: return "typeof";
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

	bool consume(Token::Type type, ex_string_view& value, const char* error_msg) {
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
		ASSERT(sym.expression || sym.kind == Symbol::IMPORT);
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

	Expression* runeLiteral(Token token) {
		const u8* p = (const u8*)data(token.value);
		const u8* const end = p + size(token.value);
		u32 value = 0;
		auto invalid = [&]() -> Expression* {
			m_output.errorAt(token, "Rune literal must contain exactly one Unicode code point");
			return nullptr;
		};
		if (p == end) return invalid();
		if (*p < 0x80) {
			value = *p++;
		}
		else {
			u32 length = 0;
			if (*p >= 0xc2 && *p <= 0xdf) { value = *p++ & 0x1f; length = 1; }
			else if (*p >= 0xe0 && *p <= 0xef) { value = *p++ & 0x0f; length = 2; }
			else if (*p >= 0xf0 && *p <= 0xf4) { value = *p++ & 0x07; length = 3; }
			else return invalid();
			for (u32 i = 0; i < length; ++i) {
				if (p == end || (*p & 0xc0) != 0x80) return invalid();
				value = (value << 6) | (*p++ & 0x3f);
			}
			if ((length == 1 && value < 0x80) || (length == 2 && value < 0x800) || (length == 3 && value < 0x10000)) return invalid();
		}
		if (p != end || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return invalid();
		IntLiteralExpression* expr = makeExpr<IntLiteralExpression>(token);
		expr->value = value;
		return expr;
	}

	bool symbolDecl(Symbol::Kind kind) {
		Symbol sym;
		sym.kind = kind;

		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) {
			m_output.errorAt(name_token, "Expected identifier");
			return false;
		}
		sym.name = name_token.value;
		sym.token = name_token;

		if (peekToken().type == Token::COLON) {
			consumeToken();
			sym.type_expr = type();
			if (!sym.type_expr) return false;
		}

		if (!consume(Token::EQUAL)) return false;

		sym.expression = expression();
		if (!sym.expression) return false;
		if (!consume(Token::SEMICOLON)) return false;

		return addSymbol(sym);
	}

	bool namedComptimeDecl(Token name_token, Expression* expression) {
		Symbol sym;
		sym.name = name_token.value;
		sym.token = name_token;
		sym.expression = expression;
		sym.kind = Symbol::COMPTIME;
		return addSymbol(sym);
	}

	static bool isExpressionDelimiter(Token::Type token) {
		switch (token) {
			case Token::END_OF_FILE:
			case Token::RIGHT_PAREN:
			case Token::RIGHT_BRACE:
			case Token::RIGHT_BRACKET:
			case Token::SEMICOLON:
			case Token::COLON:
			case Token::COMMA:
			case Token::DOT:
			case Token::LEFT_PAREN:
			case Token::LEFT_BRACE:
			case Token::LEFT_BRACKET:
			case Token::RANGE:
			case Token::RANGE_INCLUSIVE:
			case Token::QUESTION:
			case Token::PIPE:
			case Token::PLUS:
			case Token::MINUS:
			case Token::STAR:
			case Token::SLASH:
			case Token::PERCENT:
			case Token::EQUAL:
			case Token::PLUS_EQUAL:
			case Token::MINUS_EQUAL:
			case Token::STAR_EQUAL:
			case Token::SLASH_EQUAL:
			case Token::PLUS_PLUS:
			case Token::MINUS_MINUS:
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL:
			case Token::GT:
			case Token::LT:
			case Token::GT_EQUAL:
			case Token::LT_EQUAL:
			case Token::AS:
			case Token::AND:
			case Token::OR:
			case Token::IS:
				return true;
			default:
				return false;
		}
	}

	Expression* bracketPrimary(Token token) {
		// `[]` can be only slice, we don't support empty arrays
		if (peekToken().type == Token::RIGHT_BRACKET) {
			consumeToken();
			SliceTypeExpression* slice = makeExpr<SliceTypeExpression>(token);
			if (peekToken().type == Token::CONST) {
				consumeToken();
				slice->is_const = true;
			}
			// `false` so that `[]u8 | i32` is a union of a slice and i32, not a slice of a union;
			// the trailing `|` is picked up by binaryExpression
			slice->element_type = type(false);
			return slice->element_type ? slice : nullptr;
		}

		Expression* first = expression();
		if (!first) return nullptr;

		// `[expr,` can only be array literal
		if (peekToken().type == Token::COMMA) {
			ArrayLiteralExpression* array = makeExpr<ArrayLiteralExpression>(token, m_unit.arena);
			array->values.push(first);
			while (peekToken().type == Token::COMMA) {
				consumeToken();
				Expression* value = expression();
				if (!value) return nullptr;
				array->values.push(value);
			}
			if (!consume(Token::RIGHT_BRACKET)) return nullptr;
			return array;
		}

		if (!consume(Token::RIGHT_BRACKET)) return nullptr;

		// array literals with one element:
		// foo([1]);
		// var a = [1];
		// var a = [[1]];
		// for value in [1] {}
		// foo("abc", [1]);
		switch (peekToken().type) {
			case Token::RIGHT_PAREN:
			case Token::SEMICOLON:
			case Token::RIGHT_BRACKET:
			case Token::LEFT_BRACE:
			case Token::COMMA:
				ArrayLiteralExpression* array = makeExpr<ArrayLiteralExpression>(token, m_unit.arena);
				array->values.push(first);
				return array;
		}
	
		ArrayTypeExpression* array = makeExpr<ArrayTypeExpression>(token);
		array->size = first;
		array->element_type = type(false);
		return array->element_type ? array : nullptr;
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
				Expression* expr = functionOrTypeExpression(token);
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
			case Token::STAR: {
				PointerTypeExpression* pointer = makeExpr<PointerTypeExpression>(token);
				if (peekToken().type == Token::CONST) {
					consumeToken();
					pointer->is_const = true;
				}
				pointer->inner = type(false);
				return pointer->inner ? pointer : nullptr;
			}
			case Token::QUESTION: {
				NullableTypeExpression* nullable = makeExpr<NullableTypeExpression>(token);
				nullable->inner = type(false);
				return nullable->inner ? nullable : nullptr;
			}
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
			case Token::CPTR:
			case Token::CSTR:
			case Token::BYTE:
			case Token::TYPE_KW:
				return postfixSuffixes(makeExpr<TypeLiteralExpression>(token, primitiveKindFromToken(token.type)), mode);
			case Token::SIZEOF:
			case Token::ALIGNOF: {
				SizeofExpression* expr = makeExpr<SizeofExpression>(token);
				expr->is_align = token.type == Token::ALIGNOF;
				if (!consume(Token::LEFT_PAREN)) return nullptr;
				expr->type_expr = type();
				if (!expr->type_expr) return nullptr;
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
			case Token::LEFT_BRACKET: return bracketPrimary(token);
			case Token::TYPEOF: {
				TypeofExpression* expr = makeExpr<TypeofExpression>(token);
				if (!consume(Token::LEFT_PAREN)) return nullptr;
				expr->operand = expression();
				if (!expr->operand) return nullptr;
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				return expr;
			}
			case Token::LEFT_PAREN: {
				Expression* expr = expression();
				if (!expr) return nullptr;
				if (!consume(Token::RIGHT_PAREN)) return nullptr;
				expr->parenthesized = true;
				return expr;
			}
			case Token::NUMBER: {
				return numberLiteral(token);
			}
			case Token::RUNE: return runeLiteral(token);
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
					if (name.type == Token::STAR) {
						// dereference .*
						DereferenceExpression* deref = makeExpr<DereferenceExpression>(dot);
						deref->subject = expr;
						expr = deref;
						break;
					}
					if (name.type != Token::IDENTIFIER && name.type != Token::TYPE_KW) {
						m_output.errorAt(name, "Expected identifier");
						return nullptr;
					}

					MemberExpression* member = makeExpr<MemberExpression>(dot);
					member->expression = expr;
					member->name = name.value;
					expr = member;
					break;
				}
				case Token::DOUBLE_COLON: {
					Token colon = consumeToken();
					if (expr->parenthesized) {
						m_output.errorAt(colon, "Parenthesized expressions cannot be used for type member access");
						return nullptr;
					}
					Token name = consumeToken();
					if (name.type != Token::IDENTIFIER) {
						m_output.errorAt(name, "Expected identifier");
						return nullptr;
					}

					TypeMemberExpression* member = makeExpr<TypeMemberExpression>(colon);
					member->expression = expr;
					if (equalStrings(name.value, "kind")) member->kind = TypeMemberExpression::KIND;
					else if (equalStrings(name.value, "name")) member->kind = TypeMemberExpression::NAME;
					else if (equalStrings(name.value, "ret")) member->kind = TypeMemberExpression::RET;
					else if (equalStrings(name.value, "params")) member->kind = TypeMemberExpression::PARAMS;
					else if (equalStrings(name.value, "fields")) member->kind = TypeMemberExpression::FIELDS;
					else if (equalStrings(name.value, "values")) member->kind = TypeMemberExpression::VALUES;
					else if (equalStrings(name.value, "types")) member->kind = TypeMemberExpression::TYPES;
					else if (equalStrings(name.value, "min")) member->kind = TypeMemberExpression::MIN;
					else if (equalStrings(name.value, "max")) member->kind = TypeMemberExpression::MAX;
					else if (equalStrings(name.value, "child")) member->kind = TypeMemberExpression::CHILD;
					else if (equalStrings(name.value, "length")) member->kind = TypeMemberExpression::LENGTH;
					else if (equalStrings(name.value, "attribute")) member->kind = TypeMemberExpression::ATTRIBUTE;
					else {
						m_output.errorAt(name, "Unsupported type member");
						return nullptr;
					}

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
					if (call->callee->kind == Expression::IDENTIFIER
						&& equalStrings(static_cast<IdentifierExpression*>(call->callee)->name, makeStringView("panic"))
						&& call->args.size() == 1) {
						PanicExpression* panic = makeExpr<PanicExpression>(paren);
						panic->message = call->args[0];
						expr = panic;
					} else {
						expr = call;
					}
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
			case Token::AMPERSAND: {
				consumeToken();
				AddressOfExpression* expr = makeExpr<AddressOfExpression>(token);
				expr->subject = binaryExpression(3, mode);
				if (!expr->subject) return nullptr;
				return expr->subject ? expr : nullptr;
			}
			case Token::STAR: {
				consumeToken();
				PointerTypeExpression* pointer = makeExpr<PointerTypeExpression>(token);
				if (peekToken().type == Token::CONST) {
					consumeToken();
					pointer->is_const = true;
				}
				pointer->inner = type();
				return pointer->inner ? pointer : nullptr;
			}
			case Token::MINUS: {
				consumeToken();
				UnaryExpression* expr = makeExpr<UnaryExpression>(token);
				expr->op = token.type;
				// Parse only another unary operand so `-1 as u64` casts the
				// negated value rather than negating an unsigned cast result.
				expr->expression = unaryExpression(mode);
				if (!expr->expression) return nullptr;
				return expr;
			}
			case Token::NOT: {
				consumeToken();
				UnaryExpression* expr = makeExpr<UnaryExpression>(token);
				expr->op = token.type;
				// `not` binds below comparisons and `is`, but above `and`/`or`.
				expr->expression = binaryExpression(3, mode);
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
			cast->type_expr = type();
			if (!cast->type_expr) return nullptr;
			expr = cast;
		}

		return expr;
	}

	static ResolvedTypeKind primitiveKindFromToken(Token::Type type) {
		switch (type) {
			case Token::VOID: return ResolvedTypeKind::VOID;
			case Token::BOOL: return ResolvedTypeKind::BOOL;
			case Token::I8: return ResolvedTypeKind::I8;
			case Token::I16: return ResolvedTypeKind::I16;
			case Token::I32: return ResolvedTypeKind::I32;
			case Token::I64: return ResolvedTypeKind::I64;
			case Token::U8: return ResolvedTypeKind::U8;
			case Token::U16: return ResolvedTypeKind::U16;
			case Token::U32: return ResolvedTypeKind::U32;
			case Token::U64: return ResolvedTypeKind::U64;
			case Token::ISIZE: return ResolvedTypeKind::ISIZE;
			case Token::F32: return ResolvedTypeKind::F32;
			case Token::F64: return ResolvedTypeKind::F64;
			case Token::CPTR: return ResolvedTypeKind::CPTR;
			case Token::CSTR: return ResolvedTypeKind::CSTR;
			case Token::BYTE: return ResolvedTypeKind::BYTE;
			// META stands for the `type` keyword.
			case Token::TYPE_KW: return ResolvedTypeKind::META;
			default: return ResolvedTypeKind::INVALID;
		}
	}

	bool functionTypeParam(FunctionTypeParam& param) {
		if (peekToken().type == Token::COMPTIME) {
			consumeToken();
			param.is_comptime = true;
		}
		Expression* argument = expression();
		if (!argument) return false;
		if (peekToken().type != Token::COLON) {
			param.type_expr = argument;
			return true;
		}

		if (argument->kind != Expression::IDENTIFIER) {
			m_output.errorAt(argument->token, "Expected parameter name");
			return false;
		}
		param.name = static_cast<IdentifierExpression*>(argument)->name;
		consumeToken();
		if (peekToken().type == Token::COMPTIME) {
			consumeToken();
			param.is_comptime = true;
		}
		param.type_expr = expression();
		return param.type_expr != nullptr;
	}

	bool functionTypeSignature(FunctionTypeExpression& fn) {
		if (!consume(Token::LEFT_PAREN)) return false;
		while (peekToken().type != Token::RIGHT_PAREN) {
			FunctionTypeParam& param = fn.params.emplace_back();
			if (!functionTypeParam(param)) return false;
			if (peekToken().type != Token::COMMA) break;
			consumeToken();
		}
		if (!consume(Token::RIGHT_PAREN) || !consume(Token::COLON)) return false;
		fn.return_type = type();
		return fn.return_type != nullptr;
	}

	bool functionParams(FunctionExpression& fn, const FunctionTypeExpression& signature) {
		for (const FunctionTypeParam& type_param : signature.params) {
			if (empty(type_param.name)) {
				m_output.errorAt(signature.token, "Function parameters require names");
				return false;
			}
			FunctionParam& param = fn.params.emplace_back();
			param.name = type_param.name;
			param.is_comptime = type_param.is_comptime;
			param.type_expr = type_param.type_expr;
			for (i32 i = 0; i < fn.params.size() - 1; ++i) {
				if (!equalStrings(fn.params[i].name, param.name)) continue;
				m_output.error("Duplicate parameter: ", param.name);
				return false;
			}
		}
		return true;
	}

	Expression* type(bool allow_union = true) {
		// Parse prefix brackets and nullable syntax recursively.
		// Examples:
		// - [4]i32: array of 4 ints
		// - []i32: slice of ints
		// - [4]?i32: array of 4 nullable ints
		// - ?[4]i32: nullable array of 4 ints
		// - [2][3]i32: array of 2 arrays of 3 ints
		Token token = consumeToken();
		Expression* res = nullptr;

		switch (token.type) {
			case Token::LEFT_BRACKET: {
				if (peekToken().type == Token::RIGHT_BRACKET) {
					consumeToken();
					SliceTypeExpression* slice = makeExpr<SliceTypeExpression>(token);
					if (peekToken().type == Token::CONST) {
						consumeToken();
						slice->is_const = true;
					}
					slice->element_type = type(false);
					if (!slice->element_type) return nullptr;
					res = slice;
					break;
				}

				Expression* size = expression();
				if (!size) return nullptr;
				if (!consume(Token::RIGHT_BRACKET)) return nullptr;
				ArrayTypeExpression* array = makeExpr<ArrayTypeExpression>(token);
				array->size = size;
				array->element_type = type(false);
				if (!array->element_type) return nullptr;
				res = array;
				break;
			}
			case Token::STAR: {
				PointerTypeExpression* pointer = makeExpr<PointerTypeExpression>(token);
				if (peekToken().type == Token::CONST) {
					consumeToken();
					pointer->is_const = true;
				}
				pointer->inner = type(false);
				if (!pointer->inner) return nullptr;
				res = pointer;
				break;
			}
			case Token::QUESTION: {
				Expression* inner = type(false);
				if (!inner) return nullptr;
				NullableTypeExpression* nullable = makeExpr<NullableTypeExpression>(token);
				nullable->inner = inner;
				res = nullable;
				break;
			}
			case Token::DOLLAR: {
				Token name = consumeToken();
				if (name.type != Token::IDENTIFIER) {
					m_output.errorAt(name, "Expected inferred generic type name");
					return nullptr;
				}
				GenericIdentifierExpression* generic = makeExpr<GenericIdentifierExpression>(token);
				generic->name = name.value;
				return generic;
			}
			case Token::IDENTIFIER: {
				IdentifierExpression* id = makeExpr<IdentifierExpression>(token);
				id->name = token.value;
				res = id;
				if (peekToken().type == Token::DOT) {
					Token dot = consumeToken();
					Token name = consumeToken();
					if (name.type != Token::IDENTIFIER) {
						m_output.errorAt(name, "Expected identifier");
						return nullptr;
					}
					MemberExpression* member = makeExpr<MemberExpression>(dot);
					member->expression = id;
					member->name = name.value;
					res = member;
				}
				break;
			}
			case Token::FN: {
				FunctionTypeExpression* fn = makeExpr<FunctionTypeExpression>(token, m_unit.arena);
				if (!functionTypeSignature(*fn)) return nullptr;
				res = fn;
				break;
			}
			case Token::LEFT_PAREN: {
				res = type(true);
				if (!res || !consume(Token::RIGHT_PAREN)) return nullptr;
				break;
			}
			default: {
				const ResolvedTypeKind kind = primitiveKindFromToken(token.type);
				if (kind != ResolvedTypeKind::INVALID) res = makeExpr<TypeLiteralExpression>(token, kind);
				break;
			}
		}
		if (!res) {
			m_output.errorAt(token, "Expected type");
			return nullptr;
		}

		// A type factory call is an ordinary compile-time call used where a type is
		// required, for example `Vec2(T)`.
		if ((res->kind == Expression::IDENTIFIER || res->kind == Expression::MEMBER)
			&& peekToken().type == Token::LEFT_PAREN)
		{
			Token paren = consumeToken();
			CallExpression* call = makeExpr<CallExpression>(paren, m_unit.arena);
			call->callee = res;
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
			res = call;
		}

		// Brackets index a compile-time sequence, e.g. `comptime Types = [i32, f32];` used
		// as `Types[1]`. Array and slice syntax is prefix ([N]T, []T), so there is no
		// ambiguity with a type annotation. Applying type arguments is a call, not an index.
		if ((res->kind == Expression::IDENTIFIER || res->kind == Expression::MEMBER)
			&& peekToken().type == Token::LEFT_BRACKET)
		{
			Token bracket = consumeToken();
			BracketExpression* index = makeExpr<BracketExpression>(bracket, m_unit.arena);
			index->base = res;
			while (peekToken().type != Token::RIGHT_BRACKET) {
				if (peekToken().type == Token::END_OF_FILE) {
					m_output.error("Unexpected end of file");
					return nullptr;
				}
				Expression* arg = expression(ExprMode::HEAD);
				if (!arg) return nullptr;
				index->args.push(arg);
				if (peekToken().type != Token::COMMA) break;
				consumeToken();
			}
			if (!consume(Token::RIGHT_BRACKET)) return nullptr;
			res = index;
		}

		if (!allow_union || peekToken().type != Token::PIPE) return res;

		UnionTypeExpression* union_type = makeExpr<UnionTypeExpression>(res->token, m_unit.arena);
		union_type->members.push(res);
		while (peekToken().type == Token::PIPE) {
			consumeToken();
			Expression* member = type();
			if (!member) return nullptr;
			union_type->members.push(member);
		}
		return union_type;
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
			if (op.type == Token::PIPE) {
				UnionTypeExpression* union_type = makeExpr<UnionTypeExpression>(op, m_unit.arena);
				const auto append = [&](Expression* expr) {
					if (expr->kind == Expression::UNION_TYPE) {
						for (Expression* member : static_cast<UnionTypeExpression*>(expr)->members) union_type->members.push(member);
					}
					else {
						union_type->members.push(expr);
					}
				};
				append(lhs);
				append(rhs);
				lhs = union_type;
				continue;
			}

			BinaryExpression* bin = makeExpr<BinaryExpression>(op);
			bin->lhs = lhs;
			bin->rhs = rhs;
			bin->op = op.type;
			lhs = bin;
		}
	}

	// Parse a ternary expression: `condition ? true_expr : false_expr`.
	// The ternary operator has the lowest precedence and is right-associative.
	Expression* ternaryExpression(ExprMode mode = ExprMode::FULL) {
		Expression* cond = binaryExpression(1, mode);
		if (!cond) return nullptr;

		if (peekToken().type == Token::QUESTION) {
			Token question = consumeToken();
			Expression* true_expr = expression(mode);
			if (!true_expr) return nullptr;
			if (!consume(Token::COLON)) return nullptr;
			
			Expression* false_expr = expression(mode);
			if (!false_expr) return nullptr;

			TernaryExpression* ternary = makeExpr<TernaryExpression>(question);
			ternary->condition = cond;
			ternary->true_expr = true_expr;
			ternary->false_expr = false_expr;
			return ternary;
		}

		return cond;
	}

	// Parse an expression, e.g. `a + b * c` or `a ? b : c`.
	Expression* expression(ExprMode mode = ExprMode::FULL) {
		return ternaryExpression(mode);
	}

	VarDeclStatement* varDecl() {
		Token type_token = consumeToken();
		if (type_token.type != Token::CONST && type_token.type != Token::VAR && type_token.type != Token::COMPTIME) return nullptr;

		VarDeclStatement* res = makeStmt<VarDeclStatement>(type_token);
		res->is_immutable = type_token.type == Token::CONST;
		res->is_comptime = type_token.type == Token::COMPTIME;
		if (!consume(Token::IDENTIFIER, res->name, "Expected identifier")) return nullptr;
		if (peekToken().type == Token::COLON) {
			consumeToken();
			res->type_expr = type();
			if (!res->type_expr) return nullptr;
		}
		if (!consume(Token::EQUAL)) return nullptr;
		res->expression = expression();
		if (!res->expression) return nullptr;
		if (peekToken().type == Token::ELSE) {
			consumeToken();
			if (!consume(Token::RETURN)) return nullptr;
			if (!consume(Token::SEMICOLON)) return nullptr;
			res->else_return = true;
			return res;
		}
		if (!consume(Token::SEMICOLON)) return nullptr;
		return res;
	}

	ForStatement* forStatement() {
		Token for_token = consumeToken();
		if (for_token.type != Token::FOR) return nullptr;

		ForStatement* res = makeStmt<ForStatement>(for_token);
		if (!consume(Token::IDENTIFIER, res->key_var, "Expected identifier")) return nullptr;

		bool is_key_value = false;
		if (peekToken().type == Token::COMMA) {
			is_key_value = true;
			consumeToken();
			if (!consume(Token::IDENTIFIER, res->value_var, "Expected identifier")) return nullptr;
		}
		else {
			res->value_var = res->key_var;
			res->key_var = makeForIndexName();
		}

		if (!consume(Token::IN_KW)) return nullptr;
			
		res->begin = expression(ExprMode::HEAD);
		if (!res->begin) return nullptr;

		if (peekToken().type == Token::RANGE) {
			if (is_key_value) {
				m_output.errorAt(peekToken(), "Expected only one identifier for range-based for loop");
				return nullptr;
			}

			if (!consume(Token::RANGE)) return nullptr;

			res->end = expression(ExprMode::HEAD);
			if (!res->end) return nullptr;
		}

		res->is_key_value = is_key_value;

		res->body = blockStatement();
		if (!res->body) return nullptr;
		return res;
	}

	ForStatement* unrollForStatement() {
		Token unroll_token = consumeToken();
		if (unroll_token.type != Token::UNROLL) return nullptr;
		if (peekToken().type != Token::FOR) {
			m_output.errorAt(peekToken(), "Expected for after unroll");
			return nullptr;
		}
		ForStatement* res = forStatement();
		if (res) res->is_unroll = true;
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

	Expression* functionOrTypeExpression(Token token) {
		if (peekToken().type != Token::LEFT_PAREN) {
			m_output.errorAt(peekToken(), "Expected (");
			return nullptr;
		}

		FunctionTypeExpression* type_expr = makeExpr<FunctionTypeExpression>(token, m_unit.arena);
		if (!functionTypeSignature(*type_expr)) return nullptr;

		if (peekToken().type != Token::LEFT_BRACE) return type_expr;
		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		if (!functionParams(*fn, *type_expr)) return nullptr;
		fn->return_type = type_expr->return_type;
		fn->body = blockStatement();
		if (!fn->body) return nullptr;
		ExpArray<ex_string_view> generic_names(m_unit.arena);
		for (FunctionParam& param : fn->params) {
			if (param.is_comptime || isGeneric(*param.type_expr)) {
				fn->is_template = true;
			}
			if (!collectGenericParams(*param.type_expr, generic_names)) return nullptr;
		}
		return fn;
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
			m_output.errorAt(peekToken(), "Match range patterns must use ..= (inclusive), not .. (exclusive)");
			return false;
		}
		if (peekToken().type == Token::RANGE_INCLUSIVE) {
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

		while (peekToken().type != Token::RIGHT_BRACE) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				return nullptr;
			}

			if (!consume(Token::CASE)) {
				m_output.errorAt(peekToken(), "Expected case");
				return nullptr;
			}

			MatchArm& arm = res->arms.emplace_back(m_unit.arena);
			if (peekToken().type == Token::COLON) {
				arm.is_fallback = true;
			}
			else {
				for (;;) {
					MatchPattern& pattern = arm.patterns.emplace_back();
					if (!matchPattern(pattern)) return nullptr;
					if (peekToken().type != Token::COMMA) break;
					consumeToken();
				}
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
			case Token::UNROLL: return unrollForStatement();
			case Token::CONST:
			case Token::VAR:
			case Token::COMPTIME: return varDecl();
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
		FunctionTypeExpression signature(m_unit.arena);
		if (!functionTypeSignature(signature)) return false;
		if (!functionParams(*fn, signature)) return false;
		fn->return_type = signature.return_type;
		return true;
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
			case Expression::PANIC: return isGeneric(*static_cast<const PanicExpression&>(expr).message);
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
			case Expression::TYPE_MEMBER: return isGeneric(*static_cast<const TypeMemberExpression&>(expr).expression);
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
			// $ introduces inferred type parameters only, so an array size cannot be generic.
			case Expression::ARRAY_TYPE: return isGeneric(*static_cast<const ArrayTypeExpression&>(expr).element_type);
			case Expression::SLICE_TYPE: return isGeneric(*static_cast<const SliceTypeExpression&>(expr).element_type);
			case Expression::NULLABLE_TYPE: return isGeneric(*static_cast<const NullableTypeExpression&>(expr).inner);
			case Expression::POINTER_TYPE: return isGeneric(*static_cast<const PointerTypeExpression&>(expr).inner);
			case Expression::FUNCTION_TYPE: {
				const auto& fn = static_cast<const FunctionTypeExpression&>(expr);
				for (const FunctionTypeParam& param : fn.params) {
					if (isGeneric(*param.type_expr)) return true;
				}
				return isGeneric(*fn.return_type);
			}
			default: return false;
		}
	}

	bool collectGenericParams(const Expression& expr, ExpArray<ex_string_view>& names) {
		switch (expr.kind) {
			case Expression::GENERIC_IDENTIFIER: {
				ex_string_view name = static_cast<const GenericIdentifierExpression&>(expr).name;
				for (ex_string_view existing : names) {
					if (!equalStrings(existing, name)) continue;
					m_output.error("Duplicate template parameter: ", name);
					return false;
				}
				names.push(name);
				return true;
			}
			case Expression::ARRAY_TYPE: return collectGenericParams(*static_cast<const ArrayTypeExpression&>(expr).element_type, names);
			case Expression::NULLABLE_TYPE: return collectGenericParams(*static_cast<const NullableTypeExpression&>(expr).inner, names);
			case Expression::SLICE_TYPE: return collectGenericParams(*static_cast<const SliceTypeExpression&>(expr).element_type, names);
			case Expression::POINTER_TYPE: return collectGenericParams(*static_cast<const PointerTypeExpression&>(expr).inner, names);
			case Expression::BRACKET: {
				const auto& br = static_cast<const BracketExpression&>(expr);
				return collectGenericParams(*br.base, names);
			}
			case Expression::FUNCTION_TYPE: {
				const auto& fn = static_cast<const FunctionTypeExpression&>(expr);
				for (const FunctionTypeParam& param : fn.params) {
					if (!collectGenericParams(*param.type_expr, names)) return false;
				}
				return collectGenericParams(*fn.return_type, names);
			}
			default: return true;
		}
	}

	FunctionExpression* functionExpression() {
		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		bool ok = functionSignature(fn);
		if (!ok) return nullptr;

		fn->is_template = false;
		ExpArray<ex_string_view> generic_names(m_unit.arena);
		for (FunctionParam& param : fn->params) {
			if (param.is_comptime || isGeneric(*param.type_expr)) {
				fn->is_template = true;
			}
			if (!collectGenericParams(*param.type_expr, generic_names)) return nullptr;
		}

		fn->body = blockStatement();
		if (!fn->body) return nullptr;
		return fn;
	}

	inline ExpArray<Attribute>* parseAttributeList() {
		if (!consume(Token::LEFT_BRACKET)) return nullptr;
		if (peekToken().type == Token::RIGHT_BRACKET) {
			m_output.errorAt(peekToken(), "Expected attribute");
			return nullptr;
		}
		ExpArray<Attribute>* attributes = make<ExpArray<Attribute>>(m_unit.arena);
		for (;;) {
			Token name = consumeToken();
			if (name.type != Token::IDENTIFIER) {
				m_output.errorAt(name, "Expected attribute type");
				return nullptr;
			}
			IdentifierExpression* identifier = makeExpr<IdentifierExpression>(name);
			identifier->name = name.value;
			Expression* type_expr = postfixSuffixes(identifier, ExprMode::HEAD);
			if (!type_expr) return nullptr;

			Attribute& attribute = attributes->emplace_back();
			attribute.type = type_expr;
			attribute.token = name;
			attribute.value = structLiteral(type_expr);
			if (!attribute.value) return nullptr;
			if (peekToken().type != Token::COMMA) break;
			consumeToken();
		}
		if (!consume(Token::RIGHT_BRACKET)) return nullptr;
		return attributes;
	}


	StructExpression* structExpression() {
		StructExpression* st = make<StructExpression>(m_unit.arena);
		if (!consume(Token::LEFT_BRACE)) return nullptr;
		while (peekToken().type != Token::RIGHT_BRACE) {
			if (peekToken().type == Token::END_OF_FILE) {
				m_output.error("Unexpected end of file");
				return nullptr;
			}

			StructFieldDecl& field = st->fields.emplace_back();
			if (peekToken().type == Token::HASH) {
				consumeToken();
				field.attributes = parseAttributeList();
				if (!field.attributes) return nullptr;
			}
			Token field_name = consumeToken();
			// Native-facing structs may mirror C fields whose names happen to be
			// Evox keywords (e.g. ui::Event::type).
			if (field_name.type != Token::IDENTIFIER && field_name.type != Token::TYPE_KW) {
				m_output.errorAt(field_name, "Expected field name");
				return nullptr;
			}
			field.name = field_name.value;
			for (i32 i = 0; i < st->fields.size() - 1; ++i) {
				if (!equalStrings(st->fields[i].name, field.name)) continue;
				m_output.error("Duplicate field: ", field.name);
				return nullptr;
			}
			if (!consume(Token::COLON)) return nullptr;
			field.type_expr = type();
			if (!field.type_expr) return nullptr;
			if (!consume(Token::SEMICOLON)) return nullptr;
		}
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

	bool structDecl(ExpArray<Attribute>* attributes, bool is_extern) {
		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected struct name"); return false; }
		StructExpression* st = structExpression();
		if (!st) return false;
		st->attributes = attributes;
		st->is_extern = is_extern;
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
			sym.kind = Symbol::IMPORT;
			sym.name = import.alias;
			sym.token = alias_token;
			if (!addSymbol(sym)) return false;
		}

		m_unit.imports.push_back(import);
		if (peekToken().type == Token::SEMICOLON) consumeToken();
		return true;
	}

	// Parse an extern declaration, e.g. `extern fn foo(a : i32) : i32;` or `extern struct Foo { ... }`.
	bool externDecl() {
		Token declaration = consumeToken();
		if (declaration.type == Token::STRUCT) return structDecl(nullptr, true);
		if (declaration.type != Token::FN) {
			m_output.errorAt(declaration, "Expected extern fn or extern struct declaration");
			return false;
		}

		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected function name"); return false; }

		FunctionExpression* fn = make<FunctionExpression>(m_unit.arena);
		fn->is_extern = true;
		if (!functionSignature(fn)) return false;
		for (const FunctionParam& param : fn->params) {
			if (param.is_comptime) {
				m_output.errorAt(param.type_expr->token, "Extern function parameters cannot be comptime");
				return false;
			}
		}
		if (!consume(Token::SEMICOLON)) return false;

		// Per the language reference, `extern fn foo() : T;` is sugar for a
		// module-level `var` bound to a body-less function value.
		Symbol s;
		s.name = name_token.value;
		s.token = name_token;
		s.expression = fn;
		s.kind = Symbol::VARIABLE;
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
		sym.kind = Symbol::COMPTIME;
		return addSymbol(sym);
	}

	// Parse a function declaration, e.g. `fn foo(a) { }`.
	bool functionDecl() {
		Token name_token = consumeToken();
		if (name_token.type != Token::IDENTIFIER) { m_output.errorAt(name_token, "Expected function name"); return false; }

		FunctionExpression* fn = functionExpression();
		if (!fn) return false;
		fn->token = name_token;
		return namedComptimeDecl(name_token, fn);
	}

	// Parse a source file, e.g. a whole `.evox` script.
	ex_result parse(ex_string_view source, ex_string_view source_name) {
		m_tokenizer.init(source, source_name);

		for (;;) {
			Token token = consumeToken();
			switch (token.type) {
				case Token::END_OF_FILE: return EX_RESULT_OK;
				case Token::CONST: if (!symbolDecl(Symbol::CONST)) return EX_RESULT_FAILURE; break;
				case Token::VAR: if (!symbolDecl(Symbol::VARIABLE)) return EX_RESULT_FAILURE; break;
				case Token::COMPTIME: if (!symbolDecl(Symbol::COMPTIME)) return EX_RESULT_FAILURE; break;
				case Token::FN: if (!functionDecl()) return EX_RESULT_FAILURE; break;
				case Token::STRUCT: if (!structDecl(nullptr, false)) return EX_RESULT_FAILURE; break;
				case Token::HASH: {
					ExpArray<Attribute>* attributes = parseAttributeList();
					if (!attributes) return EX_RESULT_FAILURE;
					Token declaration = consumeToken();
					if (declaration.type == Token::STRUCT) {
						if (!structDecl(attributes, false)) return EX_RESULT_FAILURE;
						break;
					}
					if (declaration.type == Token::EXTERN) {
						Token extern_declaration = consumeToken();
						if (extern_declaration.type != Token::STRUCT) {
							m_output.errorAt(extern_declaration, "Attributes can only be attached to structs");
							return EX_RESULT_FAILURE;
						}
						if (!structDecl(attributes, true)) return EX_RESULT_FAILURE;
						break;
					}
					m_output.errorAt(declaration, "Attributes can only be attached to structs");
					return EX_RESULT_FAILURE;
				}
				case Token::ENUM: if (!enumDecl()) return EX_RESULT_FAILURE; break;
				case Token::IMPORT: if (!importDecl()) return EX_RESULT_FAILURE; break;
				case Token::EXTERN: if (!externDecl()) return EX_RESULT_FAILURE; break;
				case Token::OPERATOR: if (!operatorDecl()) return EX_RESULT_FAILURE; break;
				default: 
					m_output.errorAt(token, "Unexpected ", toString(token.type));
					return EX_RESULT_FAILURE;
			}
		}

	}

	Unit& m_unit;
	SourceLocTable& m_src_locs;
	Tokenizer m_tokenizer;
	OutputFormatter m_output;
};

// Parse a module, e.g. `ex_module_parse(module, source, name)`.
ex_result ex_module_parse(ex_module* module, ex_string_view source, ex_string_view source_name) {
	Unit& unit = module->units.emplace_back(source_name, module->arena);
	Parser parser(unit, module->host, module->src_locs);
	if (parser.parse(source, source_name) == EX_RESULT_FAILURE) return EX_RESULT_FAILURE;

	return EX_RESULT_OK;
}


void OutputFormatter::print(int v) {
	char tmp[32];
	toCString(v, tmp, sizeof(tmp));
	print(tmp);
}
