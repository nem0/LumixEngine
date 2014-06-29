#include "materialmanager.h"
#include "ui_materialmanager.h"
#include <qboxlayout.h>
#include <qcheckbox.h>
#include <qfilesystemmodel.h>
#include <qformlayout.h>
#include <qlineedit.h>
#include <qpainter.h>
#include <qpushbutton.h>
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/profiler.h"
#include "editor/editor_server.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "engine/engine.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "universe/universe.h"
#include "wgl_render_device.h"


class MaterialManagerUI
{
	public:
		Lumix::Engine* m_engine;
		Lumix::Universe* m_universe;
		Lumix::RenderScene* m_render_scene;
		WGLRenderDevice* m_render_device;
		Lumix::Model* m_selected_object_model;
		QFileSystemModel* m_fs_model;
		Lumix::Material* m_material;
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
	PROFILE_FUNCTION();
	m_impl->m_render_device->beginFrame();
	m_impl->m_engine->getRenderer().render(*m_impl->m_render_device);
	m_impl->m_render_device->endFrame();
}

void MaterialManager::fillObjectMaterials()
{
	m_ui->objectMaterialList->clear();
	if(m_impl->m_selected_object_model)
	{
		for(int i = 0; i < m_impl->m_selected_object_model->getMeshCount(); ++i)
		{
			const char* path = m_impl->m_selected_object_model->getMesh(i).getMaterial()->getPath().c_str();
			m_ui->objectMaterialList->addItem(path);
		}
	}
}

void MaterialManager::onPropertyList(Lumix::PropertyListEvent& event)
{
	if (event.type_hash == crc32("renderable"))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			if (event.properties[i].name_hash == crc32("source"))
			{	
				m_impl->m_selected_object_model = static_cast<Lumix::Model*>(m_impl->m_engine->getResourceManager().get(Lumix::ResourceManager::MODEL)->get((char*)event.properties[i].data));
				fillObjectMaterials();
			}
		}
	}
}

void MaterialManager::setEditorClient(Lumix::EditorClient& client)
{
	client.propertyListReceived().bind<MaterialManager, &MaterialManager::onPropertyList>(this);
}

void MaterialManager::setEditorServer(Lumix::EditorServer& server)
{
	ASSERT(m_impl->m_engine == NULL);
	HWND hwnd = (HWND)m_ui->previewWidget->winId();
	m_impl->m_engine = &server.getEngine();
	m_impl->m_universe = new Lumix::Universe();
	m_impl->m_universe->create();

	m_impl->m_render_scene = Lumix::RenderScene::createInstance(server.getEngine(), *m_impl->m_universe);
	m_impl->m_render_device = new WGLRenderDevice(server.getEngine(), "pipelines/main.json");
	m_impl->m_render_device->m_hdc = GetDC(hwnd);
	m_impl->m_render_device->m_opengl_context = wglGetCurrentContext();
	m_impl->m_render_device->getPipeline().setScene(m_impl->m_render_scene);
	
	const Lumix::Entity& camera_entity = m_impl->m_universe->createEntity();
	Lumix::Component cmp = m_impl->m_render_scene->createComponent(crc32("camera"), camera_entity);
	m_impl->m_render_scene->setCameraSlot(cmp, Lumix::string("editor"));
	
	Lumix::Entity light_entity = m_impl->m_universe->createEntity();
	light_entity.setRotation(Lumix::Quat(Lumix::Vec3(0, 1, 0), 3.14159265f));
	m_impl->m_render_scene->createComponent(crc32("light"), light_entity);
	
	Lumix::Entity model_entity = m_impl->m_universe->createEntity();
	model_entity.setPosition(0, 0, -5);
	Lumix::Component cmp2 = m_impl->m_render_scene->createComponent(crc32("renderable"), model_entity);
	m_impl->m_render_scene->setRenderablePath(cmp2, Lumix::string("models/editor/material_sphere.msh"));

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
	Lumix::RenderScene::destroyInstance(m_impl->m_render_scene);
	m_impl->m_universe->destroy();
	delete m_impl->m_render_device;
	delete m_impl->m_universe;
	delete m_impl;
	delete m_ui;
}

