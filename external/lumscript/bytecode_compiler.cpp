#include "bytecode.h"

#include "ast.h"

ls_bytecode::ls_bytecode(const ls_host* host_)
	: host(host_ ? *host_ : ls_host{})
{}

ls_bytecode::~ls_bytecode() {
	for (ls_string* s : string_literals) {
		if (s && --s->ref_count == 0) deallocateMemory(&s->host, s);
	}
}

template <typename T>
void pushCode(std::vector<u8>& code, const T& value) {
	// Bytecode is emitted into a flat byte buffer. We append raw bytes for
	// opcodes and immediates here because the compiler controls the exact layout.
	size_t s = code.size();
	code.resize(s + sizeof(value));
	memcpy(&code[s], &value, sizeof(value));
}

template <typename T>
void pushCode(ls_bytecode& bytecode, const T& value) {
	pushCode(bytecode.code, value);
}

static bool isBytecodeIntegralType(ls_type_kind kind) {
	// Only integer-like types participate in the arithmetic opcodes below.
	return kind == LS_TYPE_I8 || kind == LS_TYPE_U8
		|| kind == LS_TYPE_I16 || kind == LS_TYPE_U16
		|| kind == LS_TYPE_I32 || kind == LS_TYPE_U32
		|| kind == LS_TYPE_I64 || kind == LS_TYPE_U64
		|| kind == LS_TYPE_UNTYPED_INT;
}

static bool isBytecodeFloatType(ls_type_kind kind) {
	return kind == LS_TYPE_F32 || kind == LS_TYPE_F64 || kind == LS_TYPE_UNTYPED_FLOAT;
}

static ls_type toC(TypeRef type) {
	ls_type result = {};
	result.kind = type.kind;
	result.name = type.unresolved_name;
	result.struct_index = type.struct_index;
	result.element_kind = type.element_kind;
	result.element_name = type.element_name;
	result.array_size = type.array_size;
	result.nullable = type.nullable ? 1 : 0;
	return result;
}

static u64 encodeIntegerLiteral(double value, ls_type_kind kind) {
	// Literals are parsed as floating point first, then narrowed to the target
	// integer type so the bytecode sees the same value the language expects.
	switch (kind) {
		case LS_TYPE_I8: return (u64)(i64)(i8)value;
		case LS_TYPE_U8: return (u64)(u8)value;
		case LS_TYPE_I16: return (u64)(i64)(i16)value;
		case LS_TYPE_U16: return (u64)(u16)value;
		case LS_TYPE_I32: return (u64)(i64)(i32)value;
		case LS_TYPE_U32: return (u64)(u32)value;
		case LS_TYPE_I64: return (u64)(i64)value;
		case LS_TYPE_U64: return (u64)(u64)value;
		case LS_TYPE_UNTYPED_INT: return (u64)(i64)(i32)value;
		default: ASSERT(false); return 0;
	}
}

static size_t integerByteSize(ls_type_kind kind) {
	// The emitted operand width must match the VM opcode that will consume it.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8:
		case LS_TYPE_U8: return 1;
		case LS_TYPE_I16:
		case LS_TYPE_U16: return 2;
		case LS_TYPE_I32:
		case LS_TYPE_U32: return 4;
		case LS_TYPE_I64:
		case LS_TYPE_U64: return 8;
		default: ASSERT(false); return 0;
	}
}

static BytecodeOp floatAddOp(ls_type_kind kind) {
	switch (kind == LS_TYPE_UNTYPED_FLOAT ? LS_TYPE_F32 : kind) {
		case LS_TYPE_F32: return BytecodeOp::ADD_F32;
		case LS_TYPE_F64: return BytecodeOp::ADD_F64;
		default: ASSERT(false); return BytecodeOp::ADD_F32;
	}
}

static BytecodeOp floatSubOp(ls_type_kind kind) {
	switch (kind == LS_TYPE_UNTYPED_FLOAT ? LS_TYPE_F32 : kind) {
		case LS_TYPE_F32: return BytecodeOp::SUB_F32;
		case LS_TYPE_F64: return BytecodeOp::SUB_F64;
		default: ASSERT(false); return BytecodeOp::SUB_F32;
	}
}

static BytecodeOp floatLoadConstOp(ls_type_kind kind) {
	switch (kind == LS_TYPE_UNTYPED_FLOAT ? LS_TYPE_F32 : kind) {
		case LS_TYPE_F32: return BytecodeOp::LOAD_CONST32;
		case LS_TYPE_F64: return BytecodeOp::LOAD_CONST64;
		default: ASSERT(false); return BytecodeOp::LOAD_CONST32;
	}
}

static ls_type_kind bytecodeCastKind(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_ENUM:
		case LS_TYPE_UNTYPED_INT: return LS_TYPE_I32;
		case LS_TYPE_UNTYPED_FLOAT: return LS_TYPE_F32;
		default: return kind;
	}
}

static u64 bytecodeEncodeFunctionHandle(i32 index) {
	return (u64)(u32)(index + 1);
}

static i32 bytecodeTypeSlotCount(ls_module& module, TypeRef type);
static TypeRef bytecodeNonNullableType(TypeRef type) {
	type.nullable = false;
	return type;
}

static bool bytecodeSplitMemberName(ls_string_view name, ls_string_view* owner, ls_string_view* member) {
	for (const char* c = data(name) + size(name); c != data(name); --c) {
		if (*(c - 1) != '.') continue;
		*owner = ls_string_view{data(name), c - 1};
		*member = ls_string_view{c, data(name) + size(name)};
		return true;
	}
	return false;
}

static ls_string_view bytecodeGetTypeNamespace(ls_module& module, TypeRef type) {
	if (!empty(type.canonical_name.path)) return type.canonical_name.path;
	ls_string_view namespace_name;
	ls_string_view member_name;
	if (!bytecodeSplitMemberName(type.unresolved_name, &namespace_name, &member_name)) return {};
	return namespace_name;
}

// The bytecode VM still uses a flat `u64` stack, so aggregate values are
// represented as consecutive scalar slots rather than boxed objects.
static bool bytecodeStructFieldOffset(ls_module& module, const TypeRef& type, ls_string_view field_name, i32* offset, TypeRef* field_type) {
	if (type.kind != LS_TYPE_STRUCT || type.struct_index < 0) return false;
	i32 struct_idx = type.struct_index;
	const StructDecl* s = nullptr;
	for (const Unit* unit_ptr : module.units) {
		const Unit& unit = *unit_ptr;
		if (struct_idx < (i32)unit.structs.size()) {
			s = &unit.structs[(size_t)struct_idx];
			break;
		}
		struct_idx -= (i32)unit.structs.size();
	}
	if (!s) return false;
	i32 current_offset = 0;
	for (const FieldDecl& field : s->fields) {
		const i32 field_slots = bytecodeTypeSlotCount(module, field.type);
		if (equalStrings(field.name, field_name)) {
			if (offset) *offset = current_offset;
			if (field_type) *field_type = field.type;
			return true;
		}
		current_offset += field_slots;
	}
	return false;
}

static i32 bytecodeTypeSlotCount(ls_module& module, TypeRef type) {
	if (type.nullable) return 1 + bytecodeTypeSlotCount(module, bytecodeNonNullableType(type));
	switch (type.kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : type.kind) {
		case LS_TYPE_BOOL:
		case LS_TYPE_CPTR:
		case LS_TYPE_I8:
		case LS_TYPE_U8:
		case LS_TYPE_I16:
		case LS_TYPE_U16:
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_I64:
		case LS_TYPE_U64:
		case LS_TYPE_F32:
		case LS_TYPE_F64:
		case LS_TYPE_STRING:
		case LS_TYPE_ENUM:
		case LS_TYPE_FUNCTION:
		case LS_TYPE_NULL_VALUE:
		case LS_TYPE_UNTYPED_FLOAT:
			return 1;
		case LS_TYPE_STRUCT: {
			if (type.struct_index < 0) return 0;
			i32 struct_idx = type.struct_index;
			const StructDecl* s = nullptr;
			for (const Unit* unit_ptr : module.units) {
				const Unit& unit = *unit_ptr;
				if (struct_idx < (i32)unit.structs.size()) {
					s = &unit.structs[(size_t)struct_idx];
					break;
				}
				struct_idx -= (i32)unit.structs.size();
			}
			if (!s) return 0;
			i32 count = 0;
			for (const FieldDecl& field : s->fields) {
				const i32 field_slots = bytecodeTypeSlotCount(module, field.type);
				if (field_slots <= 0) return 0;
				count += field_slots;
			}
			return count;
		}
		case LS_TYPE_ARRAY: {
			TypeRef elem(type.element_kind, type.element_name, type.struct_index, type.token, false);
			const i32 elem_slots = bytecodeTypeSlotCount(module, elem);
			if (elem_slots <= 0 || type.array_size <= 0) return 0;
			return elem_slots * type.array_size;
		}
		case LS_TYPE_VOID: return 0;
		default: ASSERT(false); return 0;
	}
}

enum class BytecodeValueKind {
	LOCAL,
	PARAM,
	GLOBAL
};

struct BytecodeValueAccess {
	// The compiler lowers every assignable expression to one of three storage
	// classes. `slot` points at the first flattened slot for that storage, while
	// `value_slot_offset` lets field access walk deeper into a struct without
	// changing the underlying storage kind.
	BytecodeValueKind kind = BytecodeValueKind::LOCAL;
	i32 slot = -1;
	i32 value_slot_offset = 0;
	TypeRef type;
	bool is_ref = false;
};

struct BytecodeCompileContext;
static bool bytecodeResolveValueAccess(ls_module& module, BytecodeCompileContext& ctx, i32 expr_idx, BytecodeValueAccess* out);
static bool bytecodeResolveValueAccessByExpr(ls_module& module, BytecodeCompileContext& ctx, Expr& expr, BytecodeValueAccess* out);
static bool bytecodeEmitLoadValueRange(ls_module& module, ls_bytecode& bytecode, const BytecodeValueAccess& access, i32 start_offset, i32 slot_count);
static bool bytecodeEmitLoadValue(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, const BytecodeValueAccess& access);
static bool bytecodeEmitAddressOfValue(ls_module& module, ls_bytecode& bytecode, const BytecodeCompileContext& ctx, const BytecodeValueAccess& access);
static bool bytecodeEmitStoreValue(ls_module& module, ls_bytecode& bytecode, i32 slot, const TypeRef& type);
static bool bytecodeEmitPopValue(ls_module& module, ls_bytecode& bytecode, const TypeRef& type);
static bool bytecodeGetCompileTimeInteger(ls_module& module, i32 expr_idx, i64* value);
static bool bytecodeEmitFunctionValue(ls_bytecode& bytecode, i32 index);

static BytecodeOp integerAddOp(ls_type_kind kind) {
	// Signedness matters for wraparound behavior, so each width gets its own op.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8: return BytecodeOp::ADD_I8;
		case LS_TYPE_U8: return BytecodeOp::ADD_U8;
		case LS_TYPE_I16: return BytecodeOp::ADD_I16;
		case LS_TYPE_U16: return BytecodeOp::ADD_U16;
		case LS_TYPE_I32: return BytecodeOp::ADD_I32;
		case LS_TYPE_U32: return BytecodeOp::ADD_U32;
		case LS_TYPE_I64: return BytecodeOp::ADD_I64;
		case LS_TYPE_U64: return BytecodeOp::ADD_U64;
		default: ASSERT(false); return BytecodeOp::ADD_I32;
	}
}

static BytecodeOp integerSubOp(ls_type_kind kind) {
	// Subtraction follows the same width/sign mapping as addition.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8: return BytecodeOp::SUB_I8;
		case LS_TYPE_U8: return BytecodeOp::SUB_U8;
		case LS_TYPE_I16: return BytecodeOp::SUB_I16;
		case LS_TYPE_U16: return BytecodeOp::SUB_U16;
		case LS_TYPE_I32: return BytecodeOp::SUB_I32;
		case LS_TYPE_U32: return BytecodeOp::SUB_U32;
		case LS_TYPE_I64: return BytecodeOp::SUB_I64;
		case LS_TYPE_U64: return BytecodeOp::SUB_U64;
		default: ASSERT(false); return BytecodeOp::SUB_I32;
	}
}

static BytecodeOp integerMulOp(ls_type_kind kind) {
	// Integer multiplication needs one opcode per storage width to preserve
	// overflow behavior exactly as the source type would see it.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8: return BytecodeOp::MUL_I8;
		case LS_TYPE_U8: return BytecodeOp::MUL_U8;
		case LS_TYPE_I16: return BytecodeOp::MUL_I16;
		case LS_TYPE_U16: return BytecodeOp::MUL_U16;
		case LS_TYPE_I32: return BytecodeOp::MUL_I32;
		case LS_TYPE_U32: return BytecodeOp::MUL_U32;
		case LS_TYPE_I64: return BytecodeOp::MUL_I64;
		case LS_TYPE_U64: return BytecodeOp::MUL_U64;
		default: ASSERT(false); return BytecodeOp::MUL_I32;
	}
}

