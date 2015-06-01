#include "terrain_editor.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/entity_template_system.h"
#include "editor/ieditor_command.h"
#include "engine/engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/render_scene.h"
#include "graphics/texture.h"
#include "mainwindow.h"
#include "property_view.h"
#include <QColorDialog>
#include <qcombobox.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <qlayout.h>
#include <qlineedit.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qmimedata.h>
#include <qpushbutton.h>
#include <qtextstream.h>
#include <qtreewidget.h>


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const char* HEIGHTMAP_UNIFORM = "hm_texture";
static const char* SPLATMAP_UNIFORM = "splat_texture";

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
			item.m_texture_center_x = (int)local_pos.x;
			item.m_texture_center_y = (int)local_pos.z;
			item.m_texture_radius = (int)radius;
			item.m_amount = rel_amount;
		}


		virtual void serialize(Lumix::JsonSerializer& serializer) override
		{
			serializer.beginArray("items");
			for (int i = 0; i < m_items.size(); ++i)
			{
				serializer.serializeArrayItem(m_items[i].m_amount);
				serializer.serializeArrayItem(m_items[i].m_texture_center_x);
				serializer.serializeArrayItem(m_items[i].m_texture_center_y);
				serializer.serializeArrayItem(m_items[i].m_texture_radius);
			}
			serializer.endArray();
		}


		virtual void deserialize(Lumix::JsonSerializer& serializer) override
		{
			m_items.clear();
			serializer.deserializeArrayBegin("items");
			while (!serializer.isArrayEnd())
			{
				Item& item = m_items.pushEmpty();
				serializer.deserializeArrayItem(item.m_amount, 0);
				serializer.deserializeArrayItem(item.m_texture_center_x, 0);
				serializer.deserializeArrayItem(item.m_texture_center_y, 0);
				serializer.deserializeArrayItem(item.m_texture_radius, 0);
			}
			serializer.deserializeArrayEnd();
		}


		Lumix::Texture* getHeightmap()
		{
			Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
			Lumix::string material_path(allocator);
			static_cast<Lumix::RenderScene*>(m_terrain.scene)->getTerrainMaterial(m_terrain, material_path);
			Lumix::Material* material = static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(Lumix::Path(material_path.c_str())));
			return material->getTextureByUniform(HEIGHTMAP_UNIFORM);
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

			for (int item_index = 0; item_index < m_items.size(); ++item_index)
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

			for (int j = m_y; j < m_y + m_height; ++j)
			{
				for (int i = m_x; i < m_x + m_width; ++i)
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
			if (m_new_data.empty())
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
			for (int row = rect.m_from_y; row < rect.m_to_y; ++row)
			{
				memcpy(&new_data[(row - rect.m_from_y) * new_w * 4], &heightmap->getData()[row * 4 * heightmap->getWidth() + rect.m_from_x * 4], 4 * new_w);
				memcpy(&old_data[(row - rect.m_from_y) * new_w * 4], &heightmap->getData()[row * 4 * heightmap->getWidth() + rect.m_from_x * 4], 4 * new_w);
			}

			// new
			for (int row = 0; row < m_height; ++row)
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
			if (!m_can_be_merged)
			{
				return false;
			}
			AddTerrainLevelCommand& my_command = static_cast<AddTerrainLevelCommand&>(command);
			if (m_terrain == my_command.m_terrain)
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
			for (int i = 1; i < m_items.size(); ++i)
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


TerrainComponentPlugin::TerrainComponentPlugin(MainWindow& main_window)
	: m_main_window(main_window)
{
	m_terrain_editor = new TerrainEditor(*main_window.getWorldEditor(), main_window);
	connect(main_window.getPropertyView(), &PropertyView::componentNodeCreated, [this](DynamicObjectModel::Node& node, const Lumix::Component& cmp){
		if (cmp.type == crc32("terrain"))
		{
			createEditor(node, cmp);
		}
	});
}


TerrainComponentPlugin::~TerrainComponentPlugin()
{
	delete m_terrain_editor;
}

/*
uint32_t TerrainComponentPlugin::getType()
{
	return crc32("terrain");
}


void TerrainComponentPlugin::onPropertyViewCleared()
{
	m_texture_tool_item = NULL;
	m_tools_item = NULL;
}

*/
void TerrainComponentPlugin::createEditor(DynamicObjectModel::Node& node, const Lumix::Component& component)
{
	m_terrain_editor->m_component = component;
	auto splat_map = m_terrain_editor->getMaterial()->getTextureByUniform(SPLATMAP_UNIFORM);
	if (splat_map)
	{
		splat_map->addDataReference();
	}

	auto& tools_node = node.addChild("Tools");
	tools_node.m_getter = [](){ return QVariant(); };
	auto& save_node = tools_node.addChild("Save");
	save_node.enablePeristentEditor();
	save_node.m_getter = []() { return QVariant(); };
	save_node.m_setter = [](const QVariant&){};
	save_node.onCreateEditor = [this](QWidget* parent, const QStyleOptionViewItem&){
		auto container = new QWidget(parent);
		auto layout = new QHBoxLayout(container);
		auto height_button = new QPushButton("Heightmap", container);
		auto texture_button = new QPushButton("Splatmap", container);
		height_button->connect(height_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform(HEIGHTMAP_UNIFORM)->save();
		});
		texture_button->connect(texture_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform(SPLATMAP_UNIFORM)->save();
		});
		layout->addWidget(height_button);
		layout->addWidget(texture_button);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addStretch();
		return container;
	};

	auto& brush_size_node = tools_node.addChild("Brush size");
	brush_size_node.m_getter = [this]() -> QVariant { return m_terrain_editor->m_terrain_brush_size; };
	brush_size_node.m_setter = [this](const QVariant& value) { m_terrain_editor->m_terrain_brush_size = value.toInt(); };
	brush_size_node.enablePeristentEditor();
	DynamicObjectModel::setSliderEditor(brush_size_node, 1, 100, 1);

	auto& brush_strength_node = tools_node.addChild("Brush strength");
	brush_strength_node.m_getter = [this]() -> QVariant { return m_terrain_editor->m_terrain_brush_strength; };
	brush_strength_node.m_setter = [this](const QVariant& value) { m_terrain_editor->m_terrain_brush_strength = value.toFloat(); };
	brush_strength_node.enablePeristentEditor();
	DynamicObjectModel::setSliderEditor(brush_strength_node, -1.0f, 1.0f, 0.01f);

	auto& brush_type_node = tools_node.addChild("Brush type");
	brush_type_node.m_getter = [this]() { return QVariant(); };
	brush_type_node.m_setter = [this](const QVariant&) {};
	brush_type_node.enablePeristentEditor();
	int last_node_index = brush_type_node.m_index;
	brush_type_node.onCreateEditor = [this, &tools_node, last_node_index](QWidget* parent, const QStyleOptionViewItem&){
		auto editor = new QComboBox(parent);
		editor->addItem("Height");
		editor->addItem("Texture");
		editor->addItem("Entity");
		connect(editor, (void (QComboBox::*)(int))&QComboBox::currentIndexChanged, [this, &tools_node, last_node_index](int index){
			while (tools_node.m_children.size() > last_node_index + 1)
			{ 
				auto model = static_cast<DynamicObjectModel*>(m_main_window.getPropertyView()->getModel());
				model->removeNode(*tools_node.m_children.back());
			}
			switch(index)
			{
				case 0:
					m_terrain_editor->m_type = TerrainEditor::HEIGHT;
					break;
				case 1:
					m_terrain_editor->m_type = TerrainEditor::TEXTURE;
					addTextureNode(tools_node);
					break;
				case 2:
					m_terrain_editor->m_type = TerrainEditor::ENTITY;
					addEntityTemplateNode(tools_node);
					break;
				default:
					Q_ASSERT(false);
			}
		});
		return editor;
	};
}


