#include "property_view.h"
#include "ui_property_view.h"
#include "animation/animation_system.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/material.h"
#include "graphics/render_scene.h"
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


static const char* component_map[] =
{
	"Animable", "animable",
	"Camera", "camera",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Point Light", "light",
	"Renderable", "renderable",
	"Script", "script",
	"Terrain", "terrain"
};


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
					Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.system);
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
			Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.system);
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
			static_cast<Lumix::RenderScene*>(m_component.system)->getTerrainMaterial(m_component, material_path);
			return static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(material_path.c_str()));
		}


		void addSplatWeight(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
		{
			if (!terrain.isValid())
				return;
			float radius = (float)m_terrain_brush_size;
			float rel_amount = m_terrain_brush_strength;
			Lumix::string material_path;
			static_cast<Lumix::RenderScene*>(terrain.system)->getTerrainMaterial(terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(material_path));
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			Lumix::Texture* splatmap = material->getTexture(material->getTextureCount() - 1);
			Lumix::Texture* heightmap = material->getTexture(0);
			Lumix::Matrix entity_mtx = terrain.entity.getMatrix();
			entity_mtx.fastInverse();
			Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
			float xz_scale;
			static_cast<Lumix::RenderScene*>(terrain.system)->getTerrainXZScale(terrain, xz_scale);
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
			static_cast<Lumix::RenderScene*>(terrain.system)->getTerrainMaterial(terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(material_path));
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			Lumix::Texture* heightmap = material->getTexture(0);
			Lumix::Matrix entity_mtx = terrain.entity.getMatrix();
			entity_mtx.fastInverse();
			Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
			float xz_scale;
			static_cast<Lumix::RenderScene*>(terrain.system)->getTerrainXZScale(terrain, xz_scale);
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
{
	m_ui->setupUi(this);

	QStringList component_list;
	for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
	{
		component_list << component_map[j];
	}
	
	m_ui->componentTypeCombo->insertItems(0, component_list);
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

void PropertyView::on_checkboxStateChanged()
{
	if (!m_is_updating_values)
	{
		QCheckBox* cb = qobject_cast<QCheckBox*>(QObject::sender());
		int i = cb->property("cpp_prop").toInt();
		bool b = cb->isChecked();
		m_world_editor->setProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), &b, sizeof(b));
	}
}


void PropertyView::on_vec3ValueChanged()
{
	if (!m_is_updating_values)
	{
		QDoubleSpinBox* sb = qobject_cast<QDoubleSpinBox*>(QObject::sender());
		int i = sb->property("cpp_prop").toInt();
		Lumix::Vec3 v;
		v.x = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item->child(0), 1))->value();
		v.y = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item->child(1), 1))->value();
		v.z = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item->child(2), 1))->value();
		m_world_editor->setProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), &v, sizeof(v));
	}
}

void PropertyView::on_doubleSpinBoxValueChanged()
{
	if (!m_is_updating_values)
	{
		QDoubleSpinBox* sb = qobject_cast<QDoubleSpinBox*>(QObject::sender());
		int i = sb->property("cpp_prop").toInt();
		float f = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1))->value();
		m_world_editor->setProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), &f, sizeof(f));
	}
}

void PropertyView::on_lineEditEditingFinished()
{
	if (!m_is_updating_values)
	{
		QLineEdit* edit = qobject_cast<QLineEdit*>(QObject::sender());
		int i = edit->property("cpp_prop").toInt();
		QByteArray byte_array = edit->text().toLatin1();
		const char* text = byte_array.data();
		m_world_editor->setProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), text, byte_array.size());
	}
}

void PropertyView::on_browseFilesClicked()
{
	QPushButton* button = qobject_cast<QPushButton*>(QObject::sender());
	int i = button->property("cpp_prop").toInt();
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), m_properties[i]->m_file_type.c_str());
	
	QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
	edit->setText(str);
	m_world_editor->setProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), edit->text().toLocal8Bit().data(), edit->text().size());
}

void PropertyView::onPropertyValue(Property* property, const void* data, int32_t)
{
	m_is_updating_values = true;
	if(property->m_component_name == "script" && property->m_name == "source")
	{
		setScriptStatus(m_compiler->getStatus(static_cast<const char*>(data)));
	}

	switch(property->m_type)
	{
		case Property::VEC3:
			{
				Lumix::Vec3 v = *(Lumix::Vec3*)data;
				QString text;
				text.sprintf("[%f; %f; %f]", v.x, v.y, v.z);
				property->m_tree_item->setText(1, text);
				QTreeWidgetItem* item = property->m_tree_item->child(0);
				qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(item, 1))->setValue(v.x);
				item = property->m_tree_item->child(1);
				qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(item, 1))->setValue(v.y);
				item = property->m_tree_item->child(2);
				qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(item, 1))->setValue(v.z);
			}
			break;
		case Property::BOOL:
			{
				bool b = *(bool*)data; 
				QCheckBox* cb = qobject_cast<QCheckBox*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1));
				cb->setChecked(b);
			}
			break;
		case Property::FILE:
			{
				QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1)->children()[0]);
				edit->setText((char*)data);
			}
			break;
		case Property::STRING:
			{
				QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1));
				edit->setText((char*)data);
			}
			break;
		case Property::DECIMAL:
			{
				QDoubleSpinBox* edit = qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1));
				float f = *(float*)data;
				edit->setValue(f);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
	m_is_updating_values = false;
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


