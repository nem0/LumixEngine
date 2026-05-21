#include "runtime.h"

Runtime::Runtime(Module& module)
	: m_module(module)
{
	m_output.host = module.host;
}

Runtime::~Runtime() {
	for (std::vector<Value>* arr : m_owned_arrays) deleteObject(m_module.host, arr);
	for (char* str : m_owned_strings) deallocateMemory(m_module.host, str);
}

bool Runtime::call(ls_string_view function_name, std::span<const Value> args, Value* result) {
	const i32 fn_idx = findFunction(function_name);
	const i32 native_idx = findNativeFunction(function_name);
	if (fn_idx < 0 && native_idx < 0) {
		m_output.error("Unknown function '", function_name, "'");
		return false;
	}
	if (!initializeGlobals()) return false;
	Value out;
	const bool ok = fn_idx >= 0 ? callFunction(m_module.functions[fn_idx], args, {}, &out) : callNativeFunction(m_module.native_functions[native_idx], args, &out);
	if (ok && result) *result = out;
	return ok;
}

Value makeI32(i32 value) {
	Value v;
	v.type = {TypeRef::I32, {}, -1};
	v.i = value;
	v.u = (u32)value;
	v.i64 = (i64)value;
	v.u64 = (u64)value;
	v.f = (float)value;
	v.d = (double)value;
	return v;
}

Value makeU32(u32 value) {
	Value v;
	v.type = TypeRef(TypeRef::U32);
	v.u = value;
	v.i = (i32)value;
	v.i64 = (i64)value;
	v.u64 = (u64)value;
	v.f = (float)value;
	v.d = (double)value;
	return v;
}

Value makeI64(i64 value) {
	Value v;
	v.type = TypeRef(TypeRef::I64);
	v.i64 = value;
	v.u64 = (u64)value;
	v.i = (i32)value;
	v.u = (u32)value;
	v.f = (float)value;
	v.d = (double)value;
	return v;
}

Value makeU64(u64 value) {
	Value v;
	v.type = TypeRef(TypeRef::U64);
	v.u64 = value;
	v.i64 = (i64)value;
	v.u = (u32)value;
	v.i = (i32)value;
	v.f = (float)value;
	v.d = (double)value;
	return v;
}

Value makeF32(float value) {
	Value v;
	v.type = {TypeRef::F32, {}, -1};
	v.f = value;
	v.i = (i32)value;
	v.u = (u32)value;
	v.i64 = (i64)value;
	v.u64 = (u64)value;
	v.d = (double)value;
	return v;
}

Value makeF64(double value) {
	Value v;
	v.type = {TypeRef::F64, {}, -1};
	v.d = value;
	v.f = (float)value;
	v.i = (i32)value;
	v.u = (u32)value;
	v.i64 = (i64)value;
	v.u64 = (u64)value;
	return v;
}

Value makeString(ls_string_view value) {
	Value v;
	v.type = {TypeRef::STRING, {}, -1};
	v.string = value;
	return v;
}

Value makeFunction(TypeRef type, i32 index, bool is_native) {
	Value v;
	v.type = type;
	v.i = index;
	v.b = is_native;
	return v;
}

static i64 truncateSigned(TypeRef::Kind kind, i64 value) {
	switch (kind) {
		case TypeRef::I8: return (i64)(i8)value;
		case TypeRef::I16: return (i64)(i16)value;
		case TypeRef::I32: return (i64)(i32)value;
		case TypeRef::I64: return value;
		default: return value;
	}
}

static u64 truncateUnsigned(TypeRef::Kind kind, u64 value) {
	switch (kind) {
		case TypeRef::U8: return (u64)(u8)value;
		case TypeRef::U16: return (u64)(u16)value;
		case TypeRef::U32: return (u64)(u32)value;
		case TypeRef::U64: return value;
		default: return value;
	}
}

static Value makeSignedIntegral(TypeRef::Kind kind, i64 value) {
	Value v = makeI64(truncateSigned(kind, value));
	v.type = {kind, {}, -1};
	return v;
}

static Value makeUnsignedIntegral(TypeRef::Kind kind, u64 value) {
	Value v = makeU64(truncateUnsigned(kind, value));
	v.type = {kind, {}, -1};
	return v;
}

