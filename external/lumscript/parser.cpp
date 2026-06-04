#include "compiler.h"
#include "tokenizer.h"
#include "utils.h"

struct Parser {
	Parser(ls_module& module, Unit& unit, ls_string_view declaration_prefix, ls_string_view source, ls_string_view source_name = {})
		: m_module(module)
		, m_unit(unit)
		, m_declaration_prefix(declaration_prefix)
	{
		m_output.host = &module.host;
		m_tokenizer.init(source, source_name);
	}

	bool parse() {
		while (peek().type != Token::END_OF_FILE && !m_output.has_error) {
			if (match(Token::IMPORT)) parseImport();
			else if (match(Token::EXTERN)) parseExtern();
			else if (match(Token::STRUCT)) parseStruct();
			else if (match(Token::ENUM)) parseEnum();
			else if (match(Token::FN)) parseFunction();
			else if (match(Token::OPERATOR)) parseOperator();
			else if (peek().type == Token::VAR || peek().type == Token::CONST) parseGlobal();
			else error(peek(), "Expected declaration");
		}
		return !m_output.has_error;
	}

	Token peek() const { return m_tokenizer.peekToken(); }
	Token peekNext() const {
		Tokenizer tmp = m_tokenizer;
		tmp.consumeToken();
		return tmp.peekToken();
	}
	Token consumeToken() { return m_tokenizer.consumeToken(); }
	bool match(Token::Type type) {
		if (peek().type != type) return false;
		consumeToken();
		return true;
	}

	bool consume(Token::Type type, Token* out = nullptr) {
		Token t = consumeToken();
		if (t.type != type) {
			error(t, "Unexpected token");
			return false;
		}
		if (out) *out = t;
		return true;
	}

	void error(Token token, const char* msg) {
		m_output.errorAt(token, msg, " near '", token.value, "'");
	}

	// track all symbols (functions, enum, structs, externs, globals) to detect duplicates
	bool addSymbol(Token token, Unit& unit, CanonicalName name, Symbol::Kind kind, i32 index) {
		for (const Symbol& symbol : unit.symbols) {
			if (equal(symbol.name, name)) {
				error(token, "Duplicate symbol");
				return false;
			}
		}
		unit.symbols.push_back(Symbol{kind, name, &unit, index});
		return true;
	}

	ls_string_view parseQualifiedIdentifier(Token first) {
		ls_string_view name = first.value;
		while (match(Token::DOT)) {
			Token part;
			if (!consume(Token::IDENTIFIER, &part)) return name;
			name = m_module.makeQualifiedName(name, part.value);
		}
		return name;
	}

	static ls_string_view makeSynthesizedName(ls_arena& arena, ls_string_view prefix, ls_string_view infix, i32 value) {
		char num_buf[16];
		char* num_end = toCString(value, num_buf, sizeof(num_buf));
		if (!num_end) return {};
		const usize num_len = (usize)(num_end - num_buf);
		const usize prefix_size = size(prefix);
		const usize infix_size = size(infix);
		const usize separator = empty(prefix) ? 0 : 1;
		const usize total = prefix_size + separator + infix_size + num_len + 1;
		char* buffer = (char*)arena.allocate(arena.user_data, total, alignof(char));
		if (!buffer) return {};
		char* out = buffer;
		if (prefix_size > 0) {
			copyMemory(out, data(prefix), prefix_size);
			out += prefix_size;
			*out++ = '.';
		}
		if (infix_size > 0) {
			copyMemory(out, data(infix), infix_size);
			out += infix_size;
		}
		copyMemory(out, num_buf, num_len);
		out += num_len;
		*out = '\0';
		return ls_string_view{buffer, out};
	}

	void parseImport() {
		Token path;
		if (!consume(Token::STRING, &path)) return;
		ImportDecl& import = m_unit.imports.emplace_back();
		import.path = path.value;
		import.token = path;
		if (match(Token::AS)) {
			Token alias;
			if (!consume(Token::IDENTIFIER, &alias)) return;
			import.alias = alias.value;
		}
		match(Token::SEMICOLON);
	}

	enum class CanBeRef {
		YES,
		NO
	};

