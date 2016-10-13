#include "engine/engine.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/memory_file_device.h"
#include "engine/fs/os_file.h"
#include "engine/input_system.h"
#include "engine/iplugin.h"
#include "engine/lifo_allocator.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/mtjd/manager.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/timer.h"
#include "engine/universe/hierarchy.h"
#include "engine/universe/universe.h"
#include <imgui/imgui.h>


namespace Lumix
{

namespace LuaImGUI
{

int DragFloat(lua_State* L)
{
	auto* name = LuaWrapper::checkArg<const char*>(L, 1);
	float value = LuaWrapper::checkArg<float>(L, 2);
	bool changed = ImGui::DragFloat(name, &value);
	lua_pushboolean(L, changed);
	lua_pushnumber(L, value);
	return 2;
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


int Checkbox(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	bool b = LuaWrapper::checkArg<bool>(L, 2);
	bool clicked = ImGui::Checkbox(label, &b);
	lua_pushboolean(L, clicked);
	lua_pushboolean(L, b);
	return 2;
}


int SetNextWindowPos(lua_State* L)
{
	ImVec2 pos;
	pos.x = LuaWrapper::checkArg<float>(L, 1);
	pos.y = LuaWrapper::checkArg<float>(L, 2);
	ImGui::SetNextWindowPos(pos);
	return 0;
}


int AlignFirstTextHeightToWidgets(lua_State* L)
{
	ImGui::AlignFirstTextHeightToWidgets();
	return 0;
}


int Selectable(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	bool clicked = ImGui::Selectable(label);
	lua_pushboolean(L, clicked);
	return 1;
}


int Separator(lua_State* L)
{
	ImGui::Separator();
	return 0;
}


int Image(lua_State* L)
{
	auto* texture_id = LuaWrapper::checkArg<void*>(L, 1);
	float size_x = LuaWrapper::checkArg<float>(L, 2);
	float size_y = LuaWrapper::checkArg<float>(L, 3);
	ImGui::Image(texture_id, ImVec2(size_x, size_y));
	return 0;
}


int SetNextWindowPosCenter(lua_State* L)
{
	ImGui::SetNextWindowPosCenter();
	return 0;
}


int SetNextWindowSize(lua_State* L)
{
	ImVec2 size;
	size.x = LuaWrapper::checkArg<float>(L, 1);
	size.y = LuaWrapper::checkArg<float>(L, 2);
	ImGui::SetNextWindowSize(size);
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


int BeginDock(lua_State* L)
{
	auto* label = LuaWrapper::checkArg<const char*>(L, 1);
	bool res = ImGui::BeginDock(label);
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
	lua_pushvalue(L, -1);
	lua_pushcfunction(L, f);
	lua_setfield(L, -2, name);
}
}

static const uint32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'
static const ResourceType PREFAB_TYPE("prefab");
static const ComponentType HIERARCHY_TYPE = PropertyRegister::getComponentType("hierarchy");


static FS::OsFile g_error_file;
static bool g_is_error_file_opened = false;


enum class SerializedEngineVersion : int32
{
	BASE,
	SPARSE_TRANFORMATIONS,
	FOG_PARAMS,
	SCENE_VERSION,
	HIERARCHY_COMPONENT,
	SCENE_VERSION_CHECK,

	LATEST // must be the last one
};


#pragma pack(1)
class SerializedEngineHeader
{
public:
	uint32 m_magic;
	SerializedEngineVersion m_version;
	uint32 m_reserved; // for crc
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
	if (!g_is_error_file_opened) return;
	g_error_file.write(message, stringLength(message));
	g_error_file.flush();
}


class EngineImpl LUMIX_FINAL : public Engine
{
public:
	EngineImpl(const char* base_path0, const char* base_path1, FS::FileSystem* fs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_prefab_resource_manager(m_allocator)
		, m_resource_manager(m_allocator)
		, m_lua_resources(m_allocator)
		, m_last_lua_resource_idx(-1)
		, m_mtjd_manager(nullptr)
		, m_fps(0)
		, m_is_game_running(false)
		, m_last_time_delta(0)
		, m_path_manager(m_allocator)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
		, m_lifo_allocator(m_allocator, 10 * 1024 * 1024)
	{
		g_log_info.log("Core") << "Creating engine...";
		Profiler::setThreadName("Main");
		installUnhandledExceptionHandler();

		g_is_error_file_opened = g_error_file.open("error.log", FS::Mode::CREATE_AND_WRITE, allocator);

		g_log_error.getCallback().bind<logErrorToFile>();
		g_log_info.getCallback().bind<showLogInVS>();
		g_log_warning.getCallback().bind<showLogInVS>();
		g_log_error.getCallback().bind<showLogInVS>();

		m_platform_data = {};
		m_state = lua_newstate(luaAllocator, &m_allocator);
		luaL_openlibs(m_state);
		registerLuaAPI();

		m_mtjd_manager = MTJD::Manager::create(m_allocator);
		if (!fs)
		{
			m_file_system = FS::FileSystem::create(m_allocator);

			m_mem_file_device = LUMIX_NEW(m_allocator, FS::MemoryFileDevice)(m_allocator);
			m_disk_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)("disk", base_path0, m_allocator);

			m_file_system->mount(m_mem_file_device);
			m_file_system->mount(m_disk_file_device);
			bool is_patching = base_path1[0] != 0 && !equalStrings(base_path0, base_path1);
			if (is_patching)
			{
				m_patch_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)("patch", base_path1, m_allocator);
				m_file_system->mount(m_patch_file_device);
				m_file_system->setDefaultDevice("memory:patch:disk");
				m_file_system->setSaveGameDevice("memory:disk");
			}
			else
			{
				m_patch_file_device = nullptr;
				m_file_system->setDefaultDevice("memory:disk");
				m_file_system->setSaveGameDevice("memory:disk");
			}
		}
		else
		{
			m_file_system = fs;
			m_mem_file_device = nullptr;
			m_disk_file_device = nullptr;
			m_patch_file_device = nullptr;
		}

		m_resource_manager.create(*m_file_system);
		m_prefab_resource_manager.create(PREFAB_TYPE, m_resource_manager);

		m_timer = Timer::create(m_allocator);
		m_fps_timer = Timer::create(m_allocator);
		m_fps_frame = 0;
		PropertyRegister::init(m_allocator);

		m_plugin_manager = PluginManager::create(*this);
		HierarchyPlugin* hierarchy = LUMIX_NEW(m_allocator, HierarchyPlugin)(m_allocator);
		m_plugin_manager->addPlugin(hierarchy);
		m_input_system = InputSystem::create(m_allocator);

		registerProperties();

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


	static ComponentHandle LUA_createComponent(Universe* universe, Entity entity, const char* type)
	{
		if (!universe) return INVALID_COMPONENT;
		ComponentType cmp_type = PropertyRegister::getComponentType(type);
		IScene* scene = universe->getScene(cmp_type);
		if (!scene) return INVALID_COMPONENT;
		if (scene->getComponent(entity, cmp_type) != INVALID_COMPONENT)
		{
			g_log_error.log("Lua Script") << "Component " << type << " already exists in entity " << entity.index;
			return INVALID_COMPONENT;
		}

		return scene->createComponent(cmp_type, entity);
	}


	static Entity LUA_createEntity(Universe* univ)
	{
		return univ->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
	}


	static void setProperty(const ComponentUID& cmp,
		const IPropertyDescriptor& desc,
		lua_State* L,
		IAllocator& allocator)
	{
		switch (desc.getType())
		{
		case IPropertyDescriptor::STRING:
		case IPropertyDescriptor::FILE:
		case IPropertyDescriptor::RESOURCE:
			if (lua_isstring(L, -1))
			{
				const char* str = lua_tostring(L, -1);
				InputBlob input_blob(str, stringLength(str));
				desc.set(cmp, -1, input_blob);
			}
			break;
		case IPropertyDescriptor::DECIMAL:
			if (lua_isnumber(L, -1))
			{
				float f = (float)lua_tonumber(L, -1);
				InputBlob input_blob(&f, sizeof(f));
				desc.set(cmp, -1, input_blob);
			}
			break;
		case IPropertyDescriptor::ENTITY:
			if (lua_isinteger(L, -1))
			{
				int i = (int)lua_tointeger(L, -1);
				InputBlob input_blob(&i, sizeof(i));
				desc.set(cmp, -1, input_blob);
			}
			break;
		case IPropertyDescriptor::BOOL:
			if (lua_isboolean(L, -1))
			{
				bool b = lua_toboolean(L, -1) != 0;
				InputBlob input_blob(&b, sizeof(b));
				desc.set(cmp, -1, input_blob);
			}
			break;
		case IPropertyDescriptor::VEC3:
		case IPropertyDescriptor::COLOR:
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec3>(L, -1);
				InputBlob input_blob(&v, sizeof(v));
				desc.set(cmp, -1, input_blob);
			}
			break;
		case IPropertyDescriptor::VEC2:
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec2>(L, -1);
				InputBlob input_blob(&v, sizeof(v));
				desc.set(cmp, -1, input_blob);
			}
			break;
		case IPropertyDescriptor::INT2:
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Int2>(L, -1);
				InputBlob input_blob(&v, sizeof(v));
				desc.set(cmp, -1, input_blob);
			}
			break;
		default:
			g_log_error.log("Lua Script") << "Property " << desc.getName() << " has unsupported type";
			break;
		}
	}