void TerrainComponentPlugin::addTextureNode(DynamicObjectModel::Node& node)
{
	auto model = static_cast<DynamicObjectModel*>(m_main_window.getPropertyView()->getModel());
	model->childAboutToBeAdded(node);
	auto& child = node.addChild("Texture");
	child.m_getter = [](){ return QVariant(); };
	child.m_setter = [](const QVariant&){};
	
	child.onCreateEditor = [this](QWidget* parent, const QStyleOptionViewItem&){
		auto material = m_terrain_editor->getMaterial();
		QComboBox* cb = new QComboBox(parent);
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			auto uniform = material->getTextureUniform(i);
			if (strcmp(uniform, HEIGHTMAP_UNIFORM) != 0 && strcmp(uniform, SPLATMAP_UNIFORM) != 0)
			{
				cb->addItem(material->getTexture(i)->getPath().c_str());
			}
		}
		connect(cb, (void (QComboBox::*)(int))&QComboBox::activated, [this](int index){
			m_terrain_editor->m_texture_idx = index;
		});
		return cb;
	};
	child.enablePeristentEditor();
	model->childAdded();
}


void TerrainComponentPlugin::addEntityTemplateNode(DynamicObjectModel::Node& node)
{
	auto model = static_cast<DynamicObjectModel*>(m_main_window.getPropertyView()->getModel());
	model->childAboutToBeAdded(node);
	auto& child = node.addChild("Entity template");
	child.m_getter = [](){ return QVariant(); };
	child.m_setter = [](const QVariant&){};
	child.onCreateEditor = [this](QWidget* parent, const QStyleOptionViewItem&){
		auto& templates = m_main_window.getWorldEditor()->getEntityTemplateSystem().getTemplateNames();
		QComboBox* cb = new QComboBox(parent);
		m_terrain_editor->m_selected_entity_template = templates.empty() ? "" : templates[0].c_str();
		for (int i = 0; i < templates.size(); ++i)
		{
			cb->addItem(templates[i].c_str());
		}
		connect(cb, (void (QComboBox::*)(const QString&))&QComboBox::activated, [this](const QString& name){
			m_terrain_editor->m_selected_entity_template = name;
		});
		return cb;
	};
	child.enablePeristentEditor();
	model->childAdded();
}

