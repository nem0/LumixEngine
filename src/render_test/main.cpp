#include "render_test.h"
#include <QApplication>
#include <qdir.h>
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "editor/gizmo.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/gl_ext.h"
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/texture.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"
#include "GL/glew.h"


class WGLRenderDevice : public Lumix::IRenderDevice
{
public:
	WGLRenderDevice(Lumix::Engine& engine, const char* pipeline_path)
	{
		Lumix::Pipeline* pipeline_object = static_cast<Lumix::Pipeline*>(engine.getResourceManager().get(Lumix::ResourceManager::PIPELINE)->load(Lumix::Path(pipeline_path)));
		ASSERT(pipeline_object);
		if (pipeline_object)
		{
			m_pipeline = Lumix::PipelineInstance::create(*pipeline_object, engine.getAllocator());
		}

	}


	~WGLRenderDevice()
	{
		if (m_pipeline)
		{
			Lumix::PipelineInstance::destroy(m_pipeline);
		}
	}


	virtual void beginFrame() override
	{
		PROFILE_FUNCTION();
		wglMakeCurrent(m_hdc, m_opengl_context);
	}


	virtual void endFrame() override
	{
		PROFILE_FUNCTION();
		wglSwapLayerBuffers(m_hdc, WGL_SWAP_MAIN_PLANE);
	}


	virtual Lumix::PipelineInstance& getPipeline()
	{
		return *m_pipeline;
	}


	virtual int getWidth() const override
	{
		return m_pipeline->getWidth();
	}


	virtual int getHeight() const override
	{
		return m_pipeline->getHeight();
	}


	Lumix::PipelineInstance* m_pipeline;
	HDC m_hdc;
	HGLRC m_opengl_context;
};


class App
{
public:
	App()
	{
		m_qt_app = NULL;
		m_main_window = NULL;
		m_current_test = -1;
		m_is_test_universe_loaded = false;
	}

	~App()
	{
		delete m_main_window;
		delete m_qt_app;
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

	
	void universeFileLoaded(Lumix::FS::IFile* file, bool success, Lumix::FS::FileSystem& fs)
	{
		ASSERT(success);
		if (success)
		{
			ASSERT(file->getBuffer());
			Lumix::InputBlob blob(file->getBuffer(), file->size());
			uint32_t hash = 0;
			blob.read(hash);
			uint32_t engine_hash = 0;
			blob.read(engine_hash);			
			if (crc32((const uint8_t*)blob.getData() + sizeof(hash), blob.getSize() - sizeof(hash)) != hash)
			{
				ASSERT(false);
				return;
			}
			bool deserialize_succeeded = m_engine->deserialize(blob);
			m_is_test_universe_loaded = true;
			ASSERT(deserialize_succeeded);
		}

		fs.close(file);
	}


	void init(int argc, char* argv[])
	{
		m_qt_app = new QApplication(argc, argv);
		QFile file("editor/stylesheet.qss");
		file.open(QFile::ReadOnly);
		m_qt_app->setStyleSheet(QLatin1String(file.readAll()));
		
		m_main_window = new RenderTest();
		m_main_window->show();
		
		HWND hwnd = (HWND)m_main_window->centralWidget()->winId();
		HWND hwnds[] = { hwnd };
		HGLRC hglrc = createGLContext(hwnds, sizeof(hwnds) / sizeof(hwnds[0]));

		m_engine = Lumix::Engine::create("", NULL, NULL, m_allocator);
		m_render_device = new WGLRenderDevice(*m_engine, "pipelines/main.json");
		m_render_device->m_hdc = GetDC(hwnd);
		m_render_device->m_opengl_context = hglrc;
		m_engine->createUniverse();
		m_render_device->getPipeline().setScene((Lumix::RenderScene*)m_engine->getScene(crc32("renderer")));
		m_render_device->getPipeline().resize(600, 400);

		enumerateTests();
	}


	void shutdown()
	{
	}


	void handleEvents()
	{
		PROFILE_FUNCTION();
		{
			PROFILE_BLOCK("qt::processEvents");
			m_qt_app->processEvents();
		}
	}


	void enumerateTests()
	{
		QDir dir("render_tests");
		QStringList files = dir.entryList(QStringList() << "*.unv");
		for (int i = 0; i < files.size(); ++i)
		{
			m_tests.push_back(QString("render_tests/") + files[i].left(files[i].size() - 4));
		}
	}


	bool nextTest()
	{
		Lumix::FS::FileSystem& fs = m_engine->getFileSystem();

		bool can_do_next_test = m_current_test == -1 || (!m_engine->getResourceManager().isLoading() && m_is_test_universe_loaded);
		if (can_do_next_test)
		{
			char path[LUMIX_MAX_PATH];
			if (m_current_test >= 0)
			{
				Lumix::copyString(path, sizeof(path), m_tests[m_current_test].toLatin1().data());
				Lumix::catCString(path, sizeof(path), "_res.tga");
				m_engine->getRenderer().makeScreenshot(Lumix::Path(path), 500, 500);

				char path_preimage[LUMIX_MAX_PATH];
				Lumix::copyString(path_preimage, sizeof(path), m_tests[m_current_test].toLatin1().data());
				Lumix::catCString(path_preimage, sizeof(path), ".tga");

				auto file1 = fs.open(fs.getDefaultDevice(), path, Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				auto file2 = fs.open(fs.getDefaultDevice(), path_preimage, Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				unsigned int diference = Lumix::Texture::compareTGA(m_allocator, file1, file2, 10);
				fs.close(file1);
				fs.close(file2);
				ASSERT(diference < 100);
			}

			++m_current_test;
			if (m_current_test < m_tests.size())
			{
				Lumix::copyString(path, sizeof(path), m_tests[m_current_test].toLatin1().data());
				Lumix::catCString(path, sizeof(path), ".unv");
				Lumix::FS::ReadCallback file_read_cb;
				file_read_cb.bind<App, &App::universeFileLoaded>(this);
				fs.openAsync(fs.getDefaultDevice(), path, Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ, file_read_cb);
				m_is_test_universe_loaded = false;
				return true;
			}
			return false;
		}
		return true;
	}


	void run()
	{
		while (m_main_window->isVisible())
		{
			{
				PROFILE_BLOCK("tick");
				m_render_device->beginFrame();
				m_render_device->getPipeline().render();
				m_render_device->endFrame();
				m_engine->update(false, 1, -1);
				if (!m_engine->getResourceManager().isLoading())
				{
					if (!nextTest())
						return;
				}
				m_engine->getFileSystem().updateAsyncTransactions();
				handleEvents();
			}


			Lumix::g_profiler.frame();
		}
	}

private:
	Lumix::DefaultAllocator m_allocator;
	Lumix::Engine* m_engine;
	RenderTest* m_main_window;
	QApplication* m_qt_app;
	WGLRenderDevice* m_render_device;
	QWidget* m_view;
	QStringList m_tests;
	int m_current_test;
	bool m_is_test_universe_loaded;
};


int main(int argc, char* argv[])
{
	App app;
	app.init(argc, argv);
	app.run();
	app.shutdown();
	return 0;
}
