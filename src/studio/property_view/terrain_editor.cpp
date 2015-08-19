#include "terrain_editor.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/entity_template_system.h"
#include "editor/ieditor_command.h"
#include "engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "mainwindow.h"
#include "property_view.h"
#include "universe/universe.h"
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


static const uint32_t RENDERABLE_HASH = Lumix::crc32("renderable");
static const char* HEIGHTMAP_UNIFORM = "u_texHeightmap";
static const char* SPLATMAP_UNIFORM = "u_texSplatmap";
static const char* COLORMAP_UNIFORM = "u_texColormap";
static const char* TEX_COLOR_UNIFORM = "u_texColor";


class PaintTerrainCommand : public Lumix::IEditorCommand
{
private:
	struct Item
	{
		int m_center_x;
		int m_center_y;
		int m_radius;
		float m_amount;
		Lumix::Vec3 m_color;
	};


public:
	struct Rectangle
	{
		int m_from_x;
		int m_from_y;
		int m_to_x;
		int m_to_y;
	};


	PaintTerrainCommand(Lumix::WorldEditor& editor,
						TerrainEditor::Type type,
						int texture_idx,
						const Lumix::Vec3& hit_pos,
						float radius,
						float rel_amount,
						Lumix::Vec3 color,
						Lumix::ComponentUID terrain,
						bool can_be_merged)
		: m_world_editor(editor)
		, m_terrain(terrain)
		, m_can_be_merged(can_be_merged)
		, m_new_data(editor.getAllocator())
		, m_old_data(editor.getAllocator())
		, m_items(editor.getAllocator())
		, m_type(type)
		, m_texture_idx(texture_idx)
	{
		m_width = m_height = m_x = m_y = -1;
		Lumix::Matrix entity_mtx =
			editor.getUniverse()->getMatrix(terrain.entity);
		entity_mtx.fastInverse();
		Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
		float xz_scale = static_cast<Lumix::RenderScene*>(terrain.scene)
							 ->getTerrainXZScale(terrain.index);
		local_pos = local_pos / xz_scale;
		auto hm = getMaterial()->getTextureByUniform(HEIGHTMAP_UNIFORM);
		auto texture = getDestinationTexture();

		Item& item = m_items.pushEmpty();
		item.m_center_x =
			(int)(local_pos.x / hm->getWidth() * texture->getWidth());
		item.m_center_y =
			(int)(local_pos.z / hm->getHeight() * texture->getHeight());
		item.m_radius = (int)radius;
		item.m_amount = rel_amount;
		item.m_color = color;
	}


	virtual void serialize(Lumix::JsonSerializer& serializer) override
	{
		serializer.serialize("type", (int)m_type);
		serializer.serialize("texture_idx", m_texture_idx);
		serializer.beginArray("items");
		for (int i = 0; i < m_items.size(); ++i)
		{
			serializer.serializeArrayItem(m_items[i].m_amount);
			serializer.serializeArrayItem(m_items[i].m_center_x);
			serializer.serializeArrayItem(m_items[i].m_center_y);
			serializer.serializeArrayItem(m_items[i].m_radius);
			serializer.serializeArrayItem(m_items[i].m_color.x);
			serializer.serializeArrayItem(m_items[i].m_color.y);
			serializer.serializeArrayItem(m_items[i].m_color.z);
		}
		serializer.endArray();
	}


