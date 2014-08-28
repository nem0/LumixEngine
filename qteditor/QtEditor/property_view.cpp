#include "property_view.h"
#include "ui_property_view.h"
#include "animation/animation_system.h"
#include "assetbrowser.h"
#include "core/crc32.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "scripts/scriptcompiler.h"
#include <qcheckbox.h>
#include <qdesktopservices.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <qfiledialog.h>
#include <qmenu.h>
#include <qmimedata.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qtextstream.h>


static const char* component_map[] =
{
	"Animable", "animable",
	"Camera", "camera",
	"Directional light", "light",
	"Mesh", "renderable",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Script", "script",
	"Terrain", "terrain"
};


class FileEdit : public QLineEdit
{
	public:
		FileEdit(QWidget* parent, PropertyView* property_view)
			: QLineEdit(parent)
			, m_property_view(property_view)
			, m_world_editor(NULL)
		{
			setAcceptDrops(true);
		}

		virtual void dragEnterEvent(QDragEnterEvent* event) override
		{
			if (event->mimeData()->hasUrls())
			{
				event->acceptProposedAction();
			}
		}

		virtual void dropEvent(QDropEvent* event)
		{
			ASSERT(m_world_editor);
			const QList<QUrl>& list = event->mimeData()->urls();
			if(!list.empty())
			{
				QString file = list[0].toLocalFile();
				if(file.toLower().startsWith(m_world_editor->getBasePath()))
				{
					file.remove(0, QString(m_world_editor->getBasePath()).length());
				}
				if(file.startsWith("/"))
				{
					file.remove(0, 1);
				}
				setText(file);
				emit editingFinished();
			}
		}

		void setServer(Lumix::WorldEditor* server)
		{
			m_world_editor = server;
		}

	private:
		PropertyView* m_property_view;
		Lumix::WorldEditor* m_world_editor;
};


class ComponentPropertyObject : public PropertyViewObject
{
	public:
		ComponentPropertyObject(const char* name, Lumix::Component cmp, Lumix::IPropertyDescriptor& descriptor)
			: PropertyViewObject(name)
			, m_descriptor(descriptor)
			, m_component(cmp)
		{
		}


		virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
		{
			Lumix::Blob stream;
			m_descriptor.get(m_component, stream);
			
			switch (m_descriptor.getType())
			{
				case Lumix::IPropertyDescriptor::BOOL:
					{
						bool b;
						stream.read(b);
						QCheckBox* checkbox = new QCheckBox();
						item->treeWidget()->setItemWidget(item, 1, checkbox);
						checkbox->setChecked(b);
						checkbox->connect(checkbox, &QCheckBox::stateChanged, [this, &view](bool new_value)
						{
							view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), &new_value, sizeof(new_value));
						});
					}
					break;
				case Lumix::IPropertyDescriptor::VEC3:
					{
						Lumix::Vec3 value;
						stream.read(value);
						item->setText(1, QString("%1; %2; %3").arg(value.x).arg(value.y).arg(value.z));

						QDoubleSpinBox* sb1 = new QDoubleSpinBox();
						sb1->setValue(value.x);
						item->insertChild(0, new QTreeWidgetItem(QStringList() << "x"));
						item->treeWidget()->setItemWidget(item->child(0), 1, sb1);

						QDoubleSpinBox* sb2 = new QDoubleSpinBox();
						sb2->setValue(value.y);
						item->insertChild(1, new QTreeWidgetItem(QStringList() << "y"));
						item->treeWidget()->setItemWidget(item->child(1), 1, sb2);

						QDoubleSpinBox* sb3 = new QDoubleSpinBox();
						sb3->setValue(value.y);
						item->insertChild(2, new QTreeWidgetItem(QStringList() << "z"));
						item->treeWidget()->setItemWidget(item->child(2), 1, sb3);

						sb1->connect(sb1, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [&view, this, sb1, sb2, sb3](double) 
						{
							Lumix::Vec3 value;
							value.set((float)sb1->value(), (float)sb2->value(), (float)sb3->value());
							view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), &value, sizeof(value));
						});
					}
					break;
				case Lumix::IPropertyDescriptor::FILE:
					{
						char path[LUMIX_MAX_PATH];
						stream.read(path, stream.getBufferSize());
						QWidget* widget = new QWidget();
						FileEdit* edit = new FileEdit(widget, NULL);
						edit->setText(path);
						edit->setServer(view.getWorldEditor());
						QHBoxLayout* layout = new QHBoxLayout(widget);
						layout->addWidget(edit);
						layout->setContentsMargins(0, 0, 0, 0);
						QPushButton* button = new QPushButton("...", widget);
						layout->addWidget(button);
						button->connect(button, &QPushButton::clicked, [this, edit, &view]()
						{
							QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "*.msh"); TODO("not only msh");
							char rel_path[LUMIX_MAX_PATH];
							QByteArray byte_array = str.toLatin1();
							const char* text = byte_array.data();
							view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, text);
							view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), rel_path, strlen(rel_path) + 1);
							edit->setText(rel_path);
						});
				
						QPushButton* button2 = new QPushButton("->", widget);
						layout->addWidget(button2);
						button2->connect(button2, &QPushButton::clicked, [&view, edit]()
						{
							view.setSelectedResourceFilename(edit->text().toLatin1().data());
						});
				
						item->treeWidget()->setItemWidget(item, 1, widget);
						connect(edit, &QLineEdit::editingFinished, [edit, &view, this]()
						{
							TODO("todo");
							//if (!m_is_updating_values)
							{
								QByteArray byte_array = edit->text().toLatin1();
								view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), byte_array.data(), byte_array.size() + 1);
							}
						});
					}
					break;
				case Lumix::IPropertyDescriptor::INTEGER:
					{
						int value;
						stream.read(value);
						QSpinBox* edit = new QSpinBox();
						edit->setValue(value);
						item->treeWidget()->setItemWidget(item, 1, edit);
						edit->setMinimum(INT_MIN);
						edit->setMaximum(INT_MAX);
						connect(edit, (void (QSpinBox::*)(int))&QSpinBox::valueChanged, [this, &view](int new_value) 
						{
							int value = new_value;
							view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), &value, sizeof(value));
						});
					}
					break;
				case Lumix::IPropertyDescriptor::DECIMAL:
					{
						float value;
						stream.read(value);
						QDoubleSpinBox* edit = new QDoubleSpinBox();
						edit->setValue(value);
						item->treeWidget()->setItemWidget(item, 1, edit);
						edit->setMaximum(FLT_MAX);
						connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [this, &view](double new_value) 
						{
							float value = (float)new_value;
							view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), &value, sizeof(value));
						});
					}
					break;
				case Lumix::IPropertyDescriptor::STRING:
					{
						QLineEdit* edit = new QLineEdit();
						item->treeWidget()->setItemWidget(item, 1, edit);
						edit->setText((const char*)stream.getBuffer());
						connect(edit, &QLineEdit::editingFinished, [edit, this, &view]()
						{
							QByteArray byte_array = edit->text().toLatin1();
							const char* text = byte_array.data();
							view.getWorldEditor()->setProperty(m_component.type, m_descriptor.getNameHash(), text, strlen(text) + 1);
						});
					}
					break;
				default:
					ASSERT(false);
					break;
			}
		}


		virtual bool isEditable() const override
		{
			return false;
		}


		Lumix::Component getComponent() const { return m_component; }

	private:
		Lumix::IPropertyDescriptor& m_descriptor;
		Lumix::Component m_component;
};



