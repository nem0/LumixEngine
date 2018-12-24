#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/memory_file_device.h"
#include "engine/fs/pack_file_device.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/thread.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "gui/gui_system.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <cstdio>
#ifdef _WIN32
	#include <windows.h>
#endif


namespace Lumix
{


struct GUIInterface : GUISystem::Interface
{
	GUIInterface() = default;

	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0, 0); }
	Vec2 getSize() const override { return size; }

	void enableCursor(bool enable) override
	{
		OS::showCursor(enable);
	}


	Vec2 size;
	Pipeline* pipeline;
};


class Runner : public OS::Interface
{
public:
	Runner()
		: m_allocator(m_main_allocator)
		, m_window_mode(false)
		, m_universe(nullptr)
		, m_exit_code(0)
		, m_pipeline(nullptr)
	{
		m_frame_timer = Timer::create(m_allocator);
		ASSERT(!s_instance);
		s_instance = this;
	}


	~Runner()
	{
		Timer::destroy(m_frame_timer);
		ASSERT(!m_universe);
		s_instance = nullptr;
	}


	void onResize() const
	{
		const OS::Point p = OS::getWindowClientSize(m_window);
		// TODO
		//m_pipeline->resize(p.x, p.y);
		m_gui_interface->size.set((float)p.x, (float)p.y);
		Renderer* renderer = (Renderer*)m_engine->getPluginManager().getPlugin("renderer");
		renderer->resize(p.x, p.y);
	}


	void onInit() override
	{
		copyString(m_pipeline_path, "pipelines/main.pln");
		m_pipeline_define = "APP";
		copyString(m_startup_script_path, "startup.lua");
		char cmd_line[1024];
		OS::getCommandLine(cmd_line, lengthOf(cmd_line));
		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-window"))
			{
				m_window_mode = true;
			}
			else if (parser.currentEquals("-pipeline"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_pipeline_path, lengthOf(m_pipeline_path));
			}
			else if (parser.currentEquals("-pipeline_define"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_pipeline_define.data, lengthOf(m_pipeline_define.data));
			}
			else if(parser.currentEquals("-script"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_startup_script_path, lengthOf(m_startup_script_path));
			}
		}

		g_log_info.getCallback().bind<outputToConsole>();
		g_log_warning.getCallback().bind<outputToConsole>();
		g_log_error.getCallback().bind<outputToConsole>();

		enableCrashReporting(false);

		m_file_system = FS::FileSystem::create(m_allocator);