	virtual void deserialize(Lumix::JsonSerializer& serializer) override
	{
		m_items.clear();
		int type;
		serializer.deserialize("type", type, 0);
		m_type = (TerrainEditor::Type)type;
		serializer.deserialize("texture_idx", m_texture_idx, 0);
		serializer.deserializeArrayBegin("items");
		while (!serializer.isArrayEnd())
		{
			Item& item = m_items.pushEmpty();
			serializer.deserializeArrayItem(item.m_amount, 0);
			serializer.deserializeArrayItem(item.m_center_x, 0);
			serializer.deserializeArrayItem(item.m_center_y, 0);
			serializer.deserializeArrayItem(item.m_radius, 0);
			serializer.deserializeArrayItem(item.m_color.x, 0);
			serializer.deserializeArrayItem(item.m_color.y, 0);
			serializer.deserializeArrayItem(item.m_color.z, 0);
		}
		serializer.deserializeArrayEnd();
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


	virtual void undo() override { applyData(m_old_data); }


	virtual uint32_t getType() override
	{
		static const uint32_t type = Lumix::crc32("paint_terrain");
		return type;
	}


	virtual bool merge(IEditorCommand& command) override
	{
		if (!m_can_be_merged)
		{
			return false;
		}
		PaintTerrainCommand& my_command =
			static_cast<PaintTerrainCommand&>(command);
		if (m_terrain == my_command.m_terrain && m_type == my_command.m_type &&
			m_texture_idx == my_command.m_texture_idx)
		{
			my_command.m_items.push(m_items.back());
			my_command.resizeData();
			my_command.rasterItem(
				getDestinationTexture(), my_command.m_new_data, m_items.back());
			return true;
		}
		return false;
	}


private:
	Lumix::Material* getMaterial()
	{
		auto* material = static_cast<Lumix::RenderScene*>(m_terrain.scene)
							 ->getTerrainMaterial(m_terrain.index);
		return static_cast<Lumix::Material*>(
			m_world_editor.getEngine()
				.getResourceManager()
				.get(Lumix::ResourceManager::MATERIAL)
				->get(Lumix::Path(material->getPath().c_str())));
	}


	Lumix::Texture* getDestinationTexture()
	{
		const char* uniform_name = "";
		switch (m_type)
		{
			case TerrainEditor::LAYER:
				uniform_name = SPLATMAP_UNIFORM;
				break;
			case TerrainEditor::COLOR:
				uniform_name = COLORMAP_UNIFORM;
				break;
			default:
				uniform_name = HEIGHTMAP_UNIFORM;
				break;
		}

		return getMaterial()->getTextureByUniform(uniform_name);
	}


	int computeAverage32(const Lumix::Texture* texture,
						 int from_x,
						 int to_x,
						 int from_y,
						 int to_y)
	{
		ASSERT(texture->getBytesPerPixel() == 4);
		uint64_t sum = 0;
		int texture_width = texture->getWidth();
		for (int i = from_x, end = to_x; i < end; ++i)
		{
			for (int j = from_y, end2 = to_y; j < end2; ++j)
			{
				sum += texture->getData()[4 * (i + j * texture_width)];
			}
		}
		return sum / (to_x - from_x) / (to_y - from_y);
	}


	uint16_t computeAverage16(const Lumix::Texture* texture,
							  int from_x,
							  int to_x,
							  int from_y,
							  int to_y)
	{
		ASSERT(texture->getBytesPerPixel() == 2);
		uint32_t sum = 0;
		int texture_width = texture->getWidth();
		for (int i = from_x, end = to_x; i < end; ++i)
		{
			for (int j = from_y, end2 = to_y; j < end2; ++j)
			{
				sum += ((uint16_t*)texture->getData())[(i + j * texture_width)];
			}
		}
		return uint16_t(sum / (to_x - from_x) / (to_y - from_y));
	}


	float getAttenuation(Item& item, int i, int j) const
	{
		float dist = sqrt((item.m_center_x - i) * (item.m_center_x - i) +
						  (item.m_center_y - j) * (item.m_center_y - j));
		return 1.0f - Lumix::Math::minValue(dist / item.m_radius, 1.0f);
	}


	void rasterColorItem(Lumix::Texture* texture, Lumix::Array<uint8_t>& data, Item& item)
	{
		int texture_width = texture->getWidth();
		int from_x =
			Lumix::Math::maxValue((int)(item.m_center_x - item.m_radius), 0);
		int to_x = Lumix::Math::minValue((int)(item.m_center_x + item.m_radius),
			texture_width);
		int from_z =
			Lumix::Math::maxValue((int)(item.m_center_y - item.m_radius), 0);
		int to_z = Lumix::Math::minValue((int)(item.m_center_y + item.m_radius),
			texture_width);

		if (texture->getBytesPerPixel() != 4)
		{
			ASSERT(false);
			return;
		}
		for (int i = from_x, end = to_x; i < end; ++i)
		{
			for (int j = from_z, end2 = to_z; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j);
				int offset = 4 * (i - m_x + (j - m_y) * m_width);
				uint8_t* d = &data[offset];
				d[0] += uint8_t((item.m_color.x * 255 - d[0]) * attenuation);
				d[1] += uint8_t((item.m_color.y * 255 - d[1]) * attenuation);
				d[2] += uint8_t((item.m_color.z * 255 - d[2]) * attenuation);
				d[3] = 255;
			}
		}
	}


