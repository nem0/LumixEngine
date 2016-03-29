#include "engine.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/os_file.h"
#include "core/input_system.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/math_utils.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/timer.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/mtjd/manager.h"
#include "debug/debug.h"
#include "engine/iplugin.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "plugin_manager.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"
#include <lua.hpp>


namespace Lumix
{

static const uint32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'
static const uint32 HIERARCHY_HASH = crc32("hierarchy");


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


class EngineImpl : public Engine
{
public:
	EngineImpl(const char* base_path0, const char* base_path1, FS::FileSystem* fs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_resource_manager(m_allocator)
		, m_mtjd_manager(nullptr)
		, m_fps(0)
		, m_is_game_running(false)
		, m_component_types(m_allocator)
		, m_last_time_delta(0)
		, m_path_manager(m_allocator)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
	{
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
			bool is_patching = base_path1[0] != 0 && compareString(base_path0, base_path1) != 0;
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

		m_timer = Timer::create(m_allocator);
		m_fps_timer = Timer::create(m_allocator);
		m_fps_frame = 0;
		PropertyRegister::init(m_allocator);
	}


	static ComponentIndex LUA_createComponent(IScene* scene, const char* type, int entity_idx)
	{
		if (!scene) return -1;
		Entity e(entity_idx);
		uint32 hash = crc32(type);
		if (scene->getComponent(e, hash) != INVALID_COMPONENT)
		{
			g_log_error.log("Lua Script") << "Component " << type << " already exists in entity "
				<< entity_idx;
			return -1;
		}

		return scene->createComponent(hash, e);
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
				desc.set(cmp, -1, InputBlob(str, stringLength(str)));
			}
			break;
		case IPropertyDescriptor::DECIMAL:
			if (lua_isnumber(L, -1))
			{
				float f = (float)lua_tonumber(L, -1);
				desc.set(cmp, -1, InputBlob(&f, sizeof(f)));
			}
			break;
		case IPropertyDescriptor::BOOL:
			if (lua_isboolean(L, -1))
			{
				bool b = lua_toboolean(L, -1) != 0;
				desc.set(cmp, -1, InputBlob(&b, sizeof(b)));
			}
			break;
		case IPropertyDescriptor::VEC3:
		case IPropertyDescriptor::COLOR:
			if (lua_istable(L, -1))
			{
				auto v = LuaWrapper::toType<Vec3>(L, -1);
				desc.set(cmp, -1, InputBlob(&v, sizeof(v)));
			}
			break;
		default:
			g_log_error.log("Lua Script") << "Property " << desc.getName() << " has unsupported type";
			break;
		}
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
			if (compareString(parameter_name, "position") == 0)
			{
				auto pos = LuaWrapper::toType<Vec3>(L, -1);
				ctx->setPosition(e, pos);
			}
			else
			{
				uint32 cmp_hash = crc32(parameter_name);
				for (auto* scene : ctx->getScenes())
				{
					ComponentUID cmp(e, cmp_hash, scene, scene->createComponent(cmp_hash, e));
					if (cmp.isValid())
					{
						lua_pushvalue(L, -1);
						lua_pushnil(L);
						while (lua_next(L, -2) != 0)
						{
							const char* property_name = luaL_checkstring(L, -2);
							auto* desc = PropertyRegister::getDescriptor(cmp_hash, crc32(property_name));
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

		univ->setRotation(entity_index, Quat(axis, angle));
	}


	static void LUA_setEntityLocalRotation(IScene* hierarchy,
		Entity entity,
		Vec3 axis,
		float angle)
	{
		if (entity == INVALID_ENTITY) return;

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
		Vec3 axis = LuaWrapper::checkArg<Vec3>(L, 2);
		float angle = LuaWrapper::checkArg<float>(L, 3);

		Quat q(axis, angle);
		Vec3 res = q * v;

		LuaWrapper::pushLua(L, res);
		return 1;
	}


	static Vec3 LUA_getEntityPosition(Universe* universe, Entity entity)
	{
		if (entity == INVALID_ENTITY)
		{
			g_log_warning.log("Engine") << "Requesting position on invalid entity";
			return Vec3(0, 0, 0);
		}
		return universe->getPosition(entity);
	}


	static Vec3 LUA_getEntityDirection(Universe* universe, Entity entity)
	{
		Quat rot = universe->getRotation(entity);
		return rot * Vec3(0, 0, 1);
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
		REGISTER_FUNCTION(setEntityLocalRotation);
		REGISTER_FUNCTION(getInputActionValue);
		REGISTER_FUNCTION(addInputAction);
		REGISTER_FUNCTION(logError);
		REGISTER_FUNCTION(logInfo);


		#undef REGISTER_FUNCTION

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
		auto x = lua_getglobal(m_state, "package");
		auto y = lua_getfield(m_state, -1, "searchers");
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
		PropertyRegister::registerComponentType("hierarchy", "Hierarchy");
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


	bool create()
	{
		m_plugin_manager = PluginManager::create(*this);
		if (!m_plugin_manager)
		{
			return false;
		}

		HierarchyPlugin* hierarchy = LUMIX_NEW(m_allocator, HierarchyPlugin)(m_allocator);
		m_plugin_manager->addPlugin(hierarchy);

		m_input_system = InputSystem::create(m_allocator);
		if (!m_input_system)
		{
			return false;
		}

		registerProperties();

		return true;
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


	lua_State* getState() override { return m_state; }
	PathManager& getPathManager() override{ return m_path_manager; }
	float getLastTimeDelta() override { return m_last_time_delta; }

private:
	struct ComponentType
	{
		explicit ComponentType(IAllocator& allocator)
			: m_name(allocator)
			, m_id(allocator)
		{
		}

		string m_name;
		string m_id;

		uint32 m_id_hash;
		uint32 m_dependency;
	};

private:
	Debug::Allocator m_allocator;

	FS::FileSystem* m_file_system;
	FS::MemoryFileDevice* m_mem_file_device;
	FS::DiskFileDevice* m_disk_file_device;
	FS::DiskFileDevice* m_patch_file_device;

	ResourceManager m_resource_manager;
	
	MTJD::Manager* m_mtjd_manager;

	Array<ComponentType> m_component_types;
	PluginManager* m_plugin_manager;
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


static void showLogInVS(const char* system, const char* message)
{
	Debug::debugOutput(system);
	Debug::debugOutput(" : ");
	Debug::debugOutput(message);
	Debug::debugOutput("\n");
}


static FS::OsFile g_error_file;
static bool g_is_error_file_opened = false;


static void logErrorToFile(const char*, const char* message)
{
	if (!g_is_error_file_opened) return;
	g_error_file.write(message, stringLength(message));
	g_error_file.flush();
}


Engine* Engine::create(const char* base_path0,
	const char* base_path1,
	FS::FileSystem* fs,
	IAllocator& allocator)
{
	g_log_info.log("Core") << "Creating engine...";
	Profiler::setThreadName("Main");
	installUnhandledExceptionHandler();

	g_is_error_file_opened = g_error_file.open("error.log", FS::Mode::CREATE_AND_WRITE, allocator);

	g_log_error.getCallback().bind<logErrorToFile>();
	g_log_info.getCallback().bind<showLogInVS>();
	g_log_warning.getCallback().bind<showLogInVS>();
	g_log_error.getCallback().bind<showLogInVS>();

	EngineImpl* engine = LUMIX_NEW(allocator, EngineImpl)(base_path0, base_path1, fs, allocator);
	if (!engine->create())
	{
		g_log_error.log("Core") << "Failed to create engine.";
		LUMIX_DELETE(allocator, engine);
		return nullptr;
	}
	g_log_info.log("Core") << "Engine created.";
	return engine;
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, engine);

	g_error_file.close();
}


} // ~namespace Lumix