template <class Value, class Obj>
class GetterSetterObject : public PropertyViewObject
{
	public:
		typedef Value (Obj::*Getter)() const;
		typedef void (Obj::*Setter)(Value);
		typedef void(*CreateEditor)(QTreeWidgetItem*, GetterSetterObject&, Value);


		GetterSetterObject(const char* name, Obj* object, Getter getter, Setter setter, CreateEditor create_editor)
			: PropertyViewObject(name)
		{
			m_object = object;
			m_getter = getter;
			m_setter = setter;
			m_create_editor = create_editor;
		}


		virtual void createEditor(PropertyView&, QTreeWidgetItem* item) override
		{
			m_create_editor(item, *this, (m_object->*m_getter)());
		}


		virtual bool isEditable() const override
		{
			return m_setter != NULL;
		}


		void set(Value value)
		{
			(m_object->*m_setter)(value);
		}


	private:
		Obj* m_object;
		Getter m_getter;
		Setter m_setter;
		CreateEditor m_create_editor;
};


template <class T>
class InstanceObject : public PropertyViewObject
{
	public:
		typedef void(*CreateEditor)(QTreeWidgetItem*, T*);
	
		InstanceObject(const char* name, T* object, CreateEditor create_editor)
			: PropertyViewObject(name)
		{
			m_value = object;
			m_create_editor = create_editor;
		}


		virtual void createEditor(PropertyView&, QTreeWidgetItem* item) override
		{
			if (m_create_editor)
			{
				m_create_editor(item, m_value);
			}
		}

		virtual bool isEditable() const override { return false; }

	private:
		T* m_value;
		CreateEditor m_create_editor;
};



template<class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<int, T>&, int value)
{
	item->setText(1, QString::number(value));
}


template<class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<size_t, T>&, size_t value)
{
	item->setText(1, QString::number(value));
}


template<class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<float, T>&, float value)
{
	item->setText(1, QString::number(value));
}


template <class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<bool, T>& object, bool value)
{
	QCheckBox* checkbox = new QCheckBox();
	item->treeWidget()->setItemWidget(item, 1, checkbox);
	checkbox->setChecked(value);
	if (object.isEditable())
	{
		checkbox->connect(checkbox, &QCheckBox::stateChanged, [&object](bool new_state) {
			object.set(new_state);
		});
	}
	else
	{
		checkbox->setDisabled(true);
	}
}


void createEditor(QTreeWidgetItem* item, Lumix::Texture* texture)
{
	item->setText(1, texture->getPath().c_str());
}


void createEditor(QTreeWidgetItem* item, Lumix::Shader* shader)
{
	item->setText(1, shader->getPath().c_str());
}


void createEditor(QTreeWidgetItem* item, Lumix::Model* model)
{
	item->setText(1, model->getPath().c_str());
}


void createImageEditor(QTreeWidgetItem* item, Lumix::Texture* texture)
{
	QLabel* image_label = new QLabel();
	item->treeWidget()->setItemWidget(item, 1, image_label);
	QImage image(texture->getPath().c_str());
	image_label->setPixmap(QPixmap::fromImage(image).scaledToHeight(100));
	image_label->adjustSize();
}


void createEditor(QTreeWidgetItem* item, Lumix::Material* material)
{
	item->setText(1, material->getPath().c_str());
}


void createEditor(QTreeWidgetItem* item, Lumix::Mesh* mesh)
{
	item->setText(1, mesh->getName());
}


PropertyViewObject::~PropertyViewObject()
{
	for (int i = 0; i < m_members.size(); ++i)
	{
		delete m_members[i];
	}
}


