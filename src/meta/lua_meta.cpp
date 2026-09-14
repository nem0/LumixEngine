#include "meta.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>

#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

namespace {

MetaData* g_data = nullptr;

StringView skipWhitespaces(StringView value) {
	while (value.begin != value.end && isspace(*value.begin)) ++value.begin;
	return value;
}

bool isWordSeparator(char c) {
	return isspace(c) || c == '(' || c == ',' || c == '{' || c == ';' || c == '}' || c == '<';
}

StringView consumeWordLocal(StringView& str) {
	str = skipWhitespaces(str);
	StringView word = {str.begin, str.begin};
	while (word.end != str.end && !isWordSeparator(*word.end)) ++word.end;
	if (word.begin == word.end && word.end < str.end) ++word.end;
	str.begin = word.end;
	str = skipWhitespaces(str);
	return word;
}

StringView peekWord(StringView str) { return consumeWordLocal(str); }

bool readLine(StringView& content, StringView& line) {
	if (content.size() == 0) return false;
	line = {content.begin, content.begin};
	while (line.end != content.end && *line.end != '\n') ++line.end;
	line = skipWhitespaces(line);
	content.begin = line.end;
	if (content.begin != content.end) ++content.begin;
	if (line.end > line.begin && *(line.end - 1) == '\r') --line.end;
	return true;
}

void toID(StringView name, Span<char> out) {
	char* dst = out.begin;
	const char* src = name.begin;
	bool prev_lowercase = false;
	while (dst < out.end - 1 && src < name.end) {
		if (*src == ' ') *dst = '_';
		else if (*src >= 'A' && *src <= 'Z') {
			if (src != name.begin && prev_lowercase) *dst++ = '_';
			if (dst != out.end - 1) *dst = *src | 0x20;
		}
		else {
			prev_lowercase = *src >= 'a' && *src <= 'z';
			*dst = *src;
		}
		++dst;
		++src;
	}
	*dst = 0;
}

Struct* getStruct(StringView name) {
	for (Struct& s : g_data->structs) {
		if (equal(s.name, name)) return &s;
	}
	return nullptr;
}

Object* getObject(StringView name) {
	if (*(name.end - 1) == '*') {
		--name.end;
	}
	for (Object& o : g_data->objects) {
		if (equal(o.name, name)) return &o;
	}
	return nullptr;
}

Enum* getEnum(Module& module, StringView name) {
	for (Enum& e : g_data->enums) {
		if (equal(e.name, name) || equal(e.full, name)) return &e;
	}
	for (Enum& e : module.enums) {
		if (equal(e.name, name) || equal(e.full, name)) return &e;
	}
	return nullptr;
}

Object* findObject(StringView object_name) {
	for (Object& o : g_data->objects) {
		if (equal(o.full, object_name)) return &o;
	}
	return nullptr;
}

i32 pushInOutArgs(OutputStream& out, StringView args) {
	i32 num = 0;
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		if (!arg.is_ref || arg.is_const) return;

		++num;
		L("\tLuaWrapper::push(L, ", arg.name, ");");
	});
	return num;
}

// replace ':' in namespaced types with '_'
// e.g. ui::Document => ui_Document
static void outputLuaObjectTypename(OutputStream& out, StringView type) {
	for (const char* c = type.begin; c < type.end; ++c) {
		if (*c == ':') {
			if (c == type.begin || *(c - 1) != ':') out.add('_');
		}
		else out.add(*c);
	}
}

void wrap(OutputStream& out, Module& m, Function& f) {
	const bool return_is_reference = f.return_type.size() > 0 && f.return_type[f.return_type.size() - 1] == '&';
	L("int ",m.name,"_",f.name,"(lua_State* L) {");
	L("LuaWrapper::checkTableArg(L, 1);");
	L(m.name,"* module;");
	L("if (!LuaWrapper::checkField(L, 1, \"_module\", &module)) luaL_argerror(L, 1, \"Module expected\");");

	i32 arg_idx = -1;
	StringView args = f.args;
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		++arg_idx;
		Object* obj = findObject(arg.type);
		if (obj) {
			L("\tif(!LuaWrapper::checkField(L, ",(arg_idx + 2),", \"_object\", &",arg.name,")) luaL_error(L, \"Invalid argument\");");
		}
		else {
			L("\tauto ",arg.name," = LuaWrapper::checkArg<", (arg.is_const && arg.is_ptr ? "const " : ""), arg.type, (arg.is_ptr ? "*" : ""), ">(L, ",(arg_idx + 2),");");
		}
	});

	bool has_return = !equal(f.return_type, "void");
	if (has_return) out.add("\tLuaWrapper::push(L, ");
	if (return_is_reference) out.add("&");
	out.add("\tmodule->", f.name, "(");
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		out.add(arg.name);
	});
	out.add(")");
	if (has_return) {
		L(");");
	} else {
		L(";");
	}

	// push inout args
	i32 return_count = has_return ? 1 : 0;
	return_count += pushInOutArgs(out, args);

	L("\treturn ", return_count, ";");
	L("}" OUT_ENDL);
}

