#include "terrain_editor.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/json_serializer.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/entity_template_system.h"
#include "editor/ieditor_command.h"
#include "editor/imgui/imgui.h"
#include "editor/platform_interface.h"
#define STB_IMAGE_IMPLEMENTATION
#include "editor/stb/stb_image.h"
#include "editor/utils.h"
#include "engine.h"
#include "engine/iproperty_descriptor.h"
#include "engine/property_register.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "universe/universe.h"
#include <cmath>


static const Lumix::uint32 RENDERABLE_HASH = Lumix::crc32("renderable");
static const Lumix::uint32 TERRAIN_HASH = Lumix::crc32("terrain");
static const char* HEIGHTMAP_UNIFORM = "u_texHeightmap";
static const char* SPLATMAP_UNIFORM = "u_texSplatmap";
static const char* COLORMAP_UNIFORM = "u_texColormap";
static const char* TEX_COLOR_UNIFORM = "u_texColor";
static const float MIN_BRUSH_SIZE = 0.5f;


struct PaintTerrainCommand : public Lumix::IEditorCommand
{
	struct Rectangle
	{
		int m_from_x;
		int m_from_y;
		int m_to_x;
		int m_to_y;
	};


	PaintTerrainCommand(Lumix::WorldEditor& editor)
		: m_world_editor(editor)
		, m_new_data(editor.getAllocator())
		, m_old_data(editor.getAllocator())
		, m_items(editor.getAllocator())
		, m_mask(editor.getAllocator())
	{
	}


