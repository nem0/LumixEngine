#include <imgui/imgui.h>
#include "delegate.h"
#include "engine.h"
#include "file_system.h"
#include "input_system.h"
#include "log.h"
#include "lua_wrapper.h"
#include "plugin.h"
#include "prefab.h"
#include "reflection.h"
#include "string.h"
#include "world.h"
#include <lua.h>
#include <luacode.h>

namespace Lumix {

namespace LuaImGui {

int InputTextMultiline(lua_State* L)
{
	char buf[4 * 4096];
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	auto* value = LuaWrapper::checkArg<const char*>(L, 2);
	copyString(buf, value);
	bool changed = ImGui::InputTextMultiline(name, buf, sizeof(buf), ImVec2(-1, -1));
	lua_pushboolean(L, changed);
	if (changed)
	{
		lua_pushstring(L, buf);
	}
	else
	{
		lua_pushvalue(L, 2);
	}
	return 2;
}


int DragFloat(lua_State* L)
{
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	float value = LuaWrapper::checkArg<float>(L, 2);
	bool changed = ImGui::DragFloat(name, &value);
	lua_pushboolean(L, changed);
	lua_pushnumber(L, value);
	return 2;
}


int DragInt(lua_State* L)
{
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	int value = LuaWrapper::checkArg<int>(L, 2);
	bool changed = ImGui::DragInt(name, &value);
	lua_pushboolean(L, changed);
	lua_pushinteger(L, value);
	return 2;
}


void PushStyleColor(i32 var, const Vec4& color) {
	ImVec4 v;
	v.x = color.x;
	v.y = color.y;
	v.z = color.z;
	v.w = color.w;
	ImGui::PushStyleColor(var, v);
}


int PushStyleVar(lua_State* L)
{
	int var = LuaWrapper::checkArg<int>(L, 1);
	if (lua_gettop(L) > 2)
	{
		ImVec2 v;
		v.x = LuaWrapper::checkArg<float>(L, 2);
		v.y = LuaWrapper::checkArg<float>(L, 3);
		ImGui::PushStyleVar(var, v);
	}
	else
	{
		float v = LuaWrapper::checkArg<float>(L, 2);
		ImGui::PushStyleVar(var, v);
	}
	return 0;
}


void PushID(i32 id) { ImGui::PushID(id); }


void SetStyleColor(int color_index, const Vec4& color) {
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4 v;
	v.x = color.x;
	v.y = color.y;
	v.z = color.z;
	v.w = color.w;
	style.Colors[color_index] = v;
}


int SliderFloat(lua_State* L)
{
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	float value = LuaWrapper::checkArg<float>(L, 2);
	float min = LuaWrapper::checkArg<float>(L, 3);
	float max = LuaWrapper::checkArg<float>(L, 4);
	bool changed = ImGui::SliderFloat(name, &value, min, max, "");
	lua_pushboolean(L, changed);
	lua_pushnumber(L, value);
	return 2;
}


int Text(lua_State* L)
{
	auto* text = LuaWrapper::checkArg<const char*>(L, 1);
	ImGui::TextUnformatted(text);
	return 0;
}


void LabelText(const char* label, const char* text) {
	ImGui::LabelText(label, "%s", text);
}


int Button(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	ImVec2 size(0, 0);
	if (lua_gettop(L) > 2)
	{
		size.x = LuaWrapper::checkArg<float>(L, 2);
		size.y = LuaWrapper::checkArg<float>(L, 3);
	}
	bool clicked = ImGui::Button(label, size);
	lua_pushboolean(L, clicked);
	return 1;
}


int CollapsingHeader(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	lua_pushboolean(L, ImGui::CollapsingHeader(label));
	return 1;
}


int CalcTextSize(lua_State* L)
{
	auto* text = LuaWrapper::checkArg<const char*>(L, 1);
	ImVec2 size = ImGui::CalcTextSize(text);

	LuaWrapper::push(L, size.x);
	LuaWrapper::push(L, size.y);
	return 2;
}


int Checkbox(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	bool b = LuaWrapper::checkArg<bool>(L, 2);
	bool clicked = ImGui::Checkbox(label, &b);
	lua_pushboolean(L, clicked);
	lua_pushboolean(L, b);
	return 2;
}


Vec2 GetWindowPos() {
	ImVec2 pos = ImGui::GetWindowPos();
	return Vec2(pos.x, pos.y);
}


int SetNextWindowPos(lua_State* L)
{
	ImVec2 pos;
	pos.x = LuaWrapper::checkArg<float>(L, 1);
	pos.y = LuaWrapper::checkArg<float>(L, 2);
	ImGui::SetNextWindowPos(pos);
	return 0;
}


int Selectable(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	bool selected = false;
	if (lua_gettop(L) > 1)
	{
		selected = LuaWrapper::checkArg<bool>(L, 2);
	}
	bool clicked = ImGui::Selectable(label, selected);
	lua_pushboolean(L, clicked);
	return 1;
}


int SetCursorScreenPos(lua_State* L)
{
	ImVec2 pos;
	pos.x = LuaWrapper::checkArg<float>(L, 1);
	pos.y = LuaWrapper::checkArg<float>(L, 2);
	ImGui::SetCursorScreenPos(pos);
	return 0;
}


void Rect(float w, float h, u32 color)
{
	ImGuiEx::Rect(w, h, color);
}


void Dummy(float w, float h)
{
	ImGui::Dummy({w, h});
}


bool IsItemHovered()
{
	return ImGui::IsItemHovered();
}


bool IsMouseDown(int button)
{
	return ImGui::IsMouseDown(button);
}


bool IsMouseClicked(int button)
{
	return ImGui::IsMouseClicked(button);
}


int SetNextWindowPosCenter(lua_State* L)
{
	ImVec2 size = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
	return 0;
}


int SetNextWindowSize(float w, float h)
{
	ImGui::SetNextWindowSize(ImVec2(w, h));
	return 0;
}


void OpenPopup(const char* str_id) { ImGui::OpenPopup(str_id); }

int Begin(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	ImGuiWindowFlags flags = 0;
	if (lua_gettop(L) > 1)
	{
		flags = LuaWrapper::checkArg<int>(L, 2);
	}
	bool res = ImGui::Begin(label, nullptr, flags);
	lua_pushboolean(L, res);
	return 1;
}


int BeginChildFrame(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	ImVec2 size(0, 0);
	if (lua_gettop(L) > 1)
	{
		size.x = LuaWrapper::checkArg<float>(L, 2);
		size.y = LuaWrapper::checkArg<float>(L, 3);
	}
	bool res = ImGui::BeginChildFrame(ImGui::GetID(label), size);
	lua_pushboolean(L, res);
	return 1;
}


int BeginPopup(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	bool res = ImGui::BeginPopup(label);
	lua_pushboolean(L, res);
	return 1;
}

float GetDisplayWidth() { return ImGui::GetIO().DisplaySize.x; }
float GetDisplayHeight() { return ImGui::GetIO().DisplaySize.y; }


int SameLine(lua_State* L)
{
	float pos_x = 0;
	if (lua_gettop(L) > 0)
	{
		pos_x = LuaWrapper::checkArg<float>(L, 1);
	}
	ImGui::SameLine(pos_x);
	return 0;
}


void registerCFunction(lua_State* L, const char* name, lua_CFunction f)
{
	lua_pushcfunction(L, f, name);
	lua_setfield(L, -2, name);
}

} // namespace LuaImGui


static Engine* getEngineUpvalue(lua_State* L) {
	const int index = lua_upvalueindex(1);
	if (!LuaWrapper::isType<Engine>(L, index)) {
		logError("Invalid Lua closure");
		ASSERT(false);
		return 0;
	}
	return LuaWrapper::checkArg<Engine*>(L, index);
}

static int LUA_pause(lua_State* L) 
{
	bool pause = LuaWrapper::checkArg<bool>(L, 1);
	Engine* engine = getEngineUpvalue(L);
	engine->pause(pause);
	return 0;
}


static int LUA_hasFilesystemWork(lua_State* L)
{
	Engine* engine = getEngineUpvalue(L);
	bool res = engine->getFileSystem().hasWork();
	lua_pushboolean(L, res);
	return 1;
}


static int LUA_processFilesystemWork(lua_State* L)
{
	Engine* engine = getEngineUpvalue(L);
	engine->getFileSystem().processCallbacks();
	return 0;
}


static void LUA_startGame(Engine* engine, World* world)
{
	if(engine && world) engine->startGame(*world);
}

static void LUA_networkClose(os::NetworkStream* stream) {
	return os::close(*stream);
}

static int LUA_networkListen(lua_State* L) {
	const char* ip = LuaWrapper::checkArg<const char*>(L, 1);
	u16 port = LuaWrapper::checkArg<u16>(L, 2);
	os::NetworkStream* stream = os::listen(ip, port, getGlobalAllocator());
	if (!stream) return 0;
	lua_pushlightuserdata(L, stream);
	return 1;
}

static int LUA_networkConnect(lua_State* L) {
	const char* ip = LuaWrapper::checkArg<const char*>(L, 1);
	u16 port = LuaWrapper::checkArg<u16>(L, 2);
	os::NetworkStream* stream = os::connect(ip, port, getGlobalAllocator());
	if (!stream) return 0;
	lua_pushlightuserdata(L, stream);
	return 1;
}

static bool LUA_networkWrite(os::NetworkStream* stream, const char* data, u32 size) {
	return os::write(*stream, data, size);
}

static reflection::ComponentBase* LUA_getComponent(u32 index) {
	return reflection::getComponents()[index].cmp;
}

static const char* LUA_getComponentName(reflection::ComponentBase* cmp) {
	return cmp->name;
}

static i32 LUA_getNumProperties(reflection::ComponentBase* cmp) {
	return cmp->props.size();
}

static i32 LUA_getNumFunctions(reflection::ComponentBase* cmp) {
	return cmp->functions.size();
}

static i32 LUA_getPropertyType(reflection::PropertyBase* property) {
	enum class PropertyType {
		FLOAT,
		I32,
		U32,
		ENTITY,
		VEC2,
		VEC3,
		IVEC3,
		VEC4,
		PATH,
		BOOL,
		STRING,
		ARRAY,
		BLOB,
		DYNAMIC
	};
	
	struct : reflection::IPropertyVisitor {
		void visit(const reflection::Property<float>& prop) override { type = PropertyType::FLOAT; }
		void visit(const reflection::Property<int>& prop) override { type = PropertyType::I32; }
		void visit(const reflection::Property<u32>& prop) override { type = PropertyType::U32; }
		void visit(const reflection::Property<EntityPtr>& prop) override { type = PropertyType::ENTITY; }
		void visit(const reflection::Property<Vec2>& prop) override { type = PropertyType::VEC2; }
		void visit(const reflection::Property<Vec3>& prop) override { type = PropertyType::VEC3; }
		void visit(const reflection::Property<IVec3>& prop) override { type = PropertyType::IVEC3; }
		void visit(const reflection::Property<Vec4>& prop) override { type = PropertyType::VEC4; }
		void visit(const reflection::Property<Path>& prop) override { type = PropertyType::PATH; }
		void visit(const reflection::Property<bool>& prop) override { type = PropertyType::BOOL; }
		void visit(const reflection::Property<const char*>& prop) override { type = PropertyType::STRING; }
		void visit(const struct reflection::ArrayProperty& prop) override { type = PropertyType::ARRAY; }
		void visit(const struct reflection::BlobProperty& prop) override { type = PropertyType::BLOB; }
		void visit(const reflection::DynamicProperties& prop) override { type = PropertyType::DYNAMIC; }
		PropertyType type;
	} visitor;
	property->visit(visitor);
	return (i32)visitor.type;
}

static i32 LUA_getNumComponents() {
	return reflection::getComponents().length();
}

static const char* LUA_getComponentLabel(reflection::ComponentBase* cmp) {
	return cmp->label;
}

static const char* LUA_getComponentIcon(reflection::ComponentBase* cmp) {
	return cmp->icon;
}

static reflection::PropertyBase* LUA_getProperty(reflection::ComponentBase* cmp, u32 index) {
	return cmp->props[index];
}

static reflection::FunctionBase* LUA_getFunction(reflection::ComponentBase* cmp, u32 index) {
	return cmp->functions[index];
}

static const char* LUA_getFunctionName(reflection::FunctionBase* fnc) {
	return fnc->name;
}

static u32 LUA_getFunctionArgCount(reflection::FunctionBase* fnc) {
	return fnc->getArgCount();
}

static i32 LUA_getFunctionReturnType(lua_State* L) {
	auto* fnc = LuaWrapper::checkArg<reflection::FunctionBase*>(L, 1);
	StringView name = fnc->getReturnTypeName();
	lua_pushlstring(L, name.begin, name.size());
	return 1;
}

static const char* LUA_getFunctionArgType(reflection::FunctionBase* fnc, u32 arg_idx) {
	const reflection::TypeDescriptor type = fnc->getArgType(arg_idx);
	switch (type.type) {
		case reflection::Variant::VOID: return "void";
		case reflection::Variant::PTR: return "void*";
		case reflection::Variant::BOOL: return "bool";
		case reflection::Variant::I32: return "i32";
		case reflection::Variant::U32: return "u32";
		case reflection::Variant::FLOAT: return "float";
		case reflection::Variant::CSTR: return "const char*";
		case reflection::Variant::ENTITY: return "EntityPtr";
		case reflection::Variant::VEC2: return "Vec2";
		case reflection::Variant::VEC3: return "Vec3";
		case reflection::Variant::DVEC3: return "DVec3";
		case reflection::Variant::COLOR: return "Color";
	}
	ASSERT(false);
	return "";
}

static const char* LUA_getPropertyName(reflection::PropertyBase* prop) {
	return prop->name;
}

static int LUA_networkRead(lua_State* L) {
	char tmp[4096];
	os::NetworkStream* stream = LuaWrapper::checkArg<os::NetworkStream*>(L, 1);
	u32 size = LuaWrapper::checkArg<u32>(L, 2);
	if (size > sizeof(tmp)) luaL_error(L, "size too big, max %d allowed", (int)sizeof(tmp));
	if (!os::read(*stream, tmp, size)) return 0;
	lua_pushlstring(L, tmp, size);
	return 1;
}

static int LUA_packU32(lua_State* L) {
	u32 val = LuaWrapper::checkArg<u32>(L, 1);
	lua_pushlstring(L, (const char*)&val, sizeof(val));
	return 1;
}

static int LUA_unpackU32(lua_State* L) {
	size_t size;
	const char* lstr = lua_tolstring(L, 1, &size);
	u32 val;
	if (sizeof(val) != size) luaL_error(L, "Invalid argument");
	
	memcpy(&val, lstr, sizeof(val));
	lua_pushnumber(L, val);
	return 1;
}

static bool LUA_createComponent(World* world, i32 entity, const char* type)
{
	if (!world) return false;
	ComponentType cmp_type = reflection::getComponentType(type);
	IModule* module = world->getModule(cmp_type);
	if (!module) return false;
	if (world->hasComponent({entity}, cmp_type))
	{
		logError("Component ", type, " already exists in entity ", entity);
		return false;
	}

	world->createComponent(cmp_type, {entity});
	return true;
}


static bool LUA_hasComponent(World* world, i32 entity, const char* type)
{
	if (!world) return false;
	ComponentType cmp_type = reflection::getComponentType(type);
	return world->hasComponent({entity}, cmp_type);
}


static EntityRef LUA_createEntity(World* world)
{
	return world->createEntity({0, 0, 0}, Quat::IDENTITY);
}


static int LUA_setEntityRotation(lua_State* L)
{
	World* univ = LuaWrapper::checkArg<World*>(L, 1);
	int entity_index = LuaWrapper::checkArg<int>(L, 2);
	if (entity_index < 0) return 0;

	if (lua_gettop(L) > 3)
	{
		Vec3 axis = LuaWrapper::checkArg<Vec3>(L, 3);
		float angle = LuaWrapper::checkArg<float>(L, 4);
		univ->setRotation({ entity_index }, Quat(axis, angle));
	}
	else
	{
		Quat rot = LuaWrapper::checkArg<Quat>(L, 3);
		univ->setRotation({ entity_index }, rot);
	}
	return 0;
}


static IModule* LUA_getModule(World* world, const char* name)
{
	return world->getModule(name);
}


static int LUA_loadResource(Engine* engine, const char* path, const char* type)
{
	return engine->addLuaResource(Path(path), ResourceType(type));
}


static const char* LUA_getResourcePath(Engine* engine, i32 resource_handle)
{
	Resource* res = engine->getLuaResource(resource_handle);
	return res->getPath().c_str();
}


static DVec3 LUA_getEntityPosition(World* world, i32 entity)
{
	return world->getPosition({entity});
}


static Quat LUA_getEntityRotation(World* world, i32 entity)
{
	return world->getRotation({entity});
}


static Vec3 LUA_getEntityScale(World* world, i32 entity)
{
	return world->getScale({entity});
}


static i32 LUA_getFirstChild(World* world, i32 entity)
{
	return world->getFirstChild({entity}).index;
}

static i32 LUA_getParent(World* world, i32 entity)
{
	return world->getParent({entity}).index;
}

static i32 LUA_findByName(World* world, i32 entity, const char* name)
{
	return world->findByName(EntityPtr{entity}, name).index;
}

static void LUA_setParent(World* world, i32 parent, i32 child)
{
	return world->setParent(EntityPtr{parent}, EntityRef{child});
}


static const char* LUA_getEntityName(World* univ, i32 entity) { return univ->getEntityName({entity}); }
static void LUA_setEntityName(World* univ, i32 entity, const char* name) { univ->setEntityName({entity}, name); }
static void LUA_setEntityScale(World* univ, i32 entity, const Vec3& scale) { univ->setScale({entity}, scale); }
static void LUA_setEntityPosition(World* univ, i32 entity, const DVec3& pos) { univ->setPosition({entity}, pos); }
static void LUA_unloadResource(Engine* engine, int resource_idx) { engine->unloadLuaResource(resource_idx); }
static World* LUA_createWorld(Engine* engine) { return &engine->createWorld(false); }
static void LUA_destroyWorld(Engine* engine, World* world) { engine->destroyWorld(*world); }
static void LUA_destroyEntity(World* world, i32 entity) { world->destroyEntity({entity}); }
static void LUA_logError(const char* text) { logError(text); }
static void LUA_logInfo(const char* text) { logInfo(text); }
static void LUA_setTimeMultiplier(Engine* engine, float multiplier) { engine->setTimeMultiplier(multiplier); }

static void LUA_setActivePartition(World* world, u16 partition) {
	world->setActivePartition(World::PartitionHandle(partition));
}

static u16 LUA_createPartition(World* world, const char* name) {
	return (u16)world->createPartition(name);
}

static u16 LUA_getActivePartition(World* world) {
	return (u16)world->getActivePartition();
}

static int LUA_loadWorld(lua_State* L)
{
	Engine* engine = getEngineUpvalue(L);
	auto* world = LuaWrapper::checkArg<World*>(L, 1);
	auto* name = LuaWrapper::checkArg<const char*>(L, 2);
	if (!lua_isfunction(L, 3)) LuaWrapper::argError(L, 3, "function");
	struct Callback {
		~Callback() { LuaWrapper::luaL_unref(L, LUA_REGISTRYINDEX, lua_func); }

		void invoke(Span<const u8> mem, bool success) {
			if (!success) {
				logError("Failed to open world ", path);
			} else {
				InputMemoryStream blob(mem);
				EntityMap entity_map(engine->getAllocator());
				WorldVersion editor_version;
				if (!world->deserialize(blob, entity_map, editor_version)) {
					logError("Failed to deserialize world ", path);
				} else {
					lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func);
					if (lua_type(L, -1) != LUA_TFUNCTION) {
						ASSERT(false);
					}

					if (lua_pcall(L, 0, 0, 0) != 0) {
						logError(lua_tostring(L, -1));
						lua_pop(L, 1);
					}
				}
			}
			LUMIX_DELETE(engine->getAllocator(), this);
		}

		Engine* engine;
		World* world;
		Path path;
		lua_State* L;
		int lua_func;
	};