void wrap(OutputStream& out, Module& m, Component& c, Function& f) {
	StringView name = f.attributes.alias;
	if (name.size() == 0) name = f.name;
	L("int ",c.name,"_",name,"(lua_State* L) {");
	L("\tauto [imodule, entity] = checkComponent(L);");
	L("\tauto* module = (",m.name,"*)imodule;");
	
	i32 arg_idx = -1;
	StringView args = f.args;
	forEachArg(args, [&](const Arg& arg, bool is_first) {
		++arg_idx;
		if (is_first) return; // skip entity, we alredy have it
		Object* obj_arg = findObject(arg.type);
		if (obj_arg) {
			L("\tif(!LuaWrapper::checkField(L, ",(arg_idx + 1),", \"_object\", &",arg.name,")) luaL_error(L, \"Invalid argument\");");
		}
		else {
			L("\tauto ",arg.name," = LuaWrapper::checkArg<", (arg.is_const && arg.is_ptr ? "const " : ""), arg.type, (arg.is_ptr ? "*" : ""), ">(L, ",(arg_idx + 1),");");
		}
	});

	bool has_return = !equal(f.return_type, "void");
	if (has_return) out.add("\tLuaWrapper::push(L, ");
	out.add("\tmodule->",f.name,"(entity");
	forEachArg(args, [&](const Arg& arg, bool is_first){
		if (is_first) return;
		out.add(", ", arg.name);
	});
	out.add(")");
	if (has_return) {
		L(");");
	}
	else {
		L(";");
	}
	i32 return_count = has_return ? 1 : 0;

	// push inout args
	return_count += pushInOutArgs(out, args);

	L("\treturn ", return_count, ";");
	L("}", OUT_ENDL);
}

void wrap(OutputStream& out, StringView module, StringView component, StringView property_name, StringView method_name, StringView args, bool is_getter) {
	L("int ", (is_getter ? "get" : "set"), component, property_name, "(lua_State* L) {");
	L("\tauto [imodule, entity] = checkComponent(L);");
	L("\tauto* module = (",module,"*)imodule;");
	
	i32 idx = 2;
	forEachArg(args, [&](const Arg& arg, bool){
		L("\tauto ", arg.name, " = LuaWrapper::checkArg<",arg.type,">(L, ",idx,");");
		++idx;
	});
	if (is_getter) out.add("\tLuaWrapper::push(L, module->", method_name, "(");
	else out.add("\tmodule->", method_name, "(");
	forEachArg(args, [&](const Arg& arg, bool first){
		if (!first) out.add(", ");
		out.add(arg.name);
	});
	if (is_getter) out.add(")");
	L(");");
	if (is_getter) L("\treturn 1;");
	else L("\treturn 0;");
	L("}" OUT_ENDL);
}

StringView pickLabel(StringView base, StringView spec) {
	if (spec.size() > 0) return spec;
	return base;
}