class ICppObjectProperty
{
	public:
		enum Type
		{
			BOOL,
			SHADER,
			UNKNOWN
		};

	public:
		Type getType() const { return m_type; }
		const char* getName() const { return m_name.c_str(); }

		template <class T>
		static Type getType();

		template <> static Type getType<bool>() { return BOOL; }
		template <> static Type getType<Lumix::Shader*>() { return SHADER; }

	protected:
		Type m_type;
		Lumix::string m_name;

};

template <typename V, class T>
class CppObjectProperty : public ICppObjectProperty
{

	public:
		typedef V (T::*Getter)() const;
		typedef void (T::*Setter)(V);


	public:
		CppObjectProperty(const char* name, Getter getter, Setter setter)
		{
			m_name = name;
			m_type = ICppObjectProperty::getType<V>();
			m_getter = getter;
			m_setter = setter;
		}

		V get(const T& t)
		{
			return (t.*m_getter)();
		}

		void set(T& t, const V& v)
		{
			(t.*m_setter)(v);
		}

	private:
		Getter m_getter;
		Setter m_setter;
};

void MaterialManager::onBoolPropertyStateChanged(int)
{
	QCheckBox* obj = qobject_cast<QCheckBox*>(QObject::sender());
	if(obj)
	{
		CppObjectProperty<bool, Lumix::Material>* prop = static_cast<CppObjectProperty<bool, Lumix::Material>*>(obj->property("cpp_property").data());
		prop->set(*m_impl->m_material, obj->isChecked());
	}
}

void MaterialManager::onTextureChanged()
{
	QLineEdit* edit = qobject_cast<QLineEdit*>(QObject::sender());
	if(edit)
	{
		int i = edit->property("texture_index").toInt();
		m_impl->m_material->setTexture(i, static_cast<Lumix::Texture*>(m_impl->m_engine->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(edit->text().toLatin1().data())));
	}
}

void MaterialManager::onShaderChanged()
{
	QLineEdit* edit = static_cast<QLineEdit*>(QObject::sender());
	m_impl->m_material->setShader(static_cast<Lumix::Shader*>(m_impl->m_engine->getResourceManager().get(Lumix::ResourceManager::SHADER)->load(edit->text().toLatin1().data())));
}

void MaterialManager::onTextureAdded()
{
	m_impl->m_material->addTexture(static_cast<Lumix::Texture*>(m_impl->m_engine->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load("textures/default.dds")));
	selectMaterial(m_impl->m_material->getPath().c_str());
}

void MaterialManager::selectMaterial(const char* path)
{
	Lumix::Material* material = static_cast<Lumix::Material*>(m_impl->m_engine->getResourceManager().get(Lumix::ResourceManager::MATERIAL)->load(path));
	material->getObserverCb().bind<MaterialManager, &MaterialManager::onMaterialLoaded>(this);
	m_impl->m_material = material;
}