void createComponentEditor(QTreeWidgetItem* item, Lumix::Component* )
{

}


PropertyViewObject* createComponentObject(Lumix::WorldEditor& editor, Lumix::Component cmp)
{
	const char* name = "";
	for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if (crc32(component_map[i + 1]) == cmp.type)
		{
			name = component_map[i];
		}
	}
	auto c = new Lumix::Component(cmp); TODO("memory leak");
	InstanceObject<Lumix::Component>* object = new InstanceObject<Lumix::Component>(name, c, &createComponentEditor);
	
	auto& descriptors = editor.getPropertyDescriptors(cmp.type);
	
	for (int i = 0; i < descriptors.size(); ++i)
	{
		auto prop = new ComponentPropertyObject(descriptors[i]->getName(), cmp, *descriptors[i]);
		object->addMember(prop);
	}

	return object;
}


PropertyViewObject* createEntityObject(Lumix::WorldEditor& editor, Lumix::Entity entity)
{
	auto e = new Lumix::Entity(entity); TODO("memory leak");
	InstanceObject<Lumix::Entity>* object = new InstanceObject<Lumix::Entity>("Entity", e, NULL);

	auto& cmps = e->getComponents();

	for (int i = 0; i < cmps.size(); ++i)
	{
		auto prop = createComponentObject(editor, cmps[i]);
		object->addMember(prop);
	}

	return object;
}


PropertyViewObject* createTextureObject(Lumix::Resource* resource)
{
	if (Lumix::Texture* texture = dynamic_cast<Lumix::Texture*>(resource))
	{
		InstanceObject<Lumix::Texture>* object = new InstanceObject<Lumix::Texture>("Texture", texture, &createEditor);

		auto prop = new GetterSetterObject<int, Lumix::Texture>("width", texture, &Lumix::Texture::getWidth, NULL, &createEditor);
		object->addMember(prop);
		
		prop = new GetterSetterObject<int, Lumix::Texture>("height", texture, &Lumix::Texture::getHeight, NULL, &createEditor);
		object->addMember(prop);

		InstanceObject<Lumix::Texture>* img_object = new InstanceObject<Lumix::Texture>("Image", texture, &createImageEditor);
		object->addMember(img_object);

		return object;
	}
	return NULL;
}


/*
class TerrainProxy
{
	public:
		TerrainProxy(Lumix::Component cmp)
			: m_component(cmp)
		{ }


		Lumix::Material* getMaterial()
		{
			return static_cast<Lumix::RenderScene*>(m_component.scene)->getTerrainMaterial(m_component);
		}


		void setMaterial(Lumix::Material* material)
		{
			static_cast<Lumix::RenderScene*>(m_component.scene)->setTerrainMaterial(m_component, material->getPath().c_str());
		}

	private:
		Lumix::Component m_component;
};


static PropertyViewObject* createTerrainObject(Lumix::Component component)
{
	TerrainProxy* proxy = new TerrainProxy(component); TODO("memory leak");

	InstanceObject<TerrainProxy>* object = new InstanceObject<TerrainProxy>("Terrain", proxy, NULL);
	
	PropertyViewObject* prop = new InstanceObject<Lumix::Material>("Material", proxy->getMaterial(), &createEditor);
	object->addMember(prop);
	
	prop = new DelegateObject<bool, Lumix::Material>("Z test", material, &Lumix::Material::isZTest, &createEditor);
	object->addMember(prop);

	prop = new DelegateObject<bool, Lumix::Material>("Backface culling", material, &Lumix::Material::isBackfaceCulling, &createEditor);
	object->addMember(prop);

	prop = new DelegateObject<bool, Lumix::Material>("Alpha to coverage", material, &Lumix::Material::isAlphaToCoverage, &createEditor);
	object->addMember(prop);

	for (int i = 0; i < material->getTextureCount(); ++i)
	{
		prop = createTextureObject(material->getTexture(i));
		object->addMember(prop);
	}

	return object;
}
*/