void serializeMain(OutputStream& out) {
	L("namespace Lumix {" OUT_ENDL);
	L("void registerLuaAPI(lua_State* L) {");
	L("	lua_newtable(L);");
	L("	lua_setglobal(L, \"LumixModules\");");

	for (Module& m : g_data->modules) {
		if (m.functions.size == 0) continue;
		L("{");
		L("	lua_newtable(L);");
		L("	lua_getglobal(L, \"LumixModules\");");
		L("	lua_pushvalue(L, -2);");
		L("	lua_setfield(L, -2, \"",m.id,"\");");
		L("	lua_pop(L, 1);");
		L("	lua_pushvalue(L, -1);");
		L("	lua_setfield(L, -2, \"__index\");");
		L("	lua_pushcfunction(L, lua_new_module, \"new\");");
		L("	lua_setfield(L, -2, \"new\");");
		for (Function& f : m.functions) {
			L("lua_pushcfunction(L, ",m.name,"_",f.name,", \"",f.name,"\");");
			L("lua_setfield(L, -2, \"",pickLabel(f.name, f.attributes.alias),"\");");
		}
		L("	lua_pop(L, 1);");
		L("}");
	}

	for (Object& o : g_data->objects) {
		L("{");
		L("lua_getglobal(L, \"LumixAPI\");");
		L("lua_newtable(L);");
		L("lua_pushvalue(L, -1);");
		
		out.add("lua_setfield(L, -3, \"");
		outputLuaObjectTypename(out, o.full);
		out.add("\");" OUT_ENDL);
		L("lua_pushvalue(L, -1);");
		L("lua_setfield(L, -2, \"__index\");");

		for (Function& f : o.functions) {
			L("{");
			L("auto proxy = [](lua_State* L) -> int {");
				L("LuaWrapper::checkTableArg(L, 1); // self");
				L(o.full, "* obj;");
				L("if (!LuaWrapper::checkField(L, 1, \"_value\", &obj)) luaL_error(L, \"Invalid object\");");
				L("if (!obj) return 0;");
				
				i32 idx = 0;
				forEachArg(f.args, [&](const Arg& arg, bool){
					++idx;
					if (arg.is_ptr && equal(arg.type, "World")) {
						L("\tWorld* ", arg.name, ";");
						L("\tif (!LuaWrapper::checkField(L, ",(idx + 1),", \"value\", &",arg.name,")) luaL_error(L, \"Invalid argument\");");
					}
					else if (arg.is_const && arg.is_ptr && equal(arg.type, "char")) {
						L("\tauto ",arg.name," = LuaWrapper::checkArg<const char*>(L, ",(idx + 1),");");
					}
					else {
						L("auto ",arg.name," = LuaWrapper::checkArg<",arg.type,">(L, ",(idx + 1),");");
					}
				});
				
				bool has_return = !equal(f.return_type, "void");
				if (has_return) out.add("auto res = ");
				
				out.add("obj->",f.name,"(");
				forEachArg(f.args, [&](const Arg& arg, bool first){
					if (!first) out.add(", ");
					out.add(arg.name);
				});
				L(");");
				
				i32 num_returns = 0;
				if (has_return) {
					++num_returns;
					L("LuaWrapper::push(L, res);");
				}
				num_returns += pushInOutArgs(out, f.args);
				L("return ", num_returns, ";");
			L("};");

			L("const char* name = \"", f.name, "\";");
			L("lua_pushcfunction(L, proxy, name);");
			L("lua_setfield(L, -2, name);");
			L("}");
		}
		L("lua_pop(L, 2);");
		L("}");
	}
	
	// Emit enums into LumixAPI as tables so Lua can access them as LumixAPI.<EnumName>.<Member>
	for (Enum& e : g_data->enums) {
		L("{");
		L("lua_getglobal(L, \"LumixAPI\");");
		L("lua_newtable(L);");
		for (Enumerator& en : e.values) {
			L("LuaWrapper::push(L, ", en.value, ");");
			L("lua_setfield(L, -2, \"", en.name, "\");");
		}
		L("lua_setfield(L, -2, \"", e.name, "\");");
		L("lua_pop(L, 1);");
		L("}");
	}

	for (Module& m : g_data->modules) {
		for (Enum& e : m.enums) {
			L("{");
			L("lua_getglobal(L, \"LumixAPI\");");
			L("lua_newtable(L);");
			for (Enumerator& en : e.values) {
				L("LuaWrapper::push(L, ", en.value, ");");
				L("lua_setfield(L, -2, \"", en.name, "\");");
			}
			L("lua_setfield(L, -2, \"", e.name, "\");");
			L("lua_pop(L, 1);");
			L("}");
		}
		
		for (Component& c : m.components) {
			L("\tregisterLuaComponent(L, \"",c.id,"\", ",c.id,"_getter, ",c.id,"_setter);");
		}
	}
	L("}");
	L("}" OUT_ENDL);

	formatCPP(out);
}

void serializeLuaPropertySetter(OutputStream& out, Module& m, Component& c) {
	L("int ",c.id,"_setter(lua_State* L) {");
	L("auto [imodule, entity] = checkComponent(L);");
	L("auto* module = (",m.name,"*)imodule;");
	L("const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);");
	L("XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));");
	L("switch (name_hash) {");
	
	bool is_array = false;
	for (Property& p : c.properties) {
		if (isBlob(p)) continue;

		// TODO check collisions
		XXH64_hash_t hash = XXH3_64bits(p.name.begin, p.name.size());
		if (p.is_var) {
			L("case /*",p.name,"*/",hash,": module->get",c.name,"(entity).",p.name," = LuaWrapper::checkArg<",p.type,">(L, 3); break;");
			continue;
		}
		
		if (p.getter_name.size() == 0) continue;
		if (p.setter_name.size() == 0) continue;
		
		char tmp[256];
		toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 255));

		hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash,": ");
		if (Enum* e = getEnum(m, p.type)) {
			L("module->",p.setter_name,"(entity, (",e->full,")LuaWrapper::checkArg<i32>(L, 3)); break;");
		}
		else {
			L("module->",p.setter_name,"(entity, LuaWrapper::checkArg<",p.type,">(L, 3)); break;");
		}
	}
	L("case 0:"); // to avoid emtpy switch (compiler error) in case we have 0 properties
	L("default: luaL_error(L, \"Unknown property %s\", prop_name); break;");
	L("}");

	L("return 0;");
	L("}" OUT_ENDL);
}

