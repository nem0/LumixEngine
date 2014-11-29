#include "property_view.h"
#include "ui_property_view.h"
#include "animation/animation_system.h"
#include "assetbrowser.h"
#include "core/aabb.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "script/script_system.h"
#include "scripts/scriptcompiler.h"
#include <qcheckbox.h>
#include <QColorDialog>
#include <qdesktopservices.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <qfiledialog.h>
#include <qmenu.h>
#include <qmessagebox.h>
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



static const uint32_t RENDERABLE_HASH = crc32("renderable");


class AddTerrainLevelCommand : public Lumix::IEditorCommand
{
	private:
		struct Item
		{
			int m_texture_center_x;
			int m_texture_center_y;
			int m_texture_radius;
			float m_amount;
		};


	public:
		struct Rectangle
		{
			int m_from_x;
			int m_from_y;
			int m_to_x;
			int m_to_y;
		};


		AddTerrainLevelCommand(Lumix::WorldEditor& editor, const Lumix::Vec3& hit_pos, float radius, float rel_amount, Lumix::Component terrain, bool can_be_merged)
			: m_world_editor(editor)
			, m_terrain(terrain)
			, m_can_be_merged(can_be_merged)
			, m_new_data(editor.getAllocator())
			, m_old_data(editor.getAllocator())
			, m_items(editor.getAllocator())
		{
			Lumix::Matrix entity_mtx = terrain.entity.getMatrix();
			entity_mtx.fastInverse();
			Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
			float xz_scale = static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainXZScale(terrain);
			local_pos = local_pos / xz_scale;

			Item& item = m_items.pushEmpty();
			item.m_texture_center_x = local_pos.x;
			item.m_texture_center_y = local_pos.z;
			item.m_texture_radius = radius;
			item.m_amount = rel_amount;
		}


		Lumix::Texture* getHeightmap()
		{
			Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
			Lumix::string material_path(allocator);
			static_cast<Lumix::RenderScene*>(m_terrain.scene)->getTerrainMaterial(m_terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(Lumix::Path(material_path.c_str())));
			return material->getTextureByUniform("hm_texture");
		}


		void rasterItem(Lumix::Texture* heightmap, Lumix::Array<uint8_t>& data, Item& item)
		{
			int heightmap_width = heightmap->getWidth();
			int from_x = Lumix::Math::maxValue((int)(item.m_texture_center_x - item.m_texture_radius), 0);
			int to_x = Lumix::Math::minValue((int)(item.m_texture_center_x + item.m_texture_radius), heightmap_width);
			int from_z = Lumix::Math::maxValue((int)(item.m_texture_center_y - item.m_texture_radius), 0);
			int to_z = Lumix::Math::minValue((int)(item.m_texture_center_y + item.m_texture_radius), heightmap_width);

			static const float STRENGTH_MULTIPLICATOR = 100.0f;

			float amount = item.m_amount * STRENGTH_MULTIPLICATOR;

			float radius = item.m_texture_radius;
			for (int i = from_x, end = to_x; i < end; ++i)
			{
				for (int j = from_z, end2 = to_z; j < end2; ++j)
				{
					float dist = sqrt((item.m_texture_center_x - i) * (item.m_texture_center_x - i) + (item.m_texture_center_y - j) * (item.m_texture_center_y - j));
					float add_rel = 1.0f - Lumix::Math::minValue(dist / radius, 1.0f);
					int add = add_rel * amount;
					if (item.m_amount > 0)
					{
						add = Lumix::Math::minValue(add, 255 - heightmap->getData()[4 * (i + j * heightmap_width)]);
					}
					else if (item.m_amount < 0)
					{
						add = Lumix::Math::maxValue(add, 0 - heightmap->getData()[4 * (i + j * heightmap_width)]);
					}
					data[(i - m_x + (j - m_y) * m_width) * 4] += add;
					data[(i - m_x + (j - m_y) * m_width) * 4 + 1] += add;
					data[(i - m_x + (j - m_y) * m_width) * 4 + 2] += add;
					data[(i - m_x + (j - m_y) * m_width) * 4 + 3] += add;
				}
			}
		}


		void generateNewData()
		{
			auto heightmap = getHeightmap();
			ASSERT(heightmap->getBytesPerPixel() == 4);
			Rectangle rect;
			getBoundingRectangle(heightmap, rect);
			m_new_data.resize(heightmap->getBytesPerPixel() * (rect.m_to_x - rect.m_from_x) * (rect.m_to_y - rect.m_from_y));
			memcpy(&m_new_data[0], &m_old_data[0], m_new_data.size());

			for(int item_index = 0; item_index < m_items.size(); ++item_index)
			{
				Item& item = m_items[item_index];
				rasterItem(heightmap, m_new_data, item);
			}
		}


		void saveOldData()
		{
			auto heightmap = getHeightmap();
			Rectangle rect;
			getBoundingRectangle(heightmap, rect);
			m_x = rect.m_from_x;
			m_y = rect.m_from_y;
			m_width = rect.m_to_x - rect.m_from_x;
			m_height = rect.m_to_y - rect.m_from_y;
			m_old_data.resize(4 * (rect.m_to_x - rect.m_from_x) * (rect.m_to_y - rect.m_from_y));

			ASSERT(heightmap->getBytesPerPixel() == 4);

			int index = 0;
			for (int j = rect.m_from_y, end2 = rect.m_to_y; j < end2; ++j)
			{
				for (int i = rect.m_from_x, end = rect.m_to_x; i < end; ++i)
				{
					uint32_t pixel = *(uint32_t*)&heightmap->getData()[(i + j * heightmap->getWidth()) * 4];
					*(uint32_t*)&m_old_data[index] = pixel;
					index += 4;
				}
			}
		}


		void applyData(Lumix::Array<uint8_t>& data)
		{
			auto heightmap = getHeightmap();

			for(int j = m_y; j < m_y + m_height; ++j)
			{
				for(int i = m_x; i < m_x + m_width; ++i)
				{
					int index = 4 * (i + j * heightmap->getWidth());
					heightmap->getData()[index + 0] = data[4 * (i - m_x + (j - m_y) * m_width) + 0];
					heightmap->getData()[index + 1] = data[4 * (i - m_x + (j - m_y) * m_width) + 1];
					heightmap->getData()[index + 2] = data[4 * (i - m_x + (j - m_y) * m_width) + 2];
					heightmap->getData()[index + 3] = data[4 * (i - m_x + (j - m_y) * m_width) + 3];
				}
			}
			heightmap->onDataUpdated();
		}


