#include "render_test.h"
#include <QApplication>
#include <qdir.h>
#include "core/blob.h"
#include "core/crc32.h"
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
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/texture.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"



class App
{
public:
	App()
	{
		m_qt_app = NULL;
		m_main_window = NULL;
		m_current_test = -1;
		m_is_test_universe_loaded = false;
		m_universe_context = nullptr;
	}

	~App()
	{
		delete m_main_window;
		delete m_qt_app;
		ASSERT(!m_universe_context);
	}


	void universeFileLoaded(Lumix::FS::IFile& file,
							bool success,
							Lumix::FS::FileSystem& fs)
	{
		ASSERT(success);
		if (success)
		{
			ASSERT(file.getBuffer());
			Lumix::InputBlob blob(file.getBuffer(), file.size());
			uint32_t hash = 0;
			blob.read(hash);
			uint32_t engine_hash = 0;
			blob.read(engine_hash);
			if (Lumix::crc32((const uint8_t*)blob.getData() + sizeof(hash),
							 blob.getSize() - sizeof(hash)) != hash)
			{
				ASSERT(false);
				return;
			}
			bool deserialize_succeeded = m_engine->deserialize(*m_universe_context, blob);
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
		m_engine = Lumix::Engine::create(hwnd, NULL, m_allocator);
		Lumix::Pipeline* pipeline_object = static_cast<Lumix::Pipeline*>(
			m_engine->getResourceManager()
				.get(Lumix::ResourceManager::PIPELINE)
				->load(Lumix::Path("pipelines/main.json")));
		ASSERT(pipeline_object);
		if (pipeline_object)
		{
			m_pipeline = Lumix::PipelineInstance::create(
				*pipeline_object, m_engine->getAllocator());
		}

		m_universe_context = &m_engine->createUniverse();
		m_pipeline->setScene(
			(Lumix::RenderScene*)m_universe_context->getScene(Lumix::crc32("renderer")));
		m_pipeline->resize(600, 400);

		enumerateTests();
	}


	void shutdown() 
	{
		if (m_pipeline)
		{
			Lumix::PipelineInstance::destroy(m_pipeline);
		}
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
			m_tests.push_back(QString("render_tests/") +
							  files[i].left(files[i].size() - 4));
		}
	}


	bool nextTest()
	{
		Lumix::FS::FileSystem& fs = m_engine->getFileSystem();

		bool can_do_next_test = m_current_test == -1 ||
								(!m_engine->getResourceManager().isLoading() &&
								 m_is_test_universe_loaded);
		if (can_do_next_test)
		{
			char path[Lumix::MAX_PATH_LENGTH];
			if (m_current_test >= 0)
			{
				Lumix::copyString(path,
								  sizeof(path),
								  m_tests[m_current_test].toLatin1().data());
				Lumix::catString(path, sizeof(path), "_res.tga");
				m_engine->getRenderer().makeScreenshot(Lumix::Path(path));

				char path_preimage[Lumix::MAX_PATH_LENGTH];
				Lumix::copyString(path_preimage,
								  sizeof(path),
								  m_tests[m_current_test].toLatin1().data());
				Lumix::catString(path_preimage, sizeof(path), ".tga");

				auto file1 =
					fs.open(fs.getDefaultDevice(),
							path,
							Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				auto file2 =
					fs.open(fs.getDefaultDevice(),
							path_preimage,
							Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				unsigned int diference =
					Lumix::Texture::compareTGA(m_allocator, file1, file2, 10);
				fs.close(*file1);
				fs.close(*file2);
				ASSERT(diference < 100);
			}

			++m_current_test;
			if (m_current_test < m_tests.size())
			{
				Lumix::copyString(path,
								  sizeof(path),
								  m_tests[m_current_test].toLatin1().data());
				Lumix::catString(path, sizeof(path), ".unv");
				Lumix::FS::ReadCallback file_read_cb;
				file_read_cb.bind<App, &App::universeFileLoaded>(this);
				fs.openAsync(fs.getDefaultDevice(),
							 path,
							 Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ,
							 file_read_cb);
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
				m_engine->update(*m_universe_context);
				m_pipeline->render();
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
	Lumix::UniverseContext* m_universe_context;
	Lumix::PipelineInstance* m_pipeline;
	RenderTest* m_main_window;
	QApplication* m_qt_app;
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