void serializeLuaArrayGetter(OutputStream& out, Module& m, Component& c, ArrayProperty& a) {
	L("using GetterModule = ",m.name,";");
	out.add(R"#(auto getter = [](lua_State* L) ->int {
		LuaWrapper::checkTableArg(L, 1); // self
		auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
		EntityRef entity{LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
		if (lua_type(L, 2) == LUA_TSTRING) {
			auto adder = [](lua_State* L) -> int  {
				auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
				EntityRef entity{LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
				module->add)#",a.name,R"#((entity, module->get)#",a.name,R"#(Count(entity));
				return 0;
			};

			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			if (equalStrings(prop_name, "add")) {
				LuaWrapper::push(L, module);
				LuaWrapper::push(L, entity.index);
				lua_pushcclosure(L, adder, "adder", 2);
				return 1;
			}
			else {
				luaL_error(L, "Unknown property %s", prop_name);
			}
		}

		auto getter = [](lua_State* L) -> int {
			LuaWrapper::checkTableArg(L, 1);
			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
			EntityRef entity {LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
			i32 index = LuaWrapper::toType<int>(L, lua_upvalueindex(3));
			XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));
			switch (name_hash) {
	)#");

	for (Property& child : a.children) {
		if (isBlob(child)) continue;
		if (child.getter_name.size() == 0) continue;
		
		char tmp[256];
		toID(pickLabel(child.name, child.attributes.label), Span(tmp, tmp + 256));
		XXH64_hash_t hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash,": ");
		if (getEnum(m, child.type)) {
			L("LuaWrapper::push(L, (i32)module->",child.getter_name,"(entity, index)); break;");
		}
		else {
			L("LuaWrapper::push(L, module->",child.getter_name,"(entity, index)); break;");
		}
	}
	L("default: { luaL_error(L, \"Unknown property %s\", prop_name); break; }");
	L("}");

	out.add(R"#(return 1;
		};

		auto setter = [](lua_State* L) -> int {
			LuaWrapper::checkTableArg(L, 1);
			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));
			auto* module = LuaWrapper::toType<GetterModule*>(L, lua_upvalueindex(1));
			EntityRef entity {LuaWrapper::toType<i32>(L, lua_upvalueindex(2))};
			i32 index = LuaWrapper::toType<int>(L, lua_upvalueindex(3));
			switch (name_hash) {
	)#");	

	for (Property& child : a.children) {
		if (isBlob(child)) continue;
		if (child.setter_name.size() == 0) continue;
		
		char tmp[256];
		toID(pickLabel(child.name, child.attributes.label), Span(tmp, tmp + 256));

		XXH64_hash_t hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash,": ");
		if (Enum* e = getEnum(m, child.type)) {
			L("module->",child.setter_name,"(entity, index, (",e->full,")LuaWrapper::checkArg<i32>(L, 3)); break;");
		}
		else {
			L("module->",child.setter_name,"(entity, index, LuaWrapper::checkArg<",child.type,">(L, 3)); break;");
		}
	}

	out.add(R"#(
			case 0:	
			default: { luaL_error(L, "Unknown property %s", prop_name); break; }
			}
			return 0;
			};

			i32 index = LuaWrapper::checkArg<i32>(L, 2) - 1;
			i32 num_elements = module->get)#",a.name,R"#(Count(entity);
			if (index >= num_elements) {
				lua_pushnil(L);
				return 1;
			}

			lua_newtable(L);
			lua_newtable(L);

			lua_pushlightuserdata(L, (void*)module);
			LuaWrapper::push(L, entity.index);
			LuaWrapper::push(L, index);
			lua_pushcclosure(L, getter, "getter", 3);
			lua_setfield(L, -2, "__index");

			lua_pushlightuserdata(L, (void*)module);
			LuaWrapper::push(L, entity.index);
			LuaWrapper::push(L, index);
			lua_pushcclosure(L, setter, "setter", 3);
			lua_setfield(L, -2, "__newindex");

			lua_setmetatable(L, -2);
			return 1;
		};

		lua_newtable(L); // {}
		lua_newtable(L); // {}, metatable
		LuaWrapper::push(L, module);
		LuaWrapper::push(L, entity.index);
		lua_pushcclosure(L, getter, "getter", 2);
		lua_setfield(L, -2, "__index"); // {}, mt
		lua_setmetatable(L, -2); // {}
	)#");
}

