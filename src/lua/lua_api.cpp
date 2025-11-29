#include "core/delegate.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/string.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/prefab.h"
#include "engine/world.h"
#include "lua_script_system.h"
#include "lua_wrapper.h"
#include <lua.h>
#include <luacode.h>
#include <imgui/imgui.h>


namespace Lumix {

namespace LuaImGui {

int GetOsImePosRequest(lua_State* L) {
	ImVec2 p = ImGuiEx::GetOsImePosRequest();
	lua_pushnumber(L, p.x);
	lua_pushnumber(L, p.y);
	return 2;
}

int InputTextMultilineWithCallback(lua_State* L) {
	char buf[8 * 4096];
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	auto* value = LuaWrapper::checkArg<const char*>(L, 2);
	copyString(buf, value);
	auto callback = [](ImGuiInputTextCallbackData* data) -> int {
		lua_State* L = (lua_State*)data->UserData;
		lua_pushlstring(L, data->Buf, data->BufTextLen);
		lua_pushnumber(L, data->CursorPos);
		lua_pushboolean(L, data->EventFlag == ImGuiInputTextFlags_CallbackCompletion);
		LuaWrapper::pcall(L, 3, 1);
		if (lua_isstring(L, -1)) {
			const char* str = lua_tostring(L, -1);
			data->InsertChars(data->CursorPos, str);
		}
		lua_pop(L, 1);
		return 0;
	};

	bool changed = ImGui::InputTextMultiline(name, buf, sizeof(buf), ImVec2(-1, -1), ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackAlways, callback, L);
	lua_pushboolean(L, changed);
	if (changed) {
		lua_pushstring(L, buf);
		return 2;
	}
	return 1;
}

int InputTextMultiline(lua_State* L) {
	char buf[8 * 4096];
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	auto* value = LuaWrapper::checkArg<const char*>(L, 2);
	copyString(buf, value);
	bool changed = ImGui::InputTextMultiline(name, buf, sizeof(buf), ImVec2(-1, -1));
	lua_pushboolean(L, changed);
	if (changed) {
		lua_pushstring(L, buf);
		return 2;
	}
	return 1;
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


void Rect(float w, float h, u32 color) { ImGuiEx::Rect(w, h, color); }
void Dummy(float w, float h) { ImGui::Dummy({w, h}); }
bool IsItemHovered() { return ImGui::IsItemHovered(); }
bool IsMouseDown(int button) { return ImGui::IsMouseDown(button); }
bool IsMouseClicked(int button) { return ImGui::IsMouseClicked(button); }
bool IsKeyPressed(int key, bool repeat) { return ImGui::IsKeyPressed((ImGuiKey)key, repeat); }


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

void PlotLines(lua_State* L, const char* str_id) {
	LuaWrapper::checkTableArg(L, 2);
	Vec2 size = LuaWrapper::checkArg<Vec2>(L, 3);
	const i32 num_values = lua_objlen(L, 2);
	auto getter = [](void* data, i32 idx) -> float {
		lua_State* L = (lua_State*)data;
		int t = lua_rawgeti(L, 2, idx + 1);
		float res = FLT_MAX;
		if (t == LUA_TNUMBER) {
			res = (float)lua_tonumber(L, -1);
		}
		lua_pop(L, 1);
		return res;
	};
	ImGui::PlotLines(str_id, getter, L, num_values, 0, nullptr, FLT_MAX, FLT_MAX, size);
}

void OpenPopup(const char* str_id) { ImGui::OpenPopup(str_id); }

int Begin(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	ImGuiWindowFlags flags = 0;
	bool open = true;
	bool has_open = false;
	if (lua_gettop(L) > 1) {
		open = LuaWrapper::checkArg<bool>(L, 2);
		has_open = true;
	}
	if (lua_gettop(L) > 2) {
		flags = LuaWrapper::checkArg<int>(L, 3);
	}
	bool res = ImGui::Begin(label, has_open ? &open : nullptr, flags);
	lua_pushboolean(L, res);
	if (has_open) lua_pushboolean(L, open);
	return has_open ? 2 : 1;
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


static int LUA_writeFile(lua_State* L) {
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);
	if (!LuaWrapper::isType<const char*>(L, 2)) {
		LuaWrapper::argError<const char*>(L, 2);
	}
	size_t len;
	const char* content = lua_tolstring(L, 2, &len);
	FileSystem& fs = engine->getFileSystem();
	os::OutputFile file;
	if (!fs.open(path, file)) {
		lua_pushboolean(L, false);
		return 1;
	}

	bool res = file.write(content, len);
	file.close();
	lua_pushboolean(L, res);
	return 1;
}

static int LUA_pause(lua_State* L) {
	bool pause = LuaWrapper::checkArg<bool>(L, 1);
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	engine->pause(pause);
	return 0;
}


static int LUA_hasFilesystemWork(lua_State* L) {
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	bool res = engine->getFileSystem().hasWork();
	lua_pushboolean(L, res);
	return 1;
}


static int LUA_processFilesystemWork(lua_State* L) {
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
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

static void LUA_createComponent(lua_State* L, World* world, i32 entity, const char* type)
{
	if (!world) luaL_argerror(L, 1, "World can not be null");
	ComponentType cmp_type = reflection::getComponentType(type);
	IModule* module = world->getModule(cmp_type);
	if (!module) luaL_error(L, "unknown component type %s", type);
	if (world->hasComponent({entity}, cmp_type)) {
		luaL_error(L, "Component %s already exists in entity %d", type, entity);
	}

	world->createComponent(cmp_type, {entity});
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

static i32 LUA_unloadResource(lua_State* L) {
	i32 resource_idx = LuaWrapper::checkArg<i32>(L, 1);
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	auto* system = (LuaScriptSystem*)engine->getSystemManager().getSystem("lua_script");
	system->unloadLuaResource(resource_idx);
	return 0;
}

static int LUA_loadResource(lua_State* L) {
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);
	const char* type = LuaWrapper::checkArg<const char*>(L, 2);
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	auto* system = (LuaScriptSystem*)engine->getSystemManager().getSystem("lua_script");
	i32 res = system->addLuaResource(Path(path), ResourceType(type));
	LuaWrapper::push(L, res);
	return 1;
}

static int LUA_resourceTypeFromString(lua_State* L) {
	const char* type_str = LuaWrapper::checkArg<const char*>(L, 1);
	ResourceType type(type_str);
	lua_pushlightuserdata(L, (void*)type.type.getHashValue());
	return 1;
}

static const char* LUA_getResourcePath(lua_State* L, i32 handle) {
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	auto* system = (LuaScriptSystem*)engine->getSystemManager().getSystem("lua_script");
	Resource* res = system->getLuaResource(handle);
	return res ? res->getPath().c_str() : "";
}


static DVec3 LUA_getEntityPosition(World* world, i32 entity)
{
	return world->getPosition({entity});
}

static DVec3 LUA_getEntityLocalPosition(World* world, i32 entity)
{
	return world->getLocalTransform({entity}).pos;
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

static i32 LUA_getNextSibling(World* world, i32 entity)
{
	return world->getNextSibling({entity}).index;
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
static void LUA_setEntityLocalPosition(World* univ, i32 entity, const DVec3& pos) { univ->setLocalPosition({entity}, pos); }
static World* LUA_createWorld(Engine* engine) { return &engine->createWorld(); }
static void LUA_destroyWorld(Engine* engine, World* world) { engine->destroyWorld(*world); }

static void LUA_destroyEntity(World* world, i32 entity) { 
	// we defer destruction to avoid scripts destroying themselves while being used
	// or destroying other components which are using them (e.g. destroying button from callback in the button)
	auto* module = (LuaScriptModule*)world->getModule("lua_script");
	module->deferEntityDestruction({entity});
}

static void LUA_logError(const char* text) { logError(text); }
static void LUA_logInfo(const char* text) { logInfo(text); }
static void LUA_setTimeMultiplier(Engine* engine, float multiplier) { engine->setTimeMultiplier(multiplier); }

static void LUA_setActivePartition(World* world, u16 partition) {
	world->setActivePartition(World::PartitionHandle(partition));
}

static u16 LUA_createPartition(World* world, const char* name) {
	return (u16)world->createPartition(name);
}

static void LUA_destroyPartition(World* world, u16 partition) {
	// we defer destruction to avoid scripts destroying themselves while being used
	// or destroying other components which are using them
	auto* module = (LuaScriptModule*)world->getModule("lua_script");
	module->deferPartitionDestruction(partition);
}

static u16 LUA_getActivePartition(World* world) {
	return (u16)world->getActivePartition();
}

static int LUA_loadWorld(lua_State* L)
{
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	auto* world = LuaWrapper::checkArg<World*>(L, 1);
	auto* path = LuaWrapper::checkArg<const char*>(L, 2);
	if (!lua_isfunction(L, 3)) LuaWrapper::argError(L, 3, "function");
	struct Callback {
		~Callback() { LuaWrapper::releaseRef(L, lua_func); }

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
					LuaWrapper::pushRef(L, lua_func);
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
		LuaWrapper::RefHandle lua_func;
	};

	FileSystem& fs = engine->getFileSystem();
	Callback* inst = LUMIX_NEW(engine->getAllocator(), Callback);
	inst->engine = engine;
	inst->world = world;
	inst->path = Path(path);
	inst->L = L;
	lua_pushvalue(L, 3);
	inst->lua_func = LuaWrapper::createRef(L);
	lua_pop(L, 1);
	fs.getContent(inst->path, makeDelegate<&Callback::invoke>(inst));
	return 0;
}

static int LUA_loadstring(lua_State* L) {
	const char* src = LuaWrapper::checkArg<const char*>(L, 1);
	size_t bytecode_size;
	char* bytecode = luau_compile(src, stringLength(src), nullptr, &bytecode_size);
	if (bytecode_size == 0) {
		lua_pushnil(L);
		lua_pushstring(L, bytecode);
		free(bytecode);
		return 2;
	}
	int res = luau_load(L, "loadstring", bytecode, bytecode_size, 0);
	free(bytecode);
	if (res != 0) {
		lua_pushnil(L);
		lua_insert(L, -2);
		return 2;
	}
	return 1;
}

static int LUA_instantiatePrefab(lua_State* L) {
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	LuaWrapper::checkTableArg(L, 1);
	if (LuaWrapper::getField(L, 1, "value") != LUA_TLIGHTUSERDATA) {
		LuaWrapper::argError(L, 1, "world");
	}
	auto* world = LuaWrapper::toType<World*>(L, -1);
	lua_pop(L, 1);
	DVec3 position = LuaWrapper::checkArg<DVec3>(L, 2);
	int prefab_id = LuaWrapper::checkArg<int>(L, 3);
	LuaScriptSystem* system = (LuaScriptSystem*)engine->getSystemManager().getSystem("lua_script");
	PrefabResource* prefab = static_cast<PrefabResource*>(system->getLuaResource(prefab_id));
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

void registerLuaAPI(lua_State* L);

void registerEngineAPI(lua_State* L, Engine* engine)
{
	lua_pushlightuserdata(L, engine);

	lua_pushcfunction(L, &LUA_loadstring, "loadstring");
	lua_setglobal(L, "loadstring");

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
	REGISTER_FUNCTION(destroyPartition);
	REGISTER_FUNCTION(getEntityName);
	REGISTER_FUNCTION(getEntityLocalPosition);
	REGISTER_FUNCTION(getEntityPosition);
	REGISTER_FUNCTION(getEntityRotation);
	REGISTER_FUNCTION(getEntityScale);
	REGISTER_FUNCTION(getFirstChild);
	REGISTER_FUNCTION(getNextSibling);
	REGISTER_FUNCTION(getParent);
	REGISTER_FUNCTION(setParent);
	REGISTER_FUNCTION(getModule);
	REGISTER_FUNCTION(getResourcePath);
	REGISTER_FUNCTION(logError);
	REGISTER_FUNCTION(logInfo);
	REGISTER_FUNCTION(setEntityName);
	REGISTER_FUNCTION(setEntityLocalPosition);
	REGISTER_FUNCTION(setEntityPosition);
	REGISTER_FUNCTION(setEntityRotation);
	REGISTER_FUNCTION(setEntityScale);
	REGISTER_FUNCTION(setTimeMultiplier);
	REGISTER_FUNCTION(startGame);
	
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "getResourcePath", &LuaWrapper::wrap<LUA_getResourcePath>);

	LuaWrapper::createSystemFunction(L, "LumixAPI", "resourceTypeFromString", &LUA_resourceTypeFromString);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "beginProfilerBlock", LuaWrapper::wrap<&profiler::endBlock>);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "endProfilerBlock", LuaWrapper::wrap<&profiler::beginBlock>);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "createProfilerCounter", LuaWrapper::wrap<&profiler::createCounter>);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "pushProfilerCounter", LuaWrapper::wrap<&profiler::pushCounter>);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "networkRead", &LUA_networkRead);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "packU32", &LUA_packU32);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "unpackU32", &LUA_unpackU32);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "networkConnect", &LUA_networkConnect);
	LuaWrapper::createSystemFunction(L, "LumixAPI", "networkListen", &LUA_networkListen);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "loadWorld", LUA_loadWorld);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "hasFilesystemWork", LUA_hasFilesystemWork);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "processFilesystemWork", LUA_processFilesystemWork);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "pause", LUA_pause);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "writeFile", LUA_writeFile);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "loadResource", LUA_loadResource);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "unloadResource", LUA_unloadResource);
	
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
	LuaWrapper::createSystemVariable(L, "ImGui", "Key_DownArrow", ImGuiKey_DownArrow);
	LuaWrapper::createSystemVariable(L, "ImGui", "Key_Enter", ImGuiKey_Enter);
	LuaWrapper::createSystemVariable(L, "ImGui", "Key_Escape", ImGuiKey_Escape);
	LuaWrapper::createSystemVariable(L, "ImGui", "Key_UpArrow", ImGuiKey_UpArrow);
	LuaImGui::registerCFunction(L, "AlignTextToFramePadding", &LuaWrapper::wrap<ImGui::AlignTextToFramePadding>);
	LuaImGui::registerCFunction(L, "Begin", &LuaImGui::Begin);
	LuaImGui::registerCFunction(L, "BeginChildFrame", &LuaImGui::BeginChildFrame);
	LuaImGui::registerCFunction(L, "BeginMenu", &LuaWrapper::wrap<ImGui::BeginMenu>);
	LuaImGui::registerCFunction(L, "BeginPopup", LuaImGui::BeginPopup);
	LuaImGui::registerCFunction(L, "Button", &LuaImGui::Button);
	LuaImGui::registerCFunction(L, "CalcTextSize", &LuaImGui::CalcTextSize);
	LuaImGui::registerCFunction(L, "Checkbox", &LuaImGui::Checkbox);
	LuaImGui::registerCFunction(L, "CloseCurrentPopup", &LuaWrapper::wrap<ImGui::CloseCurrentPopup>);
	LuaImGui::registerCFunction(L, "CollapsingHeader", &LuaImGui::CollapsingHeader);
	LuaImGui::registerCFunction(L, "Columns", &LuaWrapper::wrap<&ImGui::Columns>);
	LuaImGui::registerCFunction(L, "DragFloat", &LuaImGui::DragFloat);
	LuaImGui::registerCFunction(L, "DragInt", &LuaImGui::DragInt);
	LuaImGui::registerCFunction(L, "Dummy", &LuaWrapper::wrap<&LuaImGui::Dummy>);
	LuaImGui::registerCFunction(L, "End", &LuaWrapper::wrap<&ImGui::End>);
	LuaImGui::registerCFunction(L, "EndChildFrame", &LuaWrapper::wrap<&ImGui::EndChildFrame>);
	LuaImGui::registerCFunction(L, "EndCombo", &LuaWrapper::wrap<&ImGui::EndCombo>);
	LuaImGui::registerCFunction(L, "EndMenu", &LuaWrapper::wrap<&ImGui::EndMenu>);
	LuaImGui::registerCFunction(L, "EndPopup", &LuaWrapper::wrap<&ImGui::EndPopup>);
	LuaImGui::registerCFunction(L, "GetColumnWidth", &LuaWrapper::wrap<&ImGui::GetColumnWidth>);
	LuaImGui::registerCFunction(L, "GetDisplayWidth", &LuaWrapper::wrap<LuaImGui::GetDisplayWidth>);
	LuaImGui::registerCFunction(L, "GetDisplayHeight", &LuaWrapper::wrap<LuaImGui::GetDisplayHeight>);
	LuaImGui::registerCFunction(L, "GetWindowWidth", &LuaWrapper::wrap<ImGui::GetWindowWidth>);
	LuaImGui::registerCFunction(L, "GetWindowHeight", &LuaWrapper::wrap<ImGui::GetWindowHeight>);
	LuaImGui::registerCFunction(L, "GetWindowPos", &LuaWrapper::wrap<LuaImGui::GetWindowPos>);
	LuaImGui::registerCFunction(L, "Indent", &LuaWrapper::wrap<&ImGui::Indent>);
	LuaImGui::registerCFunction(L, "GetOsImePosRequest", &LuaImGui::GetOsImePosRequest);
	LuaImGui::registerCFunction(L, "InputTextMultilineWithCallback", &LuaImGui::InputTextMultilineWithCallback);
	LuaImGui::registerCFunction(L, "InputTextMultiline", &LuaImGui::InputTextMultiline);
	LuaImGui::registerCFunction(L, "IsItemHovered", &LuaWrapper::wrap<&LuaImGui::IsItemHovered>);
	LuaImGui::registerCFunction(L, "IsKeyPressed", &LuaWrapper::wrap<&LuaImGui::IsKeyPressed>);
	LuaImGui::registerCFunction(L, "IsMouseClicked", &LuaWrapper::wrap<&LuaImGui::IsMouseClicked>);
	LuaImGui::registerCFunction(L, "IsMouseDown", &LuaWrapper::wrap<&LuaImGui::IsMouseDown>);
	LuaImGui::registerCFunction(L, "NewLine", &LuaWrapper::wrap<&ImGui::NewLine>);
	LuaImGui::registerCFunction(L, "NextColumn", &LuaWrapper::wrap<&ImGui::NextColumn>);
	LuaImGui::registerCFunction(L, "OpenPopup", &LuaWrapper::wrap<LuaImGui::OpenPopup>);
	LuaImGui::registerCFunction(L, "PlotLines", &LuaWrapper::wrap<&LuaImGui::PlotLines>);
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
	LuaImGui::registerCFunction(L, "SetKeyboardFocusHere", &LuaWrapper::wrap<&ImGui::SetKeyboardFocusHere>);
	LuaImGui::registerCFunction(L, "SetNextWindowPos", &LuaImGui::SetNextWindowPos);
	LuaImGui::registerCFunction(L, "SetNextWindowPosCenter", &LuaImGui::SetNextWindowPosCenter);
	LuaImGui::registerCFunction(L, "SetNextWindowSize", &LuaWrapper::wrap<&LuaImGui::SetNextWindowSize>);
	LuaImGui::registerCFunction(L, "SetStyleColor", &LuaWrapper::wrap<LuaImGui::SetStyleColor>);
	LuaImGui::registerCFunction(L, "SliderFloat", &LuaImGui::SliderFloat);
	LuaImGui::registerCFunction(L, "Text", &LuaImGui::Text);
	LuaImGui::registerCFunction(L, "Unindent", &LuaWrapper::wrap<&ImGui::Unindent>);
	LuaImGui::registerCFunction(L, "LabelText", &LuaWrapper::wrap<LuaImGui::LabelText>);

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
			if Lumix[cmp] == nil then
				error("Lua does not know component " .. cmp)
			end
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
			elseif key == "local_position" then
				return LumixAPI.getEntityLocalPosition(table._world, table._entity)
			elseif key == "parent" then
				local p = LumixAPI.getParent(table._world, table._entity)
				if p < 0 then return nil end
				return Lumix.Entity:new(table._world, p)
			elseif key == "first_child" then
				local p = LumixAPI.getFirstChild(table._world, table._entity)
				if p < 0 then return nil end
				return Lumix.Entity:new(table._world, p)
			elseif key == "next_sibling" then
				local p = LumixAPI.getNextSibling(table._world, table._entity)
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
			elseif key == "_world" or key == "_entity" then
				return rawget(table, key)
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
		Lumix.Entity.INVALID = Lumix.Entity:new(nil, -1)
		Lumix.Entity.NULL = Lumix.Entity.INVALID

		Lumix.Entity.__eq = function(a, b)
			return a._entity == b._entity and a._world == b._world
		end

		Lumix.Entity.__newindex = function(table, key, value)
			if key == "position" then
				LumixAPI.setEntityPosition(table._world, table._entity, value)
			elseif key == "local_position" then
				LumixAPI.setEntityLocalPosition(table._world, table._entity, value)
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
		function Lumix.World:destroyPartition(partition)
			return LumixAPI.destroyPartition(self.value, partition)
		end
		function Lumix.World:instantiatePrefab(position, prefab)
			return LumixAPI.instantiatePrefab(self, position, prefab._handle)
		end
		function Lumix.World:createEntity()
			local e = LumixAPI.createEntity(self.value)
			return Lumix.Entity:new(self.value, e)
		end
		function Lumix.World.__index(table, key)
			if Lumix.World[key] ~= nil then
				return Lumix.World[key]
			else
				if LumixModules[key] == nil then return nil end
				local module = LumixAPI.getModule(table.value, key)
				return LumixModules[key]:new(module)
			end
		end
		function Lumix.World:getModule(name)
			local module = LumixAPI.getModule(self.value, name)	
			if LumixModules[name] == nil then return nil end
			return LumixModules[name]:new(module)
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
					ent.position = v
				elseif k == "rotation" then
					ent.position = v
				elseif k == "scale" then
					ent.scale = v
				else
					local c = ent:createComponent(k)
					for k2, v2 in pairs(v) do
						c[k2] = v2
					end
				end
			end
			return ent
		end

		Lumix.Resource = {}
		function Lumix.Resource:new(handle, type)
			local r = { _handle = handle, _type = LumixAPI.resourceTypeFromString(type) }
			setmetatable(r, self)
			return r
		end
		function Lumix.Resource:newEmpty(type)
			local r = { _handle = -1, _type = LumixAPI.resourceTypeFromString(type) }
			setmetatable(r, self)
			return r
		end
		function Lumix.Resource:getPath()
			return LumixAPI.getResourcePath(self._handle)
		end
		function Lumix.Resource.__index(table, key)
			if Lumix.Resource[key] ~= nil then
				return Lumix.Resource[key]
			end
			if key == "path" then
				return table:getPath()
			end
			return nil
		end
	)#";

	#define TO_STR_HELPER(x) #x
	#define TO_STR(x) TO_STR_HELPER(x)
	if (!LuaWrapper::execute(L, entity_src, __FILE__ "(" TO_STR(__LINE__) ")", 0)) {
		logError("Failed to init entity api");
	}

	lua_getglobal(L, "LumixAPI");
	if (lua_type(L, -1) == LUA_TNIL) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, "LumixAPI");
	}
	else {
		lua_pop(L, 1);
	}

	registerLuaAPI(L);
}