static Value makeFloatLike(TypeRef::Kind kind, double value) {
	Value v = kind == TypeRef::F64 ? makeF64(value) : makeF32((float)value);
	v.type = {kind, {}, -1};
	return v;
}

static bool isSignedIntegral(TypeRef::Kind kind) {
	return kind == TypeRef::I8 || kind == TypeRef::I16 || kind == TypeRef::I32 || kind == TypeRef::I64;
}

static bool isUnsignedIntegral(TypeRef::Kind kind) {
	return kind == TypeRef::U8 || kind == TypeRef::U16 || kind == TypeRef::U32 || kind == TypeRef::U64;
}

static i64 asI64(const Value& v) {
	if (isSignedIntegral(v.type.kind)) return v.i64;
	if (isUnsignedIntegral(v.type.kind)) return (i64)v.u64;
	if (v.type.kind == TypeRef::F64) return (i64)v.d;
	if (v.type.kind == TypeRef::F32) return (i64)v.f;
	if (v.type.kind == TypeRef::BOOL) return v.b ? 1 : 0;
	return v.i64;
}

static u64 asU64(const Value& v) {
	if (isUnsignedIntegral(v.type.kind)) return v.u64;
	if (isSignedIntegral(v.type.kind)) return (u64)v.i64;
	if (v.type.kind == TypeRef::F64) return (u64)v.d;
	if (v.type.kind == TypeRef::F32) return (u64)v.f;
	if (v.type.kind == TypeRef::BOOL) return v.b ? 1 : 0;
	return v.u64;
}

static double asF64(const Value& v) {
	if (v.type.kind == TypeRef::F64) return v.d;
	if (v.type.kind == TypeRef::F32) return (double)v.f;
	if (isUnsignedIntegral(v.type.kind)) return (double)v.u64;
	if (isSignedIntegral(v.type.kind)) return (double)v.i64;
	if (v.type.kind == TypeRef::BOOL) return v.b ? 1.0 : 0.0;
	return 0;
}

Value makeNull() {
	Value v;
	v.type = {TypeRef::NULL_VALUE, {}, -1};
	return v;
}

i32 Runtime::findFunction(ls_string_view name) const {
	for (i32 i = 0; i < m_module.functions.size(); ++i) {
		if (!m_module.functions[i].is_nested && equalStrings(m_module.functions[i].name, name)) return i;
	}
	return -1;
}

i32 Runtime::findNativeFunction(ls_string_view name) const {
	for (i32 i = 0; i < m_module.native_functions.size(); ++i) if (equalStrings(m_module.native_functions[i].name, name)) return i;
	return -1;
}

i32 Runtime::findEnum(ls_string_view name) const {
	for (i32 i = 0; i < m_module.enums.size(); ++i) if (equalStrings(m_module.enums[i].name, name)) return i;
	return -1;
}

ls_string_view Runtime::getExpressionName(i32 expr_idx) {
	Expr& e = m_module.expressions[expr_idx];
	if (e.kind == Expr::VAR) return e.name;
	if (e.kind == Expr::FIELD) {
		if (empty(e.qualified_name)) e.qualified_name = m_module.makeQualifiedName(getExpressionName(e.left), e.name);
		return e.qualified_name;
	}
	return {};
}

bool Runtime::splitMemberName(ls_string_view name, ls_string_view* owner, ls_string_view* member) const {
	for (const char* c = data(name) + size(name); c != data(name); --c) {
		if (*(c - 1) != '.') continue;
		*owner = ls_string_view{data(name), c - 1};
		*member = ls_string_view{c, data(name) + size(name)};
		return true;
	}
	return false;
}

bool Runtime::evalQualifiedEnumMember(ls_string_view name, Value* value) {
	ls_string_view enum_name;
	ls_string_view member_name;
	if (!splitMemberName(name, &enum_name, &member_name)) return false;
	const i32 enum_idx = findEnum(enum_name);
	if (enum_idx < 0) return false;
	EnumDecl& en = m_module.enums[enum_idx];
	for (i32 i = 0; i < en.members.size(); ++i) {
		if (!equalStrings(en.members[i].name, member_name)) continue;
		*value = makeI32(en.members[i].value);
		return true;
	}
	m_output.error("Unknown enum member '", member_name, "'");
	return true;
}

