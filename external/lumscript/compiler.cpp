#include "compiler.h"
#include "utils.h"
#include <float.h>
#include <cstring>

struct TypeKindInfo {
	ResolvedType::Kind kind;
	const char* member_name;
	const char* primitive_name;
};

static constexpr TypeKindInfo TYPE_KIND_INFOS[] = {
	{ResolvedType::BOOL, "Bool", "bool"},
	{ResolvedType::I8, "I8", "i8"},
	{ResolvedType::I16, "I16", "i16"},
	{ResolvedType::I32, "I32", "i32"},
	{ResolvedType::I64, "I64", "i64"},
	{ResolvedType::ISIZE, "ISize", "isize"},
	{ResolvedType::U8, "U8", "u8"},
	{ResolvedType::U16, "U16", "u16"},
	{ResolvedType::U32, "U32", "u32"},
	{ResolvedType::U64, "U64", "u64"},
	{ResolvedType::BYTE, "Byte", "byte"},
	{ResolvedType::F32, "F32", "f32"},
	{ResolvedType::F64, "F64", "f64"},
	{ResolvedType::CSTR, "CStr", "cstr"},
	{ResolvedType::CPTR, "CPtr", "cptr"},
	{ResolvedType::VOID, "Void", "void"},
	{ResolvedType::META, "Type", "type"},
	{ResolvedType::NULLABLE, "Nullable", nullptr},
	{ResolvedType::SLICE, "Slice", nullptr},
	{ResolvedType::ARRAY, "Array", nullptr},
	{ResolvedType::ENUM, "Enum", nullptr},
	{ResolvedType::STRUCT, "Struct", nullptr},
	{ResolvedType::UNION, "Union", nullptr},
	{ResolvedType::FUNCTION, "Fn", nullptr},
	{ResolvedType::POINTER, "Pointer", nullptr},
};

ls_module::ls_module(ls_host* host)
	: host(host)
	, arena(host->arena)
	, units(arena)
	, union_types(arena)
	, type_kind_decl(arena)
{
	for (i32 i = 0; i < ResolvedType::META; ++i)
		primitives[i].kind = static_cast<ResolvedType::Kind>(i);
	type_kind_decl.cached_name = makeStringView("TypeKind");
	for (const TypeKindInfo& info : TYPE_KIND_INFOS) {
		EnumMember& member = type_kind_decl.members.emplace_back();
		member.name = makeStringView(info.member_name);
	}
	type_kind.kind = ResolvedType::ENUM;
	type_kind.decl = &type_kind_decl;
}

static ResolvedType* structFieldType(const StructResolvedType& st, i32 index) {
	ASSERT(st.decl);
	if (index < st.field_types.size()) return st.field_types[(u32)index];
	ASSERT(false);
	return nullptr;
}

u32 typeByteSize(const ResolvedType& t) {
	switch (t.kind) {
		case ResolvedType::VOID: return 0;
		case ResolvedType::BOOL:
		case ResolvedType::I8:
		case ResolvedType::U8:
		case ResolvedType::BYTE:
			return 1;
		case ResolvedType::I16:
		case ResolvedType::U16:
			return 2;
		case ResolvedType::I32:
		case ResolvedType::U32:
		case ResolvedType::F32:
		case ResolvedType::ENUM:
		case ResolvedType::FUNCTION:
			return 4;
		case ResolvedType::I64:
		case ResolvedType::U64:
		case ResolvedType::ISIZE:
		case ResolvedType::F64:
		case ResolvedType::CSTR:
		case ResolvedType::CPTR:
			return 8;
		case ResolvedType::NULLABLE: return 1 + typeByteSize(*static_cast<const NullableResolvedType&>(t).inner);
		case ResolvedType::UNION: {
			const UnionResolvedType& un = static_cast<const UnionResolvedType&>(t);
			u32 max_size = 0;
			for (ResolvedType* member : un.members) max_size = max_size > typeByteSize(*member) ? max_size : typeByteSize(*member);
			return 4 + max_size; // i32 tag followed by the largest member payload
		}
		case ResolvedType::SLICE: return 16;
		case ResolvedType::ARRAY: {
			const ArrayResolvedType& arr = static_cast<const ArrayResolvedType&>(t);
			ASSERT(arr.size > 0);
			ASSERT(arr.size < 0xffFFffFF); // TODO
			return (u32)arr.size * typeByteSize(*arr.element_type);
		}
		case ResolvedType::STRUCT: {
			const StructResolvedType& st = static_cast<const StructResolvedType&>(t);
			u32 count = 0;
			if (st.decl) {
				for (i32 i = 0; i < st.decl->fields.size(); ++i) {
					ResolvedType* field_type = structFieldType(st, i);
					if (field_type) count += typeByteSize(*field_type);
				}
			}
			return count ? count : 1;
		}
		case ResolvedType::UNTYPED_FLOAT:
		case ResolvedType::UNTYPED_INT:
			return 8;
		case ResolvedType::META:
			return 0;
		case ResolvedType::POINTER: return 8;
		default:
			ASSERT(false);
			return 1;
	}
}

// TODO merge
// Mirrors runtime_numeric_to_{i64,u64,double} in runtime.c so `comptime` casts fold to the
// same bits the VM's LS_OP_CAST would produce at runtime.
static i64 comptimeNumericToI64(const u8* p, ResolvedType::Kind kind) {
	switch (kind) {
		case ResolvedType::BOOL: return p[0] != 0 ? 1 : 0;
		case ResolvedType::I8: { i8 v; memcpy(&v, p, 1); return (i64)v; }
		case ResolvedType::U8:
		case ResolvedType::BYTE: { u8 v; memcpy(&v, p, 1); return (i64)v; }
		case ResolvedType::I16: { i16 v; memcpy(&v, p, 2); return (i64)v; }
		case ResolvedType::U16: { u16 v; memcpy(&v, p, 2); return (i64)v; }
		case ResolvedType::I32:
		case ResolvedType::ENUM: { i32 v; memcpy(&v, p, 4); return (i64)v; }
		case ResolvedType::U32: { u32 v; memcpy(&v, p, 4); return (i64)v; }
		case ResolvedType::I64:
		case ResolvedType::ISIZE:
		case ResolvedType::UNTYPED_INT: { i64 v; memcpy(&v, p, 8); return v; }
		case ResolvedType::U64: { u64 v; memcpy(&v, p, 8); return (i64)v; }
		case ResolvedType::F32: { float v; memcpy(&v, p, 4); return (i64)v; }
		case ResolvedType::F64: { double v; memcpy(&v, p, 8); return (i64)v; }
		default: return 0;
	}
}

static u64 comptimeNumericToU64(const u8* p, ResolvedType::Kind kind) {
	switch (kind) {
		case ResolvedType::BOOL: return p[0] != 0 ? 1u : 0u;
		case ResolvedType::I8: { i8 v; memcpy(&v, p, 1); return (u64)v; }
		case ResolvedType::U8:
		case ResolvedType::BYTE: { u8 v; memcpy(&v, p, 1); return (u64)v; }
		case ResolvedType::I16: { i16 v; memcpy(&v, p, 2); return (u64)v; }
		case ResolvedType::U16: { u16 v; memcpy(&v, p, 2); return (u64)v; }
		case ResolvedType::I32:
		case ResolvedType::ENUM: { i32 v; memcpy(&v, p, 4); return (u64)v; }
		case ResolvedType::U32: { u32 v; memcpy(&v, p, 4); return (u64)v; }
		case ResolvedType::I64:
		case ResolvedType::ISIZE:
		case ResolvedType::UNTYPED_INT: { i64 v; memcpy(&v, p, 8); return (u64)v; }
		case ResolvedType::U64: { u64 v; memcpy(&v, p, 8); return v; }
		case ResolvedType::F32: { float v; memcpy(&v, p, 4); return (u64)v; }
		case ResolvedType::F64: { double v; memcpy(&v, p, 8); return (u64)v; }
		default: return 0u;
	}
}

static double comptimeNumericToF64(const u8* p, ResolvedType::Kind kind) {
	switch (kind) {
		case ResolvedType::BOOL: return p[0] != 0 ? 1.0 : 0.0;
		case ResolvedType::I8: { i8 v; memcpy(&v, p, 1); return (double)v; }
		case ResolvedType::U8:
		case ResolvedType::BYTE: { u8 v; memcpy(&v, p, 1); return (double)v; }
		case ResolvedType::I16: { i16 v; memcpy(&v, p, 2); return (double)v; }
		case ResolvedType::U16: { u16 v; memcpy(&v, p, 2); return (double)v; }
		case ResolvedType::I32:
		case ResolvedType::ENUM: { i32 v; memcpy(&v, p, 4); return (double)v; }
		case ResolvedType::U32: { u32 v; memcpy(&v, p, 4); return (double)v; }
		case ResolvedType::I64:
		case ResolvedType::ISIZE: { i64 v; memcpy(&v, p, 8); return (double)v; }
		case ResolvedType::U64: { u64 v; memcpy(&v, p, 8); return (double)v; }
		case ResolvedType::F32: { float v; memcpy(&v, p, 4); return (double)v; }
		case ResolvedType::F64:
		case ResolvedType::UNTYPED_FLOAT: { double v; memcpy(&v, p, 8); return v; }
		default: return 0.0;
	}
}

static i32 compareComptimeNumeric(const ComptimeValue& a, const ComptimeValue& b) {
	const bool a_float = a.type->kind == ResolvedType::F32 || a.type->kind == ResolvedType::F64 || a.type->kind == ResolvedType::UNTYPED_FLOAT;
	const bool b_float = b.type->kind == ResolvedType::F32 || b.type->kind == ResolvedType::F64 || b.type->kind == ResolvedType::UNTYPED_FLOAT;
	if (a_float || b_float) {
		const double av = comptimeNumericToF64(a.value, a.type->kind);
		const double bv = comptimeNumericToF64(b.value, b.type->kind);
		return av < bv ? -1 : av > bv ? 1 : 0;
	}

	const bool a_unsigned = a.type->kind >= ResolvedType::U8 && a.type->kind <= ResolvedType::U64;
	const bool b_unsigned = b.type->kind >= ResolvedType::U8 && b.type->kind <= ResolvedType::U64;
	const i64 ai = comptimeNumericToI64(a.value, a.type->kind);
	const i64 bi = comptimeNumericToI64(b.value, b.type->kind);
	if (a_unsigned == b_unsigned) return a_unsigned
		? (comptimeNumericToU64(a.value, a.type->kind) < comptimeNumericToU64(b.value, b.type->kind) ? -1 : comptimeNumericToU64(a.value, a.type->kind) > comptimeNumericToU64(b.value, b.type->kind) ? 1 : 0)
		: (ai < bi ? -1 : ai > bi ? 1 : 0);
	if (a_unsigned) {
		if (bi < 0) return 1;
		const u64 au = comptimeNumericToU64(a.value, a.type->kind);
		return au < (u64)bi ? -1 : au > (u64)bi ? 1 : 0;
	}
	if (ai < 0) return -1;
	const u64 bu = comptimeNumericToU64(b.value, b.type->kind);
	return (u64)ai < bu ? -1 : (u64)ai > bu ? 1 : 0;
}

// Converts `src_bytes` (encoded as `src_kind`) to `dst_kind`, writes it to `dst`, and returns
// the number of bytes written. Shared by comptime CAST folding and int-literal materialization,
// both of which need to reinterpret one numeric encoding as another.
static u32 writeComptimeNumeric(u8* dst, const u8* src_bytes, ResolvedType::Kind src_kind, ResolvedType::Kind dst_kind) {
	switch (dst_kind) {
		case ResolvedType::I8: { i8 v = (i8)comptimeNumericToI64(src_bytes, src_kind); memcpy(dst, &v, 1); return 1; }
		case ResolvedType::U8: { u8 v = (u8)comptimeNumericToU64(src_bytes, src_kind); memcpy(dst, &v, 1); return 1; }
		case ResolvedType::I16: { i16 v = (i16)comptimeNumericToI64(src_bytes, src_kind); memcpy(dst, &v, 2); return 2; }
		case ResolvedType::U16: { u16 v = (u16)comptimeNumericToU64(src_bytes, src_kind); memcpy(dst, &v, 2); return 2; }
		case ResolvedType::I32:
		case ResolvedType::ENUM: { i32 v = (i32)comptimeNumericToI64(src_bytes, src_kind); memcpy(dst, &v, 4); return 4; }
		case ResolvedType::U32: { u32 v = (u32)comptimeNumericToU64(src_bytes, src_kind); memcpy(dst, &v, 4); return 4; }
		case ResolvedType::I64:
		case ResolvedType::ISIZE: { i64 v = comptimeNumericToI64(src_bytes, src_kind); memcpy(dst, &v, 8); return 8; }
		case ResolvedType::U64: { u64 v = comptimeNumericToU64(src_bytes, src_kind); memcpy(dst, &v, 8); return 8; }
		case ResolvedType::F32: { float v = (float)comptimeNumericToF64(src_bytes, src_kind); memcpy(dst, &v, 4); return 4; }
		case ResolvedType::F64: { double v = comptimeNumericToF64(src_bytes, src_kind); memcpy(dst, &v, 8); return 8; }
		case ResolvedType::UNTYPED_FLOAT: { double v = comptimeNumericToF64(src_bytes, src_kind); memcpy(dst, &v, 8); return 8; }
		case ResolvedType::UNTYPED_INT: { i64 v = comptimeNumericToI64(src_bytes, src_kind); memcpy(dst, &v, 8); return 8; }
		default: ASSERT(false); return 0;
	}
}

struct TemplateBinding {
	ls_string_view name = {};
	ComptimeValue arg;
};

struct TemplateBindings {
	TemplateBindings(ls_arena& arena) : values(arena) {}

	ExpArray<TemplateBinding> values; // TODO allocation
};

