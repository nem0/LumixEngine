#include "mainwindow.h"
#include <QApplication>
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include "editor/gizmo.h"
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "engine/engine.h"
#include <qdir.h>
#include "sceneview.h"
#include "gameview.h"


/// TODO refactor this
class MyRenderDevice : public Lux::IRenderDevice
{
public:
	explicit MyRenderDevice(Lux::Renderer& renderer, const char* pipeline)
	{
		m_renderer = &renderer;
		m_pipeline = Lux::PipelineInstance::create(*m_renderer->loadPipeline(pipeline));
		m_pipeline->setRenderer(renderer);
	}
	
	virtual void endFrame()
	{
		wglSwapLayerBuffers(m_hdc, WGL_SWAP_MAIN_PLANE);
	}

	virtual Lux::PipelineInstance& getPipeline()
	{
		return *m_pipeline;
	}

	Lux::PipelineInstance* m_pipeline;
	HDC m_hdc;
	Lux::Renderer* m_renderer;
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
			m_server.create(hwnd, game_hwnd, QDir::currentPath().toLocal8Bit().data());
			m_server.tick();
			m_client.create(m_server.getEngine().getBasePath());
			m_main_window->setEditorClient(m_client);
			m_main_window->setEditorServer(m_server);
			m_main_window->getSceneView()->setServer(&m_server);
			m_edit_render_device = new MyRenderDevice(m_server.getEngine().getRenderer(), "pipelines/main.json");
			m_game_render_device = new	MyRenderDevice(m_server.getEngine().getRenderer(), "pipelines/game_view.json");
			m_edit_render_device->m_hdc = GetDC(hwnd);
			m_game_render_device->m_hdc = GetDC(game_hwnd);
		}

		void run()
		{
			while (m_main_window->isVisible())
			{
				m_edit_render_device->getPipeline().setCamera(0, m_server.getCamera(0)); TODO("when universe is loaded, old camera is destroyed, handle this in a normal way");
				m_game_render_device->getPipeline().setCamera(0, m_server.getCamera(1)); TODO("when universe is loaded, old camera is destroyed, handle this in a normal way");
				m_main_window->getSceneView()->setPipeline(m_edit_render_device->getPipeline());
				m_main_window->getGameView()->setPipeline(m_game_render_device->getPipeline());
				wglMakeCurrent(m_edit_render_device->m_hdc, m_server.getHGLRC());
				m_server.render(*m_edit_render_device);
				m_server.renderIcons(*m_edit_render_device);
				m_server.getGizmo().updateScale(m_server.getCamera(0));
				m_server.getGizmo().render(m_server.getEngine().getRenderer());
				m_edit_render_device->endFrame();
				wglMakeCurrent(m_game_render_device->m_hdc, m_server.getHGLRC());
				m_server.render(*m_game_render_device);
				m_game_render_device->endFrame();
				m_server.tick();
				m_client.processMessages();
				m_qt_app->processEvents();
				BYTE keys[256];
				GetKeyboardState(keys);
				if (m_main_window->getSceneView()->hasFocus())
				{
					 /// TODO refactor
					if (keys['W'] >> 7)
					{
						m_client.navigate(1, 0, 0);
					}
					else if (keys['S'] >> 7)
					{
						m_client.navigate(-1, 0, 0);
					}
					if (keys['A'] >> 7)
					{
						m_client.navigate(0, -1, 0);
					}
					else if (keys['D'] >> 7)
					{
						m_client.navigate(0, 1, 0);
					}
				}
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
