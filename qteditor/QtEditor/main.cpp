#include "mainwindow.h"
#include <QApplication>
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "engine/engine.h"
#include <qdir.h>
#include "sceneview.h"


class MyRenderDevice : public Lux::IRenderDevice
{
	public:
	MyRenderDevice(Lux::Renderer& renderer)
	{
		m_renderer = &renderer;
	}

	virtual void endFrame()
	{
		wglSwapLayerBuffers(m_hdc, WGL_SWAP_MAIN_PLANE);
	}

	virtual Lux::Pipeline& getPipeline()
	{
		static Lux::Pipeline* p = m_renderer->loadPipeline("pipelines/main.json");
		return *p;
	}

	HDC m_hdc;
	Lux::Renderer* m_renderer;
};

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	MainWindow w;
	w.show();
	HWND hwnd = (HWND)w.getSceneView()->winId();
	Lux::EditorServer server;
	Lux::EditorClient client;
	server.create(hwnd, NULL, QDir::currentPath().toLocal8Bit().data());
	server.tick(hwnd, NULL);
	client.create(server.getEngine().getBasePath());
	w.setEditorClient(client);
	w.getSceneView()->setServer(&server);
	MyRenderDevice rd(server.getEngine().getRenderer());
	rd.m_hdc = GetDC(hwnd);
	while (w.isVisible())
	{
		rd.getPipeline().setCamera(0, server.getCamera()); TODO("when universe is loaded, old camera is destroyed, handle this in a normal way");
		w.getSceneView()->setPipeline(rd.getPipeline());
		server.render(rd);
		server.tick(hwnd, NULL);
		client.processMessages();
		a.processEvents();
		BYTE keys[256];
		GetKeyboardState(keys);
		if (w.getSceneView()->hasFocus())
		{
			if (keys['W'] >> 7)
			{
				client.navigate(1, 0, 0);
			}
			else if (keys['S'] >> 7)
			{
				client.navigate(-1, 0, 0);
			}
		}
		rd.endFrame();
	}
	return 0;
}