class FileEdit : public QLineEdit
{
	public:
		FileEdit(QWidget* parent, PropertyView* property_view)
			: QLineEdit(parent)
			, m_property_view(property_view)
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


void PropertyView::addProperty(const char* component, const char* name, const char* label, Property::Type type, const char* file_type)
{
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << label);
	m_ui->propertyList->topLevelItem(0)->insertChild(0, item);
	Property* prop = new Property();
	prop->m_component_name = component;
	prop->m_component = crc32(component);
	prop->m_name = name;
	prop->m_file_type = file_type;
	prop->m_name_hash = crc32(name);
	prop->m_type = type;
	prop->m_tree_item = item;
	m_properties.push(prop);
	switch(type)
	{
		case Property::BOOL:
			{
				QCheckBox* checkbox = new QCheckBox();
				m_ui->propertyList->setItemWidget(item, 1, checkbox);
				connect(checkbox, &QCheckBox::stateChanged, this, &PropertyView::on_checkboxStateChanged);
				checkbox->setProperty("cpp_prop", (int)(m_properties.size() - 1));
			}
			break;
		case Property::VEC3:
			{
				item->setText(1, "");
				QDoubleSpinBox* sb = new QDoubleSpinBox();
				item->insertChild(0, new QTreeWidgetItem(QStringList() << "x"));
				m_ui->propertyList->setItemWidget(item->child(0), 1, sb);
				sb->setProperty("cpp_prop", (int)(m_properties.size() - 1));
				connect(sb, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_vec3ValueChanged);

				sb = new QDoubleSpinBox();
				item->insertChild(1, new QTreeWidgetItem(QStringList() << "y"));
				m_ui->propertyList->setItemWidget(item->child(1), 1, sb);
				sb->setProperty("cpp_prop", (int)(m_properties.size() - 1));
				connect(sb, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_vec3ValueChanged);

				sb = new QDoubleSpinBox();
				item->insertChild(2, new QTreeWidgetItem(QStringList() << "z"));
				m_ui->propertyList->setItemWidget(item->child(2), 1, sb);
				sb->setProperty("cpp_prop", (int)(m_properties.size() - 1));
				connect(sb, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_vec3ValueChanged);
			}
			break;
		case Property::FILE:
			{
				QWidget* widget = new QWidget();
				FileEdit* edit = new FileEdit(widget, this);
				edit->setServer(m_world_editor);
				edit->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				QHBoxLayout* layout = new QHBoxLayout(widget);
				layout->addWidget(edit);
				layout->setContentsMargins(0, 0, 0, 0);
				QPushButton* button = new QPushButton("...", widget);
				layout->addWidget(button);
				button->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				connect(button, &QPushButton::clicked, this, &PropertyView::on_browseFilesClicked);
				m_ui->propertyList->setItemWidget(item, 1, widget);
				connect(edit, &QLineEdit::editingFinished, this, &PropertyView::on_lineEditEditingFinished);
			}
			break;
		case Property::STRING:
			{
				QLineEdit* edit = new QLineEdit();
				edit->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				m_ui->propertyList->setItemWidget(item, 1, edit);
				connect(edit, &QLineEdit::editingFinished, this, &PropertyView::on_lineEditEditingFinished);
			}
			break;
		case Property::DECIMAL:
			{
				QDoubleSpinBox* edit = new QDoubleSpinBox();
				edit->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				m_ui->propertyList->setItemWidget(item, 1, edit);
				edit->setMaximum(FLT_MAX);
				connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_doubleSpinBoxValueChanged);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
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
	for(int i = 0; i < m_properties.size(); ++i)
	{
		delete m_properties[i];
	}
	m_properties.clear();
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
		Lumix::AnimationSystem* anim_system = static_cast<Lumix::AnimationSystem*>(cmp.system);
		anim_system->setManual(cmp, !anim_system->isManual(cmp));
	}

}


void PropertyView::on_animableTimeSet(int value)
{
	Lumix::Component cmp = m_world_editor->getSelectedEntity().getComponent(crc32("animable"));
	if (cmp.isValid())
	{
		static_cast<Lumix::AnimationSystem*>(cmp.system)->setAnimationFrame(cmp, value);
	}
}


void PropertyView::on_compileScriptClicked()
{
	for(int i = 0; i < m_properties.size(); ++i)
	{
		if(m_properties[i]->m_component_name == "script" && m_properties[i]->m_name == "source")
		{
			QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
			m_compiler->compile(edit->text().toLatin1().data());
			break;
		}
	}
}