static BytecodeOp integerDivOp(ls_type_kind kind) {
	// Division mirrors the integer width of the expression so signed and
	// unsigned values keep their expected quotient semantics.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8: return BytecodeOp::DIV_I8;
		case LS_TYPE_U8: return BytecodeOp::DIV_U8;
		case LS_TYPE_I16: return BytecodeOp::DIV_I16;
		case LS_TYPE_U16: return BytecodeOp::DIV_U16;
		case LS_TYPE_I32: return BytecodeOp::DIV_I32;
		case LS_TYPE_U32: return BytecodeOp::DIV_U32;
		case LS_TYPE_I64: return BytecodeOp::DIV_I64;
		case LS_TYPE_U64: return BytecodeOp::DIV_U64;
		default: ASSERT(false); return BytecodeOp::DIV_I32;
	}
}

static BytecodeOp integerModOp(ls_type_kind kind) {
	// Modulo is only emitted for integral expressions, but still needs width
	// specialization so the runtime can use the correct signedness.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8: return BytecodeOp::MOD_I8;
		case LS_TYPE_U8: return BytecodeOp::MOD_U8;
		case LS_TYPE_I16: return BytecodeOp::MOD_I16;
		case LS_TYPE_U16: return BytecodeOp::MOD_U16;
		case LS_TYPE_I32: return BytecodeOp::MOD_I32;
		case LS_TYPE_U32: return BytecodeOp::MOD_U32;
		case LS_TYPE_I64: return BytecodeOp::MOD_I64;
		case LS_TYPE_U64: return BytecodeOp::MOD_U64;
		default: ASSERT(false); return BytecodeOp::MOD_I32;
	}
}

static BytecodeOp floatMulOp(ls_type_kind kind) {
	// Floating-point ops use the same width split so f32 and f64 stay distinct.
	switch (kind == LS_TYPE_UNTYPED_FLOAT ? LS_TYPE_F32 : kind) {
		case LS_TYPE_F32: return BytecodeOp::MUL_F32;
		case LS_TYPE_F64: return BytecodeOp::MUL_F64;
		default: ASSERT(false); return BytecodeOp::MUL_F32;
	}
}

static BytecodeOp floatDivOp(ls_type_kind kind) {
	// Float division is the direct runtime `/` operator on the concrete type.
	switch (kind == LS_TYPE_UNTYPED_FLOAT ? LS_TYPE_F32 : kind) {
		case LS_TYPE_F32: return BytecodeOp::DIV_F32;
		case LS_TYPE_F64: return BytecodeOp::DIV_F64;
		default: ASSERT(false); return BytecodeOp::DIV_F32;
	}
}

static BytecodeOp bytecodeCompoundArithmeticOp(Token::Type op, ls_type_kind kind) {
	// Compound assignment shares the same operator mapping as the plain binary
	// expression form; the only difference is that the caller arranges the load
	// and store around the arithmetic opcode returned here.
	if (isBytecodeIntegralType(kind)) {
		switch (op) {
			case Token::PLUS_EQUAL: return integerAddOp(kind);
			case Token::MINUS_EQUAL: return integerSubOp(kind);
			case Token::STAR_EQUAL: return integerMulOp(kind);
			case Token::SLASH_EQUAL: return integerDivOp(kind);
			default: ASSERT(false); return integerAddOp(kind);
		}
	}
	if (isBytecodeFloatType(kind)) {
		switch (op) {
			case Token::PLUS_EQUAL: return floatAddOp(kind);
			case Token::MINUS_EQUAL: return floatSubOp(kind);
			case Token::STAR_EQUAL: return floatMulOp(kind);
			case Token::SLASH_EQUAL: return floatDivOp(kind);
			default: ASSERT(false); return floatAddOp(kind);
		}
	}
	ASSERT(false);
	return BytecodeOp::ADD_I32;
}

static BytecodeOp integerLoadConstOp(ls_type_kind kind) {
	// Constants are specialized by width so the VM knows how many bytes to read.
	switch (kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : kind) {
		case LS_TYPE_I8:
		case LS_TYPE_U8: return BytecodeOp::LOAD_CONST8;
		case LS_TYPE_I16:
		case LS_TYPE_U16: return BytecodeOp::LOAD_CONST16;
		case LS_TYPE_I32:
		case LS_TYPE_U32: return BytecodeOp::LOAD_CONST32;
		case LS_TYPE_I64:
		case LS_TYPE_U64: return BytecodeOp::LOAD_CONST64;
		default: ASSERT(false); return BytecodeOp::LOAD_CONST32;
	}
}

static bool isBytecodeBooleanType(ls_type_kind kind) {
	return kind == LS_TYPE_BOOL;
}

static bool isBytecodeComparisonType(ls_type_kind kind) {
	// Comparisons are allowed on booleans and integer-like values.
	return kind == LS_TYPE_UNTYPED_INT || kind == LS_TYPE_ENUM || isBytecodeIntegralType(kind) || isBytecodeFloatType(kind) || isBytecodeBooleanType(kind);
}

static ls_type_kind bytecodeComparisonKind(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_UNTYPED_INT:
		case LS_TYPE_ENUM:
			return LS_TYPE_I32;
		case LS_TYPE_UNTYPED_FLOAT:
			return LS_TYPE_F32;
		default:
			return kind;
	}
}

static void emitJumpPlaceholder(std::vector<u8>& code, BytecodeOp op, size_t* operand_pos) {
	// Forward branches are emitted with a dummy offset and patched later.
	pushCode(code, op);
	*operand_pos = code.size();
	i32 placeholder = 0;
	pushCode(code, placeholder);
}

static void emitJumpPlaceholder(ls_bytecode& bytecode, BytecodeOp op, size_t* operand_pos) {
	emitJumpPlaceholder(bytecode.code, op, operand_pos);
}

static void patchJump(std::vector<u8>& code, size_t operand_pos, size_t target_pos) {
	// Jump offsets are relative to the first byte after the operand itself.
	const i32 offset = (i32)(target_pos - (operand_pos + sizeof(i32)));
	memcpy(&code[operand_pos], &offset, sizeof(offset));
}

static void patchJump(ls_bytecode& bytecode, size_t operand_pos, size_t target_pos) {
	patchJump(bytecode.code, operand_pos, target_pos);
}

static ls_string_view bytecodeGetExpressionName(ls_module& module, i32 expr_idx) {
	// Call sites may point to expression trees, so we reconstruct the qualified
	// name lazily when needed.
	if (expr_idx < 0 || expr_idx >= module.expressions.size()) return {};
	Expr& expr = module.expressions[expr_idx];
	if (expr.kind == Expr::VAR) return expr.name;
	if (expr.kind == Expr::FIELD) {
		if (empty(expr.qualified_name)) expr.qualified_name = {bytecodeGetExpressionName(module, expr.left), expr.name};
		return module.makeQualifiedName(expr.qualified_name.path, expr.qualified_name.name);
	}
	if (expr.kind == Expr::FUNCTION_REF) {
		if (expr.boolean) {
			i32 idx = expr.left;
			for (const Unit* unit_ptr : module.units) {
				const Unit& unit = *unit_ptr;
				if (idx < (i32)unit.native_functions.size()) {
					return module.makeQualifiedName(unit.native_functions[(size_t)idx].canonical_name.path, unit.native_functions[(size_t)idx].canonical_name.name);
				}
				idx -= (i32)unit.native_functions.size();
			}
		}
		else {
			i32 idx = expr.left;
			for (const Unit* unit_ptr : module.units) {
				const Unit& unit = *unit_ptr;
				if (idx < (i32)unit.functions.size()) {
					const CanonicalName& name = unit.functions[(size_t)idx].canonical_name;
					return module.makeQualifiedName(name.path, name.name);
				}
				idx -= (i32)unit.functions.size();
			}
		}
	}
	return {};
}

struct BytecodeCompileContext;
static bool bytecodeCompileExpr(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 expr_idx, const TypeRef* expected = nullptr);
static bool bytecodeCompileMatchStmt(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, Stmt& stmt);
static bool bytecodeCompileStmt(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 stmt_idx);

static bool bytecodeCompileComparisonExpr(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, Expr& expr) {
	// The left operand decides the compare width. We emit both values first, then
	// append the comparison opcode and a one-byte type tag for the VM.
	const ls_type_kind left_kind = expr.left >= 0 ? module.expressions[expr.left].type.kind : LS_TYPE_INVALID;
	if ((expr.token.type == Token::EQUAL_EQUAL || expr.token.type == Token::BANG_EQUAL)
		&& ((expr.left >= 0 && module.expressions[expr.left].kind == Expr::NULL_LITERAL)
			|| (expr.right >= 0 && module.expressions[expr.right].kind == Expr::NULL_LITERAL))) {
		i32 value_expr_idx = expr.left >= 0 && module.expressions[expr.left].kind != Expr::NULL_LITERAL ? expr.left : expr.right;
		if (value_expr_idx < 0) return false;
		BytecodeValueAccess access = {};
		if (!bytecodeResolveValueAccess(module, ctx, value_expr_idx, &access)) return false;
		const i32 tag_slot = access.slot;
		if (tag_slot < 0) return false;
		if (access.kind == BytecodeValueKind::LOCAL) {
			pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
			pushCode(bytecode, (u8)tag_slot);
		}
		else if (access.kind == BytecodeValueKind::PARAM) {
			pushCode(bytecode, BytecodeOp::LOAD_PARAM);
			pushCode(bytecode, (u8)tag_slot);
		}
		else {
			pushCode(bytecode, BytecodeOp::LOAD_GLOBAL);
			pushCode(bytecode, (u32)tag_slot);
		}
		pushCode(bytecode, BytecodeOp::LOAD_CONST8);
		pushCode(bytecode, (u8)0);
		pushCode(bytecode, expr.token.type == Token::EQUAL_EQUAL ? BytecodeOp::CMP_EQ : BytecodeOp::CMP_NE);
		pushCode(bytecode, (u8)LS_TYPE_BOOL);
		return true;
	}
	if (!isBytecodeComparisonType(left_kind)) return false;
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
	const ls_type_kind compare_kind = bytecodeComparisonKind(left_kind);
	if (expr.token.type == Token::GT) pushCode(bytecode, BytecodeOp::CMP_GT);
	else if (expr.token.type == Token::LT) pushCode(bytecode, BytecodeOp::CMP_LT);
	else if (expr.token.type == Token::GT_EQUAL) pushCode(bytecode, BytecodeOp::CMP_GE);
	else if (expr.token.type == Token::LT_EQUAL) pushCode(bytecode, BytecodeOp::CMP_LE);
	else if (expr.token.type == Token::EQUAL_EQUAL) pushCode(bytecode, BytecodeOp::CMP_EQ);
	else if (expr.token.type == Token::BANG_EQUAL) pushCode(bytecode, BytecodeOp::CMP_NE);
	else return false;
	pushCode(bytecode, (u8)compare_kind);
	return true;
}

static void bytecodePushBool(ls_bytecode& bytecode, bool value) {
	// Booleans are always materialized as explicit 0/1 constants on the stack.
	pushCode(bytecode, BytecodeOp::LOAD_CONST8);
	pushCode(bytecode, (u8)(value ? 1 : 0));
}

/*
	Bytecode boolean lowering is deliberately control-flow based.

	The VM executes a flat stream of opcodes, so there is no concept of a
	"boolean expression result" unless we explicitly push one. For `and` / `or`
	we therefore:

	- compile the left operand first
	- branch around the right operand when the left already decides the result
	- emit a real `LOAD_CONST8 0/1` as the final value

	This is also why raw `0` / `1` bytes must never be emitted here: the runtime
	reads them as opcodes, not as data. The same stack discipline is used for
	global state, where the first `global_count` slots in the runtime stack are
	reserved permanently and a small hidden init program seeds them before the
	first user call.
*/
static bool bytecodeCompileLogicalAndExpr(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, Expr& expr) {
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
	size_t false_jump = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &false_jump);
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
	size_t false_jump_rhs = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &false_jump_rhs);
	bytecodePushBool(bytecode, true);
	size_t end_jump = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &end_jump);
	const size_t false_pos = bytecode.code.size();
	bytecodePushBool(bytecode, false);
	const size_t end_pos = bytecode.code.size();
	patchJump(bytecode, false_jump, false_pos);
	patchJump(bytecode, false_jump_rhs, false_pos);
	patchJump(bytecode, end_jump, end_pos);
	return true;
}