	PaintTerrainCommand(Lumix::WorldEditor& editor,
		TerrainEditor::Type type,
		int texture_idx,
		const Lumix::Vec3& hit_pos,
		Lumix::BinaryArray& mask,
		float radius,
		float rel_amount,
		Lumix::uint16 flat_height,
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
		, m_mask(editor.getAllocator())
		, m_flat_height(flat_height)
	{
		m_mask.resize(mask.size());
		for (int i = 0; i < mask.size(); ++i)
		{
			m_mask[i] = mask[i];
		}

		m_width = m_height = m_x = m_y = -1;
		Lumix::Matrix entity_mtx =
			editor.getUniverse()->getMatrix(terrain.entity);
		entity_mtx.fastInverse();
		Lumix::Vec3 local_pos = entity_mtx.multiplyPosition(hit_pos);
		float xz_scale = static_cast<Lumix::RenderScene*>(terrain.scene)
							 ->getTerrainXZScale(terrain.index);
		local_pos = local_pos / xz_scale;
		local_pos.y = -1;

		Item& item = m_items.emplace();
		item.m_local_pos = local_pos;
		item.m_radius = radius;
		item.m_amount = rel_amount;
		item.m_color = color;
	}


	void serialize(Lumix::JsonSerializer& serializer) override
	{
		serializer.serialize("type", (int)m_type);
		serializer.serialize("texture_idx", m_texture_idx);
		serializer.beginArray("items");
		for (int i = 0; i < m_items.size(); ++i)
		{
			serializer.serializeArrayItem(m_items[i].m_amount);
			serializer.serializeArrayItem(m_items[i].m_local_pos.x);
			serializer.serializeArrayItem(m_items[i].m_local_pos.z);
			serializer.serializeArrayItem(m_items[i].m_radius);
			serializer.serializeArrayItem(m_items[i].m_color.x);
			serializer.serializeArrayItem(m_items[i].m_color.y);
			serializer.serializeArrayItem(m_items[i].m_color.z);
		}
		serializer.endArray();
		serializer.beginArray("mask");
		for (int i = 0; i < m_mask.size(); ++i)
		{
			serializer.serializeArrayItem((bool)m_mask[i]);
		}
		serializer.endArray();
	}


	void deserialize(Lumix::JsonSerializer& serializer) override
	{
		m_items.clear();
		int type;
		serializer.deserialize("type", type, 0);
		m_type = (TerrainEditor::Type)type;
		serializer.deserialize("texture_idx", m_texture_idx, 0);
		serializer.deserializeArrayBegin("items");
		while (!serializer.isArrayEnd())
		{
			Item& item = m_items.emplace();
			serializer.deserializeArrayItem(item.m_amount, 0);
			serializer.deserializeArrayItem(item.m_local_pos.x, 0);
			serializer.deserializeArrayItem(item.m_local_pos.z, 0);
			serializer.deserializeArrayItem(item.m_radius, 0);
			serializer.deserializeArrayItem(item.m_color.x, 0);
			serializer.deserializeArrayItem(item.m_color.y, 0);
			serializer.deserializeArrayItem(item.m_color.z, 0);
		}
		serializer.deserializeArrayEnd();
		
		serializer.deserializeArrayBegin("mask");
		m_mask.clear();
		int i = 0;
		while (!serializer.isArrayEnd())
		{
			bool b;
			serializer.deserialize(b, true);
			m_mask[i] = b;
			++i;
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		if (m_new_data.empty())
		{
			saveOldData();
			generateNewData();
		}
		applyData(m_new_data);
		return true;
	}


	void undo() override { applyData(m_old_data); }


	Lumix::uint32 getType() override
	{
		static const Lumix::uint32 type = Lumix::crc32("paint_terrain");
		return type;
	}


	bool merge(IEditorCommand& command) override
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
	struct Item
	{
		Rectangle getBoundingRectangle(int max_x, int max_z) const 
		{
			Rectangle r;
			r.m_from_x = Lumix::Math::maxValue(0, int(m_local_pos.x - m_radius - 0.5f));
			r.m_from_y = Lumix::Math::maxValue(0, int(m_local_pos.z - m_radius - 0.5f));
			r.m_to_x = Lumix::Math::minValue(max_x, int(m_local_pos.x + m_radius + 0.5f));
			r.m_to_y = Lumix::Math::minValue(max_z, int(m_local_pos.z + m_radius + 0.5f));
			return r;
		}

		float m_radius;
		float m_amount;
		Lumix::Vec3 m_local_pos;
		Lumix::Vec3 m_color;
	};

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


	Lumix::uint16 computeAverage16(const Lumix::Texture* texture,
							  int from_x,
							  int to_x,
							  int from_y,
							  int to_y)
	{
		ASSERT(texture->getBytesPerPixel() == 2);
		Lumix::uint32 sum = 0;
		int texture_width = texture->getWidth();
		for (int i = from_x, end = to_x; i < end; ++i)
		{
			for (int j = from_y, end2 = to_y; j < end2; ++j)
			{
				sum += ((Lumix::uint16*)texture->getData())[(i + j * texture_width)];
			}
		}
		return Lumix::uint16(sum / (to_x - from_x) / (to_y - from_y));
	}


	float getAttenuation(Item& item, int i, int j) const
	{
		float dist = sqrt(
			(item.m_local_pos.x - 0.5f - i) * (item.m_local_pos.x - 0.5f - i) +
			(item.m_local_pos.z - 0.5f - j) * (item.m_local_pos.z - 0.5f - j));
		return 1.0f - Lumix::Math::minValue(dist / item.m_radius, 1.0f);
	}


	void rasterColorItem(Lumix::Texture* texture,
						 Lumix::Array<Lumix::uint8>& data,
						 Item& item)
	{
		int texture_width = texture->getWidth();
		Rectangle r =
			item.getBoundingRectangle(texture_width, texture->getHeight());

		if (texture->getBytesPerPixel() != 4)
		{
			ASSERT(false);
			return;
		}
		float fx = 0;
		float fstepx = 1.0f / (r.m_to_x - r.m_from_x);
		float fstepy = 1.0f / (r.m_to_y - r.m_from_y);
		for (int i = r.m_from_x, end = r.m_to_x; i < end; ++i, fx += fstepx)
		{
			float fy = 0;
			for (int j = r.m_from_y, end2 = r.m_to_y; j < end2; ++j, fy += fstepy)
			{
				if (isMasked(fx, fy))
				{
					float attenuation = getAttenuation(item, i, j);
					int offset = 4 * (i - m_x + (j - m_y) * m_width);
					Lumix::uint8* d = &data[offset];
					d[0] += Lumix::uint8((item.m_color.x * 255 - d[0]) * attenuation);
					d[1] += Lumix::uint8((item.m_color.y * 255 - d[1]) * attenuation);
					d[2] += Lumix::uint8((item.m_color.z * 255 - d[2]) * attenuation);
					d[3] = 255;
				}
			}
		}
	}


	bool isMasked(float x, float y)
	{
		if (m_mask.size() == 0) return true;

		int s = int(sqrt(m_mask.size()));
		int ix = int(x * s);
		int iy = int(y * s);

		return m_mask[int(ix + x * iy)];
	}


	void rasterLayerItem(Lumix::Texture* texture,
						 Lumix::Array<Lumix::uint8>& data,
						 Item& item)
	{
		int texture_width = texture->getWidth();
		Rectangle r =
			item.getBoundingRectangle(texture_width, texture->getHeight());

		if (texture->getBytesPerPixel() != 4)
		{
			ASSERT(false);
			return;
		}

		float fx = 0;
		float fstepx = 1.0f / (r.m_to_x - r.m_from_x);
		float fstepy = 1.0f / (r.m_to_y - r.m_from_y);
		for (int i = r.m_from_x, end = r.m_to_x; i < end; ++i, fx += fstepx)
		{
			float fy = 0;
			for (int j = r.m_from_y, end2 = r.m_to_y; j < end2; ++j, fy += fstepy)
			{
				if (isMasked(fx, fy))
				{
					int offset = 4 * (i - m_x + (j - m_y) * m_width);
					float attenuation = getAttenuation(item, i, j);
					int add = int(attenuation * item.m_amount * 255);
					if (add > 0)
					{
						if (data[offset] == m_texture_idx)
						{
							data[offset + 1] += Lumix::Math::minValue(255 - data[offset + 1], add);
						}
						else
						{
							data[offset + 1] = add;
						}
						data[offset] = m_texture_idx;
						data[offset + 2] = 0;
						data[offset + 3] = 255;
					}
				}
			}
		}
	}


	void rasterSmoothHeightItem(Lumix::Texture* texture, Lumix::Array<Lumix::uint8>& data, Item& item)
	{
		ASSERT(texture->getBytesPerPixel() == 2);

		int texture_width = texture->getWidth();
		Rectangle rect;
		rect = item.getBoundingRectangle(texture_width, texture->getHeight());

		float avg = computeAverage16(texture, rect.m_from_x, rect.m_to_x, rect.m_from_y, rect.m_to_y);
		for (int i = rect.m_from_x, end = rect.m_to_x; i < end; ++i)
		{
			for (int j = rect.m_from_y, end2 = rect.m_to_y; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j);
				int offset = i - m_x + (j - m_y) * m_width;
				Lumix::uint16 x = ((Lumix::uint16*)texture->getData())[(i + j * texture_width)];
				x += Lumix::uint16((avg - x) * item.m_amount * attenuation);
				((Lumix::uint16*)&data[0])[offset] = x;
			}
		}
	}


	void rasterFlatHeightItem(Lumix::Texture* texture, Lumix::Array<Lumix::uint8>& data, Item& item)
	{
		ASSERT(texture->getBytesPerPixel() == 2);

		int texture_width = texture->getWidth();
		Rectangle rect;
		rect = item.getBoundingRectangle(texture_width, texture->getHeight());

		for (int i = rect.m_from_x, end = rect.m_to_x; i < end; ++i)
		{
			for (int j = rect.m_from_y, end2 = rect.m_to_y; j < end2; ++j)
			{
				int offset = i - m_x + (j - m_y) * m_width;
				((Lumix::uint16*)&data[0])[offset] = m_flat_height;
			}
		}
	}


	void rasterItem(Lumix::Texture* texture, Lumix::Array<Lumix::uint8>& data, Item& item)
	{
		if (m_type == TerrainEditor::COLOR)
		{
			rasterColorItem(texture, data, item);
			return;
		}
		else if (m_type == TerrainEditor::LAYER)
		{
			rasterLayerItem(texture, data, item);
			return;
		}
		else if (m_type == TerrainEditor::SMOOTH_HEIGHT)
		{
			rasterSmoothHeightItem(texture, data, item);
			return;
		}
		else if (m_type == TerrainEditor::FLAT_HEIGHT)
		{
			rasterFlatHeightItem(texture, data, item);
			return;		
		}

		ASSERT(texture->getBytesPerPixel() == 2);

		int texture_width = texture->getWidth();
		Rectangle rect;
		rect = item.getBoundingRectangle(texture_width, texture->getHeight());

		const float STRENGTH_MULTIPLICATOR = 256.0f;
		float amount =
			Lumix::Math::maxValue(item.m_amount * item.m_amount * STRENGTH_MULTIPLICATOR, 1.0f);

		for (int i = rect.m_from_x, end = rect.m_to_x; i < end; ++i)
		{
			for (int j = rect.m_from_y, end2 = rect.m_to_y; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j);
				int offset = i - m_x + (j - m_y) * m_width;

				int add = int(attenuation * amount);
				Lumix::uint16 x = ((Lumix::uint16*)texture->getData())[(i + j * texture_width)];
				x += m_type == TerrainEditor::RAISE_HEIGHT ? Lumix::Math::minValue(add, 0xFFFF - x)
														   : Lumix::Math::maxValue(-add, -x);
				((Lumix::uint16*)&data[0])[offset] = x;
			}
		}
	}