static PropertyViewObject* createMaterialObject(Lumix::Resource* resource)
{
	if (Lumix::Material* material = dynamic_cast<Lumix::Material*>(resource))
	{
		InstanceObject<Lumix::Material>* object = new InstanceObject<Lumix::Material>("Material", material, &createEditor);

		PropertyViewObject* prop = new InstanceObject<Lumix::Shader>("Shader", material->getShader(), &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>("Z test", material, &Lumix::Material::isZTest, &Lumix::Material::enableZTest, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>("Backface culling", material, &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>("Alpha to coverage", material, &Lumix::Material::isAlphaToCoverage, &Lumix::Material::enableAlphaToCoverage, &createEditor);
		object->addMember(prop);

		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			prop = createTextureObject(material->getTexture(i));
			object->addMember(prop);
		}

		return object;
	}
	return NULL;
}


PropertyViewObject* createModelObject(Lumix::Resource* resource)
{
	if (Lumix::Model* model = dynamic_cast<Lumix::Model*>(resource))
	{
		InstanceObject<Lumix::Model>* object = new InstanceObject<Lumix::Model>("Model", model, &createEditor);

		PropertyViewObject* prop = new GetterSetterObject<int, Lumix::Model>("Bone count", model, &Lumix::Model::getBoneCount, NULL, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<float, Lumix::Model>("Bounding radius", model, &Lumix::Model::getBoundingRadius, NULL, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<size_t, Lumix::Model>("Size (bytes)", model, &Lumix::Model::size, NULL, &createEditor);
		object->addMember(prop);

		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			Lumix::Mesh* mesh = &model->getMesh(i);
			InstanceObject<Lumix::Mesh>* mesh_object = new InstanceObject<Lumix::Mesh>("Mesh", mesh, &createEditor);
			object->addMember(mesh_object);

			prop = new GetterSetterObject<int, Lumix::Mesh>("Triangles", mesh, &Lumix::Mesh::getTriangleCount, NULL, &createEditor);
			mesh_object->addMember(prop);

			prop = createMaterialObject(mesh->getMaterial());
			mesh_object->addMember(prop);
		}

		return object;
	}
	return NULL;
}


class TerrainEditor : public Lumix::WorldEditor::Plugin
{
	public:
		enum Type
		{
			HEIGHT,
			TEXTURE
		};

		TerrainEditor(Lumix::WorldEditor& editor) 
			: m_world_editor(editor)
		{
			m_texture_tree_item = NULL;
			m_tree_top_level = NULL;
			m_terrain_brush_size = 10;
			m_terrain_brush_strength = 0.1f;
			m_texture_idx = 0;
		}

		virtual void tick() override
		{
			float mouse_x = m_world_editor.getMouseX();
			float mouse_y = m_world_editor.getMouseY();
			
			if (m_world_editor.getSelectedEntity().isValid())
			{
				Lumix::Component terrain = m_world_editor.getSelectedEntity().getComponent(crc32("terrain"));
				if (terrain.isValid())
				{
					Lumix::Component camera_cmp = m_world_editor.getEditCamera();
					Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.scene);
					Lumix::Vec3 origin, dir;
					scene->getRay(camera_cmp, (float)mouse_x, (float)mouse_y, origin, dir);
					Lumix::RayCastModelHit hit = scene->castRay(origin, dir, Lumix::Component::INVALID);
					if (hit.m_is_hit)
					{
						scene->setTerrainBrush(terrain, hit.m_origin + hit.m_dir * hit.m_t, m_terrain_brush_size);
						return;
					}
					scene->setTerrainBrush(terrain, Lumix::Vec3(0, 0, 0), 1);
				}
			}
		}

		virtual bool onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int) override
		{
			if (m_world_editor.getSelectedEntity() == hit.m_component.entity)
			{
				Lumix::Component terrain = hit.m_component.entity.getComponent(crc32("terrain"));
				if (terrain.isValid())
				{
					Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
					switch (m_type)
					{
						case HEIGHT:
							addTerrainLevel(terrain, hit);
							break;
						case TEXTURE:
							addSplatWeight(terrain, hit);
							break;
						default:
							ASSERT(false);
							break;
					}
					return true;
				}
			}
			return false;
		}

		virtual void onMouseMove(int x, int y, int /*rel_x*/, int /*rel_y*/, int /*mouse_flags*/) override
		{
			Lumix::Component terrain = m_world_editor.getSelectedEntity().getComponent(crc32("terrain"));
			Lumix::Component camera_cmp = m_world_editor.getEditCamera();
			Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.scene);
			Lumix::Vec3 origin, dir;
			scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
			Lumix::RayCastModelHit hit = scene->castRay(origin, dir, Lumix::Component::INVALID);
			switch (m_type)
			{
				case HEIGHT:
					if (hit.m_is_hit)
					{
						addTerrainLevel(terrain, hit);
					}
					break;
				case TEXTURE:
					if (hit.m_is_hit)
					{
						addSplatWeight(terrain, hit);
					}
					break;
				default:
					ASSERT(false);
					break;
			}
		}

		virtual void onMouseUp(int, int, Lumix::MouseButton::Value) override
		{
		}


		Lumix::Material* getMaterial()
		{
			Lumix::string material_path;
			static_cast<Lumix::RenderScene*>(m_component.scene)->getTerrainMaterial(m_component, material_path);
			return static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(material_path.c_str()));
		}


		void addSplatWeight(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
		{
			if (!terrain.isValid())
				return;
			float radius = (float)m_terrain_brush_size;
			float rel_amount = m_terrain_brush_strength;
			Lumix::string material_path;
			static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainMaterial(terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(material_path));
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			Lumix::Texture* splatmap = material->getTexture(material->getTextureCount() - 1);
			Lumix::Texture* heightmap = material->getTexture(0);
			Lumix::Matrix entity_mtx = terrain.entity.getMatrix();
			entity_mtx.fastInverse();
			Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
			float xz_scale;
			static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainXZScale(terrain, xz_scale);
			local_pos = local_pos / xz_scale;
			local_pos.x *= (float)splatmap->getWidth() / heightmap->getWidth();
			local_pos.z *= (float)splatmap->getHeight() / heightmap->getHeight();

			const float strengt_multiplicator = 1;

			int texture_idx = m_texture_idx;
			int w = splatmap->getWidth();
			if (splatmap->getBytesPerPixel() == 4)
			{
				int from_x = Lumix::Math::maxValue((int)(local_pos.x - radius), 0);
				int to_x = Lumix::Math::minValue((int)(local_pos.x + radius), splatmap->getWidth());
				int from_z = Lumix::Math::maxValue((int)(local_pos.z - radius), 0);
				int to_z = Lumix::Math::minValue((int)(local_pos.z + radius), splatmap->getHeight());

				float amount = rel_amount * 255 * strengt_multiplicator;
				amount = amount > 0 ? Lumix::Math::maxValue(amount, 1.1f) : Lumix::Math::minValue(amount, -1.1f);

				for (int i = from_x, end = to_x; i < end; ++i)
				{
					for (int j = from_z, end2 = to_z; j < end2; ++j)
					{
						float dist = sqrt((local_pos.x - i) * (local_pos.x - i) + (local_pos.z - j) * (local_pos.z - j));
						float add_rel = 1.0f - Lumix::Math::minValue(dist / radius, 1.0f);
						int add = add_rel * amount;
						if (rel_amount > 0)
						{
							add = Lumix::Math::minValue(add, 255 - splatmap->getData()[4 * (i + j * w) + texture_idx]);
						}
						else if (rel_amount < 0)
						{
							add = Lumix::Math::maxValue(add, 0 - splatmap->getData()[4 * (i + j * w) + texture_idx]);
						}
						addTexelSplatWeight(
							splatmap->getData()[4 * (i + j * w) + texture_idx]
							, splatmap->getData()[4 * (i + j * w) + (texture_idx + 1) % 4]
							, splatmap->getData()[4 * (i + j * w) + (texture_idx + 2) % 4]
							, splatmap->getData()[4 * (i + j * w) + (texture_idx + 3) % 4]
							, add
						);
					}
				}
			}
			else
			{
				ASSERT(false);
			}
			splatmap->onDataUpdated();
		}

		void addTexelSplatWeight(uint8_t& w1, uint8_t& w2, uint8_t& w3, uint8_t& w4, int value)
		{
			int add = value;
			add = Lumix::Math::minValue(add, 255 - w1);
			add = Lumix::Math::maxValue(add, -w1);
			w1 += add;
			/// TODO get rid of the Vec3 if possible
			Lumix::Vec3 v(w2, w3, w4);
			if (v.x + v.y + v.z == 0)
			{
				uint8_t rest = (255 - w1) / 3;
				w2 = rest;
				w3 = rest;
				w4 = rest;
			}
			else
			{
				v *= (255 - w1) / (v.x + v.y + v.z);
				w2 = v.x;
				w3 = v.y;
				w4 = v.z;
			}
			if (w1 + w2 + w3 + w4 > 255)
			{
				w4 = 255 - w1 - w2 - w3;
			}
		}

		void addTerrainLevel(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
		{
			float radius = (float)m_terrain_brush_size;
			float rel_amount = m_terrain_brush_strength;
			Lumix::string material_path;
			static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainMaterial(terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(material_path));
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			Lumix::Texture* heightmap = material->getTexture(0);
			Lumix::Matrix entity_mtx = terrain.entity.getMatrix();
			entity_mtx.fastInverse();
			Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
			float xz_scale;
			static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainXZScale(terrain, xz_scale);
			local_pos = local_pos / xz_scale;

			static const float strengt_multiplicator = 0.02f;

			int w = heightmap->getWidth();
			if (heightmap->getBytesPerPixel() == 4)
			{
				int from_x = Lumix::Math::maxValue((int)(local_pos.x - radius), 0);
				int to_x = Lumix::Math::minValue((int)(local_pos.x + radius), heightmap->getWidth());
				int from_z = Lumix::Math::maxValue((int)(local_pos.z - radius), 0);
				int to_z = Lumix::Math::minValue((int)(local_pos.z + radius), heightmap->getHeight());

				float amount = rel_amount * 255 * strengt_multiplicator;
				if (amount > 0)
				{
					amount = Lumix::Math::maxValue(amount, 1.1f);
				}
				else
				{
					amount = Lumix::Math::minValue(amount, -1.1f);
				}

				for (int i = from_x, end = to_x; i < end; ++i)
				{
					for (int j = from_z, end2 = to_z; j < end2; ++j)
					{
						float dist = sqrt((local_pos.x - i) * (local_pos.x - i) + (local_pos.z - j) * (local_pos.z - j));
						float add_rel = 1.0f - Lumix::Math::minValue(dist / radius, 1.0f);
						int add = add_rel * amount;
						if (rel_amount > 0)
						{
							add = Lumix::Math::minValue(add, 255 - heightmap->getData()[4 * (i + j * w)]);
						}
						else if (rel_amount < 0)
						{
							add = Lumix::Math::maxValue(add, 0 - heightmap->getData()[4 * (i + j * w)]);
						}
						heightmap->getData()[4 * (i + j * w)] += add;
						heightmap->getData()[4 * (i + j * w) + 1] += add;
						heightmap->getData()[4 * (i + j * w) + 2] += add;
						heightmap->getData()[4 * (i + j * w) + 3] += add;
					}
				}
			}
			else if (heightmap->getBytesPerPixel() == 2)
			{
				uint16_t* data = reinterpret_cast<uint16_t*>(heightmap->getData());
				int from_x = Lumix::Math::maxValue((int)(local_pos.x - radius), 0);
				int to_x = Lumix::Math::minValue((int)(local_pos.x + radius), heightmap->getWidth());
				int from_z = Lumix::Math::maxValue((int)(local_pos.z - radius), 0);
				int to_z = Lumix::Math::minValue((int)(local_pos.z + radius), heightmap->getHeight());

				float amount = rel_amount * (256 * 256 - 1) * strengt_multiplicator;

				for (int i = from_x, end = to_x; i < end; ++i)
				{
					for (int j = from_z, end2 = to_z; j < end2; ++j)
					{
						float dist = sqrt((local_pos.x - i) * (local_pos.x - i) + (local_pos.z - j) * (local_pos.z - j));
						float add_rel = 1.0f - Lumix::Math::minValue(dist / radius, 1.0f);
						uint16_t add = (uint16_t)(add_rel * amount);
						if (rel_amount > 0)
						{
							add = Lumix::Math::minValue(add, (uint16_t)((256 * 256 - 1) - data[i + j * w]));
						}
						else if ((uint16_t)(data[i + j * w] + add) > data[i + j * w])
						{
							add = (uint16_t)0 - data[i + j * w];
						}
						data[i + j * w] = data[i + j * w] + add;
					}
				}
			}
			else
			{
				ASSERT(false);
			}
			heightmap->onDataUpdated();
		}

		Lumix::WorldEditor& m_world_editor;
		Type m_type;
		QTreeWidgetItem* m_tree_top_level;
		Lumix::Component m_component;
		QTreeWidgetItem* m_texture_tree_item;
		float m_terrain_brush_strength;
		int m_terrain_brush_size;
		int m_texture_idx;
};


PropertyView::PropertyView(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::PropertyView)
	, m_terrain_editor(NULL)
	, m_is_updating_values(false)
	, m_selected_resource(NULL)
	, m_object(NULL)
{
	m_ui->setupUi(this);

	QStringList component_list;
	for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
	{
		component_list << component_map[j];
	}
	m_ui->componentTypeCombo->insertItems(0, component_list);

	addResourcePlugin(&createMaterialObject);
	addResourcePlugin(&createModelObject);
	addResourcePlugin(&createTextureObject);
}


PropertyView::~PropertyView()
{
	m_world_editor->entitySelected().unbind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().unbind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().unbind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	onUniverseCreated();
	delete m_terrain_editor;
	delete m_ui;
}


void PropertyView::onEntityPosition(Lumix::Entity& e)
{
	if (m_selected_entity == e)
	{
		Lumix::Vec3 pos = e.getPosition();
		m_ui->positionX->setValue(pos.x);
		m_ui->positionY->setValue(pos.y);
		m_ui->positionZ->setValue(pos.z);
	}
}



Lumix::WorldEditor* PropertyView::getWorldEditor()
{
	return m_world_editor;
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_terrain_editor = new TerrainEditor(editor);
	m_world_editor->addPlugin(m_terrain_editor);
	m_world_editor->entitySelected().bind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().bind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().bind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	if (m_world_editor->getEngine().getUniverse())
	{
		onUniverseCreated();
	}
}

void PropertyView::onUniverseCreated()
{
	m_world_editor->getEngine().getUniverse()->entityMoved().bind<PropertyView, &PropertyView::onEntityPosition>(this);
}

void PropertyView::onUniverseDestroyed()
{
	m_world_editor->getEngine().getUniverse()->entityMoved().unbind<PropertyView, &PropertyView::onEntityPosition>(this);
}


void PropertyView::setAssetBrowser(AssetBrowser& asset_browser)
{
	m_asset_browser = &asset_browser;
	connect(m_asset_browser, &AssetBrowser::fileSelected, this, &PropertyView::setSelectedResourceFilename);
}


void PropertyView::setSelectedResourceFilename(const char* filename)
{
	char rel_path[LUMIX_MAX_PATH];
	m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, filename);
	Lumix::ResourceManagerBase* manager = NULL;
	char extension[10];
	Lumix::PathUtils::getExtension(extension, sizeof(extension), filename);
	if (strcmp(extension, "msh") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MODEL);
	}
	else if (strcmp(extension, "mat") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL);
	}
	else if (strcmp(extension, "dds") == 0 || strcmp(extension, "tga") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::TEXTURE);
	}

	if (manager != NULL)
	{
		setSelectedResource(manager->load(rel_path));
	}
	else
	{
		setSelectedResource(NULL);
	}
}