static bool bytecodeCompileLogicalOrExpr(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, Expr& expr) {
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
	size_t rhs_jump = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &rhs_jump);
	bytecodePushBool(bytecode, true);
	size_t end_jump = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &end_jump);
	const size_t rhs_pos = bytecode.code.size();
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
	size_t false_jump = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &false_jump);
	bytecodePushBool(bytecode, true);
	size_t end_jump_rhs = 0;
	emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &end_jump_rhs);
	const size_t false_pos = bytecode.code.size();
	bytecodePushBool(bytecode, false);
	const size_t end_pos = bytecode.code.size();
	patchJump(bytecode, rhs_jump, rhs_pos);
	patchJump(bytecode, false_jump, false_pos);
	patchJump(bytecode, end_jump, end_pos);
	patchJump(bytecode, end_jump_rhs, end_pos);
	return true;
}

struct BytecodeCompileContext {
	explicit BytecodeCompileContext(const FunctionDecl& fn_) : fn(fn_) {}

	struct LocalBinding {
		// A binding can be a user local or a function parameter.
		//
		// `slot` is the first flattened slot occupied by the binding.
		// `slot_count` is how many consecutive stack slots it occupies when the
		// binding is stored by value.
		// `is_ref` changes the interpretation of the slot: for ref parameters the
		// slot holds an address rather than a copied value.
		ls_string_view name;
		i32 slot = -1;
		i32 slot_count = 1;
		i32 value_slot_offset = 0;
		TypeRef type;
		bool is_param = false;
		bool is_ref = false;
	};

	struct LoopTarget {
		// Break and continue jump sites are patched after the loop body emits.
		ls_string_view name;
		size_t continue_target = 0;
		i32 cleanup_scope_depth = 0;
		std::vector<size_t> break_jumps;
		std::vector<size_t> continue_jumps;
	};

	// Current function being compiled. This drives local slot assignment.
	const FunctionDecl& fn;
	// Visible locals and parameters in declaration order.
	std::vector<LocalBinding> locals;
	// Deferred statements are tracked per lexical scope so exits can replay them
	// in reverse declaration order, matching the interpreter's block unwinding.
	std::vector<std::vector<i32>> deferred_scopes;
	// Saved binding counts for nested lexical scopes.
	std::vector<i32> scope_starts;
	// Active loops, innermost last.
	std::vector<LoopTarget> loop_targets;
	// Highest local slot touched by this function body.
	i32 local_count = 0;

	void pushScope() {
		// Enter a new lexical block.
		scope_starts.push_back((i32)locals.size());
		deferred_scopes.emplace_back();
	}

	void popScope() {
		// Drop bindings introduced inside the block.
		locals.resize(scope_starts.back());
		scope_starts.pop_back();
		deferred_scopes.pop_back();
	}

	i32 scopeDepth() const {
		return (i32)deferred_scopes.size();
	}

	void addLocal(ls_string_view name, i32 slot, i32 slot_count = 1, TypeRef type = {}, bool is_param = false, bool is_ref = false, i32 value_slot_offset = 0) {
		// Newer bindings shadow older ones, so we append and search backwards.
		locals.push_back(LocalBinding{name, slot, slot_count, value_slot_offset, type, is_param, is_ref});
	}

	void addDeferred(i32 stmt_idx) {
		if (!deferred_scopes.empty()) deferred_scopes.back().push_back(stmt_idx);
	}

	i32 findLocalIndex(ls_string_view name) const {
		// Search from innermost scope outward.
		for (i32 i = (i32)locals.size() - 1; i >= 0; --i) {
			if (!locals[i].is_param && equalStrings(locals[i].name, name)) return locals[i].slot;
		}
		return -1;
	}

	i32 findLocal(ls_string_view name) const {
		return findLocalIndex(name);
	}

	i32 findParamIndex(ls_string_view name) const {
		// Parameters share the same namespace as locals, but are tracked
		// separately so assignments only ever target actual locals.
		for (i32 i = (i32)locals.size() - 1; i >= 0; --i) {
			if (locals[i].is_param && equalStrings(locals[i].name, name)) return locals[i].slot;
		}
		return -1;
	}

	const LocalBinding* findLocalBinding(ls_string_view name) const {
		for (i32 i = (i32)locals.size() - 1; i >= 0; --i) {
			if (!locals[i].is_param && equalStrings(locals[i].name, name)) return &locals[i];
		}
		return nullptr;
	}

	const LocalBinding* findParamBinding(ls_string_view name) const {
		for (i32 i = (i32)locals.size() - 1; i >= 0; --i) {
			if (locals[i].is_param && equalStrings(locals[i].name, name)) return &locals[i];
		}
		return nullptr;
	}

	i32 findLoopIndex(ls_string_view name) const {
		// An empty name means "the nearest active loop".
		if (empty(name)) return loop_targets.empty() ? -1 : (i32)loop_targets.size() - 1;
		for (i32 i = (i32)loop_targets.size() - 1; i >= 0; --i) {
			if (equalStrings(loop_targets[i].name, name)) return i;
		}
		return -1;
	}

};

static bool bytecodeResolveValueAccessByExpr(ls_module& module, BytecodeCompileContext& ctx, Expr& expr, BytecodeValueAccess* out) {
	// Resolve an expression to a stack-accessible value view.
	//
	// The bytecode backend does not materialize a separate aggregate value
	// representation. Instead, it treats structs as flattened slot ranges, so
	// this helper maps `var` and `field` expressions to:
	// - where the value lives (`LOCAL`, `PARAM`, or `GLOBAL`)
	// - which flattened slot offset to start from
	// - the resolved type of the value at that location
	if (expr.kind == Expr::VAR) {
		// Variables resolve directly to their storage class.
		// Ref parameters are still reported as the same binding, but with
		// `is_ref=true` so callers can choose between copying the pointed-to value
		// or passing the address through unchanged.
		if (const BytecodeCompileContext::LocalBinding* binding = ctx.findLocalBinding(expr.name)) {
			out->kind = BytecodeValueKind::LOCAL;
			out->slot = binding->slot;
			out->value_slot_offset = binding->value_slot_offset;
			out->type = binding->type;
			out->is_ref = binding->is_ref;
			return true;
		}
		if (const BytecodeCompileContext::LocalBinding* binding = ctx.findParamBinding(expr.name)) {
			out->kind = BytecodeValueKind::PARAM;
			out->slot = binding->slot;
			out->value_slot_offset = binding->value_slot_offset;
			out->type = binding->type;
			out->is_ref = binding->is_ref;
			return true;
		}
		const i32 global_idx = module.findGlobal(expr.name);
		if (global_idx >= 0) {
			out->kind = BytecodeValueKind::GLOBAL;
			i32 idx = global_idx;
			GlobalDecl* global = nullptr;
			for (Unit* unit_ptr : module.units) {
				Unit& unit = *unit_ptr;
				if (idx < (i32)unit.globals.size()) {
					global = &unit.globals[(size_t)idx];
					break;
				}
				idx -= (i32)unit.globals.size();
			}
			if (!global) return false;
			out->slot = global->slot;
			out->value_slot_offset = 0;
			out->type = global->type;
			out->is_ref = false;
			return true;
		}
		return false;
	}
	if (expr.kind == Expr::FIELD) {
		// Field access just advances the flattened slot offset. For a normal value
		// this changes the physical slot number immediately. For a ref-backed
		// value we keep the base address and only record the offset, because the
		// actual load/store will happen indirectly later.
		BytecodeValueAccess base = {};
		if (!bytecodeResolveValueAccess(module, ctx, expr.left, &base)) return false;
		if (base.type.kind != LS_TYPE_STRUCT) return false;
		i32 field_offset = 0;
		TypeRef field_type;
		if (!bytecodeStructFieldOffset(module, base.type, expr.name, &field_offset, &field_type)) return false;
		if (base.is_ref) base.value_slot_offset += field_offset;
		else {
			base.slot += base.value_slot_offset + field_offset;
			base.value_slot_offset = 0;
		}
		base.type = field_type;
		*out = base;
		return true;
	}
	if (expr.kind == Expr::INDEX) {
		BytecodeValueAccess base = {};
		if (!bytecodeResolveValueAccess(module, ctx, expr.left, &base)) return false;
		if (base.type.kind != LS_TYPE_ARRAY) return false;
		i64 index_value = 0;
		if (!bytecodeGetCompileTimeInteger(module, expr.right, &index_value)) return false;
		if (index_value < 0 || index_value >= base.type.array_size) return false;
		TypeRef elem(base.type.element_kind, base.type.element_name, base.type.struct_index, base.type.token, false);
		const i32 elem_slots = bytecodeTypeSlotCount(module, elem);
		if (elem_slots <= 0) return false;
		const i32 array_offset = (i32)index_value * elem_slots;
		if (base.is_ref) base.value_slot_offset += array_offset;
		else {
			base.slot += base.value_slot_offset + array_offset;
			base.value_slot_offset = 0;
		}
		base.type = elem;
		*out = base;
		return true;
	}
	return false;
}

static bool bytecodeGetCompileTimeInteger(ls_module& module, i32 expr_idx, i64* value) {
	if (expr_idx < 0 || expr_idx >= module.expressions.size()) return false;
	Expr& e = module.expressions[expr_idx];
	switch (e.kind) {
		case Expr::NUMBER:
			*value = (i64)e.number;
			return true;
		case Expr::UNARY:
			if (e.token.type != Token::MINUS && e.token.type != Token::PLUS) return false;
			if (!bytecodeGetCompileTimeInteger(module, e.right, value)) return false;
			if (e.token.type == Token::MINUS) *value = -*value;
			return true;
		case Expr::CAST:
			return bytecodeGetCompileTimeInteger(module, e.left, value);
		default:
			return false;
	}
}

static bool bytecodeResolveValueAccess(ls_module& module, BytecodeCompileContext& ctx, i32 expr_idx, BytecodeValueAccess* out) {
	if (expr_idx < 0 || expr_idx >= module.expressions.size()) return false;
	return bytecodeResolveValueAccessByExpr(module, ctx, module.expressions[expr_idx], out);
}

static bool bytecodeEmitRuntimeIndexedAccess(
	ls_module& module,
	ls_bytecode& bytecode,
	BytecodeCompileContext& ctx,
	Expr& expr,
	BytecodeValueAccess* base_out,
	TypeRef* element_type_out,
	i32* element_slot_count_out,
	i32* temp_slot_out
) {
	// `values[idx()]` needs special handling because the index is not known at
	// compile time. We still want to evaluate the array base once, evaluate the
	// index once, and then keep the computed element address around for the
	// eventual load/store that follows.
	BytecodeValueAccess base = {};
	if (!bytecodeResolveValueAccess(module, ctx, expr.left, &base)) return false;
	if (base.type.kind != LS_TYPE_ARRAY) return false;
	TypeRef element_type(base.type.element_kind, base.type.element_name, base.type.struct_index, base.type.token, false);
	const i32 element_slot_count = bytecodeTypeSlotCount(module, element_type);
	if (element_slot_count <= 0) return false;
	// Reserve one hidden local slot for the computed address so compound
	// assignments can reuse it after the index expression has been evaluated.
	const i32 temp_slot = ctx.local_count;
	if (temp_slot < 0 || temp_slot + 1 > 256) return false;
	ctx.local_count += 1;
	if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, base)) return false;
	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
	if (element_slot_count != 1) {
		pushCode(bytecode, BytecodeOp::LOAD_CONST32);
		pushCode(bytecode, (u32)element_slot_count);
		pushCode(bytecode, BytecodeOp::MUL_U32);
	}
	pushCode(bytecode, BytecodeOp::ADD_U32);
	pushCode(bytecode, BytecodeOp::STORE_LOCAL);
	pushCode(bytecode, (u8)temp_slot);
	if (base_out) *base_out = base;
	if (element_type_out) *element_type_out = element_type;
	if (element_slot_count_out) *element_slot_count_out = element_slot_count;
	if (temp_slot_out) *temp_slot_out = temp_slot;
	return true;
}

static bool bytecodeEmitRuntimeFieldAccess(
	ls_module& module,
	ls_bytecode& bytecode,
	BytecodeCompileContext& ctx,
	Expr& expr
) {
	if (expr.left < 0) return false;
	TypeRef base_type = module.expressions[expr.left].type;
	if (base_type.nullable || base_type.kind != LS_TYPE_STRUCT || base_type.struct_index < 0) return false;

	i32 field_offset = 0;
	TypeRef field_type;
	if (!bytecodeStructFieldOffset(module, base_type, expr.name, &field_offset, &field_type)) return false;

	const i32 base_slot_count = bytecodeTypeSlotCount(module, base_type);
	if (base_slot_count <= 0) return false;

	// Temporaries produced by calls are not addressable, so we spill the base
	// value into a scratch local before loading the requested field.
	const i32 temp_slot = ctx.local_count;
	if (temp_slot < 0 || temp_slot + base_slot_count > 256) return false;
	ctx.local_count += base_slot_count;

	if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left, &base_type)) return false;
	if (!bytecodeEmitStoreValue(module, bytecode, temp_slot, base_type)) return false;

	BytecodeValueAccess access = {};
	access.kind = BytecodeValueKind::LOCAL;
	access.slot = temp_slot;
	access.value_slot_offset = field_offset;
	access.type = field_type;
	return bytecodeEmitLoadValue(module, bytecode, ctx, access);
}