	void
	rasterItem(Lumix::Texture* texture, Lumix::Array<uint8_t>& data, Item& item)
	{
		if (m_type == TerrainEditor::COLOR)
		{
			rasterColorItem(texture, data, item);
			return;
		}

		int texture_width = texture->getWidth();
		int from_x =
			Lumix::Math::maxValue((int)(item.m_center_x - item.m_radius), 0);
		int to_x = Lumix::Math::minValue((int)(item.m_center_x + item.m_radius),
										 texture_width);
		int from_z =
			Lumix::Math::maxValue((int)(item.m_center_y - item.m_radius), 0);
		int to_z = Lumix::Math::minValue((int)(item.m_center_y + item.m_radius),
										 texture_width);

		int avg = 0;
		float avg16 = 0;
		float strength_multiplicator = 256.0f;
		if (texture->getBytesPerPixel() == 4)
		{
			avg = m_type == TerrainEditor::SMOOTH_HEIGHT
					  ? computeAverage32(texture, from_x, to_x, from_z, to_z)
					  : 0;
			strength_multiplicator = 16.0f;
		}
		else
		{
			avg16 = m_type == TerrainEditor::SMOOTH_HEIGHT
						? computeAverage16(texture, from_x, to_x, from_z, to_z)
						: 0;
		}
		float amount = Lumix::Math::maxValue(
			item.m_amount * item.m_amount * strength_multiplicator, 1.0f);
		if (m_type == TerrainEditor::LOWER_HEIGHT)
		{
			amount = -amount;
		}


		for (int i = from_x, end = to_x; i < end; ++i)
		{
			for (int j = from_z, end2 = to_z; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j);
				int offset = i - m_x + (j - m_y) * m_width;
				switch (texture->getBytesPerPixel())
				{
					case 4:
					{
						int add = attenuation * amount;
						if (m_type == TerrainEditor::LAYER)
						{
							data[4 * offset] = m_texture_idx;
							data[4 * offset + 1] = add;
							data[4 * offset + 2] = 0;
							data[4 * offset + 3] = 255;
						}
						else
						{
							if (m_type == TerrainEditor::SMOOTH_HEIGHT)
							{
								add =
									(avg -
									 texture
										 ->getData()[4 *
													 (i + j * texture_width)]) *
									item.m_amount * attenuation;
							}
							else if (add > 0)
							{
								add = Lumix::Math::minValue(
									add,
									255 -
										texture->getData()
											[4 * (i + j * texture_width)]);
							}
							else
							{
								add = Lumix::Math::maxValue(
									add,
									0 -
										texture->getData()
											[4 * (i + j * texture_width)]);
							}
							data[offset * 4] += add;
							data[offset * 4 + 1] += add;
							data[offset * 4 + 2] += add;
							data[offset * 4 + 3] = 255;
						}
						break;
					}
					case 2:
					{
						uint16_t add = uint16_t(attenuation * amount);
						uint16_t x =
							((uint16_t*)
								 texture->getData())[(i + j * texture_width)];
						x += m_type == TerrainEditor::SMOOTH_HEIGHT
								 ? (avg16 - x) * item.m_amount * attenuation
								 : add;
						((uint16_t*)&data[0])[offset] = x;
						break;
					}
					default:
						ASSERT(false);
						break;
				}
			}
		}
	}


	void generateNewData()
	{
		auto texture = getDestinationTexture();
		int bpp = texture->getBytesPerPixel();
		Rectangle rect;
		getBoundingRectangle(texture, rect);
		m_new_data.resize(bpp * (rect.m_to_x - rect.m_from_x) *
						  (rect.m_to_y - rect.m_from_y));
		memcpy(&m_new_data[0], &m_old_data[0], m_new_data.size());

		for (int item_index = 0; item_index < m_items.size(); ++item_index)
		{
			Item& item = m_items[item_index];
			rasterItem(texture, m_new_data, item);
		}
	}


	void saveOldData()
	{
		auto texture = getDestinationTexture();
		int bpp = texture->getBytesPerPixel();
		Rectangle rect;
		getBoundingRectangle(texture, rect);
		m_x = rect.m_from_x;
		m_y = rect.m_from_y;
		m_width = rect.m_to_x - rect.m_from_x;
		m_height = rect.m_to_y - rect.m_from_y;
		m_old_data.resize(bpp * (rect.m_to_x - rect.m_from_x) *
						  (rect.m_to_y - rect.m_from_y));

		int index = 0;
		for (int j = rect.m_from_y, end2 = rect.m_to_y; j < end2; ++j)
		{
			for (int i = rect.m_from_x, end = rect.m_to_x; i < end; ++i)
			{
				for (int k = 0; k < bpp; ++k)
				{
					m_old_data[index] =
						texture->getData()[(i + j * texture->getWidth()) * bpp +
										   k];
					++index;
				}
			}
		}
	}


	void applyData(Lumix::Array<uint8_t>& data)
	{
		auto texture = getDestinationTexture();
		int bpp = texture->getBytesPerPixel();

		for (int j = m_y; j < m_y + m_height; ++j)
		{
			for (int i = m_x; i < m_x + m_width; ++i)
			{
				int index = bpp * (i + j * texture->getWidth());
				for (int k = 0; k < bpp; ++k)
				{
					texture->getData()[index + k] =
						data[bpp * (i - m_x + (j - m_y) * m_width) + k];
				}
			}
		}
		texture->onDataUpdated();
	}


	void resizeData()
	{
		Lumix::Array<uint8_t> new_data(m_world_editor.getAllocator());
		Lumix::Array<uint8_t> old_data(m_world_editor.getAllocator());
		auto texture = getDestinationTexture();
		Rectangle rect;
		getBoundingRectangle(texture, rect);

		int new_w = rect.m_to_x - rect.m_from_x;
		int bpp = texture->getBytesPerPixel();
		new_data.resize(bpp * new_w * (rect.m_to_y - rect.m_from_y));
		old_data.resize(bpp * new_w * (rect.m_to_y - rect.m_from_y));

		// original
		for (int row = rect.m_from_y; row < rect.m_to_y; ++row)
		{
			memcpy(&new_data[(row - rect.m_from_y) * new_w * bpp],
				   &texture->getData()[row * bpp * texture->getWidth() +
									   rect.m_from_x * bpp],
				   bpp * new_w);
			memcpy(&old_data[(row - rect.m_from_y) * new_w * bpp],
				   &texture->getData()[row * bpp * texture->getWidth() +
									   rect.m_from_x * bpp],
				   bpp * new_w);
		}

		// new
		for (int row = 0; row < m_height; ++row)
		{
			memcpy(&new_data[((row + m_y - rect.m_from_y) * new_w + m_x -
							  rect.m_from_x) *
							 bpp],
				   &m_new_data[row * bpp * m_width],
				   bpp * m_width);
			memcpy(&old_data[((row + m_y - rect.m_from_y) * new_w + m_x -
							  rect.m_from_x) *
							 bpp],
				   &m_old_data[row * bpp * m_width],
				   bpp * m_width);
		}

		m_x = rect.m_from_x;
		m_y = rect.m_from_y;
		m_height = rect.m_to_y - rect.m_from_y;
		m_width = rect.m_to_x - rect.m_from_x;

		m_new_data.swap(new_data);
		m_old_data.swap(old_data);
	}


	void getBoundingRectangle(Lumix::Texture* texture, Rectangle& rect)
	{
		Item& item = m_items[0];
		rect.m_from_x =
			Lumix::Math::maxValue(item.m_center_x - item.m_radius, 0);
		rect.m_to_x = Lumix::Math::minValue(item.m_center_x + item.m_radius,
											texture->getWidth());
		rect.m_from_y =
			Lumix::Math::maxValue(item.m_center_y - item.m_radius, 0);
		rect.m_to_y = Lumix::Math::minValue(item.m_center_y + item.m_radius,
											texture->getHeight());
		for (int i = 1; i < m_items.size(); ++i)
		{
			Item& item = m_items[i];
			rect.m_from_x = Lumix::Math::minValue(
				item.m_center_x - item.m_radius, rect.m_from_x);
			rect.m_to_x = Lumix::Math::maxValue(item.m_center_x + item.m_radius,
												rect.m_to_x);
			rect.m_from_y = Lumix::Math::minValue(
				item.m_center_y - item.m_radius, rect.m_from_y);
			rect.m_to_y = Lumix::Math::maxValue(item.m_center_y + item.m_radius,
												rect.m_to_y);
		}
		rect.m_from_x = Lumix::Math::maxValue(rect.m_from_x, 0);
		rect.m_to_x = Lumix::Math::minValue(rect.m_to_x, texture->getWidth());
		rect.m_from_y = Lumix::Math::maxValue(rect.m_from_y, 0);
		rect.m_to_y = Lumix::Math::minValue(rect.m_to_y, texture->getHeight());
	}