Runtime::Binding* Runtime::findBinding(ls_string_view name) {
	for (i32 f = m_frames.size() - 1; f >= 0; --f) {
		Frame& frame = m_frames[f];
		for (i32 i = frame.bindings.size() - 1; i >= 0; --i) {
			if (equalStrings(frame.bindings[i].name, name)) return &frame.bindings[i];
		}
	}
	for (i32 i = m_globals.size() - 1; i >= 0; --i) {
		if (equalStrings(m_globals[i].name, name)) return &m_globals[i];
	}
	return nullptr;
}

Value* Runtime::bindingValuePtr(Binding& binding) const {
	return binding.alias ? binding.alias : &binding.value;
}

const Value& Runtime::bindingValue(const Binding& binding) const {
	return binding.alias ? *binding.alias : binding.value;
}

bool Runtime::callFunction(FunctionDecl& fn, std::span<const Value> args, std::span<Value*> ref_args, Value* result) {
	m_frames.emplace_back();
	Frame& frame = m_frames.back();
	for (i32 i = 0; i < fn.params.size(); ++i) {
		Binding& b = frame.bindings.emplace_back();
		b.name = fn.params[i].name;
		if (fn.params[i].is_ref && u32(i) < ref_args.size() && ref_args[i]) {
			b.alias = ref_args[i];
		}
		else {
			b.value = u32(i) < args.size() ? args[i] : Value{};
		}
	}
	Value ret;
	FlowSignal flow = FlowSignal::NONE;
	ls_string_view flow_label;
	execStmt(fn.body, &ret, &flow, &flow_label);
	m_frames.pop_back();
	if (result) *result = ret;
	return !m_output.has_error;
}

bool Runtime::callNativeFunction(NativeFunctionDecl& fn, std::span<const Value> args, Value* result) {
	if (!fn.callback) {
		m_output.error("Native function '", fn.name, "' has no callback");
		return false;
	}
	if (!fn.callback(args, result, fn.userdata)) {
		m_output.error("Native function '", fn.name, "' failed");
		return false;
	}
	return true;
}

bool Runtime::initializeGlobals() {
	if (m_globals_initialized) return true;
	m_globals_initialized = true;
	for (GlobalDecl& global : m_module.globals) {
		Binding& binding = m_globals.emplace_back();
		binding.name = global.name;
		binding.is_const = global.is_const;
		binding.value = global.expr >= 0 ? evalExpr(global.expr) : makeDefault(global.type);
		if (m_output.has_error) return false;
	}
	return true;
}

Value Runtime::makeDefault(TypeRef type) {
	if (type.nullable) return makeNull();
	Value v;
	v.type = type;
	if (type.kind == TypeRef::STRUCT) {
		v.fields = allocateObject<std::vector<Value>>(m_module.host);
		m_owned_arrays.push_back(v.fields);
		StructDecl& s = m_module.structs[type.struct_index];
		for (FieldDecl& field : s.fields) v.fields->push_back(makeDefault(field.type));
	}
	else if (type.kind == TypeRef::ARRAY) {
		v.fields = allocateObject<std::vector<Value>>(m_module.host);
		m_owned_arrays.push_back(v.fields);
		v.fields->resize(type.array_size);
		TypeRef elem(type.element_kind, type.element_name, type.struct_index, type.token, false);
		for (i32 i = 0; i < type.array_size; ++i) (*v.fields)[i] = makeDefault(elem);
	}
	else if (type.kind == TypeRef::NATIVE) {
		v.i = -1;
		v.ptr = nullptr;
	}
	return v;
}

Value Runtime::makeStruct(TypeRef type, Expr& e) {
	Value v = makeDefault(type);
	for (i32 i = 0; i < e.args.size(); ++i) (*v.fields)[i] = evalExpr(e.args[i]);
	return v;
}