static bool bytecodeEmitLoadValue(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, const BytecodeValueAccess& access) {
	const i32 slot_count = bytecodeTypeSlotCount(module, access.type);
	if (slot_count <= 0) return false;
	// Non-ref values are loaded directly from their flattened storage. Ref
	// values first materialize the address, then ask the VM to read through it.
	if (!access.is_ref) return bytecodeEmitLoadValueRange(module, bytecode, access, access.value_slot_offset, slot_count);
	if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
	pushCode(bytecode, BytecodeOp::LOAD_INDIRECT);
	pushCode(bytecode, (u8)slot_count);
	return true;
}

static bool bytecodeEmitAddressOfValue(ls_module& module, ls_bytecode& bytecode, const BytecodeCompileContext& ctx, const BytecodeValueAccess& access) {
	const i32 physical_slot = access.slot + access.value_slot_offset;
	if (!access.is_ref) {
		// For non-ref values we do not have a runtime address object, so the
		// compiler synthesizes the absolute slot index as an immediate constant.
		// This is enough because the VM stack already stores all values in a flat
		// array of slots. The frame layout is:
		// - globals first
		// - then function parameters
		// - then locals
		// so locals and params need an offset before the raw slot index becomes a
		// VM-stack address.
		i32 param_slot_count = 0;
		for (const Param& p : ctx.fn.params) param_slot_count += p.is_ref ? 1 : bytecodeTypeSlotCount(module, p.type);
		const i32 frame_slot = access.kind == BytecodeValueKind::LOCAL ? (i32)bytecode.global_count + param_slot_count + physical_slot
			: access.kind == BytecodeValueKind::PARAM ? (i32)bytecode.global_count + physical_slot
			: physical_slot;
		if (frame_slot < 0) return false;
		pushCode(bytecode, BytecodeOp::LOAD_CONST32);
		pushCode(bytecode, (u32)frame_slot);
		return true;
	}
	// Ref values already carry an address in their slot, so we only need to load
	// that address and optionally add a flattened field offset.
	switch (access.kind) {
		case BytecodeValueKind::LOCAL:
			pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
			pushCode(bytecode, (u8)access.slot);
			break;
		case BytecodeValueKind::PARAM:
			pushCode(bytecode, BytecodeOp::LOAD_PARAM);
			pushCode(bytecode, (u8)access.slot);
			break;
		case BytecodeValueKind::GLOBAL:
			pushCode(bytecode, BytecodeOp::LOAD_GLOBAL);
			pushCode(bytecode, (u32)access.slot);
			break;
	}
	if (access.value_slot_offset != 0) {
		pushCode(bytecode, BytecodeOp::LOAD_CONST32);
		pushCode(bytecode, (u32)access.value_slot_offset);
		pushCode(bytecode, BytecodeOp::ADD_U32);
	}
	return true;
}

static bool bytecodeEmitLoadValueRange(ls_module& module, ls_bytecode& bytecode, const BytecodeValueAccess& access, i32 start_offset, i32 slot_count) {
	if (slot_count <= 0) return false;
	const i32 physical_slot = access.slot + start_offset;
	switch (access.kind) {
		case BytecodeValueKind::LOCAL:
		case BytecodeValueKind::PARAM:
			for (i32 i = 0; i < slot_count; ++i) {
				pushCode(bytecode, access.kind == BytecodeValueKind::LOCAL ? BytecodeOp::LOAD_LOCAL : BytecodeOp::LOAD_PARAM);
				pushCode(bytecode, (u8)(physical_slot + i));
			}
			return true;
		case BytecodeValueKind::GLOBAL:
			// Globals live in the reserved module prefix, so a struct load expands
			// to one opcode per flattened slot just like locals do.
			if (physical_slot < 0) return false;
			for (i32 i = 0; i < slot_count; ++i) {
				pushCode(bytecode, BytecodeOp::LOAD_GLOBAL);
				pushCode(bytecode, (u32)(physical_slot + i));
			}
			return true;
	}
	return false;
}

// Stores and pops mirror load behavior: one opcode per flattened slot keeps
// the existing runtime stack model intact while structs flow through it.
static bool bytecodeEmitStoreValue(ls_module& module, ls_bytecode& bytecode, i32 slot, const TypeRef& type) {
	const i32 slot_count = bytecodeTypeSlotCount(module, type);
	if (slot_count <= 0) return false;
	// Stores walk backward so the last pushed value slot ends up at the highest
	// destination slot. That preserves the natural left-to-right evaluation order
	// of struct literals and assignments.
	for (i32 i = slot_count - 1; i >= 0; --i) {
		if (slot + i >= 256) return false;
		pushCode(bytecode, BytecodeOp::STORE_LOCAL);
		pushCode(bytecode, (u8)(slot + i));
	}
	return true;
}

static bool bytecodeEmitStoreGlobalValue(ls_module& module, ls_bytecode& bytecode, i32 slot, const TypeRef& type) {
	const i32 slot_count = bytecodeTypeSlotCount(module, type);
	if (slot_count <= 0) return false;
	if (slot < 0) return false;
	// `STORE_GLOBAL` consumes one flattened slot at a time, so multi-slot globals
	// are written back-to-front to preserve the same slot ordering used by local
	// struct stores and by the runtime's stack layout.
	for (i32 i = slot_count - 1; i >= 0; --i) {
		pushCode(bytecode, BytecodeOp::STORE_GLOBAL);
		pushCode(bytecode, (u32)(slot + i));
	}
	return true;
}

static bool bytecodeEmitNullValue(ls_module& module, ls_bytecode& bytecode, const TypeRef& type) {
	if (!type.nullable) return false;
	// Nullable values are encoded as a leading presence tag followed by the
	// underlying payload slots. A null literal therefore emits tag=0 and zeros
	// out the payload for determinism.
	pushCode(bytecode, BytecodeOp::LOAD_CONST8);
	pushCode(bytecode, (u8)0);
	const i32 payload_slots = bytecodeTypeSlotCount(module, bytecodeNonNullableType(type));
	if (payload_slots < 0) return false;
	for (i32 i = 0; i < payload_slots; ++i) {
		pushCode(bytecode, BytecodeOp::LOAD_CONST64);
		const u64 zero = 0;
		pushCode(bytecode, zero);
	}
	return true;
}

static bool bytecodeCompileNullablePromotion(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 expr_idx, ls_string_view* var_name, TypeRef* promoted_type, bool* promote_true_branch) {
	if (expr_idx < 0) return false;
	Expr& cond = module.expressions[expr_idx];
	if (cond.kind != Expr::BINARY) return false;
	if (cond.token.type != Token::BANG_EQUAL && cond.token.type != Token::EQUAL_EQUAL) return false;
	i32 var_expr_idx = -1;
	Expr& left = module.expressions[cond.left];
	Expr& right = module.expressions[cond.right];
	if (left.kind == Expr::VAR && right.kind == Expr::NULL_LITERAL) var_expr_idx = cond.left;
	else if (right.kind == Expr::VAR && left.kind == Expr::NULL_LITERAL) var_expr_idx = cond.right;
	else return false;
	Expr& var_expr = module.expressions[var_expr_idx];
	const BytecodeCompileContext::LocalBinding* binding = ctx.findLocalBinding(var_expr.name);
	if (!binding) return false;
	if (!binding->type.nullable) return false;
	*var_name = var_expr.name;
	*promoted_type = binding->type;
	promoted_type->nullable = false;
	*promote_true_branch = cond.token.type == Token::BANG_EQUAL;
	return true;
}

static bool bytecodeEmitWrapNullable(ls_module& module, ls_bytecode& bytecode, const TypeRef& nullable_type, const TypeRef& value_type) {
	if (!nullable_type.nullable) return false;
	TypeRef nonnull = nullable_type;
	nonnull.nullable = false;
	if (!sameBaseType(nonnull, value_type)) return false;
	// When the compiler widens a plain value into a nullable one, the payload is
	// already on the stack and only the presence tag needs to be synthesized.
	pushCode(bytecode, BytecodeOp::LOAD_CONST8);
	pushCode(bytecode, (u8)1);
	return true;
}

static bool bytecodeEmitFunctionValue(ls_bytecode& bytecode, i32 index) {
	pushCode(bytecode, BytecodeOp::LOAD_CONST64);
	pushCode(bytecode, bytecodeEncodeFunctionHandle(index));
	return true;
}

static bool bytecodeEmitStringLiteral(ls_bytecode& bytecode, ls_string_view value) {
	const size_t len = size(value);
	ls_string* storage = (ls_string*)allocateMemory(&bytecode.host, sizeof(ls_string) + len, alignof(ls_string));
	if (!storage) return false;
	storage->host = bytecode.host;
	storage->ref_count = 1;
	storage->length = (u32)len;
	if (len > 0) memcpy(storage->chars, data(value), len);
	storage->chars[len] = '\0';
	bytecode.string_literals.push_back(storage);
	pushCode(bytecode, BytecodeOp::LOAD_STRING);
	pushCode(bytecode, (u32)(bytecode.string_literals.size() - 1));
	return true;
}

static bool bytecodeEmitZeroValue(ls_bytecode& bytecode, ls_type_kind kind) {
	// Unary minus is lowered as `0 - value`, so we need a typed zero literal
	// that matches the expression width before the existing arithmetic opcodes
	// can do the rest.
	switch (kind) {
		case LS_TYPE_BOOL:
		case LS_TYPE_I8:
		case LS_TYPE_U8:
		case LS_TYPE_ENUM:
			pushCode(bytecode, BytecodeOp::LOAD_CONST8);
			pushCode(bytecode, (u8)0);
			return true;
		case LS_TYPE_I16:
		case LS_TYPE_U16:
			pushCode(bytecode, BytecodeOp::LOAD_CONST16);
			pushCode(bytecode, (u16)0);
			return true;
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_UNTYPED_INT:
			pushCode(bytecode, BytecodeOp::LOAD_CONST32);
			pushCode(bytecode, (u32)0);
			return true;
		case LS_TYPE_I64:
		case LS_TYPE_U64:
			pushCode(bytecode, BytecodeOp::LOAD_CONST64);
			pushCode(bytecode, (u64)0);
			return true;
		case LS_TYPE_F32:
		case LS_TYPE_UNTYPED_FLOAT: {
			float zero = 0.0f;
			pushCode(bytecode, BytecodeOp::LOAD_CONST32);
			pushCode(bytecode, zero);
			return true;
		}
		case LS_TYPE_F64: {
			double zero = 0.0;
			pushCode(bytecode, BytecodeOp::LOAD_CONST64);
			pushCode(bytecode, zero);
			return true;
		}
		default:
			return false;
	}
}

static bool bytecodeEmitOneValue(ls_bytecode& bytecode, ls_type_kind kind) {
	// Loop increments need a typed literal that matches the counter width.
	switch (kind) {
		case LS_TYPE_BOOL:
		case LS_TYPE_I8:
		case LS_TYPE_U8:
		case LS_TYPE_ENUM:
			pushCode(bytecode, BytecodeOp::LOAD_CONST8);
			pushCode(bytecode, (u8)1);
			return true;
		case LS_TYPE_I16:
		case LS_TYPE_U16:
			pushCode(bytecode, BytecodeOp::LOAD_CONST16);
			pushCode(bytecode, (u16)1);
			return true;
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_UNTYPED_INT:
			pushCode(bytecode, BytecodeOp::LOAD_CONST32);
			pushCode(bytecode, (u32)1);
			return true;
		case LS_TYPE_I64:
		case LS_TYPE_U64:
			pushCode(bytecode, BytecodeOp::LOAD_CONST64);
			pushCode(bytecode, (u64)1);
			return true;
		case LS_TYPE_F32:
		case LS_TYPE_UNTYPED_FLOAT: {
			float one = 1.0f;
			pushCode(bytecode, BytecodeOp::LOAD_CONST32);
			pushCode(bytecode, one);
			return true;
		}
		case LS_TYPE_F64: {
			double one = 1.0;
			pushCode(bytecode, BytecodeOp::LOAD_CONST64);
			pushCode(bytecode, one);
			return true;
		}
		default:
			return false;
	}
}

static bool bytecodeEmitCastValue(ls_bytecode& bytecode, ls_type_kind src_kind, ls_type_kind dst_kind) {
	pushCode(bytecode, BytecodeOp::CAST);
	pushCode(bytecode, (u8)bytecodeCastKind(src_kind));
	pushCode(bytecode, (u8)bytecodeCastKind(dst_kind));
	return true;
}