void PropertyView::addResourcePlugin(PropertyViewObject::Creator plugin)
{
	m_resource_plugins.push(plugin);
}


void PropertyView::onSelectedResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		m_selected_entity = Lumix::Entity::INVALID;
		clear();
		for (int i = 0; i < m_resource_plugins.size(); ++i)
		{
			if (PropertyViewObject* object = m_resource_plugins[i](m_selected_resource))
			{
				setObject(object);
				return;
			}
		}
	}
}


void PropertyView::setScriptCompiler(ScriptCompiler* compiler)
{
	m_compiler = compiler;
	if(m_compiler)
	{
		m_compiler->onCompile().bind<PropertyView, &PropertyView::onScriptCompiled>(this);
	}
}


void PropertyView::clear()
{
	m_ui->propertyList->clear();

	delete m_object;
	m_object = NULL;
}


void PropertyView::setScriptStatus(uint32_t status)
{
	for(int i = 0; i < m_ui->propertyList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* item = m_ui->propertyList->topLevelItem(i);
		if(item->text(0) == "Script")
		{
			for(int j = 0; j < item->childCount(); ++j)
			{
				if(item->child(j)->text(0) == "Status")
				{
					switch(status)
					{
						case ScriptCompiler::SUCCESS:
							item->child(j)->setText(1, "Success");
							break;
						case ScriptCompiler::NOT_COMPILED:
							item->child(j)->setText(1, "Not compiled");
							break;
						case ScriptCompiler::UNKNOWN:
							item->child(j)->setText(1, "Unknown");
							break;
						case ScriptCompiler::FAILURE:
							item->child(j)->setText(1, "Failure");
							break;
						default:
							ASSERT(false);
							break;
					}
					
					return;
				}
			}
		}
	}
}