	void generateNewData()
	{
		auto texture = getDestinationTexture();
		int bpp = texture->getBytesPerPixel();
		Rectangle rect;
		getBoundingRectangle(texture, rect);
		m_new_data.resize(bpp * Lumix::Math::maxValue(1,
									 (rect.m_to_x - rect.m_from_x) *
										 (rect.m_to_y - rect.m_from_y)));
		Lumix::copyMemory(&m_new_data[0], &m_old_data[0], m_new_data.size());

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


	void applyData(Lumix::Array<Lumix::uint8>& data)
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
		texture->onDataUpdated(m_x, m_y, m_width, m_height);
		static_cast<Lumix::RenderScene*>(m_terrain.scene)->forceGrassUpdate(m_terrain.index);
	}


	void resizeData()
	{
		Lumix::Array<Lumix::uint8> new_data(m_world_editor.getAllocator());
		Lumix::Array<Lumix::uint8> old_data(m_world_editor.getAllocator());
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
			Lumix::copyMemory(&new_data[(row - rect.m_from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->getWidth() + rect.m_from_x * bpp],
				bpp * new_w);
			Lumix::copyMemory(&old_data[(row - rect.m_from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->getWidth() + rect.m_from_x * bpp],
				bpp * new_w);
		}

		// new
		for (int row = 0; row < m_height; ++row)
		{
			Lumix::copyMemory(&new_data[((row + m_y - rect.m_from_y) * new_w + m_x - rect.m_from_x) * bpp],
				&m_new_data[row * bpp * m_width],
				bpp * m_width);
			Lumix::copyMemory(&old_data[((row + m_y - rect.m_from_y) * new_w + m_x - rect.m_from_x) * bpp],
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
		rect.m_from_x = Lumix::Math::maxValue(int(item.m_local_pos.x - item.m_radius - 0.5f), 0);
		rect.m_from_y = Lumix::Math::maxValue(int(item.m_local_pos.z - item.m_radius - 0.5f), 0);
		rect.m_to_x = Lumix::Math::minValue(int(item.m_local_pos.x + item.m_radius + 0.5f),
						   texture->getWidth());
		rect.m_to_y = Lumix::Math::minValue(int(item.m_local_pos.z + item.m_radius + 0.5f),
						   texture->getHeight());
		for (int i = 1; i < m_items.size(); ++i)
		{
			Item& item = m_items[i];
			rect.m_from_x = Lumix::Math::minValue(int(item.m_local_pos.x - item.m_radius - 0.5f),
								 rect.m_from_x);
			rect.m_to_x = Lumix::Math::maxValue(int(item.m_local_pos.x + item.m_radius + 0.5f),
							   rect.m_to_x);
			rect.m_from_y = Lumix::Math::minValue(int(item.m_local_pos.z - item.m_radius - 0.5f),
								 rect.m_from_y);
			rect.m_to_y = Lumix::Math::maxValue(int(item.m_local_pos.z + item.m_radius + 0.5f),
							   rect.m_to_y);
		}
		rect.m_from_x = Lumix::Math::maxValue(rect.m_from_x, 0);
		rect.m_to_x = Lumix::Math::minValue(rect.m_to_x, texture->getWidth());
		rect.m_from_y = Lumix::Math::maxValue(rect.m_from_y, 0);
		rect.m_to_y = Lumix::Math::minValue(rect.m_to_y, texture->getHeight());
	}


private:
	Lumix::Array<Lumix::uint8> m_new_data;
	Lumix::Array<Lumix::uint8> m_old_data;
	int m_texture_idx;
	int m_width;
	int m_height;
	int m_x;
	int m_y;
	TerrainEditor::Type m_type;
	Lumix::Array<Item> m_items;
	Lumix::ComponentUID m_terrain;
	Lumix::WorldEditor& m_world_editor;
	Lumix::BinaryArray m_mask;
	Lumix::uint16 m_flat_height;
	bool m_can_be_merged;
};


TerrainEditor::~TerrainEditor()
{
	m_world_editor.universeDestroyed().unbind<TerrainEditor, &TerrainEditor::onUniverseDestroyed>(this);
	if (m_brush_texture)
	{
		m_brush_texture->destroy();
		LUMIX_DELETE(m_world_editor.getAllocator(), m_brush_texture);
	}

	m_world_editor.removePlugin(*this);
}


void TerrainEditor::onUniverseDestroyed()
{
	m_component.scene = nullptr;
	m_component.index = Lumix::INVALID_COMPONENT;
}


static Lumix::IEditorCommand* createPaintTerrainCommand(Lumix::WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PaintTerrainCommand)(editor);
}


TerrainEditor::TerrainEditor(Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions)
	: m_world_editor(editor)
	, m_color(1, 1, 1)
	, m_current_brush(0)
	, m_selected_entity_template(0)
	, m_brush_mask(editor.getAllocator())
	, m_brush_texture(nullptr)
	, m_flat_height(0)
	, m_is_enabled(false)
{
	editor.registerEditorCommandCreator("paint_terrain", createPaintTerrainCommand);

	m_increase_brush_size =
		LUMIX_NEW(editor.getAllocator(), Action)("Increase brush size", "increaseBrushSize");
	m_increase_brush_size->is_global = false;
	m_increase_brush_size->func.bind<TerrainEditor, &TerrainEditor::increaseBrushSize>(this);
	m_decrease_brush_size =
		LUMIX_NEW(editor.getAllocator(), Action)("Decrease brush size", "decreaseBrushSize");
	m_decrease_brush_size->func.bind<TerrainEditor, &TerrainEditor::decreaseBrushSize>(this);
	m_decrease_brush_size->is_global = false;
	actions.push(m_increase_brush_size);
	actions.push(m_decrease_brush_size);

	m_increase_texture_idx =
		LUMIX_NEW(editor.getAllocator(), Action)("Next terrain texture", "nextTerrainTexture");
	m_increase_texture_idx->is_global = false;
	m_increase_texture_idx->func.bind<TerrainEditor, &TerrainEditor::nextTerrainTexture>(this);
	m_decrease_texture_idx =
		LUMIX_NEW(editor.getAllocator(), Action)("Previous terrain texture", "prevTerrainTexture");
	m_decrease_texture_idx->func.bind<TerrainEditor, &TerrainEditor::prevTerrainTexture>(this);
	m_decrease_texture_idx->is_global = false;
	actions.push(m_increase_texture_idx);
	actions.push(m_decrease_texture_idx);

	m_smooth_terrain_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Smooth terrain", "smoothTerrain");
	m_smooth_terrain_action->is_global = false;
	m_lower_terrain_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Lower terrain", "lowerTerrain");
	m_lower_terrain_action->is_global = false;
	actions.push(m_smooth_terrain_action);
	actions.push(m_lower_terrain_action);

	m_remove_entity_action = LUMIX_NEW(editor.getAllocator(), Action)(
		"Remove entities from terrain", "removeEntitiesFromTerrain");
	m_remove_entity_action->is_global = false;
	actions.push(m_remove_entity_action);

	editor.addPlugin(*this);
	m_terrain_brush_size = 10;
	m_terrain_brush_strength = 0.1f;
	m_type = RAISE_HEIGHT;
	m_texture_idx = 0;
	m_is_align_with_normal = false;
	m_is_rotate_x = false;
	m_is_rotate_z = false;

	editor.universeDestroyed().bind<TerrainEditor, &TerrainEditor::onUniverseDestroyed>(this);
}


void TerrainEditor::nextTerrainTexture()
{
	auto* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
	auto* material = scene->getTerrainMaterial(m_component.index);
	Lumix::Texture* tex = material->getTextureByUniform(TEX_COLOR_UNIFORM);
	if (tex)
	{
		m_texture_idx =
			Lumix::Math::minValue(tex->getAtlasSize() * tex->getAtlasSize() - 1, m_texture_idx + 1);
	}
}


void TerrainEditor::prevTerrainTexture()
{
	m_texture_idx = Lumix::Math::maxValue(0, m_texture_idx - 1);
}


void TerrainEditor::increaseBrushSize()
{
	if (m_terrain_brush_size < 10)
	{
		++m_terrain_brush_size;
		return;
	}
	m_terrain_brush_size = Lumix::Math::minValue(100.0f, m_terrain_brush_size + 10);
}


void TerrainEditor::decreaseBrushSize()
{
	if (m_terrain_brush_size < 10)
	{
		m_terrain_brush_size = Lumix::Math::maxValue(MIN_BRUSH_SIZE, m_terrain_brush_size - 1.0f);
		return;
	}
	m_terrain_brush_size = Lumix::Math::maxValue(MIN_BRUSH_SIZE, m_terrain_brush_size - 10.0f);
}


void TerrainEditor::drawCursor(Lumix::RenderScene& scene,
	const Lumix::ComponentUID& terrain,
	const Lumix::Vec3& center)
{
	PROFILE_FUNCTION();
	static const int SLICE_COUNT = 30;
	if (m_type == TerrainEditor::FLAT_HEIGHT && ImGui::GetIO().KeyCtrl)
	{
		scene.addDebugCross(center, 1.0f, 0xff0000ff, 0);
		return;
	}

	float w, h;
	scene.getTerrainSize(terrain.index, &w, &h);
	float brush_size = m_terrain_brush_size;
	Lumix::Vec3 local_center = getRelativePosition(center);
	Lumix::Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);

	for (int i = 0; i < SLICE_COUNT + 1; ++i)
	{
		float angle_step = Lumix::Math::PI * 2 / SLICE_COUNT;
		float angle = i * angle_step;
		float next_angle = i * angle_step + angle_step;
		Lumix::Vec3 local_from = local_center + Lumix::Vec3(cos(angle), 0, sin(angle)) * brush_size;
		local_from.y = scene.getTerrainHeightAt(terrain.index, local_from.x, local_from.z);
		local_from.y += 0.25f;
		Lumix::Vec3 local_to =
			local_center + Lumix::Vec3(cos(next_angle), 0, sin(next_angle)) * brush_size;
		local_to.y = scene.getTerrainHeightAt(terrain.index, local_to.x, local_to.z);
		local_to.y += 0.25f;

		Lumix::Vec3 from = terrain_matrix.multiplyPosition(local_from);
		Lumix::Vec3 to = terrain_matrix.multiplyPosition(local_to);
		scene.addDebugLine(from, to, 0xffff0000, 0);
	}
}


void TerrainEditor::tick()
{
	PROFILE_FUNCTION();
	if (!m_component.isValid()) return;
	if (m_type == NOT_SET) return;
	if (!m_is_enabled) return;

	float mouse_x = m_world_editor.getMouseX();
	float mouse_y = m_world_editor.getMouseY();

	for (auto entity : m_world_editor.getSelectedEntities())
	{
		Lumix::ComponentUID terrain = m_world_editor.getComponent(entity, TERRAIN_HASH);
		if (!terrain.isValid()) continue;

		Lumix::ComponentUID camera_cmp = m_world_editor.getEditCamera();
		Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(camera_cmp.scene);
		Lumix::Vec3 origin, dir;
		scene->getRay(camera_cmp.index, (float)mouse_x, (float)mouse_y, origin, dir);
		Lumix::RayCastModelHit hit = scene->castRay(origin, dir, Lumix::INVALID_COMPONENT);
		
		if (hit.m_is_hit)
		{
			Lumix::Vec3 center = hit.m_origin + hit.m_dir * hit.m_t;
			drawCursor(*scene, terrain, center);
			return;
		}
	}
}


void TerrainEditor::detectModifiers()
{
	bool is_height_tool = m_type == LOWER_HEIGHT || m_type == RAISE_HEIGHT ||
						  m_type == SMOOTH_HEIGHT;
	if (is_height_tool)
	{
		if (m_lower_terrain_action->isActive())
		{
			m_type = LOWER_HEIGHT;
		}
		else if (m_smooth_terrain_action->isActive())
		{
			m_type = SMOOTH_HEIGHT;
		}
		else
		{
			m_type = RAISE_HEIGHT;
		}
	}

	bool is_entity_tool = m_type == ENTITY || m_type == REMOVE_ENTITY;
	if (is_entity_tool)
	{
		if (m_remove_entity_action->isActive())
		{
			m_type = REMOVE_ENTITY;
		}
		else
		{
			m_type = ENTITY;
		}
	}
}


Lumix::Vec3 TerrainEditor::getRelativePosition(const Lumix::Vec3& world_pos) const
{
	Lumix::Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);
	Lumix::Matrix inv_terrain_matrix = terrain_matrix;
	inv_terrain_matrix.inverse();

