#include "mainwindow.h"
#include <QApplication>
#include <qdir.h>
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include "editor/gizmo.h"
#include "engine/engine.h"
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "sceneview.h"
#include "gameview.h"


/// TODO refactor this
class MyRenderDevice : public Lux::IRenderDevice
{
public:
	MyRenderDevice(Lux::Engine& engine, const char* pipeline_path)
	{
		Lux::Pipeline* pipeline_object = static_cast<Lux::Pipeline*>(engine.getResourceManager().get(Lux::ResourceManager::PIPELINE)->load(pipeline_path));
		ASSERT(pipeline_object);
		if(pipeline_object)
		{
			m_pipeline = Lux::PipelineInstance::create(*pipeline_object);
			m_pipeline->setRenderer(engine.getRenderer());
		}

	}
	
	virtual void beginFrame() override
	{
		wglMakeCurrent(m_hdc, m_opengl_context);
	}

	virtual void endFrame() override
	{
		wglSwapLayerBuffers(m_hdc, WGL_SWAP_MAIN_PLANE);
	}

	virtual Lux::PipelineInstance& getPipeline()
	{
		return *m_pipeline;
	}

	Lux::PipelineInstance* m_pipeline;
	HDC m_hdc;
	HGLRC m_opengl_context;
};

class App
{
	public:
		App()
		{
			m_game_render_device = NULL;
			m_edit_render_device = NULL;
			m_qt_app = NULL;
			m_main_window = NULL;
		}

		~App()
		{
			delete m_main_window;
			delete m_qt_app;
			m_client.destroy();
			m_server.destroy();
		}

		void onUniverseCreated(Lux::Event&)
		{
			m_edit_render_device->getPipeline().setScene(m_server.getEngine().getRenderScene()); 
			m_game_render_device->getPipeline().setScene(m_server.getEngine().getRenderScene()); 
		}

		void onUniverseDestroyed(Lux::Event&)
		{
			m_edit_render_device->getPipeline().setScene(NULL); 
			m_game_render_device->getPipeline().setScene(NULL); 
			
		}

		void init(int argc, char* argv[])
		{
			m_qt_app = new QApplication(argc, argv);
			QFile file("editor/stylesheet.qss");
			file.open(QFile::ReadOnly);
			m_qt_app->setStyleSheet(QLatin1String(file.readAll()));

			m_main_window = new MainWindow();
			m_main_window->show();

			HWND hwnd = (HWND)m_main_window->getSceneView()->widget()->winId();
			HWND game_hwnd = (HWND)m_main_window->getGameView()->getContentWidget()->winId();
			bool server_created = m_server.create(hwnd, game_hwnd, QDir::currentPath().toLocal8Bit().data());
			ASSERT(server_created);
			m_server.tick();
			m_client.create(m_server.getEngine().getBasePath());

			m_main_window->setEditorClient(m_client);
			m_main_window->setEditorServer(m_server);
			m_main_window->getSceneView()->setServer(&m_server);

			m_edit_render_device = new MyRenderDevice(m_server.getEngine(), "pipelines/main.json");
			m_edit_render_device->m_hdc = GetDC(hwnd);
			m_edit_render_device->m_opengl_context = m_server.getHGLRC();
			m_edit_render_device->getPipeline().setScene(m_server.getEngine().getRenderScene()); /// TODO manage scene properly
			m_server.setEditViewRenderDevice(*m_edit_render_device);

			m_game_render_device = new	MyRenderDevice(m_server.getEngine(), "pipelines/game_view.json");
			m_game_render_device->m_hdc = GetDC(game_hwnd);
			m_game_render_device->m_opengl_context = m_server.getHGLRC();
			m_game_render_device->getPipeline().setScene(m_server.getEngine().getRenderScene()); /// TODO manage scene properly
			m_server.getEngine().getRenderer().setRenderDevice(*m_game_render_device);

			m_server.getEngine().getEventManager().addListener(Lux::Engine::UniverseCreatedEvent::s_type).bind<App, &App::onUniverseCreated>(this);
			m_server.getEngine().getEventManager().addListener(Lux::Engine::UniverseDestroyedEvent::s_type).bind<App, &App::onUniverseDestroyed>(this);

			m_main_window->getSceneView()->setPipeline(m_edit_render_device->getPipeline());
			m_main_window->getGameView()->setPipeline(m_game_render_device->getPipeline());
		}

		void renderEditView()
		{
			m_edit_render_device->beginFrame();
			m_server.render(*m_edit_render_device);
			m_server.renderIcons(*m_edit_render_device);
			m_server.getGizmo().updateScale(m_server.getEditCamera());
			m_server.getGizmo().render(m_server.getEngine().getRenderer(), *m_edit_render_device);
			m_edit_render_device->endFrame();
		}

		void handleEvents()
		{
			m_client.processMessages();
			m_qt_app->processEvents();
			BYTE keys[256];
			GetKeyboardState(keys);
			if (m_main_window->getSceneView()->hasFocus())
			{
				/// TODO refactor
				if(keys[VK_CONTROL] >> 7 == 0)
				{
					int speed = 0;
					if (keys[VK_LSHIFT] >> 7)
					{
						speed = 1;
					}
					if (keys['W'] >> 7)
					{
						m_client.navigate(1, 0, speed);
					}
					else if (keys['S'] >> 7)
					{
						m_client.navigate(-1, 0, speed);
					}
					if (keys['A'] >> 7)
					{
						m_client.navigate(0, -1, speed);
					}
					else if (keys['D'] >> 7)
					{
						m_client.navigate(0, 1, speed);
					}
				}
			}
		}

		void run()
		{
			while (m_main_window->isVisible())
			{
				renderEditView();
				m_server.getEngine().getRenderer().renderGame();
				m_server.tick();
				handleEvents();
			}
		}

	private:
		MyRenderDevice* m_edit_render_device;
		MyRenderDevice* m_game_render_device;
		MainWindow* m_main_window;
		Lux::EditorServer m_server;
		Lux::EditorClient m_client;
		QApplication* m_qt_app;
};

int main(int argc, char* argv[])
{
	App app;
	app.init(argc, argv);
	app.run();
	return 0;
}