	TypeRef parseType(CanBeRef can_be_ref = CanBeRef::NO) {
		const bool is_ref = match(Token::REF);
		if (is_ref && can_be_ref == CanBeRef::NO) {
			error(peek(), "Unexpected ref");
			return {};
		}
		const bool is_nullable = match(Token::QUESTION);
		Token t = consumeToken();
		TypeRef base;
		switch (t.type) {
			case Token::FN: {
				m_module.function_types.emplace_back(*m_module.arena_owner.arena);
				const i32 fn_type_idx = (i32)m_module.function_types.size() - 1;
				if (!consume(Token::LEFT_PAREN)) return {};
				while (peek().type != Token::RIGHT_PAREN && peek().type != Token::END_OF_FILE && !m_output.has_error) {
					FunctionTypeDecl& fn_type = m_module.function_types[fn_type_idx];
					if (!fn_type.params.empty()) consume(Token::COMMA);
					fn_type.params.push_back(parseType(CanBeRef::YES));
				}
				consume(Token::RIGHT_PAREN);
				consume(Token::COLON);
				m_module.function_types[fn_type_idx].return_type = parseType();
				base = {LS_TYPE_FUNCTION, {}, fn_type_idx, t, is_nullable};
				break;
			}
			case Token::VOID: base = {LS_TYPE_VOID, t.value, -1, t, is_nullable}; break;
			case Token::BOOL: base = {LS_TYPE_BOOL, t.value, -1, t, is_nullable}; break;
			case Token::CPTR: base = {LS_TYPE_CPTR, t.value, -1, t, is_nullable}; break;
			case Token::I8: base = {LS_TYPE_I8, t.value, -1, t, is_nullable}; break;
			case Token::U8: base = {LS_TYPE_U8, t.value, -1, t, is_nullable}; break;
			case Token::I16: base = {LS_TYPE_I16, t.value, -1, t, is_nullable}; break;
			case Token::U16: base = {LS_TYPE_U16, t.value, -1, t, is_nullable}; break;
			case Token::I32: base = {LS_TYPE_I32, t.value, -1, t, is_nullable}; break;
			case Token::U32: base = {LS_TYPE_U32, t.value, -1, t, is_nullable}; break;
			case Token::I64: base = {LS_TYPE_I64, t.value, -1, t, is_nullable}; break;
			case Token::U64: base = {LS_TYPE_U64, t.value, -1, t, is_nullable}; break;
			case Token::F32: base = {LS_TYPE_F32, t.value, -1, t, is_nullable}; break;
			case Token::F64: base = {LS_TYPE_F64, t.value, -1, t, is_nullable}; break;
			case Token::STRING_KW: base = {LS_TYPE_STRING, t.value, -1, t, is_nullable}; break;
			// Keep the type name exactly as written here.
			// Declarations are qualified when they are created, but type references
			// must stay source-level names so the checker can resolve them against
			// local declarations, unaliased imports, or alias-qualified imports.
			case Token::IDENTIFIER: base = {LS_TYPE_STRUCT, parseQualifiedIdentifier(t), -1, t, is_nullable}; break;
			default: error(t, "Expected type"); return {};
		}
		if (match(Token::LEFT_BRACKET)) {
			if (match(Token::RIGHT_BRACKET)) {
				TypeRef slice_type;
				slice_type.kind = LS_TYPE_SLICE;
				slice_type.token = t;
				slice_type.nullable = is_nullable;
				slice_type.element_kind = base.kind;
				slice_type.element_name = base.unresolved_name;
				slice_type.struct_index = base.struct_index;
				return slice_type;
			}
			Token size_token;
			if (!consume(Token::NUMBER, &size_token)) return {};
			if (!consume(Token::RIGHT_BRACKET)) return {};
			if (contains(size_token.value, '.')) {
				error(size_token, "Array size must be an integer literal");
				return {};
			}
			TypeRef array_type;
			array_type.kind = LS_TYPE_ARRAY;
			array_type.token = t;
			array_type.nullable = is_nullable;
			array_type.element_kind = base.kind;
			array_type.element_name = base.unresolved_name;
			array_type.struct_index = base.struct_index;
			fromCString(size_token.value, array_type.array_size);
			if (array_type.array_size <= 0) {
				error(size_token, "Array size must be a positive integer");
				return {};
			}
			return array_type;
		}
		base.is_ref = is_ref;
		return base;
	}