	return inv_terrain_matrix.multiplyPosition(world_pos);
}


Lumix::Texture* TerrainEditor::getHeightmap()
{
	return getMaterial()->getTextureByUniform(HEIGHTMAP_UNIFORM);
}


Lumix::uint16 TerrainEditor::getHeight(const Lumix::Vec3& world_pos)
{
	auto rel_pos = getRelativePosition(world_pos);
	auto* heightmap = getHeightmap();
	if (!heightmap) return 0;

	auto* data = (Lumix::uint16*)heightmap->getData();
	return data[int(rel_pos.x) + int(rel_pos.z) * heightmap->getWidth()];
}


bool TerrainEditor::onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int)
{
	if (!m_is_enabled) return false;
	if (m_type == NOT_SET || !m_component.isValid()) return false;

	detectModifiers();

	for (int i = m_world_editor.getSelectedEntities().size() - 1; i >= 0; --i)
	{
		if (m_world_editor.getSelectedEntities()[i] == hit.m_entity && m_component.isValid())
		{
			Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
			switch (m_type)
			{
				case FLAT_HEIGHT:
					if (ImGui::GetIO().KeyCtrl)
					{
						m_flat_height = getHeight(hit_pos);
					}
					else
					{
						paint(hit, m_type, false);
					}
					break;
				case RAISE_HEIGHT:
				case LOWER_HEIGHT:
				case SMOOTH_HEIGHT:
				case COLOR:
				case LAYER: paint(hit, m_type, false); break;
				case ENTITY: paintEntities(hit); break;
				case REMOVE_ENTITY: removeEntities(hit); break;
				default: ASSERT(false); break;
			}
			return true;
		}
	}
	return false;
}