	FileSystem& fs = engine->getFileSystem();
	Callback* inst = LUMIX_NEW(engine->getAllocator(), Callback);
	inst->engine = engine;
	inst->world = world;
	const Path path("universes/", name, ".unv");
	inst->path = path;
	inst->L = L;
	inst->lua_func = LuaWrapper::luaL_ref(L, LUA_REGISTRYINDEX);
	fs.getContent(inst->path, makeDelegate<&Callback::invoke>(inst));
	return 0;
}

static int finishrequire(lua_State* L)
{
    if (lua_isstring(L, -1))
        lua_error(L);

    return 1;
}

static int LUA_require(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    luaL_findtable(L, LUA_REGISTRYINDEX, "_MODULES", 1);

    // return the module from the cache
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1))
    {
        // L stack: _MODULES result
        return finishrequire(L);
    }

    lua_pop(L, 1);

	Engine* engine = LuaWrapper::toType<Engine*>(L, lua_upvalueindex(1));
	StaticString<MAX_PATH> path(name, ".lua");
	OutputMemoryStream blob(engine->getAllocator());
	if (!engine->getFileSystem().getContentSync(Path(path), blob)) {
		luaL_argerrorL(L, 1, "error loading module");
	}

    // module needs to run in a new thread, isolated from the rest
    // note: we create ML on main thread so that it doesn't inherit environment of L
    lua_State* GL = lua_mainthread(L);
    lua_State* ML = lua_newthread(GL);
    lua_xmove(GL, L, 1);

    // new thread needs to have the globals sandboxed
    luaL_sandboxthread(ML);

    // now we can compile & run module on the new thread
	size_t bytecode_size;
	char* bytecode = luau_compile((const char*)blob.data(), blob.size(), nullptr, &bytecode_size);
    if (luau_load(ML, name, bytecode, bytecode_size, 0) == 0)
    {
        int status = lua_resume(ML, L, 0);

        if (status == 0)
        {
            if (lua_gettop(ML) == 0)
                lua_pushstring(ML, "module must return a value");
            else if (!lua_istable(ML, -1) && !lua_isfunction(ML, -1))
                lua_pushstring(ML, "module must return a table or function");
        }
        else if (status == LUA_YIELD)
        {
            lua_pushstring(ML, "module can not yield");
        }
        else if (!lua_isstring(ML, -1))
        {
            lua_pushstring(ML, "unknown error while running module");
        }
    }
	free(bytecode);

    // there's now a return value on top of ML; L stack: _MODULES ML
    lua_xmove(ML, L, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -4, name);

    // L stack: _MODULES ML result
    return finishrequire(L);
}