private:
	Lumix::Array<uint8_t> m_new_data;
	Lumix::Array<uint8_t> m_old_data;
	int m_texture_idx;
	int m_width;
	int m_height;
	int m_x;
	int m_y;
	TerrainEditor::Type m_type;
	Lumix::Array<Item> m_items;
	Lumix::ComponentUID m_terrain;
	Lumix::WorldEditor& m_world_editor;
	bool m_can_be_merged;
};


TerrainComponentPlugin::TerrainComponentPlugin(MainWindow& main_window)
	: m_main_window(main_window)
{
	m_tools_item = nullptr;
	m_texture_tool_item = nullptr;
	m_terrain_editor =
		new TerrainEditor(main_window.getWorldEditor(), main_window, *this);
	connect(
		main_window.getPropertyView(),
		&PropertyView::componentNodeCreated,
		[this](DynamicObjectModel::Node& node, const Lumix::ComponentUID& cmp)
		{
			if (cmp.type == Lumix::crc32("terrain"))
			{
				createEditor(node, cmp);
			}
		});
}


TerrainComponentPlugin::~TerrainComponentPlugin()
{
	delete m_terrain_editor;
}


void TerrainComponentPlugin::createEditor(DynamicObjectModel::Node& node,
										  const Lumix::ComponentUID& component)
{
	m_terrain_editor->m_component = component;
	if (!m_terrain_editor->getMaterial() ||
		!m_terrain_editor->getMaterial()->isReady())
	{
		return;
	}
	auto splat_map =
		m_terrain_editor->getMaterial()->getTextureByUniform(SPLATMAP_UNIFORM);
	if (splat_map)
	{
		splat_map->addDataReference();
	}

	auto& tools_node = node.addChild("Tools");
	tools_node.m_getter = []()
	{
		return QVariant();
	};
	auto& save_node = tools_node.addChild("Save");
	save_node.enablePeristentEditor();
	save_node.m_getter = []()
	{
		return QVariant();
	};
	save_node.m_setter = [](const QVariant&)
	{
	};
	save_node.onCreateEditor = [this](QWidget* parent,
									  const QStyleOptionViewItem&)
	{
		auto container = new QWidget(parent);
		auto layout = new QHBoxLayout(container);
		auto* height_button = new QPushButton("Heightmap", container);
		auto* texture_button = new QPushButton("Splatmap", container);
		height_button->connect(
			height_button,
			&QPushButton::clicked,
			[this]()
			{
				Lumix::Material* material = m_terrain_editor->getMaterial();
				material->getTextureByUniform(HEIGHTMAP_UNIFORM)->save();
			});
		texture_button->connect(
			texture_button,
			&QPushButton::clicked,
			[this]()
			{
				Lumix::Material* material = m_terrain_editor->getMaterial();
				material->getTextureByUniform(SPLATMAP_UNIFORM)->save();
			});
		layout->addWidget(height_button);
		layout->addWidget(texture_button);

		Lumix::Material* material = m_terrain_editor->getMaterial();
		if (material->getTextureByUniform(COLORMAP_UNIFORM))
		{
			auto* colormap_button = new QPushButton("Colormap", container);
			colormap_button->connect(
				colormap_button,
				&QPushButton::clicked,
				[this]()
			{
				Lumix::Material* material = m_terrain_editor->getMaterial();
				material->getTextureByUniform(COLORMAP_UNIFORM)->save();
			});
			layout->addWidget(colormap_button);
		}

		layout->setContentsMargins(0, 0, 0, 0);
		layout->addStretch();
		return container;
	};

	auto& brush_size_node = tools_node.addChild("Brush size");
	brush_size_node.m_getter = [this]() -> QVariant
	{
		return m_terrain_editor->m_terrain_brush_size;
	};
	brush_size_node.m_setter = [this](const QVariant& value)
	{
		m_terrain_editor->m_terrain_brush_size = value.toInt();
	};
	brush_size_node.enablePeristentEditor();
	DynamicObjectModel::setSliderEditor(brush_size_node, 1, 100, 1);

	auto& brush_strength_node = tools_node.addChild("Brush strength");
	brush_strength_node.m_getter = [this]() -> QVariant
	{
		return m_terrain_editor->m_terrain_brush_strength;
	};
	brush_strength_node.m_setter = [this](const QVariant& value)
	{
		m_terrain_editor->m_terrain_brush_strength = value.toFloat();
	};
	brush_strength_node.enablePeristentEditor();
	DynamicObjectModel::setSliderEditor(
		brush_strength_node, 0.01f, 1.0f, 0.01f);

	auto& brush_type_node = tools_node.addChild("Brush type");
	brush_type_node.m_getter = [this]()
	{
		return QVariant();
	};
	brush_type_node.m_setter = [this](const QVariant&)
	{
	};
	brush_type_node.enablePeristentEditor();
	int last_node_index = brush_type_node.m_index;
	brush_type_node.onCreateEditor = [this, &tools_node, last_node_index](
		QWidget* parent, const QStyleOptionViewItem&)
	{
		auto editor = new QComboBox(parent);
		editor->addItem("Raise height");
		editor->addItem("Lower height");
		editor->addItem("Smooth height");
		editor->addItem("Layers");
		editor->addItem("Entity");
		auto* material = m_terrain_editor->getMaterial();
		if (material->getTextureByUniform(COLORMAP_UNIFORM))
		{
			editor->addItem("Color");
		}
		m_terrain_editor->m_type = TerrainEditor::RAISE_HEIGHT;
		connect(editor,
				(void (QComboBox::*)(int)) & QComboBox::currentIndexChanged,
				[this, &tools_node, last_node_index](int index)
				{
					while (tools_node.m_children.size() > last_node_index + 1)
					{
						auto model = static_cast<DynamicObjectModel*>(
							m_main_window.getPropertyView()->getModel());
						model->removeNode(*tools_node.m_children.back());
					}
					m_terrain_editor->m_type = (TerrainEditor::Type)index;
					switch (m_terrain_editor->m_type)
					{
						case TerrainEditor::RAISE_HEIGHT:
						case TerrainEditor::LOWER_HEIGHT:
						case TerrainEditor::SMOOTH_HEIGHT:
							break;
						case TerrainEditor::LAYER:
							addTextureNode(tools_node);
							break;
						case TerrainEditor::ENTITY:
							addEntityTemplateNode(tools_node);
							break;
						case TerrainEditor::COLOR:
							addColorNode(tools_node);
							break;
						default:
							ASSERT(false);
					}
				});
		return editor;
	};
}