void PropertyView::onScriptCompiled(const Lumix::Path&, uint32_t status)
{
	setScriptStatus(status == 0 ? ScriptCompiler::SUCCESS : ScriptCompiler::FAILURE);
}


void PropertyView::on_animablePlayPause()
{
	Lumix::Component cmp = m_world_editor->getSelectedEntity().getComponent(crc32("animable"));
	if (cmp.isValid())
	{
		Lumix::AnimationScene* scene = static_cast<Lumix::AnimationScene*>(cmp.scene);
		scene->setManual(cmp, !scene->isManual(cmp));
	}

}


void PropertyView::on_animableTimeSet(int value)
{
	Lumix::Component cmp = m_world_editor->getSelectedEntity().getComponent(crc32("animable"));
	if (cmp.isValid())
	{
		static_cast<Lumix::AnimationScene*>(cmp.scene)->setAnimationFrame(cmp, value);
	}
}


void PropertyView::on_compileScriptClicked()
{
	/*for(int i = 0; i < m_properties.size(); ++i)
	{
		if(m_properties[i]->m_component_name == "script" && m_properties[i]->m_name == "source")
		{
			QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
			m_compiler->compile(edit->text().toLatin1().data());
			break;
		}
	}*/
}


void PropertyView::on_editScriptClicked()
{
	/*for(int i = 0; i < m_properties.size(); ++i)
	{
		if(m_properties[i]->m_name == "source")
		{
			QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
			QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
			break;
		}
	}*/
}