/*
void TerrainComponentPlugin::resetTools()
{
	if (m_texture_tool_item)
	{
		m_texture_tool_item->parent()->removeChild(m_texture_tool_item);
		m_texture_tool_item = NULL;
	}
}

*/


TerrainEditor::~TerrainEditor()
{
	m_world_editor.removePlugin(*this);
}


TerrainEditor::TerrainEditor(Lumix::WorldEditor& editor, MainWindow& main_window)
	: m_world_editor(editor)
	, m_main_window(main_window)
{
	editor.addPlugin(*this);
	m_texture_tree_item = NULL;
	m_terrain_brush_size = 10;
	m_terrain_brush_strength = 0.1f;
	m_type = NOT_SET;
	m_texture_idx = 0;
}


void TerrainEditor::tick()
{
	float mouse_x = m_world_editor.getMouseX();
	float mouse_y = m_world_editor.getMouseY();

	if (m_type != NOT_SET)
	{
		for (int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0; --i)
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
}


bool TerrainEditor::onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int)
{
	for (int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0; --i)
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
						m_main_window.getEntityList()->enableUpdate(false);
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


void TerrainEditor::onMouseMove(int x, int y, int /*rel_x*/, int /*rel_y*/, int /*mouse_flags*/)
{
	Lumix::Component camera_cmp = m_world_editor.getEditCamera();
	Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.scene);
	Lumix::Vec3 origin, dir;
	scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
	Lumix::RayCastModelHit hit = scene->castRayTerrain(m_component, origin, dir);
	if (hit.m_is_hit)
	{
		Lumix::Component terrain = m_world_editor.getComponent(hit.m_component.entity, crc32("terrain"));
		if (terrain.isValid())
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


void TerrainEditor::onMouseUp(int, int, Lumix::MouseButton::Value)
{
	m_main_window.getEntityList()->enableUpdate(true);
}


Lumix::Material* TerrainEditor::getMaterial()
{
	Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
	Lumix::string material_path(allocator);
	static_cast<Lumix::RenderScene*>(m_component.scene)->getTerrainMaterial(m_component, material_path);
	return static_cast<Lumix::Material*>(m_world_editor.getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL)->get(Lumix::Path(material_path.c_str())));
}


void TerrainEditor::getProjections(const Lumix::Vec3& axis, const Lumix::Vec3 vertices[8], float& min, float& max)
{
	min = max = Lumix::dotProduct(vertices[0], axis);
	for (int i = 1; i < 8; ++i)
	{
		float dot = Lumix::dotProduct(vertices[i], axis);
		min = Lumix::Math::minValue(dot, min);
		max = Lumix::Math::maxValue(dot, max);
	}
}


bool TerrainEditor::overlaps(float min1, float max1, float min2, float max2)
{
	return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
}


bool TerrainEditor::testOBBCollision(const Lumix::Matrix& matrix_a, const Lumix::Model* model_a, const Lumix::Matrix& matrix_b, const Lumix::Model* model_b, float scale)
{
	Lumix::Vec3 box_a_points[8];
	Lumix::Vec3 box_b_points[8];

	if (fabs(scale - 1.0) < 0.01f)
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
	for (int i = 0; i < 3; i++)
	{
		float box_a_min, box_a_max, box_b_min, box_b_max;
		getProjections(normals[i], box_a_points, box_a_min, box_a_max);
		getProjections(normals[i], box_b_points, box_b_min, box_b_max);
		if (!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
		{
			return false;
		}
	}

	Lumix::Vec3 normals_b[] = { matrix_b.getXVector(), matrix_b.getYVector(), matrix_b.getZVector() };
	for (int i = 0; i < 3; i++)
	{
		float box_a_min, box_a_max, box_b_min, box_b_max;
		getProjections(normals_b[i], box_a_points, box_a_min, box_a_max);
		getProjections(normals_b[i], box_b_points, box_b_min, box_b_max);
		if (!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
		{
			return false;
		}
	}

	return true;
}


bool TerrainEditor::isOBBCollision(Lumix::RenderScene* scene, const Lumix::Matrix& matrix, Lumix::Model* model, float scale)
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
		if ((pos_a - pos_b).squaredLength() < radius_squared * scale * scale)
		{
			if (testOBBCollision(matrix, model, *meshes[i].m_matrix, meshes[i].m_model, scale))
			{
				return true;
			}
		}
	}
	return false;
}


void TerrainEditor::paintEntities(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
{
	Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(terrain.scene);
	Lumix::Vec3 center_pos = hit.m_origin + hit.m_dir * hit.m_t;
	Lumix::Matrix terrain_matrix = terrain.entity.getMatrix();
	Lumix::Matrix inv_terrain_matrix = terrain_matrix;
	inv_terrain_matrix.inverse();
	if (m_selected_entity_template.isEmpty())
	{
		return;
	}
	Lumix::Entity tpl = m_world_editor.getEntityTemplateSystem().getInstances(crc32(m_selected_entity_template.toLatin1().data()))[0];
	if (!tpl.isValid())
	{
		return;
	}
	Lumix::Component renderable = m_world_editor.getComponent(tpl, RENDERABLE_HASH);
	if (renderable.isValid())
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
				if (!isOBBCollision(scene, mtx, model, scale))
				{
					m_world_editor.getEntityTemplateSystem().createInstance(m_selected_entity_template.toLatin1().data(), pos);
				}
			}
		}
	}
}