void TerrainEditor::removeEntities(const Lumix::RayCastModelHit& hit)
{
	if (m_selected_entity_template < 0) return;
	auto& template_system = m_world_editor.getEntityTemplateSystem();
	auto& template_names = template_system.getTemplateNames();
	int templates_count = template_names.size();
	if (m_selected_entity_template >= templates_count) return;

	PROFILE_FUNCTION();

	m_world_editor.beginCommandGroup();

	Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
	Lumix::Frustum frustum;
	frustum.computeOrtho(hit.m_origin + hit.m_dir * hit.m_t,
		Lumix::Vec3(0, 0, 1),
		Lumix::Vec3(0, 1, 0),
		2 * m_terrain_brush_size,
		2 * m_terrain_brush_size,
		-m_terrain_brush_size,
		m_terrain_brush_size);

	Lumix::Array<Lumix::Entity> entities(m_world_editor.getAllocator());
	scene->getRenderableEntities(frustum, entities, ~0);
	auto hash = Lumix::crc32(template_names[m_selected_entity_template].c_str());
	for(Lumix::Entity entity : entities)
	{
		if(template_system.getTemplate(entity) != hash) continue;

		m_world_editor.destroyEntities(&entity, 1);
	}

	m_world_editor.endCommandGroup();
}


static bool overlaps(float min1, float max1, float min2, float max2)
{
	return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
}