static bool asBool(const Value& v) {
	switch (v.type.kind) {
		case TypeRef::BOOL: return v.b;
		case TypeRef::F64: return v.d != 0;
		case TypeRef::F32: return v.f != 0;
		case TypeRef::I8:
		case TypeRef::I16:
		case TypeRef::I32:
		case TypeRef::I64: return v.i64 != 0;
		case TypeRef::U8:
		case TypeRef::U16:
		case TypeRef::U32:
		case TypeRef::U64: return v.u64 != 0;
		default: return false;
	}
}

Value Runtime::evalExpr(i32 expr_idx) {
	Expr& e = m_module.expressions[expr_idx];
	switch (e.kind) {
		case Expr::NUMBER: {
			if (e.type.kind == TypeRef::F64) return makeF64(e.number);
			if (e.type.kind == TypeRef::F32) return makeF32((float)e.number);
			if (isUnsignedIntegral(e.type.kind)) return makeUnsignedIntegral(e.type.kind, (u64)e.number);
			if (isSignedIntegral(e.type.kind)) return makeSignedIntegral(e.type.kind, (i64)e.number);
			return makeI32((i32)e.number);
		}
		case Expr::STRING_LITERAL: return makeString(e.string);
		case Expr::BOOL_LITERAL: {
			Value v;
			v.type = {TypeRef::BOOL, {}, -1};
			v.b = e.boolean;
			return v;
		}
		case Expr::NULL_LITERAL: return makeNull();
		case Expr::VAR: {
			Binding* b = findBinding(e.name);
			if (!b) {
				m_output.errorAt(e.token, "Unknown variable '", e.name, "'");
				return {};
			}
			return bindingValue(*b);
		}
		case Expr::FUNCTION_REF:
			return makeFunction(e.type, e.left, e.boolean);
		case Expr::FIELD: {
			Value enum_value;
			if (evalQualifiedEnumMember(getExpressionName(expr_idx), &enum_value)) return enum_value;
			Value base = evalExpr(e.left);
			StructDecl& s = m_module.structs[base.type.struct_index];
			for (i32 i = 0; i < s.fields.size(); ++i) if (equalStrings(s.fields[i].name, e.name)) return (*base.fields)[i];
			m_output.errorAt(e.token, "Unknown field '", e.name, "'");
			return {};
		}
		case Expr::INDEX: {
			Value base = evalExpr(e.left);
			Value index = evalExpr(e.right);
			const i32 i = (i32)asI64(index);
			if (!base.fields || i < 0 || i >= base.fields->size()) {
				m_output.errorAt(e.token, "Array index out of range");
				return {};
			}
			return (*base.fields)[i];
		}
		case Expr::UNARY: {
			Value right = evalExpr(e.right);
			if (e.token.type == Token::NOT) {
				Value v;
				v.type = {TypeRef::BOOL, {}, -1};
				v.b = !asBool(right);
				return v;
			}
			if (right.type.kind == TypeRef::F64) return makeF64(-right.d);
			if (right.type.kind == TypeRef::F32) return makeF32(-right.f);
			if (isUnsignedIntegral(right.type.kind)) return makeUnsignedIntegral(right.type.kind, (u64)(-(i64)asU64(right)));
			return makeSignedIntegral(right.type.kind, -asI64(right));
		}
		case Expr::BINARY: {
			if (e.token.type == Token::AND) {
				Value left = evalExpr(e.left);
				if (!asBool(left)) return makeBool(false);
				return makeBool(asBool(evalExpr(e.right)));
			}
			if (e.token.type == Token::OR) {
				Value left = evalExpr(e.left);
				if (asBool(left)) return makeBool(true);
				return makeBool(asBool(evalExpr(e.right)));
			}
			return evalBinary(e);
		}
		case Expr::CALL: {
			const ls_string_view callee_name = empty(e.qualified_name) ? getExpressionName(e.left) : e.qualified_name;
			const i32 fn_idx = findFunction(callee_name);
			const i32 native_idx = findNativeFunction(callee_name);
			std::vector<Value> args;
			std::vector<Value*> ref_args;
			if (fn_idx < 0 && native_idx < 0) {
				Value callee = evalExpr(e.left);
				if (callee.type.kind != TypeRef::FUNCTION) {
					m_output.errorAt(e.token, "Invalid function call");
					return {};
				}
				for (i32 arg : e.args) args.push_back(evalExpr(arg));
				Value res;
				if (callee.b) callNativeFunction(m_module.native_functions[callee.i], args, &res);
				else callFunction(m_module.functions[callee.i], args, std::span<Value*>(), &res);
				return res;
			}
			if (fn_idx >= 0) {
				FunctionDecl& fn = m_module.functions[fn_idx];
				const i32 receiver_arg_count = e.method_receiver >= 0 ? 1 : 0;
				for (i32 i = 0; i < fn.params.size(); ++i) {
					const i32 arg_idx = i - receiver_arg_count;
					const i32 expr_idx = i == 0 && e.method_receiver >= 0 ? e.method_receiver : e.args[arg_idx];
					if (fn.params[i].is_ref) {
						Expr& arg_expr = m_module.expressions[expr_idx];
						const i32 target_expr = arg_expr.kind == Expr::REF ? arg_expr.right : expr_idx;
						bool is_const = false;
						Value* target = resolveLValue(target_expr, &is_const);
						if (!target) return {};
						args.push_back(*target);
						ref_args.push_back(target);
					}
					else {
						args.push_back(evalExpr(expr_idx));
						ref_args.push_back(nullptr);
					}
				}
			}
			else {
				if (e.method_receiver >= 0) args.push_back(evalExpr(e.method_receiver));
				for (i32 arg : e.args) args.push_back(evalExpr(arg));
			}
			Value res;
			if (fn_idx >= 0) callFunction(m_module.functions[fn_idx], args, ref_args, &res);
			else callNativeFunction(m_module.native_functions[native_idx], args, &res);
			return res;
		}
		case Expr::REF: return evalExpr(e.right);
		case Expr::CAST: return castValue(evalExpr(e.left), e.cast_type);
		case Expr::STRUCT_LITERAL:
		case Expr::CONSTRUCTOR: return makeStruct(e.type, e);		case Expr::ENUM_LITERAL: {
		const i32 enum_idx = e.type.struct_index;
		if (enum_idx < 0 || enum_idx >= m_module.enums.size()) {
			m_output.errorAt(e.token, "Invalid enum value");
			return {};
		}
		EnumDecl& en = m_module.enums[enum_idx];
		for (i32 i = 0; i < en.members.size(); ++i) {
			if (equalStrings(en.members[i].name, e.name)) {
				return makeI32(en.members[i].value);
			}
		}
		m_output.errorAt(e.token, "Unknown enum member '", e.name, "'");
		return {};
	}		}
	ASSERT(false);
	return {};
}