void TerrainEditor::addSplatWeight(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
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
	Lumix::Texture* splatmap = material->getTextureByUniform(SPLATMAP_UNIFORM);
	Lumix::Texture* heightmap = material->getTextureByUniform(HEIGHTMAP_UNIFORM);
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


void TerrainEditor::addTexelSplatWeight(uint8_t& w1, uint8_t& w2, uint8_t& w3, uint8_t& w4, int value)
{
	int add = value;
	add = Lumix::Math::minValue(add, 255 - w1);
	add = Lumix::Math::maxValue(add, -w1);
	w1 += add;
	if (w2 + w3 + w4 == 0)
	{
		uint8_t rest = (255 - w1) / 3;
		w2 = rest;
		w3 = rest;
		w4 = rest;
	}
	else
	{
		float m = (255 - w1) / (float)(w2 + w3 + w4);
		w2 = w2 * m;
		w3 = w3 * m;
		w4 = w4 * m;
	}
	if (w1 + w2 + w3 + w4 > 255)
	{
		w4 = 255 - w1 - w2 - w3;
	}
}


void TerrainEditor::addTerrainLevel(Lumix::Component terrain, const Lumix::RayCastModelHit& hit, bool new_stroke)
{
	Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
	AddTerrainLevelCommand* command = m_world_editor.getAllocator().newObject<AddTerrainLevelCommand>(m_world_editor, hit_pos, (float)m_terrain_brush_size, m_terrain_brush_strength, terrain, !new_stroke);
	m_world_editor.executeCommand(command);
}