	void parseExtern() {
		if (!consume(Token::FN)) return;
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		NativeFunctionDecl& fn = m_unit.native_functions.emplace_back(*m_unit.arena_owner.arena);
		fn.canonical_name = {m_declaration_prefix, name.value};
		if (!consume(Token::LEFT_PAREN)) return;
		if (!addSymbol(name, m_unit, {m_declaration_prefix, name.value}, Symbol::EXTERN_FN, m_unit.native_functions.size() - 1)) return;

		while (peek().type != Token::RIGHT_PAREN && peek().type != Token::END_OF_FILE && !m_output.has_error) {
			if (!fn.params.empty() && !consume(Token::COMMA)) return;
			Token param_name;
			if (!consume(Token::IDENTIFIER, &param_name)) return;
			consume(Token::COLON);
			Param& p = fn.params.emplace_back();
			p.name = param_name.value;
			p.token = param_name;
			p.is_ref = match(Token::REF);
			p.type = parseType();
		}

		if (!consume(Token::RIGHT_PAREN)) return;
		if (!consume(Token::COLON)) return;
		fn.token = name;
		fn.return_type = parseType();
		consume(Token::SEMICOLON);
	}

	void parseStruct() {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		StructDecl& s = m_unit.structs.emplace_back(*m_unit.arena_owner.arena);
		// The declaration itself is canonicalized to the current module namespace.
		// Imported files are parsed with their normalized path as the prefix, so
		// this becomes the stable identity used by the checker and later imports.
		s.name = {m_declaration_prefix, name.value};
		s.token = name;
		if (!addSymbol(name, m_unit, s.name, Symbol::STRUCT, m_unit.structs.size() - 1)) return;
		if (!consume(Token::LEFT_BRACE)) return;
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
			Token field_name;
			if (!consume(Token::IDENTIFIER, &field_name)) return;
			if (!consume(Token::COLON)) return;
			FieldDecl& field = s.fields.emplace_back();
			field.name = field_name.value;
			field.token = field_name;
			field.type = parseType();
			consume(Token::SEMICOLON);
		}
		consume(Token::RIGHT_BRACE);
	}

	void parseEnum() {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		EnumDecl& e = m_unit.enums.emplace_back(*m_unit.arena_owner.arena);
		// Same rule as structs: store the declaration under the module's canonical
		// namespace, not under any source-level alias that happened to load it.
		e.name = {m_declaration_prefix, name.value};
		e.token = name;
		if (!consume(Token::LEFT_BRACE)) return;
		if (!addSymbol(name, m_unit, e.name, Symbol::ENUM, m_unit.enums.size() - 1)) return;

		i32 auto_value = 0;
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
			Token member_name;
			if (!consume(Token::IDENTIFIER, &member_name)) return;
			EnumMember& member = e.members.emplace_back();
			member.name = member_name.value;
			member.token = member_name;
			if (match(Token::EQUAL)) {
				Token num_token = consumeToken();
				if (num_token.type != Token::NUMBER) {
					error(num_token, "Expected number");
					return;
				}
				i32 value;
				fromCString(num_token.value, value);
				member.value = value;
				auto_value = value + 1;
			} else {
				member.value = auto_value++;
			}
			if (peek().type != Token::RIGHT_BRACE) consume(Token::COMMA);
		}
		consume(Token::RIGHT_BRACE);
	}

	void parseParams(FunctionDecl& fn) {
		while (peek().type != Token::RIGHT_PAREN && peek().type != Token::END_OF_FILE && !m_output.has_error) {
			if (!fn.params.empty()) consume(Token::COMMA);
			Token param_name;
			if (!consume(Token::IDENTIFIER, &param_name)) return;
			consume(Token::COLON);
			Param& p = fn.params.emplace_back();
			p.name = param_name.value;
			p.token = param_name;
			p.is_ref = match(Token::REF);
			p.type = parseType();
		}
	}

	i32 parseFunction() {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return -1;
		m_unit.functions.emplace_back(*m_unit.arena_owner.arena);
		const i32 fn_idx = (i32)m_unit.functions.size() - 1;
		FunctionDecl& fn = m_unit.functions[fn_idx];
		// Top-level functions follow the same canonical module naming rule as
		// structs/enums/globals so imported modules remain addressable by path.
		fn.canonical_name = {m_declaration_prefix, name.value};
		fn.token = name;

		GlobalDecl& global = m_unit.globals.emplace_back();
		global.canonical_name = fn.canonical_name;
		global.token = name;
		global.is_const = true;
		if (!addSymbol(name, m_unit, global.canonical_name, Symbol::GLOBAL_VAR, (i32)m_unit.globals.size() - 1)) return -1;

		if (!consume(Token::LEFT_PAREN)) return fn_idx;
		parseParams(fn);
		consume(Token::RIGHT_PAREN);
		consume(Token::COLON);
		fn.return_type = parseType();
		const i32 body = parseBlock();

		FunctionDecl& parsed_fn = m_unit.functions[fn_idx];
		parsed_fn.body = body;
		const i32 flat_idx = m_module.findFunction(parsed_fn.canonical_name);
		const i32 expr_idx = addExpr(Expr::FUNCTION_REF, name);
		m_unit.expressions[expr_idx].left = flat_idx >= 0 ? flat_idx : fn_idx;
		m_unit.expressions[expr_idx].is_native_fn = false;
		global.expr = expr_idx;
		return fn_idx;
	}

	i32 parseFunctionLiteral(Token fn_token) {
		m_unit.functions.emplace_back(*m_unit.arena_owner.arena);
		const i32 fn_idx = (i32)m_unit.functions.size() - 1;
		FunctionDecl& fn = m_unit.functions[fn_idx];
		fn.canonical_name.path = m_declaration_prefix;
		fn.canonical_name.name = makeSynthesizedName(*m_unit.arena_owner.arena, m_declaration_prefix, makeStringView("__fn_literal"), m_function_literal_counter++);
		fn.token = fn_token;

		if (!consume(Token::LEFT_PAREN)) return fn_idx;
		parseParams(fn);
		consume(Token::RIGHT_PAREN);
		consume(Token::COLON);
		fn.return_type = parseType();
		const i32 body = parseBlock();
		FunctionDecl& parsed_fn = m_unit.functions[fn_idx];
		parsed_fn.body = body;
		const i32 flat_idx = m_module.findFunction(parsed_fn.canonical_name);
		return flat_idx >= 0 ? flat_idx : fn_idx;
	}

	i32 parseOperator() {
		Token op = consumeToken();
		if (op.type != Token::PLUS
			&& op.type != Token::MINUS
			&& op.type != Token::STAR
			&& op.type != Token::SLASH
			&& op.type != Token::PERCENT
			&& op.type != Token::EQUAL_EQUAL
			&& op.type != Token::BANG_EQUAL
			&& op.type != Token::GT
			&& op.type != Token::LT
			&& op.type != Token::GT_EQUAL
			&& op.type != Token::LT_EQUAL
			&& op.type != Token::AND
			&& op.type != Token::OR
			&& op.type != Token::NOT) {
			error(op, "Expected operator");
			return -1;
		}
		m_unit.functions.emplace_back(*m_unit.arena_owner.arena);
		const i32 fn_idx = (i32)m_unit.functions.size() - 1;
		FunctionDecl& fn = m_unit.functions[fn_idx];
		fn.is_operator = true;
		fn.operator_token = op.type;
		fn.canonical_name.path = m_declaration_prefix;
		fn.canonical_name.name = makeSynthesizedName(*m_unit.arena_owner.arena, m_declaration_prefix, makeStringView("__operator"), m_operator_counter++);
		fn.token = op;
		if (!consume(Token::LEFT_PAREN)) return fn_idx;
		parseParams(fn);
		consume(Token::RIGHT_PAREN);
		consume(Token::COLON);
		fn.return_type = parseType();
		const i32 body = parseBlock();
		m_unit.functions[fn_idx].body = body;
		return fn_idx;
	}

	void parseGlobal() {
		Token t = consumeToken();
		const bool is_const = t.type == Token::CONST;
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		GlobalDecl& global = m_unit.globals.emplace_back();
		// Globals also live in the module namespace. This keeps raw symbol names
		// stable regardless of whether the file was imported with an alias.
		global.canonical_name = {m_declaration_prefix, name.value};
		global.token = name;
		global.is_const = is_const;
		if (!addSymbol(name, m_unit, {m_declaration_prefix, name.value}, Symbol::GLOBAL_VAR, m_unit.globals.size() - 1)) return;
		if (match(Token::COLON)) global.type = parseType();
		if (match(Token::EQUAL)) {
			if (peek().type == Token::IDENTIFIER && equalStrings(peek().value, "undefined")) {
				consumeToken();
				global.is_undefined_init = true;
			} else {
				global.expr = parseExpression();
			}
		}
		consume(Token::SEMICOLON);
	}

	struct DisableConstructor {
		Parser& parser;
		bool saved;
		DisableConstructor(Parser& p) : parser(p), saved(p.m_allow_constructor) { p.m_allow_constructor = false; }
		~DisableConstructor() { parser.m_allow_constructor = saved; }
	};

	i32 addStmt(Stmt::Kind kind, Token token) {
		Stmt& stmt = m_unit.statements.emplace_back(*m_unit.arena_owner.arena);
		stmt.kind = kind;
		stmt.token = token;
		return (i32)m_unit.statements.size() - 1;
	}

	i32 addExpr(Expr::Kind kind, Token token) {
		Expr& expr = m_unit.expressions.emplace_back(*m_unit.arena_owner.arena);
		expr.kind = kind;
		expr.token = token;
		return (i32)m_unit.expressions.size() - 1;
	}

	i32 addMatchArm(Token token) {
		MatchArm& arm = m_unit.match_arms.emplace_back(*m_unit.arena_owner.arena);
		arm.token = token;
		return (i32)m_unit.match_arms.size() - 1;
	}

	i32 parseBlock() {
		Token t = consumeToken();
		if (t.type != Token::LEFT_BRACE) {
			error(t, "Expected block");
			return -1;
		}
		const i32 block = addStmt(Stmt::BLOCK, t);
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
			const i32 child = parseStatement();
			m_unit.statements[block].children.push_back(child);
		}
		consume(Token::RIGHT_BRACE);
		return block;
	}

	void parseWhileBody(i32 stmt_idx) {
		DisableConstructor guard(*this);
		m_unit.statements[stmt_idx].expr = parseExpression();
	}

	void parseForBody(i32 stmt_idx) {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		m_unit.statements[stmt_idx].label_name = name.value;
		if (!consume(Token::EQUAL)) return;
		DisableConstructor guard(*this);
		m_unit.statements[stmt_idx].expr = parseExpression();
		if (!consume(Token::RANGE)) return;
		m_unit.statements[stmt_idx].left = parseExpression();
	}

	i32 parseStatement() {
		Token t = peek();
		if (t.type == Token::IDENTIFIER && peekNext().type == Token::COLON) {
			Token label = consumeToken();
			consume(Token::COLON);
			if (match(Token::WHILE)) {
				const i32 stmt_idx = addStmt(Stmt::WHILE, t);
				m_unit.statements[stmt_idx].name = label.value;
				parseWhileBody(stmt_idx);
				m_unit.statements[stmt_idx].right = parseBlock();
				return stmt_idx;
			}
			if (match(Token::FOR)) {
				const i32 stmt_idx = addStmt(Stmt::FOR, t);
				m_unit.statements[stmt_idx].name = label.value;
				parseForBody(stmt_idx);
				m_unit.statements[stmt_idx].right = parseBlock();
				return stmt_idx;
			}
			error(peek(), "Only while and for can be labeled");
			return -1;
		}
		if (t.type == Token::LEFT_BRACE) return parseBlock();
		if (t.type == Token::FN && peekNext().type == Token::IDENTIFIER) {
			consumeToken();
			error(t, "Nested functions are not supported");
			return -1;
		}
		if (match(Token::VAR) || match(Token::CONST)) {
			const bool is_const = t.type == Token::CONST;
			Token name;
			if (!consume(Token::IDENTIFIER, &name)) return -1;
			const i32 stmt_idx = addStmt(Stmt::VAR_DECL, t);
			m_unit.statements[stmt_idx].name = name.value;
			m_unit.statements[stmt_idx].is_const = is_const;
			if (match(Token::COLON)) m_unit.statements[stmt_idx].type = parseType();
			if (match(Token::EQUAL)) {
				if (peek().type == Token::IDENTIFIER && equalStrings(peek().value, "undefined")) {
					consumeToken();
					m_unit.statements[stmt_idx].is_undefined_init = true;
				} else {
					const i32 expr = parseExpression();
					m_unit.statements[stmt_idx].expr = expr;
				}
			}
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::WHILE)) {
			const i32 stmt_idx = addStmt(Stmt::WHILE, t);
			parseWhileBody(stmt_idx);
			m_unit.statements[stmt_idx].right = parseBlock();
			return stmt_idx;
		}
		if (match(Token::FOR)) {
			const i32 stmt_idx = addStmt(Stmt::FOR, t);
			parseForBody(stmt_idx);
			m_unit.statements[stmt_idx].right = parseBlock();
			return stmt_idx;
		}
		if (match(Token::IF)) {
			const i32 stmt_idx = addStmt(Stmt::IF, t);
			{
				DisableConstructor guard(*this);
				m_unit.statements[stmt_idx].expr = parseExpression();
			}
			m_unit.statements[stmt_idx].left = parseBlock();
			if (match(Token::ELSE)) {
				if (peek().type == Token::IF) {
					m_unit.statements[stmt_idx].right = parseStatement();
				} else {
					m_unit.statements[stmt_idx].right = parseBlock();
				}
			}
			return stmt_idx;
		}
		if (match(Token::MATCH)) return parseMatch(t);
		if (match(Token::BREAK)) {
			const i32 stmt_idx = addStmt(Stmt::BREAK, t);
			if (peek().type == Token::IDENTIFIER) {
				m_unit.statements[stmt_idx].name = consumeToken().value;
			}
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::CONTINUE)) {
			const i32 stmt_idx = addStmt(Stmt::CONTINUE, t);
			if (peek().type == Token::IDENTIFIER) {
				m_unit.statements[stmt_idx].name = consumeToken().value;
			}
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::RETURN)) {
			const i32 stmt_idx = addStmt(Stmt::RETURN, t);
			if (peek().type != Token::SEMICOLON) m_unit.statements[stmt_idx].expr = parseExpression();
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::DEFER)) {
			const i32 stmt_idx = addStmt(Stmt::DEFER, t);
			m_unit.statements[stmt_idx].left = parseStatement();
			return stmt_idx;
		}

		const i32 left = parseExpression();
		Token op = peek();
		if (op.type == Token::EQUAL || op.type == Token::PLUS_EQUAL || op.type == Token::MINUS_EQUAL || op.type == Token::STAR_EQUAL || op.type == Token::SLASH_EQUAL) {
			consumeToken();
			const i32 stmt_idx = addStmt(Stmt::ASSIGN, op);
			m_unit.statements[stmt_idx].left = left;
			m_unit.statements[stmt_idx].right = parseExpression();
			m_unit.statements[stmt_idx].assign_op = op.type;
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (op.type == Token::PLUS_PLUS || op.type == Token::MINUS_MINUS) {
			consumeToken();
			const i32 stmt_idx = addStmt(Stmt::ASSIGN, op);
			const i32 one = addExpr(Expr::NUMBER, op);
			m_unit.expressions[one].number = 1;
			m_unit.expressions[one].type = {LS_TYPE_UNTYPED_INT, {}, -1};
			m_unit.statements[stmt_idx].left = left;
			m_unit.statements[stmt_idx].right = one;
			m_unit.statements[stmt_idx].assign_op = op.type == Token::PLUS_PLUS ? Token::PLUS_EQUAL : Token::MINUS_EQUAL;
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		const i32 stmt_idx = addStmt(Stmt::EXPR, t);
		m_unit.statements[stmt_idx].expr = left;
		consume(Token::SEMICOLON);
		return stmt_idx;
	}

	i32 parseMatch(Token token) {
		const i32 stmt_idx = addStmt(Stmt::MATCH, token);
		{
			DisableConstructor guard(*this);
			m_unit.statements[stmt_idx].expr = parseExpression();
		}
		if (!consume(Token::LEFT_BRACE)) return stmt_idx;
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
			Token case_token;
			const bool is_else = peek().type == Token::ELSE;
			if (is_else) {
				consume(Token::ELSE, &case_token);
			} else if (!consume(Token::CASE, &case_token)) {
				return stmt_idx;
			}
			const i32 arm_idx = addMatchArm(case_token);
			MatchArm& arm = m_unit.match_arms[arm_idx];
			if (is_else) {
				MatchPattern& pattern = arm.patterns.emplace_back();
				pattern.kind = MatchPattern::DEFAULT;
				pattern.token = case_token;
			} else {
				for (;;) {
					Token pattern_token = peek();
					const i32 start_expr = parseExpression();
					MatchPattern& pattern = arm.patterns.emplace_back();
					pattern.kind = MatchPattern::VALUE;
					pattern.token = pattern_token;
					pattern.start_expr = start_expr;
					if (match(Token::RANGE)) {
						pattern.kind = MatchPattern::RANGE;
						pattern.end_expr = parseExpression();
					}
					if (!match(Token::COMMA)) break;
				}
			}
			consume(Token::COLON);
			const i32 block = addStmt(Stmt::BLOCK, case_token);
			while (peek().type != Token::CASE && peek().type != Token::ELSE && peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
				const i32 child = parseStatement();
				if (child >= 0) m_unit.statements[block].children.push_back(child);
			}
			arm.stmt = block;
			m_unit.statements[stmt_idx].children.push_back(arm_idx);
		}
		consume(Token::RIGHT_BRACE);
		return stmt_idx;
	}

	i32 parseExpression() { return parseBinary(1); }

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

	i32 parseBinary(int min_prec) {
		i32 left = parseCast();
		for (;;) {
			Token op = peek();
			const int prec = precedence(op.type);
			if (prec < min_prec) break;
			consumeToken();
			const i32 right = parseBinary(prec + 1);
			const i32 expr_idx = addExpr(Expr::BINARY, op);
			m_unit.expressions[expr_idx].left = left;
			m_unit.expressions[expr_idx].right = right;
			left = expr_idx;
		}
		return left;
	}

	i32 parseUnary() {
		Token t = peek();
		if (match(Token::REF)) {
			const i32 idx = addExpr(Expr::REF, t);
			m_unit.expressions[idx].right = parseUnary();
			return idx;
		}
		if (match(Token::MINUS) || match(Token::NOT)) {
			const i32 idx = addExpr(Expr::UNARY, t);
			m_unit.expressions[idx].right = parseUnary();
			return idx;
		}
		return parsePostfix();
	}

	i32 parseCast() {
		i32 expr_idx = parseUnary();
		while (peek().type == Token::AS) {
			Token t = consumeToken();
			const i32 idx = addExpr(Expr::CAST, t);
			m_unit.expressions[idx].left = expr_idx;
			m_unit.expressions[idx].cast_type = parseType();
			expr_idx = idx;
		}
		return expr_idx;
	}

	ls_string_view getExpressionName(i32 expr_idx) {
		Expr& e = m_unit.expressions[expr_idx];
		if (e.kind == Expr::VAR) return e.name;
		if (e.kind == Expr::FIELD) {
			if (empty(e.qualified_name)) e.qualified_name = {getExpressionName(e.left), e.name};
			return m_module.makeQualifiedName(e.qualified_name.path, e.qualified_name.name);
		}
		return {};
	}

	i32 parsePostfix() {
		i32 expr_idx = parsePrimary();
		for (;;) {
			Token t = peek();
			if (match(Token::DOT)) {
				Token field;
				consume(Token::IDENTIFIER, &field);
				const i32 idx = addExpr(Expr::FIELD, field);
				m_unit.expressions[idx].left = expr_idx;
				m_unit.expressions[idx].name = field.value;
				expr_idx = idx;
			} else if (match(Token::LEFT_PAREN)) {
				const i32 idx = addExpr(Expr::CALL, t);
				m_unit.expressions[idx].left = expr_idx;
				while (peek().type != Token::RIGHT_PAREN && peek().type != Token::END_OF_FILE && !m_output.has_error) {
					if (!m_unit.expressions[idx].args.empty()) consume(Token::COMMA);
					const i32 arg = parseExpression();
					m_unit.expressions[idx].args.push_back(arg);
				}
				consume(Token::RIGHT_PAREN);
				expr_idx = idx;
			} else if (match(Token::LEFT_BRACKET)) {
				if (peek().type == Token::COLON) {
					consumeToken();
					const i32 idx = addExpr(Expr::SLICE, t);
					m_unit.expressions[idx].left = expr_idx;
					m_unit.expressions[idx].right = -1;
					m_unit.expressions[idx].method_receiver = peek().type != Token::RIGHT_BRACKET ? parseExpression() : -1;
					consume(Token::RIGHT_BRACKET);
					expr_idx = idx;
				} else {
					const i32 start_expr = parseExpression();
					if (peek().type == Token::COLON) {
						consumeToken();
						const i32 idx = addExpr(Expr::SLICE, t);
						m_unit.expressions[idx].left = expr_idx;
						m_unit.expressions[idx].right = start_expr;
						m_unit.expressions[idx].method_receiver = peek().type != Token::RIGHT_BRACKET ? parseExpression() : -1;
						consume(Token::RIGHT_BRACKET);
						expr_idx = idx;
					} else {
						const i32 idx = addExpr(Expr::INDEX, t);
						m_unit.expressions[idx].left = expr_idx;
						m_unit.expressions[idx].right = start_expr;
						consume(Token::RIGHT_BRACKET);
						expr_idx = idx;
					}
				}
			} else if (m_allow_constructor && peek().type == Token::LEFT_BRACE && !empty(getExpressionName(expr_idx))) {
				consumeToken();
				ls_string_view name = getExpressionName(expr_idx);
				const i32 idx = addExpr(Expr::CONSTRUCTOR, t);
				m_unit.expressions[idx].name = name;
				while (peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
					if (!m_unit.expressions[idx].args.empty()) consume(Token::COMMA);
					const i32 arg = parseExpression();
					m_unit.expressions[idx].args.push_back(arg);
				}
				consume(Token::RIGHT_BRACE);
				expr_idx = idx;
			} else
				break;
		}
		return expr_idx;
	}

	i32 parsePrimary() {
		Token t = consumeToken();
		switch (t.type) {
			case Token::NUMBER: {
				const i32 idx = addExpr(Expr::NUMBER, t);
				double parsed = 0;
				fromCString(t.value, parsed);
				m_unit.expressions[idx].number = parsed;
				m_unit.expressions[idx].type = contains(t.value, '.') ? TypeRef{LS_TYPE_UNTYPED_FLOAT, {}, -1} : TypeRef{LS_TYPE_UNTYPED_INT, {}, -1};
				return idx;
			}
			case Token::STRING: {
				const i32 idx = addExpr(Expr::STRING_LITERAL, t);
				m_unit.expressions[idx].string = t.value;
				m_unit.expressions[idx].type = {LS_TYPE_STRING, {}, -1};
				return idx;
			}
			case Token::TRUE:
			case Token::FALSE: {
				const i32 idx = addExpr(Expr::BOOL_LITERAL, t);
				m_unit.expressions[idx].is_true_literal = t.type == Token::TRUE;
				m_unit.expressions[idx].type = {LS_TYPE_BOOL, {}, -1};
				return idx;
			}
			case Token::NULL_KW: {
				const i32 idx = addExpr(Expr::NULL_LITERAL, t);
				m_unit.expressions[idx].type = {LS_TYPE_NULL_VALUE, {}, -1};
				return idx;
			}
			case Token::IDENTIFIER: {
				const i32 var_idx = addExpr(Expr::VAR, t);
				m_unit.expressions[var_idx].name = t.value;
				return var_idx;
			}
			case Token::DOT: {
				// Enum shorthand syntax: .EnumValue
				Token member_name;
				if (!consume(Token::IDENTIFIER, &member_name)) return -1;
				const i32 idx = addExpr(Expr::ENUM_LITERAL, member_name);
				m_unit.expressions[idx].name = member_name.value;
				m_unit.expressions[idx].type = {LS_TYPE_ENUM, {}, -1}; // Type will be resolved by checker
				return idx;
			}
			case Token::LEFT_PAREN: {
				const i32 res = parseExpression();
				consume(Token::RIGHT_PAREN);
				return res;
			}
			case Token::FN: {
				// `fn` is overloaded here: at declaration level
				// it starts a named function, but inside an
				// expression it introduces an anonymous function
				// literal. This branch is what lets the parser
				// accept `var foo = fn() : i32 { ... }` without
				// needing a dedicated syntax token.
				const i32 fn_idx = parseFunctionLiteral(t);
				if (fn_idx < 0) return -1;
				const i32 idx = addExpr(Expr::FUNCTION_REF, t);
				m_unit.expressions[idx].left = fn_idx;
				m_unit.expressions[idx].is_native_fn = false;
				return idx;
			}
			case Token::LEFT_BRACE: {
				const i32 idx = addExpr(Expr::STRUCT_LITERAL, t);
				while (peek().type != Token::RIGHT_BRACE && peek().type != Token::END_OF_FILE && !m_output.has_error) {
					if (!m_unit.expressions[idx].args.empty()) consume(Token::COMMA);
					const i32 arg = parseExpression();
					m_unit.expressions[idx].args.push_back(arg);
				}
				consume(Token::RIGHT_BRACE);
				return idx;
			}
			default: error(t, "Expected expression"); return -1;
		}
	}

	Tokenizer m_tokenizer;
	ls_module& m_module;
	Unit& m_unit;
	OutputFormatter m_output;
	ls_string_view m_declaration_prefix;
	bool m_allow_constructor = true;
	i32 m_operator_counter = 0;
	i32 m_function_literal_counter = 0;
};

bool parse(ls_module& module, ls_string_view source, ls_string_view declaration_prefix, ls_string_view source_name) {
	Unit& unit = module.addUnit(source_name);
	Parser parser(module, unit, declaration_prefix, source, source_name);
	return parser.parse();
}

extern "C" {

ls_result ls_module_parse(ls_module* module, ls_string_view source, ls_string_view source_name) {
	if (!module) return LS_RESULT_FAILURE;
	return parse(*module, source, {}, source_name) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}
}