static struct {
	IModule* module;
	EntityRef entity;
}
checkComponent(lua_State* L) {
	LuaWrapper::checkTableArg(L, 1); // self
	if (LuaWrapper::getField(L, 1, "_module") != LUA_TLIGHTUSERDATA) {
		ASSERT(false);
		luaL_error(L, "Internal error");
	}
	auto* module = LuaWrapper::toType<IModule*>(L, -1);
	lua_pop(L, 1);
			
	if (LuaWrapper::getField(L, 1, "_entity") != LUA_TNUMBER) {
		ASSERT(false);
		luaL_error(L, "Internal error");
	}
	EntityRef entity = {LuaWrapper::toType<int>(L, -1)};
	lua_pop(L, 1);
	return {module, entity};
}

static int lua_push_script_env(lua_State* L, EntityRef entity, LuaScriptModule* module){
	const i32 scr_index = LuaWrapper::toType<i32>(L, 2) - 1;
	int env = module->getEnvironment(entity, scr_index);
	if (env < 0) {
		lua_pushnil(L);
	}
	else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
		ASSERT(lua_type(L, -1) == LUA_TTABLE);
	}
	return 1;
}

static int lua_new_cmp(lua_State* L) {
	LuaWrapper::DebugGuard guard(L, 1);
	LuaWrapper::checkTableArg(L, 1); // self
	const World* world = LuaWrapper::checkArg<World*>(L, 2);
	const EntityRef e = {LuaWrapper::checkArg<i32>(L, 3)};
		
	LuaWrapper::getField(L, 1, "cmp_type");
	const int cmp_type = LuaWrapper::toType<int>(L, -1);
	lua_pop(L, 1);
	IModule* module = world->getModule(ComponentType{cmp_type});

	lua_newtable(L);
	LuaWrapper::setField(L, -1, "_entity", e);
	LuaWrapper::setField(L, -1, "_module", module);
	lua_pushvalue(L, 1);
	lua_setmetatable(L, -2);
	return 1;
}