static void getProjections(const Lumix::Vec3& axis,
	const Lumix::Vec3 vertices[8],
	float& min,
	float& max)
{
	min = max = Lumix::dotProduct(vertices[0], axis);
	for(int i = 1; i < 8; ++i)
	{
		float dot = Lumix::dotProduct(vertices[i], axis);
		min = Lumix::Math::minValue(dot, min);
		max = Lumix::Math::maxValue(dot, max);
	}
}


static bool testOBBCollision(const Lumix::Matrix& matrix_a,
	const Lumix::Model* model_a,
	const Lumix::Matrix& matrix_b,
	const Lumix::Model* model_b,
	float scale)
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

	Lumix::Vec3 normals[] = {
		matrix_a.getXVector(), matrix_a.getYVector(), matrix_a.getZVector()};
	for(int i = 0; i < 3; i++)
	{
		float box_a_min, box_a_max, box_b_min, box_b_max;
		getProjections(normals[i], box_a_points, box_a_min, box_a_max);
		getProjections(normals[i], box_b_points, box_b_min, box_b_max);
		if(!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
		{
			return false;
		}
	}

	Lumix::Vec3 normals_b[] = {
		matrix_b.getXVector(), matrix_b.getYVector(), matrix_b.getZVector()};
	for(int i = 0; i < 3; i++)
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


static bool isOBBCollision(Lumix::RenderScene& scene,
	const Lumix::Array<Lumix::RenderableMesh>& meshes,
	const Lumix::Vec3& pos_a,
	Lumix::Model* model,
	float scale)
{
	float radius_a_squared = model->getBoundingRadius();
	radius_a_squared = radius_a_squared * radius_a_squared;
	for(auto& mesh : meshes)
	{
		auto* renderable = scene.getRenderable(mesh.renderable);
		Lumix::Vec3 pos_b = renderable->matrix.getTranslation();
		float radius_b = renderable->model->getBoundingRadius();
		float radius_squared = radius_a_squared + radius_b * radius_b;
		if((pos_a - pos_b).squaredLength() < radius_squared * scale * scale)
		{
			Lumix::Matrix matrix = Lumix::Matrix::IDENTITY;
			matrix.setTranslation(pos_a);
			if(testOBBCollision(matrix, model, renderable->matrix, renderable->model, scale))
			{
				return true;
			}
		}
	}
	return false;
}


void TerrainEditor::paintEntities(const Lumix::RayCastModelHit& hit)
{
	PROFILE_FUNCTION();
	if (m_selected_entity_template < 0) return;
	auto& template_system = m_world_editor.getEntityTemplateSystem();
	auto& template_names = template_system.getTemplateNames();
	int templates_count = template_names.size();
	if (m_selected_entity_template >= templates_count) return;

	m_world_editor.beginCommandGroup();
	{
		Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
		Lumix::Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);
		Lumix::Matrix inv_terrain_matrix = terrain_matrix;
		inv_terrain_matrix.inverse();

		auto hash = Lumix::crc32(template_names[m_selected_entity_template].c_str());
		Lumix::Entity tpl = template_system.getInstances(hash)[0];
		if(tpl < 0) return;

		Lumix::ComponentUID renderable = m_world_editor.getComponent(tpl, RENDERABLE_HASH);
		if(!renderable.isValid()) return;

		Lumix::Frustum frustum;
		auto center = hit.m_origin + hit.m_dir * hit.m_t;
		frustum.computeOrtho(center,
			Lumix::Vec3(0, 0, 1),
			Lumix::Vec3(0, 1, 0),
			2 * m_terrain_brush_size,
			2 * m_terrain_brush_size,
			-m_terrain_brush_size,
			m_terrain_brush_size);

		Lumix::Array<Lumix::RenderableMesh> meshes(m_world_editor.getAllocator());
		meshes.clear();
		scene->getRenderableInfos(frustum, meshes, ~0);

		const auto* template_name = template_names[m_selected_entity_template].c_str();

		float w, h;
		scene->getTerrainSize(m_component.index, &w, &h);
		float scale = 1.0f - Lumix::Math::maxValue(0.01f, m_terrain_brush_strength);
		Lumix::Model* model = scene->getRenderableModel(renderable.index);
		for(int i = 0; i <= m_terrain_brush_size * m_terrain_brush_size / 1000.0f; ++i)
		{
			float angle = Lumix::Math::randFloat(0, Lumix::Math::PI * 2);
			float dist = Lumix::Math::randFloat(0, 1.0f) * m_terrain_brush_size;
			Lumix::Vec3 pos(center.x + cos(angle) * dist, 0, center.z + sin(angle) * dist);
			Lumix::Vec3 terrain_pos = inv_terrain_matrix.multiplyPosition(pos);
			if(terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= w &&
				terrain_pos.z <= h)
			{
				pos.y = scene->getTerrainHeightAt(m_component.index, terrain_pos.x, terrain_pos.z);
				pos.y += terrain_matrix.getTranslation().y;
				if(!isOBBCollision(*scene, meshes, pos, model, scale))
				{
					Lumix::Quat rot(0, 0, 0, 1);
					if(m_is_align_with_normal)
					{
						Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
						Lumix::Vec3 normal = scene->getTerrainNormalAt(m_component.index, pos.x, pos.z);
						Lumix::Vec3 dir = Lumix::crossProduct(normal, Lumix::Vec3(1, 0, 0)).normalized();
						Lumix::Matrix mtx = Lumix::Matrix::IDENTITY;
						mtx.setXVector(Lumix::crossProduct(normal, dir));
						mtx.setYVector(normal);
						mtx.setXVector(dir);
						mtx.getRotation(rot);
					}
					else
					{
						if (m_is_rotate_x)
						{
							float angle = Lumix::Math::randFloat(0, Lumix::Math::PI * 2);
							Lumix::Quat q(Lumix::Vec3(1, 0, 0), angle);
							rot = rot * q;
						}

						if (m_is_rotate_z)
						{
							float angle = Lumix::Math::randFloat(0, Lumix::Math::PI * 2);
							Lumix::Quat q(rot * Lumix::Vec3(0, 0, 1), angle);
							rot = rot * q;
						}
					}

					auto entity = template_system.createInstance(template_name, pos, rot);
				}
			}
		}
	}
	m_world_editor.endCommandGroup();
}