		m_mem_file_device = LUMIX_NEW(m_allocator, FS::MemoryFileDevice)(m_allocator);
		char current_dir[MAX_PATH_LENGTH];
		#ifdef _WIN32
			GetCurrentDirectory(sizeof(current_dir), current_dir); 
		#else
			current_dir[0] = '\0';
		#endif
		m_disk_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)("disk", current_dir, m_allocator);
		m_pack_file_device = LUMIX_NEW(m_allocator, FS::PackFileDevice)(m_allocator);

		m_file_system->mount(m_mem_file_device);
		m_file_system->mount(m_disk_file_device);
		m_file_system->mount(m_pack_file_device);
		m_pack_file_device->mount("data.pak");
		m_file_system->setDefaultDevice("memory:disk:pack");
		m_file_system->setSaveGameDevice("memory:disk");

		m_engine = Engine::create(current_dir, m_file_system, m_allocator);
		ffr::preinit(m_allocator);
		
		OS::InitWindowArgs create_win_args = {};
		create_win_args.fullscreen = !m_window_mode;
		create_win_args.handle_file_drops = false;
		create_win_args.name = "Lumix App";
		m_window = OS::createWindow(create_win_args);

		Engine::PlatformData platform_data = {};
		platform_data.window_handle = m_window;
		m_engine->setPlatformData(platform_data);

		#ifdef LUMIXENGINE_PLUGINS
			const char* plugins[] = { LUMIXENGINE_PLUGINS };
			for (auto plugin : plugins)
			{
				m_engine->getPluginManager().load(plugin);
			}
		#endif
		m_engine->getInputSystem().enable(true);
		Renderer* renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = m_engine->getResourceManager().load<PipelineResource>(Path(m_pipeline_path));
		m_pipeline = Pipeline::create(*renderer, pres, m_pipeline_define, m_engine->getAllocator());
		renderer->setMainPipeline(m_pipeline);

		while (m_engine->getFileSystem().hasWork())
		{
			MT::sleep(100);
			m_engine->getFileSystem().updateAsyncTransactions();
		}

		m_universe = &m_engine->createUniverse(true);
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		// TODO
		//m_pipeline->resize(600, 400);
		renderer->resize(600, 400);

		registerLuaAPI();

		m_gui_interface = LUMIX_NEW(m_allocator, GUIInterface);
		auto* gui_system = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		m_gui_interface->pipeline = m_pipeline;

		gui_system->setInterface(m_gui_interface);
		
		OS::showCursor(false);
		onResize();

		if (!runStartupScript())
		{
			loadUniverse("universes/main.unv");
			while (m_engine->getFileSystem().hasWork()) m_engine->getFileSystem().updateAsyncTransactions();
			m_engine->startGame(*m_universe);
		}
	}


	bool runStartupScript() const
	{
		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(m_startup_script_path), FS::Mode::OPEN_AND_READ);
		if (file)
		{
			m_engine->runScript((const char*)file->getBuffer(), (int)file->size(), m_startup_script_path);
			fs.close(*file);
			return true;
		}
		return false;
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine->getState();

		#define REGISTER_FUNCTION(F) \
			do { \
				auto* f = &LuaWrapper::wrapMethodClosure<Runner, decltype(&Runner::F), &Runner::F>; \
				LuaWrapper::createSystemClosure(L, "App", this, #F, f); \
			} while(false) \

		REGISTER_FUNCTION(loadUniverse);
		REGISTER_FUNCTION(setUniverse);
		REGISTER_FUNCTION(exit);

		#undef REGISTER_FUNCTION

		LuaWrapper::createSystemVariable(L, "App", "universe", m_universe);
	}


	void universeFileLoaded(FS::IFile& file, bool success)
	{
		if (!success)
		{
			g_log_error.log("App") << "Failed to open universe.";
			return;
		}

		ASSERT(file.getBuffer());
		InputBlob blob(file.getBuffer(), (int)file.size());
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
		blob.read(header);
		if (crc32((const u8*)blob.getData() + sizeof(header), blob.getSize() - sizeof(header)) != header.hash)
		{
			g_log_error.log("App") << "Universe corrupted";
			return;
		}
		m_engine->destroyUniverse(*m_universe);
		m_universe = &m_engine->createUniverse(true);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, lengthOf(basename), m_universe_path);
		m_universe->setName(basename);
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		LuaWrapper::createSystemVariable(m_engine->getState(), "App", "universe", m_universe);
		bool deserialize_succeeded = m_engine->deserialize(*m_universe, blob);
		if (!deserialize_succeeded)
		{
			g_log_error.log("App") << "Failed to deserialize universe";
		}
	}


	void setUniverse(Universe* universe)
	{
		m_engine->destroyUniverse(*m_universe);
		m_universe = universe;
		m_universe->setName("runtime");
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		LuaWrapper::createSystemVariable(m_engine->getState(), "App", "universe", m_universe);
	}


	void loadUniverse(const char* path)
	{
		copyString(m_universe_path, path);
		auto& fs = m_engine->getFileSystem();
		FS::ReadCallback file_read_cb;
		file_read_cb.bind<Runner, &Runner::universeFileLoaded>(this);
		fs.openAsync(fs.getDefaultDevice(), Path(m_universe_path), FS::Mode::OPEN_AND_READ, file_read_cb);
	}


	void shutdown()
	{
		auto* gui_system = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		gui_system->setInterface(nullptr);
		LUMIX_DELETE(m_allocator, m_gui_interface);

		m_engine->destroyUniverse(*m_universe);
		FS::FileSystem::destroy(m_file_system);
		LUMIX_DELETE(m_allocator, m_disk_file_device);
		LUMIX_DELETE(m_allocator, m_mem_file_device);
		LUMIX_DELETE(m_allocator, m_pack_file_device);
		Pipeline::destroy(m_pipeline);
		Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_universe = nullptr;
	}


	int getExitCode() const { return m_exit_code; }


	void onEvent(const OS::Event& event) override
	{
		InputSystem& input = m_engine->getInputSystem();
		input.injectEvent(event);
		switch (event.type) {
			case OS::Event::Type::QUIT:
			case OS::Event::Type::WINDOW_CLOSE: 
				OS::quit();
				break;
			case OS::Event::Type::WINDOW_MOVE:
			case OS::Event::Type::WINDOW_SIZE:
				onResize();
				break;
			case OS::Event::Type::FOCUS: 
				m_engine->getInputSystem().enable(event.focus.gained); 
				break;
		}
	}


	static void outputToVS(const char* system, const char* message)
	{
		#ifdef _MSC_VER
			char tmp[2048];
			copyString(tmp, system);
			catString(tmp, " : ");
			catString(tmp, message);
			catString(tmp, "\r");

			OutputDebugString(tmp);
		#endif
	}


	static void outputToConsole(const char* system, const char* message) 
	{
		printf("%s: %s\n", system, message); 
	}


	void exit(int exit_code)
	{
		m_exit_code = exit_code;
		OS::quit();
	}


	void onIdle() override
	{
		float frame_time = m_frame_timer->tick();
		m_engine->update(*m_universe);
		m_pipeline->render();
		auto* renderer = m_engine->getPluginManager().getPlugin("renderer");
		static_cast<Renderer*>(renderer)->frame();
		m_engine->getFileSystem().updateAsyncTransactions();
		if (frame_time < 1 / 60.0f) {
			PROFILE_BLOCK("sleep");
			MT::sleep(u32(1000 / 60.0f - frame_time * 1000));
		}
	}


private:
	DefaultAllocator m_main_allocator;
	Debug::Allocator m_allocator;
	Engine* m_engine;
	char m_universe_path[MAX_PATH_LENGTH];
	Universe* m_universe;
	Pipeline* m_pipeline;
	FS::FileSystem* m_file_system;
	FS::MemoryFileDevice* m_mem_file_device;
	FS::DiskFileDevice* m_disk_file_device;
	FS::PackFileDevice* m_pack_file_device;
	Timer* m_frame_timer;
	GUIInterface* m_gui_interface;
	bool m_window_mode;
	int m_exit_code;
	char m_startup_script_path[MAX_PATH_LENGTH];
	char m_pipeline_path[MAX_PATH_LENGTH];
	StaticString<64> m_pipeline_define;
	OS::WindowHandle m_window;

	static Runner* s_instance;
};


Runner* Runner::s_instance = nullptr;


}

#ifdef _WIN32
INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
#else
int main(int args, char* argv[])
#endif
{
	Lumix::Runner app;
	Lumix::OS::run(app);
	app.shutdown();
	return app.getExitCode();
}