void TerrainComponentPlugin::addColorNode(DynamicObjectModel::Node& node)
{
	auto model = static_cast<DynamicObjectModel*>(
		m_main_window.getPropertyView()->getModel());
	model->childAboutToBeAdded(node);
	auto& child = node.addChild("Color");
	child.m_getter = [this]() -> QVariant
	{
		return m_selected_color;
	};
	child.m_setter = [this](const QVariant& value)
	{
		m_selected_color = value.value<QColor>();
	};
	model->childAdded();
}


void TerrainComponentPlugin::addTextureNode(DynamicObjectModel::Node& node)
{
	auto model = static_cast<DynamicObjectModel*>(
		m_main_window.getPropertyView()->getModel());
	model->childAboutToBeAdded(node);
	auto& child = node.addChild("Layer");
	child.m_getter = []()
	{
		return QVariant();
	};
	child.m_setter = [](const QVariant&)
	{
	};

	child.onCreateEditor = [this](QWidget* parent, const QStyleOptionViewItem&)
	{
		auto material = m_terrain_editor->getMaterial();
		QComboBox* cb = new QComboBox(parent);

		Lumix::Texture* tex = material->getTextureByUniform(TEX_COLOR_UNIFORM);

		for (int i = 0; i < tex->getDepth(); ++i)
		{
			cb->addItem(QString::number(1 + i));
		}

		connect(cb,
				(void (QComboBox::*)(int)) & QComboBox::activated,
				[this](int index)
				{
					m_terrain_editor->m_texture_idx = index;
				});
		return cb;
	};
	child.enablePeristentEditor();
	model->childAdded();
}


