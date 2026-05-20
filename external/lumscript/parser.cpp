#pragma once

#include "core/string.h"
#include "ast.h"
#include "lumscript.h"
#include "tokenizer.h"

namespace Lumix::LumScript {

struct Parser {
	Parser(Module& module, Callbacks& diagnostics, StringView declaration_prefix, StringView source, StringView source_name = {})
		: m_module(module)
		, m_callbacks(diagnostics)
		, m_declaration_prefix(declaration_prefix)
	{
		m_tokenizer.init(source, source_name);
	}

	bool parse() {
		while (peek().type != Token::EOF && !m_callbacks.has_error) {
			if (match(Token::IMPORT)) parseImport();
			else if (match(Token::STRUCT)) parseStruct();
			else if (match(Token::ENUM)) parseEnum();
			else if (match(Token::FN)) parseFunction(false);
			else if (peek().type == Token::VAR || peek().type == Token::CONST) parseGlobal();
			else error(peek(), "Expected declaration");
		}
		return !m_callbacks.has_error;
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
		m_callbacks.errorAt(token, msg, " near '", token.value, "'");
	}

	StringView parseQualifiedIdentifier(Token first) {
		StringView name = first.value;
		while (match(Token::DOT)) {
			Token part;
			if (!consume(Token::IDENTIFIER, &part)) return name;
			name = m_module.makeQualifiedName(name, part.value);
		}
		return name;
	}

	void parseImport() {
		Token path;
		if (!consume(Token::STRING, &path)) return;
		ImportDecl& import = m_module.imports.emplace();
		import.path = path.value;
		import.token = path;
		if (match(Token::AS)) {
			Token alias;
			if (!consume(Token::IDENTIFIER, &alias)) return;
			import.alias = alias.value;
		}
		match(Token::SEMICOLON);
	}

	TypeRef parseType() {
		const bool is_nullable = match(Token::QUESTION);
		Token t = consumeToken();
		TypeRef base;
		switch (t.type) {
			case Token::FN: {
				FunctionTypeDecl& fn_type = m_module.function_types.emplace(m_module.allocator);
				const i32 fn_type_idx = m_module.function_types.size() - 1;
				if (!consume(Token::LEFT_PAREN)) return {};
				while (peek().type != Token::RIGHT_PAREN && peek().type != Token::EOF && !m_callbacks.has_error) {
					if (!fn_type.params.empty()) consume(Token::COMMA);
					fn_type.params.push(parseType());
				}
				consume(Token::RIGHT_PAREN);
				consume(Token::COLON);
				fn_type.return_type = parseType();
				base = {TypeRef::FUNCTION, {}, fn_type_idx, t, is_nullable};
				break;
			}
			case Token::VOID: base = {TypeRef::VOID, t.value, -1, t, is_nullable}; break;
			case Token::BOOL: base = {TypeRef::BOOL, t.value, -1, t, is_nullable}; break;
			case Token::I8: base = {TypeRef::I8, t.value, -1, t, is_nullable}; break;
			case Token::U8: base = {TypeRef::U8, t.value, -1, t, is_nullable}; break;
			case Token::I16: base = {TypeRef::I16, t.value, -1, t, is_nullable}; break;
			case Token::U16: base = {TypeRef::U16, t.value, -1, t, is_nullable}; break;
			case Token::I32: base = {TypeRef::I32, t.value, -1, t, is_nullable}; break;
			case Token::U32: base = {TypeRef::U32, t.value, -1, t, is_nullable}; break;
			case Token::I64: base = {TypeRef::I64, t.value, -1, t, is_nullable}; break;
			case Token::U64: base = {TypeRef::U64, t.value, -1, t, is_nullable}; break;
			case Token::F32: base = {TypeRef::F32, t.value, -1, t, is_nullable}; break;
			case Token::F64: base = {TypeRef::F64, t.value, -1, t, is_nullable}; break;
			case Token::IDENTIFIER:
				if (equalStrings(t.value, "string")) base = {TypeRef::STRING, t.value, -1, t, is_nullable};
				else base = {TypeRef::STRUCT, m_module.makeQualifiedName(m_declaration_prefix, parseQualifiedIdentifier(t)), -1, t, is_nullable};
				break;
			default: error(t, "Expected type"); return {};
		}
		if (match(Token::LEFT_BRACKET)) {
			Token size_token;
			if (!consume(Token::NUMBER, &size_token)) return {};
			if (!consume(Token::RIGHT_BRACKET)) return {};
			if (contains(size_token.value, '.')) {
				error(size_token, "Array size must be an integer literal");
				return {};
			}
			TypeRef array_type;
			array_type.kind = TypeRef::ARRAY;
			array_type.token = t;
			array_type.nullable = is_nullable;
			array_type.element_kind = base.kind;
			array_type.element_name = base.name;
			array_type.struct_index = base.struct_index;
			fromCString(size_token.value, array_type.array_size);
			if (array_type.array_size <= 0) {
				error(size_token, "Array size must be a positive integer");
				return {};
			}
			return array_type;
		}
		return base;
	}