		virtual void execute() override
		{
			if(m_new_data.empty())
			{
				saveOldData();
				generateNewData();
			}
			applyData(m_new_data);
		}


		virtual void undo() override
		{
			applyData(m_old_data);
		}
					

		virtual uint32_t getType() override
		{
			static const uint32_t type = crc32("add_terrain_level");
			return type;
		}


		void resizeData()
		{
			Lumix::Array<uint8_t> new_data(m_world_editor.getAllocator());
			Lumix::Array<uint8_t> old_data(m_world_editor.getAllocator());
			auto heightmap = getHeightmap();
			Rectangle rect;
			getBoundingRectangle(heightmap, rect);

			int new_w = rect.m_to_x - rect.m_from_x;
			new_data.resize(heightmap->getBytesPerPixel() * new_w * (rect.m_to_y - rect.m_from_y));
			old_data.resize(heightmap->getBytesPerPixel() * new_w * (rect.m_to_y - rect.m_from_y));

			// original
			for(int row = rect.m_from_y; row < rect.m_to_y; ++row)
			{
				memcpy(&new_data[(row - rect.m_from_y) * new_w * 4], &heightmap->getData()[row * 4 * heightmap->getWidth() + rect.m_from_x * 4], 4 * new_w);
				memcpy(&old_data[(row - rect.m_from_y) * new_w * 4], &heightmap->getData()[row * 4 * heightmap->getWidth() + rect.m_from_x * 4], 4 * new_w);
			}

			// new
			for(int row = 0; row < m_height; ++row)
			{
				memcpy(&new_data[((row + m_y - rect.m_from_y) * new_w + m_x - rect.m_from_x) * 4], &m_new_data[row * 4 * m_width], 4 * m_width);
				memcpy(&old_data[((row + m_y - rect.m_from_y) * new_w + m_x - rect.m_from_x) * 4], &m_old_data[row * 4 * m_width], 4 * m_width);
			}

			m_x = rect.m_from_x;
			m_y = rect.m_from_y;
			m_height = rect.m_to_y - rect.m_from_y;
			m_width = rect.m_to_x - rect.m_from_x;
			
			m_new_data.swap(new_data);
			m_old_data.swap(old_data);
		}


		virtual bool merge(IEditorCommand& command) override
		{
			if(!m_can_be_merged)
			{
				return false;
			}
			AddTerrainLevelCommand& my_command = static_cast<AddTerrainLevelCommand&>(command);
			if(m_terrain == my_command.m_terrain)
			{
				my_command.m_items.push(m_items.back());
				my_command.resizeData();
				my_command.rasterItem(getHeightmap(), my_command.m_new_data, m_items.back());
				return true;
			}
			return false;
		}


	private:
		void getBoundingRectangle(Lumix::Texture* heightmap, Rectangle& rect)
		{
			Item& item = m_items[0];
			rect.m_from_x = Lumix::Math::maxValue(item.m_texture_center_x - item.m_texture_radius, 0);
			rect.m_to_x = Lumix::Math::minValue(item.m_texture_center_x + item.m_texture_radius, heightmap->getWidth());
			rect.m_from_y = Lumix::Math::maxValue(item.m_texture_center_y - item.m_texture_radius, 0);
			rect.m_to_y = Lumix::Math::minValue(item.m_texture_center_y + item.m_texture_radius, heightmap->getHeight());
			for(int i = 1; i < m_items.size(); ++i)
			{
				Item& item = m_items[i];
				rect.m_from_x = Lumix::Math::minValue(item.m_texture_center_x - item.m_texture_radius, rect.m_from_x);
				rect.m_to_x = Lumix::Math::maxValue(item.m_texture_center_x + item.m_texture_radius, rect.m_to_x);
				rect.m_from_y = Lumix::Math::minValue(item.m_texture_center_y - item.m_texture_radius, rect.m_from_y);
				rect.m_to_y = Lumix::Math::maxValue(item.m_texture_center_y + item.m_texture_radius, rect.m_to_y);
			}
			rect.m_from_x = Lumix::Math::maxValue(rect.m_from_x, 0);
			rect.m_to_x = Lumix::Math::minValue(rect.m_to_x, heightmap->getWidth());
			rect.m_from_y = Lumix::Math::maxValue(rect.m_from_y, 0);
			rect.m_to_y = Lumix::Math::minValue(rect.m_to_y, heightmap->getHeight());
		}


	private:
		Lumix::Array<uint8_t> m_new_data;
		Lumix::Array<uint8_t> m_old_data;
		int m_width;
		int m_height;
		int m_x;
		int m_y;
		Lumix::Array<Item> m_items;
		Lumix::Component m_terrain;
		Lumix::WorldEditor& m_world_editor;
		bool m_can_be_merged;
};



static const uint32_t TERRAIN_HASH = crc32("terrain");
static const uint32_t SCRIPT_HASH = crc32("script");


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


class ComponentArrayItemObject : public PropertyViewObject
{
	public:
		ComponentArrayItemObject(PropertyViewObject* parent, const char* name, Lumix::IArrayDescriptor& descriptor, Lumix::Component component, int index)
			: PropertyViewObject(parent, name)
			, m_descriptor(descriptor)
			, m_component(component)
			, m_index(index)
		{
		}

		virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
		{
			QWidget* widget = new QWidget();
			QHBoxLayout* layout = new QHBoxLayout(widget);
			layout->setContentsMargins(0, 0, 0, 0);
			QPushButton* button = new QPushButton(" - ");
			layout->addWidget(button);
			layout->addStretch(1);
			item->treeWidget()->setItemWidget(item, 1, widget);
			button->connect(button, &QPushButton::clicked, [this, &view]()
			{
				view.getWorldEditor()->removeArrayPropertyItem(m_component, m_index, m_descriptor);
				view.refresh();
			});
		}

		virtual bool isEditable() const override
		{
			return false;
		}

	private:
		Lumix::IArrayDescriptor& m_descriptor;
		Lumix::Component m_component;
		int m_index;
};