void TerrainComponentPlugin::addEntityTemplateNode(
	DynamicObjectModel::Node& node)
{
	auto model = static_cast<DynamicObjectModel*>(
		m_main_window.getPropertyView()->getModel());
	model->childAboutToBeAdded(node);
	auto& child = node.addChild("Entity template");
	child.m_getter = []()
	{
		return QVariant();
	};
	child.m_setter = [](const QVariant&)
	{
	};
	child.onCreateEditor = [this](QWidget* parent, const QStyleOptionViewItem&)
	{
		auto& templates = m_main_window.getWorldEditor()
							  .getEntityTemplateSystem()
							  .getTemplateNames();
		QComboBox* cb = new QComboBox(parent);
		m_terrain_editor->m_selected_entity_template =
			templates.empty() ? "" : templates[0].c_str();
		for (int i = 0; i < templates.size(); ++i)
		{
			cb->addItem(templates[i].c_str());
		}
		connect(cb,
				(void (QComboBox::*)(const QString&)) & QComboBox::activated,
				[this](const QString& name)
				{
					m_terrain_editor->m_selected_entity_template = name;
				});
		return cb;
	};
	child.enablePeristentEditor();
	model->childAdded();
}


TerrainEditor::~TerrainEditor()
{
	m_world_editor.removePlugin(*this);
}


TerrainEditor::TerrainEditor(Lumix::WorldEditor& editor,
							 MainWindow& main_window,
							 TerrainComponentPlugin& plugin)
	: m_world_editor(editor)
	, m_main_window(main_window)
	, m_plugin(plugin)
{
	editor.addPlugin(*this);
	m_texture_tree_item = nullptr;
	m_terrain_brush_size = 10;
	m_terrain_brush_strength = 0.1f;
	m_type = NOT_SET;
	m_texture_idx = 0;
}