static bool isFloat(TypeRef::Kind kind) {
	return kind == TypeRef::F32 || kind == TypeRef::F64;
}

Value Runtime::evalBinary(Expr& e) {
	Value a = evalExpr(e.left);
	Value b = evalExpr(e.right);
	if (e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL) {
		if (a.type.kind == TypeRef::NULL_VALUE || b.type.kind == TypeRef::NULL_VALUE) {
			const bool both_null = a.type.kind == TypeRef::NULL_VALUE && b.type.kind == TypeRef::NULL_VALUE;
			return makeBool(e.token.type == Token::EQUAL_EQUAL ? both_null : !both_null);
		}
	}
	const bool is_float = isFloat(a.type.kind) || isFloat(b.type.kind);
	if (e.token.type == Token::PLUS && a.type.kind == TypeRef::STRING && b.type.kind == TypeRef::STRING) {
		return concatStrings(a.string, b.string);
	}
	const double ad = asF64(a);
	const double bd = asF64(b);
	const i64 ai = asI64(a);
	const i64 bi = asI64(b);
	const u64 au = asU64(a);
	const u64 bu = asU64(b);
	if (!is_float && (e.token.type == Token::SLASH || e.token.type == Token::PERCENT)) {
		const bool zero = isUnsignedIntegral(e.type.kind) ? bu == 0 : bi == 0;
		if (zero) {
			m_output.errorAt(e.token, "Division or modulo by zero");
			return {};
		}
	}
	switch (e.token.type) {
		case Token::PLUS:
			if (is_float) return makeFloatLike(e.type.kind, ad + bd);
			return isUnsignedIntegral(e.type.kind) ? makeUnsignedIntegral(e.type.kind, au + bu) : makeSignedIntegral(e.type.kind, ai + bi);
		case Token::MINUS:
			if (is_float) return makeFloatLike(e.type.kind, ad - bd);
			return isUnsignedIntegral(e.type.kind) ? makeUnsignedIntegral(e.type.kind, au - bu) : makeSignedIntegral(e.type.kind, ai - bi);
		case Token::STAR:
			if (is_float) return makeFloatLike(e.type.kind, ad * bd);
			return isUnsignedIntegral(e.type.kind) ? makeUnsignedIntegral(e.type.kind, au * bu) : makeSignedIntegral(e.type.kind, ai * bi);
		case Token::SLASH:
			if (is_float) return makeFloatLike(e.type.kind, ad / bd);
			return isUnsignedIntegral(e.type.kind) ? makeUnsignedIntegral(e.type.kind, au / bu) : makeSignedIntegral(e.type.kind, ai / bi);
		case Token::PERCENT:
			if (is_float) {
				m_output.errorAt(e.token, "Modulo operation requires integer operands");
				return {};
			}
			return isUnsignedIntegral(e.type.kind) ? makeUnsignedIntegral(e.type.kind, au % bu) : makeSignedIntegral(e.type.kind, ai % bi);
		case Token::GT: return makeBool(ad > bd);
		case Token::LT: return makeBool(ad < bd);
		case Token::GT_EQUAL: return makeBool(ad >= bd);
		case Token::LT_EQUAL: return makeBool(ad <= bd);
		case Token::EQUAL_EQUAL: return makeBool(ad == bd);
		case Token::BANG_EQUAL: return makeBool(ad != bd);
		case Token::AND: return makeBool(asBool(a) && asBool(b));
		case Token::OR: return makeBool(asBool(a) || asBool(b));
		default: ASSERT(false); return {};
	}
}

