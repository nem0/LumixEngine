#include "mainwindow.h"
#include "core/crc32.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/fs/tcp_file_device.h"
#include "core/fs/tcp_file_server.h"
#include "core/library.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "debug/allocator.h"
#include "debug/floating_points.h"
#include "editor/world_editor.h"
#include "editor/gizmo.h"
#include "engine.h"
#include "plugin_manager.h"
#include "fps_limiter.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"
#include "sceneview.h"
#include "gameview.h"
#include <QApplication>
#include <qdir.h>
#include <Windows.h>


class App
{
public:
	App()
		: m_allocator(m_main_allocator)
	{
#ifdef _DEBUG
		Lumix::enableFloatingPointTraps(true);
#endif
		m_qt_app = nullptr;
		m_main_window = nullptr;
		m_world_editor = nullptr;
		m_file_system = nullptr;
		m_memory_device = nullptr;
		m_tcp_device = nullptr;
		m_disk_device = nullptr;
		m_tcp_file_server = nullptr;
	}

	~App()
	{
		ASSERT(!m_tcp_file_server);
		ASSERT(!m_file_system);
		ASSERT(!m_world_editor);
		ASSERT(!m_main_window);
		ASSERT(!m_qt_app);
		ASSERT(!m_engine);
	}


	void renderPhysics()
	{
		Lumix::RenderScene* render_scene =
			(Lumix::RenderScene*)m_world_editor->getScene(
				Lumix::crc32("renderer"));
		Lumix::PhysicsScene* scene = static_cast<Lumix::PhysicsScene*>(
			m_world_editor->getScene(Lumix::crc32("physics")));
		if (scene && render_scene)
		{
			scene->render(*render_scene);
		}
	}


	void initFilesystem(bool is_network)
	{
		m_file_system = Lumix::FS::FileSystem::create(m_allocator);
		m_memory_device =
			m_allocator.newObject<Lumix::FS::MemoryFileDevice>(m_allocator);
		m_file_system->mount(m_memory_device);
		m_disk_device =
			m_allocator.newObject<Lumix::FS::DiskFileDevice>(m_allocator);
		m_file_system->mount(m_disk_device);

		if (is_network)
		{
			m_tcp_file_server =
				m_allocator.newObject<Lumix::FS::TCPFileServer>();
			m_tcp_file_server->start(QDir::currentPath().toLocal8Bit().data(),
									 m_allocator);

			m_tcp_device = m_allocator.newObject<Lumix::FS::TCPFileDevice>();
			m_tcp_device->connect("127.0.0.1", 10001, m_allocator);

			m_file_system->mount(m_memory_device);
			m_file_system->mount(m_tcp_device);
			m_file_system->setDefaultDevice("memory:tcp");
			m_file_system->setSaveGameDevice("memory:tcp");
		}
		else
		{
			m_file_system->setDefaultDevice("memory:disk");
			m_file_system->setSaveGameDevice("memory:disk");
		}
	}


	void checkTests()
	{
		auto command_line_arguments = m_qt_app->arguments();
		auto index_of_run_test = command_line_arguments.indexOf("-run_test");
		if (index_of_run_test >= 0 &&
			index_of_run_test + 2 < command_line_arguments.size())
		{
			auto undo_stack_path =
				command_line_arguments[index_of_run_test + 1].toLatin1();
			auto result_universe_path =
				command_line_arguments[index_of_run_test + 2].toLatin1();
			m_world_editor->runTest(Lumix::Path(undo_stack_path.data()),
				Lumix::Path(result_universe_path.data()));
		}
	}


	void initEditorPlugins()
	{
		auto& libraries = m_engine->getPluginManager().getLibraries();
		for (auto* lib : libraries)
		{
			typedef void(*Setter)(Lumix::Engine&, MainWindow&);
			Setter setter = (Setter)lib->resolve("setStudioMainWindow");
			if (setter)
			{
				setter(*m_engine, *m_main_window);
			}
		}
	}


	void init(int argc, char* argv[])
	{
		m_qt_app = new QApplication(argc, argv);
		QFile file("editor/stylesheet.qss");
		file.open(QFile::ReadOnly);
		m_qt_app->setStyleSheet(QLatin1String(file.readAll()));

		m_main_window = new MainWindow();
		m_main_window->show();

		SceneView* scene_view =m_main_window->getSceneView();
		HWND hwnd = (HWND)scene_view->getViewWidget()->winId();
		initFilesystem(true);
		m_engine = Lumix::Engine::create(hwnd, m_file_system, m_allocator);
		m_world_editor = Lumix::WorldEditor::create(
			QDir::currentPath().toLocal8Bit().data(), *m_engine);
		
		Lumix::UniverseContext ctx(m_allocator);
		m_engine->update(ctx, 1, -1);
		m_main_window->setWorldEditor(*m_world_editor);

		Lumix::PipelineInstance* pipeline = scene_view->getPipeline();
		pipeline->addCustomCommandHandler("render_physics")
			.bind<App, &App::renderPhysics>(this);
		pipeline->addCustomCommandHandler("render_gizmos")
			.bind<App, &App::renderGizmos>(this);

		checkTests();
		initEditorPlugins();
	}