void TerrainEditor::drawCursor(Lumix::RenderScene& scene,
							   const Lumix::ComponentUID& terrain,
							   const Lumix::Vec3& center)
{
	static const int SLICE_COUNT = 30;

	Lumix::Matrix terrain_matrix =
		m_world_editor.getUniverse()->getMatrix(m_component.entity);
	Lumix::Matrix inv_terrain_matrix = terrain_matrix;
	inv_terrain_matrix.inverse();

	float w, h;
	scene.getTerrainSize(terrain.index, &w, &h);

	for (int i = 0; i < SLICE_COUNT + 1; ++i)
	{
		float angle_step = Lumix::Math::PI * 2 / SLICE_COUNT;
		float angle = i * angle_step;
		float next_angle = i * angle_step + angle_step;
		Lumix::Vec3 from =
			center +
			Lumix::Vec3(cos(angle), 0, sin(angle)) * m_terrain_brush_size;
		Lumix::Vec3 to = center +
						 Lumix::Vec3(cos(next_angle), 0, sin(next_angle)) *
							 m_terrain_brush_size;


		Lumix::Vec3 local_from = inv_terrain_matrix.multiplyPosition(from);
		if (local_from.x >= 0 && local_from.z >= 0 && local_from.x <= w &&
			local_from.z <= h)
		{
			from.y = terrain_matrix.m42 + 0.25f +
					 scene.getTerrainHeightAt(
						 terrain.index, local_from.x, local_from.z);
		}

		Lumix::Vec3 local_to = inv_terrain_matrix.multiplyPosition(to);
		if (local_to.x >= 0 && local_to.z >= 0 && local_to.x <= w &&
			local_to.z <= h)
		{
			to.y =
				terrain_matrix.m42 + 0.25f +
				scene.getTerrainHeightAt(terrain.index, local_to.x, local_to.z);
		}

		scene.addDebugLine(from, to, 0xffff0000, 0);
	}
}


void TerrainEditor::tick()
{
	float mouse_x = m_world_editor.getMouseX();
	float mouse_y = m_world_editor.getMouseY();

	if (m_type != NOT_SET)
	{
		for (int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0;
			 --i)
		{
			Lumix::ComponentUID terrain = m_world_editor.getComponent(
				m_world_editor.getSelectedEntities()[i],
				Lumix::crc32("terrain"));
			if (terrain.isValid())
			{
				Lumix::ComponentUID camera_cmp = m_world_editor.getEditCamera();
				Lumix::RenderScene* scene =
					static_cast<Lumix::RenderScene*>(camera_cmp.scene);
				Lumix::Vec3 origin, dir;
				scene->getRay(camera_cmp.index,
							  (float)mouse_x,
							  (float)mouse_y,
							  origin,
							  dir);
				Lumix::RayCastModelHit hit =
					scene->castRay(origin, dir, Lumix::INVALID_COMPONENT);
				if (hit.m_is_hit)
				{
					Lumix::Vec3 center = hit.m_origin + hit.m_dir * hit.m_t;
					scene->setTerrainBrush(
						terrain.index, center, m_terrain_brush_size);
					drawCursor(*scene, terrain, center);
					return;
				}
				scene->setTerrainBrush(terrain.index, Lumix::Vec3(0, 0, 0), 1);
			}
		}
	}
}