Value makeBool(bool value) {
	Value v;
	v.type = {TypeRef::BOOL, {}, -1};
	v.b = value;
	return v;
}

Value Runtime::concatStrings(ls_string_view a, ls_string_view b) {
	const u32 total_size = (u32)(size(a) + size(b));
	char* buffer = (char*)allocateMemory(m_module.host, total_size + 1, alignof(char));
	char* out = buffer;
	for (const char* c = data(a); c != data(a) + size(a); ++c) *out++ = *c;
	for (const char* c = data(b); c != data(b) + size(b); ++c) *out++ = *c;
	*out = '\0';
	m_owned_strings.push_back(buffer);
	return makeString(ls_string_view{buffer, buffer + total_size});
}

Value Runtime::castValue(Value value, TypeRef type) {
	if (type.kind == TypeRef::ENUM) {
		Value v = makeI32((i32)asI64(value));
		v.type = type;
		return v;
	}
	switch (type.kind) {
		case TypeRef::BOOL: return makeBool(asBool(value));
		case TypeRef::I8:
		case TypeRef::I16:
		case TypeRef::I32:
		case TypeRef::I64: return makeSignedIntegral(type.kind, asI64(value));
		case TypeRef::U8:
		case TypeRef::U16:
		case TypeRef::U32:
		case TypeRef::U64: return makeUnsignedIntegral(type.kind, asU64(value));
		case TypeRef::F32:
		case TypeRef::F64: return makeFloatLike(type.kind, asF64(value));
		default: ASSERT(false); return {};
	}
}

bool Runtime::equalValues(const Value& a, const Value& b) const {
	if (a.type.kind == TypeRef::STRING || b.type.kind == TypeRef::STRING) return equalStrings(a.string, b.string);
	if (isFloat(a.type.kind) || isFloat(b.type.kind)) return asF64(a) == asF64(b);
	if (isUnsignedIntegral(a.type.kind) || isUnsignedIntegral(b.type.kind)) return asU64(a) == asU64(b);
	if (a.type.kind == TypeRef::BOOL || b.type.kind == TypeRef::BOOL) return asBool(a) == asBool(b);
	return asI64(a) == asI64(b);
}