class ComponentPropertyObject : public PropertyViewObject
{
	public:
		ComponentPropertyObject(PropertyViewObject* parent, const char* name, Lumix::Component cmp, Lumix::IPropertyDescriptor& descriptor)
			: PropertyViewObject(parent, name)
			, m_descriptor(descriptor)
			, m_component(cmp)
		{
			m_array_index = -1;
			if(descriptor.getType() == Lumix::IPropertyDescriptor::ARRAY)
			{
				Lumix::IArrayDescriptor& array_desc = static_cast<Lumix::IArrayDescriptor&>(descriptor);
				int item_count = array_desc.getCount(cmp);
				for( int j = 0; j < item_count; ++j)
				{
					ComponentArrayItemObject* item = new ComponentArrayItemObject(this, name, array_desc, m_component, j);
					addMember(item);
					for(int i = 0; i < descriptor.getChildren().size(); ++i)
					{
						auto child = descriptor.getChildren()[i];
						auto member = new ComponentPropertyObject(this, child->getName(), cmp, *descriptor.getChildren()[i]);
						member->setArrayIndex(j);
						item->addMember(member);
					}
				}
			}
		}


		virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
		{
			Lumix::Blob stream(view.getWorldEditor()->getAllocator());
			if(m_descriptor.getType() != Lumix::IPropertyDescriptor::ARRAY)
			{
				if(m_array_index >= 0 )
				{
					m_descriptor.get(m_component, m_array_index, stream);
				}
				else
				{
					m_descriptor.get(m_component, stream);
				}
			}

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
							view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &new_value, sizeof(new_value));
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
							view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
						});
					}
					break;
				case Lumix::IPropertyDescriptor::RESOURCE:
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
							QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), dynamic_cast<Lumix::IFilePropertyDescriptor&>(m_descriptor).getFileType());
							if (str != "")
							{
								char rel_path[LUMIX_MAX_PATH];
								QByteArray byte_array = str.toLatin1();
								const char* text = byte_array.data();
								view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
								view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, rel_path, strlen(rel_path) + 1);
								edit->setText(rel_path);
							}
						});
				
						QPushButton* button2 = new QPushButton("->", widget);
						layout->addWidget(button2);
						if(m_descriptor.getType() == Lumix::IPropertyDescriptor::RESOURCE)
						{
							button2->connect(button2, &QPushButton::clicked, [&view, edit]()
							{
								view.setSelectedResourceFilename(edit->text().toLatin1().data());
							});
						}
						else
						{
							button2->connect(button2, &QPushButton::clicked, [edit]()
							{
								QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
							});
						}
				
						item->treeWidget()->setItemWidget(item, 1, widget);
						connect(edit, &QLineEdit::editingFinished, [edit, &view, this]()
						{
							if(view.getObject())
							{
								QByteArray byte_array = edit->text().toLatin1();
								view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, byte_array.data(), byte_array.size() + 1);
							}
						});
					}
					break;
				case Lumix::IPropertyDescriptor::INTEGER:
					{
						auto& int_property = static_cast<Lumix::IIntPropertyDescriptor&>(m_descriptor);
						int value;
						stream.read(value);
						QSpinBox* edit = new QSpinBox();
						edit->setValue(value);
						item->treeWidget()->setItemWidget(item, 1, edit);
						edit->setMinimum(int_property.getMin());
						edit->setMaximum(int_property.getMax());
						connect(edit, (void (QSpinBox::*)(int))&QSpinBox::valueChanged, [this, &view](int new_value) 
						{
							int value = new_value;
							view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
						});
					}
					break;
				case Lumix::IPropertyDescriptor::DECIMAL:
					{
						float value;
						stream.read(value);
						QDoubleSpinBox* edit = new QDoubleSpinBox();
						edit->setMaximum(FLT_MAX);
						edit->setDecimals(4);
						edit->setSingleStep(0.001);
						edit->setValue(value);
						item->treeWidget()->setItemWidget(item, 1, edit);
						connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [this, &view](double new_value) 
						{
							float value = (float)new_value;
							view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
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
							view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, text, strlen(text) + 1);
						});
					}
					break;
				case Lumix::IPropertyDescriptor::ARRAY:
					{
						QWidget* widget = new QWidget();
						QHBoxLayout* layout = new QHBoxLayout(widget);
						layout->setContentsMargins(0, 0, 0, 0);
						QPushButton* button = new QPushButton(" + ");
						layout->addWidget(button);
						layout->addStretch(1);
						item->treeWidget()->setItemWidget(item, 1, widget);
						button->connect(button, &QPushButton::clicked, [this, &view]()
						{
							view.getWorldEditor()->addArrayPropertyItem(m_component, static_cast<Lumix::IArrayDescriptor&>(m_descriptor));
							view.refresh();
						});
					}
					break;
				case Lumix::IPropertyDescriptor::COLOR:
					{
						Lumix::Vec4 value;
						stream.read(value);
						QWidget* widget = new QWidget();
						QHBoxLayout* layout = new QHBoxLayout(widget);
						QColor color((int)(value.x * 255), (int)(value.y * 255), (int)(value.z * 255));
						QLabel* label = new QLabel(color.name());
						layout->setContentsMargins(0, 0, 0, 0);
						layout->addWidget(label);
						label->setStyleSheet(QString("QLabel { background-color : %1; }").arg(color.name()));
						QPushButton* button = new QPushButton("...");
						layout->addWidget(button);
						item->treeWidget()->setItemWidget(item, 1, widget);
						connect(button, &QPushButton::clicked, [this, &view, label, value]()
						{
							QColorDialog* dialog = new QColorDialog(QColor::fromRgbF(value.x, value.y, value.z, value.w));
							dialog->setModal(false);
							dialog->connect(dialog, &QColorDialog::currentColorChanged, [this, &view, dialog]()
							{
								QColor color = dialog->currentColor();
								Lumix::Vec4 value;
								value.x = color.redF(); 
								value.y = color.greenF(); 
								value.z = color.blueF(); 
								value.w = color.alphaF(); 
								view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
							});
							dialog->connect(dialog, &QDialog::finished, [&view]()
							{
								view.refresh();
							});
							dialog->show();
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
		void setArrayIndex(int index) { m_array_index = index; }

	private:
		Lumix::IPropertyDescriptor& m_descriptor;
		Lumix::Component m_component;
		int m_array_index;
};



template <class Value, class Obj>
class GetterSetterObject : public PropertyViewObject
{
	public:
		typedef Value (Obj::*Getter)() const;
		typedef void (Obj::*Setter)(Value);
		typedef void(*CreateEditor)(QTreeWidgetItem*, GetterSetterObject&, Value);


		GetterSetterObject(PropertyViewObject* parent, const char* name, Obj* object, Getter getter, Setter setter, CreateEditor create_editor)
			: PropertyViewObject(parent, name)
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


template <class T, bool own_value> class InstanceObject;

template <class T>
class InstanceObject<T, false> : public PropertyViewObject
{
	public:
		typedef void(*CreateEditor)(PropertyView&, QTreeWidgetItem*, InstanceObject<T, false>*);
	
		InstanceObject(PropertyViewObject* parent, const char* name, T* object, CreateEditor create_editor)
			: PropertyViewObject(parent, name)
		{
			m_value = object;
			m_create_editor = create_editor;
		}


		~InstanceObject()
		{
		}


		void setEditor(CreateEditor create_editor)
		{
			m_create_editor = create_editor;
		}


		virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
		{
			if (m_create_editor)
			{
				m_create_editor(view, item, this);
			}
		}

		virtual bool isEditable() const override { return false; }
		T* getValue() const { return m_value; }

	private:
		T* m_value;
		CreateEditor m_create_editor;
};

template <class T>
class InstanceObject<T, true> : public PropertyViewObject
{
	public:
		typedef void(*CreateEditor)(PropertyView&, QTreeWidgetItem*, InstanceObject<T, true>*);
	
		InstanceObject(PropertyViewObject* parent, const char* name, T* object, CreateEditor create_editor)
			: PropertyViewObject(parent, name)
		{
			m_value = object;
			m_create_editor = create_editor;
		}


		~InstanceObject()
		{
			delete m_value;
		}


		void setEditor(CreateEditor create_editor)
		{
			m_create_editor = create_editor;
		}


		virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
		{
			if (m_create_editor)
			{
				m_create_editor(view, item, this);
			}
		}

		virtual bool isEditable() const override { return false; }
		T* getValue() const { return m_value; }

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


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Material::Uniform, false>* uniform)
{
	switch (uniform->getValue()->m_type)
	{
		case Lumix::Material::Uniform::FLOAT:
			{
				QDoubleSpinBox* spinbox = new QDoubleSpinBox();
				spinbox->setValue(uniform->getValue()->m_float);
				item->treeWidget()->setItemWidget(item, 1, spinbox);
				spinbox->setMaximum(FLT_MAX);
				spinbox->setMinimum(-FLT_MAX);
				spinbox->connect(spinbox, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [uniform](double new_value)
				{
					uniform->getValue()->m_float = (float)new_value;
				});
			}
			break;
		default:
			ASSERT(false);
			break;
	}
	
}


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Texture, false>* texture)
{
	item->setText(1, texture->getValue()->getPath().c_str());
}


void createEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Shader, false>* shader)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	FileEdit* edit = new FileEdit(NULL, &view);
	edit->setText(shader->getValue()->getPath().c_str());
	layout->addWidget(edit);
	layout->setContentsMargins(0, 0, 0, 0);
	item->treeWidget()->setItemWidget(item, 1, widget);
	edit->connect(edit, &FileEdit::editingFinished, [shader, edit]()
	{
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(shader->getParent())->getValue();
		auto shader = static_cast<Lumix::Shader*>(material->getResourceManager().get(Lumix::ResourceManager::SHADER)->load(Lumix::Path(edit->text().toLatin1().data())));
		material->setShader(shader);
	});
	QPushButton* button = new QPushButton("...");
	layout->addWidget(button);
	button->connect(button, &QPushButton::clicked, [&view, shader, edit]()
	{
		QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "Shader (*.shd)");
		if(str != "")
		{
			char rel_path[LUMIX_MAX_PATH];
			QByteArray byte_array = str.toLatin1();
			const char* text = byte_array.data();
			view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));

			auto material = static_cast<InstanceObject<Lumix::Material, false>* >(shader->getParent())->getValue();
			auto shader = static_cast<Lumix::Shader*>(material->getResourceManager().get(Lumix::ResourceManager::SHADER)->load(Lumix::Path(rel_path)));
			material->setShader(shader);

			edit->setText(rel_path);
		}
	});

	QPushButton* go_button = new QPushButton("->");
	layout->addWidget(go_button);
	go_button->connect(go_button, &QPushButton::clicked, [edit]()
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
	});
}


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Model, false>* model)
{
	item->setText(1, model->getValue()->getPath().c_str());
}


void createImageEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Texture, false>* texture)
{
	QLabel* image_label = new QLabel();
	item->treeWidget()->setItemWidget(item, 1, image_label);
	QImage image(texture->getValue()->getPath().c_str());
	image_label->setPixmap(QPixmap::fromImage(image).scaledToHeight(100));
	image_label->adjustSize();
}


void createEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Material, false>* material)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QLabel* label = new QLabel(material->getValue()->getPath().c_str());
	layout->addWidget(label);
	QPushButton* button = new QPushButton("Save");
	QPushButton* go_button = new QPushButton("->");
	layout->addWidget(button);
	layout->addWidget(go_button);
	go_button->connect(go_button, &QPushButton::clicked, [material]()
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(material->getValue()->getPath().c_str()));
	});

	button->connect(button, &QPushButton::clicked, [material, &view]()
	{
		Lumix::FS::FileSystem& fs = view.getWorldEditor()->getEngine().getFileSystem();
		// use temporary because otherwise the material is reloaded during saving
		char tmp_path[LUMIX_MAX_PATH];
		strcpy(tmp_path, material->getValue()->getPath().c_str());
		strcat(tmp_path, ".tmp");
		Lumix::FS::IFile* file = fs.open(fs.getDefaultDevice(), tmp_path, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if(file)
		{
			Lumix::DefaultAllocator allocator;
			Lumix::JsonSerializer serializer(allocator, *file, Lumix::JsonSerializer::AccessMode::WRITE, material->getValue()->getPath().c_str());
			material->getValue()->save(serializer);
			fs.close(file);

			QFile::remove(material->getValue()->getPath().c_str());
			QFile::rename(tmp_path, material->getValue()->getPath().c_str());
		}
		else
		{
			Lumix::g_log_error.log("Material manager") << "Could not save file " << material->getValue()->getPath().c_str();
		}
	});
	item->treeWidget()->setItemWidget(item, 1, widget);
}


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Mesh, false>* mesh)
{
	item->setText(1, mesh->getValue()->getName());
}


PropertyViewObject::~PropertyViewObject()
{
	for (int i = 0; i < m_members.size(); ++i)
	{
		delete m_members[i];
	}
}


void createComponentEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Component, true>* component)
{
	if (component->getValue()->type == TERRAIN_HASH)
	{
		view.addTerrainCustomProperties(*item, *component->getValue());
	}
	else if (component->getValue()->type == SCRIPT_HASH)
	{
		view.addScriptCustomProperties(*item, *component->getValue());
	}
}