void MaterialManager::onMaterialLoaded(Lumix::Resource::State, Lumix::Resource::State)
{
	ICppObjectProperty* properties[] = 
	{
		new CppObjectProperty<bool, Lumix::Material>("Z test", &Lumix::Material::isZTest, &Lumix::Material::enableZTest),
		new CppObjectProperty<bool, Lumix::Material>("Backface culling", &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling),
		new CppObjectProperty<Lumix::Shader*, Lumix::Material>("Shader", &Lumix::Material::getShader, &Lumix::Material::setShader)
	};

	Lumix::Model* model = static_cast<Lumix::Model*>(m_impl->m_engine->getResourceManager().get(Lumix::ResourceManager::MODEL)->get("models/editor/material_sphere.msh"));
	Lumix::Material* material = m_impl->m_material;
	material->getObserverCb().unbind<MaterialManager, &MaterialManager::onMaterialLoaded>(this);
	model->getMesh(0).setMaterial(material);

	QFormLayout* layout = m_ui->materialPropertiesLayout;
	QLayoutItem* item;
	while((item = layout->takeAt(0)) != NULL)
	{
		delete item->widget();
		delete item;
	}

	for(int i = 0; i < sizeof(properties) / sizeof(ICppObjectProperty*); ++i)
	{
		switch(properties[i]->getType())
		{
			case ICppObjectProperty::BOOL:
				{
					QCheckBox* checkbox = new QCheckBox();
					checkbox->setProperty("cpp_property", qVariantFromValue((void*)properties[i]));
					
					checkbox->setChecked(static_cast<CppObjectProperty<bool, Lumix::Material>*>(properties[i])->get(*material));
					layout->addRow(properties[i]->getName(), checkbox);
					connect(checkbox, SIGNAL(stateChanged(int)), this, SLOT(onBoolPropertyStateChanged(int)));
				}
				break;
			case ICppObjectProperty::SHADER:
				{
					QLineEdit* edit = new QLineEdit();
					Lumix::Shader* shader = static_cast<CppObjectProperty<Lumix::Shader*, Lumix::Material>*>(properties[i])->get(*material);
					if(shader)
					{
						edit->setText(shader->getPath().c_str());
					}
					connect(edit, SIGNAL(editingFinished()), this, SLOT(onShaderChanged()));
					layout->addRow(properties[i]->getName(), edit);
				}
				break;
			default: 
				ASSERT(false);
				break;
		}
	}
	for(int i = 0; i < material->getTextureCount(); ++i)
	{
		QLineEdit* edit = new QLineEdit;
		QBoxLayout* inner_layout = new QBoxLayout(QBoxLayout::Direction::LeftToRight);
		QPushButton* button = new QPushButton();
		button->setText("Remove");
		inner_layout->addWidget(edit);
		inner_layout->addWidget(button);
		edit->setText(material->getTexture(i)->getPath().c_str());
		edit->setProperty("texture_index", i);
		layout->addRow("Texture", inner_layout);
		connect(edit, SIGNAL(editingFinished()), this, SLOT(onTextureChanged()));
		connect(button, SIGNAL(clicked()), this, SLOT(onTextureRemoved()));
		button->setProperty("texture_id", i);
	}
	QPushButton* button = new QPushButton();
	button->setText("Add Texture");
	connect(button, SIGNAL(clicked()), this, SLOT(onTextureAdded()));
	layout->addRow("", button);
}

void MaterialManager::onTextureRemoved()
{
	QPushButton* button = static_cast<QPushButton*>(QObject::sender());
	int i = button->property("texture_id").toInt();
	m_impl->m_material->removeTexture(i);
	selectMaterial(m_impl->m_material->getPath().c_str());
}

void MaterialManager::on_fileListView_doubleClicked(const QModelIndex &index)
{
	QString file_path = m_impl->m_fs_model->fileInfo(index).filePath().toLower();
	selectMaterial(file_path.toLatin1().data());
}

void MaterialManager::on_objectMaterialList_doubleClicked(const QModelIndex &index)
{
	QListWidgetItem* item = m_ui->objectMaterialList->item(index.row());
	selectMaterial(item->text().toLatin1().data());
}

void MaterialManager::on_saveMaterialButton_clicked()
{
	Lumix::FS::FileSystem& fs = m_impl->m_engine->getFileSystem();
	Lumix::FS::IFile* file = fs.open(fs.getDefaultDevice(), m_impl->m_material->getPath().c_str(), Lumix::FS::Mode::RECREATE | Lumix::FS::Mode::WRITE);
	if(file)
	{
		Lumix::JsonSerializer serializer(*file, Lumix::JsonSerializer::AccessMode::WRITE, m_impl->m_material->getPath().c_str());
		m_impl->m_material->save(serializer);
		fs.close(file);
	}
	else
	{
		Lumix::g_log_error.log("Material manager", "Could not save file %s", m_impl->m_material->getPath().c_str());
	}
}