	void parseStruct() {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		StructDecl& s = m_module.structs.emplace(m_module.allocator);
		s.name = m_module.makeQualifiedName(m_declaration_prefix, name.value);
		s.token = name;
		if (!consume(Token::LEFT_BRACE)) return;
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
			Token field_name;
			if (!consume(Token::IDENTIFIER, &field_name)) return;
			if (!consume(Token::COLON)) return;
			FieldDecl& field = s.fields.emplace();
			field.name = field_name.value;
			field.token = field_name;
			field.type = parseType();
			consume(Token::SEMICOLON);
		}
		consume(Token::RIGHT_BRACE);
		match(Token::SEMICOLON);
	}

	void parseEnum() {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		EnumDecl& e = m_module.enums.emplace(m_module.allocator);
		e.name = m_module.makeQualifiedName(m_declaration_prefix, name.value);
		e.token = name;
		if (!consume(Token::LEFT_BRACE)) return;
		i32 auto_value = 0;
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
			Token member_name;
			if (!consume(Token::IDENTIFIER, &member_name)) return;
			EnumMember& member = e.members.emplace();
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
		match(Token::SEMICOLON);
	}

	i32 parseFunction(bool is_nested) {
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return -1;
		FunctionDecl& fn = m_module.functions.emplace(m_module.allocator);
		fn.local_name = name.value;
		fn.is_nested = is_nested;
		if (is_nested) {
			StaticString<256> unique_name("__nested_fn.", m_nested_function_counter++, ".", name.value);
			fn.name = m_module.copyName(unique_name);
		}
		else {
			fn.name = m_module.makeQualifiedName(m_declaration_prefix, name.value);
		}
		fn.token = name;
		const i32 fn_idx = m_module.functions.size() - 1;
		if (!consume(Token::LEFT_PAREN)) return fn_idx;
		while (peek().type != Token::RIGHT_PAREN && peek().type != Token::EOF && !m_callbacks.has_error) {
			if (!fn.params.empty()) consume(Token::COMMA);
			Token param_name;
			if (!consume(Token::IDENTIFIER, &param_name)) return fn_idx;
			consume(Token::COLON);
			Param& p = fn.params.emplace();
			p.name = param_name.value;
			p.token = param_name;
			p.is_ref = match(Token::REF);
			p.type = parseType();
		}
		consume(Token::RIGHT_PAREN);
		consume(Token::COLON);
		fn.return_type = parseType();
		fn.body = parseBlock();
		return fn_idx;
	}

	void parseGlobal() {
		Token t = consumeToken();
		const bool is_const = t.type == Token::CONST;
		Token name;
		if (!consume(Token::IDENTIFIER, &name)) return;
		GlobalDecl& global = m_module.globals.emplace();
		global.name = m_module.makeQualifiedName(m_declaration_prefix, name.value);
		global.token = name;
		global.is_const = is_const;
		if (match(Token::COLON)) global.type = parseType();
		if (match(Token::EQUAL)) {
			if (peek().type == Token::IDENTIFIER && equalStrings(peek().value, "undefined")) {
				consumeToken();
				global.is_undefined_init = true;
			}
			else {
				global.expr = parseExpression();
			}
		}
		consume(Token::SEMICOLON);
	}