bool TerrainEditor::onEntityMouseDown(const Lumix::RayCastModelHit& hit,
									  int,
									  int)
{
	if (m_type == NOT_SET)
	{
		return false;
	}

	for (int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0; --i)
	{
		if (m_world_editor.getSelectedEntities()[i] == hit.m_entity)
		{
			Lumix::ComponentUID terrain = m_world_editor.getComponent(
				hit.m_entity, Lumix::crc32("terrain"));
			if (terrain.isValid())
			{
				Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
				switch (m_type)
				{
					case RAISE_HEIGHT:
					case LOWER_HEIGHT:
					case SMOOTH_HEIGHT:
					case COLOR:
					case LAYER:
						paint(hit, m_type, false);
						break;
					case ENTITY:
						m_main_window.getEntityList()->enableUpdate(false);
						paintEntities(hit);
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


void TerrainEditor::onMouseMove(int x, int y, int, int, int)
{
	Lumix::ComponentUID camera_cmp = m_world_editor.getEditCamera();
	Lumix::RenderScene* scene =
		static_cast<Lumix::RenderScene*>(camera_cmp.scene);
	Lumix::Vec3 origin, dir;
	scene->getRay(camera_cmp.index, (float)x, (float)y, origin, dir);
	Lumix::RayCastModelHit hit =
		scene->castRayTerrain(m_component.index, origin, dir);
	if (hit.m_is_hit)
	{
		Lumix::ComponentUID terrain =
			m_world_editor.getComponent(hit.m_entity, Lumix::crc32("terrain"));
		if (terrain.isValid())
		{
			switch (m_type)
			{
				case RAISE_HEIGHT:
				case LOWER_HEIGHT:
				case SMOOTH_HEIGHT:
				case COLOR:
				case LAYER:
					paint(hit, m_type, true);
					break;
				case ENTITY:
					paintEntities(hit);
					break;
				default:
					ASSERT(false);
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
	auto* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
	return scene->getTerrainMaterial(m_component.index);
}


void TerrainEditor::getProjections(const Lumix::Vec3& axis,
								   const Lumix::Vec3 vertices[8],
								   float& min,
								   float& max)
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


bool TerrainEditor::testOBBCollision(const Lumix::Matrix& matrix_a,
									 const Lumix::Model* model_a,
									 const Lumix::Matrix& matrix_b,
									 const Lumix::Model* model_b,
									 float scale)
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

	Lumix::Vec3 normals[] = {
		matrix_a.getXVector(), matrix_a.getYVector(), matrix_a.getZVector()};
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

	Lumix::Vec3 normals_b[] = {
		matrix_b.getXVector(), matrix_b.getYVector(), matrix_b.getZVector()};
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


bool TerrainEditor::isOBBCollision(Lumix::RenderScene* scene,
								   const Lumix::Matrix& matrix,
								   Lumix::Model* model,
								   float scale)
{
	Lumix::Vec3 pos_a = matrix.getTranslation();
	static Lumix::Array<Lumix::RenderableMesh> meshes(
		m_world_editor.getAllocator());
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
			if (testOBBCollision(matrix,
								 model,
								 *meshes[i].m_matrix,
								 meshes[i].m_model,
								 scale))
			{
				return true;
			}
		}
	}
	return false;
}


void TerrainEditor::paintEntities(const Lumix::RayCastModelHit& hit)
{
	Lumix::RenderScene* scene =
		static_cast<Lumix::RenderScene*>(m_component.scene);
	Lumix::Vec3 center_pos = hit.m_origin + hit.m_dir * hit.m_t;
	Lumix::Matrix terrain_matrix =
		m_world_editor.getUniverse()->getMatrix(m_component.entity);
	Lumix::Matrix inv_terrain_matrix = terrain_matrix;
	inv_terrain_matrix.inverse();
	if (m_selected_entity_template.isEmpty())
	{
		return;
	}
	Lumix::Entity tpl = m_world_editor.getEntityTemplateSystem().getInstances(
		Lumix::crc32(m_selected_entity_template.toLatin1().data()))[0];
	if (tpl < 0)
	{
		return;
	}
	Lumix::ComponentUID renderable =
		m_world_editor.getComponent(tpl, RENDERABLE_HASH);
	if (renderable.isValid())
	{
		float w, h;
		scene->getTerrainSize(m_component.index, &w, &h);
		float scale =
			1.0f - Lumix::Math::maxValue(0.01f, m_terrain_brush_strength);
		Lumix::Model* model = scene->getRenderableModel(renderable.index);
		for (int i = 0;
			 i <= m_terrain_brush_size * m_terrain_brush_size / 1000.0f;
			 ++i)
		{
			float angle = (float)(rand() % 360);
			float dist = (rand() % 100 / 100.0f) * m_terrain_brush_size;
			Lumix::Vec3 pos(center_pos.x + cos(angle) * dist,
							0,
							center_pos.z + sin(angle) * dist);
			Lumix::Vec3 terrain_pos = inv_terrain_matrix.multiplyPosition(pos);
			if (terrain_pos.x >= 0 && terrain_pos.z >= 0 &&
				terrain_pos.x <= w && terrain_pos.z <= h)
			{
				pos.y = scene->getTerrainHeightAt(
					m_component.index, terrain_pos.x, terrain_pos.z);
				Lumix::Matrix mtx = Lumix::Matrix::IDENTITY;
				mtx.setTranslation(pos);
				if (!isOBBCollision(scene, mtx, model, scale))
				{
					m_world_editor.getEntityTemplateSystem().createInstance(
						m_selected_entity_template.toLatin1().data(), pos);
				}
			}
		}
	}
}


void TerrainEditor::paint(const Lumix::RayCastModelHit& hit,
						  Type type,
						  bool old_stroke)
{
	Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
	Lumix::Vec3 color;
	color.x = m_plugin.getSelectedColor().redF();
	color.y = m_plugin.getSelectedColor().greenF();
	color.z = m_plugin.getSelectedColor().blueF();
	PaintTerrainCommand* command =
		m_world_editor.getAllocator().newObject<PaintTerrainCommand>(
			m_world_editor,
			type,
			m_texture_idx,
			hit_pos,
			(float)m_terrain_brush_size,
			m_terrain_brush_strength,
			color,
			m_component,
			old_stroke);
	m_world_editor.executeCommand(command);
}