void PropertyView::addTerrainCustomProperties(const Lumix::Component& terrain_component)
{
	m_terrain_editor->m_tree_top_level = m_ui->propertyList->topLevelItem(0);
	m_terrain_editor->m_component = terrain_component;

	{
		QWidget* widget = new QWidget();
		QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Save");
		m_ui->propertyList->topLevelItem(0)->insertChild(0, item);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QPushButton* height_button = new QPushButton("Heightmap", widget);
		layout->addWidget(height_button);
		QPushButton* texture_button = new QPushButton("Splatmap", widget);
		layout->addWidget(texture_button);
		layout->setContentsMargins(2, 2, 2, 2);
		m_ui->propertyList->setItemWidget(item, 1, widget);
		connect(height_button, &QPushButton::clicked, this, &PropertyView::on_TerrainHeightSaveClicked);
		connect(texture_button, &QPushButton::clicked, this, &PropertyView::on_TerrainSplatSaveClicked);
	}

	QSlider* slider = new QSlider(Qt::Orientation::Horizontal);
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Brush size");
	m_ui->propertyList->topLevelItem(0)->insertChild(1, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(1);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, this, &PropertyView::on_terrainBrushSizeChanged);

	slider = new QSlider(Qt::Orientation::Horizontal);
	item = new QTreeWidgetItem(QStringList() << "Brush strength");
	m_ui->propertyList->topLevelItem(0)->insertChild(2, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(-100);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, this, &PropertyView::on_terrainBrushStrengthChanged);

	QWidget* widget = new QWidget();
	item = new QTreeWidgetItem(QStringList() << "Brush type");
	m_ui->propertyList->topLevelItem(0)->insertChild(3, item);
	QHBoxLayout* layout = new QHBoxLayout(widget);
	QPushButton* height_button = new QPushButton("Height", widget);
	layout->addWidget(height_button);
	QPushButton* texture_button = new QPushButton("Texture", widget);
	layout->addWidget(texture_button);
	layout->setContentsMargins(2, 2, 2, 2);
	m_ui->propertyList->setItemWidget(item, 1, widget);
	m_terrain_editor->m_type = TerrainEditor::HEIGHT;
	connect(height_button, &QPushButton::clicked, this, &PropertyView::on_TerrainHeightTypeClicked);
	connect(texture_button, &QPushButton::clicked, this, &PropertyView::on_TerrainTextureTypeClicked);

}


void PropertyView::on_TerrainHeightSaveClicked()
{
	Lumix::Material* material = m_terrain_editor->getMaterial();
	material->getTexture(0)->save();
}


void PropertyView::on_TerrainSplatSaveClicked()
{
	Lumix::Material* material = m_terrain_editor->getMaterial();
	material->getTexture(material->getTextureCount() - 1)->save();
}


void PropertyView::on_TerrainHeightTypeClicked()
{
	m_terrain_editor->m_type = TerrainEditor::HEIGHT;
	if (m_terrain_editor->m_texture_tree_item)
	{
		m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
	}
}

void PropertyView::on_TerrainTextureTypeClicked()
{
	m_terrain_editor->m_type = TerrainEditor::TEXTURE;

	QComboBox* combobox = new QComboBox();
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Texture");
	m_terrain_editor->m_tree_top_level->insertChild(4, item);
	Lumix::Material* material = m_terrain_editor->getMaterial();
	if (material && material->isReady())
	{
		for (int i = 1; i < material->getTextureCount() - 1; ++i)
		{
			combobox->addItem(material->getTexture(i)->getPath().c_str());
		}
	}
	m_ui->propertyList->setItemWidget(item, 1, combobox);

	m_terrain_editor->m_texture_tree_item = item;
	connect(combobox, (void (QComboBox::*)(int))&QComboBox::currentIndexChanged, this, &PropertyView::on_terrainBrushTextureChanged);
}

void PropertyView::on_terrainBrushTextureChanged(int value)
{
	m_terrain_editor->m_texture_idx = value;
}

void PropertyView::on_terrainBrushStrengthChanged(int value)
{
	m_terrain_editor->m_terrain_brush_strength = value / 100.0f;
}


void PropertyView::on_terrainBrushSizeChanged(int value)
{
	m_terrain_editor->m_terrain_brush_size = value;
}