static bool bytecodeEmitPopValue(ls_module& module, ls_bytecode& bytecode, const TypeRef& type) {
	const i32 slot_count = bytecodeTypeSlotCount(module, type);
	if (slot_count <= 0) return true;
	for (i32 i = 0; i < slot_count; ++i) pushCode(bytecode, BytecodeOp::POP);
	return true;
}

static bool bytecodeEmitDeferredScope(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 scope_depth) {
	if (scope_depth < 0 || scope_depth >= ctx.deferred_scopes.size()) return false;
	const std::vector<i32> deferreds = ctx.deferred_scopes[scope_depth];
	// Defers run last-in, first-out within a scope.
	for (i32 i = (i32)deferreds.size() - 1; i >= 0; --i) {
		if (!bytecodeCompileStmt(module, bytecode, ctx, deferreds[i])) return false;
	}
	return true;
}

static bool bytecodeEmitDeferredScopes(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 scope_depth) {
	if (scope_depth < 0) return false;
	const i32 current_depth = ctx.scopeDepth();
	if (scope_depth >= current_depth) return true;
	for (i32 i = current_depth - 1; i >= scope_depth; --i) {
		if (!bytecodeEmitDeferredScope(module, bytecode, ctx, i)) return false;
	}
	return true;
}

struct BytecodeMatchArmInfo {
	std::vector<size_t> body_jumps;
};

static bool bytecodeCompileMatchStmt(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, Stmt& stmt) {
	if (stmt.expr < 0) return false;
	const TypeRef subject_type = module.expressions[stmt.expr].type;
	if (!isBytecodeComparisonType(subject_type.kind)) return false;
	const i32 subject_slot_count = bytecodeTypeSlotCount(module, subject_type);
	if (subject_slot_count != 1) return false;

	// Match lowering keeps the subject in a temporary local slot so each arm can
	// re-read it without changing the flat stack ABI used by the rest of the VM.
	const i32 subject_slot = ctx.local_count;
	if (subject_slot < 0 || subject_slot + subject_slot_count > 256) return false;
	ctx.local_count += subject_slot_count;

	if (!bytecodeCompileExpr(module, bytecode, ctx, stmt.expr, &stmt.type)) return false;
	if (!bytecodeEmitStoreValue(module, bytecode, subject_slot, subject_type)) return false;

	std::vector<BytecodeMatchArmInfo> arm_infos;
	arm_infos.reserve(stmt.children.size());
	std::vector<size_t> pending_false_jumps;
	std::vector<size_t> end_jumps;

	for (i32 arm_idx : stmt.children) {
		if (arm_idx < 0 || arm_idx >= module.match_arms.size()) return false;
		MatchArm& arm = module.match_arms[arm_idx];
		BytecodeMatchArmInfo info;
		bool has_default = false;

		for (size_t jump_pos : pending_false_jumps) patchJump(bytecode, jump_pos, bytecode.code.size());
		pending_false_jumps.clear();

		for (i32 pattern_idx : arm.patterns) {
			if (pattern_idx < 0 || pattern_idx >= module.match_patterns.size()) return false;
			MatchPattern& pattern = module.match_patterns[pattern_idx];
			if (pattern.kind == MatchPattern::DEFAULT) {
				has_default = true;
				for (size_t jump_pos : pending_false_jumps) patchJump(bytecode, jump_pos, bytecode.code.size());
				pending_false_jumps.clear();
				break;
			}
			if (pattern.kind != MatchPattern::VALUE && pattern.kind != MatchPattern::RANGE) return false;
			for (size_t jump_pos : pending_false_jumps) patchJump(bytecode, jump_pos, bytecode.code.size());
			pending_false_jumps.clear();

			pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
			pushCode(bytecode, (u8)subject_slot);
			if (!bytecodeCompileExpr(module, bytecode, ctx, pattern.start_expr)) return false;
			const ls_type_kind compare_kind = bytecodeComparisonKind(subject_type.kind);
			pushCode(bytecode, pattern.kind == MatchPattern::RANGE ? BytecodeOp::CMP_GE : BytecodeOp::CMP_EQ);
			pushCode(bytecode, (u8)compare_kind);
			size_t false_jump = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &false_jump);
			pending_false_jumps.push_back(false_jump);

			if (pattern.kind == MatchPattern::RANGE) {
				pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
				pushCode(bytecode, (u8)subject_slot);
				if (!bytecodeCompileExpr(module, bytecode, ctx, pattern.end_expr)) return false;
				pushCode(bytecode, BytecodeOp::CMP_LE);
				pushCode(bytecode, (u8)compare_kind);
				false_jump = 0;
				emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &false_jump);
				pending_false_jumps.push_back(false_jump);
			}

			size_t body_jump = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &body_jump);
			info.body_jumps.push_back(body_jump);
		}
		if (has_default) {
			size_t body_jump = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &body_jump);
			info.body_jumps.push_back(body_jump);
		}
		arm_infos.push_back(std::move(info));
	}

	for (size_t arm_i = 0; arm_i < arm_infos.size(); ++arm_i) {
		for (size_t jump_pos : arm_infos[arm_i].body_jumps) patchJump(bytecode, jump_pos, bytecode.code.size());
		if (stmt.children[arm_i] < 0 || stmt.children[arm_i] >= module.match_arms.size()) return false;
		MatchArm& arm = module.match_arms[stmt.children[arm_i]];
		if (arm.stmt >= 0 && !bytecodeCompileStmt(module, bytecode, ctx, arm.stmt)) return false;
		size_t end_jump = 0;
		emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &end_jump);
		end_jumps.push_back(end_jump);
	}

	const size_t end_pos = bytecode.code.size();
	for (size_t jump_pos : pending_false_jumps) patchJump(bytecode, jump_pos, end_pos);
	for (size_t jump_pos : end_jumps) patchJump(bytecode, jump_pos, end_pos);
	return true;
}