void PropertyView::on_editScriptClicked()
{
	for(int i = 0; i < m_properties.size(); ++i)
	{
		if(m_properties[i]->m_name == "source")
		{
			QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
			QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
			break;
		}
	}
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
	int frame_count = static_cast<Lumix::AnimationSystem*>(cmp.system)->getFrameCount(cmp);
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


void PropertyView::onEntitySelected(Lumix::Entity& e)
{
	m_selected_entity = e;
	/// TODO miki
	clear();
	if (e.isValid())
	{
		const Lumix::Entity::ComponentList& cmps = e.getComponents();
		for (int i = 0; i < cmps.size(); ++i)
		{
			for (int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
			{
				if (cmps[i].type == crc32(component_map[j + 1]))
				{
					m_ui->propertyList->insertTopLevelItem(0, new QTreeWidgetItem());
					m_ui->propertyList->topLevelItem(0)->setText(0, component_map[j]);
					break;
				}
			}
			/// TODO refactor
			if (cmps[i].type == crc32("box_rigid_actor"))
			{
				addProperty("box_rigid_actor", "size", "Size", Property::VEC3, NULL);
				addProperty("box_rigid_actor", "dynamic", "Is dynamic", Property::BOOL, NULL);
			}
			else if (cmps[i].type == crc32("renderable"))
			{
				addProperty("renderable", "source", "Source", Property::FILE, "models (*.msh)");
			}
			else if (cmps[i].type == crc32("script"))
			{
				addProperty("script", "source", "Source", Property::FILE, "scripts (*.cpp)");
				addScriptCustomProperties();
			}
			else if (cmps[i].type == crc32("camera"))
			{
				addProperty("camera", "slot", "Slot", Property::STRING, NULL);
				addProperty("camera", "near", "Near", Property::DECIMAL, NULL);
				addProperty("camera", "far", "Far", Property::DECIMAL, NULL);
				addProperty("camera", "fov", "Field of view", Property::DECIMAL, NULL);
			}
			else if (cmps[i].type == crc32("terrain"))
			{
				addProperty("terrain", "material", "Material", Property::FILE, "material (*.mat)");
				addProperty("terrain", "xz_scale", "Meter per texel", Property::DECIMAL, NULL);
				addProperty("terrain", "y_scale", "Height scale", Property::DECIMAL, NULL);
				addProperty("terrain", "grass_mesh", "Grass mesh", Property::FILE, "mesh (*.msh)");
				addTerrainCustomProperties(cmps[i]);
			}
			else if (cmps[i].type == crc32("mesh_rigid_actor"))
			{
				addProperty("mesh_rigid_actor", "source", "Source", Property::FILE, "Physics (*.pda)");
			}
			else if (cmps[i].type == crc32("physical_controller"))
			{
			}
			else if (cmps[i].type == crc32("physical_heightfield"))
			{
				addProperty("physical_heightfield", "heightmap", "Heightmap", Property::FILE, "Heightmaps (*.tga *.raw)");
				addProperty("physical_heightfield", "xz_scale", "Meters per pixel", Property::DECIMAL, NULL);
				addProperty("physical_heightfield", "y_scale", "Height scale", Property::DECIMAL, NULL);
			}
			else if (cmps[i].type == crc32("light"))
			{
			}
			else if (cmps[i].type == crc32("animable"))
			{
				addProperty("animable", "preview", "Preview animation", Property::FILE, "Animation (*.ani)");
				addAnimableCustomProperties(cmps[i]);
			}
			else
			{
				ASSERT(false);
			}
			m_ui->propertyList->expandAll();
		}
		onEntityPosition(e);
		updateValues();
	}
}

void PropertyView::updateValues()
{
	if (m_selected_entity.isValid())
	{
		for (int i = 0; i < m_properties.size(); ++i)
		{
			Lumix::Blob stream;
			const Lumix::IPropertyDescriptor& prop = m_world_editor->getPropertyDescriptor(m_properties[i]->m_component, m_properties[i]->m_name_hash);
			prop.get(m_selected_entity.getComponent(m_properties[i]->m_component), stream);
			onPropertyValue(m_properties[i], stream.getBuffer(), stream.getBufferSize());
		}
		Lumix::Component animable = m_selected_entity.getComponent(crc32("animable"));
		if (animable.isValid())
		{
			int frame_count = static_cast<Lumix::AnimationSystem*>(animable.system)->getFrameCount(animable);
			m_ui->propertyList->findChild<QSlider*>("animation_frame_slider")->setMaximum(frame_count);
		}
	}
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

void PropertyView::on_propertyList_customContextMenuRequested(const QPoint &pos)
{
	QMenu* menu = new QMenu("Item actions", NULL);
	const QModelIndex& index = m_ui->propertyList->indexAt(pos);
	if (index.isValid() && !index.parent().isValid() && m_selected_entity.isValid())
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