	i32 addStmt(Stmt::Kind kind, Token token) {
		Stmt& stmt = m_module.statements.emplace(m_module.allocator);
		stmt.kind = kind;
		stmt.token = token;
		return m_module.statements.size() - 1;
	}

	i32 addExpr(Expr::Kind kind, Token token) {
		Expr& expr = m_module.expressions.emplace(m_module.allocator);
		expr.kind = kind;
		expr.token = token;
		return m_module.expressions.size() - 1;
	}

	i32 addMatchPattern(MatchPattern::Kind kind, Token token) {
		MatchPattern& pattern = m_module.match_patterns.emplace();
		pattern.kind = kind;
		pattern.token = token;
		return m_module.match_patterns.size() - 1;
	}

	i32 addMatchArm(Token token) {
		MatchArm& arm = m_module.match_arms.emplace(m_module.allocator);
		arm.token = token;
		return m_module.match_arms.size() - 1;
	}

	i32 parseBlock() {
		Token t = consumeToken();
		if (t.type != Token::LEFT_BRACE) {
			error(t, "Expected block");
			return -1;
		}
		const i32 block = addStmt(Stmt::BLOCK, t);
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
			const i32 child = parseStatement();
			m_module.statements[block].children.push(child);
		}
		consume(Token::RIGHT_BRACE);
		return block;
	}

	i32 parseStatement() {
		Token t = peek();
		if (t.type == Token::IDENTIFIER && peekNext().type == Token::COLON) {
			Token label = consumeToken();
			consume(Token::COLON);
			if (!match(Token::WHILE)) {
				error(peek(), "Only while can be labeled");
				return -1;
			}
			const i32 stmt_idx = addStmt(Stmt::WHILE, t);
			m_module.statements[stmt_idx].name = label.value;
			const bool old_allow_constructor = m_allow_constructor;
			m_allow_constructor = false;
			m_module.statements[stmt_idx].expr = parseExpression();
			m_allow_constructor = old_allow_constructor;
			m_module.statements[stmt_idx].right = parseBlock();
			return stmt_idx;
		}
		if (t.type == Token::LEFT_BRACE) return parseBlock();
		if (match(Token::FN)) {
			const i32 fn_idx = parseFunction(true);
			const i32 stmt_idx = addStmt(Stmt::FN_DECL, t);
			m_module.statements[stmt_idx].left = fn_idx;
			return stmt_idx;
		}
		if (match(Token::VAR) || match(Token::CONST)) {
			const bool is_const = t.type == Token::CONST;
			Token name;
			if (!consume(Token::IDENTIFIER, &name)) return -1;
			const i32 stmt_idx = addStmt(Stmt::VAR_DECL, t);
			Stmt& stmt = m_module.statements[stmt_idx];
			stmt.name = name.value;
			stmt.is_const = is_const;
			if (match(Token::COLON)) stmt.type = parseType();
			if (match(Token::EQUAL)) {
				if (peek().type == Token::IDENTIFIER && equalStrings(peek().value, "undefined")) {
					consumeToken();
					stmt.is_undefined_init = true;
				}
				else {
					stmt.expr = parseExpression();
				}
			}
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::WHILE)) {
			const i32 stmt_idx = addStmt(Stmt::WHILE, t);
			const bool old_allow_constructor = m_allow_constructor;
			m_allow_constructor = false;
			m_module.statements[stmt_idx].expr = parseExpression();
			m_allow_constructor = old_allow_constructor;
			m_module.statements[stmt_idx].right = parseBlock();
			return stmt_idx;
		}
		if (match(Token::IF)) {
			const i32 stmt_idx = addStmt(Stmt::IF, t);
			const bool old_allow_constructor = m_allow_constructor;
			m_allow_constructor = false;
			m_module.statements[stmt_idx].expr = parseExpression();
			m_allow_constructor = old_allow_constructor;
			m_module.statements[stmt_idx].left = parseBlock();
			if (match(Token::ELSE)) {
				if (peek().type == Token::IF) {
					m_module.statements[stmt_idx].right = parseStatement();
				}
				else {
					m_module.statements[stmt_idx].right = parseBlock();
				}
			}
			return stmt_idx;
		}
		if (match(Token::MATCH)) return parseMatch(t);
		if (match(Token::BREAK)) {
			const i32 stmt_idx = addStmt(Stmt::BREAK, t);
			if (peek().type == Token::IDENTIFIER) {
				m_module.statements[stmt_idx].name = consumeToken().value;
			}
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::CONTINUE)) {
			const i32 stmt_idx = addStmt(Stmt::CONTINUE, t);
			if (peek().type == Token::IDENTIFIER) {
				m_module.statements[stmt_idx].name = consumeToken().value;
			}
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::RETURN)) {
			const i32 stmt_idx = addStmt(Stmt::RETURN, t);
			if (peek().type != Token::SEMICOLON) m_module.statements[stmt_idx].expr = parseExpression();
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (match(Token::DEFER)) {
			const i32 stmt_idx = addStmt(Stmt::DEFER, t);
			m_module.statements[stmt_idx].left = parseStatement();
			return stmt_idx;
		}

		const i32 left = parseExpression();
		Token op = peek();
		if (op.type == Token::EQUAL || op.type == Token::PLUS_EQUAL || op.type == Token::MINUS_EQUAL || op.type == Token::STAR_EQUAL || op.type == Token::SLASH_EQUAL) {
			consumeToken();
			const i32 stmt_idx = addStmt(Stmt::ASSIGN, op);
			m_module.statements[stmt_idx].left = left;
			m_module.statements[stmt_idx].right = parseExpression();
			m_module.statements[stmt_idx].assign_op = op.type;
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		if (op.type == Token::PLUS_PLUS || op.type == Token::MINUS_MINUS) {
			consumeToken();
			const i32 stmt_idx = addStmt(Stmt::ASSIGN, op);
			const i32 one = addExpr(Expr::NUMBER, op);
			m_module.expressions[one].number = 1;
			m_module.expressions[one].type = {TypeRef::UNTYPED_INT, {}, -1};
			m_module.statements[stmt_idx].left = left;
			m_module.statements[stmt_idx].right = one;
			m_module.statements[stmt_idx].assign_op = op.type == Token::PLUS_PLUS ? Token::PLUS_EQUAL : Token::MINUS_EQUAL;
			consume(Token::SEMICOLON);
			return stmt_idx;
		}
		const i32 stmt_idx = addStmt(Stmt::EXPR, t);
		m_module.statements[stmt_idx].expr = left;
		consume(Token::SEMICOLON);
		return stmt_idx;
	}

	i32 parseMatch(Token token) {
		const i32 stmt_idx = addStmt(Stmt::MATCH, token);
		m_allow_constructor = false;
		m_module.statements[stmt_idx].expr = parseExpression();
		m_allow_constructor = true;
		if (!consume(Token::LEFT_BRACE)) return stmt_idx;
		while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
			Token case_token;
			if (!consume(Token::CASE, &case_token)) return stmt_idx;
			const i32 arm_idx = addMatchArm(case_token);
			MatchArm& arm = m_module.match_arms[arm_idx];
			for (;;) {
				Token pattern_token = peek();
				if (pattern_token.type == Token::IDENTIFIER && equalStrings(pattern_token.value, "_")) {
					consumeToken();
					arm.patterns.push(addMatchPattern(MatchPattern::DEFAULT, pattern_token));
				}
				else {
					const i32 start_expr = parseExpression();
					const i32 pattern_idx = addMatchPattern(MatchPattern::VALUE, pattern_token);
					MatchPattern& pattern = m_module.match_patterns[pattern_idx];
					pattern.start_expr = start_expr;
					if (match(Token::RANGE)) {
						pattern.kind = MatchPattern::RANGE;
						pattern.end_expr = parseExpression();
					}
					arm.patterns.push(pattern_idx);
				}
				if (!match(Token::COMMA)) break;
			}
			consume(Token::COLON);
			const i32 block = addStmt(Stmt::BLOCK, case_token);
			while (peek().type != Token::CASE && peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
				const i32 child = parseStatement();
				if (child >= 0) m_module.statements[block].children.push(child);
			}
			arm.stmt = block;
			m_module.statements[stmt_idx].children.push(arm_idx);
		}
		consume(Token::RIGHT_BRACE);
		return stmt_idx;
	}

	i32 parseExpression() { return parseBinary(1); }

	static int precedence(Token::Type type) {
		switch (type) {
			case Token::OR: return 1;
			case Token::AND: return 2;
			case Token::EQUAL_EQUAL: case Token::BANG_EQUAL: return 3;
			case Token::GT: case Token::LT: case Token::GT_EQUAL: case Token::LT_EQUAL: return 4;
			case Token::PLUS: case Token::MINUS: return 5;
			case Token::STAR: case Token::SLASH: case Token::PERCENT: return 6;
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
			m_module.expressions[expr_idx].left = left;
			m_module.expressions[expr_idx].right = right;
			left = expr_idx;
		}
		return left;
	}

	i32 parseUnary() {
		Token t = peek();
		if (match(Token::REF)) {
			const i32 idx = addExpr(Expr::REF, t);
			m_module.expressions[idx].right = parseUnary();
			return idx;
		}
		if (match(Token::MINUS) || match(Token::NOT)) {
			const i32 idx = addExpr(Expr::UNARY, t);
			m_module.expressions[idx].right = parseUnary();
			return idx;
		}
		return parsePostfix();
	}

	i32 parseCast() {
		i32 expr_idx = parseUnary();
		while (peek().type == Token::AS) {
			Token t = consumeToken();
			const i32 idx = addExpr(Expr::CAST, t);
			m_module.expressions[idx].left = expr_idx;
			m_module.expressions[idx].cast_type = parseType();
			expr_idx = idx;
		}
		return expr_idx;
	}

	StringView getExpressionName(i32 expr_idx) {
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return e.name;
		if (e.kind == Expr::FIELD) {
			if (e.qualified_name.empty()) e.qualified_name = m_module.makeQualifiedName(getExpressionName(e.left), e.name);
			return e.qualified_name;
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
				m_module.expressions[idx].left = expr_idx;
				m_module.expressions[idx].name = field.value;
				expr_idx = idx;
			}
			else if (match(Token::LEFT_PAREN)) {
				const i32 idx = addExpr(Expr::CALL, t);
				m_module.expressions[idx].left = expr_idx;
				while (peek().type != Token::RIGHT_PAREN && peek().type != Token::EOF && !m_callbacks.has_error) {
					if (!m_module.expressions[idx].args.empty()) consume(Token::COMMA);
					const i32 arg = parseExpression();
					m_module.expressions[idx].args.push(arg);
				}
				consume(Token::RIGHT_PAREN);
				expr_idx = idx;
			}
			else if (match(Token::LEFT_BRACKET)) {
				const i32 idx = addExpr(Expr::INDEX, t);
				m_module.expressions[idx].left = expr_idx;
				m_module.expressions[idx].right = parseExpression();
				consume(Token::RIGHT_BRACKET);
				expr_idx = idx;
			}
			else if (m_allow_constructor && peek().type == Token::LEFT_BRACE && !getExpressionName(expr_idx).empty()) {
				consumeToken();
				StringView name = getExpressionName(expr_idx);
				const i32 idx = addExpr(Expr::CONSTRUCTOR, t);
				m_module.expressions[idx].name = name;
				while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
					if (!m_module.expressions[idx].args.empty()) consume(Token::COMMA);
					const i32 arg = parseExpression();
					m_module.expressions[idx].args.push(arg);
				}
				consume(Token::RIGHT_BRACE);
				expr_idx = idx;
			}
			else break;
		}
		return expr_idx;
	}

	i32 parsePrimary() {
		Token t = consumeToken();
		switch (t.type) {
			case Token::NUMBER: {
				const i32 idx = addExpr(Expr::NUMBER, t);
				float parsed = 0;
				fromCString(t.value, parsed);
				m_module.expressions[idx].number = parsed;
				m_module.expressions[idx].type = contains(t.value, '.') ? TypeRef{TypeRef::UNTYPED_FLOAT, {}, -1} : TypeRef{TypeRef::UNTYPED_INT, {}, -1};
				return idx;
			}
			case Token::STRING: {
				const i32 idx = addExpr(Expr::STRING_LITERAL, t);
				m_module.expressions[idx].string = t.value;
				m_module.expressions[idx].type = {TypeRef::STRING, {}, -1};
				return idx;
			}
			case Token::TRUE:
			case Token::FALSE: {
				const i32 idx = addExpr(Expr::BOOL_LITERAL, t);
				m_module.expressions[idx].boolean = t.type == Token::TRUE;
				m_module.expressions[idx].type = {TypeRef::BOOL, {}, -1};
				return idx;
			}
			case Token::NULL_KW: {
				const i32 idx = addExpr(Expr::NULL_LITERAL, t);
				m_module.expressions[idx].type = {TypeRef::NULL_VALUE, {}, -1};
				return idx;
			}
			case Token::IDENTIFIER: {
				const i32 var_idx = addExpr(Expr::VAR, t);
				m_module.expressions[var_idx].name = t.value;
				if (m_allow_constructor && peek().type == Token::LEFT_BRACE) {
					consumeToken();
					const i32 idx = addExpr(Expr::CONSTRUCTOR, t);
					m_module.expressions[idx].name = t.value;
					while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
						if (!m_module.expressions[idx].args.empty()) consume(Token::COMMA);
						const i32 arg = parseExpression();
						m_module.expressions[idx].args.push(arg);
					}
					consume(Token::RIGHT_BRACE);
					return idx;
				}
				return var_idx;
			}
			case Token::DOT: {
				// Enum shorthand syntax: .EnumValue
				Token member_name;
				if (!consume(Token::IDENTIFIER, &member_name)) return -1;
				const i32 idx = addExpr(Expr::ENUM_LITERAL, member_name);
				m_module.expressions[idx].name = member_name.value;
				m_module.expressions[idx].type = {TypeRef::ENUM, {}, -1};  // Type will be resolved by checker
				return idx;
			}
			case Token::LEFT_PAREN: {
				const i32 res = parseExpression();
				consume(Token::RIGHT_PAREN);
				return res;
			}
			case Token::LEFT_BRACE: {
				const i32 idx = addExpr(Expr::STRUCT_LITERAL, t);
				while (peek().type != Token::RIGHT_BRACE && peek().type != Token::EOF && !m_callbacks.has_error) {
					if (!m_module.expressions[idx].args.empty()) consume(Token::COMMA);
					const i32 arg = parseExpression();
					m_module.expressions[idx].args.push(arg);
				}
				consume(Token::RIGHT_BRACE);
				return idx;
			}
			default: error(t, "Expected expression"); return -1;
		}
	}

	Tokenizer m_tokenizer;
	Module& m_module;
	Callbacks& m_callbacks;
	StringView m_declaration_prefix;
	bool m_allow_constructor = true;
	i32 m_nested_function_counter = 0;
};

bool parse(Module& module, StringView source, Callbacks& diagnostics, StringView declaration_prefix, StringView source_name) {
	Parser parser(module, diagnostics, declaration_prefix, source, source_name);
	return parser.parse();
}

} // namespace Lumix::LumScript