void TerrainEditor::onMouseMove(int x, int y, int, int)
{
	if (!m_is_enabled) return;

	detectModifiers();

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
				case FLAT_HEIGHT:
				case RAISE_HEIGHT:
				case LOWER_HEIGHT:
				case SMOOTH_HEIGHT:
				case COLOR:
				case LAYER:
					paint(hit, m_type, true);
					break;
				case ENTITY: paintEntities(hit); break;
				case REMOVE_ENTITY: removeEntities(hit); break;
				default: ASSERT(false); break;
			}
		}
	}
}


void TerrainEditor::onMouseUp(int, int, Lumix::MouseButton::Value)
{
}


Lumix::Material* TerrainEditor::getMaterial()
{
	auto* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
	return scene->getTerrainMaterial(m_component.index);
}


void TerrainEditor::onGUI()
{
	if (m_decrease_brush_size->isRequested()) m_decrease_brush_size->func.invoke();
	if (m_increase_brush_size->isRequested()) m_increase_brush_size->func.invoke();
	if (m_increase_texture_idx->isRequested()) m_increase_texture_idx->func.invoke();
	if (m_decrease_texture_idx->isRequested()) m_decrease_texture_idx->func.invoke();

	auto* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
	if (!ImGui::CollapsingHeader("Terrain editor", nullptr, true, true)) return;

	ImGui::Checkbox("Editor enabled", &m_is_enabled);
	if (!m_is_enabled) return;

	if (!getMaterial())
	{
		ImGui::Text("No heightmap");
		return;
	}
	ImGui::SliderFloat("Brush size", &m_terrain_brush_size, MIN_BRUSH_SIZE, 100);
	ImGui::SliderFloat("Brush strength", &m_terrain_brush_strength, 0, 1.0f);

	enum BrushType
	{
		HEIGHT,
		LAYER,
		ENTITY,
		COLOR
	};

	bool is_grass_enabled = scene->isGrassEnabled();

	if (ImGui::Checkbox("Enable grass", &is_grass_enabled)) scene->enableGrass(is_grass_enabled);

	if (ImGui::Combo(
			"Brush type", &m_current_brush, "Height\0Layer\0Entity\0Color\0"))
	{
		m_type = m_current_brush == HEIGHT ? TerrainEditor::RAISE_HEIGHT : m_type;
	}

	switch (m_current_brush)
	{
		case HEIGHT:
			if (ImGui::Button("Save heightmap"))
				getMaterial()->getTextureByUniform(HEIGHTMAP_UNIFORM)->save();
			break;
		case LAYER:
			if (ImGui::Button("Save layermap"))
				getMaterial()->getTextureByUniform(SPLATMAP_UNIFORM)->save();
			break;
		case COLOR:
			if (ImGui::Button("Save colormap"))
				getMaterial()->getTextureByUniform(COLORMAP_UNIFORM)->save();
			break;
	}

	if (m_current_brush == LAYER || m_current_brush == COLOR)
	{
		if (m_brush_texture)
		{
			static auto th = m_brush_texture->getTextureHandle();
			ImGui::Image(&th, ImVec2(100, 100));
			if (ImGui::Button("Clear mask"))
			{
				m_brush_texture->destroy();
				LUMIX_DELETE(m_world_editor.getAllocator(), m_brush_texture);
				m_brush_mask.clear();
				m_brush_texture = nullptr;
			}
			ImGui::SameLine();
		}

		ImGui::SameLine();
		if (ImGui::Button("Select mask"))
		{
			char filename[Lumix::MAX_PATH_LENGTH];
			if (PlatformInterface::getOpenFilename(filename, Lumix::lengthOf(filename), "All\0*.*\0"))
			{
				int image_width;
				int image_height;
				int image_comp;
				auto* data = stbi_load(filename, &image_width, &image_height, &image_comp, 4);
				if (data)
				{
					m_brush_mask.resize(image_width * image_height);
					for (int j = 0; j < image_width; ++j)
					{
						for (int i = 0; i < image_width; ++i)
						{
							m_brush_mask[i + j * image_width] =
								data[image_comp * (i + j * image_width)] > 128;
						}
					}

					auto& rm = m_world_editor.getEngine().getResourceManager();
					if (m_brush_texture)
					{
						m_brush_texture->destroy();
						LUMIX_DELETE(m_world_editor.getAllocator(), m_brush_texture);
					}
					m_brush_texture = LUMIX_NEW(m_world_editor.getAllocator(), Lumix::Texture)(
						Lumix::Path("brush_texture"), rm, m_world_editor.getAllocator());
					m_brush_texture->create(image_width, image_height, data);

					stbi_image_free(data);
				}
			}
		}
	}


	switch (m_current_brush)
	{
		case HEIGHT:
		{
			bool is_flat_tool = m_type == TerrainEditor::FLAT_HEIGHT;
			if (ImGui::Checkbox("Flat", &is_flat_tool))
			{
				m_type = is_flat_tool ? TerrainEditor::FLAT_HEIGHT : TerrainEditor::RAISE_HEIGHT;
			}

			if (m_type == TerrainEditor::FLAT_HEIGHT)
			{
				ImGui::SameLine();
				ImGui::Text("- Press Ctrl to pick height");
			}
			break;
		}
		case COLOR:
		{
			m_type = TerrainEditor::COLOR;
			ImGui::ColorPicker(&m_color.x, false);
			break;
		}
		case LAYER:
		{
			m_type = TerrainEditor::LAYER;
			Lumix::Texture* tex = getMaterial()->getTextureByUniform(TEX_COLOR_UNIFORM);
			if (tex)
			{
				for (int i = 0; i < tex->getAtlasSize() * tex->getAtlasSize(); ++i)
				{
					if (i % 4 != 0) ImGui::SameLine();
					if (ImGui::RadioButton(StringBuilder<20>("", i, "###rb", i), m_texture_idx == i))
					{
						m_texture_idx = i;
					}
				}
			}
			break;
		}
		case ENTITY:
		{
			m_type = TerrainEditor::ENTITY;
			auto& template_system = m_world_editor.getEntityTemplateSystem();
			auto& template_names = template_system.getTemplateNames();
			if (template_names.empty())
			{
				ImGui::Text("No templates, please create one.");
			}
			else
			{
				ImGui::Combo("Entity",
					&m_selected_entity_template,
					[](void* data, int idx, const char** out_text) -> bool
				{
					auto& template_names = *static_cast<Lumix::Array<Lumix::string>*>(data);
					if (idx >= template_names.size()) return false;
					*out_text = template_names[idx].c_str();
					return true;
				},
					&template_names,
					template_names.size());
			}
			if (ImGui::Checkbox("Align with normal", &m_is_align_with_normal))
			{
				if (m_is_align_with_normal) m_is_rotate_x = m_is_rotate_z = false;
			}
			if (ImGui::Checkbox("Rotate around X", &m_is_rotate_x))
			{
				if (m_is_rotate_x) m_is_align_with_normal = false;
			}
			ImGui::SameLine();
			if (ImGui::Checkbox("Rotate around Z", &m_is_rotate_z))
			{
				if (m_is_rotate_z) m_is_align_with_normal = false;
			}
		}
		break;
		default: ASSERT(false); break;
	}	
}


void TerrainEditor::paint(const Lumix::RayCastModelHit& hit,
						  Type type,
						  bool old_stroke)
{
	Lumix::Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;

	PaintTerrainCommand* command =
		LUMIX_NEW(m_world_editor.getAllocator(), PaintTerrainCommand)(
			m_world_editor,
			type,
			m_texture_idx,
			hit_pos,
			m_brush_mask,
			m_terrain_brush_size,
			m_terrain_brush_strength,
			m_flat_height,
			m_color,
			m_component,
			old_stroke);
	m_world_editor.executeCommand(command);
}