// TODO move lua stuff into separate file
void serializeLuaPropertyGetter(OutputStream& out, Module& m, Component& c) {
	L("int ",c.id,"_getter(lua_State* L) {");
	L("\tauto [imodule, entity] = checkComponent(L);");
	L("\tauto* module = (",m.name,"*)imodule;");

	if (equal(c.id, "lua_script")) {
		L("if (lua_isnumber(L, 2)) return lua_push_script_env(L, entity, module);");
	}

	L("const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);");
	L("XXH64_hash_t name_hash = XXH3_64bits(prop_name, strlen(prop_name));");
	L("switch (name_hash) {");

	for (ArrayProperty& a : c.arrays) {
		XXH64_hash_t hash = XXH3_64bits(a.id.begin, a.id.size());
		L("case /*",a.id,"*/",hash, ": {");
		serializeLuaArrayGetter(out, m, c, a);
		L("break;");
		L("}");
	}

	for (Property& p : c.properties) {
		if (isBlob(p)) continue;
		if (p.is_var) {
			XXH64_hash_t hash = XXH3_64bits(p.name.begin, p.name.size());
			out.add("case /*",p.name,"*/",hash, ": ");
			L("LuaWrapper::push(L, module->get",c.name,"(entity).",p.name,"); break;");
			continue;
		}
		if (p.getter_name.size() == 0) continue;

		char tmp[256];
		toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 255));
		XXH64_hash_t hash = XXH3_64bits(tmp, strlen(tmp));
		out.add("case /*",tmp,"*/",hash, ": ");
		
		if (getEnum(m, p.type)) {
			L("LuaWrapper::push(L, (i32)module->",p.getter_name,"(entity)); break;");
		}
		else {
			L("LuaWrapper::push(L, module->",p.getter_name,"(entity)); break;");
		}
	}

	for (Function& f : c.functions) {
		StringView name = pickLabel(f.name, f.attributes.alias);
		XXH64_hash_t hash = XXH3_64bits(name.begin, name.size());
		out.add("case /*",name,"*/",hash, ": ");
		L("lua_pushcfunction(L, ",c.name,"_",name,", \"",c.name,"_",name,"\"); break;");
	}
	L("case 0:"); // to avoid emtpy switch (compiler error) in case we have 0 properties
	L("default: { luaL_error(L, \"Unknown property %s\", prop_name); break; }");
	L("}");
	L("\treturn 1;");
	L("}" OUT_ENDL);
}

void serializeLuaHelpers(OutputStream& out) {
	// enums 
	L("namespace Lumix::LuaWrapper {");
	for (Enum& e : g_data->enums) {
		L(" void push(lua_State* L, ", e.full, " value) { LuaWrapper::push(L, (i32)value); }");
		L("template <> ", e.full, " checkArg<", e.full, ">(lua_State* L, int index) { return (", e.full, ")checkArg<i32>(L, index); }");
		out.add(OUT_ENDL);
	}

	// structs
	for (Struct& s : g_data->structs) {
		L("void push(lua_State* L, const ", s.full, "& value) {");
		L("\tlua_newtable(L);");
		for (StructVar& v : s.vars) {
			L("\tpush(L, value.", v.name, ");");
			L("\tlua_setfield(L, -2, \"", v.name, "\");");
		}
		L("}");

		L("template <> ", s.full, " checkArg<", s.full, ">(lua_State* L, int index) {");
		L("\t", s.full, " res;");
		L("\tif (!lua_istable(L, index)) luaL_argerror(L, index, \"expected table\");");
		for (StructVar& v : s.vars) {
			StringView type = v.type;
			for (Enum& e : g_data->enums) {
				if (equal(e.name, v.type)) {
					type = e.full;
					break;
				}
			}
			L("\tlua_getfield(L, index, \"", v.name, "\");");
			L("\tres.", v.name, " = checkArg<", type, ">(L, -1);");
			L("\tlua_pop(L, 1);");
		}
		L("\treturn res;");
		L("}");
	}
	
	// objects
	for (Object& o : g_data->objects) {
		L("void push(lua_State* L, ", o.full, "* value) {");
		out.add("\tpushObject(L, (void*)value, \"");
		outputLuaObjectTypename(out, o.full);
		out.add("\");" OUT_ENDL);
		L("}");
	}

	L("}");
}

