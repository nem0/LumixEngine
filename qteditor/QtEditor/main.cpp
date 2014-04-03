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

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	QFile file("editor/stylesheet.qss");
	file.open(QFile::ReadOnly);
	a.setStyleSheet(QLatin1String(file.readAll()));
	MainWindow w;
	w.show();
	HWND hwnd = (HWND)w.getSceneView()->widget()->winId();
	HWND game_hwnd = (HWND)w.getGameView()->getContentWidget()->winId();
	Lux::EditorServer server;
	Lux::EditorClient client;
	server.create(hwnd, game_hwnd, QDir::currentPath().toLocal8Bit().data());
	server.tick(hwnd, NULL);
	client.create(server.getEngine().getBasePath());
	w.setEditorClient(client);
	w.getSceneView()->setServer(&server);
	MyRenderDevice rd(server.getEngine().getRenderer(), "pipelines/main.json");
	MyRenderDevice rd2(server.getEngine().getRenderer(), "pipelines/game_view.json");
	rd.m_hdc = GetDC(hwnd);
	rd2.m_hdc = GetDC(game_hwnd);
	while (w.isVisible())
	{
		rd.getPipeline().setCamera(0, server.getCamera(0)); TODO("when universe is loaded, old camera is destroyed, handle this in a normal way");
		rd2.getPipeline().setCamera(0, server.getCamera(1)); TODO("when universe is loaded, old camera is destroyed, handle this in a normal way");
		w.getSceneView()->setPipeline(rd.getPipeline());
		w.getGameView()->setPipeline(rd2.getPipeline());
		wglMakeCurrent(rd.m_hdc, server.getHGLRC());
		server.render(rd);
		server.getGizmo().updateScale(server.getCamera(0));
		rd.endFrame();
		wglMakeCurrent(rd2.m_hdc, server.getHGLRC());
		server.render(rd2);
		rd2.endFrame();
		server.tick(hwnd, NULL);
		client.processMessages();
		a.processEvents();
		BYTE keys[256];
		GetKeyboardState(keys);
		if (w.getSceneView()->hasFocus())
		{
			 /// TODO refactor
			if (keys['W'] >> 7)
			{
				client.navigate(1, 0, 0);
			}
			else if (keys['S'] >> 7)
			{
				client.navigate(-1, 0, 0);
			}
			if (keys['A'] >> 7)
			{
				client.navigate(0, -1, 0);
			}
			else if (keys['D'] >> 7)
			{
				client.navigate(0, 1, 0);
			}
		}
	}
	return 0;
}