PropertyViewObject* createComponentObject(PropertyViewObject* parent, Lumix::WorldEditor& editor, Lumix::Component cmp)
{
	const char* name = "";
	for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if (crc32(component_map[i + 1]) == cmp.type)
		{
			name = component_map[i];
		}
	}
	auto c = new Lumix::Component(cmp);
	InstanceObject<Lumix::Component, true>* object = new InstanceObject<Lumix::Component, true>(parent, name, c, &createComponentEditor);
	
	auto& descriptors = editor.getPropertyDescriptors(cmp.type);
	
	for (int i = 0; i < descriptors.size(); ++i)
	{
		auto prop = new ComponentPropertyObject(object, descriptors[i]->getName(), cmp, *descriptors[i]);
		object->addMember(prop);
	}

	return object;
}


PropertyViewObject* createEntityObject(Lumix::WorldEditor& editor, Lumix::Entity entity)
{
	auto e = new Lumix::Entity(entity);
	InstanceObject<Lumix::Entity, true>* object = new InstanceObject<Lumix::Entity, true>(NULL, "Entity", e, NULL);

	auto& cmps = editor.getComponents(*e);

	for (int i = 0; i < cmps.size(); ++i)
	{
		auto prop = createComponentObject(object, editor, cmps[i]);
		object->addMember(prop);
	}

	return object;
}


PropertyViewObject* createTextureObject(PropertyViewObject* parent, Lumix::Resource* resource)
{
	if (Lumix::Texture* texture = dynamic_cast<Lumix::Texture*>(resource))
	{
		auto object = new InstanceObject<Lumix::Texture, false>(parent, "Texture", texture, &createEditor);

		auto prop = new GetterSetterObject<int, Lumix::Texture>(object, "width", texture, &Lumix::Texture::getWidth, NULL, &createEditor);
		object->addMember(prop);
		
		prop = new GetterSetterObject<int, Lumix::Texture>(object, "height", texture, &Lumix::Texture::getHeight, NULL, &createEditor);
		object->addMember(prop);

		auto img_object = new InstanceObject<Lumix::Texture, false>(object, "Image", texture, &createImageEditor);
		object->addMember(img_object);

		return object;
	}
	return NULL;
}


void createTextureInMaterialEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Texture, false>* texture)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QLineEdit* edit = new QLineEdit(texture->getValue()->getPath().c_str());
	layout->addWidget(edit);
	edit->connect(edit, &QLineEdit::editingFinished, [&view, edit, texture]() 
	{
		char rel_path[LUMIX_MAX_PATH];
		QByteArray byte_array = edit->text().toLatin1();
		const char* text = byte_array.data();
		view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
		for(int i = 0; i < material->getTextureCount(); ++i)
		{
			if(material->getTexture(i) == texture->getValue())
			{
				Lumix::Texture* new_texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path(rel_path)));
				material->setTexture(i, new_texture);
				break;
			}
		}
	});

	QPushButton* browse_button = new QPushButton("...");
	layout->addWidget(browse_button);
	browse_button->connect(browse_button, &QPushButton::clicked, [&view, edit, texture]()
	{
		QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "Texture (*.tga; *.dds)");
		if(str != "")
		{
			char rel_path[LUMIX_MAX_PATH];
			QByteArray byte_array = str.toLatin1();
			const char* text = byte_array.data();
			view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
			auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
			for(int i = 0; i < material->getTextureCount(); ++i)
			{
				if(material->getTexture(i) == texture->getValue())
				{
					Lumix::Texture* new_texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path(rel_path)));
					material->setTexture(i, new_texture);
					break;
				}
			}
			edit->setText(rel_path);
		}
	});
	
	QPushButton* remove_button = new QPushButton(" - ");
	layout->addWidget(remove_button);
	remove_button->connect(remove_button, &QPushButton::clicked, [texture, &view, item]()
	{
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
		for(int i = 0; i < material->getTextureCount(); ++i)
		{
			if(material->getTexture(i) == texture->getValue())
			{
				material->removeTexture(i);
				item->parent()->removeChild(item);
				break;
			}
		}
	});

	QPushButton* add_button = new QPushButton(" + "); 
	layout->addWidget(add_button);
	add_button->connect(add_button, &QPushButton::clicked, [texture, &view, item]()
	{
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
		Lumix::Texture* new_texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path("models/editor/default.tga")));
		material->addTexture(new_texture);
	});

	item->treeWidget()->setItemWidget(item, 1, widget);
}


static PropertyViewObject* createMaterialObject(PropertyViewObject* parent, Lumix::Resource* resource)
{
	if (Lumix::Material* material = dynamic_cast<Lumix::Material*>(resource))
	{
		auto object = new InstanceObject<Lumix::Material, false>(parent, "Material", material, &createEditor);

		PropertyViewObject* prop = new InstanceObject<Lumix::Shader, false>(object, "Shader", material->getShader(), &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Z test", material, &Lumix::Material::isZTest, &Lumix::Material::enableZTest, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Backface culling", material, &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Alpha to coverage", material, &Lumix::Material::isAlphaToCoverage, &Lumix::Material::enableAlphaToCoverage, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Shadow receiver", material, &Lumix::Material::isShadowReceiver, &Lumix::Material::enableShadowReceiving, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Alpha cutout", material, &Lumix::Material::isAlphaCutout, &Lumix::Material::enableAlphaCutout, &createEditor);
		object->addMember(prop);

		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			prop = createTextureObject(object, material->getTexture(i));
			static_cast<InstanceObject<Lumix::Texture, false>*>(prop)->setEditor(createTextureInMaterialEditor) ;
			object->addMember(prop);
		}

		for (int i = 0; i < material->getUniformCount(); ++i)
		{
			Lumix::Material::Uniform& uniform = material->getUniform(i);
			if (uniform.m_is_editable)
			{
				prop = new InstanceObject<Lumix::Material::Uniform, false>(object, uniform.m_name, &uniform, createEditor);
				object->addMember(prop);
			}
		}
		return object;
	}
	return NULL;
}