static void appendReflectedTypeName(char*& out, char* end, const ResolvedType& type) {
		auto text = [&out, end](const char* value) { while (*value && out < end) *out++ = *value++; };
		auto view = [&out, end](ls_string_view value) { while (value.begin != value.end && out < end) *out++ = *value.begin++; };
	switch (type.kind) {
		case ResolvedType::VOID: case ResolvedType::BOOL: case ResolvedType::I8: case ResolvedType::I16:
		case ResolvedType::I32: case ResolvedType::I64: case ResolvedType::U8: case ResolvedType::U16:
		case ResolvedType::U32: case ResolvedType::U64: case ResolvedType::ISIZE: case ResolvedType::F32:
		case ResolvedType::F64: case ResolvedType::CSTR: case ResolvedType::CPTR:
		case ResolvedType::BYTE: {
			static const char* names[] = {"void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "f32", "f64", "cstr", "cptr", "byte"};
			text(names[type.kind - ResolvedType::VOID]); return;
		}
		case ResolvedType::ENUM: view(static_cast<const EnumResolvedType&>(type).decl->cached_name); return;
		case ResolvedType::STRUCT: {
			const StructResolvedType& st = static_cast<const StructResolvedType&>(type);
			view(st.decl->cached_name);
			return;
		}
		case ResolvedType::FUNCTION: {
			const FunctionResolvedType& fn = static_cast<const FunctionResolvedType&>(type); text("fn(");
			for (i32 i = 0; i < fn.params.size(); ++i) {
				if (i) text(", ");
				if (fn.params[i].is_comptime) text("comptime ");
				if (!empty(fn.params[i].name)) { view(fn.params[i].name); text(" : "); }
				appendReflectedTypeName(out, end, *fn.params[i].type);
			}
			text(") : "); appendReflectedTypeName(out, end, *fn.return_type); return;
		}
		case ResolvedType::ARRAY: {
			const ArrayResolvedType& a = static_cast<const ArrayResolvedType&>(type); text("[");
			char digits[32]; char* digits_end = digits + sizeof(digits); char* d = digits_end; i64 n = a.size; if (!n) *--d = '0';
			while (n) { *--d = char('0' + n % 10); n /= 10; } while (d != digits_end && out < end) *out++ = *d++;
			text("]"); appendReflectedTypeName(out, end, *a.element_type); return;
		}
		case ResolvedType::SLICE: {
			const auto& slice = static_cast<const SliceResolvedType&>(type);
			text("[]"); if (slice.is_const) text("const ");
			appendReflectedTypeName(out, end, *slice.element_type); return;
		}
		case ResolvedType::POINTER: {
			const auto& pointer = static_cast<const PointerResolvedType&>(type);
			text("*"); if (pointer.is_const) text("const ");
			appendReflectedTypeName(out, end, *pointer.inner); return;
		}
		case ResolvedType::NULLABLE: text("?"); appendReflectedTypeName(out, end, *static_cast<const NullableResolvedType&>(type).inner); return;
		case ResolvedType::UNION: {
			const UnionResolvedType& u = static_cast<const UnionResolvedType&>(type);
			for (i32 i = 0; i < u.members.size(); ++i) {
				if (i) text(" | ");
				appendReflectedTypeName(out, end, *u.members[i]);
			}
			return;
		}
		default: text("type"); return;
	}
}

// TODO what to do with you
	static ls_string_view reflectedTypeName(Unit& unit, const ResolvedType& type) {
	char buffer[4096]; char* out = buffer; char* end = buffer + sizeof(buffer) - 1;
	appendReflectedTypeName(out, end, type); *out = 0;
	char* copy = static_cast<char*>(unit.arena.allocate(unit.arena.user_data, (u32)(out - buffer), 1));
	copyMemory(copy, buffer, (usize)(out - buffer));
		return {copy, copy + (out - buffer)};
	}

	static ls_string_view factoryTypeName(Unit& unit, const FunctionExpression& fn, const ExpArray<ComptimeValue>& args) {
		char buffer[4096];
		char* out = buffer;
		char* end = buffer + sizeof(buffer) - 1;
		auto text = [&out, end](const char* value) { while (*value && out < end) *out++ = *value++; };
		auto integer = [&out, end](u64 value) {
			char digits[32];
			char* p = digits + sizeof(digits);
			do { *--p = char('0' + value % 10); value /= 10; } while (value);
			while (p != digits + sizeof(digits) && out < end) *out++ = *p++;
		};
		for (const char* p = fn.token.value.begin; p && p != fn.token.value.end && out < end; ++p) *out++ = *p;
		text("(");
		for (i32 i = 0; i < args.size(); ++i) {
			if (i) text(", ");
			const ComptimeValue& arg = args[i];
			if (arg.kind == ComptimeValue::TYPE) appendReflectedTypeName(out, end, *arg.type);
			else if (arg.kind == ComptimeValue::VALUE && arg.type && arg.value) {
				if (arg.type->kind == ResolvedType::BOOL) text(*arg.value ? "true" : "false");
				else if (arg.type->kind >= ResolvedType::U8 && arg.type->kind <= ResolvedType::U64) integer(comptimeNumericToU64(arg.value, arg.type->kind));
				else if (arg.type->kind == ResolvedType::UNTYPED_INT || (arg.type->kind >= ResolvedType::I8 && arg.type->kind <= ResolvedType::ISIZE)) {
					i64 value = comptimeNumericToI64(arg.value, arg.type->kind);
					if (value < 0) { text("-"); integer(u64(-(value + 1)) + 1); }
					else integer(u64(value));
				}
				else text("value");
			}
			else text("value");
		}
		text(")");
		char* copy = static_cast<char*>(unit.arena.allocate(unit.arena.user_data, (u32)(out - buffer), 1));
		copyMemory(copy, buffer, (usize)(out - buffer));
		return {copy, copy + (out - buffer)};
	}

struct Checker {
	struct ComptimeSliceValue {
		u8* data;
		i64 count;
	};
	struct ComptimeFrame {
		struct Local {
			ls_string_view name;
			u8* bytes = nullptr;
			ResolvedType* type;
			ComptimeValue value;
		};

		Unit& unit;
		ExpArray<Local> locals;

		ComptimeFrame(Unit& unit) : unit(unit), locals(unit.arena) {}
		Local* find(ls_string_view name) {
			for (i32 i = (i32)locals.size() - 1; i >= 0; --i) {
				if (equalStrings(locals[i].name, name)) return &locals[i];
			}
			return nullptr;
		}
	};

	Checker(ls_module& module)
		: module(module)
	{
		error_stream.host = module.host;
		meta_value_type = makeType<MetaType>(module.arena);
		const_u8_slice = makeType<SliceResolvedType>(module.arena);
		const_u8_slice->element_type = primitiveType(ResolvedType::U8);
		const_u8_slice->is_const = true;
		slice_of_types = makeType<SliceResolvedType>(module.arena);
		slice_of_types->element_type = meta_value_type;
		const char* descriptor_names[] = {"name", "type"};
		ResolvedType* descriptor_types[] = {const_u8_slice, meta_value_type};
		field_descriptor_type = makeStructType(module.arena, descriptor_names, descriptor_types, 2);
		param_descriptor_type = makeStructType(module.arena, descriptor_names, descriptor_types, 2);
		slice_of_fields = makeType<SliceResolvedType>(module.arena);
		slice_of_fields->element_type = field_descriptor_type;
		slice_of_params = makeType<SliceResolvedType>(module.arena);
		slice_of_params->element_type = param_descriptor_type;
	}

	// `isNumericType` and `isIntegerType` deliberately report concrete types only, so the
	// existing hint/conversion logic keeps treating UNTYPED_INT as "not pinned yet". The
	// operator code that wants to accept an untyped operand uses these *OrUntyped helpers.
	static bool isIntegerType(const ResolvedType& t) { return t.kind >= ResolvedType::I8 && t.kind <= ResolvedType::ISIZE; }
	static bool isFloatType(const ResolvedType& t) { return t.kind >= ResolvedType::F32 && t.kind <= ResolvedType::F64; }
	static bool isNumericType(const ResolvedType& type) { return type.kind >= ResolvedType::I8 && type.kind <= ResolvedType::F64; }
	static bool isUntypedNumeric(const ResolvedType& t) { return t.kind == ResolvedType::UNTYPED_INT || t.kind == ResolvedType::UNTYPED_FLOAT; }
	static bool isNumericOrUntyped(const ResolvedType& t) { return isNumericType(t) || isUntypedNumeric(t); }
	static bool isIntegerOrUntyped(const ResolvedType& t) { return isIntegerType(t) || t.kind == ResolvedType::UNTYPED_INT; }

	// Element types that slice `==` can compare. Restricted to scalars so the
	// comparison never dispatches to a user `operator ==`, and so integral
	// elements can be compared as raw bytes without padding getting in the way.
	static bool hasBuiltinElementEquality(const ResolvedType& t) {
		return isNumericType(t) || t.kind == ResolvedType::BOOL || t.kind == ResolvedType::BYTE || t.kind == ResolvedType::ENUM;
	}

	// `[]T` and `[]const T` compare with each other: `const` restricts writing,
	// not the values being read.
	static bool sliceTypesComparable(const ResolvedType* a, const ResolvedType* b) {
		if (!a || !b || a->kind != ResolvedType::SLICE || b->kind != ResolvedType::SLICE) return false;
		const SliceResolvedType* sa = static_cast<const SliceResolvedType*>(a);
		const SliceResolvedType* sb = static_cast<const SliceResolvedType*>(b);
		if (!sa->element_type || !typesEqual(sa->element_type, sb->element_type)) return false;
		return hasBuiltinElementEquality(*sa->element_type);
	}
	template <typename T, typename... Args> static T* makeType(ls_arena& arena, Args&&... args) {
		// Semantic nodes live as long as their owning unit. Allocating them from the
		// unit arena also keeps cached types and template instances pointer-stable.
		void* mem = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		return ::new (mem) T(static_cast<Args&&>(args)...);
	}

	StructResolvedType* makeStructType(ls_arena& arena, const char* const* names, ResolvedType* const* types, u32 count) {
		StructExpression* decl = makeType<StructExpression>(arena, arena);
		StructResolvedType* type = makeType<StructResolvedType>(arena, arena);
		type->decl = decl;
		type->is_compiler_only = true;
		for (u32 i = 0; i < count; ++i) {
			NamedDecl& field = decl->fields.emplace_back();
			field.name = makeStringView(names[i]);
			field.resolved_type = types[i];
			type->field_types.push(types[i]);
		}
		return type;
	}

	ComptimeValue makePersistentValue(Unit& unit, ResolvedType* type, const void* bytes, u32 size) {
		u8* value = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, size, 1));
		copyMemory(value, bytes, size);
		return {ComptimeValue::VALUE, type, value};
	}

	ComptimeValue makeStringValue(Unit& unit, ls_string_view value) {
		ComptimeSliceValue slice{(u8*)value.begin, (i64)(value.end - value.begin)};
		return makePersistentValue(unit, const_u8_slice, &slice, sizeof(slice));
	}

	u32 comptimeSize(const ResolvedType& type) const {
		switch (type.kind) {
			case ResolvedType::META: return sizeof(ResolvedType*);
			case ResolvedType::STRUCT: {
				const StructResolvedType& st = static_cast<const StructResolvedType&>(type);
				u32 size = 0;
				for (ResolvedType* field : st.field_types) size += comptimeSize(*field);
				return size;
			}
			case ResolvedType::ARRAY: return (u32)static_cast<const ArrayResolvedType&>(type).size * comptimeSize(*static_cast<const ArrayResolvedType&>(type).element_type);
			case ResolvedType::SLICE: return sizeof(ComptimeSliceValue);
			default: return typeByteSize(type);
		}
	}

	u32 comptimeFieldOffset(const StructResolvedType& type, u32 index) const {
		u32 offset = 0;
		for (u32 i = 0; i < index; ++i) offset += comptimeSize(*structFieldType(type, i));
		return offset;
	}

	void writeComptimeValue(u8* destination, const ResolvedType& type, const ComptimeValue& value) const {
		if (value.kind == ComptimeValue::TYPE) {
			copyMemory(destination, &value.type, sizeof(value.type));
			return;
		}
		if (value.kind == ComptimeValue::VALUE) {
			copyMemory(destination, value.value, comptimeSize(type));
			return;
		}
	}

	ResolvedType* primitiveType(ResolvedType::Kind kind) const {
		ASSERT(kind >= ResolvedType::VOID && kind < ResolvedType::META);
		return &module.primitives[kind];
	}

	static const char* primitiveTypeName(ResolvedType::Kind kind) {
		for (const TypeKindInfo& info : TYPE_KIND_INFOS) {
			if (info.kind == kind) {
				ASSERT(info.primitive_name);
				return info.primitive_name;
			}
		}
		ASSERT(false);
		return "<invalid>";
	}

	static bool typesEqual(const ResolvedType* a, const ResolvedType* b) {
		if (a == b) return true;
		if (!a || !b) return false;
		if (a->kind != b->kind) return false;
		switch (a->kind) {
			case ResolvedType::META: return true;
			case ResolvedType::FUNCTION: {
				const auto* fa = static_cast<const FunctionResolvedType*>(a);
				const auto* fb = static_cast<const FunctionResolvedType*>(b);
				if (fa->params.size() != fb->params.size()) return false;
				if (!typesEqual(fa->return_type, fb->return_type)) return false;
				for (i32 i = 0; i < fa->params.size(); ++i) {
					if (fa->params[i].is_comptime != fb->params[i].is_comptime) return false;
					if (!typesEqual(fa->params[i].type, fb->params[i].type)) return false;
				}
				return true;
			}
			case ResolvedType::ARRAY: {
				const auto* aa = static_cast<const ArrayResolvedType*>(a);
				const auto* ab = static_cast<const ArrayResolvedType*>(b);
				return aa->size == ab->size && typesEqual(aa->element_type, ab->element_type);
			}
			case ResolvedType::POINTER: {
				const auto* pa = static_cast<const PointerResolvedType*>(a);
				const auto* pb = static_cast<const PointerResolvedType*>(b);
				return pa->is_const == pb->is_const && typesEqual(pa->inner, pb->inner);
			}
			case ResolvedType::SLICE: {
				const auto* sa = static_cast<const SliceResolvedType*>(a);
				const auto* sb = static_cast<const SliceResolvedType*>(b);
				return sa->is_const == sb->is_const && typesEqual(sa->element_type, sb->element_type);
			}
			case ResolvedType::NULLABLE: {
				const auto* na = static_cast<const NullableResolvedType*>(a);
				const auto* nb = static_cast<const NullableResolvedType*>(b);
				return typesEqual(na->inner, nb->inner);
			}
			// STRUCT/ENUM/UNION: canonical instances make a==b definitive.
			default: return false;
		}
	}

	static bool canImplicitlyConvert(const ResolvedType* src, const ResolvedType* dst) {
		if (typesEqual(src, dst)) return true;
		if (!src || !dst) return false;
		if (src->kind == ResolvedType::META && dst->kind == ResolvedType::META) return true;
		if (dst->kind == ResolvedType::UNION) {
			const UnionResolvedType* un = static_cast<const UnionResolvedType*>(dst);
			if (src->kind == ResolvedType::UNION) {
				const UnionResolvedType* source = static_cast<const UnionResolvedType*>(src);
				for (ResolvedType* source_member : source->members) {
					bool found = false;
					for (ResolvedType* member : un->members) {
						if (typesEqual(source_member, member)) { found = true; break; }
					}
					if (!found) return false;
				}
				return true;
			}
			for (ResolvedType* member : un->members) {
				if (typesEqual(src, member)) return true;
			}
			return false;
		}
		// An untyped literal converts to any concrete numeric type (its width is chosen at the
		// materialization point). This is only a safety net; callers materialize first.
		if (src->kind == ResolvedType::UNTYPED_INT) return isNumericType(*dst);
		if (src->kind == ResolvedType::UNTYPED_FLOAT) return isFloatType(*dst);
		if (src->kind == ResolvedType::ARRAY && dst->kind == ResolvedType::SLICE) {
			const auto* arr = static_cast<const ArrayResolvedType*>(src);
			const auto* slice = static_cast<const SliceResolvedType*>(dst);
			return typesEqual(arr->element_type, slice->element_type);
		}
		if (src->kind == ResolvedType::SLICE && dst->kind == ResolvedType::SLICE) {
			const auto* source = static_cast<const SliceResolvedType*>(src);
			const auto* target = static_cast<const SliceResolvedType*>(dst);
			return !source->is_const && target->is_const && typesEqual(source->element_type, target->element_type);
		}
		if (src->kind == ResolvedType::POINTER && dst->kind == ResolvedType::POINTER) {
			const auto* source = static_cast<const PointerResolvedType*>(src);
			const auto* target = static_cast<const PointerResolvedType*>(dst);
			return !source->is_const && target->is_const && typesEqual(source->inner, target->inner);
		}
		if (dst->kind == ResolvedType::NULLABLE) {
			const auto* nb = static_cast<const NullableResolvedType*>(dst);
			return typesEqual(src, nb->inner);
		}
		return false;
	}

	static TemplateBinding* findTemplateBinding(TemplateBindings* bindings, ls_string_view name) {
		if (!bindings) return nullptr;
		for (TemplateBinding& binding : bindings->values) {
			if (equalStrings(binding.name, name)) return &binding;
		}
		return nullptr;
	}

	static const TemplateBinding* findTemplateBinding(const TemplateBindings* bindings, ls_string_view name) {
		return findTemplateBinding(const_cast<TemplateBindings*>(bindings), name);
	}

	// Floats cannot be compared as bytes: NaN is not equal to itself and +0.0
	// equals -0.0 despite differing bit patterns. Every other element kind this
	// accepts is an integral scalar, so its bytes carry no padding.
	bool comptimeSlicePayloadEqual(const u8* lhs, const u8* rhs, const ResolvedType& element, i64 count) const {
		if (element.kind == ResolvedType::F32) {
			for (i64 i = 0; i < count; ++i) {
				float l, r;
				memcpy(&l, lhs + i * sizeof(float), sizeof(l));
				memcpy(&r, rhs + i * sizeof(float), sizeof(r));
				if (!(l == r)) return false;
			}
			return true;
		}
		if (element.kind == ResolvedType::F64) {
			for (i64 i = 0; i < count; ++i) {
				double l, r;
				memcpy(&l, lhs + i * sizeof(double), sizeof(l));
				memcpy(&r, rhs + i * sizeof(double), sizeof(r));
				if (!(l == r)) return false;
			}
			return true;
		}
		return compareMemory(lhs, rhs, (usize)count * comptimeSize(element)) == 0;
	}

	bool comptimeValuesEqual(const ComptimeValue& a, const ComptimeValue& b) {
		if (a.kind != b.kind) return false;
		if (a.kind == ComptimeValue::VALUE && a.type && b.type && isNumericOrUntyped(*a.type) && isNumericOrUntyped(*b.type)) {
			return compareComptimeNumeric(a, b) == 0;
		}
		// `[]T` and `[]const T` hold the same values, so slice equality compares
		// them; every other kind requires identical types.
		const bool slices_comparable = sliceTypesComparable(a.type, b.type);
		if (!slices_comparable && !typesEqual(a.type, b.type)) return false;
		if (a.kind == ComptimeValue::TYPE) return true;
		if (a.kind != ComptimeValue::VALUE || !a.type) return false;

		if (slices_comparable || typesEqual(a.type, const_u8_slice)) {
			ComptimeSliceValue lhs;
			ComptimeSliceValue rhs;
			memcpy(&lhs, a.value, sizeof(lhs));
			memcpy(&rhs, b.value, sizeof(rhs));
			if (lhs.count != rhs.count) return false;
			if (lhs.count == 0) return true;
			const SliceResolvedType& slice = static_cast<const SliceResolvedType&>(*a.type);
			return comptimeSlicePayloadEqual(lhs.data, rhs.data, *slice.element_type, lhs.count);
		}

		u32 size = comptimeSize(*a.type);
		return memcmp(a.value, b.value, size) == 0;
	}

	// we intern unions type to make sure A | B is the same type as B | A
	UnionResolvedType* getUnionType(ExpArray<ResolvedType*>& members) {
		ExpArray<ResolvedType*> unique_members(module.arena);
		// flatten nested unions and remove duplicates
		for (i32 i = 0, count = members.size(); i < count; ++i) {
			ResolvedType* member = members[i];
			if (member->kind == ResolvedType::UNION) {
				for (ResolvedType* nested_member : static_cast<UnionResolvedType*>(member)->members) {
					bool duplicate = false;
					for (ResolvedType* existing : unique_members) if (typesEqual(existing, nested_member)) { duplicate = true; break; }
					if (!duplicate) unique_members.push(nested_member);
				}
			}
			else {
				bool duplicate = false;
				for (ResolvedType* existing : unique_members) if (typesEqual(existing, member)) { duplicate = true; break; }
				if (!duplicate) unique_members.push(member);
			}
		}

		// TODO optimize
		for (UnionResolvedType* existing : module.union_types) {
			if (existing->members.size() != unique_members.size()) continue;
			bool equal = true;
			for (ResolvedType* member : unique_members) {
				bool found = false;
				for (ResolvedType* existing_member : existing->members) if (typesEqual(member, existing_member)) { found = true; break; }
				if (!found) { equal = false; break; }
			}
			if (equal) return existing;
		}

		UnionResolvedType* result = makeType<UnionResolvedType>(module.arena, module.arena);
		for (ResolvedType* member : unique_members) result->members.push(member);
		module.union_types.push(result);
		return result;
	}

	bool bindTemplateArg(TemplateBindings& bindings, ls_string_view name, const ComptimeValue& arg) {
		if (TemplateBinding* existing = findTemplateBinding(&bindings, name)) {
			return comptimeValuesEqual(existing->arg, arg);
		}
		TemplateBinding& binding = bindings.values.emplace_back();
		binding.name = name;
		binding.arg = arg;
		return true;
	}

	// Unwrap MetaType to get the actual type for value use; pass through for value symbols.
	static ResolvedType* unwrapMeta(ResolvedType* t) { return t && t->kind == ResolvedType::META ? static_cast<MetaType*>(t)->inner : t; }

	// Literal type-checking resolves against the non-nullable destination: a hint of
	// `i32?` still constrains an integer literal as an i32.
	static ResolvedType* unwrapNullable(ResolvedType* t) { return (t && t->kind == ResolvedType::NULLABLE) ? static_cast<NullableResolvedType*>(t)->inner : t; }

	struct SemanticLocalBinding {
		ls_string_view name = {};
		ResolvedType* type = nullptr;
		bool is_immutable = false;
		bool is_comptime = false;
		ComptimeValue comptime_value;
		// Slot of the underlying declaration (frame slot for locals, the symbol's
		// global slot when the binding narrows a global).
		StorageSlot* slot = nullptr;
	};

	struct FunctionCheckContext {
		explicit FunctionCheckContext(ls_arena& arena)
			: locals(arena)
			, scope_marks(arena)
			, loop_labels(arena)
			, label_names(arena)
			, declared_loop_labels(arena)
			, declared_loop_kinds(arena) {}

		ExpArray<SemanticLocalBinding> locals;
		ExpArray<i32> scope_marks;
		ExpArray<ls_string_view> loop_labels;
		ExpArray<ls_string_view> label_names;
		ExpArray<ls_string_view> declared_loop_labels;
		ExpArray<Statement::Kind> declared_loop_kinds;
		i32 in_defer = 0;
	};

	Expression::EvalStage combineEvalStages(const Expression& lhs, const Expression& rhs) const {
		if (lhs.eval_stage == Expression::COMPTIME_ONLY || rhs.eval_stage == Expression::COMPTIME_ONLY) return Expression::COMPTIME_ONLY;
		return lhs.eval_stage == Expression::COMPTIME_VALUE && rhs.eval_stage == Expression::COMPTIME_VALUE
			? Expression::COMPTIME_VALUE
			: Expression::RUNTIME;
	}

	bool requireMaterializable(const Expression& expr, const char* context) {
		if (expr.eval_stage != Expression::COMPTIME_ONLY) return true;
		errorLine(expr.token, "Compile-time-only value cannot be used as ", context);
		return false;
	}

	Expression::EvalStage comptimeStageForType(const ResolvedType* type) const {
		return type && !isRuntimeMaterializable(*type)
			? Expression::COMPTIME_ONLY
			: Expression::COMPTIME_VALUE;
	}

	bool isRuntimeMaterializable(const ResolvedType& type) const {
		switch (type.kind) {
			case ResolvedType::META: return false;
			case ResolvedType::ARRAY: return isRuntimeMaterializable(*static_cast<const ArrayResolvedType&>(type).element_type);
			case ResolvedType::SLICE: return isRuntimeMaterializable(*static_cast<const SliceResolvedType&>(type).element_type);
			case ResolvedType::STRUCT: {
				const StructResolvedType& st = static_cast<const StructResolvedType&>(type);
				for (i32 i = 0; i < st.field_types.size(); ++i) if (!isRuntimeMaterializable(*st.field_types[i])) return false;
				return true;
			}
			case ResolvedType::NULLABLE: return isRuntimeMaterializable(*static_cast<const NullableResolvedType&>(type).inner);
			case ResolvedType::UNION: {
				for (ResolvedType* member : static_cast<const UnionResolvedType&>(type).members) if (!isRuntimeMaterializable(*member)) return false;
				return true;
			}
			default: return true;
		}
	}

	void error(i64 value) {
		char buffer[32];
		char* end = buffer + sizeof(buffer);
		char* cursor = end;
		u64 magnitude = value < 0 ? (u64)(-(value + 1)) + 1u : (u64)value;
		do {
			*--cursor = (char)('0' + magnitude % 10u);
			magnitude /= 10u;
		} while (magnitude);
		if (value < 0) *--cursor = '-';
		error(ls_string_view{cursor, end});
	}

	void error(ResolvedType* type) { error(static_cast<const ResolvedType*>(type)); }

	void error(const ResolvedType* type) {
		if (!type) {
			error("<unresolved>");
			return;
		}
		switch (type->kind) {
			case ResolvedType::UNTYPED_INT: error("{untyped integer}"); return;
			case ResolvedType::UNTYPED_FLOAT: error("{untyped float}"); return;
			case ResolvedType::META: error("type"); return;
			case ResolvedType::ENUM: {
				const EnumResolvedType* en = static_cast<const EnumResolvedType*>(type);
				error(empty(en->decl->cached_name) ? makeStringView("<anonymous>") : en->decl->cached_name);
				return;
			}
			case ResolvedType::STRUCT: {
				const StructResolvedType* st = static_cast<const StructResolvedType*>(type);
				error(empty(st->decl->cached_name) ? makeStringView("<anonymous>") : st->decl->cached_name);
				return;
			}
			case ResolvedType::FUNCTION: {
				const FunctionResolvedType* fn = static_cast<const FunctionResolvedType*>(type);
				error("fn(");
				for (i32 i = 0; i < fn->params.size(); ++i) {
					if (i > 0) error(", ");
					error(fn->params[i].type);
				}
				error(") : ");
				error(fn->return_type);
				return;
			}
			case ResolvedType::ARRAY: {
				const ArrayResolvedType* array = static_cast<const ArrayResolvedType*>(type);
				error(array->element_type);
				error("[");
				error(array->size);
				error("]");
				return;
			}
			case ResolvedType::SLICE:
				error(static_cast<const SliceResolvedType*>(type)->element_type);
				error("[]");
				return;
			case ResolvedType::NULLABLE:
				error("?");
				error(static_cast<const NullableResolvedType*>(type)->inner);
				return;
			case ResolvedType::UNION: {
				const UnionResolvedType* un = static_cast<const UnionResolvedType*>(type);
				for (i32 i = 0; i < un->members.size(); ++i) {
					if (i > 0) error(" | ");
					error(un->members[i]);
				}
				return;
			}
			default:
				if (type->kind >= ResolvedType::VOID && type->kind <= ResolvedType::BYTE) {
					error(primitiveTypeName(type->kind));
					return;
				}
				error("<invalid>");
				return;
		}
	}

	template <typename T> void error(T&& arg) {
		if (suppress_errors == 0) {
			error_stream.print(static_cast<T&&>(arg));
		}
	}

	template <typename... Args> void errorLine(Token token, Args&&... args) {
		if (suppress_errors != 0) return;
		if (!empty(token.source_name)) {
			error_stream.print(token.source_name);
			error_stream.print(": ");
		}
		if (token.line > 0) {
			error_stream.print("line ");
			error_stream.print(token.line);
			error_stream.print(": ");
		}
		int dummy[] = {(error(static_cast<Args&&>(args)), 0)...};
		(void)dummy;
		error("\n");
	}

	enum class LookupPolicy { NameOnly, Checked };

	// Result of a symbol lookup. `check_failed` is set only under LookupPolicy::Checked
	// when the symbol was found but its checkSymbol() failed - distinguishing a genuine
	// declaration error from an undeclared name (both used to collapse to nullptr).
	struct SymbolRef {
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		bool ambiguous = false;
		bool check_failed = false;
		explicit operator bool() const { return symbol && !ambiguous && !check_failed; }
	};

	// `sizeof(T)` / `alignof(T)`. Rejects a value-denoting name (the operand must be a type).
	bool resolveSizeofValue(Unit& unit, SizeofExpression& sz, FunctionCheckContext* ctx = nullptr, TemplateBindings* bindings = nullptr) {
		ComptimeValue operand = evalComptime(unit, *sz.type_expr, ctx, bindings);
		if (!operand) return false;
		ResolvedType* measured = asType(operand, sz.type_expr->token);
		if (!measured) return false;
		// Preserve the concrete operand for backends that need to emit a native
		// sizeof/alignof expression instead of using the folded integer value.
		sz.type_expr->resolved_type = measured;

		const i64 size = typeByteSize(*measured);
		i64 align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
		if (measured->kind == ResolvedType::UNION) {
			align = 4;
			for (ResolvedType* member : static_cast<UnionResolvedType*>(measured)->members) {
				const i64 member_size = typeByteSize(*member);
				const i64 member_align = member_size >= 8 ? 8 : member_size >= 4 ? 4 : member_size >= 2 ? 2 : 1;
				if (member_align > align) align = member_align;
			}
		}
		sz.value = (u64)sz.is_align ? align : size;
		return true;
	}


	// TODO Legacy wrappers for backwards compatibility, inline and remove
	bool evalComptimeIntValue(Unit& unit, Expression* expr, i64& out, TemplateBindings* bindings = nullptr, ComptimeFrame* frame = nullptr) {
		ComptimeValue t = evalComptime(unit, *expr, nullptr, bindings, frame);
		if (!t) return false;
		if (t.kind != ComptimeValue::VALUE) {
			errorLine(expr->token, "Expected compile-time integer value, got type");
			return false;
		}
		if (!isIntegerOrUntyped(*t.type)) {
			errorLine(expr->token, "Expected compile-time integer value, got ", t.type);
			return false;
		}
		const u32 size = typeByteSize(*t.type);
		out = comptimeNumericToI64(t.value, t.type->kind);
		comptime_stack_ptr -= size;
		return true;
	}

	ResolvedType* checkExprForTarget(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* target) {
		ResolvedType* t = checkExpr(unit, ctx, expr, target);
		if (!t) return nullptr;
		if (target && target->kind == ResolvedType::SLICE && !static_cast<SliceResolvedType*>(target)->is_const && t->kind == ResolvedType::ARRAY) {
			bool writable = false;
			++suppress_errors;
			ResolvedType* storage_type = checkAssignableExpr(unit, ctx, expr, writable);
			--suppress_errors;
			if (storage_type && !writable) {
				errorLine(expr.token, "Cannot create a mutable slice from immutable storage");
				return nullptr;
			}
		}
		return makeConcrete(expr, target);
	}

	// Unified symbol resolution. A bare name is ambiguous when it matches multiple
	// declarations across the current module and unaliased imports. A qualified
	// lookup is confined to the aliased unit.
	SymbolRef resolveSymbol(Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy, ResolvedType* first_arg_type = nullptr) {
		SymbolRef ref;
		if (!empty(qualifier)) {
			if (Unit* owner = findImportedUnitByAlias(unit, qualifier)) {
				if (Symbol* candidate = findSymbol(*owner, name)) ref = {owner, candidate};
			}
		} else {
			SymbolRef namespaced;
			if (first_arg_type) {
				if (Unit* namespace_unit = findTypeNamespaceUnit(*first_arg_type)) {
					if (Symbol* candidate = findSymbol(*namespace_unit, name)) {
						namespaced = {namespace_unit, candidate};
					}
				}
			}

			Symbol* local = findSymbol(unit, name);
			SymbolRef imported;
			for (const Import& imp : unit.imports) {
				if (!empty(imp.alias)) continue;
				ASSERT(imp.unit);
				Unit* imported_unit = imp.unit;
				if (Symbol* candidate = findSymbol(*imported_unit, name)) {
					if (imported.symbol) {
						imported = {nullptr, nullptr, true};
						break;
					}
					imported = {imported_unit, candidate};
				}
			}
			bool ambiguous = imported.ambiguous || (local && imported.symbol);
			// ADL: namespaced is only used when no local or unaliased-import match exists
			if (local) {
				ref.owner = &unit;
				ref.symbol = local;
				ref.ambiguous = ambiguous;
			} else if (imported.symbol) {
				ref = imported;
				ref.ambiguous = ambiguous;
			} else if (namespaced.symbol) {
				ref = namespaced;
			}
		}
		if (!ref.symbol) return ref;

		if (policy == LookupPolicy::Checked && checkSymbol(*ref.owner, *ref.symbol) == LS_RESULT_FAILURE) {
			ref.check_failed = true;
		}
		return ref;
	}

	Expression* makeComptimeValueExpression(Unit& unit, const ComptimeValue& arg) {
		switch (arg.kind) {
			case ComptimeValue::FAILURE: return nullptr;
			case ComptimeValue::TYPE: {
				ResolvedTypeExpression* expr = makeType<ResolvedTypeExpression>(unit.arena);
				expr->resolved_type = arg.type;
				return expr;
			}
			case ComptimeValue::VALUE: {
				if (!arg.type || !arg.value) return nullptr;
				switch (arg.type->kind) {
					case ResolvedType::BOOL: {
						bool value;
						memcpy(&value, arg.value, sizeof(value));
						BoolLiteralExpression* expr = makeType<BoolLiteralExpression>(unit.arena, value);
						expr->resolved_type = arg.type;
						return expr;
					}
					case ResolvedType::SLICE: {
						if (!typesEqual(arg.type, const_u8_slice)) return nullptr;
						StringLiteralExpression* expr = makeType<StringLiteralExpression>(unit.arena);
						ComptimeSliceValue value;
						memcpy(&value, arg.value, sizeof(value));
						expr->value = {(const char*)value.data, (const char*)value.data + value.count};
						expr->resolved_type = arg.type;
						return expr;
					}
					case ResolvedType::F32:
					case ResolvedType::F64: {
						FloatLiteralExpression* expr = makeType<FloatLiteralExpression>(unit.arena);
						expr->value = comptimeNumericToF64(arg.value, arg.type->kind);
						expr->resolved_type = arg.type;
						return expr;
					}
					case ResolvedType::ENUM:
					default:
						if (!isIntegerType(*arg.type) && arg.type->kind != ResolvedType::ENUM) return nullptr;
						if (arg.type->kind >= ResolvedType::U8 && arg.type->kind <= ResolvedType::U64) {
							IntLiteralExpression* expr = makeType<IntLiteralExpression>(unit.arena);
							expr->value = comptimeNumericToU64(arg.value, arg.type->kind);
							expr->resolved_type = arg.type;
							return expr;
						}
						const i64 value = comptimeNumericToI64(arg.value, arg.type->kind);
						if (value >= 0) {
							IntLiteralExpression* expr = makeType<IntLiteralExpression>(unit.arena);
							expr->value = (u64)value;
							expr->resolved_type = arg.type;
							return expr;
						}
						UnaryExpression* neg = makeType<UnaryExpression>(unit.arena);
						IntLiteralExpression* magnitude = makeType<IntLiteralExpression>(unit.arena);
						magnitude->value = (u64)(-(value + 1)) + 1u;
						neg->expression = magnitude;
						neg->op = Token::MINUS;
						neg->resolved_type = arg.type;
						return neg;
				}
			}
		}

		ASSERT(false);
		return nullptr;
	}

	Expression* cloneExpression(Unit& unit, Expression* src, const TemplateBindings* bindings) {
		if (!src) return nullptr;
		Expression* out = nullptr;
		switch (src->kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression* s = static_cast<IdentifierExpression*>(src);
				if (const TemplateBinding* binding = findTemplateBinding(bindings, s->name)) {
					out = makeComptimeValueExpression(unit, binding->arg);
					break;
				}
				IdentifierExpression* id = makeType<IdentifierExpression>(unit.arena);
				id->name = s->name;
				out = id;
				break;
			}
			case Expression::UNION_TYPE: {
				UnionTypeExpression* s = static_cast<UnionTypeExpression*>(src);
				UnionTypeExpression* un = makeType<UnionTypeExpression>(unit.arena, unit.arena);
				for (Expression* member : s->members) un->members.push(cloneExpression(unit, member, bindings));
				out = un;
				break;
			}
			case Expression::INT_LITERAL: {
				IntLiteralExpression* s = static_cast<IntLiteralExpression*>(src);
				IntLiteralExpression* lit = makeType<IntLiteralExpression>(unit.arena);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::FLOAT_LITERAL: {
				FloatLiteralExpression* s = static_cast<FloatLiteralExpression*>(src);
				FloatLiteralExpression* lit = makeType<FloatLiteralExpression>(unit.arena);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::BOOL_LITERAL: out = makeType<BoolLiteralExpression>(unit.arena, static_cast<BoolLiteralExpression*>(src)->value); break;
			case Expression::STRING_LITERAL: {
				StringLiteralExpression* s = static_cast<StringLiteralExpression*>(src);
				StringLiteralExpression* lit = makeType<StringLiteralExpression>(unit.arena);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::NULL_LITERAL: out = makeType<NullLiteralExpression>(unit.arena); break;
			case Expression::UNDEFINED: out = makeType<UndefinedExpression>(unit.arena); break;
			case Expression::TYPEOF: {
				TypeofExpression* s = static_cast<TypeofExpression*>(src);
				TypeofExpression* typeof_expr = makeType<TypeofExpression>(unit.arena);
				typeof_expr->operand = cloneExpression(unit, s->operand, bindings);
				out = typeof_expr;
				break;
			}
			case Expression::TYPE_LITERAL: out = makeType<TypeLiteralExpression>(unit.arena, static_cast<TypeLiteralExpression*>(src)->type); break;
			case Expression::GENERIC_IDENTIFIER: {
				GenericIdentifierExpression* s = static_cast<GenericIdentifierExpression*>(src);
				if (const TemplateBinding* binding = findTemplateBinding(bindings, s->name)) {
					out = makeComptimeValueExpression(unit, binding->arg);
					break;
				}
				GenericIdentifierExpression* generic = makeType<GenericIdentifierExpression>(unit.arena);
				generic->name = s->name;
				out = generic;
				break;
			}
			case Expression::RESOLVED_TYPE: {
				ResolvedTypeExpression* type = makeType<ResolvedTypeExpression>(unit.arena);
				type->resolved_type = static_cast<ResolvedTypeExpression*>(src)->resolved_type;
				out = type;
				break;
			}
			case Expression::ARRAY_TYPE: {
				ArrayTypeExpression* s = static_cast<ArrayTypeExpression*>(src);
				ArrayTypeExpression* arr = makeType<ArrayTypeExpression>(unit.arena);
				arr->size = cloneExpression(unit, s->size, bindings);
				arr->element_type = cloneExpression(unit, s->element_type, bindings);
				out = arr;
				break;
			}
			case Expression::POINTER_TYPE: {
				PointerTypeExpression* s = static_cast<PointerTypeExpression*>(src);
				PointerTypeExpression* ptr = makeType<PointerTypeExpression>(unit.arena);
				ptr->inner = cloneExpression(unit, s->inner, bindings);
				ptr->is_const = s->is_const;
				out = ptr;
				break;
			}
			case Expression::DEREFERENCE: {
				DereferenceExpression* deref = makeType<DereferenceExpression>(unit.arena);
				deref->subject = cloneExpression(unit, static_cast<DereferenceExpression*>(src)->subject, bindings);
				out = deref;
				break;
			}
			case Expression::SLICE_TYPE: {
				SliceTypeExpression* sl = makeType<SliceTypeExpression>(unit.arena);
				sl->element_type = cloneExpression(unit, static_cast<SliceTypeExpression*>(src)->element_type, bindings);
				sl->is_const = static_cast<SliceTypeExpression*>(src)->is_const;
				out = sl;
				break;
			}
			case Expression::NULLABLE_TYPE: {
				NullableTypeExpression* nullable = makeType<NullableTypeExpression>(unit.arena);
				nullable->inner = cloneExpression(unit, static_cast<NullableTypeExpression*>(src)->inner, bindings);
				out = nullable;
				break;
			}
			case Expression::FUNCTION_TYPE: {
				FunctionTypeExpression* s = static_cast<FunctionTypeExpression*>(src);
				FunctionTypeExpression* fn = makeType<FunctionTypeExpression>(unit.arena, unit.arena);
				for (FunctionTypeParam& param : s->params) {
					FunctionTypeParam& clone = fn->params.emplace_back();
					clone.name = param.name;
					clone.is_comptime = param.is_comptime;
					clone.type_expr = cloneExpression(unit, param.type_expr, bindings);
				}
				fn->return_type = cloneExpression(unit, s->return_type, bindings);
				out = fn;
				break;
			}
			case Expression::SIZEOF: {
				SizeofExpression* s = static_cast<SizeofExpression*>(src);
				SizeofExpression* sz = makeType<SizeofExpression>(unit.arena);
				sz->type_expr = cloneExpression(unit, s->type_expr, bindings);
				sz->is_align = s->is_align;
				out = sz;
				break;
			}
			case Expression::CALL: {
				CallExpression* s = static_cast<CallExpression*>(src);
				CallExpression* call = makeType<CallExpression>(unit.arena, unit.arena);
				call->callee = cloneExpression(unit, s->callee, bindings);
				for (Expression* arg : s->args) call->args.push(cloneExpression(unit, arg, bindings));
				out = call;
				break;
			}
			case Expression::UNARY: {
				UnaryExpression* s = static_cast<UnaryExpression*>(src);
				UnaryExpression* un = makeType<UnaryExpression>(unit.arena);
				un->op = s->op;
				un->expression = cloneExpression(unit, s->expression, bindings);
				out = un;
				break;
			}
			case Expression::BINARY: {
				BinaryExpression* s = static_cast<BinaryExpression*>(src);
				BinaryExpression* bin = makeType<BinaryExpression>(unit.arena);
				bin->op = s->op;
				bin->lhs = cloneExpression(unit, s->lhs, bindings);
				bin->rhs = cloneExpression(unit, s->rhs, bindings);
				out = bin;
				break;
			}
			case Expression::TERNARY: {
				TernaryExpression* s = static_cast<TernaryExpression*>(src);
				TernaryExpression* tern = makeType<TernaryExpression>(unit.arena);
				tern->condition = cloneExpression(unit, s->condition, bindings);
				tern->true_expr = cloneExpression(unit, s->true_expr, bindings);
				tern->false_expr = cloneExpression(unit, s->false_expr, bindings);
				out = tern;
				break;
			}
			case Expression::CAST: {
				CastExpression* s = static_cast<CastExpression*>(src);
				CastExpression* cast = makeType<CastExpression>(unit.arena);
				cast->expression = cloneExpression(unit, s->expression, bindings);
				cast->type_expr = cloneExpression(unit, s->type_expr, bindings);
				out = cast;
				break;
			}
			case Expression::MEMBER: {
				MemberExpression* s = static_cast<MemberExpression*>(src);
				MemberExpression* mem = makeType<MemberExpression>(unit.arena);
				// In a qualified type name (`lib.Foo`), the left-hand identifier is
				// an import alias, not a template binding. Keep it intact so a generic
				// parameter with the same spelling cannot rewrite the qualifier.
				if (s->expression && s->expression->kind == Expression::IDENTIFIER
					&& findImportedUnitByAlias(unit, static_cast<IdentifierExpression*>(s->expression)->name)) {
					IdentifierExpression* id = makeType<IdentifierExpression>(unit.arena);
					id->name = static_cast<IdentifierExpression*>(s->expression)->name;
					id->token = s->expression->token;
					mem->expression = id;
				} else {
					mem->expression = cloneExpression(unit, s->expression, bindings);
				}
				mem->name = s->name;
				out = mem;
				break;
			}
			case Expression::TYPE_MEMBER: {
				TypeMemberExpression* s = static_cast<TypeMemberExpression*>(src);
				TypeMemberExpression* mem = makeType<TypeMemberExpression>(unit.arena);
				mem->expression = cloneExpression(unit, s->expression, bindings);
				mem->kind = s->kind;
				mem->comptime_string = s->comptime_string;
				mem->reflected_type = s->reflected_type;
				out = mem;
				break;
			}
			case Expression::BRACKET: {
				BracketExpression* s = static_cast<BracketExpression*>(src);
				BracketExpression* br = makeType<BracketExpression>(unit.arena, unit.arena);
				br->base = cloneExpression(unit, s->base, bindings);
				for (Expression* arg : s->args) br->args.push(cloneExpression(unit, arg, bindings));
				br->struct_field_name = s->struct_field_name;
				out = br;
				break;
			}
			case Expression::SLICE: {
				SliceExpression* s = static_cast<SliceExpression*>(src);
				SliceExpression* sl = makeType<SliceExpression>(unit.arena);
				sl->base = cloneExpression(unit, s->base, bindings);
				sl->begin = cloneExpression(unit, s->begin, bindings);
				sl->end = cloneExpression(unit, s->end, bindings);
				out = sl;
				break;
			}
			case Expression::STRUCT_LITERAL: {
				StructLiteralExpression* s = static_cast<StructLiteralExpression*>(src);
				StructLiteralExpression* lit = makeType<StructLiteralExpression>(unit.arena, unit.arena);
				lit->type = cloneExpression(unit, s->type, bindings);
				for (Expression* value : s->values) lit->values.push(cloneExpression(unit, value, bindings));
				out = lit;
				break;
			}
			case Expression::ARRAY_LITERAL: {
				ArrayLiteralExpression* s = static_cast<ArrayLiteralExpression*>(src);
				ArrayLiteralExpression* lit = makeType<ArrayLiteralExpression>(unit.arena, unit.arena);
				for (Expression* value : s->values) lit->values.push(cloneExpression(unit, value, bindings));
				out = lit;
				break;
			}
			case Expression::STRUCT: {
				StructExpression* s = static_cast<StructExpression*>(src);
				StructExpression* st = makeType<StructExpression>(unit.arena, unit.arena);
				for (NamedDecl& field : s->fields) {
					NamedDecl& clone = st->fields.emplace_back();
					clone.name = field.name;
					clone.type_expr = cloneExpression(unit, field.type_expr, bindings);
				}
				out = st;
				break;
			}
			default: out = makeType<Expression>(unit.arena, src->kind); break;
		}
		out->token = src->token;
		out->parenthesized = src->parenthesized;
		return out;
	}

	Statement* cloneStatement(Unit& unit, Statement* src, const TemplateBindings* bindings) {
		if (!src) return nullptr;
		Statement* out = nullptr;
		switch (src->kind) {
			case Statement::BLOCK: {
				BlockStatement* s = static_cast<BlockStatement*>(src);
				BlockStatement* block = makeType<BlockStatement>(unit.arena, unit.arena);
				for (Statement* st : s->statements) block->statements.push(cloneStatement(unit, st, bindings));
				out = block;
				break;
			}
			case Statement::EXPRESSION: {
				ExpressionStatement* s = static_cast<ExpressionStatement*>(src);
				ExpressionStatement* st = makeType<ExpressionStatement>(unit.arena);
				st->expression = cloneExpression(unit, s->expression, bindings);
				out = st;
				break;
			}
			case Statement::RETURN: {
				ReturnStatement* s = static_cast<ReturnStatement*>(src);
				ReturnStatement* st = makeType<ReturnStatement>(unit.arena);
				st->expression = cloneExpression(unit, s->expression, bindings);
				out = st;
				break;
			}
			case Statement::VAR_DECL: {
				VarDeclStatement* s = static_cast<VarDeclStatement*>(src);
				VarDeclStatement* st = makeType<VarDeclStatement>(unit.arena);
				st->name = s->name;
				st->type_expr = cloneExpression(unit, s->type_expr, bindings);
				st->expression = cloneExpression(unit, s->expression, bindings);
				st->else_return = s->else_return;
				st->else_return_zero = s->else_return_zero;
				st->else_return_target_mask = s->else_return_target_mask;
				st->is_immutable = s->is_immutable;
				st->is_comptime = s->is_comptime;
				out = st;
				break;
			}
			case Statement::ASSIGN: {
				AssignStatement* s = static_cast<AssignStatement*>(src);
				AssignStatement* st = makeType<AssignStatement>(unit.arena);
				st->lhs = cloneExpression(unit, s->lhs, bindings);
				st->rhs = cloneExpression(unit, s->rhs, bindings);
				st->op = s->op;
				out = st;
				break;
			}
			case Statement::IF: {
				IfStatement* s = static_cast<IfStatement*>(src);
				IfStatement* st = makeType<IfStatement>(unit.arena);
				st->condition = cloneExpression(unit, s->condition, bindings);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body, bindings));
				st->else_branch = cloneStatement(unit, s->else_branch, bindings);
				st->comptime_known = false;
				st->comptime_value = false;
				out = st;
				break;
			}
			case Statement::MATCH: {
				MatchStatement* s = static_cast<MatchStatement*>(src);
				MatchStatement* st = makeType<MatchStatement>(unit.arena, unit.arena);
				st->subject = cloneExpression(unit, s->subject, bindings);
				st->comptime_known = false;
				st->comptime_arm = -1;
				for (MatchArm& src_arm : s->arms) {
					MatchArm& dst_arm = st->arms.emplace_back(unit.arena);
					dst_arm.is_fallback = src_arm.is_fallback;
					for (MatchPattern& src_pattern : src_arm.patterns) {
						MatchPattern& dst_pattern = dst_arm.patterns.emplace_back();
						dst_pattern.begin = cloneExpression(unit, src_pattern.begin, bindings);
						dst_pattern.end = cloneExpression(unit, src_pattern.end, bindings);
					}
					dst_arm.body = static_cast<BlockStatement*>(cloneStatement(unit, src_arm.body, bindings));
				}
				out = st;
				break;
			}
			case Statement::WHILE: {
				WhileStatement* s = static_cast<WhileStatement*>(src);
				WhileStatement* st = makeType<WhileStatement>(unit.arena);
				st->condition = cloneExpression(unit, s->condition, bindings);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body, bindings));
				out = st;
				break;
			}
			case Statement::FOR: {
				ForStatement* s = static_cast<ForStatement*>(src);
				ForStatement* st = makeType<ForStatement>(unit.arena);
				st->key_var = s->key_var;
				st->value_var = s->value_var;
				st->is_key_value = s->is_key_value;
				st->is_unroll = s->is_unroll;
				st->unroll_begin = s->unroll_begin;
				st->unroll_end = s->unroll_end;
				st->unroll_elements = s->unroll_elements;
				st->begin = cloneExpression(unit, s->begin, bindings);
				st->end = cloneExpression(unit, s->end, bindings);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body, bindings));
				out = st;
				break;
			}
			case Statement::BREAK: {
				BreakStatement* s = static_cast<BreakStatement*>(src);
				BreakStatement* st = makeType<BreakStatement>(unit.arena);
				st->label = s->label;
				out = st;
				break;
			}
			case Statement::CONTINUE: {
				ContinueStatement* s = static_cast<ContinueStatement*>(src);
				ContinueStatement* st = makeType<ContinueStatement>(unit.arena);
				st->label = s->label;
				out = st;
				break;
			}
			case Statement::DEFER: {
				DeferStatement* s = static_cast<DeferStatement*>(src);
				DeferStatement* st = makeType<DeferStatement>(unit.arena);
				st->statement = cloneStatement(unit, s->statement, bindings);
				out = st;
				break;
			}
			case Statement::LABEL: {
				LabelStatement* s = static_cast<LabelStatement*>(src);
				LabelStatement* st = makeType<LabelStatement>(unit.arena);
				st->name = s->name;
				st->statement = cloneStatement(unit, s->statement, bindings);
				out = st;
				break;
			}
			default: return src;
		}
		out->token = src->token;
		return out;
	}

	// A type produced by a `: type` function has no declaration of its own, so it is named
	// after the call that produced it. Records the type on the instance as well, so
	// template argument inference can match a factory call pattern against it.
	void recordFactoryResult(Unit& unit, FunctionExpression& decl, FunctionExpression& instance,
		const ExpArray<ComptimeValue>& args, const ComptimeValue& result)
	{
		if (result.kind != ComptimeValue::TYPE || !result.type) return;
		for (TemplateFunctionInstance& i : decl.template_function_instances) {
			if (i.instance != &instance) continue;
			i.produced_type = result.type;
			break;
		}
		if (result.type->kind != ResolvedType::STRUCT) return;
		StructResolvedType& st = *static_cast<StructResolvedType*>(result.type);
		if (!st.decl || !empty(st.decl->cached_name)) return;
		st.decl->cached_name = factoryTypeName(unit, decl, args);
	}

	// The common numeric type two operands must share, or null if they are not numerically
	// compatible. UNTYPED_INT adopts any concrete numeric partner; two untyped ints stay
	// untyped (resolved to a default later). Callers materialize the operands afterwards.
	ResolvedType* unifyNumeric(ResolvedType& a, ResolvedType& b) const {
		const bool ui_a = a.kind == ResolvedType::UNTYPED_INT, ui_b = b.kind == ResolvedType::UNTYPED_INT;
		const bool uf_a = a.kind == ResolvedType::UNTYPED_FLOAT, uf_b = b.kind == ResolvedType::UNTYPED_FLOAT;
		// Two untyped operands: float wins over int (1 + 1.5 -> untyped float).
		if ((ui_a || uf_a) && (ui_b || uf_b)) return (uf_a || uf_b) ? primitiveType(ResolvedType::UNTYPED_FLOAT) : primitiveType(ResolvedType::UNTYPED_INT);
		// One untyped, one concrete: untyped adopts the concrete type if compatible.
		if (ui_a) return isNumericType(b) ? &b : nullptr;
		if (ui_b) return isNumericType(a) ? &a : nullptr;
		if (uf_a) return isFloatType(b) ? &b : nullptr;
		if (uf_b) return isFloatType(a) ? &a : nullptr;
		return (isNumericType(a) && typesEqual(&a, &b)) ? &a : nullptr;
	}

	static bool intLiteralFitsType(u64 value, ResolvedType::Kind kind) {
		switch (kind) {
			case ResolvedType::I8: return value <= 127u;
			case ResolvedType::U8: return value <= 255u;
			case ResolvedType::I16: return value <= 32767u;
			case ResolvedType::U16: return value <= 65535u;
			case ResolvedType::I32: return value <= 2147483647u;
			case ResolvedType::U32: return value <= 4294967295u;
			case ResolvedType::I64:
			case ResolvedType::ISIZE: return value <= 9223372036854775807ull;
			case ResolvedType::U64: return true;
			case ResolvedType::F32: {
				// Value must be exactly representable as f32.
				if (value > (1ULL << 24)) return false;
				float as_f32 = (float)value;
				return (u64)as_f32 == value;
			}
			case ResolvedType::F64: {
				// Value must be exactly representable as f64.
				double as_f64 = (double)value;
				return (u64)as_f64 == value;
			}
			default: return false;
		}
	}

	static bool negatedIntLiteralFitsType(u64 magnitude, ResolvedType::Kind kind) {
		switch (kind) {
			case ResolvedType::I8: return magnitude <= 128u;
			case ResolvedType::U8: return false;
			case ResolvedType::I16: return magnitude <= 32768u;
			case ResolvedType::U16: return false;
			case ResolvedType::I32: return magnitude <= 2147483648u;
			case ResolvedType::U32: return false;
			case ResolvedType::I64: return magnitude <= 9223372036854775808ull;
			case ResolvedType::ISIZE: return magnitude <= 9223372036854775808ull;
			case ResolvedType::U64: return false;
			case ResolvedType::F32: return intLiteralFitsType(magnitude, ResolvedType::F32);
			case ResolvedType::F64: return intLiteralFitsType(magnitude, ResolvedType::F64);
			default: return false;
		}
	}

	// The raw storage of an untyped integer cannot distinguish -1 from U64_MAX, so the sign
	// has to come from the declaration that produced the value: only a literal that large,
	// or an alias for one, is an unsigned value. Anything computed is signed.
	static bool untypedIntIsUnsigned(const Expression& expr) {
		switch (expr.kind) {
			case Expression::INT_LITERAL:
			case Expression::SIZEOF: return true;
			case Expression::IDENTIFIER: {
				const IdentifierExpression& ie = static_cast<const IdentifierExpression&>(expr);
				if (!ie.symbol || !ie.symbol->expression) return true;
				return untypedIntIsUnsigned(*ie.symbol->expression);
			}
			default: return false;
		}
	}

	static u8* getComptimeBytes(const IdentifierExpression& expr) {
		ASSERT(expr.eval_stage != Expression::RUNTIME);
		if (expr.comptime_bytes) return expr.comptime_bytes;
		return expr.symbol->comptime_bytes;
	}

	// Check whether an expression that has already resolved as untyped numeric or 
	// is not yet resolved can be pinned to `concrete`, without changing the AST. 
	// This is shared by normal materialization and overload matching so a candidate
	// never matches only to fail during the commit pass.
	static bool canMakeConcrete(const Expression& expr, const ResolvedType& concrete) {
		if (!isNumericType(concrete)) return false;
		switch (expr.kind) {
			case Expression::IDENTIFIER: {
				auto& ie = static_cast<const IdentifierExpression&>(expr);
				ASSERT(ie.eval_stage != Expression::RUNTIME);
				switch (ie.resolved_type->kind) {
					case ResolvedType::UNTYPED_INT: {
						i64 val = comptimeNumericToI64(getComptimeBytes(ie), ie.resolved_type->kind);
						// A negative value is a magnitude against a different set of bounds;
						// passing it as u64 would make every negative value look out of range.
						if (val < 0 && !untypedIntIsUnsigned(ie)) return negatedIntLiteralFitsType(u64(-(val + 1)) + 1, concrete.kind);
						return intLiteralFitsType((u64)val, concrete.kind);
					}
					case ResolvedType::UNTYPED_FLOAT: {
						double val = comptimeNumericToF64(getComptimeBytes(ie), ie.resolved_type->kind);
						if (!isFloatType(concrete)) return false;
						return concrete.kind != ResolvedType::F32 || (val <= (double)FLT_MAX && val >= -(double)FLT_MAX);
						return isFloatType(concrete);
					}
					default: break;
				}
				ASSERT(false);
				return false;
			}
			case Expression::INT_LITERAL:
				return intLiteralFitsType(static_cast<const IntLiteralExpression&>(expr).value, concrete.kind);
			case Expression::SIZEOF:
				return intLiteralFitsType(static_cast<const SizeofExpression&>(expr).value, concrete.kind);
			case Expression::FLOAT_LITERAL: {
				if (!isFloatType(concrete)) return false;
				const double value = static_cast<const FloatLiteralExpression&>(expr).value;
				return concrete.kind != ResolvedType::F32 || (value <= (double)FLT_MAX && value >= -(double)FLT_MAX);
			}
			case Expression::UNARY: {
				const UnaryExpression& un = static_cast<const UnaryExpression&>(expr);
				if (un.op == Token::MINUS && un.expression->resolved_type->kind == ResolvedType::UNTYPED_INT) {
					u64 magnitude;
					if (un.expression->kind == Expression::INT_LITERAL) {
						magnitude = static_cast<const IntLiteralExpression*>(un.expression)->value;
					}
					else if (un.expression->kind == Expression::IDENTIFIER) {
						magnitude = comptimeNumericToU64(getComptimeBytes(static_cast<const IdentifierExpression&>(*un.expression)), ResolvedType::UNTYPED_INT);
					}
					else {
						return canMakeConcrete(*un.expression, concrete);
					}
					return negatedIntLiteralFitsType(magnitude, concrete.kind);
				}
				return canMakeConcrete(*un.expression, concrete);
			}
			case Expression::BINARY: {
				const BinaryExpression& bin = static_cast<const BinaryExpression&>(expr);
				return (!bin.lhs || canMakeConcrete(*bin.lhs, concrete))
					&& (!bin.rhs || canMakeConcrete(*bin.rhs, concrete));
			}
			case Expression::TERNARY: {
				const TernaryExpression& tern = static_cast<const TernaryExpression&>(expr);
				return canMakeConcrete(*tern.true_expr, concrete)
					&& canMakeConcrete(*tern.false_expr, concrete);
			}
			default: break;
		}
		return true;
	}

	FunctionResolvedType* buildFunctionType(Unit& unit, FunctionExpression& fn) {
		ASSERT(!fn.is_template);
		if (fn.resolved_type) return static_cast<FunctionResolvedType*>(fn.resolved_type);

		FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit.arena, unit.arena);
		fn_type->decl = &fn;
		for (FunctionParam& param : fn.params) {
			param.resolved_type = asType(evalComptime(unit, *param.type_expr), param.type_expr->token);
			if (!param.resolved_type) return nullptr;

			if (param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) {
				// not supported by the language
				errorLine(fn.token, "Function parameter ", param.name, " cannot be a nullable reference");
				return nullptr;
			}
			FunctionResolvedParam& resolved_param = fn_type->params.emplace_back();
			resolved_param.name = param.name;
			resolved_param.type = param.resolved_type;
			resolved_param.is_comptime = param.is_comptime;
		}
		fn_type->return_type = asType(evalComptime(unit, *fn.return_type), fn.return_type->token);
		if (!fn_type->return_type) return nullptr;

		fn.resolved_type = fn_type;
		return fn_type;
	}

	enum class OverloadResult { NOT_FOUND, FOUND, AMBIGUOUS, FAILED };

	static FunctionResolvedType* asFunctionType(ResolvedType* type) {
		type = unwrapMeta(type);
		return type && type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(type) : nullptr;
	}

	static FunctionExpression* asFunctionExpression(Symbol& symbol) {
		return symbol.expression && symbol.expression->kind == Expression::FUNCTION ? static_cast<FunctionExpression*>(symbol.expression) : nullptr;
	}

	static const char* symbolKind(const Symbol& symbol) {
		switch (symbol.storage) {
			case Symbol::VARIABLE: return "variable";
			case Symbol::CONST: return "constant";
			case Symbol::IMPORT: return "namespace";
			case Symbol::COMPTIME:
				if (symbol.resolved_type && symbol.resolved_type->kind == ResolvedType::META) return "type";
				return "compile-time value";
		}
		return "symbol";
	}

	ResolvedType* checkCallCandidate(Unit& unit,
		FunctionCheckContext* ctx,
		CallExpression& call,
		FunctionResolvedType& fn_type,
		FunctionExpression* resolved_fn = nullptr,
		u32 ufcs_param_offset = 0)
	{
		// num args mismatch
		if (fn_type.params.size() != call.args.size() + ufcs_param_offset) {
			errorLine(call.token, "Function call argument count mismatch: expected ", fn_type.params.size() - ufcs_param_offset, ", got ", call.args.size());
			return nullptr;
		}

		// ufcs type mismatch
		if (ufcs_param_offset) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			ResolvedType* receiver_type = mem.expression->resolved_type;
			ResolvedType* parameter_type = fn_type.params[0].type;
			if (!receiver_type || !canImplicitlyConvert(receiver_type, parameter_type)) {
				return nullptr;
			}
		}

		for (i32 i = 0; i < call.args.size(); ++i) {
			const i32 param_index = ufcs_param_offset + i;
			ResolvedType* param_type = fn_type.params[param_index].type;
			Expression* arg = call.args[i];
			if (fn_type.params[param_index].is_comptime) continue;

			ResolvedType* arg_type = checkExprForTarget(unit, ctx, *arg, param_type);
			if (!arg_type) return nullptr;
			if (!requireMaterializable(*arg, "a runtime function argument")) return nullptr;

			if (!canImplicitlyConvert(arg_type, param_type)) {
				errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for argument ", i + 1, " of function call");
				return nullptr;
			}
		}

		if (resolved_fn) call.resolved_fn = resolved_fn;
		call.resolved_type = fn_type.return_type;
		call.eval_stage = fn_type.return_type->kind == ResolvedType::META || fn_type.return_type == &module.type_kind
			? Expression::COMPTIME_ONLY
			: Expression::RUNTIME;
		return call.resolved_type;
	}

	static bool operandMatchesParam(Expression& operand, ResolvedType& type, ResolvedType& param) {
		if (typesEqual(&type, &param)) return true;
		if (isUntypedNumeric(type)) return canMakeConcrete(operand, param);
		return false;
	}

	OverloadResult resolveOperatorOverload(Unit& unit,
		FunctionCheckContext* ctx,
		Token::Type op,
		i32 arity,
		Expression** operands, // array of `arity` expression pointers
		ResolvedType** operand_types,
		StructExpression& host,
		ResolvedType*& result_type,
		FunctionExpression*& result_fn
	) {
		FunctionResolvedType* matched_type = nullptr;
		FunctionExpression* matched_fn = nullptr;
		for (StructOperator& cand : host.operators) {
			if (cand.op != op) continue;

			FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(cand.fn->resolved_type);
			if (fn_type->params.size() != arity) continue;

			if (!operandMatchesParam(*operands[0], *operand_types[0], *fn_type->params[0].type)) continue;
			if (arity > 1 && !operandMatchesParam(*operands[1], *operand_types[1], *fn_type->params[1].type)) continue;

			if (matched_fn) return OverloadResult::AMBIGUOUS;

			matched_type = fn_type;
			matched_fn = cand.fn;
		}

		if (!matched_fn) return OverloadResult::NOT_FOUND;

		// Commit pass: pin untyped numeric operands to the winning signature.
		for (i32 j = 0; j < arity; ++j) {
			bool b = makeConcrete(*operands[j], matched_type->params[j].type);
			ASSERT(b); // already checked in `operandMatchesParam`
		}
		result_type = matched_type->return_type;
		result_fn = matched_fn;
		return OverloadResult::FOUND;
	}

	static Symbol* findSymbol(Unit& unit, ls_string_view name) {
		for (Symbol& sym : unit.symbols) {
			if (equalStrings(sym.name, name)) return &sym;
		}
		return nullptr;
	}

	static SemanticLocalBinding* findLocal(FunctionCheckContext& ctx, ls_string_view name) {
		for (i32 i = (i32)ctx.locals.size() - 1; i >= 0; --i) {
			SemanticLocalBinding& binding = ctx.locals[(u32)i];
			if (equalStrings(binding.name, name)) return &binding;
		}
		return nullptr;
	}

	static void pushScope(FunctionCheckContext& ctx) { ctx.scope_marks.push((u32)ctx.locals.size()); }

	static void popScope(FunctionCheckContext& ctx) {
		if (ctx.scope_marks.empty()) return;
		const i32 mark = ctx.scope_marks.back();
		ctx.scope_marks.pop_back();
		while (ctx.locals.size() > mark) ctx.locals.pop_back();
	}

	bool comptimeValueMatchesExpected(const ComptimeValue& value, ResolvedType* expected) {
		ASSERT(value.kind != ComptimeValue::FAILURE);
		if (!expected) return value.kind == ComptimeValue::TYPE;

		if (expected->kind == ResolvedType::META) return value.kind == ComptimeValue::TYPE;
		if (value.kind == ComptimeValue::TYPE) return false;
		
		if (value.type == expected) return true;

		if (!canImplicitlyConvert(value.type, expected)) return false;
		if (!isNumericOrUntyped(*value.type) || !isNumericType(*expected)) return typesEqual(value.type, expected);

		switch (value.type->kind) {
			case ResolvedType::UNTYPED_INT: {
				i64 val = comptimeNumericToI64(value.value, value.type->kind);
				// A negative value is a magnitude against a different set of bounds; passing
				// it as u64 would make every negative value look out of range.
				if (val < 0) return negatedIntLiteralFitsType(u64(-(val + 1)) + 1, expected->kind);
				return intLiteralFitsType((u64)val, expected->kind);
			}
			case ResolvedType::UNTYPED_FLOAT: {
				double val = comptimeNumericToF64(value.value, value.type->kind);
				if (expected->kind == ResolvedType::F32) return val <= (double)FLT_MAX && val >= -(double)FLT_MAX;
				return true;
			}
			default: break;
		}
		return canImplicitlyConvert(value.type, expected);
	}

	ComptimeValue coerceComptimeValue(const ComptimeValue& value, ResolvedType* target) {
		ASSERT(value.kind == ComptimeValue::VALUE && target);
		if (value.type == target) return value;

		if (target->kind == ResolvedType::NULLABLE) {
			ResolvedType* inner = static_cast<NullableResolvedType*>(target)->inner;
			ComptimeValue source = value;
			if (value.type->kind == ResolvedType::NULLABLE) {
				if (!typesEqual(value.type, target)) return {};
				if (!value.value[0]) {
					u8* bytes = comptime_stack_ptr;
					memset(bytes, 0, comptimeSize(*target));
					comptime_stack_ptr += comptimeSize(*target);
					return {ComptimeValue::VALUE, target, bytes};
				}
				NullableResolvedType* source_type = static_cast<NullableResolvedType*>(value.type);
				source = {ComptimeValue::VALUE, source_type->inner, value.value + 1};
			}
			else if (!canImplicitlyConvert(value.type, target)) {
				return {};
			}

			ComptimeValue converted = coerceComptimeValue(source, inner);
			if (!converted) return {};
			u8* bytes = comptime_stack_ptr;
			*comptime_stack_ptr = 1;
			comptime_stack_ptr++;
			copyMemory(comptime_stack_ptr, converted.value, comptimeSize(*inner));
			comptime_stack_ptr += comptimeSize(*inner);
			return {ComptimeValue::VALUE, target, bytes};
		}

		if (target->kind == ResolvedType::UNION) {
			UnionResolvedType* un = static_cast<UnionResolvedType*>(target);
			ComptimeValue source = value;
			if (value.type->kind == ResolvedType::UNION) {
				if (!canImplicitlyConvert(value.type, target)) return {};
				UnionResolvedType* source_type = static_cast<UnionResolvedType*>(value.type);
				i32 tag;
				copyMemory(&tag, value.value, sizeof(tag));
				if (tag < 0 || tag >= source_type->members.size()) return {};
				source = {ComptimeValue::VALUE, source_type->members[tag], value.value + sizeof(tag)};
			}

			i32 matched_index = -1;
			for (i32 i = 0; i < un->members.size(); ++i) {
				ResolvedType* member = un->members[i];
				const bool matches = isUntypedNumeric(*source.type)
					? (source.type->kind == ResolvedType::UNTYPED_INT ? isIntegerType(*member) : isFloatType(*member))
					: typesEqual(source.type, member);
				if (!matches) continue;
				if (matched_index >= 0) return {};
				matched_index = i;
			}
			if (matched_index < 0) return {};

			ResolvedType* member = un->members[matched_index];
			ComptimeValue converted = coerceComptimeValue(source, member);
				if (!converted) return {};
				u8* bytes = comptime_stack_ptr;
				memset(bytes, 0, comptimeSize(*target));
			copyMemory(bytes, &matched_index, sizeof(matched_index));
			copyMemory(bytes + sizeof(matched_index), converted.value, comptimeSize(*member));
				comptime_stack_ptr += comptimeSize(*target);
				return {ComptimeValue::VALUE, target, bytes};
		}

		if (typesEqual(value.type, target)) return {ComptimeValue::VALUE, target, value.value};

		if (isUntypedNumeric(*value.type) && canImplicitlyConvert(value.type, target)) {
			u8* bytes = comptime_stack_ptr;
			comptime_stack_ptr += writeComptimeNumeric(bytes, value.value, value.type->kind, target->kind);
			return {ComptimeValue::VALUE, target, bytes};
		}

		return {};
	}

	bool resolveStructFields(Unit& unit, const Symbol* sym, StructExpression& st, StructResolvedType& st_type) {
		for (NamedDecl& field : st.fields) {
			ResolvedType* field_type = asType(evalComptime(unit, *field.type_expr), field.type_expr->token);
			if (!field_type) return false;

			st_type.field_types.push(field_type);
		}

		ExpArray<ResolvedType*> visited(unit.arena);
		for (ResolvedType* field_type : st_type.field_types) {
			if (containsStructByValue(*field_type, st_type, visited)) {
				if (sym) errorLine(sym->token, "Recursive by-value field in struct ", sym->name);
				else errorLine(st.token, "Recursive by-value field in struct");
				return false;
			}
		}
		return true;
	}

	// TODO error msgs here instead of in the callers
	bool inferTemplateArg(Unit& unit, TemplateBindings& bindings, Expression& pattern, const ComptimeValue& actual) {
		ResolvedType* actual_type = actual.kind == ComptimeValue::TYPE ? actual.type : nullptr;
		switch (pattern.kind) {
			case Expression::GENERIC_IDENTIFIER: {
				GenericIdentifierExpression& generic = static_cast<GenericIdentifierExpression&>(pattern);
				ComptimeValue value = actual_type ? ComptimeValue{ComptimeValue::TYPE, actual_type} : actual;
				return bindTemplateArg(bindings, generic.name, value);
			}
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(pattern);
				ComptimeValue value = actual_type ? ComptimeValue{ComptimeValue::TYPE, actual_type} : actual;
				if (findTemplateBinding(&bindings, id.name)) return bindTemplateArg(bindings, id.name, value);
				if (actual_type && !resolveSymbol(unit, {}, id.name, LookupPolicy::NameOnly)) {
					return bindTemplateArg(bindings, id.name, value);
				}
				break;
			}
			case Expression::ARRAY_TYPE: {
				if (!actual_type || actual_type->kind != ResolvedType::ARRAY) return false;
				ArrayTypeExpression& p = static_cast<ArrayTypeExpression&>(pattern);
				ArrayResolvedType& a = static_cast<ArrayResolvedType&>(*actual_type);
				return inferTemplateArg(unit, bindings, *p.element_type, ComptimeValue{ComptimeValue::TYPE, a.element_type});
			}
			case Expression::SLICE_TYPE: {
				if (!actual_type) return false;
				ResolvedType* actual_element = nullptr;
				if (actual_type->kind == ResolvedType::SLICE) actual_element = static_cast<SliceResolvedType*>(actual_type)->element_type;
				else if (actual_type->kind == ResolvedType::ARRAY) actual_element = static_cast<ArrayResolvedType*>(actual_type)->element_type;
				else return false;
				return inferTemplateArg(unit, bindings, *static_cast<SliceTypeExpression&>(pattern).element_type, ComptimeValue{ComptimeValue::TYPE, actual_element});
			}
			case Expression::NULLABLE_TYPE: {
				if (!actual_type || actual_type->kind != ResolvedType::NULLABLE) return false;
				return inferTemplateArg(unit, bindings, *static_cast<NullableTypeExpression&>(pattern).inner, ComptimeValue{ComptimeValue::TYPE, static_cast<NullableResolvedType*>(actual_type)->inner});
			}
			case Expression::POINTER_TYPE: {
				if (!actual_type || actual_type->kind != ResolvedType::POINTER) return false;
				return inferTemplateArg(unit, bindings, *static_cast<PointerTypeExpression&>(pattern).inner, ComptimeValue{ComptimeValue::TYPE, static_cast<PointerResolvedType*>(actual_type)->inner});
			}
			case Expression::CALL: {
				// `fn foo(v : Box($T))` - find the factory instance that produced the
				// argument's type, then infer from the arguments that produced it.
				if (!actual_type) break;
				CallExpression& call = static_cast<CallExpression&>(pattern);
				SymbolRef ref = resolveSymbol(unit, *call.callee);
				if (!ref || !ref.symbol->expression || ref.symbol->expression->kind != Expression::FUNCTION) break;
				FunctionExpression& factory = *static_cast<FunctionExpression*>(ref.symbol->expression);
				if (!factory.is_template || factory.params.size() != call.args.size()) break;

				for (TemplateFunctionInstance& instance : factory.template_function_instances) {
					if (instance.check_failed || !instance.instance || instance.args.size() != call.args.size()) continue;
					if (instance.produced_type != actual_type) continue;

					for (i32 i = 0; i < call.args.size(); ++i) {
						if (!inferTemplateArg(unit, bindings, *call.args[i], instance.args[i])) return false;
					}
					return true;
				}
				return false;
			}
		}

		if (actual.kind != ComptimeValue::TYPE) return false;

		ResolvedType* pattern_type = asType(evalComptime(unit, pattern, nullptr, &bindings), pattern.token);
		if (!pattern_type) return false;

		return typesEqual(pattern_type, actual.type);
	}

	FunctionExpression* instantiateFunctionTemplate(Unit& unit, FunctionExpression& fn, const TemplateBindings& bindings) {
		// check cache first
		for (TemplateFunctionInstance& instance : fn.template_function_instances) {
			if (instance.args.size() == bindings.values.size()) {
				bool equal = true;
				for (i32 i = 0; i < instance.args.size(); ++i) {
					if (!comptimeValuesEqual(instance.args[i], bindings.values[i].arg)) {
						equal = false;
						break;
					}
				}
				if (equal) {
					// The first instantiation may have been attempted while errors were
					// suppressed, so say something instead of failing silently.
					if (instance.check_failed) {
						errorLine(fn.token, "Instantiation of ", fn.token.value, " with these arguments failed");
						return nullptr;
					}
					return instance.instance;
				}
			}
		}

		// create new instance
		TemplateFunctionInstance& instance = fn.template_function_instances.emplace_back(unit.arena);
		for (const TemplateBinding& binding : bindings.values) instance.args.push(binding.arg);

		FunctionExpression* clone = makeType<FunctionExpression>(unit.arena, unit.arena);
		clone->token = fn.token;
		clone->is_template = false;
		clone->is_extern = fn.is_extern;
		FunctionResolvedType* fn_type = nullptr;
		bool body_ok = false;
		for (FunctionParam& src_param : fn.params) {
			FunctionParam& dst_param = clone->params.emplace_back();
			dst_param.name = src_param.name;
			dst_param.is_ref = src_param.is_ref;
			dst_param.is_comptime = src_param.is_comptime;
			dst_param.is_generic = false;
			 dst_param.type_expr = cloneExpression(unit, src_param.type_expr, &bindings);
		}
		clone->return_type = cloneExpression(unit, fn.return_type, &bindings);
		clone->body = cloneStatement(unit, fn.body, &bindings);
		instance.instance = clone;

		fn_type = buildFunctionType(unit, *clone);
		if (!fn_type) {
			instance.check_failed = true;
			clone->resolved_type = nullptr;
			return nullptr;
		}
		instance.type = fn_type;

		if (!checkFunctionBody(unit, *clone)) {
			errorLine(clone->token, "Failed to check function body for template instantiation");
			instance.check_failed = true;
			clone->resolved_type = nullptr;
			return nullptr;
		}
		return clone;
	}

	SymbolRef resolveSymbol(Unit& unit, const Expression& expression) {
		ls_string_view qualifier = {};
		ls_string_view name = {};
		if (expression.kind == Expression::IDENTIFIER) {
			name = static_cast<const IdentifierExpression&>(expression).name;
		} else if (expression.kind == Expression::MEMBER) {
			const MemberExpression& member = static_cast<const MemberExpression&>(expression);
			if (!member.expression || member.expression->kind != Expression::IDENTIFIER) return {};
			qualifier = static_cast<IdentifierExpression*>(member.expression)->name;
			name = member.name;
		} else {
			return {};
		}
		return resolveSymbol(unit, qualifier, name, LookupPolicy::NameOnly);
	}

	ls_result checkComptimeFunctionSymbol(Unit& unit, Symbol& sym) {
		FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
		if (fn.is_template) {
			sym.resolved_type = nullptr;
			return LS_RESULT_OK;
		}

		// Signature was built and published by checkSymbol before dispatching here.
		FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(fn.resolved_type);
		if (const Token::Type op_token = tokenFromOperatorName(sym.name); op_token != Token::ERROR) {
			const i32 arity = (i32)fn_type->params.size();
			if ((op_token == Token::MINUS ? (arity != 1 && arity != 2) : arity != 2)) {
				errorLine(sym.token, "Invalid operator arity");
				return LS_RESULT_FAILURE;
			}

			bool struct_signature = false;
			for (const FunctionResolvedParam& param : fn_type->params) {
				if (param.type->kind == ResolvedType::STRUCT) {
					struct_signature = true;
					break;
				}
			}
			if (!struct_signature) {
				errorLine(sym.token, "Operator overloads must have at least one struct parameter");
				return LS_RESULT_FAILURE;
			}

			for (const FunctionResolvedParam& param : fn_type->params) {
				if (param.type->kind == ResolvedType::ENUM) {
					errorLine(sym.token, "Operator overloads with enum parameters are not allowed; use a wrapper struct instead");
					return LS_RESULT_FAILURE;
				}
			}
		}

		if (!checkFunctionBody(unit, fn)) return LS_RESULT_FAILURE;
		return LS_RESULT_OK;
	}

	bool containsStructByValue(ResolvedType& type, StructResolvedType& target, ExpArray<ResolvedType*>& visited) {
		if (&type == &target) return true;
		if (type.kind == ResolvedType::ARRAY) {
			return containsStructByValue(*static_cast<ArrayResolvedType&>(type).element_type, target, visited);
		}
		if (type.kind == ResolvedType::NULLABLE) {
			return containsStructByValue(*static_cast<NullableResolvedType&>(type).inner, target, visited);
		}
		if (type.kind == ResolvedType::UNION) {
			for (ResolvedType* member : static_cast<UnionResolvedType&>(type).members) {
				if (containsStructByValue(*member, target, visited)) return true;
			}
			return false;
		}
		if (type.kind != ResolvedType::STRUCT) return false;
		for (ResolvedType* seen : visited) {
			if (seen == &type) return false;
		}
		visited.push(&type);
		StructResolvedType& st = static_cast<StructResolvedType&>(type);
		for (ResolvedType* field_type : st.field_types) {
			if (containsStructByValue(*field_type, target, visited)) return true;
		}
		return false;
	}

	ls_result checkComptimeStructSymbol(Unit& unit, Symbol& sym) {
		StructExpression& st = static_cast<StructExpression&>(*sym.expression);
		st.cached_name = sym.name;
		st.cached_owner = &unit;
		StructResolvedType* st_type = makeType<StructResolvedType>(unit.arena, unit.arena);
		st_type->decl = &st;
		MetaType* meta = makeType<MetaType>(unit.arena);
		meta->inner = st_type;
		sym.resolved_type = meta;
		if (!resolveStructFields(unit, &sym, st, *st_type)) return LS_RESULT_FAILURE;
		return LS_RESULT_OK;
	}

	ls_result checkComptimeEnumSymbol(Unit& unit, Symbol& sym) {
		EnumExpression& en = static_cast<EnumExpression&>(*sym.expression);
		en.cached_name = sym.name;
		en.cached_owner = &unit;
		EnumResolvedType* en_type = makeType<EnumResolvedType>(unit.arena);
		en_type->decl = &en;
		MetaType* meta = makeType<MetaType>(unit.arena);
		meta->inner = en_type;
		sym.resolved_type = meta;
		return LS_RESULT_OK;
	}

	ls_result checkComptimeValueSymbol(Unit& unit, Symbol& sym) {
		// Plain comptime value: comptime N = expr;
		ResolvedType* annotation = nullptr;
		if (sym.type_expr) {
			annotation = asType(evalComptime(unit, *sym.type_expr), sym.type_expr->token);
			if (!annotation) return LS_RESULT_FAILURE;
		}

		ResolvedType* initializer_type = checkExpr(unit, nullptr, *sym.expression, annotation);
		if (!initializer_type) return LS_RESULT_FAILURE;
		if (annotation && isUntypedNumeric(*initializer_type) && !makeConcrete(*sym.expression, annotation)) return LS_RESULT_FAILURE;

		ComptimeValue t = evalComptime(unit, *sym.expression, nullptr, nullptr, nullptr, annotation);
		if (!t) return LS_RESULT_FAILURE;

		if (annotation && t.kind == ComptimeValue::VALUE && (annotation->kind == ResolvedType::NULLABLE || annotation->kind == ResolvedType::UNION || isUntypedNumeric(*t.type))) {
			ResolvedType* initializer_type = t.type;
			t = coerceComptimeValue(t, annotation);
			if (!t) {
				errorLine(sym.token, "Cannot convert comptime initializer type ", initializer_type, " to annotated type ", annotation, " for: ", sym.name);
				return LS_RESULT_FAILURE;
			}
		}
		else if (annotation && t.kind == ComptimeValue::VALUE && !canImplicitlyConvert(t.type, annotation)) {
			errorLine(sym.token, "Cannot convert comptime initializer type ", t.type, " to annotated type ", annotation, " for: ", sym.name);
			return LS_RESULT_FAILURE;
		}

		sym.comptime_value = t;

		if (t.kind == ComptimeValue::VALUE) {
			sym.comptime_byte_size = comptimeSize(*t.type);
			u8* bytes = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, sym.comptime_byte_size, 1));
			copyMemory(bytes, t.value, sym.comptime_byte_size);
			sym.comptime_bytes = bytes;
			sym.comptime_value.value = const_cast<u8*>(sym.comptime_bytes);
			sym.resolved_type = t.type;
		}
		else if (t.kind == ComptimeValue::TYPE) {
			auto* meta = makeType<MetaType>(unit.arena);
			meta->inner = t.type;
			sym.resolved_type = meta;
		}
		else sym.resolved_type = t.type;
		return LS_RESULT_OK;
	}

	ls_result checkComptimeSymbol(Unit& unit, Symbol& sym) {
		switch (sym.expression->kind) {
			case Expression::FUNCTION: return checkComptimeFunctionSymbol(unit, sym);
			case Expression::STRUCT: return checkComptimeStructSymbol(unit, sym);
			case Expression::ENUM: return checkComptimeEnumSymbol(unit, sym);
			default: return checkComptimeValueSymbol(unit, sym);
		}
	}

	ls_result checkRuntimeSymbol(Unit& unit, Symbol& sym) {
		ResolvedType* annotation = nullptr;
		if (sym.type_expr) {
			annotation = asType(evalComptime(unit, *sym.type_expr), sym.type_expr->token);
			if (!annotation) return LS_RESULT_FAILURE;
		}
		ASSERT(sym.expression);
		Expression& expr = *sym.expression;

		if (expr.kind == Expression::UNDEFINED) {
			if (!annotation) {
				// var a = undefined; - no way to know the type
				errorLine(sym.token, "'undefined' initializer requires an explicit type annotation: ", sym.name);
				return LS_RESULT_FAILURE;
			}
			if (sym.storage == Symbol::CONST) {
				// const a : i32 = undefined; - not useful
				errorLine(sym.token, "const cannot be initialized with 'undefined': ", sym.name);
				return LS_RESULT_FAILURE;
			}
		}

		if (expr.kind == Expression::FUNCTION && !static_cast<FunctionExpression&>(expr).is_template) {
			// checkSymbol already published the signature as the symbol's type; the body
			// must be checked here because checkExpr treats the pre-built signature as an
			// already-checked cache entry and skips the body.
			if (!checkFunctionBody(unit, static_cast<FunctionExpression&>(expr))) return LS_RESULT_FAILURE;
		}

		ResolvedType* expr_type = checkExprForTarget(unit, nullptr, expr, annotation);
		if (!expr_type) return LS_RESULT_FAILURE;

		if (!requireMaterializable(expr, "a runtime global initializer")) return LS_RESULT_FAILURE;

		if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(sym.token, "Cannot convert initializer type ", expr_type, " to annotated type ", annotation, " for: ", sym.name);
			return LS_RESULT_FAILURE;
		}

		sym.resolved_type = annotation ? annotation : expr_type;
		return LS_RESULT_OK;
	}

	FunctionExpression* instantiateAndCheckTemplate(Unit& unit, FunctionCheckContext* ctx, Expression& call_expr, CallExpression& call, Unit& template_unit, FunctionExpression& fn, u32 ufcs_param_offset = 0) {
		ASSERT(fn.is_template);
		TemplateBindings bindings(unit.arena); // TODO reuse?
		if (fn.params.size() != call.args.size() + ufcs_param_offset) {
			errorLine(call_expr.token, "Function call argument count mismatch: expected ", fn.params.size() - ufcs_param_offset, ", got ", call.args.size());
			return nullptr;
		}
		if (ufcs_param_offset) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			if (!inferTemplateArg(template_unit, bindings, *fn.params[0].type_expr, ComptimeValue{ComptimeValue::TYPE, mem.expression->resolved_type})) {
				errorLine(call_expr.token, "Cannot infer template arguments for receiver of ", fn.token.value);
				return nullptr;
			}
		}

		for (i32 i = 0; i < call.args.size(); ++i) {
			const u32 param_index = ufcs_param_offset + i;
			FunctionParam& param = fn.params[param_index];
			Expression* arg = call.args[i];

			ResolvedType* expected = nullptr;
			if (param.is_comptime
				&& param.type_expr->kind == Expression::TYPE_LITERAL
				&& static_cast<TypeLiteralExpression*>(param.type_expr)->type == ResolvedType::META)
			{
				expected = nullptr;
			} else {
				// TODO why suppress errors?
				++suppress_errors;
				expected = asType(evalComptime(template_unit, *param.type_expr, nullptr, &bindings), param.type_expr->token);
				--suppress_errors;
			}

			if (param.is_comptime) {
				if (expected && !checkExprForTarget(unit, ctx, *arg, expected)) return nullptr;
				ComptimeValue template_arg = evalComptime(unit, *arg, ctx, &bindings, nullptr, expected);
				if (!template_arg) return nullptr;
				
				if (!comptimeValueMatchesExpected(template_arg, expected)) {
					errorLine(arg->token, "Could not resolve comptime template argument, expected ", expected, ", got ", template_arg.type);
					return nullptr;
				}
				if (!bindTemplateArg(bindings, param.name, template_arg)) {
					errorLine(arg->token, "Conflicting comptime template argument");
					return nullptr;
				}
				continue;
			}

			ResolvedType* arg_type = checkExprForTarget(unit, ctx, *arg, expected);
			if (!arg_type) return nullptr;
			if (expected && !canImplicitlyConvert(arg_type, expected)) {
				errorLine(arg->token, "Cannot convert ", arg_type, " to ", expected, " for argument ", i + 1, " of function call");
				return nullptr;
			}
			if (!inferTemplateArg(template_unit, bindings, *param.type_expr, ComptimeValue{ComptimeValue::TYPE, arg_type})) {
				errorLine(arg->token, "Cannot infer template parameter type for argument ", i + 1, " of ", fn.token.value);
				return nullptr;
			}
		}

		FunctionExpression* instance = instantiateFunctionTemplate(template_unit, fn, bindings);
		if (!instance) return nullptr;
		return instance;
	}

	ResolvedType* checkCallExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		CallExpression& call = static_cast<CallExpression&>(expr);

		// Suppress errors while probing normal lookup because a failed member lookup can
		// fall back to UFCS, and the first argument can be checked again during the call.
		++suppress_errors;
		ResolvedType* first_arg_type = nullptr;
		if (!call.args.empty()) {
			first_arg_type = checkExpr(unit, ctx, *call.args[0], nullptr);
		}
		ResolvedType* callee_type = checkExpr(unit, ctx, *call.callee, nullptr, first_arg_type);
		--suppress_errors;

		if (callee_type) {
			switch (callee_type->kind) {
				case ResolvedType::FUNCTION:
					return checkCallCandidate(unit, ctx, call, static_cast<FunctionResolvedType&>(*callee_type));
				case ResolvedType::NULLABLE:
					errorLine(expr.token, "Cannot call nullable function type ", callee_type, "without a null check");
					return nullptr;
				default:
					errorLine(expr.token, "Cannot call non-function type ", callee_type);
					return nullptr;
			}
		}

		// template with inferred parameters
		if (SymbolRef sym = resolveSymbol(unit, *call.callee)) {
			FunctionExpression* fn = asFunctionExpression(*sym.symbol);
			if (!fn) {
				errorLine(expr.token, "Cannot call ", symbolKind(*sym.symbol), " '", sym.symbol->name, "' as a function");
				return nullptr;
			}
			if (fn->is_template) {
				FunctionExpression* instance = instantiateAndCheckTemplate(unit, ctx, expr, call, *sym.owner, *fn);
				if (!instance) return nullptr;
				if (call.callee->kind == Expression::IDENTIFIER) static_cast<IdentifierExpression*>(call.callee)->symbol = sym.symbol;
				FunctionResolvedType* fn_type = asFunctionType(instance->resolved_type);
				ASSERT(fn_type);
				return checkCallCandidate(unit, ctx, call, *fn_type, instance);
			}

			errorLine(expr.token, "Cannot call ", symbolKind(*sym.symbol), " '", sym.symbol->name, "' as a function");
			return nullptr;
		}

		// UFCS: x.foo(a, b) -> foo(x, a, b)
		if (call.callee->kind != Expression::MEMBER) {
			// check again without suppressed errors
			checkExpr(unit, ctx, *call.callee, nullptr, first_arg_type);
			return nullptr;
		}

		MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);

		if (!mem.expression) {
			errorLine(expr.token, "Expected receiver expression for function call ", mem.name);
			return nullptr;
		}

		if (!mem.expression->resolved_type) {
			errorLine(expr.token, "Cannot call function ", mem.name, " with UFCS receiver with unknown type");
			return nullptr;
		}

		ResolvedType* receiver_type = mem.expression->resolved_type;
		if (receiver_type->kind == ResolvedType::POINTER) {
			receiver_type = static_cast<PointerResolvedType*>(receiver_type)->inner;
		}
		if (receiver_type->kind != ResolvedType::STRUCT && receiver_type->kind != ResolvedType::ENUM) {
			errorLine(expr.token, "Cannot call member function ", mem.name, " on type ", receiver_type, ", expected struct or enum");
			return nullptr;
		}

		// Method syntax dispatches on the receiver: the type's own unit wins over
		// local and imported declarations, so e.g. a script's own `init` does not
		// shadow `array.init` in `a.init()`. Lexical lookup is only a fallback.
		SymbolRef ref;
		if (Unit* namespace_unit = findTypeNamespaceUnit(*receiver_type)) {
			if (Symbol* candidate = findSymbol(*namespace_unit, mem.name)) {
				ref = {namespace_unit, candidate};
				if (checkSymbol(*namespace_unit, *candidate) == LS_RESULT_FAILURE) ref.check_failed = true;
			}
		}
		if (!ref.symbol) ref = resolveSymbol(unit, {}, mem.name, LookupPolicy::Checked);

		if (!ref) {
			errorLine(expr.token, "Could not resolve member function: ", mem.name);
			return nullptr;
		}

		FunctionExpression* fn = asFunctionExpression(*ref.symbol);
		if (fn && fn->is_template) {
			FunctionExpression* instance = instantiateAndCheckTemplate(unit, ctx, expr, call, *ref.owner, *fn, 1);
			if (!instance) return nullptr;
			fn = instance;
		}
		FunctionResolvedType* fn_type = asFunctionType(fn ? fn->resolved_type : ref.symbol->resolved_type);
		if (!fn_type) {
			errorLine(expr.token, "Cannot call ", symbolKind(*ref.symbol), " '", ref.symbol->name, "' as a function");
			return nullptr;
		}
		return checkCallCandidate(unit, ctx, call, *fn_type, fn, 1);
	}

	// Pin an untyped numeric expression to a concrete type. With no compatible
	// target, choose the expression's default type; recursive calls pass false
	// after the complete expression has already been range-checked.
	ResolvedType* makeConcrete(Expression& expr, ResolvedType* concrete, bool check_fit = true) {
		if (!isUntypedNumeric(*expr.resolved_type)) return expr.resolved_type;

		const ResolvedType::Kind untyped_kind = expr.resolved_type->kind;
		concrete = unwrapNullable(concrete);
		// no hint, use default type
		if (!concrete) {
			if (untyped_kind == ResolvedType::UNTYPED_FLOAT) {
				concrete = primitiveType(ResolvedType::F64);
			}
			else {
				ResolvedType* defaults[] = {
					primitiveType(ResolvedType::I32),
					primitiveType(ResolvedType::I64),
				};
				concrete = primitiveType(ResolvedType::U64);

				if (canMakeConcrete(expr, *primitiveType(ResolvedType::I32))) concrete = primitiveType(ResolvedType::I32);
				else if (canMakeConcrete(expr, *primitiveType(ResolvedType::I64))) concrete = primitiveType(ResolvedType::I64);
			}
		}

		// union, e.g. var a : i32 | bool = 42;
		if (concrete->kind == ResolvedType::UNION && isUntypedNumeric(*expr.resolved_type)) {
			UnionResolvedType& un = static_cast<UnionResolvedType&>(*concrete);
			ResolvedType* member = nullptr;
			for (ResolvedType* candidate : un.members) {
				const bool compatible = expr.resolved_type->kind == ResolvedType::UNTYPED_INT ? isIntegerType(*candidate) : isFloatType(*candidate);
				if (!compatible) continue;

				if (member) {
					errorLine(expr.token, "Cannot infer union member type for numeric literal, multiple candidates found: ", member, " and ", candidate);
					return nullptr;
				}
				
				member = candidate;
			}
			if (member) concrete = makeConcrete(expr, member);
		}

		const bool compatible = untyped_kind == ResolvedType::UNTYPED_INT ? isNumericType(*concrete) : isFloatType(*concrete);
		if (!compatible) {
			errorLine(expr.token, "Untyped numeric expression does not match target type ", concrete);
			return nullptr;
		}

		if (check_fit && !canMakeConcrete(expr, *concrete)) {
			errorLine(expr.token, "Untyped numeric expression does not fit in ", concrete);
			return nullptr;
		}

		expr.resolved_type = concrete;
		switch (expr.kind) {
			case Expression::UNARY: {
				UnaryExpression& un = static_cast<UnaryExpression&>(expr);
				if (un.expression) makeConcrete(*un.expression, concrete, false);
				break;
			}
			case Expression::BINARY: {
				BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
				if (bin.lhs) makeConcrete(*bin.lhs, concrete, false);
				if (bin.rhs) makeConcrete(*bin.rhs, concrete, false);
				break;
			}
			case Expression::TERNARY: {
				TernaryExpression& tern = static_cast<TernaryExpression&>(expr);
				if (tern.true_expr) makeConcrete(*tern.true_expr, concrete, false);
				if (tern.false_expr) makeConcrete(*tern.false_expr, concrete, false);
				break;
			}
			default: break;
		}
		return concrete;
	}

	ResolvedType* checkUnaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		UnaryExpression& un = static_cast<UnaryExpression&>(expr);
		// TODO do we need this?
		if (un.op == Token::MINUS && un.expression->kind == Expression::INT_LITERAL) {
			// Range-check the negated integer literal against the expected type.
			IntLiteralExpression* lit = static_cast<IntLiteralExpression*>(un.expression);
			lit->resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
			ResolvedType* int_hint = unwrapNullable(hint);
			if (int_hint && isNumericType(*int_hint)) {
				if (!negatedIntLiteralFitsType(lit->value, int_hint->kind)) {
					errorLine(expr.token, "Integer literal does not fit in type ", int_hint);
					return nullptr;
				}
				lit->resolved_type = int_hint;
				expr.resolved_type = int_hint;
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return int_hint;
			}
			// No concrete target: stay untyped until materialization chooses a width.
			expr.resolved_type = lit->resolved_type;
			expr.eval_stage = Expression::COMPTIME_VALUE;
			return expr.resolved_type;
		}
		ResolvedType* inner = checkExpr(unit, ctx, *un.expression, hint);
		if (!inner) return nullptr;

		if (un.op == Token::MINUS && inner->kind == ResolvedType::STRUCT) {
			ResolvedType* overload_result = nullptr;
			FunctionExpression* overload_fn = nullptr;
			StructExpression* host = static_cast<StructResolvedType*>(inner)->decl;
			OverloadResult unary_result = resolveOperatorOverload(unit, ctx, un.op, 1, &un.expression, &inner, *host, overload_result, overload_fn);
			switch (unary_result) {
				case OverloadResult::FOUND: un.resolved_fn = overload_fn; expr.resolved_type = overload_result; expr.eval_stage = Expression::RUNTIME; return overload_result;
				case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(un.op), " overload"); return nullptr;
				case OverloadResult::FAILED: return nullptr;
				case OverloadResult::NOT_FOUND: errorLine(expr.token, "No matching operator ", operatorSymbolName(un.op), " overload"); return nullptr;
			}
		}

		switch (un.op) {
			case Token::MINUS: {
				if (!isNumericOrUntyped(*inner)) {
					errorLine(expr.token, "Cannot apply negation operator to ", inner);
					return nullptr;
				}

				if (inner->kind == ResolvedType::U8 || inner->kind == ResolvedType::U16 || inner->kind == ResolvedType::U32 || inner->kind == ResolvedType::U64) {
					errorLine(expr.token, "Cannot apply negation operator to unsigned integer ", inner);
					return nullptr;
				}

				expr.resolved_type = inner;
				expr.eval_stage = un.expression->eval_stage;
				return inner;
			}
			case Token::NOT:
				if (!typesEqual(inner, primitiveType(ResolvedType::BOOL))) {
					errorLine(expr.token, "Cannot apply not operator to ", inner);
					return nullptr;
				}
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				expr.eval_stage = un.expression->eval_stage == Expression::COMPTIME_VALUE
					? Expression::COMPTIME_VALUE
					: Expression::RUNTIME;
				return expr.resolved_type;
			default:
				// TODO error msg
				return nullptr;
		}
	}

	ResolvedType* checkBinaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
		// lhs is probed first because context-dependent syntax may need rhs as its hint.
		// e.g. .Idle == e
		++suppress_errors;
		ResolvedType* lhs = checkExpr(unit, ctx, *bin.lhs, nullptr);
		--suppress_errors;
		// typeless struct literals are not allowed in operators
		// so we can't use structs as hints
		const bool lhs_is_struct = lhs && lhs->kind == ResolvedType::STRUCT;
		ResolvedType* rhs = checkExpr(unit, ctx, *bin.rhs, lhs_is_struct ? nullptr : (lhs ? lhs : hint));
		if (!rhs) return nullptr;
		if (bin.op == Token::IS && rhs->kind != ResolvedType::META && bin.rhs->kind == Expression::TYPE_LITERAL) {
			MetaType* meta = makeType<MetaType>(unit.arena);
			meta->inner = rhs;
			rhs = meta;
			bin.rhs->resolved_type = meta;
		}
		// Retry the lhs once the rhs gives it a non-struct contextual type.
		// try to use rhs's type as hint for lhs, e.g. 2 * v, or .Idle == e
		if (!lhs) lhs = checkExpr(unit, ctx, *bin.lhs, rhs->kind == ResolvedType::STRUCT ? nullptr : rhs);
		if (!lhs) return nullptr;
		if (bin.op == Token::IS) {
			if (lhs->kind != ResolvedType::UNION || rhs->kind != ResolvedType::META) {
				errorLine(expr.token, "Union membership test requires a union value and member type");
				return nullptr;
			}
			ResolvedType* member = unwrapMeta(rhs);
			for (ResolvedType* candidate : static_cast<UnionResolvedType*>(lhs)->members) {
				if (typesEqual(candidate, member)) {
					expr.resolved_type = primitiveType(ResolvedType::BOOL);
					expr.eval_stage = bin.lhs->eval_stage == Expression::COMPTIME_VALUE
						? Expression::COMPTIME_VALUE
						: Expression::RUNTIME;
					return expr.resolved_type;
				}
			}
			errorLine(expr.token, "Type ", member, " is not a member of union ", lhs);
			return nullptr;
		}
		if (bin.op == Token::EQUAL_EQUAL || bin.op == Token::BANG_EQUAL) {
			const bool lhs_type_value = lhs->kind == ResolvedType::META
				|| bin.lhs->kind == Expression::TYPE_LITERAL || bin.lhs->kind == Expression::TYPEOF
				|| bin.lhs->kind == Expression::RESOLVED_TYPE || bin.lhs->kind == Expression::ARRAY_TYPE
				|| bin.lhs->kind == Expression::SLICE_TYPE || bin.lhs->kind == Expression::NULLABLE_TYPE
				|| bin.lhs->kind == Expression::FUNCTION_TYPE || bin.lhs->kind == Expression::UNION_TYPE;
			const bool rhs_type_value = rhs->kind == ResolvedType::META
				|| bin.rhs->kind == Expression::TYPE_LITERAL || bin.rhs->kind == Expression::TYPEOF
				|| bin.rhs->kind == Expression::RESOLVED_TYPE || bin.rhs->kind == Expression::ARRAY_TYPE
				|| bin.rhs->kind == Expression::SLICE_TYPE || bin.rhs->kind == Expression::NULLABLE_TYPE
				|| bin.rhs->kind == Expression::FUNCTION_TYPE || bin.rhs->kind == Expression::UNION_TYPE;
			if (lhs_type_value || rhs_type_value) {
				if (!lhs_type_value || !rhs_type_value) {
					errorLine(expr.token, "Type equality requires two compile-time type values");
					return nullptr;
				}
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			if (lhs == &module.type_kind && rhs == &module.type_kind
				&& bin.lhs->eval_stage != Expression::RUNTIME
				&& bin.rhs->eval_stage != Expression::RUNTIME) {
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
		}

		// operator overload, at least one of operands must be struct
		if (lhs_is_struct || rhs->kind == ResolvedType::STRUCT) {
			if (!operatorSymbolName(bin.op)) {
				errorLine(expr.token, "Operator `", bin.token.value, "` can not be applied to a struct type (", lhs, " and ", rhs, ")");
				return nullptr;
			}
			ResolvedType* overload_result = nullptr;
			FunctionExpression* overload_fn = nullptr;
			Expression* bin_operands[2] = {bin.lhs, bin.rhs};

			ResolvedType* operand_types[2] = {lhs, rhs};
			StructExpression* host =
				lhs->kind == ResolvedType::STRUCT
					? static_cast<StructResolvedType*>(lhs)->decl
					: static_cast<StructResolvedType*>(rhs)->decl;
			OverloadResult bin_overload = resolveOperatorOverload(unit, ctx, bin.op, 2, bin_operands, operand_types, *host, overload_result, overload_fn);
			// An overload call never folds, but a comptime-only operand still has to
			// propagate so the use site rejects it.
			const Expression::EvalStage overload_stage = combineEvalStages(*bin.lhs, *bin.rhs) == Expression::COMPTIME_ONLY
				? Expression::COMPTIME_ONLY
				: Expression::RUNTIME;
			switch (bin_overload) {
				case OverloadResult::FOUND: bin.resolved_fn = overload_fn; expr.resolved_type = overload_result; expr.eval_stage = overload_stage; return overload_result;
				case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(bin.op), " overload"); return nullptr;
				case OverloadResult::FAILED: return nullptr;
				case OverloadResult::NOT_FOUND: errorLine(expr.token, "No matching operator ", operatorSymbolName(bin.op), " overload"); return nullptr;
			}
		}

		// Resolve a numeric operator: unify the operands and pin both to the result type.
		enum class NumericMode { ARITHMETIC, INTEGER, COMPARISON };
		auto resolveNumeric = [&](NumericMode mode) -> ResolvedType* {
			if (!isNumericOrUntyped(*lhs) || !isNumericOrUntyped(*rhs)) {
				errorLine(expr.token, "Operator ", operatorSymbolName(bin.op), " expects numeric operands, got ", lhs, " and ", rhs);
				return nullptr;
			}
			ResolvedType* unified = unifyNumeric(*lhs, *rhs);
			if (mode == NumericMode::INTEGER && (!unified || !isIntegerOrUntyped(*unified))) {
				errorLine(expr.token, "Operator ", operatorSymbolName(bin.op), " expects integer operands, got ", lhs, " and ", rhs);
				return nullptr;
			}
			if (!unified) {
				if ((isFloatType(*lhs) && isIntegerType(*rhs)) || (isIntegerType(*lhs) && isFloatType(*rhs))) {
					errorLine(expr.token, "Cannot mix integer and float operands with operator ", operatorSymbolName(bin.op), ", got ", lhs, " and ", rhs);
					return nullptr;
				}
				errorLine(expr.token, "Cannot apply operator ", operatorSymbolName(bin.op), " to ", lhs, " and ", rhs);
				return nullptr;
			}

			if (mode != NumericMode::COMPARISON && isUntypedNumeric(*unified)) {
				return unified;
			}
			ResolvedType* concrete = unified;
			if (mode == NumericMode::COMPARISON) {
				if (unified->kind == ResolvedType::UNTYPED_INT)
					concrete = primitiveType(ResolvedType::I32);
				else if (unified->kind == ResolvedType::UNTYPED_FLOAT)
					concrete = primitiveType(ResolvedType::F64);
			}
			if (!makeConcrete(*bin.lhs, concrete) || !makeConcrete(*bin.rhs, concrete)) return nullptr;
			return concrete;
		};

		ResolvedType* result = nullptr;
		switch (bin.op) {
			case Token::PLUS:
				[[fallthrough]];
			case Token::MINUS:
			case Token::STAR:
			case Token::SLASH:
				// Arithmetic keeps the result untyped when both operands are untyped.
				result = resolveNumeric(NumericMode::ARITHMETIC);
				break;
			case Token::PERCENT: result = resolveNumeric(NumericMode::INTEGER); break;
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL: {
				// Equality also works on non-numerics such as enums; only unify when numeric.
				if (isNumericOrUntyped(*lhs) || isNumericOrUntyped(*rhs)) {
					if (!resolveNumeric(NumericMode::COMPARISON)) return nullptr;
				} else if (lhs->kind == ResolvedType::SLICE || rhs->kind == ResolvedType::SLICE) {
					// Checked before typesEqual because that treats `[]T` and
					// `[]const T` as distinct types, which equality does not.
					if (!sliceTypesComparable(lhs, rhs)) {
						errorLine(expr.token, "Cannot compare ", lhs, " and ", rhs);
						return nullptr;
					}
				} else if (!typesEqual(lhs, rhs)) {
					errorLine(expr.token, "Cannot compare ", lhs, " and ", rhs);
					return nullptr;
				} else {
					// Equality is defined only for kinds with a well-defined comparison:
					// value kinds compare bitwise and cstr/cptr by
					// address. Nullable values compare only against the null literal.
					// Aggregates (arrays, slices, nullables) have no equality.
					bool comparable = false;
					switch (lhs->kind) {
						case ResolvedType::BOOL:
						case ResolvedType::ENUM:
						case ResolvedType::CSTR:
						case ResolvedType::CPTR:
						case ResolvedType::BYTE:
						case ResolvedType::FUNCTION:
							comparable = true;
							break;
						case ResolvedType::NULLABLE:
							comparable = (bin.rhs && bin.rhs->kind == Expression::NULL_LITERAL)
								|| (bin.lhs && bin.lhs->kind == Expression::NULL_LITERAL);
							break;
						default:
							break;
					}
					if (!comparable) {
						errorLine(expr.token, "Cannot compare ", lhs, " and ", rhs);
						return nullptr;
					}
				}
				result = primitiveType(ResolvedType::BOOL);
				break;
			}
			case Token::LT:
			case Token::LT_EQUAL:
			case Token::GT:
			case Token::GT_EQUAL:
				if (!resolveNumeric(NumericMode::COMPARISON)) return nullptr;
				result = primitiveType(ResolvedType::BOOL);
				break;
			case Token::AND:
			case Token::OR:
				if (!typesEqual(lhs, primitiveType(ResolvedType::BOOL)) || !typesEqual(rhs, primitiveType(ResolvedType::BOOL))) {
					errorLine(expr.token, "Logical operator requires bool operands, got ", lhs, " and ", rhs);
					return nullptr;
				}
				result = primitiveType(ResolvedType::BOOL);
				break;
			default:
				ASSERT(false); 
				return nullptr;
		}
		
		if (!result) return nullptr;
		expr.resolved_type = result;
		expr.eval_stage = combineEvalStages(*bin.lhs, *bin.rhs);
		return result;
	}

	ResolvedType* checkTernaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		TernaryExpression& tern = static_cast<TernaryExpression&>(expr);

		ResolvedType* cond = checkExpr(unit, ctx, *tern.condition, nullptr);
		if (!cond) return nullptr;

		if (!typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
			errorLine(expr.token, "Ternary condition must be bool, got ", cond);
			return nullptr;
		}

		ResolvedType* true_type = checkExpr(unit, ctx, *tern.true_expr, hint);
		if (!true_type) return nullptr;

		ResolvedType* false_type = checkExpr(unit, ctx, *tern.false_expr, hint);
		if (!false_type) return nullptr;

		// The hint only reaches literals directly, so a branch can stay untyped while the other
		// is concrete, e.g. `c ? x : 2 + 3`. Pin the untyped one to the concrete one.
		if (isUntypedNumeric(*true_type) != isUntypedNumeric(*false_type)) {
			if (isUntypedNumeric(*true_type)) {
				true_type = makeConcrete(*tern.true_expr, false_type);
				if (!true_type) return nullptr;
			}
			else {
				false_type = makeConcrete(*tern.false_expr, true_type);
				if (!false_type) return nullptr;
			}
		}
		else if (isUntypedNumeric(*true_type) && isUntypedNumeric(*false_type)) {
			// both branches untyped - result is untyped. int & float -> float, e.g. 2.0 & 3 -> float
			if (true_type->kind == ResolvedType::UNTYPED_FLOAT || false_type->kind == ResolvedType::UNTYPED_FLOAT) {
				true_type = false_type = primitiveType(ResolvedType::UNTYPED_FLOAT);
			}
			else {
				true_type = false_type = primitiveType(ResolvedType::UNTYPED_INT);
			}
			tern.true_expr->resolved_type = true_type;
			tern.false_expr->resolved_type = false_type;
		}


		if (!typesEqual(true_type, false_type)) {
			errorLine(expr.token, "Ternary branches have different types: ", true_type, " and ", false_type);
			return nullptr;
		}

		expr.resolved_type = true_type;
		expr.eval_stage = tern.condition->eval_stage == Expression::COMPTIME_VALUE
			&& tern.true_expr->eval_stage == Expression::COMPTIME_VALUE
			&& tern.false_expr->eval_stage == Expression::COMPTIME_VALUE
			? Expression::COMPTIME_VALUE
			: Expression::RUNTIME;
		return true_type;
	}

	ResolvedType* checkCastExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		CastExpression& cast = static_cast<CastExpression&>(expr);
		ResolvedType* dst_type = asType(evalComptime(unit, *cast.type_expr), cast.type_expr->token);
		if (!dst_type) return nullptr;

		// Don't pass dst_type as hint: explicit casts allow out-of-range values and
		// the operand resolves independently (e.g. `-1 as u8` should work).
		ResolvedType* src_type = checkExpr(unit, ctx, *cast.expression, nullptr);
		if (!src_type) return nullptr;
		// The source is consumed as a value here; pin an untyped literal to its i32 default
		// (an explicit cast then converts it, so no range check against the destination).
		if (isUntypedNumeric(*src_type)) {
			src_type = makeConcrete(*cast.expression, nullptr);
			if (!src_type) return nullptr;
		}
		const bool src_numeric = isNumericType(*src_type);
		const bool dst_numeric = isNumericType(*dst_type);
		const bool src_integer = isIntegerType(*src_type);
		const bool dst_integer = isIntegerType(*dst_type);
		const bool src_enum = src_type->kind == ResolvedType::ENUM;
		const bool dst_enum = dst_type->kind == ResolvedType::ENUM;
		// Slice reinterpret cast: `[]byte as []T` / `[]T as []byte`. Exactly one side must
		// have `byte` elements; reinterpreting between two unrelated typed slices is rejected.
		bool slice_reinterpret = false;
		if (src_type->kind == ResolvedType::SLICE && dst_type->kind == ResolvedType::SLICE) {
			ResolvedType* src_elem = static_cast<SliceResolvedType*>(src_type)->element_type;
			ResolvedType* dst_elem = static_cast<SliceResolvedType*>(dst_type)->element_type;
			const bool src_byte = src_elem && src_elem->kind == ResolvedType::BYTE;
			const bool dst_byte = dst_elem && dst_elem->kind == ResolvedType::BYTE;
			slice_reinterpret = src_byte != dst_byte;
		}
		// bool->bool (and any other same-type cast) is covered by the trailing typesEqual.
		const bool valid_cast = (src_numeric && dst_numeric) || (src_enum && dst_integer) || (src_integer && dst_enum) || slice_reinterpret || typesEqual(src_type, dst_type);
		if (!valid_cast) {
			errorLine(expr.token, "Cannot cast ", src_type, " to ", dst_type);
			return nullptr;
		}
		expr.resolved_type = dst_type;
		expr.eval_stage = cast.expression->eval_stage == Expression::COMPTIME_VALUE
			? Expression::COMPTIME_VALUE
			: Expression::RUNTIME;
		return dst_type;
	}

	ResolvedType* checkTypeMemberExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		TypeMemberExpression& member = static_cast<TypeMemberExpression&>(expr);
		// Type-member access may follow any compile-time expression producing a
		// type. Fall back to normal expression typing for value receivers.
		++suppress_errors;
		ResolvedType* t = asType(evalComptime(unit, *member.expression, ctx), member.expression->token);
		--suppress_errors;
		if (!t) t = checkExpr(unit, ctx, *member.expression, nullptr);
		if (!t) return nullptr;

		if (t->kind == ResolvedType::META) {
			ComptimeValue reflected = evalComptime(unit, *member.expression, ctx);
			t = asType(reflected, member.expression->token);
			if (!t) return nullptr;
		}

		member.reflected_type = t;
		expr.eval_stage = Expression::COMPTIME_ONLY;

		switch (member.kind) {
			case TypeMemberExpression::KIND: {
				expr.resolved_type = &module.type_kind;
				return expr.resolved_type;
			}
			case TypeMemberExpression::NAME: {
				member.comptime_string = reflectedTypeName(unit, *t);
				expr.resolved_type = const_u8_slice;
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case TypeMemberExpression::TYPES: {
				ResolvedType* utype = t;
				if (utype->kind != ResolvedType::UNION) {
					errorLine(expr.token, "::types requires a union type");
					return nullptr;
				}
				expr.resolved_type = slice_of_types;
				return slice_of_types;
			}
			case TypeMemberExpression::FIELDS: {
				if (t->kind != ResolvedType::STRUCT) {
					errorLine(expr.token, "::fields requires a struct type");
					return nullptr;
				}
				expr.resolved_type = slice_of_fields;
				return expr.resolved_type;
			}
			case TypeMemberExpression::VALUES: {
				if (t->kind != ResolvedType::ENUM) {
					errorLine(expr.token, "::values requires an enum type");
					return nullptr;
				}
				EnumResolvedType& en = *static_cast<EnumResolvedType*>(t);
				const char* names[] = {"name", "value"};
				ResolvedType* types[] = {const_u8_slice, &en};
				StructResolvedType* descriptor_type = makeStructType(unit.arena, names, types, 2);
				SliceResolvedType* slice_type = makeType<SliceResolvedType>(unit.arena);
				slice_type->element_type = descriptor_type;
				expr.resolved_type = slice_type;
				return expr.resolved_type;
			}
			case TypeMemberExpression::PARAMS: {
				if (t->kind != ResolvedType::FUNCTION) {
					errorLine(expr.token, "::params requires a function type");
					return nullptr;
				}
				expr.resolved_type = slice_of_params;
				return expr.resolved_type;
			}
			case TypeMemberExpression::CHILD: {
				ResolvedType* child = nullptr;
					switch (t->kind) {
					case ResolvedType::NULLABLE: child = static_cast<NullableResolvedType*>(t)->inner; break;
					case ResolvedType::SLICE: child = static_cast<SliceResolvedType*>(t)->element_type; break;
					case ResolvedType::ARRAY: child = static_cast<ArrayResolvedType*>(t)->element_type; break;
					case ResolvedType::POINTER: child = static_cast<PointerResolvedType*>(t)->inner; break;
					default: 
						errorLine(expr.token, "::child requires a nullable, pointer, slice, or array type");
						return nullptr;
				}
				MetaType* meta = makeType<MetaType>(unit.arena);
				meta->inner = child;
				expr.resolved_type = meta;
				return meta;
			}
			case TypeMemberExpression::LENGTH: {
				if (t->kind != ResolvedType::ARRAY) {
					errorLine(expr.token, "::length requires an array type");
					return nullptr;
				}
				expr.resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case TypeMemberExpression::MAX:
			case TypeMemberExpression::MIN: {
				if (!isNumericType(*t) && t->kind != ResolvedType::BYTE) {
					errorLine(expr.token, "::", (member.kind == TypeMemberExpression::MIN ? "min" : "max"), " requires a numeric type");
					return nullptr;
				}
				expr.resolved_type = primitiveType(isFloatType(*t) ? ResolvedType::UNTYPED_FLOAT : ResolvedType::UNTYPED_INT);
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case TypeMemberExpression::RET: {
				if (t->kind != ResolvedType::FUNCTION) {
					errorLine(expr.token, "::ret requires a function type");
					return nullptr;
				}
				MetaType* meta = makeType<MetaType>(unit.arena);
				meta->inner = static_cast<FunctionResolvedType*>(t)->return_type;
				expr.resolved_type = meta;
				return meta;
			}
		}
		ASSERT(false);
		return {};
	}

	bool hasEnumMember(const EnumResolvedType& type, ls_string_view name) {
		for (const EnumMember& member : type.decl->members) {
			if (equalStrings(member.name, name)) return true;
		}
		return false;
	}

	ResolvedType* checkMemberExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		MemberExpression& member = static_cast<MemberExpression&>(expr);

		// .enum_member
		if (!member.expression) {
			if (!hint) {
				errorLine(expr.token, "Cannot resolve .", member.name, ", use EnumName.value syntax or provide a hint");
				return nullptr;
			}

			hint = unwrapNullable(hint);
			if (hint->kind != ResolvedType::ENUM) {
				errorLine(expr.token, "Cannot convert .", member.name, " to ", hint);
				return nullptr;
			}

			EnumResolvedType* en = static_cast<EnumResolvedType*>(hint);
			if (hasEnumMember(*en, member.name)) {
				expr.resolved_type = en;
				expr.eval_stage = hint == &module.type_kind ? Expression::COMPTIME_ONLY : Expression::COMPTIME_VALUE;
				return en;
			}

			errorLine(expr.token, ".", member.name, " not found in ", hint);
			return nullptr;
		}

		// namespace.member
		if (member.expression->kind == Expression::IDENTIFIER) {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(member.expression);
			if (Unit* imported_unit = findImportedUnitByAlias(unit, id->name)) {
				SymbolRef sym = resolveSymbol(*imported_unit, {}, member.name, LookupPolicy::Checked);
				if (!sym.symbol) {
					errorLine(expr.token, member.name, " not found in ", id->name);
					return nullptr;
				}
				if (sym.check_failed) return nullptr;

				expr.resolved_type = sym.symbol->resolved_type;
				expr.eval_stage = sym.symbol->storage == Symbol::COMPTIME ? comptimeStageForType(expr.resolved_type) : Expression::RUNTIME;
				member.resolved_symbol = sym.symbol;
				if (sym.symbol->expression && sym.symbol->expression->kind == Expression::FUNCTION) {
					member.resolved_fn = static_cast<FunctionExpression*>(sym.symbol->expression);
				}
				return expr.resolved_type;
			}
		}

		// value.member
		ResolvedType* base_type = checkExpr(unit, ctx, *member.expression, nullptr);
		if (!base_type) return nullptr;

		if (equalStrings(member.name, makeStringView("length"))
			&& (base_type->kind == ResolvedType::ARRAY || base_type->kind == ResolvedType::SLICE)) {
			expr.resolved_type = primitiveType(ResolvedType::ISIZE);
			expr.eval_stage = base_type->kind == ResolvedType::ARRAY || member.expression->eval_stage != Expression::RUNTIME
				? Expression::COMPTIME_VALUE
				: Expression::RUNTIME;
			if (expr.eval_stage != Expression::RUNTIME) {
				++suppress_errors;
				ComptimeValue value = evalComptime(unit, expr, ctx);
				--suppress_errors;
				if (value) expr.comptime_value = value;
			}
			return expr.resolved_type;
		}

		if (base_type->kind == ResolvedType::POINTER) {
			PointerResolvedType* pointer = static_cast<PointerResolvedType*>(base_type);
			base_type = pointer->inner;
		}

		switch (base_type->kind) {
			case ResolvedType::STRUCT: {
				// struct.field
				StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
				for (i32 i = 0; i < st->decl->fields.size(); ++i) {
					const NamedDecl& field = st->decl->fields[i];
					if (!equalStrings(field.name, member.name)) continue;

					ResolvedType* field_type = structFieldType(*st, i);
					expr.resolved_type = field_type;
					if (member.expression->eval_stage != Expression::RUNTIME) {
						++suppress_errors;
						ComptimeValue value = evalComptime(unit, expr, ctx);
						--suppress_errors;
						if (value) {
							expr.comptime_value = value;
							expr.eval_stage = comptimeStageForType(field_type);
						}
					}
					return expr.resolved_type;
				}
				errorLine(expr.token, member.name, " not found in ", base_type);
				return nullptr;
			}
			case ResolvedType::META: {
				// TypeName.member - only enums support member access through a type name
				ResolvedType* inner = static_cast<MetaType*>(base_type)->inner;
				if (inner->kind == ResolvedType::ENUM) {
					EnumResolvedType* en = static_cast<EnumResolvedType*>(inner);
					if (hasEnumMember(*en, member.name)) {
						expr.resolved_type = inner;
						expr.eval_stage = Expression::COMPTIME_VALUE;
						return inner;
					}
					errorLine(expr.token, member.name, " not found in ", inner);
					return nullptr;
				}
				errorLine(expr.token, "Cannot access member '", member.name, "' on type");
				return nullptr;
			}
			case ResolvedType::ENUM: {
				// If the name matches a variant, the user wrote instance.Variant - give a clear error.
				// Otherwise return nullptr silently so the call checker can try UFCS.
				EnumResolvedType* en = static_cast<EnumResolvedType*>(base_type);
				if (hasEnumMember(*en, member.name)) {
					errorLine(expr.token, "Cannot access enum member '", member.name, "' through an instance; use the enum type name instead");
				}
				return nullptr;
			}
			case ResolvedType::NULLABLE:
				errorLine(expr.token, "Cannot access member ", member.name, " of nullable type without a null check");
				return nullptr;
			default:
				errorLine(expr.token, "Cannot access member ", member.name, " on type ", base_type);
				return nullptr;
		}
	}

	bool checkIndexableBase(const Token& token, ResolvedType* base_type) {
		if (base_type->kind == ResolvedType::NULLABLE) {
			errorLine(token, "Cannot index nullable type without a null check");
			return false;
		}
		if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) {
			errorLine(token, "Cannot index type ", base_type);
			return false;
		}
		return true;
	}

	ResolvedType* checkBracketExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BracketExpression& br = static_cast<BracketExpression&>(expr);
		++suppress_errors;
		bool type_member_value_index = false;
		ResolvedType* template_type = nullptr;
		if (br.base->kind == Expression::TYPE_MEMBER) {
			switch (static_cast<TypeMemberExpression*>(br.base)->kind) {
				case TypeMemberExpression::FIELDS:
				case TypeMemberExpression::VALUES:
				case TypeMemberExpression::TYPES:
				case TypeMemberExpression::PARAMS:
					break;
				default: template_type = asType(evalComptime(unit, expr), expr.token); break;
			}
		}
		--suppress_errors;
		if (template_type) {
			// templates
			expr.resolved_type = template_type;
			return template_type;
		}

		ResolvedType* base_type = checkExpr(unit, ctx, *br.base, nullptr);
		if (!base_type) return nullptr;

		if (base_type->kind == ResolvedType::STRUCT) {
			if (br.args.size() != 1) {
				errorLine(expr.token, "Struct field access expects exactly one argument");
				return nullptr;
			}

			ResolvedType* index_type = checkExpr(unit, ctx, *br.args[0], const_u8_slice);
			if (!index_type) return nullptr;
			if (!typesEqual(index_type, const_u8_slice)) {
				errorLine(expr.token, "Struct field access expects a compile-time []const u8, got ", index_type);
				return nullptr;
			}

			ComptimeValue index = evalComptime(unit, *br.args[0], ctx);
			if (!index) return nullptr;

			ComptimeSliceValue field_name_value;
			copyMemory(&field_name_value, index.value, sizeof(field_name_value));
			if (!field_name_value.data) {
				errorLine(expr.token, "Struct field access expects a valid compile-time []const u8");
				return nullptr;
			}
			const ls_string_view field_name{(const char*)field_name_value.data, (const char*)field_name_value.data + field_name_value.count};
			StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
			for (i32 i = 0; i < st->decl->fields.size(); ++i) {
				if (equalStrings(st->decl->fields[i].name, field_name)) {
					br.struct_field_name = st->decl->fields[i].name;
					expr.resolved_type = structFieldType(*st, i);
					return expr.resolved_type;
				}
			}
			errorLine(expr.token, field_name, " not found in ", base_type);
			return nullptr;
		}

		if (!checkIndexableBase(expr.token, base_type)) return nullptr;

		if (br.args.size() != 1) {
			errorLine(expr.token, "Indexing expects exactly one argument");
			return nullptr;
		}

		ResolvedType* index_type = checkExpr(unit, ctx, *br.args[0], primitiveType(ResolvedType::I32));
		if (!index_type) return nullptr;

		if (!isIntegerOrUntyped(*index_type)) {
			errorLine(expr.token, "Cannot index with type ", index_type, ", expected integer type");
			return nullptr;
		}

		if (base_type->kind == ResolvedType::ARRAY) {
			const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
			i64 index = 0;
			++suppress_errors;
			const bool is_comptime = evalComptimeIntValue(unit, br.args[0], index);
			--suppress_errors;
			if (is_comptime) {
				if (index < 0 || index >= arr->size) {
					errorLine(expr.token, "Array index out of bounds: ", index, " (array size: ", arr->size, ")");
					return nullptr;
				}
			}
			expr.resolved_type = arr->element_type;
		} else {
			expr.resolved_type = static_cast<SliceResolvedType*>(base_type)->element_type;
		}
		if (br.base->eval_stage != Expression::RUNTIME && !isRuntimeMaterializable(*expr.resolved_type)) {
			expr.eval_stage = Expression::COMPTIME_ONLY;
		}
		else if (br.base->eval_stage != Expression::RUNTIME) expr.eval_stage = Expression::COMPTIME_VALUE;
		return expr.resolved_type;
	}

	ResolvedType* checkAddressOfExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		AddressOfExpression& addr = static_cast<AddressOfExpression&>(expr);
		bool writable = false;
		ResolvedType* base_type = checkAssignableExpr(unit, ctx, *addr.subject, writable);
		if (!base_type) return nullptr;

		PointerResolvedType* ptr = makeType<PointerResolvedType>(unit.arena);
		ptr->inner = base_type;
		ptr->is_const = !writable;
		expr.resolved_type = ptr;
		expr.eval_stage = Expression::RUNTIME;
		return ptr;
	}

	ResolvedType* checkSliceExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		SliceExpression& sl = static_cast<SliceExpression&>(expr);
		ResolvedType* base_type = checkExpr(unit, ctx, *sl.base, nullptr);
		if (!base_type) return nullptr;

		if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) {
			if (sl.begin || sl.end) {
				errorLine(expr.token, "Scalar slice views require omitted bounds");
				return nullptr;
			}
			bool writable = false;
			++suppress_errors;
			ResolvedType* source_type = checkAssignableExpr(unit, ctx, *sl.base, writable);
			--suppress_errors;
			if (!source_type) {
				errorLine(expr.token, "Cannot create a slice from non-addressable storage");
				return nullptr;
			}
			SliceResolvedType* slice = makeType<SliceResolvedType>(unit.arena);
			slice->element_type = base_type;
			slice->is_const = !writable;
			expr.resolved_type = slice;
			expr.eval_stage = Expression::RUNTIME;
			return slice;
		}

		bool source_writable = true;
		if (base_type->kind == ResolvedType::ARRAY) {
			bool writable = false;
			++suppress_errors;
			checkAssignableExpr(unit, ctx, *sl.base, writable);
			--suppress_errors;
			source_writable = writable;
		}

		Expression* bounds[] = {sl.begin, sl.end};
		for (Expression* bound : bounds) {
			if (!bound) continue;
			ResolvedType* bound_type = checkExpr(unit, ctx, *bound, primitiveType(ResolvedType::I32));
			if (!bound_type) return nullptr;

			if (!isIntegerOrUntyped(*bound_type)) {
				errorLine(expr.token, "Cannot slice with type ", bound_type, ", expected integer type");
				return nullptr;
			}
		}

		SliceResolvedType* slice = makeType<SliceResolvedType>(unit.arena);
		expr.resolved_type = slice;
		
		if (base_type->kind == ResolvedType::ARRAY) {
			const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
			i64 begin = 0;
			i64 end = arr->size;
			++suppress_errors;
			const bool has_begin = sl.begin && evalComptimeIntValue(unit, sl.begin, begin);
			const bool has_end = sl.end ? evalComptimeIntValue(unit, sl.end, end) : true;
			--suppress_errors;
			if (has_begin && (begin < 0 || begin > arr->size)) {
				errorLine(expr.token, "Array slice begin index out of bounds: ", begin, " (array size: ", arr->size, ")");
				return nullptr;
			}
			if (has_end && (end < 0 || end > arr->size)) {
				errorLine(expr.token, "Array slice end index out of bounds: ", end, " (array size: ", arr->size, ")");
				return nullptr;
			}
			if (has_begin && has_end && begin > end) {
				errorLine(expr.token, "Array slice begin index ", begin, " is greater than end index ", end);
				return nullptr;
			}
			slice->element_type = arr->element_type;
		}
		else {
			const auto* source = static_cast<SliceResolvedType*>(base_type);
			slice->element_type = source->element_type;
			slice->is_const = source->is_const;
		}
		if (base_type->kind == ResolvedType::ARRAY) slice->is_const = !source_writable;

		return slice;
	}

	ResolvedType* checkStructLiteralExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		StructLiteralExpression& lit = static_cast<StructLiteralExpression&>(expr);
		// In the type position of a literal, a top-level type name wins over a
		// same-named local value. This is also what lets `template[x](x { ... })`
		// use `x` as a type argument and as an ordinary local index nearby.
		ResolvedType* type = nullptr;
		if (lit.type) {
			type = asType(evalComptime(unit, *lit.type), lit.type->token);
			if (!type) return nullptr;
		}
		if (!type) type = hint;
		if (!type) {
			errorLine(expr.token, "Cannot resolve struct literal type");
			return nullptr;
		}
		if (type->kind != ResolvedType::STRUCT) {
			errorLine(expr.token, "Expected struct type, got ", type);
			return nullptr;
		}
		if (lit.type) lit.type->resolved_type = type;
		StructResolvedType* st = static_cast<StructResolvedType*>(type);
		if (st->decl->fields.size() != lit.values.size()) {
			errorLine(expr.token, "Struct literal has ", lit.values.size(), " values, but struct type has ", st->decl->fields.size(), " fields");
			return nullptr;
		}
		for (i32 i = 0; i < lit.values.size(); ++i) {
			ResolvedType* field_type = structFieldType(*st, i);
			ASSERT(field_type);
			ResolvedType* value_type = checkExprForTarget(unit, ctx, *lit.values[i], field_type);
			if (!value_type) return nullptr;

			if (!canImplicitlyConvert(value_type, field_type)) {
				errorLine(expr.token, "Cannot convert struct literal value ", i, " from ", value_type, " to ", field_type);
				return nullptr;
			}
		}
		expr.resolved_type = type;
		expr.eval_stage = comptimeStageForType(type);
		for (Expression* value : lit.values) if (value->eval_stage == Expression::RUNTIME) expr.eval_stage = Expression::RUNTIME;
		return type;
	}

	ResolvedType* checkArrayLiteralExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		ArrayLiteralExpression& lit = static_cast<ArrayLiteralExpression&>(expr);
		ResolvedType* expected_element = nullptr;
		if (hint && hint->kind == ResolvedType::ARRAY) {
			ArrayResolvedType& array = static_cast<ArrayResolvedType&>(*hint);
			if (array.size != lit.values.size()) {
				errorLine(expr.token, "Array literal has ", lit.values.size(), " values, but array type has ", array.size, " elements");
				return nullptr;
			}
			expected_element = array.element_type;
		}
		else if (hint && hint->kind == ResolvedType::SLICE) {
			expected_element = static_cast<SliceResolvedType*>(hint)->element_type;
		}
		if (lit.values.empty()) {
			errorLine(expr.token, "Array literal cannot be empty");
			return nullptr;
		}

		ResolvedType* element_type = expected_element;
		for (Expression* value : lit.values) {
			ResolvedType* value_type = checkExprForTarget(unit, ctx, *value, element_type);
			if (!value_type) return nullptr;
			
			if (!element_type) element_type = value_type;
			if (!canImplicitlyConvert(value_type, element_type)) {
				errorLine(expr.token, "Cannot convert array literal element from ", value_type, " to ", element_type);
				return nullptr;
			}
		}

		ArrayResolvedType* array = makeType<ArrayResolvedType>(unit.arena);
		array->element_type = element_type;
		array->size = lit.values.size();
		expr.resolved_type = array;
		expr.eval_stage = comptimeStageForType(array);
		for (Expression* value : lit.values) if (value->eval_stage == Expression::RUNTIME) expr.eval_stage = Expression::RUNTIME;
		return array;
	}

	ResolvedType* checkIdentifierExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr) {
	IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
		if (id.slot && id.slot->type) {
			expr.resolved_type = id.slot->type;
			expr.eval_stage = Expression::RUNTIME;
			return expr.resolved_type;
		}
		if (ctx) {
			if (SemanticLocalBinding* local = findLocal(*ctx, id.name); local && (!id.slot || local->slot == id.slot)) {
				id.symbol = nullptr;
				id.slot = local->slot;
				if (local->is_comptime) {
					expr.comptime_value = local->comptime_value;
					if (local->comptime_value.kind == ComptimeValue::VALUE) {
						u32 size = comptimeSize(*local->type);
						id.comptime_bytes = (u8*)unit.arena.allocate(unit.arena.user_data, size, 1);
						memcpy(id.comptime_bytes, local->comptime_value.value, size);
					}
				}
				expr.resolved_type = local->type;
				expr.eval_stage = local->is_comptime ? comptimeStageForType(local->type) : Expression::RUNTIME;
				return expr.resolved_type;
			}
		}
		SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked, first_arg_type);
		if (ref.ambiguous) {
			// TODO list collisions
			errorLine(expr.token, "Ambiguous identifier ", id.name);
			return nullptr;
		}
		if (!ref) {
			errorLine(expr.token, "Unknown identifier ", id.name);
			return nullptr;
		}
		id.symbol = ref.symbol;

		/* handle something like
			fn identity(a : $T) : T { return a; }
			fn get_identity() : fn(i32) : i32 { return identity; }
		*/
		if (hint && hint->kind == ResolvedType::FUNCTION && ref.symbol->expression && ref.symbol->expression->kind == Expression::FUNCTION) {
			FunctionExpression* fn = static_cast<FunctionExpression*>(ref.symbol->expression);
			if (fn->is_template) {
				FunctionResolvedType& target = *static_cast<FunctionResolvedType*>(hint);
				TemplateBindings bindings(ref.owner->arena);
				if (fn->params.size() != target.params.size()) {
					errorLine(expr.token, "Mismatched number of parameters for function template : expected ", target.params.size(), ", got ", fn->params.size());
					return nullptr;
				}
				for (FunctionParam& param : fn->params) {
					u32 target_param_index = u32(&param - fn->params.data());
					if (param.is_comptime) {
						errorLine(expr.token, "Cannot infer template argument for comptime parameter ", param.name);
						return nullptr;
					}
				if (!inferTemplateArg(*ref.owner, bindings, *param.type_expr, ComptimeValue{ComptimeValue::TYPE, target.params[target_param_index].type})) {
						errorLine(expr.token, "Cannot infer template argument for parameter ", param.name);
						return nullptr;
					}
				}
				if (!inferTemplateArg(*ref.owner, bindings, *fn->return_type, ComptimeValue{ComptimeValue::TYPE, target.return_type})) {
					errorLine(expr.token, "Cannot infer return type for function template");
					return nullptr;
				}
				FunctionExpression* instance = instantiateFunctionTemplate(*ref.owner, *fn, bindings);
				if (!instance) return nullptr;

				FunctionResolvedType* instance_type = asFunctionType(instance->resolved_type);
				if (!(instance_type && typesEqual(instance_type, &target))) {
					errorLine(expr.token, "Function template instantiation does not match the expected function type");
					return nullptr;
				}
				id.resolved_fn = instance;
				expr.resolved_type = instance->resolved_type;
				return expr.resolved_type;
			}
		}

		if (symbolHasGlobalStorage(*ref.symbol)) id.slot = &ref.symbol->slot;
		// Reflection lengths are untyped compile-time integers. Keep the symbol's
		// default type, but allow each use to adopt its numeric context.
		if (hint && isIntegerType(*hint) && ref.symbol->storage == Symbol::COMPTIME
			&& ref.symbol->expression->kind == Expression::TYPE_MEMBER
			&& static_cast<TypeMemberExpression*>(ref.symbol->expression)->kind == TypeMemberExpression::LENGTH) {
			expr.resolved_type = hint;
			expr.eval_stage = Expression::COMPTIME_VALUE;
			return expr.resolved_type;
		}
		expr.resolved_type = ref.symbol->resolved_type;
		expr.eval_stage = ref.symbol->storage == Symbol::COMPTIME
			? comptimeStageForType(expr.resolved_type)
			: Expression::RUNTIME;
		return expr.resolved_type;
	}

	ResolvedType* checkExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr) {
		switch (expr.kind) {
			case Expression::INT_LITERAL: {
				expr.resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case Expression::FLOAT_LITERAL: {
				expr.resolved_type = primitiveType(ResolvedType::UNTYPED_FLOAT);
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case Expression::SIZEOF: {
				SizeofExpression& sz = static_cast<SizeofExpression&>(expr);
				if (!resolveSizeofValue(unit, sz, ctx)) return nullptr;

				ResolvedType* int_hint = unwrapNullable(hint);
				if (int_hint && isNumericType(*int_hint)) {
					if (!intLiteralFitsType(sz.value, int_hint->kind)) {
						errorLine(expr.token, "Constant does not fit in ", int_hint);
						return nullptr;
					}
					expr.resolved_type = int_hint;
				} else {
					expr.resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
				}
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case Expression::UNDEFINED: expr.resolved_type = hint; expr.eval_stage = Expression::RUNTIME; return expr.resolved_type;
			case Expression::BOOL_LITERAL: expr.resolved_type = primitiveType(ResolvedType::BOOL); expr.eval_stage = Expression::COMPTIME_VALUE; return expr.resolved_type;
			case Expression::STRING_LITERAL: {
				StringLiteralExpression& literal = static_cast<StringLiteralExpression&>(expr);
				if (hint && hint->kind == ResolvedType::CSTR) {
					if (contains(literal.value, '\0')) {
						errorLine(expr.token, "String literal containing a null byte cannot convert to cstr");
						return nullptr;
					}
					expr.resolved_type = hint;
				}
				else expr.resolved_type = const_u8_slice;
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			}
			case Expression::NULL_LITERAL:
				if (!hint) {
					errorLine(expr.token, "Cannot use null literal without a type hint");
					return nullptr;
				}
				if (hint->kind != ResolvedType::NULLABLE && hint->kind != ResolvedType::SLICE && hint->kind != ResolvedType::CPTR) {
					errorLine(expr.token, "Cannot use null literal as ", hint);
					return nullptr;
				}
				expr.resolved_type = hint;
				expr.eval_stage = Expression::COMPTIME_VALUE;
				return expr.resolved_type;
			case Expression::RESOLVED_TYPE: expr.eval_stage = Expression::COMPTIME_ONLY; return expr.resolved_type;
			case Expression::TYPEOF: {
				TypeofExpression& typeof_expr = static_cast<TypeofExpression&>(expr);
				if (!typeof_expr.operand || typeof_expr.operand->kind == Expression::TYPE_LITERAL
					|| typeof_expr.operand->kind == Expression::UNION_TYPE
					|| typeof_expr.operand->kind == Expression::FUNCTION_TYPE
					|| typeof_expr.operand->kind == Expression::ARRAY_TYPE
					|| typeof_expr.operand->kind == Expression::SLICE_TYPE
					|| typeof_expr.operand->kind == Expression::NULLABLE_TYPE)
				{
					errorLine(expr.token, "typeof expects a value expression");
					return nullptr;
				}

				ResolvedType* operand_type = checkExpr(unit, ctx, *typeof_expr.operand, nullptr);
				if (!operand_type) return nullptr;
				if (isUntypedNumeric(*operand_type)) {
					errorLine(expr.token, "typeof cannot be applied to an untyped numeric value; cast or annotate it first");
					return nullptr;
				}
				expr.resolved_type = operand_type;
				expr.eval_stage = Expression::COMPTIME_ONLY;
				return operand_type;
			}
			case Expression::TYPE_LITERAL: {
				auto* meta = makeType<MetaType>(unit.arena);
				expr.resolved_type = meta;
				expr.eval_stage = Expression::COMPTIME_ONLY;
				const ResolvedType::Kind kind = static_cast<TypeLiteralExpression&>(expr).type;
				if (kind == ResolvedType::META) {
					// TODO what about meta->inner?
					return expr.resolved_type;
				}
				meta->inner = primitiveType(kind);
				return meta;
			}
			case Expression::UNION_TYPE:
			case Expression::ARRAY_TYPE:
			case Expression::SLICE_TYPE:
			case Expression::NULLABLE_TYPE:
			case Expression::POINTER_TYPE:
			case Expression::FUNCTION_TYPE: {
				ResolvedType* type = asType(evalComptime(unit, expr, ctx), expr.token);
				expr.resolved_type = type;
				expr.eval_stage = Expression::COMPTIME_ONLY;
				return type;
			}
			case Expression::FUNCTION: {
				// stuff like const foo = fn() : i32 { return 1; };
				FunctionExpression& fn = static_cast<FunctionExpression&>(expr);
				if (fn.resolved_type) return fn.resolved_type;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn);
				if (!fn_type) return nullptr;

				// resolved_type must be set before checkFunctionBody (it reads the return
				// type from it), but a body checked under suppressed errors that fails must
				// not stay cached: clearing it lets a later non-suppressed check re-run the
				// body and surface the real diagnostic.
				expr.resolved_type = fn_type;
				if (!checkFunctionBody(unit, fn)) {
					expr.resolved_type = nullptr;
					return nullptr;
				}
				return fn_type;
			}
			case Expression::STRUCT: {
				StructExpression& st = static_cast<StructExpression&>(expr);
				if (expr.resolved_type) return expr.resolved_type;
				st.cached_owner = &unit;
				StructResolvedType* st_type = makeType<StructResolvedType>(unit.arena, unit.arena);
				st_type->decl = &st;
				if (!resolveStructFields(unit, nullptr, st, *st_type)) return nullptr;
				MetaType* meta = makeType<MetaType>(unit.arena);
				meta->inner = st_type;
				expr.resolved_type = meta;
				expr.eval_stage = Expression::COMPTIME_ONLY;
				return meta;
			}
			case Expression::IDENTIFIER: return checkIdentifierExpr(unit, ctx, expr, hint, first_arg_type);
			case Expression::CALL: return checkCallExpr(unit, ctx, expr);
			case Expression::UNARY: return checkUnaryExpr(unit, ctx, expr, hint);
			case Expression::BINARY: return checkBinaryExpr(unit, ctx, expr, hint);
			case Expression::TERNARY: return checkTernaryExpr(unit, ctx, expr, hint);
			case Expression::CAST: return checkCastExpr(unit, ctx, expr);
			case Expression::MEMBER: return checkMemberExpr(unit, ctx, expr, hint);
			case Expression::TYPE_MEMBER: return checkTypeMemberExpr(unit, ctx, expr);
			case Expression::BRACKET: return checkBracketExpr(unit, ctx, expr, hint);
			case Expression::SLICE: return checkSliceExpr(unit, ctx, expr);
			case Expression::STRUCT_LITERAL: return checkStructLiteralExpr(unit, ctx, expr, hint);
			case Expression::ARRAY_LITERAL: return checkArrayLiteralExpr(unit, ctx, expr, hint);
			case Expression::DEREFERENCE: {
				auto& deref = static_cast<DereferenceExpression&>(expr);
				ResolvedType* pointer_type = checkExpr(unit, ctx, *deref.subject, nullptr);
				if (!pointer_type) return nullptr;
				
				if (pointer_type->kind != ResolvedType::POINTER) {
					errorLine(expr.token, "Cannot dereference non-pointer type ", pointer_type);
					return nullptr;
				}
				expr.resolved_type = static_cast<PointerResolvedType*>(pointer_type)->inner;
				return expr.resolved_type;
			}
			case Expression::ADDRESSOF: return checkAddressOfExpr(unit, ctx, expr);
			default:
				errorLine(expr.token, "Cannot resolve expression of kind ", expr.kind);
				return nullptr;
		}
	}

	ResolvedType* checkAssignableExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, bool& is_writable) {
		switch (expr.kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id.name); local && (!id.slot || local->slot == id.slot)) {
						is_writable = !local->is_immutable;
						id.slot = local->slot;
						expr.resolved_type = local->type;
						return local->type;
					}
				}
				if (id.slot && id.slot->type) {
					is_writable = id.slot->storage == StorageSlot::LOCAL;
					expr.resolved_type = id.slot->type;
					return expr.resolved_type;
				}
				// Import aliases name namespaces rather than values.  They are
				// encountered by speculative compile-time probes, so leave them
				// unresolved without emitting a misleading diagnostic.
				if (findImportedUnitByAlias(unit, id.name)) return {};
				SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked);
				if (!ref) {
					errorLine(expr.token, "Unknown identifier ", id.name);
					is_writable = false;
					return nullptr;
				}
				id.symbol = ref.symbol;
				// TODO why is this here?
				if (symbolHasGlobalStorage(*ref.symbol)) id.slot = &ref.symbol->slot;
				is_writable = ref.symbol->storage == Symbol::VARIABLE;
				expr.resolved_type = unwrapMeta(ref.symbol->resolved_type);
				return expr.resolved_type;
			}
			case Expression::MEMBER: {
				MemberExpression& member = static_cast<MemberExpression&>(expr);
				if (!member.expression) {
					errorLine(expr.token, "Cannot resolve .", member.name);
					return nullptr;
				}
				ResolvedType* descriptor_type = member.expression->resolved_type;
				if (!descriptor_type && ctx && member.expression->kind == Expression::IDENTIFIER) {
					if (SemanticLocalBinding* local = findLocal(*ctx, static_cast<IdentifierExpression*>(member.expression)->name)) descriptor_type = local->type;
				}
				SymbolRef ref = {};
				if (member.expression->kind == Expression::IDENTIFIER) {
					ref = resolveSymbol(unit, static_cast<IdentifierExpression&>(*member.expression).name, member.name, LookupPolicy::Checked);
				}
				if (ref) {
					is_writable = ref.symbol->storage == Symbol::VARIABLE;
					expr.resolved_type = unwrapMeta(ref.symbol->resolved_type);
					return expr.resolved_type;
				}
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, *member.expression, base_writable);
				if (!base_type) {
					is_writable = false;
					return nullptr;
				}
				if (base_type->kind == ResolvedType::POINTER) {
					base_writable = !static_cast<PointerResolvedType*>(base_type)->is_const;
				}
				ResolvedType* field_type = checkExpr(unit, ctx, expr, nullptr);
				is_writable = base_writable && field_type != nullptr;
				expr.resolved_type = field_type;
				return field_type;
			}
			case Expression::DEREFERENCE: {
				auto& deref = static_cast<DereferenceExpression&>(expr);
				ResolvedType* inner_type = checkExpr(unit, ctx, *deref.subject, nullptr);
				if (!inner_type) {
					is_writable = false;
					return nullptr;
				}
				if (inner_type->kind != ResolvedType::POINTER) {
					errorLine(expr.token, "Cannot dereference non-pointer type ", inner_type);
					is_writable = false;
					return nullptr;
				}
				// TODO check pointer mutability
				is_writable = true;
				auto* ptr = static_cast<PointerResolvedType*>(inner_type);
				if (ptr->is_const) {
					errorLine(expr.token, "Expression is not assignable because it is a pointer to const");
					is_writable = false;
					return nullptr;
				}
				return ptr->inner;
			}
			case Expression::BRACKET: {
				BracketExpression& br = static_cast<BracketExpression&>(expr);
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, *br.base, base_writable);
				if (!base_type) {
					is_writable = false;
					return nullptr;
				}
				ResolvedType* value_type = checkExpr(unit, ctx, expr, nullptr);
				// A mutable slice writes through to its backing storage regardless of
				// whether the slice binding itself is immutable.
				is_writable = value_type && (base_type->kind == ResolvedType::SLICE
					? !static_cast<SliceResolvedType*>(base_type)->is_const
					: base_writable);
				expr.resolved_type = value_type;
				return value_type;
			}
			default:
				errorLine(expr.token, "Expression is not assignable");
				is_writable = false;
				return nullptr;
		}
	}

	static bool isPrimitiveShadowName(ls_string_view name) {
		for (i32 kind = ResolvedType::VOID; kind <= ResolvedType::CPTR; ++kind) {
			if (equalStrings(name, makeStringView(primitiveTypeName(static_cast<ResolvedType::Kind>(kind))))) return true;
		}
		return equalStrings(name, makeStringView("type"));
	}

	Unit* findImportedUnitByAlias(Unit& unit, ls_string_view alias) {
		for (const Import& import : unit.imports) {
			if (!equalStrings(import.alias, alias)) continue;
			ASSERT(import.unit);
			return import.unit;
		}
		return nullptr;
	}

	Unit* findTypeNamespaceUnit(const ResolvedType& type) {
		switch (type.kind) {
			case ResolvedType::STRUCT: return static_cast<const StructResolvedType&>(type).decl->cached_owner;
			case ResolvedType::ENUM: return static_cast<const EnumResolvedType&>(type).decl->cached_owner;
			default: return nullptr;
		}
	}

	// Conservative reachability check: true only if every path through `st`
	// is guaranteed to hit a `return`. Loops are never credited (the body may
	// run zero times) and `match` is only credited when it has a fallback arm
	// (exhaustiveness for enum-only matches is intentionally not special-cased
	// here to keep this analysis simple).
	static bool statementAlwaysReturns(Statement& st) {
		switch (st.kind) {
			case Statement::RETURN: return true;
			case Statement::BLOCK: return blockAlwaysReturns(static_cast<BlockStatement&>(st));
			case Statement::LABEL: return statementAlwaysReturns(*static_cast<LabelStatement&>(st).statement);
			case Statement::IF: {
				IfStatement& ifst = static_cast<IfStatement&>(st);
				if (ifst.comptime_known) {
					Statement* selected = ifst.comptime_value ? static_cast<Statement*>(ifst.body) : ifst.else_branch;
					return selected && statementAlwaysReturns(*selected);
				}
				if (!ifst.else_branch) return false;
				return blockAlwaysReturns(*ifst.body) && statementAlwaysReturns(*ifst.else_branch);
			}
			case Statement::MATCH: {
				MatchStatement& ms = static_cast<MatchStatement&>(st);
				if (ms.comptime_known) {
					return ms.comptime_arm >= 0 && blockAlwaysReturns(*ms.arms[(u32)ms.comptime_arm].body);
				}
				bool has_fallback = false;
				for (MatchArm& arm : ms.arms) {
					if (arm.is_fallback) has_fallback = true;
					if (!blockAlwaysReturns(*arm.body)) return false;
				}
				return has_fallback || (ms.subject && ms.subject->resolved_type
					&& (ms.subject->resolved_type->kind == ResolvedType::UNION
						|| ms.subject->resolved_type->kind == ResolvedType::ENUM));
			}
			default: return false;
		}
	}

	static bool blockAlwaysReturns(BlockStatement& block) {
		for (Statement* st : block.statements) {
			if (statementAlwaysReturns(*st)) return true;
		}
		return false;
	}

	// True when every path exits the loop containing the statement.  This is
	// intentionally conservative: a loop body may run zero times, but a
	// branch inside the body can still make the statements after that branch
	// reachable only through its other arm.
	static bool statementAlwaysExitsLoop(Statement& st) {
		switch (st.kind) {
			case Statement::BREAK:
			case Statement::CONTINUE: return true;
			case Statement::BLOCK: {
				BlockStatement& block = static_cast<BlockStatement&>(st);
				for (Statement* child : block.statements) {
					if (statementAlwaysExitsLoop(*child)) return true;
				}
				return false;
			}
			case Statement::LABEL: return statementAlwaysExitsLoop(*static_cast<LabelStatement&>(st).statement);
			case Statement::IF: {
				IfStatement& ifst = static_cast<IfStatement&>(st);
				return ifst.else_branch
					&& statementAlwaysExitsLoop(*ifst.body)
					&& statementAlwaysExitsLoop(*ifst.else_branch);
			}
			default: return false;
		}
	}

	bool checkFunctionBody(Unit& unit, FunctionExpression& fn) {
		if (!fn.body) return true;
		ASSERT(fn.body->kind == Statement::BLOCK);

		ResolvedType* return_type = static_cast<FunctionResolvedType*>(fn.resolved_type)->return_type;
		ASSERT(return_type);
		FunctionCheckContext ctx(unit.arena); // TODO reuse?
		pushScope(ctx);
		for (FunctionParam& param : fn.params) {
			if (findSymbol(unit, param.name)) {
				errorLine(fn.token, "Parameter ", param.name, " shadows a global symbol");
				return false;
			}
			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = param.name;
			binding.type = param.resolved_type;
			binding.is_immutable = !param.is_ref;
			binding.slot = &param.slot;
		}

		BlockStatement* body = static_cast<BlockStatement*>(fn.body);
		for (Statement* st : body->statements) {
			if (!checkStatement(unit, ctx, st, return_type, {})) return false;
		}

		if (return_type->kind != ResolvedType::VOID && !blockAlwaysReturns(*body)) {
			errorLine(fn.token, "Function must return a value on all code paths");
			return false;
		}
		return true;
	}

	static bool checkLabelTarget(FunctionCheckContext& ctx, ls_string_view label) {
		if (empty(label)) return !ctx.loop_labels.empty();
		for (i32 i = (i32)ctx.loop_labels.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.loop_labels[(u32)i], label)) return true;
		}
		return false;
	}

	bool checkVarDeclStatement(Unit& unit, FunctionCheckContext& ctx, VarDeclStatement& var, ResolvedType* return_type) {
		// TODO collision with templates
		if (findLocal(ctx, var.name)) {
			errorLine(var.token, "Variable ", var.name, " shadows an existing local or parameter");
			return false;
		}

		if (findSymbol(unit, var.name)) {
			errorLine(var.token, "Variable ", var.name, " conflicts with a symbol of the same name in the same unit");
			return false;
		}

		ResolvedType* annotation = nullptr;
		if (var.type_expr) {
			annotation = asType(evalComptime(unit, *var.type_expr, &ctx), var.type_expr->token);
			if (!annotation) {
				// TODO can we even get here?
				return false;
			}
		}
		// The parser always attaches an initializer (`var x = ...;`); there is no
		// uninitialized local form. Unlike global symbols, this path may dereference
		// it unconditionally.
		ASSERT(var.expression);
		if (var.expression->kind == Expression::UNDEFINED) {
			if (!annotation) {
				errorLine(var.token, "Variable ", var.name, " must have a type annotation if initialized with undefined");
				return false;
			}
			if (var.is_immutable) {
				errorLine(var.token, "Variable ", var.name, " cannot be immutable if initialized with undefined");
				return false;
			}
		}

		if (var.is_comptime) {
			ResolvedType* expr_type = checkExpr(unit, &ctx, *var.expression, annotation);
			if (!expr_type) return false;
			if (annotation && isUntypedNumeric(*expr_type) && !makeConcrete(*var.expression, annotation)) return false;

			ComptimeValue value = evalComptime(unit, *var.expression);
			if (!value) return false;
			if (annotation && value.kind == ComptimeValue::VALUE && (annotation->kind == ResolvedType::NULLABLE || annotation->kind == ResolvedType::UNION || isUntypedNumeric(*value.type))) {
				value = coerceComptimeValue(value, annotation);
				if (!value) {
					errorLine(var.token, "Cannot convert comptime initializer to annotated type ", annotation);
					return false;
				}
			}

			if (value.kind == ComptimeValue::TYPE) {
				auto* meta = makeType<MetaType>(unit.arena);
				meta->inner = value.type;
				var.resolved_type = meta;
			}
			else {
				var.resolved_type = annotation ? annotation : value.type;
			}

			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = var.name;
			binding.type = var.resolved_type;
			binding.is_immutable = true;
			binding.is_comptime = true;
			binding.comptime_value = value;
			return true;
		}

		ResolvedType* expr_type = checkExprForTarget(unit, &ctx, *var.expression, annotation);
		if (!expr_type) return false;

		if (!requireMaterializable(*var.expression, "a runtime variable initializer")) return false;
		if (var.else_return) {
			if (!annotation || expr_type->kind != ResolvedType::UNION) {
				errorLine(var.token, "else return requires a union initializer and an explicit target type");
				return false;
			}
			UnionResolvedType& source = static_cast<UnionResolvedType&>(*expr_type);
			u32 target_count = 0;
			for (ResolvedType* member : source.members) {
				if (typesEqual(member, annotation)) {
					++target_count;
					continue;
				}
				if (annotation->kind == ResolvedType::UNION) {
					for (ResolvedType* target_member : static_cast<UnionResolvedType&>(*annotation).members) {
						if (typesEqual(member, target_member)) { ++target_count; break; }
					}
				}
			}
			if (target_count == 0 || (annotation->kind == ResolvedType::UNION && static_cast<UnionResolvedType&>(*annotation).members.size() != target_count)) {
				errorLine(var.token, "else return target type must be a nonempty subset of initializer union");
				return false;
			}
			if (target_count == (u32)source.members.size()) {
				errorLine(var.token, "else return target type must be a proper subset of initializer union");
				return false;
			}
			ExpArray<ResolvedType*> residual(unit.arena);
			for (i32 member_index = 0; member_index < source.members.size(); ++member_index) {
				ResolvedType* member = source.members[member_index];
				bool selected = typesEqual(member, annotation);
				if (!selected && annotation->kind == ResolvedType::UNION) {
					for (ResolvedType* target_member : static_cast<UnionResolvedType&>(*annotation).members) {
						if (typesEqual(member, target_member)) { selected = true; break; }
					}
				}
				if (selected) var.else_return_target_mask |= 1ull << (u32)member_index;
				if (!selected) residual.push(member);
			}
			var.else_return_type = residual.size() == 1 ? residual[0] : getUnionType(residual);
			const bool checks_residual_return = return_type->kind == ResolvedType::STRUCT
				|| return_type->kind == ResolvedType::UNION;
			if (checks_residual_return && !canImplicitlyConvert(var.else_return_type, return_type)) {
				errorLine(var.token, "else return residual type ", var.else_return_type,
					" cannot be returned from function returning ", return_type);
				return false;
			}
		}
		if (!var.else_return && annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(var.token, "Cannot convert initializer expression of type ", expr_type, " to annotated type ", annotation);
			return false;
		}
		ResolvedType* final_type = annotation ? annotation : expr_type;
		var.resolved_type = final_type;

		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = var.name;
		binding.type = final_type;
		binding.is_immutable = var.is_immutable;
		binding.slot = &var.slot;
		return true;
	}

	bool checkAssignStatement(Unit& unit, FunctionCheckContext& ctx, AssignStatement& assign) {
		bool writable = false;
		ResolvedType* lhs_type = checkAssignableExpr(unit, &ctx, *assign.lhs, writable);
		if (!lhs_type) return false;
		if (!writable) {
			errorLine(assign.token, "Expression is immutable and cannot be assigned to");
			return false;
		}

		assign.lhs->resolved_type = lhs_type;

		const bool custom_compound = assign.op != Token::EQUAL && !isNumericType(*lhs_type);
		ResolvedType* rhs_type = custom_compound
			? checkExpr(unit, &ctx, *assign.rhs, nullptr)
			: checkExprForTarget(unit, &ctx, *assign.rhs, lhs_type);
		if (!rhs_type) return false;
		if (!requireMaterializable(*assign.rhs, "an assignment value")) return false;
		ResolvedType* assignment_target = lhs_type;
		if (assign.lhs->kind == Expression::IDENTIFIER) {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(assign.lhs);
			if (id->slot && id->slot->type && id->slot->type->kind == ResolvedType::UNION) assignment_target = id->slot->type;
			if (id->slot) {
				for (i32 i = (i32)ctx.locals.size() - 2; i >= 0; --i) {
					SemanticLocalBinding& binding = ctx.locals[(u32)i];
					if (equalStrings(binding.name, id->name) && binding.slot == id->slot && binding.type->kind == ResolvedType::UNION) {
						assignment_target = binding.type;
						break;
					}
				}
			}
		}

		if (isNumericType(*lhs_type)) {
			if (!canImplicitlyConvert(rhs_type, lhs_type)) {
				errorLine(assign.token, "Cannot convert ", rhs_type, " to ", lhs_type, " for compound assignment");
				return false;
			}
			return true;
		}

		ResolvedType* op_result = nullptr;
		switch (assign.op) {
			case Token::EQUAL:
				if (!canImplicitlyConvert(rhs_type, assignment_target)) {
					errorLine(assign.token, "Cannot convert ", rhs_type, " to ", assignment_target, " for assignment");
					return false;
				}
				return true;
			case Token::PLUS_EQUAL:
			case Token::MINUS_EQUAL:
			case Token::STAR_EQUAL:
			case Token::SLASH_EQUAL: {
				const Token::Type base_op = assign.op == Token::PLUS_EQUAL	   ? Token::PLUS
											: assign.op == Token::MINUS_EQUAL ? Token::MINUS
											: assign.op == Token::STAR_EQUAL  ? Token::STAR
																			   : Token::SLASH;
				Expression* operands[2] = {assign.lhs, assign.rhs};
				FunctionExpression* op_fn = nullptr;
				ResolvedType* operand_types[2] = {lhs_type, rhs_type};
				StructExpression* host = lhs_type->kind == ResolvedType::STRUCT ? static_cast<StructResolvedType*>(lhs_type)->decl
					: rhs_type->kind == ResolvedType::STRUCT ? static_cast<StructResolvedType*>(rhs_type)->decl : nullptr;
				if (!host) {
					errorLine(assign.token, "No matching operator overload for compound assignment on type ", lhs_type);
					return false;
				}
				switch (resolveOperatorOverload(unit, &ctx, base_op, 2, operands, operand_types, *host, op_result, op_fn)) {
					case OverloadResult::FOUND: assign.resolved_op_fn = op_fn; break;
					case OverloadResult::AMBIGUOUS: errorLine(assign.token, "Ambiguous operator overload for compound assignment on type ", lhs_type); return false;
					case OverloadResult::FAILED: return false;
					case OverloadResult::NOT_FOUND: errorLine(assign.token, "No matching operator overload for compound assignment on type ", lhs_type); return false;
				}
				assign.lhs = operands[0];
				assign.rhs = operands[1];

				if (!canImplicitlyConvert(op_result, lhs_type)) {
					// operator +(a : Vec2, b : Vec2) : i32 { return a.x + b.x; }
					// value += Vec2 { 2 };
					errorLine(assign.token, "Compound assignment operator returns ", op_result, " which cannot be implicitly converted to the target type ", lhs_type);
					return false;
				}
				return true;
			}
			default:
			// parser rejects all other operators
				ASSERT(false);
				return false;
		}
	}

	bool checkIfStatement(Unit& unit, FunctionCheckContext& ctx, IfStatement& ifst, ResolvedType* return_type) {
		ResolvedType* cond = checkExpr(unit, &ctx, *ifst.condition, primitiveType(ResolvedType::BOOL));
		if (!cond) return false;
		if (!typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
			errorLine(ifst.token, "If condition must be of type bool, got ", cond);
			return false;
		}

		// A condition made entirely from compile-time values selects one branch.
		// Suppress resolver diagnostics here because a runtime condition is valid;
		// it simply falls through to ordinary two-arm checking below.
		++suppress_errors;
		u8* comptime_eval_start = comptime_stack_ptr;
		ComptimeValue cmptime_t = evalComptime(unit, *ifst.condition, &ctx);
		--suppress_errors;
		if (!comptime_eval_start) comptime_eval_start = comptime_stack;
		if (cmptime_t && cmptime_t.kind == ComptimeValue::VALUE && cmptime_t.type->kind == ResolvedType::BOOL) {
			bool comptime_bool_value;
			memcpy(&comptime_bool_value, cmptime_t.value, sizeof(bool));
			comptime_stack_ptr = comptime_eval_start;

			ifst.comptime_known = true;
			ifst.comptime_value = comptime_bool_value;
			Statement* selected = ifst.comptime_value ? static_cast<Statement*>(ifst.body) : ifst.else_branch;
			return checkStatement(unit, ctx, selected, return_type, {});
		}
		comptime_stack_ptr = comptime_eval_start;

		// Detect `x != null` / `x == null` to narrow x inside the respective branch.
		ls_string_view narrowed_name = {};
		ResolvedType* narrowed_type = nullptr;
		ResolvedType* narrowed_member = nullptr;
		ResolvedType* subject_type = nullptr;
		bool narrowed_is_immutable = false;
		bool narrow_in_true = false;
		PointerResolvedType* narrowing_pointer = nullptr;
		StorageSlot* narrowed_slot = nullptr;
		auto bindingType = [&](ResolvedType* type) -> ResolvedType* {
			if (!narrowing_pointer) return type;
			PointerResolvedType* pointer = makeType<PointerResolvedType>(unit.arena);
			pointer->inner = type;
			pointer->is_const = narrowing_pointer->is_const;
			return pointer;
		};
		if (ifst.condition && ifst.condition->kind == Expression::BINARY) {
			BinaryExpression* bin = static_cast<BinaryExpression*>(ifst.condition);
			if (bin->op == Token::BANG_EQUAL || bin->op == Token::EQUAL_EQUAL) {
				Expression* id_side = nullptr;
				if (bin->rhs && bin->rhs->kind == Expression::NULL_LITERAL)
					id_side = bin->lhs;
				else if (bin->lhs && bin->lhs->kind == Expression::NULL_LITERAL)
					id_side = bin->rhs;
				if (id_side && id_side->kind == Expression::IDENTIFIER) {
					ResolvedType* id_type = id_side->resolved_type;
					if (id_type && id_type->kind == ResolvedType::NULLABLE) {
						IdentifierExpression* id = static_cast<IdentifierExpression*>(id_side);
						narrowed_name = id->name;
						narrowed_type = static_cast<NullableResolvedType*>(id_type)->inner;
						if (SemanticLocalBinding* local = findLocal(ctx, id->name)) {
							narrowed_is_immutable = local->is_immutable;
							narrowed_slot = local->slot;
						} else if (id->symbol) {
							narrowed_is_immutable = id->symbol->storage != Symbol::VARIABLE;
							narrowed_slot = id->slot;
						}
						narrow_in_true = (bin->op == Token::BANG_EQUAL);
					}
				}
			}
			else if (bin->op == Token::IS && bin->lhs
				&& (bin->lhs->kind == Expression::IDENTIFIER || bin->lhs->kind == Expression::DEREFERENCE)) {
				IdentifierExpression* id = nullptr;
				subject_type = bin->lhs->resolved_type;
				if (bin->lhs->kind == Expression::IDENTIFIER) {
					id = static_cast<IdentifierExpression*>(bin->lhs);
				} else {
					DereferenceExpression* deref = static_cast<DereferenceExpression*>(bin->lhs);
					if (deref->subject->kind == Expression::IDENTIFIER && deref->subject->resolved_type
						&& deref->subject->resolved_type->kind == ResolvedType::POINTER) {
						id = static_cast<IdentifierExpression*>(deref->subject);
						narrowing_pointer = static_cast<PointerResolvedType*>(id->resolved_type);
						subject_type = narrowing_pointer->inner;
					}
				}
				ResolvedType* member = bin->rhs ? unwrapMeta(bin->rhs->resolved_type) : nullptr;
				if (id && member) {
					narrowed_name = id->name;
					narrowed_member = member;
					narrowed_type = bindingType(member);
					if (SemanticLocalBinding* local = findLocal(ctx, id->name)) {
						narrowed_is_immutable = local->is_immutable;
						narrowed_slot = local->slot;
					} else if (id->symbol) {
						narrowed_is_immutable = id->symbol->storage != Symbol::VARIABLE;
						narrowed_slot = id->slot;
					}
					narrow_in_true = true;
				}
			}
		}

		ResolvedType* residual_type = nullptr;
		if (subject_type && subject_type->kind == ResolvedType::UNION && narrowed_member) {
			UnionResolvedType& subject_union = static_cast<UnionResolvedType&>(*subject_type);
			u32 residual_count = 0;
			ResolvedType* residual_member = nullptr;
			for (ResolvedType* candidate : subject_union.members) {
				if (typesEqual(candidate, narrowed_member)) continue;
				residual_member = candidate;
				++residual_count;
			}
			if (residual_count == 0) {
				errorLine(ifst.token, "Union narrowing leaves no members");
				return false;
			}
			if (residual_count == 1) {
				residual_type = residual_member;
			} else {
				ExpArray<ResolvedType*> residual_members(unit.arena);
				for (ResolvedType* candidate : subject_union.members) {
					if (!typesEqual(candidate, narrowed_member)) residual_members.push(candidate);
				}
				residual_type = getUnionType(residual_members);
			}
			residual_type = bindingType(residual_type);
		}

		auto checkBranchWithNarrowing = [&](Statement* branch, ResolvedType* branch_type) -> bool {
			if (branch && branch_type) {
				pushScope(ctx);
				SemanticLocalBinding& nb = ctx.locals.emplace_back();
				nb.name = narrowed_name;
				nb.type = branch_type;
				nb.is_immutable = narrowed_is_immutable;
				nb.slot = narrowed_slot;
				bool ok = checkStatement(unit, ctx, branch, return_type, {});
				popScope(ctx);
				return ok;
			}
			return checkStatement(unit, ctx, branch, return_type, {});
		};

		ResolvedType* true_branch_type = narrow_in_true ? narrowed_type : residual_type;
		ResolvedType* false_branch_type = narrow_in_true ? residual_type : narrowed_type;
		if (!checkBranchWithNarrowing(ifst.body, true_branch_type)) return false;
		if (!checkBranchWithNarrowing(ifst.else_branch, false_branch_type)) return false;

		// A terminating branch leaves only the type from the branch that can
		// continue.  For unions this is the residual, not the tested member.
		const bool true_exits = ifst.body && statementAlwaysReturns(*ifst.body);
		const bool false_exits = ifst.else_branch && statementAlwaysReturns(*ifst.else_branch);
		const bool true_exits_loop = ifst.body && !ctx.loop_labels.empty() && statementAlwaysExitsLoop(*ifst.body);
		const bool false_exits_loop = ifst.else_branch && !ctx.loop_labels.empty() && statementAlwaysExitsLoop(*ifst.else_branch);
		ResolvedType* continues_type = nullptr;
		if (true_exits || true_exits_loop) continues_type = false_branch_type;
		else if (false_exits || false_exits_loop) continues_type = true_branch_type;
		if (continues_type) {
			SemanticLocalBinding& nb = ctx.locals.emplace_back();
			nb.name = narrowed_name;
			nb.type = continues_type;
			nb.is_immutable = narrowed_is_immutable;
			nb.slot = narrowed_slot;
		}
		return true;
	}

	// `unroll for` over an array/slice needs the actual element expressions at
	// codegen time (each iteration is baked as a separate literal init), so trace
	// through comptime identifier indirections down to the literal that backs them.
	ArrayLiteralExpression* resolveUnrollElements(Expression& expr) {
		if (expr.kind == Expression::ARRAY_LITERAL) return static_cast<ArrayLiteralExpression*>(&expr);
		if (expr.kind != Expression::IDENTIFIER) return nullptr;
		IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
		if (!id.symbol || id.symbol->storage != Symbol::COMPTIME || !id.symbol->expression) return nullptr;
		return resolveUnrollElements(*id.symbol->expression);
	}

	bool checkForStatement(Unit& unit, FunctionCheckContext& ctx, ForStatement& fs, ResolvedType* return_type, ls_string_view pending_label) {
		// TODO collision with templates
		// Bounds are checked without a forced hint first so an untyped bound can adopt
		// the other bound's concrete type (`for i in 0..s.length` iterates as isize).
		// Two untyped bounds default to i32.

		ResolvedType* begin_type = checkExpr(unit, &ctx, *fs.begin, nullptr);
		if (!begin_type) return false;

		ResolvedType* end_type = nullptr;

		if (fs.end) {
			end_type = checkExpr(unit, &ctx, *fs.end, isUntypedNumeric(*begin_type) ? nullptr : begin_type);
			if (end_type) {
				if (begin_type->kind == ResolvedType::UNTYPED_INT) {
					begin_type = makeConcrete(*fs.begin, isIntegerType(*end_type) ? end_type : nullptr);
				}
				if (begin_type && end_type->kind == ResolvedType::UNTYPED_INT) {
					end_type = makeConcrete(*fs.end, isIntegerType(*begin_type) ? begin_type : nullptr);
				}
			}

			if (!begin_type || !end_type || !isIntegerType(*begin_type) || !isIntegerType(*end_type)) {
				errorLine(fs.token, "For loop bounds must be of integer type, got ", begin_type, " and ", end_type);
				return false;
			}

			if (!typesEqual(begin_type, end_type)) {
				errorLine(fs.token, "For loop bounds must have the same type, got ", begin_type, " and ", end_type);
				return false;
			}
		}
		else {
			if (begin_type->kind != ResolvedType::ARRAY && begin_type->kind != ResolvedType::SLICE) {
				errorLine(fs.token, "For loop over a single bound must be an array or slice, got ", begin_type);
				return false;
			}
			if (!fs.is_unroll && (begin_type->kind == ResolvedType::ARRAY || begin_type->kind == ResolvedType::SLICE)) {
				ResolvedType* element = begin_type->kind == ResolvedType::ARRAY
					? static_cast<ArrayResolvedType*>(begin_type)->element_type
					: static_cast<SliceResolvedType*>(begin_type)->element_type;
				if (!isRuntimeMaterializable(*element) && fs.begin->eval_stage != Expression::RUNTIME) {
					errorLine(fs.token, "Runtime for cannot iterate over a compile-time-only sequence");
					return false;
				}
			}
		}

		if (fs.is_unroll) {
			++suppress_errors;
			ComptimeValue elements = evalComptime(unit, *fs.begin, &ctx);
			--suppress_errors;
			ComptimeSliceValue slice = {};
			if (!fs.end && elements.kind == ComptimeValue::VALUE && elements.type && elements.type->kind == ResolvedType::SLICE) copyMemory(&slice, elements.value, sizeof(slice));
			const bool slice_unroll = !fs.end && (elements.kind == ComptimeValue::VALUE && elements.type && elements.type->kind == ResolvedType::SLICE);
			if (slice_unroll) {
				BlockStatement* source_body = fs.body;
				BlockStatement* expanded_body = makeType<BlockStatement>(unit.arena, unit.arena);
				expanded_body->token = source_body->token;
				ctx.loop_labels.push({});
				const u32 count = (u32)slice.count;
				for (u32 i = 0; i < count; ++i) {
					ComptimeValue binding_value;
					ResolvedType* binding_type = nullptr;
					binding_type = static_cast<SliceResolvedType*>(elements.type)->element_type;
					u8* element_data = slice.data + comptimeSize(*binding_type) * i;
					if (binding_type->kind == ResolvedType::META) {
						ResolvedType* reflected_type;
						copyMemory(&reflected_type, element_data, sizeof(reflected_type));
						MetaType* concrete_meta = makeType<MetaType>(unit.arena);
						concrete_meta->inner = reflected_type;
						binding_type = concrete_meta;
						binding_value = {ComptimeValue::TYPE, reflected_type};
					} else {
						binding_value = {ComptimeValue::VALUE, binding_type, element_data};
					}
					Statement* body = cloneStatement(unit, source_body, nullptr);
					pushScope(ctx);
					if (fs.is_key_value) {
						SemanticLocalBinding& index_binding = ctx.locals.emplace_back();
						index_binding.name = fs.key_var;
						index_binding.type = primitiveType(ResolvedType::I32);
						index_binding.is_immutable = true;
						index_binding.is_comptime = true;
						i32 index = (i32)i;
						index_binding.comptime_value = copyComptimeValue(index_binding.type, &index, sizeof(index));
					}
					SemanticLocalBinding& value_binding = ctx.locals.emplace_back();
					value_binding.name = fs.value_var;
					value_binding.type = binding_type;
					value_binding.is_immutable = true;
					value_binding.is_comptime = true;
					value_binding.comptime_value = binding_value;
					bool body_ok = checkStatement(unit, ctx, body, return_type, {});
					popScope(ctx);
					if (!body_ok) return false;
					expanded_body->statements.push(body);
				}
				ctx.loop_labels.pop_back();
				fs.body = expanded_body;
				fs.is_expanded = true;
				return true;
			}
			if (fs.end) {
				if (!evalComptimeIntValue(unit, fs.begin, fs.unroll_begin) || !evalComptimeIntValue(unit, fs.end, fs.unroll_end)) {
					errorLine(fs.token, "unroll for bounds must be compile-time constant integers");
					return false;
				}
			}
			else {
				fs.unroll_elements = resolveUnrollElements(*fs.begin);
				if (!fs.unroll_elements) {
					errorLine(fs.token, "unroll for over an array or slice requires a compile-time constant array literal");
					return false;
				}
			}
		}

		// Both range loops and single-variable array/slice loops carry the
		// user-visible name in value_var (key_var is an auto-generated hidden
		// index in that case). The paired `for i, v in xs` form leaves a real
		// name in key_var too, bound to the iteration index below.
		ResolvedType* element_type = begin_type;
		if (!fs.end) {
			element_type = begin_type->kind == ResolvedType::ARRAY
				? static_cast<ArrayResolvedType*>(begin_type)->element_type
				: static_cast<SliceResolvedType*>(begin_type)->element_type;
		}

		pushScope(ctx);
		if (!fs.end && fs.is_key_value) {
			SemanticLocalBinding& key_binding = ctx.locals.emplace_back();
			key_binding.name = fs.key_var;
			key_binding.type = primitiveType(ResolvedType::ISIZE);
			key_binding.is_immutable = true;
			key_binding.slot = &fs.index_slot;
		}
		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = fs.value_var;
		binding.type = element_type;
		binding.is_immutable = true;
		binding.slot = &fs.slot;
		ctx.loop_labels.push(pending_label);
		bool ok = checkStatement(unit, ctx, fs.body, return_type, {});
		ctx.loop_labels.pop_back();
		popScope(ctx);
		return ok;
	}

	bool checkLabelStatement(Unit& unit, FunctionCheckContext& ctx, LabelStatement& label, ResolvedType* return_type) {
		for (i32 i = (i32)ctx.label_names.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.label_names[(u32)i], label.name)) {
				errorLine(label.token, "Label ", label.name, " already declared in this function"); // TODO isn't this already caught below?
				return false;
			}
		}
		const bool labeled_loop = label.statement && (label.statement->kind == Statement::WHILE || label.statement->kind == Statement::FOR);
		if (labeled_loop) {
			// Active labels catch lexical duplicates. Keep a second function-wide
			// registry because reusing a name for a different loop construct is
			// ambiguous to later control-flow lowering, while sequential loops of
			// the same construct intentionally reuse labels.
			bool known_label = false;
			for (i32 i = 0; i < ctx.declared_loop_labels.size(); ++i) {
				if (!equalStrings(ctx.declared_loop_labels[i], label.name)) continue;
				if (ctx.declared_loop_kinds[i] != label.statement->kind) {
					errorLine(label.token, "Label ", label.name, " already declared for a different loop construct");
					return false;
				}
				known_label = true;
				break;
			}
			if (!known_label) {
				ctx.declared_loop_labels.push(label.name);
				ctx.declared_loop_kinds.push(label.statement->kind);
			}
		}
		ctx.label_names.push(label.name);
		const bool ok = checkStatement(unit, ctx, label.statement, return_type, labeled_loop ? label.name : ls_string_view{});
		ctx.label_names.pop_back();
		return ok;
	}

	bool checkMatchStatement(Unit& unit, FunctionCheckContext& ctx, MatchStatement& ms, ResolvedType* return_type) {
		ResolvedType* subject = checkExprForTarget(unit, &ctx, *ms.subject, nullptr);
		if (!subject) return false;

		// Subject must be a scalar numeric type, enum, or union.
		const bool subject_is_numeric = isNumericOrUntyped(*subject);
		const bool subject_is_enum = subject->kind == ResolvedType::ENUM;
		const bool subject_is_union = subject->kind == ResolvedType::UNION;
		if (!subject_is_numeric && !subject_is_enum && !subject_is_union) {
			errorLine(ms.token, "Match statement subject must be a numeric type, enum, or union, got ", subject);
			return false;
		}
		// A compile-time subject selects one arm before checking match bodies, just
		// like a compile-time if condition selects one branch. Pattern validation
		// below still runs for every arm so exhaustiveness and duplicate checks are
		// not bypassed.
		++suppress_errors;
		u8* comptime_eval_start = comptime_stack_ptr;
		ComptimeValue comptime_subject = evalComptime(unit, *ms.subject, &ctx);
		--suppress_errors;
		if (!comptime_eval_start) comptime_eval_start = comptime_stack;
		if (comptime_subject && !subject_is_union) {
			const auto matches = [&](MatchPattern& pattern) {
				if (subject_is_enum && pattern.begin->kind == Expression::MEMBER && !static_cast<MemberExpression*>(pattern.begin)->expression) {
					const ls_string_view name = static_cast<MemberExpression*>(pattern.begin)->name;
					const EnumResolvedType* en = static_cast<const EnumResolvedType*>(subject);
					for (i32 i = 0; i < en->decl->members.size(); ++i) {
						const EnumMember& member = en->decl->members[i];
						if (!equalStrings(member.name, name)) continue;
						ComptimeValue value = member.value
							? evalComptime(unit, *member.value, &ctx)
							: makeComptimeEnumResult(const_cast<EnumResolvedType*>(en), (i64)i);
						if (!value) return false;
						if (value.type != subject) {
							i64 numeric = comptimeNumericToI64(value.value, value.type->kind);
							value = makeComptimeEnumResult(const_cast<EnumResolvedType*>(en), numeric);
						}
						return comptimeValuesEqual(comptime_subject, value);
					}
					return false;
				}
				ComptimeValue begin = evalComptime(unit, *pattern.begin, &ctx);
				if (!begin) return false;
				if (pattern.end) {
					ComptimeValue end = evalComptime(unit, *pattern.end, &ctx);
					if (!end) return false;
					if (comptime_subject.kind != ComptimeValue::VALUE || begin.kind != ComptimeValue::VALUE || end.kind != ComptimeValue::VALUE
						|| !isNumericOrUntyped(*comptime_subject.type) || !isNumericOrUntyped(*begin.type) || !isNumericOrUntyped(*end.type)) return false;
					return compareComptimeNumeric(comptime_subject, begin) >= 0 && compareComptimeNumeric(comptime_subject, end) <= 0;
				}
				return comptimeValuesEqual(comptime_subject, begin);
			};

			for (i32 i = 0; i < ms.arms.size(); ++i) {
				MatchArm& arm = ms.arms[i];
				if (arm.is_fallback) {
					if (ms.comptime_arm < 0) ms.comptime_arm = (i32)i;
					continue;
				}
				for (MatchPattern& pattern : arm.patterns) {
					if (matches(pattern)) {
						ms.comptime_arm = (i32)i;
						break;
					}
				}
				if (ms.comptime_arm == (i32)i) break;
			}
			if (ms.comptime_arm < 0) {
				comptime_stack_ptr = comptime_eval_start;
				errorLine(ms.token, "Compile-time match subject does not match any arm");
				return false;
			}
			ms.comptime_known = true;
		}
		comptime_stack_ptr = comptime_eval_start;

		bool has_fallback = false;
		// Track covered enum members for exhaustiveness checking.
		const EnumResolvedType* subject_enum = subject_is_enum ? static_cast<const EnumResolvedType*>(subject) : nullptr;
		ExpArray<bool> covered_enum_members(unit.arena);
		if (subject_enum) covered_enum_members.resize(subject_enum->decl->members.size(), false);
		u32 covered_enum_count = 0;
		const UnionResolvedType* subject_union = subject_is_union ? static_cast<const UnionResolvedType*>(subject) : nullptr;
		ExpArray<bool> covered_union_members(unit.arena);
		if (subject_union) covered_union_members.resize(subject_union->members.size(), false);
		u32 covered_union_count = 0;

		for (i32 arm_index = 0; arm_index < ms.arms.size(); ++arm_index) {
			MatchArm& arm = ms.arms[arm_index];
			if (arm.is_fallback) {
				if (has_fallback) {
					errorLine(ms.token, "Multiple fallback arms in match statement");
					return false;
				}
				has_fallback = true;
			}
			for (MatchPattern& pattern : arm.patterns) {
				if (subject_union) {
					if (pattern.end) {
						errorLine(pattern.begin->token, "Range patterns are not valid for union matches");
						return false;
					}
					ResolvedType* member = nullptr;
					if (pattern.begin->kind == Expression::IDENTIFIER) {
						const ls_string_view name = static_cast<IdentifierExpression*>(pattern.begin)->name;
						for (ResolvedType* candidate : subject_union->members) {
							ls_string_view candidate_name = {};
							if (candidate->kind == ResolvedType::STRUCT) candidate_name = static_cast<StructResolvedType*>(candidate)->decl->cached_name;
							else if (candidate->kind == ResolvedType::ENUM) candidate_name = static_cast<EnumResolvedType*>(candidate)->decl->cached_name;
							if (!equalStrings(name, candidate_name)) continue;
							if (member && !typesEqual(member, candidate)) {
								errorLine(pattern.begin->token, "Ambiguous union member type ", name);
								return false;
							}
							member = candidate;
						}
					}
					if (!member) member = asType(evalComptime(unit, *pattern.begin), pattern.begin->token);
					if (!member) return false;
					i32 member_index = -1;
					for (i32 i = 0; i < subject_union->members.size(); ++i) {
						if (typesEqual(subject_union->members[i], member)) { member_index = i; break; }
					}
					if (member_index < 0) {
						errorLine(pattern.begin->token, "Type ", member, " is not a member of union ", subject);
						return false;
					}
					if (covered_union_members[member_index]) {
						errorLine(pattern.begin->token, "Duplicate match arm for union member ", member);
						return false;
					}
					covered_union_members[member_index] = true;
					++covered_union_count;
					MetaType* meta = makeType<MetaType>(unit.arena);
					meta->inner = member;
					pattern.begin->resolved_type = meta;
					continue;
				}
				ResolvedType* begin = checkExprForTarget(unit, &ctx, *pattern.begin, subject);
				if (!begin || !typesEqual(begin, subject)) return false;
				if (pattern.end) {
					// Range patterns are only valid for numeric types.
					if (!subject_is_numeric) {
						errorLine(pattern.begin->token, "Range patterns are only valid for numeric types, got ", subject);
						return false;
					}
					ResolvedType* end = checkExprForTarget(unit, &ctx, *pattern.end, subject);
					if (!end || !typesEqual(end, subject)) return false;
				}
				// Track enum coverage and detect duplicates.
				if (subject_enum && pattern.begin && pattern.begin->kind == Expression::MEMBER) {
					MemberExpression* mem = static_cast<MemberExpression*>(pattern.begin);
					for (i32 i = 0; i < subject_enum->decl->members.size(); ++i) {
						if (!equalStrings(subject_enum->decl->members[i].name, mem->name)) continue;
						if (covered_enum_members[i]) {
							errorLine(pattern.begin->token, "Duplicate match arm for enum member ", mem->name);
							return false;
						}
						covered_enum_members[i] = true;
						++covered_enum_count;
						break;
					}
				}
			}
			if (ms.comptime_known && (i32)arm_index != ms.comptime_arm) continue;

			ResolvedType* narrowed_union_member = nullptr;
			if (subject_union && arm.patterns.size() == 1) {
				narrowed_union_member = unwrapMeta(arm.patterns[0].begin->resolved_type);
			}
			else if (subject_union && arm.is_fallback && covered_union_count == (u32)subject_union->members.size() - 1u) {
				for (i32 i = 0; i < subject_union->members.size(); ++i) {
					if (!covered_union_members[i]) { narrowed_union_member = subject_union->members[i]; break; }
				}
			}
			if (narrowed_union_member && ms.subject->kind == Expression::IDENTIFIER) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(ms.subject);
				SemanticLocalBinding* local = findLocal(ctx, id->name);
				pushScope(ctx);
				SemanticLocalBinding& narrowed = ctx.locals.emplace_back();
				narrowed.name = id->name;
				narrowed.type = narrowed_union_member;
				if (local) {
					narrowed.is_immutable = local->is_immutable;
					narrowed.slot = local->slot;
				} else {
					narrowed.is_immutable = id->symbol && id->symbol->storage != Symbol::VARIABLE;
					narrowed.slot = id->slot;
				}
				const bool ok = checkStatement(unit, ctx, arm.body, return_type, {});
				popScope(ctx);
				if (!ok) return false;
			}
			else if (!checkStatement(unit, ctx, arm.body, return_type, {})) return false;
		}

		// Enum match must cover all variants or have a fallback.
		if (subject_enum && !has_fallback) {
			if (covered_enum_count != (u32)subject_enum->decl->members.size()) {
				errorLine(ms.token, "Match statement on enum is not exhaustive");
				return false;
			}
		}
		if (subject_union && !has_fallback && covered_union_count != (u32)subject_union->members.size()) {
			errorLine(ms.token, "Match statement on union is not exhaustive");
			return false;
		}

		return true;
	}

	bool checkStatement(Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label) {
		if (!st) return true;

		switch (st->kind) {
			case Statement::BLOCK: {
				BlockStatement* block = static_cast<BlockStatement*>(st);
				pushScope(ctx);
				for (Statement* child : block->statements) {
					if (!checkStatement(unit, ctx, child, return_type, {})) {
						popScope(ctx);
						return false;
					}
				}
				popScope(ctx);
				return true;
			}
			case Statement::EXPRESSION: {
				ExpressionStatement* expr = static_cast<ExpressionStatement*>(st);
				if (!checkExpr(unit, &ctx, *expr->expression, nullptr)) return false;
				if (!requireMaterializable(*expr->expression, "a runtime expression")) return false;
				// A discarded expression has no context to pin its width; default it to i32.
				makeConcrete(*expr->expression, nullptr);
				return true;
			}
			case Statement::RETURN: {
				ReturnStatement* ret = static_cast<ReturnStatement*>(st);
				if (ctx.in_defer) {
					errorLine(ret->token, "Defer statement cannot contain a return statement");
					return false;
				}
				ASSERT(return_type);

				if (return_type->kind == ResolvedType::VOID) {
					if (ret->expression) {
						errorLine(ret->token, "Cannot return a value from a void function");
						return false;
					}
					return true;
				}
				if (!ret->expression) {
					errorLine(ret->token, "Return statement must return a value of type ", return_type);
					return false;
				}
				ResolvedType* expr_type = checkExprForTarget(unit, &ctx, *ret->expression, return_type);
				if (!expr_type) return false;
				if (return_type->kind != ResolvedType::META && !requireMaterializable(*ret->expression, "a return value")) return false;
				if (!canImplicitlyConvert(expr_type, return_type)) {
					errorLine(ret->token, "Cannot convert return expression of type ", expr_type, " to function return type ", return_type);
					return false;
				}
				return true;
			}
			case Statement::WHILE: {
				WhileStatement* ws = static_cast<WhileStatement*>(st);
				ResolvedType* cond = checkExpr(unit, &ctx, *ws->condition, primitiveType(ResolvedType::BOOL));
				if (!cond || !typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
					errorLine(ws->token, "While condition must be of type bool, got ", cond);
					return false;
				}
				ctx.loop_labels.push(pending_label);
				bool ok = checkStatement(unit, ctx, ws->body, return_type, {});
				ctx.loop_labels.pop_back();
				return ok;
			}
			case Statement::BREAK:
			case Statement::CONTINUE: {
				BreakStatement* br = static_cast<BreakStatement*>(st);
				ls_string_view label = st->kind == Statement::BREAK ? br->label : static_cast<ContinueStatement*>(st)->label;
				if (!checkLabelTarget(ctx, label)) {
					errorLine(br->token, "No matching loop to label ", label);
					return false;
				}
				return true;
			}
			case Statement::DEFER: {
				DeferStatement* df = static_cast<DeferStatement*>(st);
				++ctx.in_defer;
				bool ok = checkStatement(unit, ctx, df->statement, return_type, {});
				--ctx.in_defer;
				return ok;
			}
			case Statement::FOR: return checkForStatement(unit, ctx, static_cast<ForStatement&>(*st), return_type, pending_label);
			case Statement::VAR_DECL: return checkVarDeclStatement(unit, ctx, static_cast<VarDeclStatement&>(*st), return_type);
			case Statement::ASSIGN: return checkAssignStatement(unit, ctx, static_cast<AssignStatement&>(*st));
			case Statement::IF: return checkIfStatement(unit, ctx, static_cast<IfStatement&>(*st), return_type);
			case Statement::LABEL: return checkLabelStatement(unit, ctx, static_cast<LabelStatement&>(*st), return_type);
			case Statement::MATCH: return checkMatchStatement(unit, ctx, static_cast<MatchStatement&>(*st), return_type);
			default: return false;
		}
	}

	static inline const char builtin_math_source[] = R"(
		extern fn sin(v : f32) : f32;
		extern fn cos(v : f32) : f32;
		extern fn sqrt(v : f32) : f32;
		extern fn sin_f64(v : f64) : f64;
		extern fn cos_f64(v : f64) : f64;
		extern fn sqrt_f64(v : f64) : f64;
		extern fn pow(v : f32, exponent : f32) : f32;
		extern fn pow_f64(v : f64, exponent : f64) : f64;
	)";

	static inline const char builtin_mem_source[] = R"(
		extern fn alloc(size : isize, align : isize) : []byte;
		extern fn free(memory : []byte) : void;
	)";

	bool resolveImportsForUnit(Unit& unit, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
		if (unit.import_state == Unit::IMPORT_DONE) return true;
		if (unit.import_state == Unit::IMPORT_RESOLVING) {
			errorLine({}, "Import cycle detected: ", unit.path);
			return false;
		}
		// This is the gray state of a depth-first traversal; reaching it again through
		// an import edge identifies a cycle, while IMPORT_DONE permits shared imports.
		unit.import_state = Unit::IMPORT_RESOLVING;

		// Check for duplicate aliases within this unit.
		for (i32 i = 0; i < unit.imports.size(); ++i) {
			const Import& a = unit.imports[i];
			for (i32 j = i + 1; j < unit.imports.size(); ++j) {
				const Import& b = unit.imports[j];
				if (!empty(a.alias) && equalStrings(a.alias, b.alias)) {
					errorLine({}, "Duplicate import alias: ", a.alias);
					return false;
				}
				if (equalStrings(a.path, b.path)) {
					errorLine({}, "Duplicate import: ", a.path);
					return false;
				}
			}
		}

		for (i32 i = 0; i < unit.imports.size(); ++i) {
			Import& import = unit.imports[i];
			Unit* imported = import.unit;
			if (!imported) {
				for (Unit& u : module.units) {
					if (equalStrings(u.path, import.path)) {
						imported = &u;
						break;
					}
				}
			}
			if (!imported) {
				ls_string_view source = {};
				if (equalStrings(import.path, makeStringView("std:math"))) {
					source = makeStringView(builtin_math_source);
				} else if (equalStrings(import.path, makeStringView("std:mem"))) {
					source = makeStringView(builtin_mem_source);
				} else {
					if (!import_resolver) {
						errorLine({}, "No import resolver for: ", import.path);
						return false;
					}
					if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
						errorLine({}, "Import not found: ", import.path);
						return false;
					}
				}
				if (ls_module_parse(&module, source, import.path) == LS_RESULT_FAILURE) return false;
				imported = &module.units.back();
			}
			import.unit = imported;
			if (!resolveImportsForUnit(*imported, import_resolver, import_resolver_userdata)) return false;
		}

		unit.import_state = Unit::IMPORT_DONE;
		return true;
	}

	ls_result typecheck() {
		// Phase 1: resolve operator signatures and attach to host structs without checking
		// bodies. Operator bodies may call other operators, so all attachments must be
		// complete before any body is checked.
		for (Unit& unit : module.units) {
			for (Symbol& sym : unit.symbols) {
				const Token::Type op = tokenFromOperatorName(sym.name);
				if (op == Token::ERROR) continue;
				if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				if (fn.is_template) continue;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn);
				if (!fn_type) return LS_RESULT_FAILURE;
				for (const FunctionResolvedParam& param : fn_type->params) {
					if (param.type->kind != ResolvedType::STRUCT) continue;
					static_cast<StructResolvedType*>(param.type)->decl->operators.push({op, &fn});
					break;
				}
			}
		}
		// Phase 2: check all symbols including operator bodies.
		for (Unit& unit : module.units) {
			for (Symbol& sym : unit.symbols) {
				if (checkSymbol(unit, sym) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
			}
		}
		// Phase 3: promote template function instances into unit.symbols so the
		// bytecode compiler can iterate a single list. ExpArray bins are stable, so
		// pointers into the array remain valid after appending new entries.
		for (Unit& unit : module.units) {
			const i32 n = unit.symbols.size();
			for (i32 i = 0; i < n; ++i) {
				Symbol& sym = unit.symbols[i];
				if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				if (!fn.is_template) continue;
				for (TemplateFunctionInstance& instance : fn.template_function_instances) {
					Symbol& new_sym = unit.symbols.emplace_back();
					new_sym.storage = Symbol::COMPTIME;
					new_sym.check_state = Symbol::CHECKED;
					new_sym.expression = instance.instance;
					new_sym.resolved_type = instance.type;
				}
			}
		}
		return LS_RESULT_OK;
	}

	ls_result checkSymbol(Unit& unit, Symbol& sym) {
		if (sym.check_state == Symbol::CHECKED) return LS_RESULT_OK;
		if (sym.check_state == Symbol::FAILED) return LS_RESULT_FAILURE;

		if (sym.storage == Symbol::IMPORT) {
			sym.check_state = Symbol::CHECKED;
			return LS_RESULT_OK;
		}

		if (sym.storage == Symbol::COMPTIME && isPrimitiveShadowName(sym.name)) {
			errorLine(sym.token, "Can not shadow primitive type: ", sym.name);
			sym.check_state = Symbol::FAILED;
			return LS_RESULT_FAILURE;
		}

		if (sym.check_state == Symbol::CHECKING) {
			// Recursive functions publish a provisional type while their bodies are being checked.
			// Struct declarations publish a provisional meta-type before field resolution.
			if (sym.expression && sym.resolved_type) {
				if (sym.expression->kind == Expression::FUNCTION) return LS_RESULT_OK;
				if (sym.expression->kind == Expression::STRUCT && sym.resolved_type->kind == ResolvedType::META) return LS_RESULT_OK;
			}

			errorLine(sym.token, "Cyclic definition: ", sym.name);
			sym.check_state = Symbol::FAILED;
			return LS_RESULT_FAILURE;
		}

		sym.check_state = Symbol::CHECKING;

		// Publish a function symbol's signature before any expression or body is
		// checked: identifier resolution reads sym.resolved_type directly, so
		// recursive and mutual references only resolve if the type is available up
		// front. An explicit annotation wins so e.g. nullable function globals stay
		// nullable.
		if (sym.expression && sym.expression->kind == Expression::FUNCTION) {
			FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
			if (!fn.is_template) {
				// TODO what's going on on the next line?
				ResolvedType* annotation = sym.storage == Symbol::COMPTIME ? nullptr : (sym.type_expr ? asType(evalComptime(unit, *sym.type_expr), sym.type_expr->token) : nullptr);
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn);
				if (!fn_type) {
					sym.check_state = Symbol::FAILED;
					return LS_RESULT_FAILURE;
				}
				sym.resolved_type = annotation ? annotation : fn_type;
			}
		}

		if (sym.storage == Symbol::COMPTIME) {
			if (checkComptimeSymbol(unit, sym) == LS_RESULT_FAILURE) {
				sym.check_state = Symbol::FAILED;
				return LS_RESULT_FAILURE;
			}
		} else {
			if (checkRuntimeSymbol(unit, sym) == LS_RESULT_FAILURE) {
				sym.check_state = Symbol::FAILED;
				return LS_RESULT_FAILURE;
			}
		}

		sym.check_state = Symbol::CHECKED;
		return LS_RESULT_OK;
	}

	ResolvedType* asType(ComptimeValue result, Token token) {
		if (result.kind == ComptimeValue::FAILURE) return nullptr;
		if (result.kind == ComptimeValue::TYPE) return result.type;
		errorLine(token, "Expected a type, got a value of type ", result.type);
		return nullptr;
	}

	template <typename T> ResolvedType* getPrimitiveType();
	template <> ResolvedType* getPrimitiveType<i8>() { return primitiveType(ResolvedType::I8); }
	template <> ResolvedType* getPrimitiveType<i16>() { return primitiveType(ResolvedType::I16); }
	template <> ResolvedType* getPrimitiveType<i32>() { return primitiveType(ResolvedType::I32); }
	template <> ResolvedType* getPrimitiveType<i64>() { return primitiveType(ResolvedType::I64); }
	template <> ResolvedType* getPrimitiveType<u8>() { return primitiveType(ResolvedType::U8); }
	template <> ResolvedType* getPrimitiveType<u16>() { return primitiveType(ResolvedType::U16); }
	template <> ResolvedType* getPrimitiveType<u32>() { return primitiveType(ResolvedType::U32); }
	template <> ResolvedType* getPrimitiveType<u64>() { return primitiveType(ResolvedType::U64); }
	template <> ResolvedType* getPrimitiveType<f32>() { return primitiveType(ResolvedType::F32); }
	template <> ResolvedType* getPrimitiveType<f64>() { return primitiveType(ResolvedType::F64); }
	template <> ResolvedType* getPrimitiveType<bool>() { return primitiveType(ResolvedType::BOOL); }

	template <typename T>
	ComptimeValue makeComptimeResult(T value, u8* address) {
		memcpy(address, &value, sizeof(value));
		comptime_stack_ptr = address + sizeof(value);
		return {ComptimeValue::VALUE, getPrimitiveType<T>(), address};
	}

	ComptimeValue copyComptimeValue(ResolvedType* type, const void* bytes, u32 size) {
		u8* value = comptime_stack_ptr;
		memcpy(value, bytes, size);
		comptime_stack_ptr += size;
		return {ComptimeValue::VALUE, type, value};
	}

	ComptimeValue copyComptimeValue(ResolvedType* type, const void* bytes) {
		return copyComptimeValue(type, bytes, comptimeSize(*type));
	}

	ComptimeValue makeUntypedIntResult(u64 value) {
		return copyComptimeValue(primitiveType(ResolvedType::UNTYPED_INT), &value);
	}

	ComptimeValue makeUntypedFloatResult(f64 value) {
		return copyComptimeValue(primitiveType(ResolvedType::UNTYPED_FLOAT), &value);
	}

	ComptimeValue makeComptimeEnumResult(ResolvedType* type, i64 value) {
		i32 enum_value = (i32)value;
		return copyComptimeValue(type, &enum_value, sizeof(enum_value));
	}

	template <typename Storage, typename Arithmetic>
	bool applyComptimeCompoundAssignment(Token token, Token::Type op, u8* lhs_bytes, const u8* rhs_bytes) {
		Storage lhs_value;
		Storage rhs_value;
		memcpy(&lhs_value, lhs_bytes, sizeof(lhs_value));
		memcpy(&rhs_value, rhs_bytes, sizeof(rhs_value));
		Arithmetic lhs = (Arithmetic)lhs_value;
		const Arithmetic rhs = (Arithmetic)rhs_value;
		switch (op) {
			case Token::PLUS_EQUAL: lhs += rhs; break;
			case Token::MINUS_EQUAL: lhs -= rhs; break;
			case Token::STAR_EQUAL: lhs *= rhs; break;
			case Token::SLASH_EQUAL:
				if (rhs == 0) {
					errorLine(token, "Division by zero");
					return false;
				}
				lhs /= rhs;
				break;
			default: ASSERT(false); return false;
		}
		lhs_value = (Storage)lhs;
		memcpy(lhs_bytes, &lhs_value, sizeof(lhs_value));
		return true;
	}

	ComptimeValue evalComptime(Unit& unit, Statement& statement, ComptimeFrame& frame) {
		switch (statement.kind) {
			case Statement::BREAK:
			case Statement::CONTINUE:
			case Statement::LABEL:
			case Statement::MATCH:
			case Statement::EXPRESSION: {
				errorLine(statement.token, "Comptime evaluation of statement kind ", statement.kind, " not implemented yet");
				return {};
			}
			case Statement::FOR: {
				ForStatement& fs = static_cast<ForStatement&>(statement);
				ASSERT(fs.begin);

				// Array/slice unroll loops use the resolved source literal. Keep the
				// loop bindings in the frame and overwrite their storage for each copy.
				if (!fs.end) {
					if (!fs.unroll_elements) {
						errorLine(fs.token, "Comptime for loop requires an unrolled array or range");
						return {};
					}
					ResolvedType* container_type = fs.begin->resolved_type;
					if (!container_type || (container_type->kind != ResolvedType::ARRAY && container_type->kind != ResolvedType::SLICE)) {
						errorLine(fs.begin->token, "Comptime array for loop requires an array or slice");
						return {};
					}
					ResolvedType* element_type = container_type->kind == ResolvedType::ARRAY
						? static_cast<ArrayResolvedType*>(container_type)->element_type
						: static_cast<SliceResolvedType*>(container_type)->element_type;
					ComptimeFrame::Local& value_binding = frame.locals.emplace_back();
					value_binding.name = fs.value_var;
					value_binding.type = element_type;
					const u32 element_size = typeByteSize(*element_type);
					value_binding.bytes = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, element_size, 1));
					ComptimeFrame::Local* key_binding = nullptr;
					if (fs.is_key_value) {
						key_binding = &frame.locals.emplace_back();
						key_binding->name = fs.key_var;
						key_binding->type = primitiveType(ResolvedType::ISIZE);
						key_binding->bytes = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, sizeof(i64), alignof(i64)));
					}

					for (i32 i = 0; i < fs.unroll_elements->values.size(); ++i) {
						ComptimeValue element = evalComptime(unit, *fs.unroll_elements->values[i], nullptr, nullptr, &frame);
						if (!element) return element;
						if (element.kind != ComptimeValue::VALUE || !canImplicitlyConvert(element.type, element_type)) {
							errorLine(fs.body->token, "Cannot convert comptime for element to ", element_type);
							return {};
						}
						memcpy(value_binding.bytes, element.value, element_size);
						if (key_binding) {
							i64 index = i;
							memcpy(key_binding->bytes, &index, sizeof(index));
						}
						ComptimeValue body = evalComptime(unit, *fs.body, frame);
						if (!body) return body;
						if (body.kind != ComptimeValue::VOID) return body;
					}
					return {ComptimeValue::VOID};
				}

				ComptimeValue begin = evalComptime(unit, *fs.begin, nullptr, nullptr, &frame);
				if (!begin) return begin;
				ComptimeValue end = evalComptime(unit, *fs.end, nullptr, nullptr, &frame);
				if (!end) return end;
				if (begin.kind != ComptimeValue::VALUE || end.kind != ComptimeValue::VALUE
					|| !isIntegerOrUntyped(*begin.type) || !isIntegerOrUntyped(*end.type)) {
					errorLine(fs.token, "Comptime for loop bounds must be integer values");
					return {};
				}
				i64 begin_value = comptimeNumericToI64(begin.value, begin.type->kind);
				i64 end_value = comptimeNumericToI64(end.value, end.type->kind);
				ResolvedType* loop_type = fs.begin->resolved_type;
				if (!loop_type || !isIntegerType(*loop_type)) loop_type = primitiveType(ResolvedType::I64);
				ComptimeFrame::Local& value_binding = frame.locals.emplace_back();
				value_binding.name = fs.value_var;
				value_binding.type = loop_type;
				value_binding.bytes = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, typeByteSize(*loop_type), 1));
				ComptimeFrame::Local* key_binding = nullptr;
				if (fs.is_key_value) {
					key_binding = &frame.locals.emplace_back();
					key_binding->name = fs.key_var;
					key_binding->type = primitiveType(ResolvedType::ISIZE);
					key_binding->bytes = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, sizeof(i64), alignof(i64)));
				}
				for (i64 value = begin_value; value < end_value; ++value) {
					const u64 bits = static_cast<u64>(value);
					writeComptimeNumeric(value_binding.bytes, (const u8*)&bits, ResolvedType::U64, loop_type->kind);
					if (key_binding) memcpy(key_binding->bytes, &value, sizeof(value));
					ComptimeValue body = evalComptime(unit, *fs.body, frame);
					if (!body) return body;
					if (body.kind != ComptimeValue::VOID) return body;
				}
				return {ComptimeValue::VOID};
			}
			case Statement::WHILE: {
				WhileStatement& ws = static_cast<WhileStatement&>(statement);
				while (true) {
					ComptimeValue cond = evalComptime(unit, *ws.condition, nullptr, nullptr, &frame);
					if (!cond) return cond;

					if (cond.kind != ComptimeValue::VALUE || cond.type->kind != ResolvedType::BOOL) {
						errorLine(ws.condition->token, "Comptime while condition must be a boolean value, got ", cond.type);
						return {};
					}
					if (!*(bool*)cond.value) break;
					ComptimeValue body = evalComptime(unit, *ws.body, frame);
					if (!body) return body;
				}
				return {ComptimeValue::VOID};
			}
			case Statement::ASSIGN: {
				AssignStatement& assign = static_cast<AssignStatement&>(statement);
				ComptimeValue value = evalComptime(unit, *assign.rhs, nullptr, nullptr, &frame);
				if (!value) return value;

				u8* lhs_bytes = nullptr;
				ResolvedType* lhs_type = nullptr;
				if (!resolveComptimeLValue(unit, *assign.lhs, frame, lhs_bytes, lhs_type)) {
					errorLine(assign.lhs->token, "Comptime assignment lhs is not writable");
					return {};
				}

				if (!canImplicitlyConvert(value.type, lhs_type)) {
					errorLine(assign.lhs->token, "Cannot assign value of type ", value.type, " to comptime variable of type ", lhs_type);
					return {};
				}
				if (assign.op == Token::EQUAL) {
					memcpy(lhs_bytes, value.value, typeByteSize(*lhs_type));
					return {ComptimeValue::VOID};
				}
				if (!isNumericType(*lhs_type) || !isNumericType(*value.type)) {
					errorLine(assign.token, "Comptime compound assignment requires numeric values");
					return {};
				}
				ASSERT(typesEqual(lhs_type, value.type));
				bool applied = false;
				switch (lhs_type->kind) {
					case ResolvedType::I8: applied = applyComptimeCompoundAssignment<i8, i64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::I16: applied = applyComptimeCompoundAssignment<i16, i64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::I32: applied = applyComptimeCompoundAssignment<i32, i64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::I64:
					case ResolvedType::ISIZE: applied = applyComptimeCompoundAssignment<i64, i64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::U8:
					case ResolvedType::BYTE: applied = applyComptimeCompoundAssignment<u8, u64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::U16: applied = applyComptimeCompoundAssignment<u16, u64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::U32: applied = applyComptimeCompoundAssignment<u32, u64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::U64: applied = applyComptimeCompoundAssignment<u64, u64>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::F32: applied = applyComptimeCompoundAssignment<f32, f32>(assign.token, assign.op, lhs_bytes, value.value); break;
					case ResolvedType::F64: applied = applyComptimeCompoundAssignment<f64, f64>(assign.token, assign.op, lhs_bytes, value.value); break;
					default: ASSERT(false); return {};
				}
				if (!applied) return {};
				return {ComptimeValue::VOID};
			}
			case Statement::IF: {
				IfStatement& ifs = static_cast<IfStatement&>(statement);
				ComptimeValue cond = evalComptime(unit, *ifs.condition, nullptr, nullptr, &frame);
				if (!cond) return cond;
				if (cond.kind != ComptimeValue::VALUE || cond.type->kind != ResolvedType::BOOL) {
					errorLine(ifs.condition->token, "Comptime if condition must be a boolean value, got ", cond.type);
					return {};
				}
				if (*(bool*)cond.value) {
					return evalComptime(unit, *ifs.body, frame);
				} else if (ifs.else_branch) {
					return evalComptime(unit, *ifs.else_branch, frame);
				}
				return {ComptimeValue::VOID};
			}
			case Statement::VAR_DECL: {
				VarDeclStatement& decl = static_cast<VarDeclStatement&>(statement);
				ComptimeValue result = evalComptime(unit, *decl.expression, nullptr, nullptr, &frame);
				if (!result) return result;

				ComptimeFrame::Local& binding = frame.locals.emplace_back();
				binding.name = decl.name;
				binding.bytes = result.value;
				binding.value = result;
				binding.type = result.type;
				return {ComptimeValue::VOID, nullptr, nullptr};
			}
			case Statement::RETURN: {
				ReturnStatement& ret = static_cast<ReturnStatement&>(statement);
				return evalComptime(unit, *ret.expression, nullptr, nullptr, &frame);
			}
			case Statement::BLOCK: {
				BlockStatement& block = static_cast<BlockStatement&>(statement);
				for (Statement* child : block.statements) {
					ComptimeValue result = evalComptime(unit, *child, frame);
					if (!result) return result;
					if (result.kind != ComptimeValue::VOID) return result;
				}
				return {ComptimeValue::VOID};
			}
		}
		errorLine(statement.token, "Comptime evaluation of statement kind ", statement.kind, " not implemented yet");
		return {};
	}

	static u32 fieldOffset(const StructResolvedType& st, i32 field_index) {
		u32 offset = 0;
		for (i32 i = 0; i < field_index; ++i) {
			ResolvedType* ft = structFieldType(st, i);
			if (ft) offset += typeByteSize(*ft);
		}
		return offset;
	}

	bool resolveComptimeLValue(Unit& unit, Expression& expr, ComptimeFrame& frame, u8*& out_ptr, ResolvedType*& out_type) {
		switch (expr.kind) {
			case Expression::IDENTIFIER: {
				ComptimeFrame::Local* local = frame.find(static_cast<IdentifierExpression&>(expr).name);
				if (!local) return false;
				out_ptr = local->bytes;
				out_type = local->type;
				return true;
			}
			case Expression::MEMBER: {
				MemberExpression& member = static_cast<MemberExpression&>(expr);
				if (!member.expression) return false;
				u8* base_ptr = nullptr;
				ResolvedType* base_type = nullptr;
				if (!resolveComptimeLValue(unit, *member.expression, frame, base_ptr, base_type)) return false;
				if (base_type->kind != ResolvedType::STRUCT) return false;
				StructResolvedType& st = static_cast<StructResolvedType&>(*base_type);
				if (!st.decl) return false;
				for (i32 i = 0; i < st.decl->fields.size(); ++i) {
					if (!equalStrings(st.decl->fields[i].name, member.name)) continue;
					ResolvedType* field_type = structFieldType(st, i);
					if (!field_type) return false;
					out_ptr = base_ptr + fieldOffset(st, i);
					out_type = field_type;
					return true;
				}
				return false;
			}
			case Expression::BRACKET: {
				BracketExpression& br = static_cast<BracketExpression&>(expr);
				if (!br.base || br.args.size() != 1) return false;
				u8* base_ptr = nullptr;
				ResolvedType* base_type = nullptr;
				if (!resolveComptimeLValue(unit, *br.base, frame, base_ptr, base_type)) return false;
				if (base_type->kind != ResolvedType::ARRAY) return false;
				ArrayResolvedType& arr = static_cast<ArrayResolvedType&>(*base_type);
				ComptimeValue index = evalComptime(unit, *br.args[0], nullptr, nullptr, &frame);
				if (!index || index.kind != ComptimeValue::VALUE || !isIntegerOrUntyped(*index.type)) return false;
				const i64 i = comptimeNumericToI64(index.value, index.type->kind);
				if (i < 0 || i >= arr.size) return false;
				out_ptr = base_ptr + (u32)i * typeByteSize(*arr.element_type);
				out_type = arr.element_type;
				return true;
			}
			default: return false;
		}
	}

	template <typename T>
	T modulo(T lhs, T rhs) {
		T result = lhs % rhs;
		if (result < 0) result += rhs;
		return result;
	}

	template <> float modulo(float lhs, float rhs) {
		errorLine({}, "Modulo operator is not defined for floating point types");
		return 0;
	}
	template <> double modulo(double lhs, double rhs) {
		errorLine({}, "Modulo operator is not defined for floating point types");
		return 0;
	}

	template <typename T>
	ComptimeValue arithmeticOp(Token::Type op, const u8* lhs_bytes, const u8* rhs_bytes, u8* address) {
		T lhs, rhs;
		memcpy(&lhs, lhs_bytes, sizeof(lhs));
		memcpy(&rhs, rhs_bytes, sizeof(rhs));
		switch (op) {
			case Token::PERCENT:
				if (rhs == 0) {
					errorLine({}, "Modulo by zero");
					return {};
				}
				return makeComptimeResult(modulo(lhs, rhs), address);
			case Token::PLUS: return makeComptimeResult((T)(lhs + rhs), address);
			case Token::MINUS: return makeComptimeResult((T)(lhs - rhs), address);
			case Token::STAR: return makeComptimeResult((T)(lhs * rhs), address);
			case Token::SLASH:
				if (rhs == 0) {
					errorLine({}, "Division by zero");
					return {};
				}
				return makeComptimeResult((T)(lhs / rhs), address);
			case Token::LT: return makeComptimeResult(lhs < rhs, address);
			case Token::LT_EQUAL: return makeComptimeResult(lhs <= rhs, address);
			case Token::GT: return makeComptimeResult(lhs > rhs, address);
			case Token::GT_EQUAL: return makeComptimeResult(lhs >= rhs, address);
			default: ASSERT(false); return {};
		}
	}

	ComptimeValue evalComptime(Unit& unit, Expression& expr, FunctionCheckContext* ctx = nullptr, TemplateBindings* bindings = nullptr, ComptimeFrame* frame = nullptr, ResolvedType* hint = nullptr) {
		// TODO free?
		if (!comptime_stack) {
			comptime_stack = (u8*)module.arena.allocate(module.arena.user_data, 1024 * 1024, 1);
			comptime_stack_ptr = comptime_stack;
		}
		switch (expr.kind) {
			case Expression::TYPE_MEMBER: {
				if (expr.comptime_value) return expr.comptime_value;
				auto& tme = static_cast<TypeMemberExpression&>(expr);
				switch (tme.kind) {
					case TypeMemberExpression::NAME: {
						// TODO double allocation
						if (empty(tme.comptime_string)) tme.comptime_string = reflectedTypeName(unit, *tme.reflected_type);
						expr.comptime_value = makeStringValue(unit, tme.comptime_string);
						return expr.comptime_value;
					}
					case TypeMemberExpression::TYPES: {
						UnionResolvedType& un = *static_cast<UnionResolvedType*>(tme.reflected_type);
						SliceResolvedType* slice_type = static_cast<SliceResolvedType*>(expr.resolved_type);
						ComptimeSliceValue slice;
						slice.data = (u8*)un.members.data();
						slice.count = un.members.size();
						expr.comptime_value = copyComptimeValue(slice_type, &slice, sizeof(slice));
						return expr.comptime_value;
					}
					case TypeMemberExpression::FIELDS: {
						StructResolvedType& st = *static_cast<StructResolvedType*>(tme.reflected_type);
						SliceResolvedType* slice_type = static_cast<SliceResolvedType*>(expr.resolved_type);
						ResolvedType* descriptor_type = slice_type->element_type;
						ComptimeSliceValue slice;
						slice.count = st.decl->fields.size();
						slice.data = comptime_stack_ptr;
						comptime_stack_ptr += st.decl->fields.size() * comptimeSize(*descriptor_type);
						expr.comptime_value = copyComptimeValue(slice_type, &slice, sizeof(slice));

						const u32 type_offset = comptimeFieldOffset(*static_cast<StructResolvedType*>(descriptor_type), 1);
						const u32 descriptor_size = comptimeSize(*descriptor_type);
						for (i32 i = 0; i < st.decl->fields.size(); ++i) {
							u8* descriptor = slice.data + descriptor_size * i;
							const ls_string_view name = st.decl->fields[i].name;
							ComptimeSliceValue name_slice{(u8*)name.begin, (i64)(name.end - name.begin)};
							ResolvedType* type = structFieldType(st, i);
							copyMemory(descriptor, &name_slice, sizeof(name_slice));
							copyMemory(descriptor + type_offset, &type, sizeof(type));
						}
						return expr.comptime_value;
					}
					case TypeMemberExpression::VALUES: {
						EnumResolvedType& en = *static_cast<EnumResolvedType*>(tme.reflected_type);
						SliceResolvedType* slice_type = static_cast<SliceResolvedType*>(expr.resolved_type);
						ResolvedType* descriptor_type = slice_type->element_type;
						ComptimeSliceValue slice;
						slice.count = en.decl->members.size();
						slice.data = (u8*)comptime_stack_ptr;
						comptime_stack_ptr += en.decl->members.size() * comptimeSize(*descriptor_type);
						expr.comptime_value = copyComptimeValue(slice_type, &slice, sizeof(slice));

						const u32 descriptor_size = comptimeSize(*descriptor_type);
						for (i32 i = 0; i < en.decl->members.size(); ++i) {
							i32 enum_value = i;
							if (en.decl->members[i].value) {
								ComptimeValue value = evalComptime(unit, *en.decl->members[i].value, ctx, bindings, frame);
								if (!value || value.kind != ComptimeValue::VALUE) return {};
								enum_value = (i32)comptimeNumericToI64(value.value, value.type->kind);
							}
							u8* descriptor = slice.data + descriptor_size * i;
							const ls_string_view name = en.decl->members[i].name;
							ComptimeSliceValue name_slice{(u8*)name.begin, (i64)(name.end - name.begin)};
							copyMemory(descriptor, &name_slice, sizeof(name_slice));
							copyMemory(descriptor + comptimeFieldOffset(*static_cast<StructResolvedType*>(descriptor_type), 1), &enum_value, sizeof(enum_value));
						}
						return expr.comptime_value;
					}
					case TypeMemberExpression::PARAMS: {
						FunctionResolvedType& fn = *static_cast<FunctionResolvedType*>(tme.reflected_type);
						SliceResolvedType* slice_type = static_cast<SliceResolvedType*>(expr.resolved_type);
						ResolvedType* descriptor_type = slice_type->element_type;
						ComptimeSliceValue slice;
						slice.count = fn.params.size();
						slice.data = comptime_stack_ptr;
						comptime_stack_ptr += fn.params.size() * comptimeSize(*descriptor_type);
						expr.comptime_value = copyComptimeValue(slice_type, &slice, sizeof(slice));

						const u32 type_offset = comptimeFieldOffset(*static_cast<StructResolvedType*>(descriptor_type), 1);
						const u32 descriptor_size = comptimeSize(*descriptor_type);
						for (i32 i = 0; i < fn.params.size(); ++i) {
							u8* descriptor = slice.data + descriptor_size * i;
							const ls_string_view name = fn.params[i].name;
							ComptimeSliceValue name_slice{(u8*)name.begin, (i64)(name.end - name.begin)};
							copyMemory(descriptor, &name_slice, sizeof(name_slice));
							copyMemory(descriptor + type_offset, &fn.params[i].type, sizeof(fn.params[i].type));
						}
						return expr.comptime_value;
					}
					case TypeMemberExpression::KIND: {
						ResolvedType::Kind kind_ = tme.reflected_type->kind;
						if (kind_ == ResolvedType::UNTYPED_INT) kind_ = ResolvedType::I32;
						if (kind_ == ResolvedType::UNTYPED_FLOAT) kind_ = ResolvedType::F64;
						i64 idx = -1;
						for (u32 i = 0; i < sizeof(TYPE_KIND_INFOS) / sizeof(TYPE_KIND_INFOS[0]); ++i) {
							if (TYPE_KIND_INFOS[i].kind == kind_) { idx = i; break; }
						}
						return makeComptimeEnumResult(&module.type_kind, idx);
					}
					case TypeMemberExpression::CHILD: {
						ResolvedType* child = nullptr;
						switch (tme.reflected_type->kind) {
							case ResolvedType::NULLABLE: child = static_cast<NullableResolvedType*>(tme.reflected_type)->inner; break;
							case ResolvedType::SLICE: child = static_cast<SliceResolvedType*>(tme.reflected_type)->element_type; break;
							case ResolvedType::ARRAY: child = static_cast<ArrayResolvedType*>(tme.reflected_type)->element_type; break;
							case ResolvedType::POINTER: child = static_cast<PointerResolvedType*>(tme.reflected_type)->inner; break;
							default: return {};
						}
						return {ComptimeValue::TYPE, child};
					}
					case TypeMemberExpression::LENGTH: {
						if (tme.reflected_type->kind != ResolvedType::ARRAY) return {};
						i64 length = static_cast<ArrayResolvedType*>(tme.reflected_type)->size;
						return copyComptimeValue(primitiveType(ResolvedType::UNTYPED_INT), &length);
					}
					case TypeMemberExpression::RET: {
						if (!tme.reflected_type || tme.reflected_type->kind != ResolvedType::FUNCTION) return {};
						return {ComptimeValue::TYPE, static_cast<FunctionResolvedType*>(tme.reflected_type)->return_type};
					}
					case TypeMemberExpression::MIN:
					case TypeMemberExpression::MAX: {
						const bool min = tme.kind == TypeMemberExpression::MIN;
						switch (tme.reflected_type->kind) {
							case ResolvedType::I8: return makeUntypedIntResult(min ? (u64)-128 : (u64)127);
							case ResolvedType::I16: return makeUntypedIntResult(min ? (u64)-32768 : (u64)32767);
							case ResolvedType::I32: return makeUntypedIntResult(min ? (u64)(-2147483647 - 1) : (u64)2147483647);
							case ResolvedType::I64:
							case ResolvedType::ISIZE: return makeUntypedIntResult(min ? (u64)(-9223372036854775807LL - 1) : (u64)9223372036854775807LL);
							case ResolvedType::U8:
							case ResolvedType::BYTE: return makeUntypedIntResult(min ? (u64)0 : (u64)255);
							case ResolvedType::U16: return makeUntypedIntResult(min ? (u64)0 : (u64)65535);
							case ResolvedType::U32: return makeUntypedIntResult(min ? (u64)0 : (u64)4294967295u);
							case ResolvedType::U64: return makeUntypedIntResult(min ? (u64)0 : (u64)18446744073709551615ULL);
							case ResolvedType::F32: return makeUntypedFloatResult(min ? -(f64)FLT_MAX : (f64)FLT_MAX);
							case ResolvedType::F64: return makeUntypedFloatResult(min ? -DBL_MAX : DBL_MAX);
							default: return {};
						}
					}
				}
			}
			case Expression::CALL: {
				auto& call = static_cast<CallExpression&>(expr);
				SymbolRef ref = resolveSymbol(unit, *call.callee);
				if (ref && checkSymbol(*ref.owner, *ref.symbol) == LS_RESULT_FAILURE) return {};
				if (!ref) {
					// A bare name that is no symbol at all cannot resolve later either, so
					// report it here. Any other callee (a local value, a method) merely does
					// not fold, which the caller reports in its own terms.
					if (call.callee->kind == Expression::IDENTIFIER) {
						ls_string_view name = static_cast<IdentifierExpression*>(call.callee)->name;
						if (!ctx || !findLocal(*ctx, name)) errorLine(call.callee->token, "Unknown symbol '", name, "'");
					}
					return {};
				}
				if (ref.symbol->expression->kind != Expression::FUNCTION) return {};
				FunctionExpression* decl = static_cast<FunctionExpression*>(ref.symbol->expression);
				FunctionExpression* fn = call.resolved_fn ? call.resolved_fn : decl;
				if (fn->is_template) {
					if (fn->params.size() != call.args.size()) {
						errorLine(expr.token, ref.symbol->name, " expects ", fn->params.size(), " arguments, but got ", call.args.size());
						return {};
					}
					TemplateBindings factory_bindings(unit.arena);
					for (i32 i = 0; i < fn->params.size(); ++i) {
						FunctionParam& param = fn->params[i];
						// A runtime parameter makes the call unfoldable rather than invalid.
						if (!param.is_comptime) return {};
						ComptimeValue arg = evalComptime(unit, *call.args[i], ctx, bindings, frame);
						if (!arg) return {};
						ResolvedType* expected = param.type_expr->kind == Expression::TYPE_LITERAL
							&& static_cast<TypeLiteralExpression*>(param.type_expr)->type == ResolvedType::META
							? nullptr
							: asType(evalComptime(*ref.owner, *param.type_expr, nullptr, bindings), param.type_expr->token);
						if (!expected && param.type_expr->kind != Expression::TYPE_LITERAL) return {};
						if (!comptimeValueMatchesExpected(arg, expected)) {
							if (expected) errorLine(call.args[i]->token, "Argument ", i + 1, " of ", ref.symbol->name, " must be a compile-time ", expected);
							else errorLine(call.args[i]->token, "Argument ", i + 1, " of ", ref.symbol->name, " must be a type");
							return {};
						}
						if (expected && arg.kind == ComptimeValue::VALUE && isUntypedNumeric(*arg.type)) {
							arg = coerceComptimeValue(arg, expected);
							if (!arg) {
								errorLine(call.args[i]->token, "Argument ", i + 1, " of ", ref.symbol->name, " does not fit in ", expected);
								return {};
							}
						}
						if (!bindTemplateArg(factory_bindings, param.name, arg)) return {};
					}
					fn = instantiateFunctionTemplate(*ref.owner, *fn, factory_bindings);
					if (!fn) return {};
				}
				if (fn->is_extern) return {};

				// Fast path: nullary function whose body is a single `return <expr>`
				// folds by evaluating that expression directly (no interpreter frame).
				if (call.args.empty()) {
					BlockStatement* body = static_cast<BlockStatement*>(fn->body);
					if (body->statements.size() == 1 && body->statements[0]->kind == Statement::RETURN) {
						ReturnStatement* ret = static_cast<ReturnStatement*>(body->statements[0]);
						if (!ret->expression) {
							// TODO calling void function in comptime context does not make sense, does it?
							errorLine(expr.token, "Void function cannot be called in comptime context");
							return ComptimeValue{};
						}
						return evalComptime(*ref.owner, *ret->expression, ctx, bindings, frame);
					}
				}
				// General path: interpret the body for a scalar-returning function,
				// binding folded arguments (evaluated in the current frame) as locals.
				if (fn->params.size() != call.args.size()) return {};

				FunctionResolvedType* fn_type = asFunctionType(fn->resolved_type);
				if (!fn_type) return {};

				ResolvedType* rt = fn_type ? fn_type->return_type : nullptr;
				ComptimeFrame callee_frame(unit);
				ExpArray<ComptimeValue> args(unit.arena);
				for (i32 i = 0; i < call.args.size(); ++i) {
					ComptimeValue arg = evalComptime(unit, *call.args[i], ctx, bindings, frame);
					if (!arg) return {};
					args.push(arg);
					ComptimeFrame::Local& binding = callee_frame.locals.emplace_back();
					binding.name = fn->params[i].name;
					binding.bytes = arg.value;
					binding.value = arg;
					binding.type = arg.type;
				}

				ComptimeValue result = evalComptime(unit, *fn->body, callee_frame);
				recordFactoryResult(unit, *decl, *fn, args, result);
				return result;
			}
			case Expression::STRUCT: {
				if (!expr.resolved_type || expr.resolved_type->kind != ResolvedType::META) return {};
				return {ComptimeValue::TYPE, static_cast<MetaType*>(expr.resolved_type)->inner};
			}
			case Expression::MEMBER: {
				if (expr.comptime_value) return expr.comptime_value;
				// TODO review this whole branch
				auto& member = static_cast<MemberExpression&>(expr);

				if (member.expression && member.expression->resolved_type
					&& (member.expression->resolved_type->kind == ResolvedType::ARRAY || member.expression->resolved_type->kind == ResolvedType::SLICE)
					&& equalStrings(member.name, makeStringView("length"))) {
					i64 length = member.expression->resolved_type->kind == ResolvedType::ARRAY
						? static_cast<ArrayResolvedType*>(member.expression->resolved_type)->size
						: 0;
					if (member.expression->resolved_type->kind == ResolvedType::SLICE) {
						ComptimeValue base = evalComptime(unit, *member.expression, ctx, bindings, frame);
						if (!base || base.kind != ComptimeValue::VALUE) return {};
						ComptimeSliceValue slice;
						copyMemory(&slice, base.value, sizeof(slice));
						length = slice.count;
					}
					return copyComptimeValue(primitiveType(ResolvedType::UNTYPED_INT), &length);
				}

				// struct.field
				if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::STRUCT) {
					++suppress_errors;
					ComptimeValue base_value = evalComptime(unit, *member.expression, ctx, bindings, frame);
					--suppress_errors;
					if (base_value.kind == ComptimeValue::VALUE) {
						StructResolvedType& st = *static_cast<StructResolvedType*>(member.expression->resolved_type);
						u32 offset = 0;
						for (i32 i = 0; i < st.decl->fields.size(); ++i) {
							ResolvedType* field_type = structFieldType(st, i);
							if (equalStrings(st.decl->fields[i].name, member.name)) {
								if (field_type->kind == ResolvedType::META) {
									ResolvedType* inner = nullptr;
									copyMemory(&inner, base_value.value + offset, sizeof(inner));
									return {ComptimeValue::TYPE, inner};
								}
								return {ComptimeValue::VALUE, field_type, base_value.value + offset};
							}
							offset += st.is_compiler_only ? comptimeSize(*field_type) : typeByteSize(*field_type);
						}
					}
				}

				// TODO handle `hint`
				// .enum_value
				if (!member.expression) {
					if (expr.resolved_type && expr.resolved_type->kind == ResolvedType::ENUM) {
						EnumResolvedType* en = static_cast<EnumResolvedType*>(expr.resolved_type);
						for (i32 i = 0; i < en->decl->members.size(); ++i) {
							const EnumMember& enum_member = en->decl->members[i];
							if (!equalStrings(enum_member.name, member.name)) continue;

							if (!enum_member.value) return makeComptimeEnumResult(en, (i64)i);
							ComptimeValue value = evalComptime(unit, *enum_member.value, ctx, bindings, frame);
							if (!value) return {};

							return makeComptimeEnumResult(en, comptimeNumericToI64(value.value, value.type->kind));
						}
					}
					ASSERT(false);
				}

				if (member.expression->kind != Expression::IDENTIFIER) {
					errorLine(expr.token, "Expected identifier");
					return {};
				}

				// enum.value
				const ls_string_view qualifier = static_cast<IdentifierExpression*>(member.expression)->name;
				SymbolRef type_ref = resolveSymbol(unit, {}, qualifier, LookupPolicy::Checked);
				if (type_ref && type_ref.symbol->resolved_type && type_ref.symbol->resolved_type->kind == ResolvedType::META) {
					ResolvedType* inner = unwrapMeta(type_ref.symbol->resolved_type);
					if (inner->kind == ResolvedType::ENUM) {
						EnumResolvedType* en = static_cast<EnumResolvedType*>(inner);
						for (i32 i = 0; i < en->decl->members.size(); ++i) {
							const EnumMember& enum_member = en->decl->members[i];
							if (!equalStrings(enum_member.name, member.name)) continue;

							if (!enum_member.value) return makeComptimeEnumResult(inner, (i64)i);

							ComptimeValue value = evalComptime(unit, *enum_member.value, ctx, bindings, frame);
							if (!value) return {};

							i64 numeric = comptimeNumericToI64(value.value, value.type->kind);
							return makeComptimeEnumResult(inner, numeric);
						}
					}
				}

				// namespace.*
				if (!findImportedUnitByAlias(unit, qualifier)) {
					errorLine(expr.token, "Unknown import alias ", qualifier);
					return {};
				}

				SymbolRef ref = resolveSymbol(unit, qualifier, member.name, LookupPolicy::Checked);
				if (!ref) {
					errorLine(expr.token, "Unknown symbol ", qualifier, ".", member.name);
					return {};
				}

				if (ref.symbol->storage != Symbol::COMPTIME) {
					errorLine(expr.token, "Symbol ", qualifier, ".", member.name, " is not a compile-time value");
					return {};
				}

				// TODO why can't we use ref.symbol->comptime_value here?
				if (ref.symbol->resolved_type && ref.symbol->resolved_type->kind == ResolvedType::META) {
					return ComptimeValue{ComptimeValue::TYPE, unwrapMeta(ref.symbol->resolved_type)};
				}
				
				return ref.symbol->comptime_value;
			}
			case Expression::BRACKET: {
				auto& be = static_cast<BracketExpression&>(expr);
				ASSERT(be.base);

				// A[i] - arary, slice, or struct["field"]
				if (be.args.size() == 1) {
					if (be.base->kind == Expression::ARRAY_LITERAL) {
						errorLine(be.base->token, "Indexing a compile-time array literal directly is not supported");
						return {};
					}

					ComptimeValue base_value = evalComptime(unit, *be.base, ctx, bindings, frame);
					if (base_value.kind == ComptimeValue::VALUE) {
						// struct_value["field"]
						if (!empty(be.struct_field_name) && base_value.type->kind == ResolvedType::STRUCT) {
							StructResolvedType* st = static_cast<StructResolvedType*>(base_value.type);
							u32 offset = 0;
							for (i32 field_index = 0; field_index < st->decl->fields.size(); ++field_index) {
								ResolvedType* field_type = structFieldType(*st, field_index);
								if (equalStrings(st->decl->fields[field_index].name, be.struct_field_name)) {
									if (field_type->kind == ResolvedType::META) {
										ResolvedType* inner = nullptr;
										copyMemory(&inner, base_value.value + offset, sizeof(inner));
										return {ComptimeValue::TYPE, inner};
									}
									return {ComptimeValue::VALUE, field_type, base_value.value + offset};
								}
								offset += comptimeSize(*field_type);
							}
							return {};
						}

						// slice_value[index]
						if (base_value.type->kind == ResolvedType::SLICE) {
							ComptimeValue index = evalComptime(unit, *be.args[0], ctx, bindings, frame);
							if (!index || index.kind != ComptimeValue::VALUE || !isIntegerOrUntyped(*index.type)) {
								errorLine(be.args[0]->token, "Comptime slice index must be a compile-time integer");
								return {};
							}

							ComptimeSliceValue slice;
							copyMemory(&slice, base_value.value, sizeof(slice));
							i64 i = comptimeNumericToI64(index.value, index.type->kind);
							if (i >= slice.count) {
								errorLine(be.base->token, "Comptime slice index `", i, "` out of bounds, must be < ", slice.count);
								return {};
							}

							if (i < 0) {
								errorLine(be.base->token, "Comptime slice index cannot be negative, got ", i);
								return {};
							}

							ResolvedType* element_type = static_cast<SliceResolvedType*>(base_value.type)->element_type;
							if (element_type->kind == ResolvedType::META) {
								ResolvedType* inner = nullptr;
								copyMemory(&inner, slice.data + comptimeSize(*element_type) * i, sizeof(inner));
								return {ComptimeValue::TYPE, inner};
							}
							return {ComptimeValue::VALUE, element_type, slice.data + comptimeSize(*element_type) * i};
						}

						// array_value[index]
						if (base_value.type->kind == ResolvedType::ARRAY) {
							ComptimeValue index = evalComptime(unit, *be.args[0], ctx, bindings, frame);
							if (!index || index.kind != ComptimeValue::VALUE || !isIntegerOrUntyped(*index.type)) {
								errorLine(be.args[0]->token, "Comptime array index must be a compile-time integer");
								return {};
							}
							i64 i = comptimeNumericToI64(index.value, index.type->kind);
							ArrayResolvedType* array_type = static_cast<ArrayResolvedType*>(base_value.type);
							if (i < 0) {
								errorLine(be.base->token, "Comptime array index cannot be negative, got ", i);
								return {};
							}
							if (i >= array_type->size) {
								errorLine(be.base->token, "Comptime array index `", i, "` out of bounds, must be < ", array_type->size);
								return {};
							}
							ResolvedType* element_type = array_type->element_type;
							u8* element = base_value.value + comptimeSize(*element_type) * i;
							if (element_type->kind == ResolvedType::META) {
								ResolvedType* inner = nullptr;
								copyMemory(&inner, element, sizeof(inner));
								return {ComptimeValue::TYPE, inner};
							}
							return {ComptimeValue::VALUE, element_type, element};
						}
					}
				}

				// `Foo[i32]` used to apply a struct template. Diagnose it as such instead of
				// reporting that the whole expression is not a type.
				++suppress_errors;
				ComptimeValue base_value = evalComptime(unit, *be.base, ctx, bindings, frame);
				--suppress_errors;
				bool base_is_type = base_value.kind == ComptimeValue::TYPE;
				if (!base_is_type) {
					SymbolRef ref = resolveSymbol(unit, *be.base);
					if (ref && ref.symbol->expression && ref.symbol->expression->kind == Expression::FUNCTION) {
						Expression* ret = static_cast<FunctionExpression*>(ref.symbol->expression)->return_type;
						base_is_type = ret && ret->kind == Expression::TYPE_LITERAL
							&& static_cast<TypeLiteralExpression*>(ret)->type == ResolvedType::META;
					}
				}
				if (base_is_type) {
					errorLine(be.base->token, "Type arguments are applied with a call, e.g. `Name(i32)`, not with brackets");
				}
				return {};
			}
			case Expression::FUNCTION_TYPE: {
				auto& ft = static_cast<FunctionTypeExpression&>(expr);
				FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit.arena, unit.arena);
				for (FunctionTypeParam& param : ft.params) {
					ResolvedType* param_type = asType(evalComptime(unit, *param.type_expr, ctx, bindings), param.type_expr->token);
					if (!param_type) return {};
					FunctionResolvedParam& resolved_param = fn_type->params.emplace_back();
					resolved_param.name = param.name;
					resolved_param.type = param_type;
					resolved_param.is_comptime = param.is_comptime;
				}
				fn_type->return_type = asType(evalComptime(unit, *ft.return_type, ctx, bindings), ft.return_type->token);
				if (!fn_type->return_type) return {};
				return {ComptimeValue::TYPE, fn_type};
			}
			case Expression::GENERIC_IDENTIFIER: {
				// this should be rechecked with checkExpr later
				return {};
			}
			case Expression::BINARY: {
				u8* res_bytes = comptime_stack_ptr;
				auto& be = static_cast<BinaryExpression&>(expr);
				ComptimeValue lhs = evalComptime(unit, *be.lhs, ctx, bindings, frame);
				if (!lhs) return {};
				ComptimeValue rhs = evalComptime(unit, *be.rhs, ctx, bindings, frame);
				if (!rhs) return {};
				if (be.op == Token::IS) {
					i32 tag;
					copyMemory(&tag, lhs.value, sizeof(tag));
					UnionResolvedType* un = static_cast<UnionResolvedType*>(lhs.type);
					bool matches = tag >= 0 && tag < un->members.size() && typesEqual(un->members[tag], rhs.type);
					return makeComptimeResult(matches, res_bytes);
				}

				// TODO if only one side is untyped, we should promote it to the other side's type

				if (be.op == Token::EQUAL_EQUAL) return makeComptimeResult(comptimeValuesEqual(lhs, rhs), res_bytes);
				if (be.op == Token::BANG_EQUAL) return makeComptimeResult(!comptimeValuesEqual(lhs, rhs), res_bytes);
				if (be.op == Token::AND || be.op == Token::OR) {
					// TODO add test to hit this
					if (lhs.kind != ComptimeValue::VALUE || rhs.kind != ComptimeValue::VALUE) {
						errorLine(be.token, "Logical operators can only be applied to bool values, got ", lhs.type, " and ", rhs.type);
						return {};
					}
					bool lhs_bool; memcpy(&lhs_bool, lhs.value, sizeof(lhs_bool));
					bool rhs_bool; memcpy(&rhs_bool, rhs.value, sizeof(rhs_bool));
					return be.op == Token::AND
						? makeComptimeResult(lhs_bool && rhs_bool, res_bytes)
						: makeComptimeResult(lhs_bool || rhs_bool, res_bytes);
				}

				if (!isNumericOrUntyped(*lhs.type) || !isNumericOrUntyped(*rhs.type)) {
					errorLine(be.token, "Expected numeric values, got ", lhs.type, " and ", rhs.type);
					return {};
				}


				if (!typesEqual(lhs.type, rhs.type)) {
					errorLine(be.token, "Expected matching numeric types, got ", lhs.type, " and ", rhs.type);
					return {};
				}

				switch (lhs.type->kind) {
					case ResolvedType::F32: return arithmeticOp<float>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::F64: return arithmeticOp<double>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::I8: return arithmeticOp<i8>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::I16: return arithmeticOp<i16>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::I32: return arithmeticOp<i32>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::I64: return arithmeticOp<i64>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::U8: return arithmeticOp<u8>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::U16: return arithmeticOp<u16>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::U32: return arithmeticOp<u32>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::U64: return arithmeticOp<u64>(be.op, lhs.value, rhs.value, res_bytes);
					case ResolvedType::UNTYPED_INT: {
						ComptimeValue v = arithmeticOp<u64>(be.op, lhs.value, rhs.value, res_bytes);
						v.type = primitiveType(ResolvedType::UNTYPED_INT);
						return v;
					}
					case ResolvedType::UNTYPED_FLOAT: {
						ComptimeValue v = arithmeticOp<double>(be.op, lhs.value, rhs.value, res_bytes);
						v.type = primitiveType(ResolvedType::UNTYPED_FLOAT);
						return v;
					}
					default: errorLine(be.token, "Arithmetic operations are not supported for type ", lhs.type); return {};
				}
			}
			case Expression::STRING_LITERAL: {
				auto& sl = static_cast<StringLiteralExpression&>(expr);
				ComptimeSliceValue value{(u8*)sl.value.begin, (i64)(sl.value.end - sl.value.begin)};
				return copyComptimeValue(const_u8_slice, &value, sizeof(value));
			}
			case Expression::ARRAY_LITERAL: {
				auto& al = static_cast<ArrayLiteralExpression&>(expr);
				if (expr.resolved_type && !isRuntimeMaterializable(*expr.resolved_type)) {
					u8* data = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, comptimeSize(*expr.resolved_type), 1));
					ResolvedType* element_type = static_cast<ArrayResolvedType*>(expr.resolved_type)->element_type;
					for (i32 i = 0; i < al.values.size(); ++i) {
						ComptimeValue element = evalComptime(unit, *al.values[i], ctx, bindings, frame);
						if (!element) return {};
						writeComptimeValue(data + comptimeSize(*element_type) * i, *element_type, element);
					}
					return {ComptimeValue::VALUE, expr.resolved_type, data};
				}
				u8* value = comptime_stack_ptr;
				for (Expression* element_expr : al.values) {
					// TODO are we sure we don't have to check the element_expr type against the array type?
					if (!evalComptime(unit, *element_expr, ctx, bindings, frame)) return {};
				}
				return {ComptimeValue::VALUE, expr.resolved_type, value};
			}
			case Expression::STRUCT_LITERAL: {
				auto& sl = static_cast<StructLiteralExpression&>(expr);
				if (expr.resolved_type && !isRuntimeMaterializable(*expr.resolved_type)) {
					StructResolvedType* st = static_cast<StructResolvedType*>(expr.resolved_type);
					u8* data = static_cast<u8*>(unit.arena.allocate(unit.arena.user_data, comptimeSize(*st), 1));
					for (i32 i = 0; i < sl.values.size(); ++i) {
						ComptimeValue field = evalComptime(unit, *sl.values[i], ctx, bindings, frame);
						if (!field) return {};
						writeComptimeValue(data + comptimeFieldOffset(*st, i), *structFieldType(*st, i), field);
					}
					return {ComptimeValue::VALUE, expr.resolved_type, data};
				}
				u8* value = comptime_stack_ptr;
				for (Expression* field_expr : sl.values) {
					if (!evalComptime(unit, *field_expr, ctx, bindings, frame)) return {};
				}

				return {ComptimeValue::VALUE, sl.type->resolved_type, value};
			}
			case Expression::CAST: {
				auto& ce = static_cast<CastExpression&>(expr);
				ComptimeValue lhs = evalComptime(unit, *ce.expression, ctx, bindings, frame);
				if (!lhs) return {};
				if (lhs.kind != ComptimeValue::VALUE) {
					errorLine(ce.expression->token, "Cast source must be a value, got a type");
					return {};
				}

				ResolvedType* rhs = asType(evalComptime(unit, *ce.type_expr, ctx, bindings, frame), ce.type_expr->token);
				if (!rhs) return {};

				const u32 src_size = typeByteSize(*lhs.type);
				comptime_stack_ptr -= src_size;
				const u8* src_bytes = comptime_stack_ptr;

				const u32 dst_size = writeComptimeNumeric(comptime_stack_ptr, src_bytes, lhs.type->kind, rhs->kind);
				u8* value = comptime_stack_ptr;
				comptime_stack_ptr += dst_size;

				return {ComptimeValue::VALUE, rhs, value};
			}
			case Expression::NULL_LITERAL: {
				auto& nl = static_cast<NullLiteralExpression&>(expr);
				if (!nl.resolved_type) {
					// e.g. `comptime A = B | null;`
					errorLine(nl.token, "Null literal must have a type");
					return {};
				}
				// TODO make a test to hit this
				u64 size = typeByteSize(*nl.resolved_type);
				u8* value = comptime_stack_ptr;
				memset(comptime_stack_ptr, 0, size);
				comptime_stack_ptr += size;
				return {ComptimeValue::VALUE, nl.resolved_type, value};
			}
			case Expression::TYPEOF: {
				auto& toe = static_cast<TypeofExpression&>(expr);
				return {ComptimeValue::TYPE, toe.operand->resolved_type};
			}
			case Expression::UNION_TYPE: {
				auto& ut = static_cast<UnionTypeExpression&>(expr);
				ExpArray<ResolvedType*> members(module.arena);
				for (Expression* member_expr : ut.members) {
					ResolvedType* member_type = asType(evalComptime(unit, *member_expr, ctx, bindings, frame), member_expr->token);
					if (!member_type) return {};
					if (member_type->kind == ResolvedType::VOID) {
						errorLine(member_expr->token, "Union cannot contain void");
						return {};
					}
					members.push(member_type);
				}

				return {ComptimeValue::TYPE, getUnionType(members)};
			}
			case Expression::TERNARY: {
				auto& te = static_cast<TernaryExpression&>(expr);
				ComptimeValue cond_type = evalComptime(unit, *te.condition, ctx, bindings, frame);
				if (!cond_type) return {};
				if (cond_type.kind != ComptimeValue::VALUE) {
					errorLine(te.condition->token, "Ternary condition must be a compile-time constant value, got a type");
					return {};
				}

				if (cond_type.type->kind != ResolvedType::BOOL) {
					errorLine(te.condition->token, "Ternary condition must be a compile-time constant bool, got ", cond_type);
					return {};
				}

				bool cond_value;
				memcpy(&cond_value, comptime_stack_ptr - sizeof(bool), sizeof(bool));
				if (cond_value) return evalComptime(unit, *te.true_expr, ctx, bindings, frame);
				return evalComptime(unit, *te.false_expr, ctx, bindings, frame);
			}
			case Expression::SLICE_TYPE: {
				auto& st = static_cast<SliceTypeExpression&>(expr);
				SliceResolvedType* slice_type = makeType<SliceResolvedType>(module.arena);
				slice_type->is_const = st.is_const;
				slice_type->element_type = asType(evalComptime(unit, *st.element_type, ctx, bindings, frame), st.element_type->token);
				if (!slice_type->element_type) return {};

				return {ComptimeValue::TYPE, slice_type};
			}
			case Expression::NULLABLE_TYPE: {
				auto& nt = static_cast<NullableTypeExpression&>(expr);
				NullableResolvedType* nullable_type = makeType<NullableResolvedType>(module.arena);
				nullable_type->inner = asType(evalComptime(unit, *nt.inner, ctx, bindings, frame), nt.inner->token);
				if (!nullable_type->inner) return {};

				return {ComptimeValue::TYPE, nullable_type};
			}
			case Expression::POINTER_TYPE: {
				auto& pt = static_cast<PointerTypeExpression&>(expr);
				PointerResolvedType* pointer_type = makeType<PointerResolvedType>(module.arena);
				pointer_type->is_const = pt.is_const;
				pointer_type->inner = asType(evalComptime(unit, *pt.inner, ctx, bindings, frame), pt.inner->token);
				if (!pointer_type->inner) return {};
				return {ComptimeValue::TYPE, pointer_type};
			}
			case Expression::ARRAY_TYPE: {
				auto& at = static_cast<ArrayTypeExpression&>(expr);
				ArrayResolvedType* array_type = makeType<ArrayResolvedType>(module.arena);
				array_type->element_type = asType(evalComptime(unit, *at.element_type, ctx, bindings, frame), at.element_type->token);
				if (!array_type->element_type) return {};

				if (!evalComptimeIntValue(unit, at.size, array_type->size, bindings)) return {};
				if (array_type->size <= 0) {
					errorLine(at.size->token, "Static array size must be greater than zero");
					return {};
				}

				return {ComptimeValue::TYPE, array_type};
			}
			case Expression::RESOLVED_TYPE: {
				auto& rte = static_cast<ResolvedTypeExpression&>(expr);
				return {ComptimeValue::TYPE, rte.resolved_type};
			}
			case Expression::IDENTIFIER: {
				auto& id = static_cast<IdentifierExpression&>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id.name); local && (!id.slot || local->slot == id.slot)) {
						if (local->is_comptime) return local->comptime_value;
						errorLine(id.token, "Symbol '", id.name, "' is not a compile-time value");
						return {};
					}
				}

				if (const TemplateBinding* binding = findTemplateBinding(bindings, id.name)) {
					if (binding->arg.kind == ComptimeValue::TYPE) id.resolved_type = binding->arg.type;
					return binding->arg.kind == ComptimeValue::VALUE
						? copyComptimeValue(binding->arg.type, binding->arg.value)
						: binding->arg;
				}

				if (frame) {
					if (ComptimeFrame::Local* local = frame->find(id.name)) {
						if (local->value && local->value.kind != ComptimeValue::VALUE) return local->value;
						return copyComptimeValue(local->type, local->bytes);
					}
				}

				SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked);
				if (!ref) {
					errorLine(id.token, "Unknown symbol '", id.name, "'");
					return {};
				}

				if (ref.symbol->storage != Symbol::COMPTIME) {
					errorLine(id.token, "Symbol '", id.name, "' is not a compile-time value");
					return {};
				}

				id.symbol = ref.symbol;
				if (ref.symbol->comptime_value.kind == ComptimeValue::TYPE) return ref.symbol->comptime_value;
				if (!ref.symbol->resolved_type) {
					// A template has no type of its own until it is instantiated.
					errorLine(id.token, "'", id.name, "' is a template, it has no value until it is called");
					return {};
				}
				if (ref.symbol->resolved_type->kind == ResolvedType::META) {
					ASSERT(ref.symbol->comptime_byte_size == 0);
					return {ComptimeValue::TYPE, static_cast<MetaType*>(ref.symbol->resolved_type)->inner};
				}
				return copyComptimeValue(ref.symbol->resolved_type, ref.symbol->comptime_bytes, ref.symbol->comptime_byte_size);
			}
			case Expression::TYPE_LITERAL: {
				const ResolvedType::Kind kind = static_cast<TypeLiteralExpression&>(expr).type;
				if (kind >= ResolvedType::VOID && kind <= ResolvedType::BYTE) return {ComptimeValue::TYPE, primitiveType(kind)};
				if (kind == ResolvedType::META) return {ComptimeValue::TYPE, makeType<MetaType>(module.arena)};

				errorLine(expr.token, "Type literal is not a compile-time constant");
				return {};
			}
			case Expression::UNARY: {
				auto& un = static_cast<UnaryExpression&>(expr);
				if (un.op == Token::MINUS && un.expression->kind == Expression::INT_LITERAL && expr.resolved_type) {
					const u64 magnitude = static_cast<IntLiteralExpression&>(*un.expression).value;
					switch (expr.resolved_type->kind) {
						case ResolvedType::I8: return makeComptimeResult<i8>((i8)(-(i64)magnitude), comptime_stack_ptr);
						case ResolvedType::I16: return makeComptimeResult<i16>((i16)(-(i64)magnitude), comptime_stack_ptr);
						case ResolvedType::I32: return makeComptimeResult<i32>((i32)(-(i64)magnitude), comptime_stack_ptr);
						case ResolvedType::I64: return makeComptimeResult<i64>(-(i64)magnitude, comptime_stack_ptr);
						case ResolvedType::ISIZE: {
							const u64 value = 0ull - magnitude;
							return copyComptimeValue(primitiveType(ResolvedType::ISIZE), &value);
						}
						case ResolvedType::F32: return makeComptimeResult<f32>(-(f32)magnitude, comptime_stack_ptr);
						case ResolvedType::F64: return makeComptimeResult<f64>(-(f64)magnitude, comptime_stack_ptr);
						default: break;
					}
				}
				
				ComptimeValue operand_type = evalComptime(unit, *un.expression, ctx, bindings, frame);
				if (!operand_type) return {};
				if (operand_type.kind != ComptimeValue::VALUE) {
					errorLine(un.expression->token, "Unary operator operand must be a compile-time constant value, got a type");
					return {};
				}

				if (un.op == Token::NOT) {
					if (operand_type.type->kind != ResolvedType::BOOL) {
						errorLine(un.token, "Not is only valid for bool, got ", operand_type);
						return {};
					}
					bool value;
					memcpy(&value, operand_type.value, sizeof(value));
					value = !value;
					memcpy(operand_type.value, &value, sizeof(value));
					return operand_type;
				}

				ASSERT(un.op == Token::MINUS);
				auto negate = [&](auto value) {
					memcpy(&value, operand_type.value, sizeof(value));
					value = -value;
					memcpy(operand_type.value, &value, sizeof(value));
					return operand_type;
				};
				switch (operand_type.type->kind) {
					case ResolvedType::I8: return negate(i8{});
					case ResolvedType::I16: return negate(i16{});
					case ResolvedType::I32: return negate(i32{});
					case ResolvedType::I64:
					case ResolvedType::ISIZE:
					case ResolvedType::UNTYPED_INT: return negate(i64{});
					case ResolvedType::F32: return negate(f32{});
					case ResolvedType::F64:
					case ResolvedType::UNTYPED_FLOAT: return negate(f64{});
					default: break;
				}
				ASSERT(false);
				return {};
			}
			case Expression::INT_LITERAL: {
				auto& il = static_cast<IntLiteralExpression&>(expr);
				if (!expr.resolved_type) expr.resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
				u8* value = comptime_stack_ptr;
				const u32 dst_size = writeComptimeNumeric(comptime_stack_ptr, (const u8*)&il.value, ResolvedType::U64, expr.resolved_type->kind);
				comptime_stack_ptr += dst_size;
				return {ComptimeValue::VALUE, expr.resolved_type, value};
			}
			case Expression::SIZEOF: {
				auto& sz = static_cast<SizeofExpression&>(expr);
				if (!resolveSizeofValue(unit, sz, ctx, bindings)) return {};
				return copyComptimeValue(primitiveType(ResolvedType::UNTYPED_INT), &sz.value);
			}
			case Expression::FLOAT_LITERAL: {
				auto& fl = static_cast<FloatLiteralExpression&>(expr);
				if (!expr.resolved_type) expr.resolved_type = primitiveType(ResolvedType::UNTYPED_FLOAT);
				u8* value = comptime_stack_ptr;
				const u32 dst_size = writeComptimeNumeric(comptime_stack_ptr, (const u8*)&fl.value, ResolvedType::F64, expr.resolved_type->kind);
				comptime_stack_ptr += dst_size;
				return {ComptimeValue::VALUE, expr.resolved_type, value};
			}
			case Expression::BOOL_LITERAL: {
				auto& bl = static_cast<BoolLiteralExpression&>(expr);
				return copyComptimeValue(primitiveType(ResolvedType::BOOL), &bl.value);
			}
			case Expression::UNDEFINED:
				errorLine(expr.token, "`undefined` is not a compile-time constant");
				return {};
			default:
				errorLine(expr.token, "Expression is not a compile-time constant");
				return {};
		}
	}

	ls_module& module;
	OutputFormatter error_stream;
	i32 suppress_errors = 0;
	MetaType* meta_value_type;
	SliceResolvedType* const_u8_slice;
	SliceResolvedType* slice_of_types; // []type for Union::types type member
	StructResolvedType* field_descriptor_type;
	StructResolvedType* param_descriptor_type;
	SliceResolvedType* slice_of_fields;
	SliceResolvedType* slice_of_params;
	u8* comptime_stack = nullptr;
	u8* comptime_stack_ptr = nullptr;

}; // struct Checker

ls_result ls_module_typecheck(ls_module* module) {
	if (!module) return LS_RESULT_FAILURE;
	if (Checker(*module).typecheck() == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	for (Unit& unit : module->units) {
		unit.native_symbols.clear();
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (fn->is_extern && !fn->is_template) unit.native_symbols.push(&sym);
		}
	}
	return LS_RESULT_OK;
}

ls_result ls_module_compile(ls_module* module, ls_string_view source, ls_string_view source_name, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	if (!module) return LS_RESULT_FAILURE;
	Checker checker(*module);
	if (ls_module_parse(module, source, source_name) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	for (i32 unit_index = 0; unit_index < checker.module.units.size(); ++unit_index) {
		Unit& unit = checker.module.units[unit_index];
		if (!checker.resolveImportsForUnit(unit, import_resolver, import_resolver_userdata)) return LS_RESULT_FAILURE;
	}
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
