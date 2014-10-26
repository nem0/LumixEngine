#include "mainwindow.h"
#include <QApplication>
#include <qdir.h>
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "editor/gizmo.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include <graphics/gl_ext.h>
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"
#include "sceneview.h"
#include "gameview.h"
#include "wgl_render_device.h"


class App
{
	public:
		App()
		{
			m_game_render_device = NULL;
			m_edit_render_device = NULL;
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

		void onUniverseCreated()
		{
			m_edit_render_device->getPipeline().setScene((Lumix::RenderScene*)m_world_editor->getEngine().getScene(crc32("renderer")));
			m_game_render_device->getPipeline().setScene((Lumix::RenderScene*)m_world_editor->getEngine().getScene(crc32("renderer")));
		}

		void onUniverseDestroyed()
		{
			if(m_edit_render_device)
			{
				m_edit_render_device->getPipeline().setScene(NULL); 
			}
			if(m_game_render_device)
			{
				m_game_render_device->getPipeline().setScene(NULL); 
			}
			
		}

		HGLRC createGLContext(HWND hwnd[], int count)
		{
			ASSERT(count > 0);
			QWidget* widget = new QWidget();
			HWND gl_hwnd = (HWND)widget->winId();
			HDC hdc;
			hdc = GetDC(gl_hwnd);
			ASSERT(hdc != NULL);
			if (hdc == NULL)
			{
				Lumix::g_log_error.log("renderer") << "Could not get the device context";
				return NULL;
			}
			BOOL success;
			PIXELFORMATDESCRIPTOR pfd =
			{
				sizeof(PIXELFORMATDESCRIPTOR),  //  size of this pfd  
				1,                     // version number  
				PFD_DRAW_TO_WINDOW |   // support window  
				PFD_SUPPORT_OPENGL |   // support OpenGL  
				PFD_DOUBLEBUFFER,      // double buffered  
				PFD_TYPE_RGBA,         // RGBA type  
				24,                    // 24-bit color depth  
				0, 0, 0, 0, 0, 0,      // color bits ignored  
				0,                     // no alpha buffer  
				0,                     // shift bit ignored  
				0,                     // no accumulation buffer  
				0, 0, 0, 0,            // accum bits ignored  
				32,                    // 32-bit z-buffer      
				0,                     // no stencil buffer  
				0,                     // no auxiliary buffer  
				PFD_MAIN_PLANE,        // main layer  
				0,                     // reserved  
				0, 0, 0                // layer masks ignored  
			};
			int pixelformat = ChoosePixelFormat(hdc, &pfd);
			if (pixelformat == 0)
			{
				delete widget;
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not choose a pixel format";
				return NULL;
			}
			success = SetPixelFormat(hdc, pixelformat, &pfd);
			if (success == FALSE)
			{
				delete widget;
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not set a pixel format";
				return NULL;
			}
			HGLRC hglrc = wglCreateContext(hdc);
			if (hglrc == NULL)
			{
				delete widget;
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not create an opengl context";
				return NULL;
			}
			success = wglMakeCurrent(hdc, hglrc);
			if (success == FALSE)
			{
				delete widget;
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not make the opengl context current rendering context";
				return NULL;
			}

			{
				UINT numFormats;
				int atrribs[] =
				{
					WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
					WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
					WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
					WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
					WGL_COLOR_BITS_ARB, 32,
					WGL_DEPTH_BITS_ARB, 24,
					WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
					WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
					WGL_SAMPLES_ARB, 4,
					0
				};
				PFNWGLCHOOSEPIXELFORMATARBPROC foo;
				foo = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
				if (!foo)
				{
					delete widget;
					ASSERT(false);
					Lumix::g_log_error.log("renderer") << "Could not get function wglChoosePixelFormatARB";
					return NULL;
				}
				success = foo(hdc, atrribs, NULL, 1, &pixelformat, &numFormats);
			}
			wglDeleteContext(hglrc);
			hglrc = NULL;
			delete widget;

			for (int i = 0; i < count; ++i)
			{
				if (hwnd[i])
				{
					hdc = GetDC(hwnd[i]);
					ChoosePixelFormat(hdc, &pfd);
					if (hdc == NULL)
					{
						ASSERT(false);
						Lumix::g_log_error.log("renderer") << "Could not get the device context";
						return NULL;
					}
					SetPixelFormat(hdc, pixelformat, &pfd);

					if (!hglrc)
					{
						hglrc = wglCreateContext(hdc);
						if (hglrc == NULL)
						{
							ASSERT(false);
							Lumix::g_log_error.log("renderer") << "Could not create an opengl context";
							return NULL;
						}
						success = wglMakeCurrent(hdc, hglrc);
						if (success == FALSE)
						{
							ASSERT(false);
							Lumix::g_log_error.log("renderer") << "Could not make the opengl context current rendering context";
							return NULL;
						}
					}
				}
			}


			return hglrc;
		}	


		void renderPhysics()
		{
			Lumix::PhysicsScene* scene = static_cast<Lumix::PhysicsScene*>(m_world_editor->getEngine().getScene(crc32("physics")));
			if(scene)
			{
				scene->render();
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
			HWND game_hwnd = (HWND)m_main_window->getGameView()->getContentWidget()->winId();
			HWND hwnds[] = { hwnd, game_hwnd };
			HGLRC hglrc = createGLContext(hwnds, 2);

			m_world_editor = Lumix::WorldEditor::create(QDir::currentPath().toLocal8Bit().data());
			ASSERT(m_world_editor);
			m_world_editor->tick();

			m_main_window->setWorldEditor(*m_world_editor);
			m_main_window->getSceneView()->setWorldEditor(m_world_editor);

			m_edit_render_device = new WGLRenderDevice(m_world_editor->getEngine(), "pipelines/main.json");
			m_edit_render_device->m_hdc = GetDC(hwnd);
			m_edit_render_device->m_opengl_context = hglrc;
			m_edit_render_device->getPipeline().setScene((Lumix::RenderScene*)m_world_editor->getEngine().getScene(crc32("renderer")));
			m_world_editor->setEditViewRenderDevice(*m_edit_render_device);
			m_edit_render_device->getPipeline().addCustomCommandHandler("render_physics").bind<App, &App::renderPhysics>(this);
			m_edit_render_device->getPipeline().addCustomCommandHandler("render_gizmos").bind<App, &App::renderGizmos>(this);

			m_game_render_device = new	WGLRenderDevice(m_world_editor->getEngine(), "pipelines/game_view.json");
			m_game_render_device->m_hdc = GetDC(game_hwnd);
			m_game_render_device->m_opengl_context = hglrc;
			m_game_render_device->getPipeline().setScene((Lumix::RenderScene*)m_world_editor->getEngine().getScene(crc32("renderer")));
			m_world_editor->getEngine().getRenderer().setRenderDevice(*m_game_render_device);

			m_world_editor->universeCreated().bind<App, &App::onUniverseCreated>(this);
			m_world_editor->universeDestroyed().bind<App, &App::onUniverseDestroyed>(this);

			m_main_window->getSceneView()->setPipeline(m_edit_render_device->getPipeline());
			m_main_window->getGameView()->setPipeline(m_game_render_device->getPipeline());
		}


		void shutdown()
		{
			delete m_game_render_device;
			m_game_render_device = NULL;
			delete m_edit_render_device;
			m_edit_render_device = NULL;
		}


		void renderGizmos()
		{
			m_world_editor->renderIcons(*m_edit_render_device);
			m_world_editor->getGizmo().updateScale(m_world_editor->getEditCamera());
			m_world_editor->getGizmo().render(m_world_editor->getEngine().getRenderer(), *m_edit_render_device);
		}


		void renderEditView()
		{
			if (m_main_window->getSceneView()->getViewWidget()->isVisible() && !m_main_window->getSceneView()->visibleRegion().isEmpty())
			{
				PROFILE_FUNCTION();
				m_edit_render_device->beginFrame();
				m_world_editor->render(*m_edit_render_device);
				m_world_editor->getEngine().getRenderer().cleanup();
				m_edit_render_device->endFrame();
			}
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
				/// TODO refactor
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
			while (m_main_window->isVisible())
			{
				{
					PROFILE_BLOCK("tick");
					m_main_window->update();
					renderEditView();

					if(!m_main_window->getGameView()->getContentWidget()->visibleRegion().isEmpty())
					{
						m_world_editor->getEngine().getRenderer().renderGame();
					}
					m_world_editor->tick();
					handleEvents();
				}
				Lumix::g_profiler.frame();
			}
		}

	private:
		WGLRenderDevice* m_edit_render_device;
		WGLRenderDevice* m_game_render_device;
		MainWindow* m_main_window;
		Lumix::WorldEditor* m_world_editor;
		QApplication* m_qt_app;
};

int main(int argc, char* argv[])
{
	App app;
	app.init(argc, argv);
	app.run();
	app.shutdown();
	return 0;
}