PropertyViewObject* createModelObject(PropertyViewObject* parent, Lumix::Resource* resource)
{
	if (Lumix::Model* model = dynamic_cast<Lumix::Model*>(resource))
	{
		auto* object = new InstanceObject<Lumix::Model, false>(parent, "Model", model, &createEditor);

		PropertyViewObject* prop = new GetterSetterObject<int, Lumix::Model>(object, "Bone count", model, &Lumix::Model::getBoneCount, NULL, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<float, Lumix::Model>(object, "Bounding radius", model, &Lumix::Model::getBoundingRadius, NULL, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<size_t, Lumix::Model>(object, "Size (bytes)", model, &Lumix::Model::size, NULL, &createEditor);
		object->addMember(prop);

		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			Lumix::Mesh* mesh = &model->getMesh(i);
			auto mesh_object = new InstanceObject<Lumix::Mesh, false>(object, "Mesh", mesh, &createEditor);
			object->addMember(mesh_object);

			prop = new GetterSetterObject<int, Lumix::Mesh>(mesh_object, "Triangles", mesh, &Lumix::Mesh::getTriangleCount, NULL, &createEditor);
			mesh_object->addMember(prop);

			prop = createMaterialObject(mesh_object, mesh->getMaterial());
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
			TEXTURE,
			ENTITY
		};

		TerrainEditor(Lumix::WorldEditor& editor, EntityTemplateList* template_list, EntityList* entity_list)
			: m_world_editor(editor)
			, m_entity_template_list(template_list)
			, m_entity_list(entity_list)
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
			
			for(int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0; --i)
			{
				Lumix::Component terrain = m_world_editor.getComponent(m_world_editor.getSelectedEntities()[i], crc32("terrain"));
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
			for(int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0; --i)
			{
				if (m_world_editor.getSelectedEntities()[i] == hit.m_component.entity)
				{
					Lumix::Component terrain = m_world_editor.getComponent(hit.m_component.entity, crc32("terrain"));
					if (terrain.isValid())
					{
						Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
						switch (m_type)
						{
							case HEIGHT:
								addTerrainLevel(terrain, hit, true);
								break;
							case TEXTURE:
								addSplatWeight(terrain, hit);
								break;
							case ENTITY:
								m_entity_list->enableUpdate(false);
								paintEntities(terrain, hit);
								break;
							default:
								ASSERT(false);
								break;
						}
						return true;
					}
				}
			}
			return false;
		}

		virtual void onMouseMove(int x, int y, int /*rel_x*/, int /*rel_y*/, int /*mouse_flags*/) override
		{
			Lumix::Component camera_cmp = m_world_editor.getEditCamera();
			Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.scene);
			Lumix::Vec3 origin, dir;
			scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
			Lumix::RayCastModelHit hit = scene->castRayTerrain(m_component, origin, dir);
			if (hit.m_is_hit)
			{
				Lumix::Component terrain = m_world_editor.getComponent(hit.m_component.entity, crc32("terrain"));
				if(terrain.isValid())
				{
					switch (m_type)
					{
						case HEIGHT:
							addTerrainLevel(terrain, hit, false);
							break;
						case TEXTURE:
							addSplatWeight(terrain, hit);
							break;
						case ENTITY:
							paintEntities(terrain, hit);
							break;
						default:
							break;
					}
				}
			}
		}

		virtual void onMouseUp(int, int, Lumix::MouseButton::Value) override
		{
			m_entity_list->enableUpdate(true);
		}


		Lumix::Material* getMaterial()
		{
			Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
			Lumix::string material_path(allocator);
			static_cast<Lumix::RenderScene*>(m_component.scene)->getTerrainMaterial(m_component, material_path);
			return static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(Lumix::Path(material_path.c_str())));
		}


		static void getProjections(const Lumix::Vec3& axis, const Lumix::Vec3 vertices[8], float& min, float& max)
		{
		  min = max = Lumix::dotProduct(vertices[0], axis);
		  for(int i = 1 ; i < 8; ++i)
		  {
			float dot = Lumix::dotProduct(vertices[i], axis);
			min = Lumix::Math::minValue(dot, min);
			max = Lumix::Math::maxValue(dot, max);
		  }
		}


		bool overlaps(float min1, float max1, float min2, float max2)
		{
			return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
		}


		bool testOBBCollision(const Lumix::Matrix& matrix_a, const Lumix::Model* model_a, const Lumix::Matrix& matrix_b, const Lumix::Model* model_b, float scale)
		{
			Lumix::Vec3 box_a_points[8];
			Lumix::Vec3 box_b_points[8];

			if(fabs(scale - 1.0) < 0.01f)
			{
				model_a->getAABB().getCorners(matrix_a, box_a_points);
				model_b->getAABB().getCorners(matrix_b, box_b_points);
			}
			else
			{
				Lumix::Matrix scale_matrix_a = matrix_a;
				scale_matrix_a.multiply3x3(scale);
				Lumix::Matrix scale_matrix_b = matrix_b;
				scale_matrix_b.multiply3x3(scale);
				model_a->getAABB().getCorners(scale_matrix_a, box_a_points);
				model_b->getAABB().getCorners(scale_matrix_b, box_b_points);
			}

			Lumix::Vec3 normals[] = { matrix_a.getXVector(), matrix_a.getYVector(), matrix_a.getZVector() };
			for( int i = 0 ; i < 3; i++ )
			{
				float box_a_min, box_a_max, box_b_min, box_b_max;
				getProjections(normals[i], box_a_points, box_a_min, box_a_max);
				getProjections(normals[i], box_b_points, box_b_min, box_b_max);
				if(!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
				{
					return false;
				}
			}
			
			Lumix::Vec3 normals_b[] = { matrix_b.getXVector(), matrix_b.getYVector(), matrix_b.getZVector() };
			for( int i = 0 ; i < 3; i++ )
			{
				float box_a_min, box_a_max, box_b_min, box_b_max;
				getProjections(normals_b[i], box_a_points, box_a_min, box_a_max);
				getProjections(normals_b[i], box_b_points, box_b_min, box_b_max);
				if(!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
				{
					return false;
				}
			}

			return true;
		}


		bool isOBBCollision(Lumix::RenderScene* scene, const Lumix::Matrix& matrix, Lumix::Model* model, float scale)
		{
			Lumix::Vec3 pos_a = matrix.getTranslation();
			static Lumix::Array<Lumix::RenderableMesh> meshes(m_world_editor.getAllocator());
			meshes.clear();
			scene->getRenderableMeshes(meshes, ~0);
			float radius_a_squared = model->getBoundingRadius();
			radius_a_squared = radius_a_squared * radius_a_squared;
			for (int i = 0, c = meshes.size(); i < c; ++i)
			{
				Lumix::Vec3 pos_b = meshes[i].m_matrix->getTranslation();
				float radius_b = meshes[i].m_model->getBoundingRadius();
				float radius_squared = radius_a_squared + radius_b * radius_b;
				if((pos_a - pos_b).squaredLength() < radius_squared * scale * scale)
				{
					if (testOBBCollision(matrix, model, *meshes[i].m_matrix, meshes[i].m_model, scale))
					{
						return true;
					}
				}
			}
			return false;
		}


		void paintEntities(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
		{
			Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(terrain.scene);
			Lumix::Vec3 center_pos = hit.m_origin + hit.m_dir * hit.m_t;
			Lumix::Matrix terrain_matrix = terrain.entity.getMatrix();
			Lumix::Matrix inv_terrain_matrix = terrain_matrix;
			inv_terrain_matrix.inverse();
			Lumix::Entity tpl = m_entity_template_list->getTemplate();
			if(!tpl.isValid())
			{
				return;
			}
			Lumix::Component renderable = m_world_editor.getComponent(tpl, RENDERABLE_HASH);
			if(renderable.isValid())
			{
				float w, h;
				scene->getTerrainSize(terrain, &w, &h);
				float scale = 1.0f - Lumix::Math::maxValue(0.01f, m_terrain_brush_strength);
				Lumix::Model* model = scene->getRenderableModel(renderable);
				for (int i = 0; i <= m_terrain_brush_size * m_terrain_brush_size / 1000.0f; ++i)
				{
					float angle = (float)(rand() % 360);
					float dist = (rand() % 100 / 100.0f) * m_terrain_brush_size;
					Lumix::Vec3 pos(center_pos.x + cos(angle) * dist, 0, center_pos.z + sin(angle) * dist);
					Lumix::Vec3 terrain_pos = inv_terrain_matrix.multiplyPosition(pos);
					if (terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= w && terrain_pos.z <= h)
					{
						pos.y = scene->getTerrainHeightAt(terrain, terrain_pos.x, terrain_pos.z);
						Lumix::Matrix mtx = Lumix::Matrix::IDENTITY;
						mtx.setTranslation(pos);
						if(!isOBBCollision(scene, mtx, model, scale))
						{
							m_entity_template_list->instantiateTemplateAt(pos);
						}
					}
				}
			}
		}


		void addSplatWeight(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
		{
			if (!terrain.isValid())
				return;
			float radius = (float)m_terrain_brush_size;
			float rel_amount = m_terrain_brush_strength;
			Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
			Lumix::string material_path(allocator);
			static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainMaterial(terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(Lumix::Path(material_path.c_str())));
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			Lumix::Texture* splatmap = material->getTextureByUniform("splat_texture");
			Lumix::Texture* heightmap = material->getTextureByUniform("hm_texture");
			Lumix::Matrix entity_mtx = terrain.entity.getMatrix();
			entity_mtx.fastInverse();
			Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
			float xz_scale = static_cast<Lumix::RenderScene*>(terrain.scene)->getTerrainXZScale(terrain);
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

		void addTerrainLevel(Lumix::Component terrain, const Lumix::RayCastModelHit& hit, bool new_stroke)
		{
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			AddTerrainLevelCommand* command = m_world_editor.getAllocator().newObject<AddTerrainLevelCommand>(m_world_editor, hit_pos, m_terrain_brush_size, m_terrain_brush_strength, terrain, !new_stroke);
			m_world_editor.executeCommand(command);
		}

		Lumix::WorldEditor& m_world_editor;
		Type m_type;
		QTreeWidgetItem* m_tree_top_level;
		Lumix::Component m_component;
		QTreeWidgetItem* m_texture_tree_item;
		float m_terrain_brush_strength;
		int m_terrain_brush_size;
		int m_texture_idx;
		EntityTemplateList* m_entity_template_list;
		EntityList* m_entity_list;
};


PropertyView::PropertyView(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::PropertyView)
	, m_terrain_editor(NULL)
	, m_selected_resource(NULL)
	, m_object(NULL)
	, m_selected_entity(Lumix::Entity::INVALID)
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


void PropertyView::onEntityPosition(const Lumix::Entity& e)
{
	if (m_selected_entity == e)
	{
		bool b1 = m_ui->positionX->blockSignals(true);
		bool b2 = m_ui->positionY->blockSignals(true);
		bool b3 = m_ui->positionZ->blockSignals(true);
		
		Lumix::Vec3 pos = e.getPosition();
		m_ui->positionX->setValue(pos.x);
		m_ui->positionY->setValue(pos.y);
		m_ui->positionZ->setValue(pos.z);
		
		m_ui->positionX->blockSignals(b1);
		m_ui->positionY->blockSignals(b2);
		m_ui->positionZ->blockSignals(b3);
	}
}


void PropertyView::refresh()
{
	setObject(NULL);
	onEntitySelected(getWorldEditor()->getSelectedEntities());
}


Lumix::WorldEditor* PropertyView::getWorldEditor()
{
	return m_world_editor;
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_terrain_editor = new TerrainEditor(editor, m_entity_template_list, m_entity_list);
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
	m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(filename));
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
		setSelectedResource(manager->load(Lumix::Path(rel_path)));
	}
	else
	{
		setSelectedResource(NULL);
	}
}


void PropertyView::addResourcePlugin(PropertyViewObject::Creator plugin)
{
	m_resource_plugins.push_back(plugin);
}


void PropertyView::onSelectedResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		m_selected_entity = Lumix::Entity::INVALID;
		clear();
		for (int i = 0; i < m_resource_plugins.size(); ++i)
		{
			if (PropertyViewObject* object = m_resource_plugins[i](NULL, m_selected_resource))
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
		connect(m_compiler, &ScriptCompiler::compiled, this, &PropertyView::on_script_compiled);
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
		QTreeWidgetItem* top_level_item = m_ui->propertyList->topLevelItem(i);
		for (int k = 0; k < top_level_item->childCount(); ++k)
		{
			QTreeWidgetItem* item = m_ui->propertyList->topLevelItem(i)->child(k);
			if (item->text(0) == "Script")
			{
				for (int j = 0; j < item->childCount(); ++j)
				{
					if (item->child(j)->text(0) == "Status")
					{
						switch (status)
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
}


void PropertyView::on_script_compiled(const Lumix::Path&, uint32_t status)
{
	setScriptStatus(status == 0 ? ScriptCompiler::SUCCESS : ScriptCompiler::FAILURE);
}


void PropertyView::addScriptCustomProperties(QTreeWidgetItem& tree_item, const Lumix::Component& script_component)
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	tree_item.insertChild(0, tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Compile", widget);
	layout->addWidget(compile_button);
	m_ui->propertyList->setItemWidget(tools_item, 1, widget);
	connect(compile_button, &QPushButton::clicked, this, [this, script_component](){
		Lumix::string path(m_world_editor->getAllocator());
		static_cast<Lumix::ScriptScene*>(script_component.scene)->getScriptPath(script_component, path);
		m_compiler->compile(Lumix::Path(path.c_str()));
	});

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	tree_item.insertChild(0, status_item);
	Lumix::string path(m_world_editor->getAllocator());
	static_cast<Lumix::ScriptScene*>(script_component.scene)->getScriptPath(script_component, path);
	switch (m_compiler->getStatus(Lumix::Path(path.c_str())))
	{
		case ScriptCompiler::SUCCESS:
			status_item->setText(1, "Compiled");
			break;
		case ScriptCompiler::FAILURE:
			status_item->setText(1, "Failure");
			break;
		default:
			status_item->setText(1, "Unknown");
			break;
	}
}


void PropertyView::addTerrainCustomProperties(QTreeWidgetItem& tree_item, const Lumix::Component& terrain_component)
{
	m_terrain_editor->m_tree_top_level = &tree_item;
	m_terrain_editor->m_component = terrain_component;

	{
		QWidget* widget = new QWidget();
		QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Save");
		tree_item.insertChild(0, item);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QPushButton* height_button = new QPushButton("Heightmap", widget);
		layout->addWidget(height_button);
		QPushButton* texture_button = new QPushButton("Splatmap", widget);
		layout->addWidget(texture_button);
		layout->setContentsMargins(2, 2, 2, 2);
		m_ui->propertyList->setItemWidget(item, 1, widget);
		connect(height_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("hm_texture")->save();
		});
		connect(texture_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("splat_texture")->save();
		});
	}

	QSlider* slider = new QSlider(Qt::Orientation::Horizontal);
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Brush size");
	tree_item.insertChild(1, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(1);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_size = value;
	});

	slider = new QSlider(Qt::Orientation::Horizontal);
	item = new QTreeWidgetItem(QStringList() << "Brush strength");
	tree_item.insertChild(2, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(-100);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_strength = value / 100.0f;
	});

	QWidget* widget = new QWidget();
	item = new QTreeWidgetItem(QStringList() << "Brush type");
	tree_item.insertChild(3, item);
	QHBoxLayout* layout = new QHBoxLayout(widget);
	QPushButton* height_button = new QPushButton("Height", widget);
	layout->addWidget(height_button);
	QPushButton* texture_button = new QPushButton("Texture", widget);
	layout->addWidget(texture_button);
	QPushButton* entity_button = new QPushButton("Entity", widget);
	layout->addWidget(entity_button);
	layout->setContentsMargins(2, 2, 2, 2);
	m_ui->propertyList->setItemWidget(item, 1, widget);
	m_terrain_editor->m_type = TerrainEditor::HEIGHT;
	connect(height_button, &QPushButton::clicked, [this]()
	{
		m_terrain_editor->m_type = TerrainEditor::HEIGHT;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});
	connect(texture_button, &QPushButton::clicked, this, &PropertyView::on_TerrainTextureTypeClicked);
	connect(entity_button, &QPushButton::clicked, [this]()
	{
		m_terrain_editor->m_type = TerrainEditor::ENTITY;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});

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



void PropertyView::setSelectedResource(Lumix::Resource* resource)
{
	if(resource)
	{
		m_world_editor->selectEntities(NULL, 0);
	}
	clear();
	if (m_selected_resource)
	{
		m_selected_resource->getObserverCb().unbind<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
	m_selected_resource = resource;
	if (resource)
	{
		m_selected_resource->onLoaded<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
}


void PropertyView::onEntitySelected(const Lumix::Array<Lumix::Entity>& e)
{
	setSelectedResource(NULL);
	m_selected_entity = e.empty() ? Lumix::Entity::INVALID : e[0];
	clear();
	if (e.size() == 1 && e[0].isValid())
	{
		setObject(createEntityObject(*m_world_editor, e[0]));
		m_ui->propertyList->expandAll();
		onEntityPosition(e[0]);
		m_ui->nameEdit->setText(e[0].getName());
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
			Lumix::Array<Lumix::Entity> tmp(m_world_editor->getAllocator());
			tmp.push(m_selected_entity);
			onEntitySelected(tmp);
			return;
		}
	}
	ASSERT(false); // unknown component type
}

void PropertyView::updateSelectedEntityPosition()
{
	if(m_world_editor->getSelectedEntities().size() == 1)
	{
		Lumix::Array<Lumix::Vec3> positions(m_world_editor->getAllocator());
		positions.push(Lumix::Vec3((float)m_ui->positionX->value(), (float)m_ui->positionY->value(), (float)m_ui->positionZ->value()));
		m_world_editor->setEntitiesPositions(m_world_editor->getSelectedEntities(), positions);
	}
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


PropertyViewObject* PropertyView::getObject()
{
	return m_object;
}


void PropertyView::setObject(PropertyViewObject* object)
{
	if(object != m_object)
	{
		clear();
	}
	else
	{
		m_ui->propertyList->clear();
	}

	m_object = object;
	
	if(object)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		m_ui->propertyList->insertTopLevelItem(0, item);
		createObjectEditor(item, object);

		m_ui->propertyList->expandAll();
		m_ui->propertyList->resizeColumnToContents(0);
	}
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
			const Lumix::WorldEditor::ComponentList& cmps = m_world_editor->getComponents(m_selected_entity);
			for (int i = 0, c = cmps.size(); i < c; ++i)
			{
				if (cmps[i].type == cmp_hash)
				{
					Lumix::Entity entity = cmps[i].entity;
					m_world_editor->destroyComponent(cmps[i]);
					Lumix::Array<Lumix::Entity> tmp(m_world_editor->getAllocator());
					tmp.push(m_selected_entity);
					onEntitySelected(tmp);
					break;
				}
			}
		}
	}
}

void PropertyView::on_nameEdit_editingFinished()
{
	if (m_selected_entity.isValid() && strcmp(m_ui->nameEdit->text().toLatin1().data(), m_selected_entity.getName()) != 0)
	{
		if (m_selected_entity.universe->nameExists(m_ui->nameEdit->text().toLatin1().data()))
		{
			static bool is = false;
			if (!is)
			{
				is = true;
				QMessageBox::critical(NULL, "Error", "Name already taken", QMessageBox::StandardButton::Ok, 0);
				is = false;
			}
		}
		else
		{
			m_world_editor->setEntityName(m_selected_entity, m_ui->nameEdit->text().toLatin1().data());
		}
	}
}