	void shutdown()
	{
		m_main_window->shutdown();
		Lumix::WorldEditor::destroy(m_world_editor);
		Lumix::Engine::destroy(m_engine);
		m_main_window->deleteLater();
		m_qt_app->processEvents();
		delete m_qt_app;
		m_engine = nullptr;
		m_main_window = nullptr;
		m_qt_app = nullptr;
		m_world_editor = nullptr;
		m_allocator.deleteObject(m_memory_device);
		m_memory_device = nullptr;
		m_allocator.deleteObject(m_disk_device);
		m_disk_device = nullptr;
		if (m_tcp_device)
		{
			m_tcp_device->disconnect();
			m_allocator.deleteObject(m_tcp_device);
			m_tcp_device = nullptr;
		}
		if (m_tcp_file_server)
		{
			m_tcp_file_server->stop();
			m_allocator.deleteObject(m_tcp_file_server);
			m_tcp_file_server = nullptr;
		}
		Lumix::FS::FileSystem::destroy(m_file_system);
		m_file_system = nullptr;
	}


	void renderGizmos()
	{
		m_world_editor->renderIcons(
			*m_main_window->getSceneView()->getPipeline());
		m_world_editor->getGizmo().updateScale(
			m_world_editor->getEditCamera().index);
		m_world_editor->getGizmo().render(
			*m_main_window->getSceneView()->getPipeline());
	}


	void handleEvents()
	{
		PROFILE_FUNCTION();
		{
			PROFILE_BLOCK("qt::processEvents");
			m_qt_app->processEvents();
		}
		BYTE keys[256];
		GetKeyboardState(keys);
		if (m_main_window->getSceneView()->getViewWidget()->hasFocus())
		{
			if (keys[VK_CONTROL] >> 7 == 0)
			{
				float speed =
					m_main_window->getSceneView()->getNavigationSpeed();
				if (keys[VK_LSHIFT] >> 7)
				{
					speed *= 10;
				}
				if (keys['W'] >> 7)
				{
					m_world_editor->navigate(1, 0, speed);
				}
				else if (keys['S'] >> 7)
				{
					m_world_editor->navigate(-1, 0, speed);
				}
				if (keys['A'] >> 7)
				{
					m_world_editor->navigate(0, -1, speed);
				}
				else if (keys['D'] >> 7)
				{
					m_world_editor->navigate(0, 1, speed);
				}
			}
		}
	}


	void run()
	{
		FPSLimiter* fps_limiter = FPSLimiter::create(60, m_allocator);

		while (m_main_window->isVisible())
		{
			{
				PROFILE_BLOCK("tick");
				fps_limiter->beginFrame();

				m_main_window->update();

				m_main_window->getSceneView()->render();
				m_main_window->getGameView()->render();

				m_engine->getRenderer().frame();

				m_world_editor->update();
				if (m_main_window->getSceneView()->isFrameDebuggerActive())
				{
					if (m_main_window->getSceneView()->isFrameRequested())
					{
						m_world_editor->updateEngine(
							1.0f / 30.0f,
							m_main_window->getSceneView()
								->getTimeDeltaMultiplier());
						m_main_window->getSceneView()->frameServed();
					}
				}
				else
				{
					m_world_editor->updateEngine(
						-1,
						m_main_window->getSceneView()
							->getTimeDeltaMultiplier());
				}
				handleEvents();

				fps_limiter->endFrame();
			}
			Lumix::g_profiler.frame();
		}

		FPSLimiter::destroy(fps_limiter);
	}

private:
	Lumix::DefaultAllocator m_main_allocator;
	Lumix::Debug::Allocator m_allocator;
	MainWindow* m_main_window;
	Lumix::WorldEditor* m_world_editor;
	Lumix::Engine* m_engine;
	Lumix::FS::FileSystem* m_file_system;
	Lumix::FS::MemoryFileDevice* m_memory_device;
	Lumix::FS::TCPFileDevice* m_tcp_device;
	Lumix::FS::DiskFileDevice* m_disk_device;
	Lumix::FS::TCPFileServer* m_tcp_file_server;
	QApplication* m_qt_app;
};

int main(int argc, char* argv[])
{
	App app;
	QCoreApplication::addLibraryPath(QDir::currentPath());
	QCoreApplication::addLibraryPath(QDir::currentPath() + "/bin");
	app.init(argc, argv);
	app.run();
	app.shutdown();
	return 0;
}
