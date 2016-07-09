#include "engine/engine.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/fs/os_file.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/timer.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/memory_file_device.h"
#include "engine/mtjd/manager.h"
#include "engine/debug/debug.h"
#include "engine/iplugin.h"
#include "engine/prefab.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/plugin_manager.h"
#include "engine/universe/hierarchy.h"
#include "engine/universe/universe.h"


namespace Lumix
{

static const uint32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'
static const uint32 PREFAB_HASH = crc32("prefab");
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


class EngineImpl : public Engine
{
public:
	EngineImpl(const char* base_path0, const char* base_path1, FS::FileSystem* fs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_prefab_resource_manager(m_allocator)
		, m_resource_manager(m_allocator)
		, m_mtjd_manager(nullptr)
		, m_fps(0)
		, m_is_game_running(false)
		, m_last_time_delta(0)
		, m_path_manager(m_allocator)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
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
		m_prefab_resource_manager.create(PREFAB_HASH, m_resource_manager);

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


	static ComponentHandle LUA_createComponent(IScene* scene, const char* type, int entity_idx)
	{
		if (!scene) return INVALID_COMPONENT;
		Entity e = {entity_idx};
		ComponentType handle = PropertyRegister::getComponentType(type);
		if (scene->getComponent(e, handle) != INVALID_COMPONENT)
		{
			g_log_error.log("Lua Script") << "Component " << type << " already exists in entity "
				<< entity_idx;
			return INVALID_COMPONENT;
		}

		return scene->createComponent(handle, e);
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
		for (auto* scene : universe->getScenes())
		{
			if (!scene->ownComponentType(type)) continue;
			return scene->getComponent(entity, type);
		}
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
				ComponentType type_handle = PropertyRegister::getComponentType(parameter_name);
				for (auto* scene : ctx->getScenes())
				{
					ComponentUID cmp(e, type_handle, scene, scene->createComponent(type_handle, e));
					if (cmp.isValid())
					{
						lua_pushvalue(L, -1);
						lua_pushnil(L);
						while (lua_next(L, -2) != 0)
						{
							const char* property_name = luaL_checkstring(L, -2);
							auto* desc = PropertyRegister::getDescriptor(type_handle, crc32(property_name));
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
						break;
					}
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		LuaWrapper::pushLua(L, e);
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


	static void LUA_setEntityLocalRotation(IScene* hierarchy,
		Entity entity,
		Vec3 axis,
		float angle)
	{
		if (!isValid(entity)) return;

		static_cast<Hierarchy*>(hierarchy)->setLocalRotation(entity, Quat(axis, angle));
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

		LuaWrapper::pushLua(L, res);
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

		REGISTER_FUNCTION(createComponent);
		REGISTER_FUNCTION(createEntity);
		REGISTER_FUNCTION(setEntityPosition);
		REGISTER_FUNCTION(getEntityPosition);
		REGISTER_FUNCTION(getEntityDirection);
		REGISTER_FUNCTION(setEntityRotation);
		REGISTER_FUNCTION(getEntityRotation);
		REGISTER_FUNCTION(setEntityLocalRotation);
		REGISTER_FUNCTION(getInputActionValue);
		REGISTER_FUNCTION(addInputAction);
		REGISTER_FUNCTION(logError);
		REGISTER_FUNCTION(logInfo);
		REGISTER_FUNCTION(startGame);
		REGISTER_FUNCTION(hasFilesystemWork);
		REGISTER_FUNCTION(processFilesystemWork);
		REGISTER_FUNCTION(destroyEntity);
		REGISTER_FUNCTION(preloadPrefab);
		REGISTER_FUNCTION(unloadPrefab);
		REGISTER_FUNCTION(getComponent);
		REGISTER_FUNCTION(getComponentType);

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
		PropertyRegister::add(
			"hierarchy",
			LUMIX_NEW(m_allocator, EntityPropertyDescriptor<Hierarchy>)(
				"Parent", &Hierarchy::getParent, &Hierarchy::setParent, m_allocator));
		PropertyRegister::add("hierarchy",
			LUMIX_NEW(m_allocator, SimplePropertyDescriptor<Vec3, Hierarchy>)("Relative position",
								  &Hierarchy::getLocalPosition,
								  &Hierarchy::setLocalPosition,
								  m_allocator));
	}


	~EngineImpl()
	{
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
	}


	void stopGame(Universe& context) override
	{
		ASSERT(m_is_game_running);
		m_is_game_running = false;
		for (auto* scene : context.getScenes())
		{
			scene->stopGame();
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
		m_time_multiplier = multiplier;
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


	static void LUA_unloadPrefab(EngineImpl* engine, PrefabResource* prefab)
	{
		engine->m_prefab_resource_manager.unload(*prefab);
	}


	static PrefabResource* LUA_preloadPrefab(EngineImpl* engine, const char* path)
	{
		return static_cast<PrefabResource*>(engine->m_prefab_resource_manager.load(Path(path)));
	}


	ComponentUID createComponent(Universe& universe, Entity entity, ComponentType type)
	{
		const Array<IScene*>& scenes = universe.getScenes();
		ComponentUID cmp;
		for (int i = 0; i < scenes.size(); ++i)
		{
			cmp = ComponentUID(entity, type, scenes[i], scenes[i]->createComponent(type, entity));

			if (cmp.isValid())
			{
				return cmp;
			}
		}
		return ComponentUID::INVALID;
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
		auto* prefab = LuaWrapper::checkArg<PrefabResource*>(L, 4);
		ASSERT(prefab->isReady());
		if (!prefab->isReady())
		{
			g_log_error.log("Editor") << "Prefab " << prefab->getPath().c_str() << " is not ready, preload it.";
			return 0;
		}
		InputBlob blob(prefab->blob.getData(), prefab->blob.getPos());
		Array<Entity> entities(engine->m_allocator);
		engine->pasteEntities(position, *universe, blob, entities);

		lua_createtable(L, entities.size(), 0);
		for (int i = 0; i < entities.size(); ++i)
		{
			LuaWrapper::pushLua(L, entities[i]);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
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
	Debug::Allocator m_allocator;

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