static bool bytecodeCompileExpr(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 expr_idx, const TypeRef* expected) {
	if (expr_idx < 0 || expr_idx >= module.expressions.size()) return false;
	Expr& expr = module.expressions[expr_idx];
	TypeRef wrapped_expected = {};
	if (expected && expected->nullable && !expr.type.nullable) {
		wrapped_expected = *expected;
		wrapped_expected.nullable = false;
		if (sameBaseType(wrapped_expected, expr.type)) {
			if (!bytecodeEmitWrapNullable(module, bytecode, *expected, expr.type)) return false;
			expected = &wrapped_expected;
		}
	}
	switch (expr.kind) {
		case Expr::NUMBER: {
			// Numeric literals are lowered as typed loads with the narrowest
			// runtime operand that preserves the compile-time type.
			const ls_type_kind kind = expr.type.kind == LS_TYPE_UNTYPED_INT ? LS_TYPE_I32 : expr.type.kind;
			if (isBytecodeIntegralType(kind)) {
				pushCode(bytecode, integerLoadConstOp(kind));
				const u64 raw = encodeIntegerLiteral(expr.number, kind);
				const size_t s = bytecode.code.size();
				const size_t size = integerByteSize(kind);
				bytecode.code.resize(s + size);
				memcpy(&bytecode.code[s], &raw, size);
				return true;
			}
			if (isBytecodeFloatType(kind)) {
				pushCode(bytecode, floatLoadConstOp(kind));
				const size_t s = bytecode.code.size();
				if ((kind == LS_TYPE_UNTYPED_FLOAT ? LS_TYPE_F32 : kind) == LS_TYPE_F32) {
					const float value = (float)expr.number;
					bytecode.code.resize(s + sizeof(value));
					memcpy(&bytecode.code[s], &value, sizeof(value));
				}
				else {
					const double value = expr.number;
					bytecode.code.resize(s + sizeof(value));
					memcpy(&bytecode.code[s], &value, sizeof(value));
				}
				return true;
			}
			return false;
		}
		case Expr::BOOL_LITERAL: {
			// Booleans are emitted as normal constants so the stack always carries
			// concrete values instead of source-level truthiness markers.
			pushCode(bytecode, BytecodeOp::LOAD_CONST8);
			const u8 value = expr.boolean ? 1 : 0;
			pushCode(bytecode, value);
			return true;
		}
		case Expr::STRING_LITERAL:
			return bytecodeEmitStringLiteral(bytecode, expr.string);
		case Expr::NULL_LITERAL:
			return expected && expected->nullable ? bytecodeEmitNullValue(module, bytecode, *expected) : false;
		case Expr::ENUM_LITERAL: {
			if (expr.type.kind != LS_TYPE_ENUM) return false;
			i32 enum_idx = expr.type.struct_index;
			if (enum_idx < 0) {
				enum_idx = -1;
				i32 running_enum_idx = 0;
				for (const Unit* unit_ptr : module.units) {
					const Unit& unit = *unit_ptr;
					for (const EnumDecl& e : unit.enums) {
						if (module.findEnumMember(e, expr.name) >= 0) {
							if (enum_idx >= 0) return false;
							enum_idx = running_enum_idx;
						}
						++running_enum_idx;
					}
				}
				if (enum_idx < 0) return false;
			}
			i32 idx = enum_idx;
			const EnumDecl* e = nullptr;
			for (const Unit* unit_ptr : module.units) {
				const Unit& unit = *unit_ptr;
				if (idx < (i32)unit.enums.size()) {
					e = &unit.enums[(size_t)idx];
					break;
				}
				idx -= (i32)unit.enums.size();
			}
			if (!e) return false;
			const i32 member_idx = module.findEnumMember(*e, expr.name);
			if (member_idx < 0) return false;
			pushCode(bytecode, BytecodeOp::LOAD_CONST32);
			const i32 value = e->members[member_idx].value;
			pushCode(bytecode, (u32)value);
			return true;
		}
		case Expr::VAR: {
			BytecodeValueAccess access = {};
			if (!bytecodeResolveValueAccess(module, ctx, expr_idx, &access)) return false;
			return bytecodeEmitLoadValue(module, bytecode, ctx, access);
		}
		case Expr::FUNCTION_REF:
			if (expr.boolean) return false;
			return bytecodeEmitFunctionValue(bytecode, expr.left);
		case Expr::REF: {
			BytecodeValueAccess access = {};
			if (!bytecodeResolveValueAccess(module, ctx, expr.right, &access)) return false;
			if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
			return true;
		}
		case Expr::CAST: {
			if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
			if (!bytecodeEmitCastValue(bytecode, module.expressions[expr.left].type.kind, expr.cast_type.kind)) return false;
			return true;
		}
		case Expr::UNARY: {
			if (expr.resolved_function >= 0) {
				// Overloaded unary operators are ordinary function calls once type
				// checking has resolved the exact target function.
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				pushCode(bytecode, BytecodeOp::CALL);
				pushCode(bytecode, (u32)expr.resolved_function);
				return true;
			}
			// Unary plus is a no-op. Unary minus becomes `0 - value` so the backend
			// can reuse the same typed arithmetic opcodes as normal subtraction.
			if (expr.token.type == Token::PLUS) return bytecodeCompileExpr(module, bytecode, ctx, expr.right);
			if (expr.token.type != Token::MINUS && expr.token.type != Token::NOT) return false;
			if (expr.token.type == Token::MINUS) {
				if (!bytecodeEmitZeroValue(bytecode, expr.type.kind)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				if (isBytecodeIntegralType(expr.type.kind)) pushCode(bytecode, integerSubOp(expr.type.kind));
				else if (isBytecodeFloatType(expr.type.kind)) pushCode(bytecode, floatSubOp(expr.type.kind));
				else return false;
				return true;
			}
			if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
			pushCode(bytecode, BytecodeOp::NOT_BOOL);
			return true;
		}
		case Expr::BINARY: {
			if (expr.resolved_function >= 0) {
				// Binary operator overloads follow the same lowering pattern: evaluate
				// both operands, then call the resolved declaration directly.
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				pushCode(bytecode, BytecodeOp::CALL);
				pushCode(bytecode, (u32)expr.resolved_function);
				return true;
			}
			// Binary expressions are split by operator family to preserve operand
			// order and the correct runtime opcode shape. Arithmetic lowering is
			// intentionally direct: evaluate both sides, then emit the width-specific
			// opcode that matches the result type.
			if (expr.token.type == Token::PLUS) {
				if (!isBytecodeIntegralType(expr.type.kind) && !isBytecodeFloatType(expr.type.kind)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				if (isBytecodeIntegralType(expr.type.kind)) pushCode(bytecode, integerAddOp(expr.type.kind));
				else pushCode(bytecode, floatAddOp(expr.type.kind));
				return true;
			}
			if (expr.token.type == Token::MINUS) {
				if (!isBytecodeIntegralType(expr.type.kind) && !isBytecodeFloatType(expr.type.kind)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				if (isBytecodeIntegralType(expr.type.kind)) pushCode(bytecode, integerSubOp(expr.type.kind));
				else pushCode(bytecode, floatSubOp(expr.type.kind));
				return true;
			}
			if (expr.token.type == Token::STAR) {
				if (!isBytecodeIntegralType(expr.type.kind) && !isBytecodeFloatType(expr.type.kind)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				if (isBytecodeIntegralType(expr.type.kind)) pushCode(bytecode, integerMulOp(expr.type.kind));
				else pushCode(bytecode, floatMulOp(expr.type.kind));
				return true;
			}
			if (expr.token.type == Token::SLASH) {
				if (!isBytecodeIntegralType(expr.type.kind) && !isBytecodeFloatType(expr.type.kind)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				if (isBytecodeIntegralType(expr.type.kind)) pushCode(bytecode, integerDivOp(expr.type.kind));
				else pushCode(bytecode, floatDivOp(expr.type.kind));
				return true;
			}
			if (expr.token.type == Token::PERCENT) {
				// `%` is integral-only in the language, so the bytecode path rejects
				// any non-integral result type before it reaches the VM.
				if (!isBytecodeIntegralType(expr.type.kind)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left)) return false;
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.right)) return false;
				pushCode(bytecode, integerModOp(expr.type.kind));
				return true;
			}
			if (expr.token.type == Token::GT
				|| expr.token.type == Token::LT
				|| expr.token.type == Token::GT_EQUAL
				|| expr.token.type == Token::LT_EQUAL
				|| expr.token.type == Token::EQUAL_EQUAL
				|| expr.token.type == Token::BANG_EQUAL) {
				return bytecodeCompileComparisonExpr(module, bytecode, ctx, expr);
			}
			if (expr.token.type == Token::AND) {
				return bytecodeCompileLogicalAndExpr(module, bytecode, ctx, expr);
			}
			if (expr.token.type == Token::OR) {
				return bytecodeCompileLogicalOrExpr(module, bytecode, ctx, expr);
			}
			return false;
		}
		case Expr::FIELD: {
			// Field reads resolve to a slot offset inside the flattened aggregate,
			// then reuse the normal load path.
			BytecodeValueAccess access = {};
			if (!bytecodeResolveValueAccess(module, ctx, expr_idx, &access)) {
				return bytecodeEmitRuntimeFieldAccess(module, bytecode, ctx, expr);
			}
			return bytecodeEmitLoadValue(module, bytecode, ctx, access);
		}
		case Expr::INDEX: {
			BytecodeValueAccess access = {};
			if (!bytecodeResolveValueAccess(module, ctx, expr_idx, &access)) {
				// Dynamic array indices can not be folded into a direct slot offset.
				// We fall back to the runtime helper above, which computes the final
				// element address into a temp slot and then reads it back here.
				i32 temp_slot = -1;
				i32 slot_count = 0;
				if (!bytecodeEmitRuntimeIndexedAccess(module, bytecode, ctx, expr, &access, nullptr, &slot_count, &temp_slot)) return false;
				pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
				pushCode(bytecode, (u8)temp_slot);
				pushCode(bytecode, BytecodeOp::LOAD_INDIRECT);
				pushCode(bytecode, (u8)slot_count);
				return true;
			}
			return bytecodeEmitLoadValue(module, bytecode, ctx, access);
		}
		case Expr::STRUCT_LITERAL:
		case Expr::CONSTRUCTOR: {
			// Constructor arguments are already evaluated left-to-right, so the
			// resulting struct value is just the sequence of pushed field slots.
			for (i32 arg_idx : expr.args) {
				if (!bytecodeCompileExpr(module, bytecode, ctx, arg_idx)) return false;
			}
			return true;
		}
		case Expr::CALL: {
			// Calls are compiled to a resolved function index and a left-to-right
			// evaluation of receiver and arguments.
			const ls_string_view callee_name = empty(expr.qualified_name) ? bytecodeGetExpressionName(module, expr.left) : module.makeQualifiedName(expr.qualified_name.path, expr.qualified_name.name);
			i32 fn_idx = -1;
			const bool shadowed_by_local = expr.left >= 0 && module.expressions[expr.left].kind == Expr::VAR && ctx.findLocal(module.expressions[expr.left].name) >= 0;
			if (!shadowed_by_local && !empty(callee_name)) {
				fn_idx = module.findFunction(callee_name);
				if (fn_idx < 0 && expr.method_receiver < 0 && !expr.args.empty()) {
					TypeRef first_arg_type = module.expressions[expr.args[0]].type;
					const ls_string_view namespace_name = bytecodeGetTypeNamespace(module, first_arg_type);
					if (!empty(namespace_name)) {
						fn_idx = module.findFunction({namespace_name, callee_name});
						if (fn_idx >= 0) expr.qualified_name = {namespace_name, callee_name};
					}
				}
			}
			if (fn_idx >= 0) {
				i32 idx = fn_idx;
				FunctionDecl* callee_ptr = nullptr;
				for (Unit* unit_ptr : module.units) {
					Unit& unit = *unit_ptr;
					if (idx < (i32)unit.functions.size()) {
						callee_ptr = &unit.functions[(size_t)idx];
						break;
					}
					idx -= (i32)unit.functions.size();
				}
				if (callee_ptr) {
					const FunctionDecl& callee = *callee_ptr;
					i32 param_idx = 0;
					if (expr.method_receiver >= 0) {
						if (callee.params.empty()) return false;
						const TypeRef* expected = !callee.params.empty() ? &callee.params[0].type : nullptr;
						if (!callee.params.empty() && callee.params[0].is_ref) {
							// Method-style calls still obey the same ref rules as normal calls.
							// The receiver expression must be an explicit `ref ...` node, and
							// we forward the underlying address instead of copying the value.
							Expr& receiver_expr = module.expressions[expr.method_receiver];
							if (receiver_expr.kind != Expr::REF) return false;
							BytecodeValueAccess access = {};
							if (!bytecodeResolveValueAccess(module, ctx, receiver_expr.right, &access)) return false;
							if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
						}
						else if (!bytecodeCompileExpr(module, bytecode, ctx, expr.method_receiver, expected)) return false;
						param_idx = 1;
					}
					if ((i32)callee.params.size() != (i32)expr.args.size() + (expr.method_receiver >= 0 ? 1 : 0)) return false;
					for (i32 i = 0; i < expr.args.size(); ++i, ++param_idx) {
						const TypeRef* expected = param_idx < callee.params.size() ? &callee.params[param_idx].type : nullptr;
						if (param_idx < callee.params.size() && callee.params[param_idx].is_ref) {
							// Ref parameters are not value arguments. The AST represents them
							// explicitly as `Expr::REF`, so we resolve the pointed-to lvalue
							// and pass its address to the VM.
							Expr& arg_expr = module.expressions[expr.args[i]];
							if (arg_expr.kind != Expr::REF) return false;
							BytecodeValueAccess access = {};
							if (!bytecodeResolveValueAccess(module, ctx, arg_expr.right, &access)) return false;
							if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
						}
						else {
							if (!bytecodeCompileExpr(module, bytecode, ctx, expr.args[i], expected)) return false;
						}
					}
					pushCode(bytecode, BytecodeOp::CALL);
					pushCode(bytecode, (u32)fn_idx);
					return true;
				}
			}

			i32 native_idx = -1;
			if (!shadowed_by_local && !empty(callee_name)) {
				native_idx = module.findNativeFunction(callee_name);
				if (native_idx < 0 && expr.method_receiver < 0 && !expr.args.empty()) {
					TypeRef first_arg_type = module.expressions[expr.args[0]].type;
					const ls_string_view namespace_name = bytecodeGetTypeNamespace(module, first_arg_type);
					if (!empty(namespace_name)) {
						native_idx = module.findNativeFunction({namespace_name, callee_name});
						if (native_idx >= 0) expr.qualified_name = {namespace_name, callee_name};
					}
				}
			}
			if (native_idx >= 0) {
				i32 idx = native_idx;
				NativeFunctionDecl* callee_ptr = nullptr;
				for (Unit* unit_ptr : module.units) {
					Unit& unit = *unit_ptr;
					if (idx < (i32)unit.native_functions.size()) {
						callee_ptr = &unit.native_functions[(size_t)idx];
						break;
					}
					idx -= (i32)unit.native_functions.size();
				}
				if (!callee_ptr) return false;
				const NativeFunctionDecl& callee = *callee_ptr;
				i32 param_idx = 0;
				if (expr.method_receiver >= 0) {
					if (callee.params.empty()) return false;
					const TypeRef* expected = !callee.params.empty() ? &callee.params[0].type : nullptr;
					if (!callee.params.empty() && callee.params[0].is_ref) {
						Expr& receiver_expr = module.expressions[expr.method_receiver];
						if (receiver_expr.kind != Expr::REF) return false;
						BytecodeValueAccess access = {};
						if (!bytecodeResolveValueAccess(module, ctx, receiver_expr.right, &access)) return false;
						if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
					}
					else if (!bytecodeCompileExpr(module, bytecode, ctx, expr.method_receiver, expected)) return false;
					param_idx = 1;
				}
				if ((i32)callee.params.size() != (i32)expr.args.size() + (expr.method_receiver >= 0 ? 1 : 0)) return false;
				for (i32 i = 0; i < expr.args.size(); ++i, ++param_idx) {
					const TypeRef* expected = param_idx < callee.params.size() ? &callee.params[param_idx].type : nullptr;
					if (param_idx < callee.params.size() && callee.params[param_idx].is_ref) {
						Expr& arg_expr = module.expressions[expr.args[i]];
						if (arg_expr.kind != Expr::REF) return false;
						BytecodeValueAccess access = {};
						if (!bytecodeResolveValueAccess(module, ctx, arg_expr.right, &access)) return false;
						if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
					}
					else {
						if (!bytecodeCompileExpr(module, bytecode, ctx, expr.args[i], expected)) return false;
					}
				}
				pushCode(bytecode, BytecodeOp::CALL_NATIVE);
				pushCode(bytecode, (u32)native_idx);
				return true;
			}

			TypeRef callee_type = module.expressions[expr.left].type;
			if (callee_type.kind != LS_TYPE_FUNCTION || callee_type.struct_index < 0 || callee_type.struct_index >= module.function_types.size()) return false;
			FunctionTypeDecl& fn_type = module.function_types[callee_type.struct_index];
			if (fn_type.params.size() != expr.args.size()) return false;
			if (!bytecodeCompileExpr(module, bytecode, ctx, expr.left, &callee_type)) return false;
			for (i32 i = 0; i < expr.args.size(); ++i) {
				if (!bytecodeCompileExpr(module, bytecode, ctx, expr.args[i], &fn_type.params[i])) return false;
			}
			pushCode(bytecode, BytecodeOp::CALL_INDIRECT);
			pushCode(bytecode, (u8)fn_type.params.size());
			return true;
		}
		default:
			ASSERT(false);
			return false;
	}
}

static bool bytecodeCompileStmt(ls_module& module, ls_bytecode& bytecode, BytecodeCompileContext& ctx, i32 stmt_idx) {
	if (stmt_idx < 0 || stmt_idx >= module.statements.size()) return false;
	Stmt& stmt = module.statements[stmt_idx];
	switch (stmt.kind) {
		case Stmt::BLOCK: {
			// Blocks create a temporary lexical scope.
			ctx.pushScope();
			for (i32 child : stmt.children) {
				if (!bytecodeCompileStmt(module, bytecode, ctx, child)) {
					ctx.popScope();
					return false;
				}
			}
			if (!bytecodeEmitDeferredScope(module, bytecode, ctx, ctx.scopeDepth() - 1)) {
				ctx.popScope();
				return false;
			}
			ctx.popScope();
			return true;
		}
		case Stmt::EXPR: {
			// Expression statements must leave the stack balanced, so the value is
			// immediately discarded.
			if (stmt.expr >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.expr, &module.expressions[stmt.expr].type)) return false;
			if (stmt.expr >= 0 && !bytecodeEmitPopValue(module, bytecode, module.expressions[stmt.expr].type)) return false;
			return true;
		}
		case Stmt::VAR_DECL: {
			// Declarations evaluate the initializer first, then store the result in
			// the newly allocated local slot range.
			TypeRef type = stmt.type;
			if (type.kind == LS_TYPE_INVALID) return false;
			const i32 slot_count = bytecodeTypeSlotCount(module, type);
			if (slot_count <= 0) return false;
			const i32 local_slot = ctx.local_count;
			if (local_slot < 0 || local_slot + slot_count > 256) return false;
			if (stmt.expr >= 0) {
				if (!bytecodeCompileExpr(module, bytecode, ctx, stmt.expr, &stmt.type)) return false;
				if (!bytecodeEmitStoreValue(module, bytecode, local_slot, type)) return false;
			}
			else if (!stmt.is_undefined_init) {
				return false;
			}
			ctx.addLocal(stmt.name, local_slot, slot_count, type);
			ctx.local_count += slot_count;
			return true;
		}
		case Stmt::ASSIGN: {
			Expr& lhs = module.expressions[stmt.left];
			if (lhs.kind != Expr::VAR && lhs.kind != Expr::FIELD && lhs.kind != Expr::INDEX) return false;
			if (lhs.kind == Expr::FIELD || lhs.kind == Expr::INDEX) {
				BytecodeValueAccess access = {};
				if (!bytecodeResolveValueAccess(module, ctx, stmt.left, &access)) {
					if (lhs.kind != Expr::INDEX) return false;
					// Compound assignment on a non-constant array index needs the
					// address twice: once to read the old value and once to write the
					// updated value back. We compute that address once and park it in a
					// scratch slot so the index expression is still evaluated exactly once.
					i32 temp_slot = -1;
					i32 slot_count = 0;
					if (!bytecodeEmitRuntimeIndexedAccess(module, bytecode, ctx, lhs, &access, nullptr, &slot_count, &temp_slot)) return false;
					switch (stmt.assign_op) {
						case Token::EQUAL:
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &lhs.type)) return false;
							pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
							pushCode(bytecode, (u8)temp_slot);
							pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
							pushCode(bytecode, (u8)slot_count);
							return true;
						case Token::PLUS_EQUAL:
						case Token::MINUS_EQUAL:
						case Token::STAR_EQUAL:
						case Token::SLASH_EQUAL:
							if (slot_count != 1) return false;
							pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
							pushCode(bytecode, (u8)temp_slot);
							pushCode(bytecode, BytecodeOp::LOAD_INDIRECT);
							pushCode(bytecode, (u8)slot_count);
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &lhs.type)) return false;
							pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, lhs.type.kind));
							pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
							pushCode(bytecode, (u8)temp_slot);
							pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
							pushCode(bytecode, (u8)slot_count);
							return true;
						default:
							return false;
					}
				}
				const i32 slot_count = bytecodeTypeSlotCount(module, access.type);
				if (slot_count <= 0) return false;
				if (access.is_ref) {
					// Ref-backed field assignments use the same value shape as ordinary
					// stores, but the destination is reached through an address instead
					// of direct frame-relative slots.
					if (stmt.resolved_function >= 0) {
						if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
						pushCode(bytecode, BytecodeOp::CALL);
						pushCode(bytecode, (u32)stmt.resolved_function);
						if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
						pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
						pushCode(bytecode, (u8)slot_count);
						return true;
					}
					switch (stmt.assign_op) {
						case Token::EQUAL:
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
							if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
							pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
							pushCode(bytecode, (u8)slot_count);
							return true;
						case Token::PLUS_EQUAL:
						case Token::MINUS_EQUAL:
						case Token::STAR_EQUAL:
						case Token::SLASH_EQUAL:
							if (slot_count != 1) return false;
							if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
							pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, access.type.kind));
							if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
							pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
							pushCode(bytecode, (u8)slot_count);
							return true;
						default:
							return false;
					}
				}
				if (access.kind == BytecodeValueKind::LOCAL) {
					if (access.slot < 0 || access.slot + slot_count > 256) return false;
					if (stmt.resolved_function >= 0) {
						if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
						pushCode(bytecode, BytecodeOp::CALL);
						pushCode(bytecode, (u32)stmt.resolved_function);
						if (!bytecodeEmitStoreValue(module, bytecode, access.slot, access.type)) return false;
						return true;
					}
					switch (stmt.assign_op) {
						case Token::EQUAL:
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
							if (!bytecodeEmitStoreValue(module, bytecode, access.slot, access.type)) return false;
							return true;
						case Token::PLUS_EQUAL:
						case Token::MINUS_EQUAL:
						case Token::STAR_EQUAL:
						case Token::SLASH_EQUAL:
							// Compound assignment is compiled as `load -> eval rhs ->
							// arithmetic -> store`, which preserves single evaluation of the
							// left-hand side and works for both scalars and flat aggregates.
							if (slot_count != 1) return false;
							if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
							pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, access.type.kind));
							if (!bytecodeEmitStoreValue(module, bytecode, access.slot, access.type)) return false;
							return true;
						default:
							return false;
					}
				}
				if (access.kind == BytecodeValueKind::GLOBAL) {
					if (slot_count != 1 || access.slot < 0) return false;
					const GlobalDecl* global_ptr = nullptr;
					i32 global_idx = access.slot;
					for (const Unit* unit_ptr : module.units) {
						const Unit& unit = *unit_ptr;
						if (global_idx < (i32)unit.globals.size()) {
							global_ptr = &unit.globals[(size_t)global_idx];
							break;
						}
						global_idx -= (i32)unit.globals.size();
					}
					if (!global_ptr) return false;
					switch (stmt.assign_op) {
						case Token::EQUAL:
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &global_ptr->type)) return false;
							// Ordinary global assignment writes directly into the reserved
							// module-state prefix. This path stays separate from operator
							// lowering so a plain `=` never depends on overload resolution.
							pushCode(bytecode, BytecodeOp::STORE_GLOBAL);
							pushCode(bytecode, (u32)access.slot);
							return true;
						case Token::PLUS_EQUAL:
						case Token::MINUS_EQUAL:
						case Token::STAR_EQUAL:
						case Token::SLASH_EQUAL:
							pushCode(bytecode, BytecodeOp::LOAD_GLOBAL);
							pushCode(bytecode, (u32)access.slot);
							if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &global_ptr->type)) return false;
							pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, global_ptr->type.kind));
							pushCode(bytecode, BytecodeOp::STORE_GLOBAL);
							pushCode(bytecode, (u32)access.slot);
							return true;
						default:
							return false;
					}
				}
				return false;
			}
			const BytecodeCompileContext::LocalBinding* local_binding = ctx.findLocalBinding(lhs.name);
			if (local_binding) {
				const i32 local_slot = local_binding->slot;
				const i32 slot_count = local_binding->slot_count;
				// Local stores use frame-relative slot ranges, so structs can be
				// assigned without changing the stack representation.
				if (local_slot < 0 || local_slot + slot_count > 256) return false;
				BytecodeValueAccess access = {};
				if (!bytecodeResolveValueAccess(module, ctx, stmt.left, &access)) return false;
				if (stmt.resolved_function >= 0) {
					if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
					if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &local_binding->type)) return false;
					pushCode(bytecode, BytecodeOp::CALL);
					pushCode(bytecode, (u32)stmt.resolved_function);
					if (!bytecodeEmitStoreValue(module, bytecode, access.slot, access.type)) return false;
					return true;
				}
				switch (stmt.assign_op) {
					case Token::EQUAL:
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &local_binding->type)) return false;
						if (slot_count == 1) {
							pushCode(bytecode, BytecodeOp::STORE_LOCAL);
							pushCode(bytecode, (u8)local_slot);
						}
						else {
							if (!bytecodeEmitStoreValue(module, bytecode, local_slot, local_binding->type)) return false;
						}
						return true;
					case Token::PLUS_EQUAL:
					case Token::MINUS_EQUAL:
					case Token::STAR_EQUAL:
					case Token::SLASH_EQUAL:
						// Ref-backed writes follow the same compound-assignment shape as
						// locals, but the final store is indirect through the resolved slot
						// address instead of a frame-relative destination.
						if (slot_count != 1) return false;
						pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
						pushCode(bytecode, (u8)local_slot);
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &local_binding->type)) return false;
						pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, local_binding->type.kind));
						pushCode(bytecode, BytecodeOp::STORE_LOCAL);
						pushCode(bytecode, (u8)local_slot);
						return true;
					default:
						return false;
				}
			}
			if (const BytecodeCompileContext::LocalBinding* param_binding = ctx.findParamBinding(lhs.name)) {
				if (!param_binding->is_ref) return false;
				BytecodeValueAccess access = {};
				if (!bytecodeResolveValueAccess(module, ctx, stmt.left, &access)) return false;
				const i32 slot_count = bytecodeTypeSlotCount(module, access.type);
				if (slot_count <= 0) return false;
				// A ref parameter is itself just one slot holding an address. The
				// actual write still uses the pointed-to target, so the assignment is
				// lowered with the same indirect helpers as `Expr::REF` calls.
				if (stmt.resolved_function >= 0) {
					if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
					if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
					pushCode(bytecode, BytecodeOp::CALL);
					pushCode(bytecode, (u32)stmt.resolved_function);
					if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
					pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
					pushCode(bytecode, (u8)slot_count);
					return true;
				}
				switch (stmt.assign_op) {
					case Token::EQUAL:
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
						if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
						pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
						pushCode(bytecode, (u8)slot_count);
						return true;
					case Token::PLUS_EQUAL:
					case Token::MINUS_EQUAL:
					case Token::STAR_EQUAL:
					case Token::SLASH_EQUAL:
						if (slot_count != 1) return false;
						if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &access.type)) return false;
						pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, access.type.kind));
						if (!bytecodeEmitAddressOfValue(module, bytecode, ctx, access)) return false;
						pushCode(bytecode, BytecodeOp::STORE_INDIRECT);
						pushCode(bytecode, (u8)slot_count);
						return true;
					default:
						return false;
				}
			}
			const i32 global_idx = module.findGlobal(lhs.name);
			if (global_idx < 0) return false;
			// Global writes target the reserved module-state prefix.
			const GlobalDecl* global_ptr = nullptr;
			i32 remaining_global_idx = global_idx;
			for (const Unit* unit_ptr : module.units) {
				const Unit& unit = *unit_ptr;
				if (remaining_global_idx < (i32)unit.globals.size()) {
					global_ptr = &unit.globals[(size_t)remaining_global_idx];
					break;
				}
				remaining_global_idx -= (i32)unit.globals.size();
			}
			if (!global_ptr) return false;
			switch (stmt.assign_op) {
				case Token::EQUAL:
					if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &global_ptr->type)) return false;
					return bytecodeEmitStoreGlobalValue(module, bytecode, global_idx, global_ptr->type);
				case Token::PLUS_EQUAL:
				case Token::MINUS_EQUAL:
				case Token::STAR_EQUAL:
				case Token::SLASH_EQUAL:
					if (stmt.resolved_function >= 0) {
						// When compound assignment resolves to a user operator, we route
						// through the same load/call/store shape used for locals. The only
						// difference is that the final write targets the global prefix.
						BytecodeValueAccess access = {};
						access.kind = BytecodeValueKind::GLOBAL;
						access.slot = global_idx;
						access.type = global_ptr->type;
						if (!bytecodeEmitLoadValue(module, bytecode, ctx, access)) return false;
						if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &global_ptr->type)) return false;
						pushCode(bytecode, BytecodeOp::CALL);
						pushCode(bytecode, (u32)stmt.resolved_function);
						return bytecodeEmitStoreGlobalValue(module, bytecode, global_idx, global_ptr->type);
					}
					if (!isBytecodeIntegralType(global_ptr->type.kind) && !isBytecodeFloatType(global_ptr->type.kind)) return false;
					// Globals reuse the same arithmetic lowering as locals; the only
					// difference is that the final write is indexed into the module's
					// global prefix rather than a function frame.
					pushCode(bytecode, BytecodeOp::LOAD_GLOBAL);
					pushCode(bytecode, (u32)global_idx);
					if (stmt.right >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.right, &global_ptr->type)) return false;
					pushCode(bytecode, bytecodeCompoundArithmeticOp(stmt.assign_op, global_ptr->type.kind));
					pushCode(bytecode, BytecodeOp::STORE_GLOBAL);
					pushCode(bytecode, (u32)global_idx);
					return true;
				default:
					return false;
			}
		}
		case Stmt::RETURN:
			// Return statements optionally emit a value before the RETURN opcode.
			if (stmt.expr >= 0 && !bytecodeCompileExpr(module, bytecode, ctx, stmt.expr, &ctx.fn.return_type)) return false;
			if (!bytecodeEmitDeferredScopes(module, bytecode, ctx, 0)) return false;
			pushCode(bytecode, BytecodeOp::RETURN);
			return true;
		case Stmt::WHILE: {
			// While loops lower to: condition, conditional exit, body, backward
			// jump. Break/continue branches are patched once the loop end is known.
			if (stmt.expr < 0) return false;
			const size_t cond_pos = bytecode.code.size();
			if (!bytecodeCompileExpr(module, bytecode, ctx, stmt.expr)) return false;
			size_t end_jump = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &end_jump);

			BytecodeCompileContext::LoopTarget loop_target{stmt.name, cond_pos, ctx.scopeDepth()};
			ctx.loop_targets.push_back(loop_target);
			if (stmt.right >= 0 && !bytecodeCompileStmt(module, bytecode, ctx, stmt.right)) {
				ctx.loop_targets.pop_back();
				return false;
			}

			pushCode(bytecode, BytecodeOp::JUMP);
			const size_t back_jump_pos = bytecode.code.size();
			const i32 back_offset = (i32)(cond_pos - (back_jump_pos + sizeof(i32)));
			pushCode(bytecode, back_offset);

			const size_t end_pos = bytecode.code.size();
			BytecodeCompileContext::LoopTarget& loop = ctx.loop_targets.back();
			for (size_t break_jump : loop.break_jumps) patchJump(bytecode, break_jump, end_pos);
			for (size_t continue_jump : loop.continue_jumps) patchJump(bytecode, continue_jump, loop.continue_target);
			patchJump(bytecode, end_jump, end_pos);
			ctx.loop_targets.pop_back();
			return true;
		}
		case Stmt::FOR: {
			// `for` lowers to a single-evaluation header, a loop-local counter, and
			// a separate increment step that `continue` branches to.
			if (stmt.expr < 0 || stmt.left < 0 || stmt.right < 0) return false;
			const TypeRef start_type = module.expressions[stmt.expr].type;
			const TypeRef end_type = module.expressions[stmt.left].type;
			if (!isBytecodeComparisonType(start_type.kind) || !isBytecodeComparisonType(end_type.kind) || !sameBaseType(start_type, end_type)) return false;

			const i32 loop_slot = ctx.local_count;
			const i32 end_slot = loop_slot + 1;
			if (loop_slot < 0 || end_slot >= 256) return false;

			ctx.pushScope();
			ctx.addLocal(stmt.label_name, loop_slot, 1, start_type);
			ctx.addLocal({}, end_slot, 1, end_type);
			ctx.local_count += 2;

			if (!bytecodeCompileExpr(module, bytecode, ctx, stmt.expr, &start_type)) {
				ctx.popScope();
				return false;
			}
			if (!bytecodeEmitStoreValue(module, bytecode, loop_slot, start_type)) {
				ctx.popScope();
				return false;
			}
			if (!bytecodeCompileExpr(module, bytecode, ctx, stmt.left, &end_type)) {
				ctx.popScope();
				return false;
			}
			if (!bytecodeEmitStoreValue(module, bytecode, end_slot, end_type)) {
				ctx.popScope();
				return false;
			}

			const size_t cond_pos = bytecode.code.size();
			pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
			pushCode(bytecode, (u8)loop_slot);
			pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
			pushCode(bytecode, (u8)end_slot);
			pushCode(bytecode, BytecodeOp::CMP_LE);
			pushCode(bytecode, (u8)bytecodeComparisonKind(start_type.kind));

			size_t end_jump = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &end_jump);

			BytecodeCompileContext::LoopTarget loop_target{stmt.name, 0, ctx.scopeDepth()};
			ctx.loop_targets.push_back(loop_target);
			if (stmt.right >= 0 && !bytecodeCompileStmt(module, bytecode, ctx, stmt.right)) {
				ctx.loop_targets.pop_back();
				ctx.popScope();
				return false;
			}

			const size_t continue_pos = bytecode.code.size();
			pushCode(bytecode, BytecodeOp::LOAD_LOCAL);
			pushCode(bytecode, (u8)loop_slot);
			if (!bytecodeEmitOneValue(bytecode, start_type.kind)) {
				ctx.loop_targets.pop_back();
				ctx.popScope();
				return false;
			}
			pushCode(bytecode, bytecodeCompoundArithmeticOp(Token::PLUS_EQUAL, start_type.kind));
			pushCode(bytecode, BytecodeOp::STORE_LOCAL);
			pushCode(bytecode, (u8)loop_slot);

			pushCode(bytecode, BytecodeOp::JUMP);
			const size_t back_jump_pos = bytecode.code.size();
			const i32 back_offset = (i32)(cond_pos - (back_jump_pos + sizeof(i32)));
			pushCode(bytecode, back_offset);

			const size_t end_pos = bytecode.code.size();
			BytecodeCompileContext::LoopTarget& loop = ctx.loop_targets.back();
			loop.continue_target = continue_pos;
			for (size_t break_jump : loop.break_jumps) patchJump(bytecode, break_jump, end_pos);
			for (size_t continue_jump : loop.continue_jumps) patchJump(bytecode, continue_jump, loop.continue_target);
			patchJump(bytecode, end_jump, end_pos);
			ctx.loop_targets.pop_back();
			ctx.popScope();
			return true;
		}
		case Stmt::BREAK:
		case Stmt::CONTINUE: {
			// Loop exits are emitted as placeholders and patched later.
			const i32 loop_idx = ctx.findLoopIndex(stmt.name);
			if (loop_idx < 0) return false;
			if (!bytecodeEmitDeferredScopes(module, bytecode, ctx, ctx.loop_targets[loop_idx].cleanup_scope_depth)) return false;
			size_t jump_pos = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &jump_pos);
			if (stmt.kind == Stmt::BREAK) ctx.loop_targets[loop_idx].break_jumps.push_back(jump_pos);
			else ctx.loop_targets[loop_idx].continue_jumps.push_back(jump_pos);
			return true;
		}
		case Stmt::IF: {
			// If/else lowers to a conditional branch over the then block, plus an
			// optional unconditional jump over the else block.
			if (stmt.expr < 0) return false;
			ls_string_view promoted_name;
			TypeRef promoted_type;
			bool promote_true_branch = false;
			const bool has_promotion = bytecodeCompileNullablePromotion(module, bytecode, ctx, stmt.expr, &promoted_name, &promoted_type, &promote_true_branch);
			if (!bytecodeCompileExpr(module, bytecode, ctx, stmt.expr)) return false;
			size_t else_jump = 0;
			emitJumpPlaceholder(bytecode, BytecodeOp::JUMP_IF_FALSE, &else_jump);
			auto compileBranch = [&](i32 stmt_idx, bool promote) -> bool {
				if (stmt_idx < 0) return true;
				ctx.pushScope();
				if (promote) {
					const BytecodeCompileContext::LocalBinding* binding = ctx.findLocalBinding(promoted_name);
					if (!binding) {
						ctx.popScope();
						return false;
					}
					ctx.addLocal(promoted_name, binding->slot, binding->slot_count, promoted_type, false, false, 1);
				}
				const bool ok = bytecodeCompileStmt(module, bytecode, ctx, stmt_idx);
				ctx.popScope();
				return ok;
			};
			if (!has_promotion || promote_true_branch) {
				if (stmt.left >= 0 && !compileBranch(stmt.left, has_promotion && promote_true_branch)) return false;
			}
			else if (stmt.left >= 0 && !compileBranch(stmt.left, false)) return false;
			size_t end_jump = 0;
			if (stmt.right >= 0) {
				emitJumpPlaceholder(bytecode, BytecodeOp::JUMP, &end_jump);
			}
			const size_t else_pos = bytecode.code.size();
			patchJump(bytecode, else_jump, else_pos);
			if (stmt.right >= 0) {
				const bool promote_else_branch = has_promotion && !promote_true_branch;
				if (!compileBranch(stmt.right, promote_else_branch)) return false;
			}
			if (stmt.right >= 0) {
				const size_t end_pos = bytecode.code.size();
				patchJump(bytecode, end_jump, end_pos);
			}
			return true;
		}
		case Stmt::DEFER:
			ctx.addDeferred(stmt.left);
			return true;
		case Stmt::MATCH:
			return bytecodeCompileMatchStmt(module, bytecode, ctx, stmt);
		default:
			return false;
	}
}