static void registerLuaComponent(lua_State* L, const char* cmp_name, lua_CFunction getter, lua_CFunction setter) {
	const ComponentType cmp_type = reflection::getComponentType(cmp_name);
	lua_newtable(L);
	lua_getglobal(L, "Lumix");
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, cmp_name);
	lua_pop(L, 1);

	lua_pushcfunction(L, lua_new_cmp, "new");
	lua_setfield(L, -2, "new");

	LuaWrapper::setField(L, -1, "cmp_type", cmp_type.index);

	LuaWrapper::push(L, cmp_type);
	lua_pushcclosure(L, getter, "getter", 1);
	lua_setfield(L, -2, "__index");
		
	LuaWrapper::push(L, cmp_type);
	lua_pushcclosure(L, setter, "setter", 1);
	lua_setfield(L, -2, "__newindex");

	lua_pop(L, 1);
}

static int lua_new_module(lua_State* L) {
	LuaWrapper::DebugGuard guard(L, 1);
	LuaWrapper::checkTableArg(L, 1); // self
	IModule* module = LuaWrapper::checkArg<IModule*>(L, 2);
		
	lua_newtable(L);
	LuaWrapper::setField(L, -1, "_module", module);
	lua_pushvalue(L, 1);
	lua_setmetatable(L, -2);
	return 1;
}

} // namespace Lumix

#include "lua_capi.gen.h"