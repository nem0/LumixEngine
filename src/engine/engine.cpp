#include "engine/engine.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/input_system.h"
#include "engine/iplugin.h"
#include "engine/job_system.h"
#include "engine/lifo_allocator.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/page_allocator.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/timer.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include <imgui/imgui.h>


namespace Lumix
{

namespace LuaImGui
{


int InputTextMultiline(lua_State* L)
{
	char buf[4096];
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	auto* value = LuaWrapper::checkArg<const char*>(L, 2);
	copyString(buf, value);
	bool changed = ImGui::InputTextMultiline(name, buf, lengthOf(buf), ImVec2(-1, -1));
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


int ShowTestWindow(lua_State* L)
{
	ImGui::ShowDemoWindow();
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
	ImGui::Rect(w, h, color);
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
}

static const u32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'


static FS::OSOutputFile g_error_file;
static bool g_is_error_file_open = false;


#pragma pack(1)
class SerializedEngineHeader
{
public:
	u32 m_magic;
	u32 m_reserved; // for crc
};
#pragma pack()


static void showLogInVS(const char* system, const char* message)
{
	Debug::debugOutput(system);
	Debug::debugOutput(" : ");
	Debug::debugOutput(message);
	Debug::debugOutput("\n");
}


static void logErrorToFile(const char*, const char* message)
{
	if (!g_is_error_file_open) return;
	g_error_file.write(message, stringLength(message));
	g_error_file.flush();
}


class EngineImpl final : public Engine
{
public:
	void operator=(const EngineImpl&) = delete;
	EngineImpl(const EngineImpl&) = delete;

	EngineImpl(const char* working_dir, FS::FileSystem* fs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_prefab_resource_manager(m_allocator)
		, m_resource_manager(m_allocator)
		, m_lua_resources(m_allocator)
		, m_last_lua_resource_idx(-1)
		, m_fps(0)
		, m_is_game_running(false)
		, m_last_time_delta(0)
		, m_time(0)
		, m_path_manager(m_allocator)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
		, m_working_dir(working_dir)
	{
		g_log_info.log("Core") << "Creating engine...";
		Profiler::setThreadName("Worker");
		installUnhandledExceptionHandler();

		g_is_error_file_open = g_error_file.open("error.log");

		g_log_error.getCallback().bind<logErrorToFile>();
		g_log_info.getCallback().bind<showLogInVS>();
		g_log_warning.getCallback().bind<showLogInVS>();
		g_log_error.getCallback().bind<showLogInVS>();

		m_platform_data = {};
		m_state = luaL_newstate();
		luaL_openlibs(m_state);
		registerLuaAPI();

		m_file_system = FS::FileSystem::create(working_dir, m_allocator);

		m_resource_manager.init(*m_file_system);
		m_prefab_resource_manager.create(PrefabResource::TYPE, m_resource_manager);

		m_timer = Timer::create(m_allocator);
		m_fps_timer = Timer::create(m_allocator);
		m_fps_frame = 0;
		Reflection::init(m_allocator);

		m_plugin_manager = PluginManager::create(*this);
		m_input_system = InputSystem::create(*this);

		g_log_info.log("Core") << "Engine created.";
	}


	static bool LUA_hasFilesystemWork(Engine* engine)
	{
		return engine->getFileSystem().hasWork();
	}


	static void LUA_processFilesystemWork(Engine* engine)
	{
		engine->getFileSystem().updateAsyncTransactions();
	}


	static void LUA_startGame(Engine* engine, Universe* universe)
	{
		if(engine && universe) engine->startGame(*universe);
	}


	static bool LUA_createComponent(Universe* universe, EntityRef entity, const char* type)
	{
		if (!universe) return false;
		ComponentType cmp_type = Reflection::getComponentType(type);
		IScene* scene = universe->getScene(cmp_type);
		if (!scene) return false;
		if (universe->hasComponent(entity, cmp_type))
		{
			g_log_error.log("Lua Script") << "Component " << type << " already exists in entity " << entity.index;
			return false;
		}

		universe->createComponent(cmp_type, entity);
		return true;
	}


	static EntityRef LUA_createEntity(Universe* univ)
	{
		return univ->createEntity(DVec3(0, 0, 0), Quat(0, 0, 0, 1));
	}


	struct SetPropertyVisitor : public Reflection::IPropertyVisitor
	{
		void visit(const Reflection::Property<float>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isnumber(L, -1))
			{
				float f = (float)lua_tonumber(L, -1);
				InputMemoryStream input_blob(&f, sizeof(f));
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<int>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isnumber(L, -1))
			{
				int i = (int)lua_tointeger(L, -1);
				InputMemoryStream input_blob(&i, sizeof(i));
				prop.setValue(cmp, -1, input_blob);
			}

		}


		void visit(const Reflection::Property<EntityPtr>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isnumber(L, -1))
			{
				int i = (int)lua_tointeger(L, -1);
				InputMemoryStream input_blob(&i, sizeof(i));
				prop.setValue(cmp, -1, input_blob);
			}

		}


		void visit(const Reflection::Property<IVec2>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<IVec2>(L, -1);
				InputMemoryStream input_blob(&v, sizeof(v));
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<Vec2>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec2>(L, -1);
				InputMemoryStream input_blob(&v, sizeof(v));
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<Vec3>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec3>(L, -1);
				InputMemoryStream input_blob(&v, sizeof(v));
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<Vec4>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec4>(L, -1);
				InputMemoryStream input_blob(&v, sizeof(v));
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<Path>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isstring(L, -1))
			{
				const char* str = lua_tostring(L, -1);
				InputMemoryStream input_blob(str, stringLength(str) + 1);
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<bool>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isboolean(L, -1))
			{
				bool b = lua_toboolean(L, -1) != 0;
				InputMemoryStream input_blob(&b, sizeof(b));
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::Property<const char*>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isstring(L, -1))
			{
				const char* str = lua_tostring(L, -1);
				InputMemoryStream input_blob(str, stringLength(str) + 1);
				prop.setValue(cmp, -1, input_blob);
			}
		}


		void visit(const Reflection::IArrayProperty& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			
			if (lua_istable(L, -1)) {
				const int count = (int)lua_objlen(L, -1);
				for (int i = 0; i < count; ++i) {
					prop.addItem(cmp, prop.getCount(cmp));
				}
			}
		}


		void visit(const Reflection::IEnumProperty& prop) override { 
			if (!equalStrings(property_name, prop.name)) return;
			if (lua_isstring(L, -1)) {
				const char* str = lua_tostring(L, -1);
				for (int i = 0, c = prop.getEnumCount(cmp); i < c; ++i) {
					if (equalStrings(prop.getEnumName(cmp, i), str)) {
						const int value = prop.getEnumValue(cmp, i);
						InputMemoryStream input_blob(&value, sizeof(value));
						prop.setValue(cmp, -1, input_blob);
					}
				}
			}
		}


		void visit(const Reflection::IBlobProperty& prop) override { notSupported(prop); }
		void visit(const Reflection::ISampledFuncProperty& prop) override { notSupported(prop); }


		void notSupported(const Reflection::PropertyBase& prop)
		{
			if (!equalStrings(property_name, prop.name)) return;
			g_log_error.log("Lua Script") << "Property " << prop.name << " has unsupported type";
		}


		lua_State* L;
		ComponentUID cmp;
		const char* property_name;
	};


