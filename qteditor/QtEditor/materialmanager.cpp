#include "materialmanager.h"
#include "ui_materialmanager.h"
#include <qfilesystemmodel.h>
#include <qpainter.h>
#include "core/crc32.h"
#include "editor/editor_server.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "engine/engine.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/render_scene.h"
#include "universe/universe.h"
#include "wgl_render_device.h"


class MaterialManagerUI
{
	public:
		Lux::Engine* m_engine;
		Lux::Universe* m_universe;
		Lux::RenderScene* m_render_scene;
		WGLRenderDevice* m_render_device;
		Lux::Model* m_selected_object_model;
		QFileSystemModel* m_fs_model;
};


MaterialManager::MaterialManager(QWidget *parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::MaterialManager)
{
	m_impl = new MaterialManagerUI();
	m_impl->m_selected_object_model = NULL;
	m_ui->setupUi(this);
	m_impl->m_fs_model = new QFileSystemModel();
	m_impl->m_fs_model->setRootPath(QDir::currentPath() + "/materials");
	QStringList filters;
	filters << "*.mat";
	m_impl->m_fs_model->setNameFilters(filters);
	m_impl->m_fs_model->setNameFilterDisables(false);
	m_ui->fileListView->setModel(m_impl->m_fs_model);
	m_ui->fileListView->setRootIndex(m_impl->m_fs_model->index(QDir::currentPath() + "/materials"));
	m_impl->m_engine = NULL;
	m_impl->m_universe = NULL;
	m_impl->m_render_scene = NULL;
}

void MaterialManager::updatePreview()
{
	m_impl->m_render_device->beginFrame();
	m_impl->m_engine->getRenderer().render(*m_impl->m_render_device);
	m_impl->m_render_device->endFrame();
}

void MaterialManager::fillObjectMaterials()
{
	m_ui->objectMaterialList->clear();
	for(int i = 0; i < m_impl->m_selected_object_model->getMeshCount(); ++i)
	{
		const char* path = m_impl->m_selected_object_model->getMesh(i).getMaterial()->getPath().c_str();
		m_ui->objectMaterialList->addItem(path);
	}
}

void MaterialManager::onPropertyList(Lux::Event& evt)
{
	Lux::PropertyListEvent& event = static_cast<Lux::PropertyListEvent&>(evt);
	if (event.type_hash == crc32("renderable"))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			if (event.properties[i].name_hash == crc32("source"))
			{	
				m_impl->m_selected_object_model = static_cast<Lux::Model*>(m_impl->m_engine->getResourceManager().get(Lux::ResourceManager::MODEL)->get((char*)event.properties[i].data));
				fillObjectMaterials();
			}
		}
	}
}

void MaterialManager::setEditorClient(Lux::EditorClient& client)
{
	client.getEventManager().addListener(Lux::ServerMessageType::PROPERTY_LIST).bind<MaterialManager, &MaterialManager::onPropertyList>(this);
}

void MaterialManager::setEditorServer(Lux::EditorServer& server)
{
	ASSERT(m_impl->m_engine == NULL);
	HWND hwnd = (HWND)m_ui->previewWidget->winId();
	m_impl->m_engine = &server.getEngine();
	m_impl->m_universe = new Lux::Universe();
	m_impl->m_universe->create();

	m_impl->m_render_scene = Lux::RenderScene::createInstance(server.getEngine(), *m_impl->m_universe);
	m_impl->m_render_device = new WGLRenderDevice(server.getEngine(), "pipelines/main.json");
	m_impl->m_render_device->m_hdc = GetDC(hwnd);
	m_impl->m_render_device->m_opengl_context = wglGetCurrentContext();
	m_impl->m_render_device->getPipeline().setScene(m_impl->m_render_scene);
	
	const Lux::Entity& camera_entity = m_impl->m_universe->createEntity();
	Lux::Component cmp = m_impl->m_render_scene->createComponent(crc32("camera"), camera_entity);
	m_impl->m_render_scene->setCameraSlot(cmp, Lux::string("editor"));
	
	Lux::Entity light_entity = m_impl->m_universe->createEntity();
	light_entity.setRotation(Lux::Quat(Lux::Vec3(0, 1, 0), 3.14159265f));
	m_impl->m_render_scene->createComponent(crc32("light"), light_entity);
	
	Lux::Entity model_entity = m_impl->m_universe->createEntity();
	model_entity.setPosition(0, 0, -5);
	Lux::Component cmp2 = m_impl->m_render_scene->createComponent(crc32("renderable"), model_entity);
	m_impl->m_render_scene->setRenderablePath(cmp2, Lux::string("models/material_sphere.msh"));

	m_ui->previewWidget->setAttribute(Qt::WA_NoSystemBackground);
	m_ui->previewWidget->setAutoFillBackground(false);
	m_ui->previewWidget->setAttribute(Qt::WA_OpaquePaintEvent);
	m_ui->previewWidget->setAttribute(Qt::WA_TranslucentBackground);
	m_ui->previewWidget->setAttribute(Qt::WA_PaintOnScreen);
	m_ui->previewWidget->m_render_device = m_impl->m_render_device;
	m_ui->previewWidget->m_engine = m_impl->m_engine;
	/// TODO refactor (EditorServer::create)
	HDC hdc;
	hdc = GetDC(hwnd);
	ASSERT(hdc != NULL);
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
		ASSERT(false);
	}
	BOOL success = SetPixelFormat(hdc, pixelformat, &pfd);
	if (success == FALSE)
	{
		ASSERT(false);
	}
}

MaterialManager::~MaterialManager()
{
	Lux::RenderScene::destroyInstance(m_impl->m_render_scene);
	m_impl->m_universe->destroy();
	delete m_impl->m_universe;
	delete m_impl;
	delete m_ui;
}

void MaterialManager::on_fileListView_doubleClicked(const QModelIndex &index)
{
	Lux::Model* model = static_cast<Lux::Model*>(m_impl->m_engine->getResourceManager().get(Lux::ResourceManager::MODEL)->get("models/material_sphere.msh"));
	QString file_path = m_impl->m_fs_model->fileInfo(index).filePath().toLower();

	Lux::Material* material = static_cast<Lux::Material*>(m_impl->m_engine->getResourceManager().get(Lux::ResourceManager::MATERIAL)->load(file_path.toLatin1().data()));
	model->getMesh(0).setMaterial(material);
}

void MaterialManager::on_objectMaterialList_doubleClicked(const QModelIndex &index)
{
	Lux::Model* model = static_cast<Lux::Model*>(m_impl->m_engine->getResourceManager().get(Lux::ResourceManager::MODEL)->get("models/material_sphere.msh"));
	QListWidgetItem* item = m_ui->objectMaterialList->item(index.row());
	
	Lux::Material* material = static_cast<Lux::Material*>(m_impl->m_engine->getResourceManager().get(Lux::ResourceManager::MATERIAL)->load(item->text().toLatin1().data()));
	model->getMesh(0).setMaterial(material);

}