void serializeLuaCAPI(OutputStream& out, Module& m) {
	L("namespace Lumix {");
	for (Function& f : m.functions) {
		wrap(out, m, f);
	}
	for (Component& c : m.components) {
		for (Function& f : c.functions) {
			wrap(out, m, c, f);
		}
		serializeLuaPropertyGetter(out, m, c);
		serializeLuaPropertySetter(out, m, c);
	}
	L("}" OUT_ENDL);
}

StringView toLuaType(StringView ctype) {
	if (equal(ctype, "void")) return makeStringView("()");

	#define C(CTYPE, LUATYPE) do { if (equal(ctype, #CTYPE)) return makeStringView(#LUATYPE); } while (false)
		C(int, number);
		C(const char *, string);
		C(const char*, string);
		C(char const *, string);
		C(Vec3, Vec3);
		C(Quat, Quat);
		C(Vec2, Vec2);
		C(Color, Color);
		C(DVec3, DVec3);
		C(EntityPtr, Entity?);
		C(EntityRef, Entity);
		C(Path, string);
		C(i32, number);
		C(u32, number);
		C(float, number);
		C(bool, boolean);
	#undef C

	// TODO structs	
	StringView base_type = ctype;
	if (base_type.size() > 0 && base_type[base_type.size() - 1] == '&') --base_type.end;
	Struct* s = getStruct(base_type);
	if (s) return s->name;

	Object* o = getObject(base_type);
	if (o) return o->name;

	return makeStringView("any");
}

void serializeLuaType(OutputStream& out, StringView self_type, const char* self_type_suffix, Function& f, bool skip_first_arg) {
	out.add("\t",pickLabel(f.name, f.attributes.alias),": (");
	out.add(self_type,self_type_suffix);
	forEachArg(f.args, [&](const Arg& arg, bool first){
		if (!first || !skip_first_arg) {
			out.add(", ", toLuaType(arg.type));
		}
	});
	out.add(") -> ");

	bool has_return = f.return_type.size() > 0 && !equal(f.return_type, "void");
	i32 num_returns = has_return ? 1 : 0;
	forEachArg(f.args, [&](const Arg& arg, bool){
		if (arg.is_ref && !arg.is_const) ++num_returns;
	});

	if (num_returns > 1) out.add("(");
	if (has_return) {
		out.add(toLuaType(f.return_type));
	}
	bool first_ret_val = !has_return;
	forEachArg(f.args, [&](const Arg& arg, bool){
		if (!arg.is_ref || arg.is_const) return;

		if (!first_ret_val) out.add(", ");
		out.add(toLuaType(arg.type));
		first_ret_val = false;
	});
	if (num_returns == 0) out.add("()");
	else if (num_returns > 1) out.add(")");
	out.add("," OUT_ENDL);
}