	static int LUA_getComponentType(const char* component_type)
	{
		return PropertyRegister::getComponentType(component_type).index;
	}


	static ComponentHandle LUA_getComponent(Universe* universe, Entity entity, int component_type)
	{
		if (!universe->hasComponent(entity, {component_type})) return INVALID_COMPONENT;
		ComponentType type = {component_type};
		IScene* scene = universe->getScene(type);
		if (scene) return scene->getComponent(entity, type);
		
		ASSERT(false);
		return INVALID_COMPONENT;
	}


	static int LUA_createEntityEx(lua_State* L)
	{
		auto* engine = LuaWrapper::checkArg<Engine*>(L, 1);
		auto* ctx = LuaWrapper::checkArg<Universe*>(L, 2);
		LuaWrapper::checkTableArg(L, 3);

		Entity e = ctx->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));

		lua_pushvalue(L, 3);
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			const char* parameter_name = luaL_checkstring(L, -2);
			if (equalStrings(parameter_name, "position"))
			{
				auto pos = LuaWrapper::toType<Vec3>(L, -1);
				ctx->setPosition(e, pos);
			}
			else
			{
				ComponentType cmp_type = PropertyRegister::getComponentType(parameter_name);
				IScene* scene = ctx->getScene(cmp_type);
				if (scene)
				{
					ComponentUID cmp(e, cmp_type, scene, scene->createComponent(cmp_type, e));
					if (cmp.isValid())
					{
						lua_pushvalue(L, -1);
						lua_pushnil(L);
						while (lua_next(L, -2) != 0)
						{
							const char* property_name = luaL_checkstring(L, -2);
							auto* desc = PropertyRegister::getDescriptor(cmp_type, crc32(property_name));
							if (!desc)
							{
								g_log_error.log("Lua Script") << "Unknown property " << property_name;
							}
							else
							{
								setProperty(cmp, *desc, L, engine->getAllocator());
							}

							lua_pop(L, 1);
						}
						lua_pop(L, 1);
					}
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		LuaWrapper::push(L, e);
		return 1;
	}


	static void LUA_setEntityPosition(Universe* univ, Entity entity, Vec3 pos)
	{
		univ->setPosition(entity, pos);
	}


	static void LUA_setEntityRotation(Universe* univ,
		int entity_index,
		Vec3 axis,
		float angle)
	{
		if (entity_index < 0 || entity_index > univ->getEntityCount()) return;

		univ->setRotation({entity_index}, Quat(axis, angle));
	}

	static void LUA_unloadResource(EngineImpl* engine, int resource_idx)
	{
		engine->unloadLuaResource(resource_idx);
	}

	static int LUA_loadResource(EngineImpl* engine, const char* path, const char* type)
	{
		ResourceManagerBase* res_manager = engine->getResourceManager().get(ResourceType(type));
		if (!res_manager) return -1;
		Resource* res = res_manager->load(Path(path));
		++engine->m_last_lua_resource_idx;
		engine->m_lua_resources.insert(engine->m_last_lua_resource_idx, res);
		return engine->m_last_lua_resource_idx;
	}


	static void LUA_setEntityLocalRotation(IScene* scene,
		Entity entity,
		const Quat& rotation)
	{
		if (!isValid(entity)) return;

		auto* hierarchy = static_cast<Hierarchy*>(scene);
		ComponentHandle cmp = hierarchy->getComponent(entity, HIERARCHY_TYPE);
		if (isValid(cmp)) hierarchy->setLocalRotation(cmp, rotation);
	}


	static void LUA_setEntityLocalPosition(IScene* scene,
		Entity entity,
		const Vec3& position)
	{
		if (!isValid(entity)) return;

		auto* hierarchy = static_cast<Hierarchy*>(scene);
		ComponentHandle cmp = hierarchy->getComponent(entity, HIERARCHY_TYPE);
		if (isValid(cmp)) hierarchy->setLocalPosition(cmp, position);
	}


	static void LUA_logError(const char* text)
	{
		g_log_error.log("Lua Script") << text;
	}


	static void LUA_logInfo(const char* text)
	{
		g_log_info.log("Lua Script") << text;
	}


	static float LUA_getInputActionValue(Engine* engine, uint32 action)
	{
		auto v = engine->getInputSystem().getActionValue(action);
		return v;
	}


	static void LUA_addInputAction(Engine* engine, uint32 action, int type, int key, int controller_id)
	{
		engine->getInputSystem().addAction(
			action, Lumix::InputSystem::InputType(type), key, controller_id);
	}


	static Entity LUA_getFirstEntity(Universe* universe)
	{
		return universe->getFirstEntity();
	}


	static Entity LUA_getNextEntity(Universe* universe, Entity entity)
	{
		return universe->getNextEntity(entity);
	}


	static Vec4 LUA_multMatrixVec(const Matrix& m, const Vec4& v)
	{
		return m * v;
	}


	static Quat LUA_multQuat(const Quat& a, const Quat& b)
	{
		return a * b;
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


	static Vec3 LUA_getEntityPosition(Universe* universe, Entity entity)
	{
		if (!isValid(entity))
		{
			g_log_warning.log("Engine") << "Requesting position on invalid entity";
			return Vec3(0, 0, 0);
		}
		return universe->getPosition(entity);
	}


	static Quat LUA_getEntityRotation(Universe* universe, Entity entity)
	{
		if (!isValid(entity))
		{
			g_log_warning.log("Engine") << "Requesting rotation on invalid entity";
			return Quat(0, 0, 0, 1);
		}
		return universe->getRotation(entity);
	}


	static void LUA_destroyEntity(Universe* universe, Entity entity)
	{
		universe->destroyEntity(entity);
	}


	static Vec3 LUA_getEntityDirection(Universe* universe, Entity entity)
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

		REGISTER_FUNCTION(loadResource);
		REGISTER_FUNCTION(unloadResource);
		REGISTER_FUNCTION(createComponent);
		REGISTER_FUNCTION(createEntity);
		REGISTER_FUNCTION(setEntityPosition);
		REGISTER_FUNCTION(getEntityPosition);
		REGISTER_FUNCTION(getEntityDirection);
		REGISTER_FUNCTION(setEntityRotation);
		REGISTER_FUNCTION(getEntityRotation);
		REGISTER_FUNCTION(setEntityLocalRotation);
		REGISTER_FUNCTION(setEntityLocalPosition);
		REGISTER_FUNCTION(getInputActionValue);
		REGISTER_FUNCTION(addInputAction);
		REGISTER_FUNCTION(logError);
		REGISTER_FUNCTION(logInfo);
		REGISTER_FUNCTION(startGame);
		REGISTER_FUNCTION(hasFilesystemWork);
		REGISTER_FUNCTION(processFilesystemWork);
		REGISTER_FUNCTION(destroyEntity);
		REGISTER_FUNCTION(getComponent);
		REGISTER_FUNCTION(getComponentType);
		REGISTER_FUNCTION(multMatrixVec);
		REGISTER_FUNCTION(multQuat);
		REGISTER_FUNCTION(getFirstEntity);
		REGISTER_FUNCTION(getNextEntity);

		#undef REGISTER_FUNCTION

		LuaWrapper::createSystemFunction(m_state, "Engine", "instantiatePrefab", &LUA_instantiatePrefab);
		LuaWrapper::createSystemFunction(m_state, "Engine", "createEntityEx", &LUA_createEntityEx);
		LuaWrapper::createSystemFunction(m_state, "Engine", "multVecQuat", &LUA_multVecQuat);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_DOWN", InputSystem::DOWN);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_PRESSED", InputSystem::PRESSED);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_MOUSE_X", InputSystem::MOUSE_X);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_MOUSE_Y", InputSystem::MOUSE_Y);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_LTHUMB_X", InputSystem::LTHUMB_X);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_LTHUMB_Y", InputSystem::LTHUMB_Y);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_RTHUMB_X", InputSystem::RTHUMB_X);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_RTHUMB_Y", InputSystem::RTHUMB_Y);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_RTRIGGER", InputSystem::RTRIGGER);
		LuaWrapper::createSystemVariable(m_state, "Engine", "INPUT_TYPE_LTRIGGER", InputSystem::LTRIGGER);

		lua_newtable(m_state);
		lua_pushvalue(m_state, -1);
		lua_setglobal(m_state, "ImGui");

		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoMove", ImGuiWindowFlags_NoMove);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoCollapse", ImGuiWindowFlags_NoCollapse);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoResize", ImGuiWindowFlags_NoResize);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoTitleBar", ImGuiWindowFlags_NoTitleBar);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_NoScrollbar", ImGuiWindowFlags_NoScrollbar);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "WindowFlags_AlwaysAutoResize", ImGuiWindowFlags_AlwaysAutoResize);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_FrameBg", ImGuiCol_FrameBg);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_WindowBg", ImGuiCol_WindowBg);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_Button", ImGuiCol_Button);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_ButtonActive", ImGuiCol_ButtonActive);
		LuaWrapper::createSystemVariable(m_state, "ImGui", "Col_ButtonHovered", ImGuiCol_ButtonHovered);
		LuaImGUI::registerCFunction(m_state, "SetStyleColor", &LuaImGUI::SetStyleColor);
		LuaImGUI::registerCFunction(m_state, "DragFloat", &LuaImGUI::DragFloat);
		LuaImGUI::registerCFunction(m_state, "SliderFloat", &LuaImGUI::SliderFloat);
		LuaImGUI::registerCFunction(m_state, "Button", &LuaImGUI::Button);
		LuaImGUI::registerCFunction(m_state, "Text", &LuaImGUI::Text);
		LuaImGUI::registerCFunction(m_state, "Checkbox", &LuaImGUI::Checkbox);
		LuaImGUI::registerCFunction(m_state, "SetNextWindowPos", &LuaImGUI::SetNextWindowPos);
		LuaImGUI::registerCFunction(m_state, "AlignFirstTextHeightToWidgets", &LuaImGUI::AlignFirstTextHeightToWidgets);
		LuaImGUI::registerCFunction(m_state, "Selectable", &LuaImGUI::Selectable);
		LuaImGUI::registerCFunction(m_state, "Separator", &LuaImGUI::Separator);
		LuaImGUI::registerCFunction(m_state, "GetDisplayWidth", &LuaImGUI::GetDisplayWidth);
		LuaImGUI::registerCFunction(m_state, "GetDisplayHeight", &LuaImGUI::GetDisplayHeight);
		LuaImGUI::registerCFunction(m_state, "GetWindowWidth", &LuaImGUI::GetWindowWidth);
		LuaImGUI::registerCFunction(m_state, "GetWindowHeight", &LuaImGUI::GetWindowHeight);
		LuaImGUI::registerCFunction(m_state, "SameLine", &LuaImGUI::SameLine);
		LuaImGUI::registerCFunction(m_state, "Begin", &LuaImGUI::Begin);
		LuaImGUI::registerCFunction(m_state, "BeginDock", LuaImGUI::BeginDock);
		LuaImGUI::registerCFunction(m_state, "BeginChildFrame", &LuaImGUI::BeginChildFrame);
		LuaImGUI::registerCFunction(m_state, "SetNextWindowPosCenter", &LuaImGUI::SetNextWindowPosCenter);
		LuaImGUI::registerCFunction(m_state, "SetNextWindowSize", &LuaImGUI::SetNextWindowSize);
		LuaImGUI::registerCFunction(m_state, "BeginPopup", &LuaWrapper::wrap<decltype(&ImGui::BeginPopup), &ImGui::BeginPopup>);
		LuaImGUI::registerCFunction(m_state, "EndPopup", &LuaWrapper::wrap<decltype(&ImGui::EndPopup), &ImGui::EndPopup>);
		LuaImGUI::registerCFunction(m_state, "OpenPopup", &LuaWrapper::wrap<decltype(&ImGui::OpenPopup), &ImGui::OpenPopup>);
		LuaImGUI::registerCFunction(m_state, "EndDock", &LuaWrapper::wrap<decltype(&ImGui::EndDock), &ImGui::EndDock>);
		LuaImGUI::registerCFunction(m_state, "End", &LuaWrapper::wrap<decltype(&ImGui::End), &ImGui::End>);
		LuaImGUI::registerCFunction(m_state, "EndChildFrame", &LuaWrapper::wrap<decltype(&ImGui::EndChildFrame), &ImGui::EndChildFrame>);
		LuaImGUI::registerCFunction(m_state, "PushItemWidth", &LuaWrapper::wrap<decltype(&ImGui::PushItemWidth), &ImGui::PushItemWidth>);
		LuaImGUI::registerCFunction(m_state, "PopItemWidth", &LuaWrapper::wrap<decltype(&ImGui::PopItemWidth), &ImGui::PopItemWidth>);
		LuaImGUI::registerCFunction(m_state, "Image", &LuaWrapper::wrap<decltype(&LuaImGUI::Image), &LuaImGUI::Image>);

		lua_pop(m_state, 1);

		installLuaPackageLoader();
	}


	void installLuaPackageLoader() const
	{
		if (lua_getglobal(m_state, "package") != LUA_TTABLE)
		{
			g_log_error.log("Engine") << "Lua \"package\" is not a table";
			return;
		};
		if (lua_getfield(m_state, -1, "searchers") != LUA_TTABLE)
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
		const char* module = LuaWrapper::toType<const char*>(L, 1);
		StaticString<MAX_PATH_LENGTH> tmp(module);
		tmp << ".lua";
		lua_getglobal(L, "g_engine");
		auto* engine = (Engine*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		auto& fs = engine->getFileSystem();
		auto* file = fs.open(fs.getDefaultDevice(), Path(tmp), FS::Mode::OPEN_AND_READ);
		if (!file)
		{
			g_log_error.log("Engine") << "Failed to open file " << tmp;
			StaticString<MAX_PATH_LENGTH + 40> msg("Failed to open file ");
			msg << tmp;
			lua_pushstring(L, msg);
		}
		else if (luaL_loadbuffer(L, (const char*)file->getBuffer(), file->size(), tmp) != LUA_OK)
		{
			g_log_error.log("Engine") << "Failed to load package " << tmp << ": " << lua_tostring(L, -1);
		}
		if (file) fs.close(*file);
		return 1;
	}


	static void* luaAllocator(void* ud, void* ptr, size_t osize, size_t nsize)
	{
		auto& allocator = *static_cast<IAllocator*>(ud);
		if (nsize == 0)
		{
			allocator.deallocate(ptr);
			return nullptr;
		}
		if (nsize > 0 && ptr == nullptr) return allocator.allocate(nsize);

		void* new_mem = allocator.allocate(nsize);
		copyMemory(new_mem, ptr, Math::minimum(osize, nsize));
		allocator.deallocate(ptr);
		return new_mem;
	}


	void registerProperties()
	{
		PropertyRegister::add("hierarchy",
			LUMIX_NEW(m_allocator, EntityPropertyDescriptor<Hierarchy>)(
				"Parent", &Hierarchy::getParent, &Hierarchy::setParent));
		PropertyRegister::add("hierarchy",
			LUMIX_NEW(m_allocator, SimplePropertyDescriptor<Vec3, Hierarchy>)(
				"Relative position", &Hierarchy::getLocalPosition, &Hierarchy::setLocalPosition));
		auto relative_rot = LUMIX_NEW(m_allocator, SimplePropertyDescriptor<Vec3, Hierarchy>)(
			"Relative rotation", &Hierarchy::getLocalRotationEuler, &Hierarchy::setLocalRotationEuler);
		relative_rot->setIsInRadians(true);
		PropertyRegister::add("hierarchy", relative_rot);
	}


	~EngineImpl()
	{
		for (Resource* res : m_lua_resources)
		{
			res->getResourceManager().unload(*res);
		}

		PropertyRegister::shutdown();
		Timer::destroy(m_timer);
		Timer::destroy(m_fps_timer);
		PluginManager::destroy(m_plugin_manager);
		if (m_input_system) InputSystem::destroy(*m_input_system);
		if (m_disk_file_device)
		{
			FS::FileSystem::destroy(m_file_system);
			LUMIX_DELETE(m_allocator, m_mem_file_device);
			LUMIX_DELETE(m_allocator, m_disk_file_device);
			LUMIX_DELETE(m_allocator, m_patch_file_device);
		}

		m_prefab_resource_manager.destroy();
		m_resource_manager.destroy();
		MTJD::Manager::destroy(*m_mtjd_manager);
		lua_close(m_state);

		g_error_file.close();
	}


	void setPatchPath(const char* path) override
	{
		if (!path || path[0] == '\0')
		{
			if(m_patch_file_device)
			{
				m_file_system->setDefaultDevice("memory:disk");
				m_file_system->unMount(m_patch_file_device);
				LUMIX_DELETE(m_allocator, m_patch_file_device);
				m_patch_file_device = nullptr;
			}

			return;
		}

		if (!m_patch_file_device)
		{
			m_patch_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)("patch", path, m_allocator);
			m_file_system->mount(m_patch_file_device);
			m_file_system->setDefaultDevice("memory:patch:disk");
			m_file_system->setSaveGameDevice("memory:disk");
		}
		else
		{
			m_patch_file_device->setBasePath(path);
		}
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


	Universe& createUniverse() override
	{
		Universe* universe = LUMIX_NEW(m_allocator, Universe)(m_allocator);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		for (auto* plugin : plugins)
		{
			IScene* scene = plugin->createScene(*universe);
			if (scene)
			{
				universe->addScene(scene);
			}
		}

		for (auto* scene : universe->getScenes())
		{
			const char* name = scene->getPlugin().getName();
			char tmp[128];

			copyString(tmp, "g_scene_");
			catString(tmp, name);
			lua_pushlightuserdata(m_state, scene);
			lua_setglobal(m_state, tmp);
		}

		return *universe;
	}


	MTJD::Manager& getMTJDManager() override { return *m_mtjd_manager; }


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
	FS::DiskFileDevice* getDiskFileDevice() override { return m_disk_file_device; }
	FS::DiskFileDevice* getPatchFileDevice() override { return m_patch_file_device; }

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
		float dt;
		++m_fps_frame;
		if (m_fps_timer->getTimeSinceTick() > 0.5f)
		{
			m_fps = m_fps_frame / m_fps_timer->tick();
			m_fps_frame = 0;
		}
		dt = m_timer->tick() * m_time_multiplier;
		if (m_next_frame)
		{
			m_paused = false;
			dt = 1 / 30.0f;
		}
		m_last_time_delta = dt;
		{
			PROFILE_BLOCK("update scenes");
			for (auto* scene : context.getScenes())
			{
				scene->update(dt, m_paused);
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


	ResourceManager& getResourceManager() override
	{
		return m_resource_manager;
	}


	float getFPS() const override { return m_fps; }


	void serializerSceneVersions(OutputBlob& serializer, Universe& ctx)
	{
		serializer.write(ctx.getScenes().size());
		for (auto* scene : ctx.getScenes())
		{
			serializer.write(crc32(scene->getPlugin().getName()));
			serializer.write(scene->getVersion());
		}
	}


	void serializePluginList(OutputBlob& serializer)
	{
		serializer.write((int32)m_plugin_manager->getPlugins().size());
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			serializer.writeString(plugin->getName());
		}
	}


	bool hasSupportedSceneVersions(InputBlob& serializer, Universe& ctx)
	{
		int32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			uint32 hash;
			serializer.read(hash);
			auto* scene = ctx.getScene(hash);
			int version;
			serializer.read(version);
			if (version > scene->getVersion())
			{
				g_log_error.log("Core") << "Plugin " << scene->getPlugin().getName() << " is too old";
				return false;
			}
		}
		return true;
	}


	bool hasSerializedPlugins(InputBlob& serializer)
	{
		int32 count;
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


	uint32 serialize(Universe& ctx, OutputBlob& serializer) override
	{
		SerializedEngineHeader header;
		header.m_magic = SERIALIZED_ENGINE_MAGIC; // == '_LEN'
		header.m_version = SerializedEngineVersion::LATEST;
		header.m_reserved = 0;
		serializer.write(header);
		serializePluginList(serializer);
		serializerSceneVersions(serializer, ctx);
		m_path_manager.serialize(serializer);
		int pos = serializer.getPos();
		ctx.serialize(serializer);
		m_plugin_manager->serialize(serializer);
		serializer.write((int32)ctx.getScenes().size());
		for (auto* scene : ctx.getScenes())
		{
			serializer.writeString(scene->getPlugin().getName());
			serializer.write(scene->getVersion());
			scene->serialize(serializer);
		}
		uint32 crc = crc32((const uint8*)serializer.getData() + pos, serializer.getPos() - pos);
		return crc;
	}


	bool deserialize(Universe& ctx, InputBlob& serializer) override
	{
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.m_magic != SERIALIZED_ENGINE_MAGIC)
		{
			g_log_error.log("Core") << "Wrong or corrupted file";
			return false;
		}
		if (header.m_version > SerializedEngineVersion::LATEST)
		{
			g_log_error.log("Core") << "Unsupported version";
			return false;
		}
		if (!hasSerializedPlugins(serializer))
		{
			return false;
		}
		if (header.m_version > SerializedEngineVersion::SCENE_VERSION_CHECK &&
			!hasSupportedSceneVersions(serializer, ctx))
		{
			return false;
		}

		m_path_manager.deserialize(serializer);
		ctx.deserialize(serializer);

		if (header.m_version <= SerializedEngineVersion::HIERARCHY_COMPONENT)
		{
			static const uint32 HIERARCHY_HASH = crc32("hierarchy");
			ctx.getScene(HIERARCHY_HASH)->deserialize(serializer, 0);
		}

		m_plugin_manager->deserialize(serializer);
		int32 scene_count;
		serializer.read(scene_count);
		for (int i = 0; i < scene_count; ++i)
		{
			char tmp[32];
			serializer.readString(tmp, sizeof(tmp));
			IScene* scene = ctx.getScene(crc32(tmp));
			int scene_version = -1;
			if (header.m_version > SerializedEngineVersion::SCENE_VERSION)
			{
				serializer.read(scene_version);
			}
			scene->deserialize(serializer, scene_version);
		}
		m_path_manager.clear();
		return true;
	}


	ComponentUID createComponent(Universe& universe, Entity entity, ComponentType type)
	{
		ComponentUID cmp;
		IScene* scene = universe.getScene(type);
		if (!scene) return ComponentUID::INVALID;

		return ComponentUID(entity, type, scene, scene->createComponent(type, entity));
	}


	void pasteEntities(const Vec3& position, Universe& universe, InputBlob& blob, Array<Entity>& entities) override
	{
		int entity_count;
		blob.read(entity_count);
		entities.reserve(entities.size() + entity_count);

		Matrix base_matrix = Matrix::IDENTITY;
		base_matrix.setTranslation(position);
		for (int i = 0; i < entity_count; ++i)
		{
			Matrix mtx;
			blob.read(mtx);
			if (i == 0)
			{
				Matrix inv = mtx;
				inv.inverse();
				base_matrix.copy3x3(mtx);
				base_matrix = base_matrix * inv;
				mtx.setTranslation(position);
			}
			else
			{
				mtx = base_matrix * mtx;
			}
			Entity new_entity = universe.createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
			entities.push(new_entity);
			universe.setMatrix(new_entity, mtx);
			int32 count;
			blob.read(count);
			for (int i = 0; i < count; ++i)
			{
				uint32 hash;
				blob.read(hash);
				ComponentType type = PropertyRegister::getComponentTypeFromHash(hash);
				ComponentUID cmp = createComponent(universe, new_entity, type);
				Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(type);
				for (int j = 0; j < props.size(); ++j)
				{
					props[j]->set(cmp, -1, blob);
				}
			}
		}
	}


	static int LUA_instantiatePrefab(lua_State* L)
	{
		auto* engine = LuaWrapper::checkArg<EngineImpl*>(L, 1);
		auto* universe = LuaWrapper::checkArg<Universe*>(L, 2);
		Vec3 position = LuaWrapper::checkArg<Vec3>(L, 3);
		int prefab_id = LuaWrapper::checkArg<int>(L, 4);
		PrefabResource* prefab = static_cast<PrefabResource*>(engine->getLuaResource(prefab_id));
		ASSERT(prefab->isReady());
		if (!prefab->isReady())
		{
			g_log_error.log("Editor") << "Prefab " << prefab->getPath().c_str() << " is not ready, preload it.";
			return 0;
		}
		InputBlob blob(prefab->blob.getData(), prefab->blob.getPos());
		Array<Entity> entities(engine->m_lifo_allocator);
		engine->pasteEntities(position, *universe, blob, entities);

		lua_createtable(L, entities.size(), 0);
		for (int i = 0; i < entities.size(); ++i)
		{
			LuaWrapper::push(L, entities[i]);
			lua_rawseti(L, -2, i + 1);
		}
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
		ResourceManagerBase* manager = m_resource_manager.get(type);
		if (!manager) return -1;
		Resource* res = manager->load(path);
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


	IAllocator& getLIFOAllocator() override
	{
		return m_lifo_allocator;
	}


	void runScript(const char* src, int src_length, const char* path) override
	{
		if (luaL_loadbuffer(m_state, src, src_length, path) != LUA_OK)
		{
			g_log_error.log("Engine") << path << ": " << lua_tostring(m_state, -1);
			lua_pop(m_state, 1);
			return;
		}

		if (lua_pcall(m_state, 0, 0, 0) != LUA_OK)
		{
			g_log_error.log("Engine") << path << ": " << lua_tostring(m_state, -1);
			lua_pop(m_state, 1);
		}
	}


	lua_State* getState() override { return m_state; }
	PathManager& getPathManager() override{ return m_path_manager; }
	float getLastTimeDelta() override { return m_last_time_delta / m_time_multiplier; }

private:
	IAllocator& m_allocator;
	LIFOAllocator m_lifo_allocator;

	FS::FileSystem* m_file_system;
	FS::MemoryFileDevice* m_mem_file_device;
	FS::DiskFileDevice* m_disk_file_device;
	FS::DiskFileDevice* m_patch_file_device;

	ResourceManager m_resource_manager;
	
	MTJD::Manager* m_mtjd_manager;

	PluginManager* m_plugin_manager;
	PrefabResourceManager m_prefab_resource_manager;
	InputSystem* m_input_system;
	Timer* m_timer;
	Timer* m_fps_timer;
	int m_fps_frame;
	float m_time_multiplier;
	float m_fps;
	float m_last_time_delta;
	bool m_is_game_running;
	bool m_paused;
	bool m_next_frame;
	PlatformData m_platform_data;
	PathManager m_path_manager;
	lua_State* m_state;
	HashMap<int, Resource*> m_lua_resources;
	int m_last_lua_resource_idx;

private:
	void operator=(const EngineImpl&);
	EngineImpl(const EngineImpl&);
};


Engine* Engine::create(const char* base_path0,
	const char* base_path1,
	FS::FileSystem* fs,
	IAllocator& allocator)
{
	return LUMIX_NEW(allocator, EngineImpl)(base_path0, base_path1, fs, allocator);
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, engine);
}


} // ~namespace Lumix
