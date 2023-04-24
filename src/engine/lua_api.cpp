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
#include <lua.hpp>

namespace Lumix {

namespace LuaImGui {

int InputTextMultiline(lua_State* L)
{
	char buf[4096];
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


int PushStyleColor(lua_State* L)
{
	int var = LuaWrapper::checkArg<int>(L, 1);
	ImVec4 v;
	v.x = LuaWrapper::checkArg<float>(L, 2);
	v.y = LuaWrapper::checkArg<float>(L, 3);
	v.z = LuaWrapper::checkArg<float>(L, 4);
	v.w = LuaWrapper::checkArg<float>(L, 5);
	ImGui::PushStyleColor(var, v);
	return 0;
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


int PushID(lua_State* L)
{
	int id = LuaWrapper::checkArg<int>(L, 1);
	ImGui::PushID(id);
	return 0;
}


int SetStyleColor(lua_State* L)
{
	auto& style = ImGui::GetStyle();
	int index = LuaWrapper::checkArg<int>(L, 1);
	ImVec4 color;
	color.x = LuaWrapper::checkArg<float>(L, 2);
	color.y = LuaWrapper::checkArg<float>(L, 3);
	color.z = LuaWrapper::checkArg<float>(L, 4);
	color.w = LuaWrapper::checkArg<float>(L, 5);
	style.Colors[index] = color;
	return 0;
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
	ImGui::Text("%s", text);
	return 0;
}


int LabelText(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	auto* text = LuaWrapper::checkArg<const char*>(L, 2);
	ImGui::LabelText(label, "%s", text);
	return 0;
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


int GetWindowPos(lua_State* L)
{
	ImVec2 pos = ImGui::GetWindowPos();
	LuaWrapper::push(L, Vec2(pos.x, pos.y));
	return 1;
}


int SetNextWindowPos(lua_State* L)
{
	ImVec2 pos;
	pos.x = LuaWrapper::checkArg<float>(L, 1);
	pos.y = LuaWrapper::checkArg<float>(L, 2);
	ImGui::SetNextWindowPos(pos);
	return 0;
}


int AlignTextToFramePadding(lua_State* L)
{
	ImGui::AlignTextToFramePadding();
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


int Separator(lua_State* L)
{
	ImGui::Separator();
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


int OpenPopup(lua_State* L) {
	auto* str_id = LuaWrapper::checkArg<const char*>(L, 1);
	ImGui::OpenPopup(str_id);
	return 0;
}

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


int GetDisplayWidth(lua_State* L)
{
	float w = ImGui::GetIO().DisplaySize.x;
	LuaWrapper::push(L, w);
	return 1;
}


int GetDisplayHeight(lua_State* L)
{
	float w = ImGui::GetIO().DisplaySize.y;
	LuaWrapper::push(L, w);
	return 1;
}


int GetWindowWidth(lua_State* L)
{
	float w = ImGui::GetWindowWidth();
	LuaWrapper::push(L, w);
	return 1;
}


int GetWindowHeight(lua_State* L)
{
	float w = ImGui::GetWindowHeight();
	LuaWrapper::push(L, w);
	return 1;
}


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
	lua_pushcfunction(L, f);
	lua_setfield(L, -2, name);
}

} // namespace LuaImGui


static int LUA_packageLoader(lua_State* L)
{
	const char* module = LuaWrapper::toType<const char*>(L, 1);
	StaticString<LUMIX_MAX_PATH> tmp(module);
	lua_getglobal(L, "LumixAPI");
	lua_getfield(L, -1, "engine");
	lua_remove(L, -2);
	auto* engine = (Engine*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	auto& fs = engine->getFileSystem();
	OutputMemoryStream buf(engine->getAllocator());
	bool loaded = true;
	if (!fs.getContentSync(Path(tmp), buf)) {
		tmp.add(".lua");
		if (!fs.getContentSync(Path(tmp), buf)) {
			loaded = false;
			logError("Failed to open file ", tmp);
			StaticString<LUMIX_MAX_PATH + 40> msg("Failed to open file ");
			msg.add(tmp);
			lua_pushstring(L, msg);
		}
	}
	if (loaded && luaL_loadbuffer(L, (const char*)buf.data(), buf.size(), tmp) != 0) {
		logError("Failed to load package ", tmp, ": ", lua_tostring(L, -1));
	}
	return 1;
}


static void installLuaPackageLoader(lua_State* L)
{
	lua_getglobal(L, "package");
	if (lua_type(L, -1) != LUA_TTABLE) {
		logError("Lua \"package\" is not a table");
		return;
	}
	lua_getfield(L, -1, "searchers");
	if (lua_type(L, -1) != LUA_TTABLE) {
		lua_pop(L, 1);
		lua_getfield(L, -1, "loaders");
		if (lua_type(L, -1) != LUA_TTABLE) {
			logError("Lua \"package.searchers\"/\"package.loaders\" is not a table");
			return;
		}
	}
	int numLoaders = 0;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		lua_pop(L, 1);
		numLoaders++;
	}

	lua_pushinteger(L, numLoaders + 1);
	lua_pushcfunction(L, LUA_packageLoader);
	lua_rawset(L, -3);
	lua_pop(L, 2);
}

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


static bool LUA_createComponent(World* world, i32 entity, const char* type)
{
	if (!world) return false;
	ComponentType cmp_type = reflection::getComponentType(type);
	IScene* scene = world->getScene(cmp_type);
	if (!scene) return false;
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
	IScene* scene = world->getScene(cmp_type);
	if (!scene) return false;
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


static IScene* LUA_getScene(World* world, const char* name)
{
	return world->getScene(name);
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

static int LUA_loadWorld(lua_State* L)
{
	Engine* engine = getEngineUpvalue(L);
	auto* world = LuaWrapper::checkArg<World*>(L, 1);
	auto* name = LuaWrapper::checkArg<const char*>(L, 2);
	if (!lua_isfunction(L, 3)) LuaWrapper::argError(L, 3, "function");
	struct Callback {
		~Callback() { luaL_unref(L, LUA_REGISTRYINDEX, lua_func); }

		void invoke(u64 size, const u8* mem, bool success) {
			if (!success) {
				logError("Failed to open world ", path);
			} else {
				InputMemoryStream blob(mem, size);
				#pragma pack(1)
					struct Header {
						u32 magic;
						int version;
						u32 hash;
						u32 engine_hash;
					};
				#pragma pack()
				Header header;
				blob.read(&header, sizeof(header));

				EntityMap entity_map(engine->getAllocator());
				if (!world->deserialize(blob, entity_map)) {
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
	inst->lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
	fs.getContent(inst->path, makeDelegate<&Callback::invoke>(inst));
	return 0;
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
	LuaWrapper::createSystemVariable(L, "LumixAPI", "engine", engine);

	#define REGISTER_FUNCTION(name) \
		LuaWrapper::createSystemFunction(L, "LumixAPI", #name, \
			&LuaWrapper::wrap<LUA_##name>); \

	REGISTER_FUNCTION(createComponent);
	REGISTER_FUNCTION(hasComponent);
	REGISTER_FUNCTION(createEntity);
	REGISTER_FUNCTION(createWorld);
	REGISTER_FUNCTION(destroyEntity);
	REGISTER_FUNCTION(destroyWorld);
	REGISTER_FUNCTION(findByName);
	REGISTER_FUNCTION(getEntityName);
	REGISTER_FUNCTION(getEntityPosition);
	REGISTER_FUNCTION(getEntityRotation);
	REGISTER_FUNCTION(getEntityScale);
	REGISTER_FUNCTION(getFirstChild);
	REGISTER_FUNCTION(getParent);
	REGISTER_FUNCTION(setParent);
	REGISTER_FUNCTION(getScene);
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

	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "loadWorld", LUA_loadWorld);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "hasFilesystemWork", LUA_hasFilesystemWork);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "processFilesystemWork", LUA_processFilesystemWork);
	LuaWrapper::createSystemClosure(L, "LumixAPI", engine, "pause", LUA_pause);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(F) \
		do { \
			auto f = &LuaWrapper::wrapMethod<&World::F>; \
			LuaWrapper::createSystemFunction(L, "LumixAPI", #F, f); \
		} while(false)

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
	LuaImGui::registerCFunction(L, "AlignTextToFramePadding", &LuaImGui::AlignTextToFramePadding);
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
	LuaImGui::registerCFunction(L, "GetDisplayWidth", &LuaImGui::GetDisplayWidth);
	LuaImGui::registerCFunction(L, "GetDisplayHeight", &LuaImGui::GetDisplayHeight);
	LuaImGui::registerCFunction(L, "GetWindowWidth", &LuaImGui::GetWindowWidth);
	LuaImGui::registerCFunction(L, "GetWindowHeight", &LuaImGui::GetWindowHeight);
	LuaImGui::registerCFunction(L, "GetWindowPos", &LuaImGui::GetWindowPos);
	LuaImGui::registerCFunction(L, "Indent", &LuaWrapper::wrap<&ImGui::Indent>);
	LuaImGui::registerCFunction(L, "InputTextMultiline", &LuaImGui::InputTextMultiline);
	LuaImGui::registerCFunction(L, "IsItemHovered", &LuaWrapper::wrap<&LuaImGui::IsItemHovered>);
	LuaImGui::registerCFunction(L, "IsMouseClicked", &LuaWrapper::wrap<&LuaImGui::IsMouseClicked>);
	LuaImGui::registerCFunction(L, "IsMouseDown", &LuaWrapper::wrap<&LuaImGui::IsMouseDown>);
	LuaImGui::registerCFunction(L, "NewLine", &LuaWrapper::wrap<&ImGui::NewLine>);
	LuaImGui::registerCFunction(L, "NextColumn", &LuaWrapper::wrap<&ImGui::NextColumn>);
	LuaImGui::registerCFunction(L, "OpenPopup", &LuaImGui::OpenPopup);
	LuaImGui::registerCFunction(L, "PopItemWidth", &LuaWrapper::wrap<&ImGui::PopItemWidth>);
	LuaImGui::registerCFunction(L, "PopID", &LuaWrapper::wrap<&ImGui::PopID>);
	LuaImGui::registerCFunction(L, "PopStyleColor", &LuaWrapper::wrap<&ImGui::PopStyleColor>);
	LuaImGui::registerCFunction(L, "PopStyleVar", &LuaWrapper::wrap<&ImGui::PopStyleVar>);
	LuaImGui::registerCFunction(L, "PushItemWidth", &LuaWrapper::wrap<&ImGui::PushItemWidth>);
	LuaImGui::registerCFunction(L, "PushID", &LuaImGui::PushID);
	LuaImGui::registerCFunction(L, "PushStyleColor", &LuaImGui::PushStyleColor);
	LuaImGui::registerCFunction(L, "PushStyleVar", &LuaImGui::PushStyleVar);
	LuaImGui::registerCFunction(L, "Rect", &LuaWrapper::wrap<&LuaImGui::Rect>);
	LuaImGui::registerCFunction(L, "SameLine", &LuaImGui::SameLine);
	LuaImGui::registerCFunction(L, "Selectable", &LuaImGui::Selectable);
	LuaImGui::registerCFunction(L, "Separator", &LuaImGui::Separator);
	LuaImGui::registerCFunction(L, "SetCursorScreenPos", &LuaImGui::SetCursorScreenPos);
	LuaImGui::registerCFunction(L, "SetNextWindowPos", &LuaImGui::SetNextWindowPos);
	LuaImGui::registerCFunction(L, "SetNextWindowPosCenter", &LuaImGui::SetNextWindowPosCenter);
	LuaImGui::registerCFunction(L, "SetNextWindowSize", &LuaWrapper::wrap<&LuaImGui::SetNextWindowSize>);
	LuaImGui::registerCFunction(L, "SetStyleColor", &LuaImGui::SetStyleColor);
	LuaImGui::registerCFunction(L, "SliderFloat", &LuaImGui::SliderFloat);
	LuaImGui::registerCFunction(L, "Text", &LuaImGui::Text);
	LuaImGui::registerCFunction(L, "Unindent", &LuaWrapper::wrap<&ImGui::Unindent>);
	LuaImGui::registerCFunction(L, "LabelText", &LuaImGui::LabelText);

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
		function Lumix.World:createEntity()
			local e = LumixAPI.createEntity(self.value)
			return Lumix.Entity:new(self.value, e)
		end
		function Lumix.World:getScene(scene_name)
			local scene = LumixAPI.getScene(self.value, scene_name)	
			return Lumix[scene_name]:new(scene)
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
	if (!LuaWrapper::execute(L, Span(entity_src, stringLength(entity_src)), __FILE__ "(" TO_STR(__LINE__) ")", 0)) {
		logError("Failed to init entity api");
	}

	installLuaPackageLoader(L);
}


} // namespace Lumix