bool Runtime::matchPattern(const Value& subject, MatchPattern& pattern) {
	if (pattern.kind == MatchPattern::DEFAULT) return true;
	Value start = evalExpr(pattern.start_expr);
	if (pattern.kind == MatchPattern::VALUE) return equalValues(subject, start);
	Value end = evalExpr(pattern.end_expr);
	const bool is_float = isFloat(subject.type.kind) || isFloat(start.type.kind) || isFloat(end.type.kind);
	if (is_float) {
		const double value = asF64(subject);
		return value >= asF64(start) && value <= asF64(end);
	}
	if (isUnsignedIntegral(subject.type.kind)) {
		const u64 value = asU64(subject);
		return value >= asU64(start) && value <= asU64(end);
	}
	const i64 value = asI64(subject);
	return value >= asI64(start) && value <= asI64(end);
}

bool Runtime::matchArm(const Value& subject, MatchArm& arm) {
	for (i32 pattern_idx : arm.patterns) {
		if (matchPattern(subject, m_module.match_patterns[pattern_idx])) return true;
	}
	return false;
}

Value* Runtime::resolveLValue(i32 expr_idx, bool* is_const) {
	Expr& e = m_module.expressions[expr_idx];
	if (e.kind == Expr::VAR) {
		Binding* b = findBinding(e.name);
		if (!b) {
			m_output.errorAt(e.token, "Unknown variable '", e.name, "'");
			return nullptr;
		}
		if (is_const) *is_const = b->is_const;
		return bindingValuePtr(*b);
	}
	if (e.kind == Expr::FIELD) {
		Value* base = resolveLValue(e.left, is_const);
		if (!base) return nullptr;
		StructDecl& s = m_module.structs[base->type.struct_index];
		for (i32 i = 0; i < s.fields.size(); ++i) if (equalStrings(s.fields[i].name, e.name)) return &(*base->fields)[i];
	}
	if (e.kind == Expr::INDEX) {
		Value* base = resolveLValue(e.left, is_const);
		if (!base) return nullptr;
		const i32 idx = (i32)asI64(evalExpr(e.right));
		if (!base->fields || idx < 0 || idx >= base->fields->size()) {
			m_output.errorAt(e.token, "Array index out of range");
			return nullptr;
		}
		return &(*base->fields)[idx];
	}
	m_output.errorAt(e.token, "Invalid assignment target");
	return nullptr;
}

void Runtime::assign(i32 left_expr, Token::Type op, Value value) {
	bool is_const = false;
	Value* dst = resolveLValue(left_expr, &is_const);
	if (!dst) return;
	if (is_const) {
		m_output.error("Can not assign to const"); // TODO token location
		return;
	}
	if (op == Token::EQUAL) {
		*dst = value;
		return;
	}
	Value cur = *dst;
	Expr fake;
	fake.left = -1;
	const bool is_float = isFloat(cur.type.kind);
	const bool is_f64 = cur.type.kind == TypeRef::F64;
	const double cv = is_f64 ? cur.d : is_float ? (double)cur.f : asF64(cur);
	const double vv = asF64(value);
	if (op == Token::SLASH_EQUAL && !is_float) {
		const bool zero = isUnsignedIntegral(cur.type.kind) ? asU64(value) == 0 : asI64(value) == 0;
		if (zero) {
			m_output.error("Division or modulo by zero");
			return;
		}
	}
	switch (op) {
		case Token::PLUS_EQUAL: *dst = is_float ? makeFloatLike(cur.type.kind, cv + vv) : (isUnsignedIntegral(cur.type.kind) ? makeUnsignedIntegral(cur.type.kind, asU64(cur) + asU64(value)) : makeSignedIntegral(cur.type.kind, asI64(cur) + asI64(value))); break;
		case Token::MINUS_EQUAL: *dst = is_float ? makeFloatLike(cur.type.kind, cv - vv) : (isUnsignedIntegral(cur.type.kind) ? makeUnsignedIntegral(cur.type.kind, asU64(cur) - asU64(value)) : makeSignedIntegral(cur.type.kind, asI64(cur) - asI64(value))); break;
		case Token::STAR_EQUAL: *dst = is_float ? makeFloatLike(cur.type.kind, cv * vv) : (isUnsignedIntegral(cur.type.kind) ? makeUnsignedIntegral(cur.type.kind, asU64(cur) * asU64(value)) : makeSignedIntegral(cur.type.kind, asI64(cur) * asI64(value))); break;
		case Token::SLASH_EQUAL: *dst = is_float ? makeFloatLike(cur.type.kind, cv / vv) : (isUnsignedIntegral(cur.type.kind) ? makeUnsignedIntegral(cur.type.kind, asU64(cur) / asU64(value)) : makeSignedIntegral(cur.type.kind, asI64(cur) / asI64(value))); break;
		default: ASSERT(false); break;
	}
}