static int LUA_instantiatePrefab(lua_State* L) {
	Engine* engine = getEngineUpvalue(L);
	LuaWrapper::checkTableArg(L, 1);
	if (LuaWrapper::getField(L, 1, "value") != LUA_TLIGHTUSERDATA) {
		LuaWrapper::argError(L, 1, "world");
	}
	auto* world = LuaWrapper::toType<World*>(L, -1);
	lua_pop(L, 1);
	DVec3 position = LuaWrapper::checkArg<DVec3>(L, 2);
	int prefab_id = LuaWrapper::checkArg<int>(L, 3);
	PrefabResource* prefab = static_cast<PrefabResource*>(engine->getLuaResource(prefab_id));
	if (!prefab) {
		luaL_argerror(L, 3, "Unknown prefab.");
	}
	if (!prefab->isReady()) {
		luaL_error(L, "Prefab '%s' is not ready, preload it.", prefab->getPath().c_str());
	}
	EntityMap entity_map(engine->getAllocator());
	if (engine->instantiatePrefab(*world, *prefab, position, {0, 0, 0, 1}, {1, 1, 1}, entity_map)) {
		LuaWrapper::pushEntity(L, entity_map.m_map[0], world);
		return 1;
	}
	luaL_error(L, "Failed to instantiate prefab");
	return 0;
}