void PropertyView::addAnimableCustomProperties(const Lumix::Component& cmp)
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	m_ui->propertyList->topLevelItem(0)->insertChild(0, tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Play/Pause", widget);
	QSlider* slider = new QSlider(Qt::Orientation::Horizontal, widget);
	slider->setObjectName("animation_frame_slider");
	slider->setMinimum(0);
	int frame_count = static_cast<Lumix::AnimationScene*>(cmp.scene)->getFrameCount(cmp);
	slider->setMaximum(frame_count);
	layout->addWidget(compile_button);
	layout->addWidget(slider);
	m_ui->propertyList->setItemWidget(tools_item, 1, widget);
	connect(compile_button, &QPushButton::clicked, this, &PropertyView::on_animablePlayPause);
	connect(slider, &QSlider::valueChanged, this, &PropertyView::on_animableTimeSet);
}


void PropertyView::addScriptCustomProperties()
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	m_ui->propertyList->topLevelItem(0)->insertChild(0, tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Compile", widget);
	QPushButton* edit_button = new QPushButton("Edit", widget);
	layout->addWidget(compile_button);
	layout->addWidget(edit_button);
	m_ui->propertyList->setItemWidget(tools_item, 1, widget);
	connect(compile_button, &QPushButton::clicked, this, &PropertyView::on_compileScriptClicked);
	connect(edit_button, &QPushButton::clicked, this, &PropertyView::on_editScriptClicked);

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	m_ui->propertyList->topLevelItem(0)->insertChild(0, status_item);
	status_item->setText(1, "Unknown");
}


void PropertyView::setSelectedResource(Lumix::Resource* resource)
{
	clear();
	if (m_selected_resource)
	{
		m_selected_resource->getObserverCb().unbind<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
	m_selected_resource = resource;
	if (resource)
	{
		m_selected_resource->getObserverCb().bind<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
		if (m_selected_resource->isReady())
		{
			onSelectedResourceLoaded(Lumix::Resource::State::READY, Lumix::Resource::State::READY);
		}
	}
}


void PropertyView::onEntitySelected(Lumix::Entity& e)
{
	setSelectedResource(NULL);
	m_selected_entity = e;
	/// TODO miki
	clear();
	if (e.isValid())
	{
		setObject(createEntityObject(*m_world_editor, e));
		/*
				addScriptCustomProperties();
				addTerrainCustomProperties(cmps[i]);
				addAnimableCustomProperties(cmps[i]);
		*/
		m_ui->propertyList->expandAll();
		onEntityPosition(e);
		updateValues();
	}
}

void PropertyView::updateValues()
{
	//ASSERT(false);
	TODO("");
}


void PropertyView::on_addComponentButton_clicked()
{
	QByteArray s = m_ui->componentTypeCombo->currentText().toLocal8Bit();
	const char* c = s.data();

	for(int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if(strcmp(c, component_map[i]) == 0)
		{
			m_world_editor->addComponent(crc32(component_map[i+1]));
			return;
		}
	}
	ASSERT(false); // unknown component type
}

void PropertyView::updateSelectedEntityPosition()
{
	m_world_editor->getSelectedEntity().setPosition(Lumix::Vec3((float)m_ui->positionX->value(), (float)m_ui->positionY->value(), (float)m_ui->positionZ->value()));
}

void PropertyView::on_positionX_valueChanged(double)
{
	updateSelectedEntityPosition();
}

void PropertyView::on_positionY_valueChanged(double)
{
	updateSelectedEntityPosition();
}

void PropertyView::on_positionZ_valueChanged(double)
{
	updateSelectedEntityPosition();
}


void PropertyView::createObjectEditor(QTreeWidgetItem* item, PropertyViewObject* object)
{
	item->setText(0, object->getName());
	object->createEditor(*this, item);

	PropertyViewObject** properties = object->getMembers();
	for (int i = 0; i < object->getMemberCount(); ++i)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->insertChild(0, subitem);

		createObjectEditor(subitem, properties[i]);
	}
}


void PropertyView::setObject(PropertyViewObject* object)
{
	clear();

	m_object = object;
	
	QTreeWidgetItem* item = new QTreeWidgetItem();
	m_ui->propertyList->insertTopLevelItem(0, item);
	createObjectEditor(item, object);

	m_ui->propertyList->expandAll();
	m_ui->propertyList->resizeColumnToContents(0);
}

void PropertyView::on_propertyList_customContextMenuRequested(const QPoint &pos)
{
	QMenu* menu = new QMenu("Item actions", NULL);
	const QModelIndex& index = m_ui->propertyList->indexAt(pos);
	if (index.isValid() && index.parent().isValid() && !index.parent().parent().isValid() && m_selected_entity.isValid())
	{
		QAction* remove_component_action = new QAction("Remove component", menu);
		menu->addAction(remove_component_action);
		QAction* action = menu->exec(m_ui->propertyList->mapToGlobal(pos));
		if (action == remove_component_action)
		{
			uint32_t cmp_hash = 0;
			QByteArray label = m_ui->propertyList->itemAt(pos)->text(0).toLatin1();
			for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
			{
				if (strcmp(component_map[i], label.data()) == 0)
				{
					cmp_hash = crc32(component_map[i + 1]);
					break;
				}
			}
			const Lumix::Entity::ComponentList& cmps = m_selected_entity.getComponents();
			for (int i = 0, c = cmps.size(); i < c; ++i)
			{
				if (cmps[i].type == cmp_hash)
				{
					Lumix::Entity entity = cmps[i].entity;
					m_world_editor->destroyComponent(cmps[i]);
					break;
				}
			}
		}
	}
}