void serializeLuaTypes(OutputStream& out_formatted) {
	OutputStream out;
	out.add(R"#(
	export type Vec2 = {number}
	export type Vec3 = {number}
	export type Color = {number}
	export type Quat = {number}
	export type DVec3 = {number}
	declare ImGui: {
		AlignTextToFramePadding : () -> (),
		Begin : (string, boolean?) -> (boolean, boolean?),
		BeginChildFrame : (string, number, number) -> boolean,
		BeginMenu : (string, boolean) -> boolean,
		BeginPopup : (string) -> boolean,
		Button : (string) -> boolean,
		CalcTextSize : (string) -> (number, number),
		Checkbox : (string, boolean) -> (boolean, boolean),
		CloseCurrentPopup : () -> (),
		CollapsingHeader : (string) -> boolean,
		Columns : (number) -> (),
		DragFloat : (string, number) -> (boolean, number),
		DragInt : (string, number) -> (boolean, number),
		Dummy : (number, number) -> (),
		End : () -> (),
		EndChildFrame : () -> (),
		EndCombo : () -> (),
		EndMenu : () -> (),
		EndPopup : () -> (),
		GetColumnWidth : (number) -> number,
		GetDisplayWidth : () -> number,
		GetDisplayHeight : () -> number,
		GetOsImePosRequest : () -> (number, number),
		GetWindowWidth : () -> (),
		GetWindowHeight : () -> (),
		GetWindowPos : () -> any,
		Indent : (number) -> (),
		InputTextMultiline : (string, string) -> (boolean, string?),
		InputTextMultilineWithCallback : (string, string, (string, number, boolean) -> ()) -> (boolean, string?),
		InputText : (string, string) -> (boolean, string?),
		IsItemHovered : () -> boolean,
		IsKeyPressed : (number, boolean) -> boolean,
		IsMouseClicked : (number) -> boolean,
		IsMouseDown : (number) -> boolean,
		LabelText : (string, string) -> (),
		NewLine : () -> (),
		NextColumn : () -> (),
		OpenPopup : (string) -> (),
		PlotLines : (string, {number}, Vec2) -> (),
		PopItemWidth : () -> (),
		PopID : () -> (),
		PopStyleColor : (number) -> (),
		PopStyleVar : (number) -> (),
		PopItemWidth : () -> (),
		PushItemWidth : (number) -> (),
		PushID : (number) -> (),
		PushStyleColor : (number, any) -> (),
		PushStyleVar : (number, number, number) -> () | (number, number) -> () ,
		Rect : (number, number, number) -> (),
		SameLine : () -> (),
		Selectable : (string, boolean) -> boolean | (string) -> boolean,
		Separator : () -> (),
		SetCursorScreenPos : (number, number) -> (),
		SetKeyboardFocusHere : (number) -> (),
		SetNextWindowPos : (number, number) -> (),
		SetNextWindowPosCenter : () -> (),
		SetNextWindowSize : (number, number) -> (),
		SetStyleColor : (number, any) -> (),
		SliderFloat : (string, number, number, number) -> (boolean, number),
		Text : (string) -> (),
		Unindent : (number) -> (),

		Key_DownArrow : number,
		Key_Enter : number,
		Key_Escape : number,
		Key_UpArrow : number
	}

	export type Resource = {
		newEmpty: (Resource, string) -> Resource,
		getPath: (Resource) -> string,
		path : string,
	}

	declare Lumix : {
		Resource : Resource,
		Entity : Entity
	}

	export type World = {
		create : () -> World,
		destroy : (World) -> (),
		load : (World, string, any) -> (),
		instantiatePrefab : (World, Vec3, Resource) -> Entity,
		getActivePartition : (World) -> number,
		setActivePartition : (World, number) -> (),
		createPartition : (World, string) -> number,
		destroyPartition : (World, number) -> (),
		getAllEntities : (World) -> any,
		getModule : (World, string) -> any,
		createEntity : (World) -> Entity,
		createEntityEx : (World, any) -> Entity,
		findEntityByName : (World, Entity, string) -> Entity,
	)#");

	for (Module& m : g_data->modules) {
		L(m.id,": ",m.id,"_module,");
	}

	L("}" OUT_ENDL);

	for (Struct& s : g_data->structs) {
		L("type ",s.name, " = {");
		for (StructVar& v : s.vars) {
			L(v.name,": ",toLuaType(v.type), ",");
		}
		L("}" OUT_ENDL);
	}

	for (Object& o : g_data->objects) {
		L("type ",o.name, " = {");
		for (Function& f : o.functions) {
			serializeLuaType(out, o.name, "", f, false);
		}
		L("}" OUT_ENDL);
	}

	for (Module& m : g_data->modules) {
		L("type ",m.id,"_module = {");
		for (Function& f : m.functions) {
			serializeLuaType(out, m.id, "_module", f, false);
		}
		L("}" OUT_ENDL);
		
		for (Component& c : m.components) {
			L("type ",c.id,"_component =  {");
			for (Property& p : c.properties) {
				char tmp[256];
				StringView lua_name;
				if (p.is_var) {
					lua_name = p.name;
				}
				else {
					toID(pickLabel(p.name, p.attributes.label), Span(tmp, tmp + 256));
					lua_name = makeStringView(tmp);
				}
				if (!isBlob(p) && p.type.size() > 0) {
					L("\t", lua_name, ": ", toLuaType(p.type), ",");
				}
			}
			for (Function& f : c.functions) {
				serializeLuaType(out, c.id, "_component", f, true);
			}
			L("}" OUT_ENDL);
		}
	}

	out.add(R"#(
	export type Entity = {
		NULL : Entity,
		world : World,
		name : string,
		parent : Entity?,
		rotation : any,
		position : Vec3,
		local_position : Vec3,
		first_child : Entity?,
		next_sibling : Entity?,
		scale : Vec3,
		hasComponent : (Entity, any) -> boolean,
		getComponent : (Entity, any) -> any,
		destroy : (Entity) -> (),
		createComponent : (Entity, any) -> any,
	)#");

	for (Module& m : g_data->modules) {
		for (Component& c : m.components) {
			L(c.id,": ",c.id,"_component,");
		}
	}
	
	L("}" OUT_ENDL);

	out.add(R"#(
	declare this : Entity

	type ActionDesc = {
		name : string,
		label : string,
		run : () -> ()
	}

	declare Editor: {
		RESOURCE_PROPERTY : number,
		COLOR_PROPERTY : number,
		ENTITY_PROPERTY : number,
		BOOLEAN_PROPERTY : number,
		setPropertyType : (any, string, number, string?) -> (),
		setArrayPropertyType : (any, string, number, string?) -> (),
		getSelectedEntitiesCount : () -> number,
		getSelectedEntity : (number) -> Entity,
		addAction : (ActionDesc) -> (),
		createEntityEx : (any) -> Entity,
		scene_view : SceneView,
		asset_browser : AssetBrowser
	}

	declare LumixAPI: {
		hasFilesystemWork : () -> boolean,

		engine : any,
		logError : (string) -> (),
		logInfo : (string) -> (),
		loadResource : (any, path:string, restype:string) -> any,
		writeFile : (string, string) -> boolean,
		createPipeline : () -> Pipeline,
		destroyPipeline : (Pipeline) -> (),
	)#");

	// Emit enum typings into LumixAPI so editors see LumixAPI.<EnumName>.<Member>
	for (Enum& e : g_data->enums) {
		L("\t", e.name, " : {");
		for (Enumerator& en : e.values) {
			L("\t\t", en.name, " : number,");
		}
		L("\t},");
	}

	for (Module& m : g_data->modules) {
		for (Enum& e : m.enums) {
			L("\t", e.name, " : {");
			for (Enumerator& en : e.values) {
				L("\t\t", en.name, " : number,");
			}
			L("\t},");
		}
	}

	L("}" OUT_ENDL);

	out.add(R"#(
	type InputDevice = {
		type : "mouse" | "keyboard" | "gamepad",
		index : number
	}

	type AxisInputEvent = {
		type : "axis",
		device : InputDevice,
		x : number,
		y : number,
		x_abs : number,
		y_abs : number
	}

	type MouseWheelInputEvent = {
		type : "mouse_wheel",	
		x : number,
		y : number
	}

	type ButtonInputEvent = {
		type : "button",
		device : InputDevice,
		key_id : number,
		down : boolean,
		is_repeat : boolean,
		x : number,
		y : number
	}

	export type InputEvent = ButtonInputEvent | AxisInputEvent | MouseWheelInputEvent
	)#");

	// format output
	StringView raw(out.data, out.data + out.length);
	StringView line;
	i32 indent = 0;
	const char* tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	while (readLine(raw, line)) {
		i32 prev_indent = indent;
		StringView word = peekWord(line);
		if (equal(word, "declare")) {
			++indent;
		}
		else if (equal(word, "end")) {
			--indent;
		}
		else {
			for (const char* c = line.begin; c < line.end; ++c) {
				if (*c == '{') ++indent;
				else if (*c == '}') --indent;
			}
		}

		if (indent > 0) out_formatted.add(StringView(tabs, tabs + (indent < prev_indent ? indent : prev_indent)));
		out_formatted.add(line, OUT_ENDL);
	}
}


void serializeLuaMeta(MetaData& data) {
	g_data = &data;
	OutputStream capi;
	OutputStream definitions;
	capi.add("// Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	definitions.add("-- Generated by meta.cpp" OUT_ENDL OUT_ENDL);

	for (Object& object : data.objects) {
		StringView include_path = makeStringView(object.filename);
		if (startsWith(include_path, "plugins/")) capi.add("#include \"../", include_path, "\"" OUT_ENDL);
		else {
			if (startsWith(include_path, "src/")) include_path = withoutPrefix(include_path, 4);
			capi.add("#include \"", include_path, "\"" OUT_ENDL);
		}
	}
	for (Module& module : data.modules) {
		for (StringView include_path : module.includes) capi.add("#include \"", include_path, "\"" OUT_ENDL);
		StringView include_path = makeStringView(module.filename);
		if (startsWith(include_path, "plugins/")) capi.add("#include \"../", include_path, "\"" OUT_ENDL);
		else {
			if (startsWith(include_path, "src/")) include_path = withoutPrefix(include_path, 4);
			capi.add("#include \"", include_path, "\"" OUT_ENDL);
		}
	}
	capi.add("#define XXH_STATIC_LINKING_ONLY" OUT_ENDL);
	capi.add("#include \"xxhash/xxhash.h\"" OUT_ENDL OUT_ENDL);

	serializeLuaHelpers(capi);
	for (Module& module : data.modules) serializeLuaCAPI(capi, module);
	serializeLuaTypes(definitions);
	serializeMain(capi);
	writeFile("src/lua/lua_capi.gen.h", capi);
	writeFile("data/scripts/lumix.d.lua", definitions);
	g_data = nullptr;
}

} // namespace

META_PLUGIN(serializeLuaMeta)