void registerEngineAPI(lua_State* L, Engine* engine)
{
	lua_pushlightuserdata(L, engine);
	lua_pushcclosure(L, &LUA_require, "require", 1);
	lua_setglobal(L, "require");

	LuaWrapper::createSystemVariable(L, "LumixAPI", "engine", engine);

	#define REGISTER_FUNCTION(name) \
		LuaWrapper::createSystemFunction(L, "LumixAPI", #name, \
			&LuaWrapper::wrap<LUA_##name>); \

	REGISTER_FUNCTION(networkClose);
	REGISTER_FUNCTION(networkWrite);
	REGISTER_FUNCTION(createComponent);
	REGISTER_FUNCTION(hasComponent);
	REGISTER_FUNCTION(createEntity);
	REGISTER_FUNCTION(createWorld);
	REGISTER_FUNCTION(destroyEntity);
	REGISTER_FUNCTION(destroyWorld);
	REGISTER_FUNCTION(findByName);
	REGISTER_FUNCTION(getActivePartition);
	REGISTER_FUNCTION(setActivePartition);
	REGISTER_FUNCTION(createPartition);
	REGISTER_FUNCTION(getEntityName);
	REGISTER_FUNCTION(getEntityPosition);
	REGISTER_FUNCTION(getEntityRotation);
	REGISTER_FUNCTION(getEntityScale);
	REGISTER_FUNCTION(getFirstChild);
	REGISTER_FUNCTION(getParent);
	REGISTER_FUNCTION(setParent);
	REGISTER_FUNCTION(getModule);
	REGISTER_FUNCTION(loadResource);
	REGISTER_FUNCTION(getResourcePath);
	REGISTER_FUNCTION(logError);
	REGISTER_FUNCTION(logInfo);
	REGISTER_FUNCTION(setEntityName);
	REGISTER_FUNCTION(setEntityPosition);
	REGISTER_FUNCTION(setEntityRotation);
	REGISTER_FUNCTION(setEntityScale);
	REGISTER_FUNCTION(setTimeMultiplier);
	REGISTER_FUNCTION(startGame);
	REGISTER_FUNCTION(unloadResource);

	LuaWrapper::createSystemFunction(L, "LumixReflection", "getComponent", &LuaWrapper::wrap<LUA_getComponent>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getNumComponents", &LuaWrapper::wrap<LUA_getNumComponents>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getNumProperties", &LuaWrapper::wrap<LUA_getNumProperties>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getNumFunctions", &LuaWrapper::wrap<LUA_getNumFunctions>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getComponentName", &LuaWrapper::wrap<LUA_getComponentName>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getComponentLabel", &LuaWrapper::wrap<LUA_getComponentLabel>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getComponentIcon", &LuaWrapper::wrap<LUA_getComponentIcon>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getProperty", &LuaWrapper::wrap<LUA_getProperty>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getFunction", &LuaWrapper::wrap<LUA_getFunction>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getFunctionName", &LuaWrapper::wrap<LUA_getFunctionName>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getFunctionArgCount", &LuaWrapper::wrap<LUA_getFunctionArgCount>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getFunctionReturnType", &LUA_getFunctionReturnType);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getFunctionArgType", &LuaWrapper::wrap<LUA_getFunctionArgType>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getPropertyType", &LuaWrapper::wrap<LUA_getPropertyType>);
	LuaWrapper::createSystemFunction(L, "LumixReflection", "getPropertyName", &LuaWrapper::wrap<LUA_getPropertyName>);
	
	LuaWrapper::createSystemFunction(L, "LumixAPI", "networkRead", &LUA_networkRead);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "packU32", &LUA_packU32);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "unpackU32", &LUA_unpackU32);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "networkConnect", &LUA_networkConnect);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "networkListen", &LUA_networkListen);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "loadWorld", LUA_loadWorld);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "hasFilesystemWork", LUA_hasFilesystemWork);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "processFilesystemWork", LUA_processFilesystemWork);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "pause", LUA_pause);

	#undef REGISTER_FUNCTION

	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "instantiatePrefab", &LUA_instantiatePrefab);

	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setglobal(L, "ImGui");

	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_NoMove", ImGuiWindowFlags_NoMove);
	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_NoCollapse", ImGuiWindowFlags_NoCollapse);
	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_NoInputs", ImGuiWindowFlags_NoInputs);
	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_NoResize", ImGuiWindowFlags_NoResize);
	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_NoTitleBar", ImGuiWindowFlags_NoTitleBar);
	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_NoScrollbar", ImGuiWindowFlags_NoScrollbar);
	LuaWrapper::createSystemVariable(L, "ImGui", "WindowFlags_AlwaysAutoResize", ImGuiWindowFlags_AlwaysAutoResize);
	LuaWrapper::createSystemVariable(L, "ImGui", "Col_FrameBg", ImGuiCol_FrameBg);
	LuaWrapper::createSystemVariable(L, "ImGui", "Col_WindowBg", ImGuiCol_WindowBg);
	LuaWrapper::createSystemVariable(L, "ImGui", "Col_Button", ImGuiCol_Button);
	LuaWrapper::createSystemVariable(L, "ImGui", "Col_ButtonActive", ImGuiCol_ButtonActive);
	LuaWrapper::createSystemVariable(L, "ImGui", "Col_ButtonHovered", ImGuiCol_ButtonHovered);
	LuaWrapper::createSystemVariable(L, "ImGui", "StyleVar_FramePadding", ImGuiStyleVar_FramePadding);
	LuaWrapper::createSystemVariable(L, "ImGui", "StyleVar_IndentSpacing", ImGuiStyleVar_IndentSpacing);
	LuaWrapper::createSystemVariable(L, "ImGui", "StyleVar_ItemSpacing", ImGuiStyleVar_ItemSpacing);
	LuaWrapper::createSystemVariable(L, "ImGui", "StyleVar_ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing);
	LuaWrapper::createSystemVariable(L, "ImGui", "StyleVar_WindowPadding", ImGuiStyleVar_WindowPadding);
	LuaImGui::registerCFunction(L, "AlignTextToFramePadding", &LuaWrapper::wrap<ImGui::AlignTextToFramePadding>);
	LuaImGui::registerCFunction(L, "Begin", &LuaImGui::Begin);
	LuaImGui::registerCFunction(L, "BeginChildFrame", &LuaImGui::BeginChildFrame);
	LuaImGui::registerCFunction(L, "BeginPopup", LuaImGui::BeginPopup);
	LuaImGui::registerCFunction(L, "Button", &LuaImGui::Button);
	LuaImGui::registerCFunction(L, "CalcTextSize", &LuaImGui::CalcTextSize);
	LuaImGui::registerCFunction(L, "Checkbox", &LuaImGui::Checkbox);
	LuaImGui::registerCFunction(L, "CollapsingHeader", &LuaImGui::CollapsingHeader);
	LuaImGui::registerCFunction(L, "Columns", &LuaWrapper::wrap<&ImGui::Columns>);
	LuaImGui::registerCFunction(L, "DragFloat", &LuaImGui::DragFloat);
	LuaImGui::registerCFunction(L, "DragInt", &LuaImGui::DragInt);
	LuaImGui::registerCFunction(L, "Dummy", &LuaWrapper::wrap<&LuaImGui::Dummy>);
	LuaImGui::registerCFunction(L, "End", &LuaWrapper::wrap<&ImGui::End>);
	LuaImGui::registerCFunction(L, "EndChildFrame", &LuaWrapper::wrap<&ImGui::EndChildFrame>);
	LuaImGui::registerCFunction(L, "EndCombo", &LuaWrapper::wrap<&ImGui::EndCombo>);
	LuaImGui::registerCFunction(L, "EndPopup", &LuaWrapper::wrap<&ImGui::EndPopup>);
	LuaImGui::registerCFunction(L, "GetColumnWidth", &LuaWrapper::wrap<&ImGui::GetColumnWidth>);
	LuaImGui::registerCFunction(L, "GetDisplayWidth", &LuaWrapper::wrap<LuaImGui::GetDisplayWidth>);
	LuaImGui::registerCFunction(L, "GetDisplayHeight", &LuaWrapper::wrap<LuaImGui::GetDisplayHeight>);
	LuaImGui::registerCFunction(L, "GetWindowWidth", &LuaWrapper::wrap<ImGui::GetWindowWidth>);
	LuaImGui::registerCFunction(L, "GetWindowHeight", &LuaWrapper::wrap<ImGui::GetWindowHeight>);
	LuaImGui::registerCFunction(L, "GetWindowPos", &LuaWrapper::wrap<LuaImGui::GetWindowPos>);
	LuaImGui::registerCFunction(L, "Indent", &LuaWrapper::wrap<&ImGui::Indent>);
	LuaImGui::registerCFunction(L, "InputTextMultiline", &LuaImGui::InputTextMultiline);
	LuaImGui::registerCFunction(L, "IsItemHovered", &LuaWrapper::wrap<&LuaImGui::IsItemHovered>);
	LuaImGui::registerCFunction(L, "IsMouseClicked", &LuaWrapper::wrap<&LuaImGui::IsMouseClicked>);
	LuaImGui::registerCFunction(L, "IsMouseDown", &LuaWrapper::wrap<&LuaImGui::IsMouseDown>);
	LuaImGui::registerCFunction(L, "NewLine", &LuaWrapper::wrap<&ImGui::NewLine>);
	LuaImGui::registerCFunction(L, "NextColumn", &LuaWrapper::wrap<&ImGui::NextColumn>);
	LuaImGui::registerCFunction(L, "OpenPopup", &LuaWrapper::wrap<LuaImGui::OpenPopup>);
	LuaImGui::registerCFunction(L, "PopItemWidth", &LuaWrapper::wrap<&ImGui::PopItemWidth>);
	LuaImGui::registerCFunction(L, "PopID", &LuaWrapper::wrap<&ImGui::PopID>);
	LuaImGui::registerCFunction(L, "PopStyleColor", &LuaWrapper::wrap<&ImGui::PopStyleColor>);
	LuaImGui::registerCFunction(L, "PopStyleVar", &LuaWrapper::wrap<&ImGui::PopStyleVar>);
	LuaImGui::registerCFunction(L, "PushItemWidth", &LuaWrapper::wrap<&ImGui::PushItemWidth>);
	LuaImGui::registerCFunction(L, "PushID", &LuaWrapper::wrap<LuaImGui::PushID>);
	LuaImGui::registerCFunction(L, "PushStyleColor", &LuaWrapper::wrap<LuaImGui::PushStyleColor>);
	LuaImGui::registerCFunction(L, "PushStyleVar", &LuaImGui::PushStyleVar);
	LuaImGui::registerCFunction(L, "Rect", &LuaWrapper::wrap<&LuaImGui::Rect>);
	LuaImGui::registerCFunction(L, "SameLine", &LuaImGui::SameLine);
	LuaImGui::registerCFunction(L, "Selectable", &LuaImGui::Selectable);
	LuaImGui::registerCFunction(L, "Separator", &LuaWrapper::wrap<ImGui::Separator>);
	LuaImGui::registerCFunction(L, "SetCursorScreenPos", &LuaImGui::SetCursorScreenPos);
	LuaImGui::registerCFunction(L, "SetNextWindowPos", &LuaImGui::SetNextWindowPos);
	LuaImGui::registerCFunction(L, "SetNextWindowPosCenter", &LuaImGui::SetNextWindowPosCenter);
	LuaImGui::registerCFunction(L, "SetNextWindowSize", &LuaWrapper::wrap<&LuaImGui::SetNextWindowSize>);
	LuaImGui::registerCFunction(L, "SetStyleColor", &LuaWrapper::wrap<LuaImGui::SetStyleColor>);
	LuaImGui::registerCFunction(L, "SliderFloat", &LuaImGui::SliderFloat);
	LuaImGui::registerCFunction(L, "Text", &LuaImGui::Text);
	LuaImGui::registerCFunction(L, "Unindent", &LuaWrapper::wrap<&ImGui::Unindent>);
	LuaImGui::registerCFunction(L, "LabelText", &LuaWrapper::wrap<LuaImGui::LabelText>);

	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_DEVICE_KEYBOARD", InputSystem::Device::KEYBOARD);
	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_DEVICE_MOUSE", InputSystem::Device::MOUSE);
	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_DEVICE_CONTROLLER", InputSystem::Device::CONTROLLER);

	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_EVENT_BUTTON", InputSystem::Event::BUTTON);
	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_EVENT_AXIS", InputSystem::Event::AXIS);
	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_EVENT_TEXT_INPUT", InputSystem::Event::TEXT_INPUT);
	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_EVENT_DEVICE_ADDED", InputSystem::Event::DEVICE_ADDED);
	LuaWrapper::createSystemVariable(L, "LumixAPI", "INPUT_EVENT_DEVICE_REMOVED", InputSystem::Event::DEVICE_REMOVED);

	lua_pop(L, 1);

	const char* entity_src = R"#(
		Lumix = {}
		Lumix.Entity = {}
		function Lumix.Entity:new(world, entity)
			local e = { _entity = entity, _world = world }
			setmetatable(e, self)
			return e
		end
		function Lumix.Entity:destroy()
			LumixAPI.destroyEntity(self._world, self._entity)
			self._entity = 0xffFFffFF
		end
		function Lumix.Entity:createComponent(cmp)
			LumixAPI.createComponent(self._world, self._entity, cmp)
			return Lumix[cmp]:new(self._world, self._entity)
		end
		function Lumix.Entity:getComponent(cmp)
			if not LumixAPI.hasComponent(self._world, self._entity, cmp) then return nil end
			return Lumix[cmp]:new(self._world, self._entity)
		end
		function Lumix.Entity:hasComponent(cmp)
			return LumixAPI.hasComponent(self._world, self._entity, cmp)
		end
		Lumix.Entity.__index = function(table, key)
			if key == "position" then
				return LumixAPI.getEntityPosition(table._world, table._entity)
			elseif key == "parent" then
				local p = LumixAPI.getParent(table._world, table._entity)
				if p < 0 then return nil end
				return Lumix.Entity:new(table._world, p)
			elseif key == "first_child" then
				local p = LumixAPI.getFirstChild(table._world, table._entity)
				if p < 0 then return nil end
				return Lumix.Entity:new(table._world, p)
			elseif key == "rotation" then
				return LumixAPI.getEntityRotation(table._world, table._entity)
			elseif key == "name" then
				return LumixAPI.getEntityName(table._world, table._entity)
			elseif key == "scale" then
				return LumixAPI.getEntityScale(table._world, table._entity)
			elseif key == "world" then
				return Lumix.World:new(table._world)
			elseif Lumix.Entity[key] ~= nil then
				return Lumix.Entity[key]
			else 
				if LumixAPI.hasComponent(table._world, table._entity, key) then
					return Lumix[key]:new(table._world, table._entity)
				else
					return nil
				end
			end
		end
		Lumix.Entity.__newindex = function(table, key, value)
			if key == "position" then
				LumixAPI.setEntityPosition(table._world, table._entity, value)
			elseif key == "name" then
				LumixAPI.setEntityName(table._world, table._entity, value)
			elseif key == "rotation" then
				LumixAPI.setEntityRotation(table._world, table._entity, value)
			elseif key == "scale" then
				LumixAPI.setEntityScale(table._world, table._entity, value)
			elseif key == "parent" then
				LumixAPI.setParent(table._world, value._entity, table._entity)
			elseif Lumix.Entity[key] ~= nil then
				Lumix.Entity[key] = value
			else
				error("key " .. tostring(key) .. " not found")
			end
		end

		Lumix.World = {}
		function Lumix.World:create() 
			local u = LumixAPI.createWorld(LumixAPI.engine)
			return Lumix.World:new(u)
		end
		function Lumix.World:destroy()
			LumixAPI.destroyWorld(LumixAPI.engine, self.value)
		end
		function Lumix.World:load(path, callback_fn)
			LumixAPI.loadWorld(self.value, path, callback_fn)
		end
		function Lumix.World:new(_world)
			local u = { value = _world }
			setmetatable(u, self)
			self.__index = self
			return u
		end
		function Lumix.World:setActivePartition(partition)
			LumixAPI.setActivePartition(self.value, partition)
		end
		function Lumix.World:getActivePartition()
			return LumixAPI.getActivePartition(self.value)
		end
		function Lumix.World:createPartition(name)
			return LumixAPI.createPartition(self.value, name)
		end
		function Lumix.World:createEntity()
			local e = LumixAPI.createEntity(self.value)
			return Lumix.Entity:new(self.value, e)
		end
		function Lumix.World:getModule(name)
			local module = LumixAPI.getModule(self.value, name)	
			return Lumix[name]:new(module)
		end
		function Lumix.World:findEntityByName(parent, name)
			local p = LumixAPI.findByName(self.value, parent._entity or -1, name)
			if p < 0 then return nil end
			return Lumix.Entity:new(self.value, p)			
		end
		function Lumix.World:createEntityEx(desc)
			local ent = self:createEntity()
			for k, v in pairs(desc) do
				if k == "position" then
					e.position = v
				elseif k == "rotation" then
					e.position = v
				else
					local c = ent:createComponent(k)
					for k2, v2 in pairs(v) do
						c[k2] = v2
					end
				end
			end
			return ent
		end
	)#";

	#define TO_STR_HELPER(x) #x
	#define TO_STR(x) TO_STR_HELPER(x)
	if (!LuaWrapper::execute(L, entity_src, __FILE__ "(" TO_STR(__LINE__) ")", 0)) {
		logError("Failed to init entity api");
	}
}


} // namespace Lumix