void Runtime::execStmt(i32 stmt_idx, Value* ret, FlowSignal* flow, ls_string_view* flow_label, bool allow_after_return) {
	if (stmt_idx < 0) return;
	if ((!allow_after_return && *flow == FlowSignal::RETURN) || m_output.has_error) return;
	Stmt& stmt = m_module.statements[stmt_idx];
	switch (stmt.kind) {
		case Stmt::BLOCK: {
			const i32 old_size = m_frames.back().bindings.size();
			const i32 old_deferred_size = m_deferred_statements.size();
			for (i32 child : stmt.children) {
				if (child < 0) continue;
				execStmt(child, ret, flow, flow_label);
				if (*flow != FlowSignal::NONE || m_output.has_error) break;
			}
			while (m_deferred_statements.size() > old_deferred_size && !m_output.has_error) {
				const i32 deferred_stmt = m_deferred_statements.back();
				m_deferred_statements.pop_back();
				execStmt(deferred_stmt, ret, flow, flow_label, true);
			}
			m_frames.back().bindings.resize(old_size);
			break;
		}
		case Stmt::VAR_DECL: {
			Binding& b = m_frames.back().bindings.emplace_back();
			b.name = stmt.name;
			b.is_const = stmt.is_const;
			b.value = stmt.expr >= 0 ? evalExpr(stmt.expr) : makeDefault(stmt.type);
			break;
		}
		case Stmt::FN_DECL:
			break;
		case Stmt::EXPR: evalExpr(stmt.expr); break;
		case Stmt::ASSIGN: assign(stmt.left, stmt.assign_op, evalExpr(stmt.right)); break;
		case Stmt::BREAK:
			*flow = FlowSignal::BREAK;
			*flow_label = stmt.name;
			break;
		case Stmt::CONTINUE:
			*flow = FlowSignal::CONTINUE;
			*flow_label = stmt.name;
			break;
		case Stmt::WHILE:
			while (asBool(evalExpr(stmt.expr)) && !m_output.has_error) {
				if (stmt.right >= 0) execStmt(stmt.right, ret, flow, flow_label);
				if (m_output.has_error) return;
				if (*flow == FlowSignal::RETURN) return;
				if (*flow == FlowSignal::BREAK) {
					if (empty(*flow_label) || equalStrings(*flow_label, stmt.name)) {
						*flow = FlowSignal::NONE;
						*flow_label = {};
						break;
					}
					return;
				}
				if (*flow == FlowSignal::CONTINUE) {
					if (empty(*flow_label) || equalStrings(*flow_label, stmt.name)) {
						*flow = FlowSignal::NONE;
						*flow_label = {};
						continue;
					}
					return;
				}
			}
			break;
		case Stmt::IF:
			if (asBool(evalExpr(stmt.expr))) {
				if (stmt.left >= 0) execStmt(stmt.left, ret, flow, flow_label);
			}
			else if (stmt.right >= 0) {
				execStmt(stmt.right, ret, flow, flow_label);
			}
			break;
		case Stmt::MATCH: {
			Value subject = evalExpr(stmt.expr);
			for (i32 arm_idx : stmt.children) {
				if (arm_idx < 0) continue;
				MatchArm& arm = m_module.match_arms[arm_idx];
				if (!matchArm(subject, arm)) continue;
				if (arm.stmt >= 0) execStmt(arm.stmt, ret, flow, flow_label);
				break;
			}
			break;
		}
		case Stmt::RETURN:
			*ret = stmt.expr >= 0 ? evalExpr(stmt.expr) : Value{};
			*flow = FlowSignal::RETURN;
			break;
		case Stmt::DEFER:
			m_deferred_statements.push_back(stmt.left);
			break;
	}
}
