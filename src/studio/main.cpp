#include "mainwindow.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "debug/floating_points.h"
#include "editor/world_editor.h"
#include "editor/gizmo.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "fps_limiter.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"
#include "sceneview.h"
#include "gameview.h"
#include "wgl_render_device.h"
#include <QApplication>
#include <qdir.h>
#include <Windows.h>

class App
{
	public:
		App()
		{
			#ifdef _DEBUG
				Lumix::enableFloatingPointTraps(true);
			#endif
			m_qt_app = NULL;
			m_main_window = NULL;
			m_world_editor = NULL;
		}

		~App()
		{
			delete m_main_window;
			delete m_qt_app;
			Lumix::WorldEditor::destroy(m_world_editor);
		}
		

		void renderPhysics()
		{
			Lumix::RenderScene* render_scene = (Lumix::RenderScene*)m_world_editor->getEngine().getScene(crc32("renderer"));
			Lumix::PhysicsScene* scene = static_cast<Lumix::PhysicsScene*>(m_world_editor->getEngine().getScene(crc32("physics")));
			if (scene && render_scene)
			{
				scene->render(*render_scene);
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

			HWND hwnd = (HWND)m_main_window->getSceneView()->getViewWidget()->winId();
			Lumix::Renderer::init(hwnd);
			m_world_editor = Lumix::WorldEditor::create(QDir::currentPath().toLocal8Bit().data(), m_allocator);
			ASSERT(m_world_editor);
			m_world_editor->update();
			m_world_editor->updateEngine(-1, 1);

			m_main_window->setWorldEditor(*m_world_editor);

			m_main_window->getSceneView()->getPipeline()->addCustomCommandHandler("render_physics").bind<App, &App::renderPhysics>(this);
			m_main_window->getSceneView()->getPipeline()->addCustomCommandHandler("render_gizmos").bind<App, &App::renderGizmos>(this);

			auto command_line_arguments = m_qt_app->arguments();
			auto index_of_run_test = command_line_arguments.indexOf("-run_test");
			if (index_of_run_test >= 0 && index_of_run_test + 2 < command_line_arguments.size())
			{
				auto undo_stack_path = command_line_arguments[index_of_run_test + 1].toLatin1();
				auto result_universe_path = command_line_arguments[index_of_run_test + 2].toLatin1();
				m_world_editor->runTest(Lumix::Path(undo_stack_path.data()), Lumix::Path(result_universe_path.data()));
			}

		}


		void shutdown()
		{
		}


		void renderGizmos()
		{
			m_world_editor->renderIcons(*m_main_window->getSceneView()->getPipeline());
			m_world_editor->getGizmo().updateScale(m_world_editor->getEditCamera());
			m_world_editor->getGizmo().render(*m_main_window->getSceneView()->getPipeline());
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
				if(keys[VK_CONTROL] >> 7 == 0)
				{
					float speed = m_main_window->getSceneView()->getNavigationSpeed();
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

					Lumix::Renderer::frame();

					m_world_editor->update();
					if (m_main_window->getSceneView()->isFrameDebuggerActive())
					{
						if (m_main_window->getSceneView()->isFrameRequested())
						{
							m_world_editor->updateEngine(1.0f / 30.0f, m_main_window->getSceneView()->getTimeDeltaMultiplier());
							m_main_window->getSceneView()->frameServed();
						}
					}
					else
					{
						m_world_editor->updateEngine(-1, m_main_window->getSceneView()->getTimeDeltaMultiplier());
					}
					handleEvents();

					fps_limiter->endFrame();
				}
				Lumix::g_profiler.frame();
			}

			FPSLimiter::destroy(fps_limiter);
		}

	private:
		Lumix::DefaultAllocator m_allocator;
		MainWindow* m_main_window;
		Lumix::WorldEditor* m_world_editor;
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