	static int LUA_getComponentType(const char* component_type)
	{
		return Reflection::getComponentType(component_type).index;
	}


	static int LUA_getComponentTypesCount()
	{
		return Reflection::getComponentTypesCount();
	}


	static int LUA_getComponentTypeByIndex(int index)
	{
		const char* id = Reflection::getComponentTypeID(index);
		return Reflection::getComponentType(id).index;
	}


	static int LUA_createEntityEx(lua_State* L)
	{
		auto* ctx = LuaWrapper::checkArg<Universe*>(L, 2);
		LuaWrapper::checkTableArg(L, 3);

		EntityRef e = ctx->createEntity(DVec3(0, 0, 0), Quat(0, 0, 0, 1));

		lua_pushvalue(L, 3);
		lua_getfield(L, -1, "parent");
		if (lua_type(L, -1) != LUA_TNIL)
		{
			EntityRef parent = LuaWrapper::toType<EntityRef>(L, -1);
			ctx->setParent(parent, e);
		}
		lua_pop(L, 1);

		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			const char* parameter_name = luaL_checkstring(L, -2);
			if (equalStrings(parameter_name, "position"))
			{
				auto pos = LuaWrapper::toType<DVec3>(L, -1);
				ctx->setPosition(e, pos);
			}
			else if (equalStrings(parameter_name, "pitch"))
			{
				const float angle = LuaWrapper::toType<float>(L, -1);
				const Quat rot(Vec3(1, 0, 0), Math::degreesToRadians(angle)); 
				ctx->setRotation(e, rot);
			}
			else if (equalStrings(parameter_name, "rotation"))
			{
				auto rot = LuaWrapper::toType<Quat>(L, -1);
				ctx->setRotation(e, rot);
			}
			else
			{
				ComponentType cmp_type = Reflection::getComponentType(parameter_name);
				IScene* scene = ctx->getScene(cmp_type);
				if (scene)
				{
					ComponentUID cmp(e, cmp_type, scene);
					const Reflection::ComponentBase* cmp_des = Reflection::getComponent(cmp_type);
					ctx->createComponent(cmp_type, e);
					lua_pushvalue(L, -1);
					lua_pushnil(L);
					while (lua_next(L, -2) != 0)
					{
						const char* property_name = luaL_checkstring(L, -2);
						SetPropertyVisitor v;
						v.property_name = property_name;
						v.cmp = cmp;
						v.L = L;
						cmp_des->visit(v);

						lua_pop(L, 1);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		LuaWrapper::push(L, e);
		return 1;
	}


	static int LUA_setEntityRotation(lua_State* L)
	{
		Universe* univ = LuaWrapper::checkArg<Universe*>(L, 1);
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


	static IScene* LUA_getScene(Universe* universe, const char* name)
	{
		u32 hash = crc32(name);
		return universe->getScene(hash);
	}


	static int LUA_loadResource(EngineImpl* engine, const char* path, const char* type)
	{
		Resource* res = engine->getResourceManager().load(ResourceType(type), Path(path));
		if(!res) return -1;
		++engine->m_last_lua_resource_idx;
		engine->m_lua_resources.insert(engine->m_last_lua_resource_idx, res);
		return engine->m_last_lua_resource_idx;
	}


	static void LUA_setEntityLocalRotation(Universe* universe, EntityRef entity, const Quat& rotation)
	{
		if (!universe->getParent(entity).isValid()) return;

		universe->setLocalRotation(entity, rotation);
	}


	static void LUA_setEntityLocalPosition(Universe* universe, EntityRef entity, const DVec3& position)
	{
		if (!universe->getParent(entity).isValid()) return;

		universe->setLocalPosition(entity, position);
	}


	static void LUA_setEntityPosition(Universe* univ, EntityRef entity, const DVec3& pos) { univ->setPosition(entity, pos); }
	static void LUA_unloadResource(EngineImpl* engine, int resource_idx) { engine->unloadLuaResource(resource_idx); }
	static Universe* LUA_createUniverse(EngineImpl* engine) { return &engine->createUniverse(false); }
	static void LUA_destroyUniverse(EngineImpl* engine, Universe* universe) { engine->destroyUniverse(*universe); }
	static Universe* LUA_getSceneUniverse(IScene* scene) { return &scene->getUniverse(); }
	static void LUA_logError(const char* text) { g_log_error.log("Lua Script") << text; }
	static void LUA_logInfo(const char* text) { g_log_info.log("Lua Script") << text; }
	static void LUA_pause(Engine* engine, bool pause) { engine->pause(pause); }
	static void LUA_nextFrame(Engine* engine) { engine->nextFrame(); }
	static void LUA_setTimeMultiplier(Engine* engine, float multiplier) { engine->setTimeMultiplier(multiplier); }
	static Vec4 LUA_multMatrixVec(const Matrix& m, const Vec4& v) { return m * v; }
	static Quat LUA_multQuat(const Quat& a, const Quat& b) { return a * b; }


	static int LUA_loadUniverse(lua_State* L)
	{
		auto* engine = LuaWrapper::checkArg<Engine*>(L, 1);
		auto* universe = LuaWrapper::checkArg<Universe*>(L, 2);
		auto* path = LuaWrapper::checkArg<const char*>(L, 3);
		if (!lua_isfunction(L, 4)) LuaWrapper::argError(L, 4, "function");
		FS::FileSystem& fs = engine->getFileSystem();
		FS::ContentCallback cb;
		struct Callback
		{
			~Callback()
			{
				luaL_unref(L, LUA_REGISTRYINDEX, lua_func);
			}

			void invoke(u64 size, const u8* mem, bool success)
			{
				if (!success)
				{
					g_log_error.log("Engine") << "Failed to open universe " << path;
				}
				else
				{
					InputMemoryStream blob(mem, size);
					#pragma pack(1)
						struct Header
						{
							u32 magic;
							int version;
							u32 hash;
							u32 engine_hash;
						};
					#pragma pack()
					Header header;
					blob.read(&header, sizeof(header));

					if (!engine->deserialize(*universe, blob))
					{
						g_log_error.log("Engine") << "Failed to deserialize universe " << path;
					}
					else
					{
						lua_rawgeti(L, LUA_REGISTRYINDEX, lua_func);
						if (lua_type(L, -1) != LUA_TFUNCTION)
						{
							ASSERT(false);
						}

						if (lua_pcall(L, 0, 0, 0) != 0)
						{
							g_log_error.log("Engine") << lua_tostring(L, -1);
							lua_pop(L, 1);
						}
					}
				}
				LUMIX_DELETE(engine->getAllocator(), this);
			}

			Engine* engine;
			Universe* universe;
			Path path;
			lua_State* L;
			int lua_func;
		};
		Callback* inst = LUMIX_NEW(engine->getAllocator(), Callback);
		inst->engine = engine;
		inst->universe = universe;
		inst->path = path;
		inst->L = L;
		inst->lua_func = luaL_ref(L, LUA_REGISTRYINDEX);
		cb.bind<Callback, &Callback::invoke>(inst);
		fs.getContent(inst->path, cb);
		return 0;
	}


	static int LUA_multVecQuat(lua_State* L)
	{
		Vec3 v = LuaWrapper::checkArg<Vec3>(L, 1);
		Quat q;
		if (LuaWrapper::isType<Quat>(L, 2))
		{
			q = LuaWrapper::checkArg<Quat>(L, 2);
		}
		else
		{
			Vec3 axis = LuaWrapper::checkArg<Vec3>(L, 2);
			float angle = LuaWrapper::checkArg<float>(L, 3);

			q = Quat(axis, angle);
		}
		
		Vec3 res = q.rotate(v);

		LuaWrapper::push(L, res);
		return 1;
	}


	static DVec3 LUA_getEntityPosition(Universe* universe, EntityRef entity)
	{
		return universe->getPosition(entity);
	}


	static Quat LUA_getEntityRotation(Universe* universe, EntityRef entity)
	{
		return universe->getRotation(entity);
	}


	static Vec3 LUA_getEntityDirection(Universe* universe, EntityRef entity)
	{
		Quat rot = universe->getRotation(entity);
		return rot.rotate(Vec3(0, 0, 1));
	}


	void registerLuaAPI()
	{
		lua_pushlightuserdata(m_state, this);
		lua_setglobal(m_state, "g_engine");

		#define REGISTER_FUNCTION(name) \
			LuaWrapper::createSystemFunction(m_state, "Engine", #name, \
				&LuaWrapper::wrap<decltype(&LUA_##name), LUA_##name>); \

		REGISTER_FUNCTION(createComponent);
		REGISTER_FUNCTION(createEntity);
		REGISTER_FUNCTION(createUniverse);
		REGISTER_FUNCTION(destroyUniverse);
		REGISTER_FUNCTION(getComponentType);
		REGISTER_FUNCTION(getComponentTypeByIndex);
		REGISTER_FUNCTION(getComponentTypesCount);
		REGISTER_FUNCTION(getEntityDirection);
		REGISTER_FUNCTION(getEntityPosition);
		REGISTER_FUNCTION(getEntityRotation);
		REGISTER_FUNCTION(getScene);
		REGISTER_FUNCTION(getSceneUniverse);
		REGISTER_FUNCTION(hasFilesystemWork);
		REGISTER_FUNCTION(loadResource);
		REGISTER_FUNCTION(logError);
		REGISTER_FUNCTION(logInfo);
		REGISTER_FUNCTION(multMatrixVec);
		REGISTER_FUNCTION(multQuat);
		REGISTER_FUNCTION(nextFrame);
		REGISTER_FUNCTION(pause);
		REGISTER_FUNCTION(processFilesystemWork);
		REGISTER_FUNCTION(setEntityLocalPosition);
		REGISTER_FUNCTION(setEntityLocalRotation);
		REGISTER_FUNCTION(setEntityPosition);
		REGISTER_FUNCTION(setEntityRotation);
		REGISTER_FUNCTION(setTimeMultiplier);
		REGISTER_FUNCTION(startGame);
		REGISTER_FUNCTION(unloadResource);

		LuaWrapper::createSystemFunction(m_state, "Engine", "loadUniverse", LUA_loadUniverse);

		#undef REGISTER_FUNCTION

		#define REGISTER_FUNCTION(F) \
			do { \
				auto f = &LuaWrapper::wrapMethod<Universe, decltype(&Universe::F), &Universe::F>; \
				LuaWrapper::createSystemFunction(m_state, "Engine", #F, f); \
			} while(false)

		REGISTER_FUNCTION(getFirstChild);
		REGISTER_FUNCTION(getNextSibling);
		REGISTER_FUNCTION(cloneEntity);
		REGISTER_FUNCTION(destroyEntity);
		REGISTER_FUNCTION(findByName);
		REGISTER_FUNCTION(getFirstEntity);
		REGISTER_FUNCTION(getNextEntity);
		REGISTER_FUNCTION(getParent);
		REGISTER_FUNCTION(hasComponent);
		REGISTER_FUNCTION(setParent);

		#undef REGISTER_FUNCTION


		LuaWrapper::createSystemFunction(m_state, "Engine", "instantiatePrefab", &LUA_instantiatePrefab);
		LuaWrapper::createSystemFunction(m_state, "Engine", "createEntityEx", &LUA_createEntityEx);
		LuaWrapper::createSystemFunction(m_state, "Engine", "multVecQuat", &LUA_multVecQuat);

		lua_newtable(m_state);
		lua_pushvalue(m_state, -1);
		lua_setglobal(m_state, "ImGui");

		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoMove", ImGuiWindowFlags_NoMove);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoCollapse", ImGuiWindowFlags_NoCollapse);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoInputs", ImGuiWindowFlags_NoInputs);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoResize", ImGuiWindowFlags_NoResize);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoTitleBar", ImGuiWindowFlags_NoTitleBar);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoScrollbar", ImGuiWindowFlags_NoScrollbar);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_AlwaysAutoResize", ImGuiWindowFlags_AlwaysAutoResize);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_FrameBg", ImGuiCol_FrameBg);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_WindowBg", ImGuiCol_WindowBg);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_Button", ImGuiCol_Button);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_ButtonActive", ImGuiCol_ButtonActive);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_ButtonHovered", ImGuiCol_ButtonHovered);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "StyleVar_FramePadding", ImGuiStyleVar_FramePadding);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "StyleVar_IndentSpacing", ImGuiStyleVar_IndentSpacing);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "StyleVar_ItemSpacing", ImGuiStyleVar_ItemSpacing);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "StyleVar_ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "StyleVar_WindowPadding", ImGuiStyleVar_WindowPadding);
		LuaImGui::registerCFunction(m_state, "AlignTextToFramePadding", &LuaImGui::AlignTextToFramePadding);
		LuaImGui::registerCFunction(m_state, "Begin", &LuaImGui::Begin);
		LuaImGui::registerCFunction(m_state, "BeginChildFrame", &LuaImGui::BeginChildFrame);
		LuaImGui::registerCFunction(m_state, "BeginPopup", LuaImGui::BeginPopup);
		LuaImGui::registerCFunction(m_state, "Button", &LuaImGui::Button);
		LuaImGui::registerCFunction(m_state, "CalcTextSize", &LuaImGui::CalcTextSize);
		LuaImGui::registerCFunction(m_state, "Checkbox", &LuaImGui::Checkbox);
		LuaImGui::registerCFunction(m_state, "CollapsingHeader", &LuaImGui::CollapsingHeader);
		LuaImGui::registerCFunction(m_state, "Columns", &LuaWrapper::wrap<decltype(&ImGui::Columns), &ImGui::Columns>);
		LuaImGui::registerCFunction(m_state, "DragFloat", &LuaImGui::DragFloat);
		LuaImGui::registerCFunction(m_state, "Dummy", &LuaWrapper::wrap<decltype(&LuaImGui::Dummy), &LuaImGui::Dummy>);
		LuaImGui::registerCFunction(m_state, "End", &LuaWrapper::wrap<decltype(&ImGui::End), &ImGui::End>);
		LuaImGui::registerCFunction(m_state, "EndChildFrame", &LuaWrapper::wrap<decltype(&ImGui::EndChildFrame), &ImGui::EndChildFrame>);
		LuaImGui::registerCFunction(m_state, "EndPopup", &LuaWrapper::wrap<decltype(&ImGui::EndPopup), &ImGui::EndPopup>);
		LuaImGui::registerCFunction(m_state, "GetColumnWidth", &LuaWrapper::wrap<decltype(&ImGui::GetColumnWidth), &ImGui::GetColumnWidth>);
		LuaImGui::registerCFunction(m_state, "GetDisplayWidth", &LuaImGui::GetDisplayWidth);
		LuaImGui::registerCFunction(m_state, "GetDisplayHeight", &LuaImGui::GetDisplayHeight);
		LuaImGui::registerCFunction(m_state, "GetWindowWidth", &LuaImGui::GetWindowWidth);
		LuaImGui::registerCFunction(m_state, "GetWindowHeight", &LuaImGui::GetWindowHeight);
		LuaImGui::registerCFunction(m_state, "GetWindowPos", &LuaImGui::GetWindowPos);
		LuaImGui::registerCFunction(m_state, "Indent", &LuaWrapper::wrap<decltype(&ImGui::Indent), &ImGui::Indent>);
		LuaImGui::registerCFunction(m_state, "InputTextMultiline", &LuaImGui::InputTextMultiline);
		LuaImGui::registerCFunction(m_state, "IsItemHovered", &LuaWrapper::wrap<decltype(&LuaImGui::IsItemHovered), &LuaImGui::IsItemHovered>);
		LuaImGui::registerCFunction(m_state, "IsMouseClicked", &LuaWrapper::wrap<decltype(&LuaImGui::IsMouseClicked), &LuaImGui::IsMouseClicked>);
		LuaImGui::registerCFunction(m_state, "IsMouseDown", &LuaWrapper::wrap<decltype(&LuaImGui::IsMouseDown), &LuaImGui::IsMouseDown>);
		LuaImGui::registerCFunction(m_state, "NewLine", &LuaWrapper::wrap<decltype(&ImGui::NewLine), &ImGui::NewLine>);
		LuaImGui::registerCFunction(m_state, "NextColumn", &LuaWrapper::wrap<decltype(&ImGui::NextColumn), &ImGui::NextColumn>);
		LuaImGui::registerCFunction(m_state, "OpenPopup", &LuaWrapper::wrap<decltype(&ImGui::OpenPopup), &ImGui::OpenPopup>);
		LuaImGui::registerCFunction(m_state, "PopItemWidth", &LuaWrapper::wrap<decltype(&ImGui::PopItemWidth), &ImGui::PopItemWidth>);
		LuaImGui::registerCFunction(m_state, "PopID", &LuaWrapper::wrap<decltype(&ImGui::PopID), &ImGui::PopID>);
		LuaImGui::registerCFunction(m_state, "PopStyleColor", &LuaWrapper::wrap<decltype(&ImGui::PopStyleColor), &ImGui::PopStyleColor>);
		LuaImGui::registerCFunction(m_state, "PopStyleVar", &LuaWrapper::wrap<decltype(&ImGui::PopStyleVar), &ImGui::PopStyleVar>);
		LuaImGui::registerCFunction(m_state, "PushItemWidth", &LuaWrapper::wrap<decltype(&ImGui::PushItemWidth), &ImGui::PushItemWidth>);
		LuaImGui::registerCFunction(m_state, "PushID", &LuaImGui::PushID);
		LuaImGui::registerCFunction(m_state, "PushStyleColor", &LuaImGui::PushStyleColor);
		LuaImGui::registerCFunction(m_state, "PushStyleVar", &LuaImGui::PushStyleVar);
		LuaImGui::registerCFunction(m_state, "Rect", &LuaWrapper::wrap<decltype(&LuaImGui::Rect), &LuaImGui::Rect>);
		LuaImGui::registerCFunction(m_state, "SameLine", &LuaImGui::SameLine);
		LuaImGui::registerCFunction(m_state, "Selectable", &LuaImGui::Selectable);
		LuaImGui::registerCFunction(m_state, "Separator", &LuaImGui::Separator);
		LuaImGui::registerCFunction(m_state, "SetCursorScreenPos", &LuaImGui::SetCursorScreenPos);
		LuaImGui::registerCFunction(m_state, "SetNextWindowPos", &LuaImGui::SetNextWindowPos);
		LuaImGui::registerCFunction(m_state, "SetNextWindowPosCenter", &LuaImGui::SetNextWindowPosCenter);
		LuaImGui::registerCFunction(m_state, "SetNextWindowSize", &LuaWrapper::wrap<decltype(&LuaImGui::SetNextWindowSize), &LuaImGui::SetNextWindowSize>);
		LuaImGui::registerCFunction(m_state, "SetStyleColor", &LuaImGui::SetStyleColor);
		LuaImGui::registerCFunction(m_state, "ShowTestWindow", &LuaImGui::ShowTestWindow);
		LuaImGui::registerCFunction(m_state, "SliderFloat", &LuaImGui::SliderFloat);
		LuaImGui::registerCFunction(m_state, "Text", &LuaImGui::Text);
		LuaImGui::registerCFunction(m_state, "Unindent", &LuaWrapper::wrap<decltype(&ImGui::Unindent), &ImGui::Unindent>);
		LuaImGui::registerCFunction(m_state, "LabelText", &LuaImGui::LabelText);

		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_DEVICE_KEYBOARD", InputSystem::Device::KEYBOARD);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_DEVICE_MOUSE", InputSystem::Device::MOUSE);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_DEVICE_CONTROLLER", InputSystem::Device::CONTROLLER);

		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_BUTTON_STATE_UP", InputSystem::ButtonEvent::UP);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_BUTTON_STATE_DOWN", InputSystem::ButtonEvent::DOWN);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_EVENT_BUTTON", InputSystem::Event::BUTTON);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_EVENT_AXIS", InputSystem::Event::AXIS);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_EVENT_TEXT_INPUT", InputSystem::Event::TEXT_INPUT);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_EVENT_DEVICE_ADDED", InputSystem::Event::DEVICE_ADDED);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_EVENT_DEVICE_REMOVED", InputSystem::Event::DEVICE_REMOVED);

		lua_pop(m_state, 1);

		installLuaPackageLoader();
	}


	void installLuaPackageLoader() const
	{
		lua_getglobal(m_state, "package");
		if (lua_type(m_state, -1) != LUA_TTABLE)
		{
			g_log_error.log("Engine") << "Lua \"package\" is not a table";
			return;
		}
		lua_getfield(m_state, -1, "searchers");
		if (lua_type(m_state, -1) != LUA_TTABLE)
		{
			g_log_error.log("Engine") << "Lua \"package.searchers\" is not a table";
			return;
		}
		int numLoaders = 0;
		lua_pushnil(m_state);
		while (lua_next(m_state, -2) != 0)
		{
			lua_pop(m_state, 1);
			numLoaders++;
		}

		lua_pushinteger(m_state, numLoaders + 1);
		lua_pushcfunction(m_state, LUA_packageLoader);
		lua_rawset(m_state, -3);
		lua_pop(m_state, 2);
	}


	static int LUA_packageLoader(lua_State* L)
	{
		/*
		const char* module = LuaWrapper::toType<const char*>(L, 1);
		StaticString<MAX_PATH_LENGTH> tmp(module);
		tmp << ".lua";
		lua_getglobal(L, "g_engine");
		auto* engine = (Engine*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		auto& fs = engine->getFileSystem();
		auto* file = fs.open(Path(tmp), FS::Mode::OPEN_AND_READ);
		if (!file)
		{
			g_log_error.log("Engine") << "Failed to open file " << tmp;
			StaticString<MAX_PATH_LENGTH + 40> msg("Failed to open file ");
			msg << tmp;
			lua_pushstring(L, msg);
		}
		else if (luaL_loadbuffer(L, (const char*)file->getBuffer(), file->size(), tmp) != 0)
		{
			g_log_error.log("Engine") << "Failed to load package " << tmp << ": " << lua_tostring(L, -1);
		}
		if (file) fs.close(*file);
		return 1;*/
		ASSERT(false);
		// TODO
		return 0;
	}

	
	~EngineImpl()
	{
		for (Resource* res : m_lua_resources)
		{
			res->getResourceManager().unload(*res);
		}

		Reflection::shutdown();
		Timer::destroy(m_timer);
		Timer::destroy(m_fps_timer);
		PluginManager::destroy(m_plugin_manager);
		if (m_input_system) InputSystem::destroy(*m_input_system);
		FS::FileSystem::destroy(m_file_system);

		m_prefab_resource_manager.destroy();
		lua_close(m_state);

		g_error_file.close();
	}


	void setPlatformData(const PlatformData& data) override
	{
		m_platform_data = data;
	}


	const PlatformData& getPlatformData() override
	{
		return m_platform_data;
	}



	IAllocator& getAllocator() override { return m_allocator; }
	PageAllocator& getPageAllocator() override { return m_page_allocator; }


	const char* getWorkingDirectory() const override { return m_working_dir; }


	Universe& createUniverse(bool set_lua_globals) override
	{
		Universe* universe = LUMIX_NEW(m_allocator, Universe)(m_allocator);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		for (auto* plugin : plugins)
		{
			plugin->createScenes(*universe);
		}

		if (set_lua_globals)
		{
			for (auto* scene : universe->getScenes())
			{
				const char* name = scene->getPlugin().getName();
				char tmp[128];

				copyString(tmp, "g_scene_");
				catString(tmp, name);
				lua_pushlightuserdata(m_state, scene);
				lua_setglobal(m_state, tmp);
			}
			lua_pushlightuserdata(m_state, universe);
			lua_setglobal(m_state, "g_universe");
		}

		return *universe;
	}


	void destroyUniverse(Universe& universe) override
	{
		auto& scenes = universe.getScenes();
		for (int i = scenes.size() - 1; i >= 0; --i)
		{
			auto* scene = scenes[i];
			scenes.pop();
			scene->clear();
			scene->getPlugin().destroyScene(scene);
		}
		LUMIX_DELETE(m_allocator, &universe);
		m_resource_manager.removeUnreferenced();
	}


	PluginManager& getPluginManager() override
	{
		return *m_plugin_manager;
	}


	FS::FileSystem& getFileSystem() override { return *m_file_system; }

	void startGame(Universe& context) override
	{
		ASSERT(!m_is_game_running);
		m_is_game_running = true;
		for (auto* scene : context.getScenes())
		{
			scene->startGame();
		}
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			plugin->startGame();
		}
	}


	void stopGame(Universe& context) override
	{
		ASSERT(m_is_game_running);
		m_is_game_running = false;
		for (auto* scene : context.getScenes())
		{
			scene->stopGame();
		}
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			plugin->stopGame();
		}
	}


	void pause(bool pause) override
	{
		m_paused = pause;
	}


	void nextFrame() override
	{
		m_next_frame = true;
	}


	void setTimeMultiplier(float multiplier) override
	{
		m_time_multiplier = Math::maximum(multiplier, 0.001f);
	}


	void update(Universe& context) override
	{
		PROFILE_FUNCTION();
		++m_fps_frame;
		if (m_fps_timer->getTimeSinceTick() > 0.5f)
		{
			m_fps = m_fps_frame / m_fps_timer->tick();
			m_fps_frame = 0;
		}
		float dt = m_timer->tick() * m_time_multiplier;
		if (m_next_frame)
		{
			m_paused = false;
			dt = 1 / 30.0f;
		}
		m_time += dt;
		m_last_time_delta = dt;
		{
			PROFILE_BLOCK("update scenes");
			for (auto* scene : context.getScenes())
			{
				scene->update(dt, m_paused);
			}
		}
		{
			PROFILE_BLOCK("late update scenes");
			for (auto* scene : context.getScenes())
			{
				scene->lateUpdate(dt, m_paused);
			}
		}
		m_plugin_manager->update(dt, m_paused);
		m_input_system->update(dt);
		getFileSystem().updateAsyncTransactions();

		if (m_next_frame)
		{
			m_paused = true;
			m_next_frame = false;
		}
	}


	InputSystem& getInputSystem() override { return *m_input_system; }


	ResourceManagerHub& getResourceManager() override
	{
		return m_resource_manager;
	}


	float getFPS() const override { return m_fps; }


	void serializerSceneVersions(OutputMemoryStream& serializer, Universe& ctx)
	{
		serializer.write(ctx.getScenes().size());
		for (auto* scene : ctx.getScenes())
		{
			serializer.write(crc32(scene->getPlugin().getName()));
			serializer.write(scene->getVersion());
		}
	}


	void serializePluginList(OutputMemoryStream& serializer)
	{
		serializer.write((i32)m_plugin_manager->getPlugins().size());
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			serializer.writeString(plugin->getName());
		}
	}


	bool hasSupportedSceneVersions(InputMemoryStream& serializer, Universe& ctx)
	{
		i32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			u32 hash;
			serializer.read(hash);
			auto* scene = ctx.getScene(hash);
			int version;
			serializer.read(version);
			if (version != scene->getVersion())
			{
				g_log_error.log("Core") << "Plugin " << scene->getPlugin().getName() << " has incompatible version";
				return false;
			}
		}
		return true;
	}


	bool hasSerializedPlugins(InputMemoryStream& serializer)
	{
		i32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			char tmp[32];
			serializer.readString(tmp, sizeof(tmp));
			if (!m_plugin_manager->getPlugin(tmp))
			{
				g_log_error.log("Core") << "Missing plugin " << tmp;
				return false;
			}
		}
		return true;
	}


	u32 serialize(Universe& ctx, IOutputStream& serializer) override
	{
		SerializedEngineHeader header;
		header.m_magic = SERIALIZED_ENGINE_MAGIC; // == '_LEN'
		header.m_reserved = 0;
		serializer.write(header);
		serializePluginList(serializer);
		serializerSceneVersions(serializer, ctx);
		m_path_manager.serialize(serializer);
		int pos = serializer.getPos();
		ctx.serialize(serializer);
		m_plugin_manager->serialize(serializer);
		serializer.write((i32)ctx.getScenes().size());
		for (auto* scene : ctx.getScenes())
		{
			serializer.writeString(scene->getPlugin().getName());
			scene->serialize(serializer);
		}
		u32 crc = crc32((const u8*)serializer.getData() + pos, serializer.getPos() - pos);
		return crc;
	}


	bool deserialize(Universe& ctx, IInputStream& serializer) override
	{
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.m_magic != SERIALIZED_ENGINE_MAGIC)
		{
			g_log_error.log("Core") << "Wrong or corrupted file";
			return false;
		}
		if (!hasSerializedPlugins(serializer)) return false;
		if (!hasSupportedSceneVersions(serializer, ctx)) return false;

		m_path_manager.deserialize(serializer);
		ctx.deserialize(serializer);
		m_plugin_manager->deserialize(serializer);
		i32 scene_count;
		serializer.read(scene_count);
		for (int i = 0; i < scene_count; ++i)
		{
			char tmp[32];
			serializer.readString(tmp, sizeof(tmp));
			IScene* scene = ctx.getScene(crc32(tmp));
			scene->deserialize(serializer);
		}
		m_path_manager.clear();
		return true;
	}


	ComponentUID createComponent(Universe& universe, EntityRef entity, ComponentType type) override
	{
		IScene* scene = universe.getScene(type);
		if (!scene) return ComponentUID::INVALID;

		universe.createComponent(type, entity);
		return ComponentUID(entity, type, scene);
	}


	static int LUA_instantiatePrefab(lua_State* L)
	{
		auto* engine = LuaWrapper::checkArg<EngineImpl*>(L, 1);
		auto* universe = LuaWrapper::checkArg<Universe*>(L, 2);
		DVec3 position = LuaWrapper::checkArg<DVec3>(L, 3);
		int prefab_id = LuaWrapper::checkArg<int>(L, 4);
		PrefabResource* prefab = static_cast<PrefabResource*>(engine->getLuaResource(prefab_id));
		if (!prefab)
		{
			g_log_error.log("Editor") << "Cannot instantiate null prefab.";
			return 0;
		}
		if (!prefab->isReady())
		{
			g_log_error.log("Editor") << "Prefab " << prefab->getPath().c_str() << " is not ready, preload it.";
			return 0;
		}
		EntityPtr entity = universe->instantiatePrefab(*prefab, position, {0, 0, 0, 1}, 1);

		LuaWrapper::push(L, entity);
		return 1;
	}


	void unloadLuaResource(int resource_idx) override
	{
		if (resource_idx < 0) return;
		Resource* res = m_lua_resources[resource_idx];
		m_lua_resources.erase(resource_idx);
		res->getResourceManager().unload(*res);
	}


	int addLuaResource(const Path& path, ResourceType type) override
	{
		Resource* res = m_resource_manager.load(type, path);
		if (!res) return -1;
		++m_last_lua_resource_idx;
		m_lua_resources.insert(m_last_lua_resource_idx, res);
		return m_last_lua_resource_idx;
	}


	Resource* getLuaResource(int idx) const override
	{
		auto iter = m_lua_resources.find(idx);
		if (iter.isValid()) return iter.value();
		return nullptr;
	}


	void runScript(const char* src, int src_length, const char* path) override
	{
		if (luaL_loadbuffer(m_state, src, src_length, path) != 0)
		{
			g_log_error.log("Engine") << path << ": " << lua_tostring(m_state, -1);
			lua_pop(m_state, 1);
			return;
		}

		if (lua_pcall(m_state, 0, 0, 0) != 0)
		{
			g_log_error.log("Engine") << path << ": " << lua_tostring(m_state, -1);
			lua_pop(m_state, 1);
		}
	}


	lua_State* getState() override { return m_state; }
	PathManager& getPathManager() override{ return m_path_manager; }
	float getLastTimeDelta() const override { return m_last_time_delta / m_time_multiplier; }
	double getTime() const override { return m_time; }

private:
	IAllocator& m_allocator;
	PageAllocator m_page_allocator;

	FS::FileSystem* m_file_system;

	ResourceManagerHub m_resource_manager;
	
	PluginManager* m_plugin_manager;
	PrefabResourceManager m_prefab_resource_manager;
	InputSystem* m_input_system;
	Timer* m_timer;
	Timer* m_fps_timer;
	int m_fps_frame;
	float m_time_multiplier;
	float m_fps;
	float m_last_time_delta;
	double m_time;
	bool m_is_game_running;
	bool m_paused;
	bool m_next_frame;
	PlatformData m_platform_data;
	PathManager m_path_manager;
	lua_State* m_state;
	HashMap<int, Resource*> m_lua_resources;
	int m_last_lua_resource_idx;
	StaticString<MAX_PATH_LENGTH> m_working_dir;
};


Engine* Engine::create(const char* working_dir,
	FS::FileSystem* fs,
	IAllocator& allocator)
{
	return LUMIX_NEW(allocator, EngineImpl)(working_dir, fs, allocator);
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, engine);
}


} // namespace Lumix