static bool bytecodeCompileFunction(ls_module& module, ls_bytecode& bytecode, FunctionDecl& fn) {
	BytecodeFunction out;
	out.name = module.makeQualifiedName(fn.canonical_name.path, fn.canonical_name.name);
	out.code_offset = (i32)bytecode.code.size();
	BytecodeCompileContext ctx(fn);
	ctx.pushScope();
	i32 param_slot = 0;
	for (const Param& p : fn.params) {
		// Parameters are predeclared bindings in the function frame.
		const i32 slot_count = p.is_ref ? 1 : bytecodeTypeSlotCount(module, p.type);
		if (slot_count <= 0) return false;
		out.params.push_back(toC(p.type));
		ctx.addLocal(p.name, param_slot, slot_count, p.type, true, p.is_ref);
		param_slot += slot_count;
	}
	out.param_slot_count = param_slot;
	if (!bytecodeCompileStmt(module, bytecode, ctx, fn.body)) return false;

	// Frame size and code span are finalized after the body is emitted.
	out.code_size = (i32)bytecode.code.size() - out.code_offset;
	out.local_count = ctx.local_count;
	out.return_count = bytecodeTypeSlotCount(module, fn.return_type);
	out.return_type = toC(fn.return_type);
	bytecode.functions.push_back(out);
	return true;
}

static bool bytecodeCompileGlobals(ls_module& module, ls_bytecode& bytecode, const ls_host* host) {
	// Global storage is counted up front because it reserves the runtime stack
	// prefix even before initialization runs.
	i32 global_slot = 0;
	bytecode.global_init_code.clear();
	bool has_globals = false;
	for (const Unit* unit_ptr : module.units) {
		const Unit& unit = *unit_ptr;
		if (!unit.globals.empty()) {
			has_globals = true;
			break;
		}
	}
	if (!has_globals) return true;

	/*
		Globals are stored in the same `stack` vector as call frames, but the first
		`global_count` entries are reserved permanently for module state.

		We compile a hidden init program that evaluates globals in declaration order
		and stores each result into that reserved prefix. This preserves the runtime
		semantics already used by the interpreter:

		- globals initialize once
		- later globals can read earlier globals
		- assignments to globals mutate persistent storage, not a transient frame

		Keeping this separate from normal function bytecode makes the startup path
		explicit and avoids mixing module initialization with user-visible code.
	*/
	ls_bytecode init_bytecode(host);
	FunctionDecl init_fn;
	BytecodeCompileContext ctx(init_fn);
	ctx.pushScope();

	for (Unit* unit_ptr : module.units) {
		Unit& unit = *unit_ptr;
		for (GlobalDecl& global : unit.globals) {
			const i32 slot_count = bytecodeTypeSlotCount(module, global.type);
			if (slot_count <= 0) return false;
			// Assign slot ranges before compiling any function bodies so reads and
			// writes to globals can resolve the flattened offset immediately.
			global.slot = global_slot;
			global.slot_count = slot_count;
			global_slot += slot_count;
		}
	}

	for (Unit* unit_ptr : module.units) {
		Unit& unit = *unit_ptr;
		for (GlobalDecl& global : unit.globals) {
			if (global.expr < 0) continue;
			if (!bytecodeCompileExpr(module, init_bytecode, ctx, global.expr, &global.type)) return false;
			if (!bytecodeEmitStoreGlobalValue(module, init_bytecode, global.slot, global.type)) return false;
		}
	}

	bytecode.global_count = global_slot;
	bytecode.global_init_code = std::move(init_bytecode.code);
	return true;
}

ls_bytecode* compileBytecode(ls_module& module, const ls_host* host) {
	// Lay out globals first so function lowering can resolve their flattened
	// stack slots, then emit functions and the hidden global initializer program.
	const ls_host* bytecode_host = host ? host : &module.host;
	void* bytecode_mem = bytecode_host && bytecode_host->allocate
		? bytecode_host->allocate(bytecode_host->allocator_userdata, sizeof(ls_bytecode), alignof(ls_bytecode))
		: ::operator new(sizeof(ls_bytecode), std::nothrow);
	ls_bytecode* bytecode = bytecode_mem ? ::new (bytecode_mem) ls_bytecode(bytecode_host) : nullptr;
	if (!bytecode) return nullptr;
	i32 native_function_count = 0;
	for (const Unit* unit_ptr : module.units) native_function_count += (i32)unit_ptr->native_functions.size();
	bytecode->native_functions.reserve(native_function_count);
	for (const Unit* unit_ptr : module.units) {
		const Unit& unit = *unit_ptr;
		for (const NativeFunctionDecl& fn : unit.native_functions) {
			BytecodeNativeFunction out;
			out.canonical_name = module.makeQualifiedName(fn.canonical_name.path, fn.canonical_name.name);
			for (const Param& param : fn.params) out.params.push_back(toC(param.type));
			out.return_type = toC(fn.return_type);
			out.return_count = bytecodeTypeSlotCount(module, fn.return_type);
			bytecode->native_functions.push_back(std::move(out));
		}
	}
	
	if (!bytecodeCompileGlobals(module, *bytecode, bytecode_host)) {
		ls_bytecode_destroy(bytecode);
		return nullptr;
	}
	for (Unit* unit_ptr : module.units) {
		Unit& unit = *unit_ptr;
		for (FunctionDecl& fn : unit.functions) {
			if (!bytecodeCompileFunction(module, *bytecode, fn)) {
				ls_bytecode_destroy(bytecode);
				return nullptr;
			}
		}
	}
	return bytecode;
}

extern "C" {

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	if (!bytecode) return;
	ls_host host = bytecode->host;
	bytecode->~ls_bytecode();
	deallocateMemory(&host, bytecode);
}

ls_bytecode* ls_bytecode_compile(
	ls_module* module,
	ls_host* host
) {
	if (!module) return nullptr;
	return compileBytecode(*module, host);
}

}
