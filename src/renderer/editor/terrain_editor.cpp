#include "terrain_editor.h"
#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
#include "editor/platform_interface.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/properties.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "physics/physics_scene.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"
#include <cmath>


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Properties::getComponentType("renderable");
static const ComponentType TERRAIN_TYPE = Properties::getComponentType("terrain");
static const ComponentType HEIGHTFIELD_TYPE = Properties::getComponentType("physical_heightfield");
static const ResourceType MATERIAL_TYPE("material");
static const ResourceType TEXTURE_TYPE("texture");
static const ResourceType PREFAB_TYPE("prefab");
static const char* HEIGHTMAP_UNIFORM = "u_texHeightmap";
static const char* SPLATMAP_UNIFORM = "u_texSplatmap";
static const char* COLORMAP_UNIFORM = "u_texColormap";
static const char* TEX_COLOR_UNIFORM = "u_texColor";
static const float MIN_BRUSH_SIZE = 0.5f;


struct PaintTerrainCommand LUMIX_FINAL : public IEditorCommand
{
	struct Rectangle
	{
		int from_x;
		int from_y;
		int to_x;
		int to_y;
	};


	explicit PaintTerrainCommand(WorldEditor& editor)
		: m_world_editor(editor)
		, m_new_data(editor.getAllocator())
		, m_old_data(editor.getAllocator())
		, m_items(editor.getAllocator())
		, m_mask(editor.getAllocator())
	{
	}


	PaintTerrainCommand(WorldEditor& editor,
		TerrainEditor::ActionType action_type,
		int texture_idx,
		const Vec3& hit_pos,
		BinaryArray& mask,
		float radius,
		float rel_amount,
		u16 flat_height,
		Vec3 color,
		ComponentUID terrain,
		bool can_be_merged)
		: m_world_editor(editor)
		, m_terrain(terrain)
		, m_can_be_merged(can_be_merged)
		, m_new_data(editor.getAllocator())
		, m_old_data(editor.getAllocator())
		, m_items(editor.getAllocator())
		, m_action_type(action_type)
		, m_texture_idx(texture_idx)
		, m_grass_mask((u16)texture_idx)
		, m_mask(editor.getAllocator())
		, m_flat_height(flat_height)
	{
		m_mask.resize(mask.size());
		for (int i = 0; i < mask.size(); ++i)
		{
			m_mask[i] = mask[i];
		}

		m_width = m_height = m_x = m_y = -1;
		Matrix entity_mtx = editor.getUniverse()->getMatrix(terrain.entity);
		entity_mtx.fastInverse();
		Vec3 local_pos = entity_mtx.transformPoint(hit_pos);
		float terrain_size = static_cast<RenderScene*>(terrain.scene)->getTerrainSize(terrain.handle).x;
		local_pos = local_pos / terrain_size;
		local_pos.y = -1;

		Item& item = m_items.emplace();
		item.m_local_pos = local_pos;
		item.m_radius = radius / terrain_size;
		item.m_amount = rel_amount;
		item.m_color = color;
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("type", (int)m_action_type);
		serializer.serialize("texture_idx", m_texture_idx);
		serializer.serialize("grass_mask", m_grass_mask);
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


	void deserialize(JsonSerializer& serializer) override
	{
		m_items.clear();
		int action_type;
		serializer.deserialize("type", action_type, 0);
		m_action_type = (TerrainEditor::ActionType)action_type;
		serializer.deserialize("texture_idx", m_texture_idx, 0);
		serializer.deserialize("grass_mask", m_grass_mask, 0);
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


	const char* getType() override
	{
		return "paint_terrain";
	}


	bool merge(IEditorCommand& command) override
	{
		if (!m_can_be_merged)
		{
			return false;
		}
		PaintTerrainCommand& my_command = static_cast<PaintTerrainCommand&>(command);
		if (m_terrain == my_command.m_terrain && m_action_type == my_command.m_action_type &&
			m_texture_idx == my_command.m_texture_idx)
		{
			my_command.m_items.push(m_items.back());
			my_command.resizeData();
			my_command.rasterItem(getDestinationTexture(), my_command.m_new_data, m_items.back());
			return true;
		}
		return false;
	}

private:
	struct Item
	{
		Rectangle getBoundingRectangle(int texture_size) const
		{
			Rectangle r;
			r.from_x = Math::maximum(0, int(texture_size * (m_local_pos.x - m_radius) - 0.5f));
			r.from_y = Math::maximum(0, int(texture_size * (m_local_pos.z - m_radius) - 0.5f));
			r.to_x = Math::minimum(texture_size, int(texture_size * (m_local_pos.x + m_radius) + 0.5f));
			r.to_y = Math::minimum(texture_size, int(texture_size * (m_local_pos.z + m_radius) + 0.5f));
			return r;
		}

		float m_radius;
		float m_amount;
		Vec3 m_local_pos;
		Vec3 m_color;
	};

private:
	Material* getMaterial()
	{
		auto* scene = static_cast<RenderScene*>(m_terrain.scene);
		return scene->getTerrainMaterial(m_terrain.handle);
	}


	Texture* getDestinationTexture()
	{
		const char* uniform_name = "";
		switch (m_action_type)
		{
			case TerrainEditor::REMOVE_GRASS:
			case TerrainEditor::ADD_GRASS:
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


	u16 computeAverage16(const Texture* texture, int from_x, int to_x, int from_y, int to_y)
	{
		ASSERT(texture->bytes_per_pixel == 2);
		u32 sum = 0;
		int texture_width = texture->width;
		for (int i = from_x, end = to_x; i < end; ++i)
		{
			for (int j = from_y, end2 = to_y; j < end2; ++j)
			{
				sum += ((u16*)texture->getData())[(i + j * texture_width)];
			}
		}
		return u16(sum / (to_x - from_x) / (to_y - from_y));
	}


	float getAttenuation(Item& item, int i, int j, int texture_size) const
	{
		float dist =
			sqrt((texture_size * item.m_local_pos.x - 0.5f - i) * (texture_size * item.m_local_pos.x - 0.5f - i) +
				 (texture_size * item.m_local_pos.z - 0.5f - j) * (texture_size * item.m_local_pos.z - 0.5f - j));
		return 1.0f - Math::minimum(dist / (texture_size * item.m_radius), 1.0f);
	}


	void rasterColorItem(Texture* texture, Array<u8>& data, Item& item)
	{
		int texutre_size = texture->width;
		Rectangle r = item.getBoundingRectangle(texutre_size);

		if (texture->bytes_per_pixel != 4)
		{
			ASSERT(false);
			return;
		}
		float fx = 0;
		float fstepx = 1.0f / (r.to_x - r.from_x);
		float fstepy = 1.0f / (r.to_y - r.from_y);
		for (int i = r.from_x, end = r.to_x; i < end; ++i, fx += fstepx)
		{
			float fy = 0;
			for (int j = r.from_y, end2 = r.to_y; j < end2; ++j, fy += fstepy)
			{
				if (isMasked(fx, fy))
				{
					float attenuation = getAttenuation(item, i, j, texutre_size);
					int offset = 4 * (i - m_x + (j - m_y) * m_width);
					u8* d = &data[offset];
					d[0] += u8((item.m_color.x * 255 - d[0]) * attenuation);
					d[1] += u8((item.m_color.y * 255 - d[1]) * attenuation);
					d[2] += u8((item.m_color.z * 255 - d[2]) * attenuation);
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


	void rasterLayerItem(Texture* texture, Array<u8>& data, Item& item)
	{
		int texture_size = texture->width;
		Rectangle r = item.getBoundingRectangle(texture_size);

		if (texture->bytes_per_pixel != 4)
		{
			ASSERT(false);
			return;
		}

		float fx = 0;
		float fstepx = 1.0f / (r.to_x - r.from_x);
		float fstepy = 1.0f / (r.to_y - r.from_y);
		for (int i = r.from_x, end = r.to_x; i < end; ++i, fx += fstepx)
		{
			float fy = 0;
			for (int j = r.from_y, end2 = r.to_y; j < end2; ++j, fy += fstepy)
			{
				if (isMasked(fx, fy))
				{
					int offset = 4 * (i - m_x + (j - m_y) * m_width);
					float attenuation = getAttenuation(item, i, j, texture_size);
					int add = int(attenuation * item.m_amount * 255);
					if (add > 0)
					{
						if (data[offset] == m_texture_idx)
						{
							data[offset + 1] += Math::minimum(255 - data[offset + 1], add);
						}
						else
						{
							data[offset + 1] = add;
						}
						data[offset] = m_texture_idx;
					}
				}
			}
		}
	}

	void rasterGrassItem(Texture* texture, Array<u8>& data, Item& item, TerrainEditor::ActionType action_type)
	{
		int texture_size = texture->width;
		Rectangle r = item.getBoundingRectangle(texture_size);

		if (texture->bytes_per_pixel != 4)
		{
			ASSERT(false);
			return;
		}

		float fx = 0;
		float fstepx = 1.0f / (r.to_x - r.from_x);
		float fstepy = 1.0f / (r.to_y - r.from_y);
		for (int i = r.from_x, end = r.to_x; i < end; ++i, fx += fstepx)
		{
			float fy = 0;
			for (int j = r.from_y, end2 = r.to_y; j < end2; ++j, fy += fstepy)
			{
				if (isMasked(fx, fy))
				{
					int offset = 4 * (i - m_x + (j - m_y) * m_width) + 2;
					float attenuation = getAttenuation(item, i, j, texture_size);
					int add = int(attenuation * item.m_amount * 255);
					if (add > 0)
					{
						u16* tmp = ((u16*)&data[offset]);
						if (m_action_type == TerrainEditor::REMOVE_GRASS)
						{
							*tmp &= ~m_grass_mask;
						}
						else
						{
							*tmp |= m_grass_mask;
						}
					}
				}
			}
		}
	}


	void rasterSmoothHeightItem(Texture* texture, Array<u8>& data, Item& item)
	{
		ASSERT(texture->bytes_per_pixel == 2);

		int texture_size = texture->width;
		Rectangle rect;
		rect = item.getBoundingRectangle(texture_size);

		float avg = computeAverage16(texture, rect.from_x, rect.to_x, rect.from_y, rect.to_y);
		for (int i = rect.from_x, end = rect.to_x; i < end; ++i)
		{
			for (int j = rect.from_y, end2 = rect.to_y; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j, texture_size);
				int offset = i - m_x + (j - m_y) * m_width;
				u16 x = ((u16*)texture->getData())[(i + j * texture_size)];
				x += u16((avg - x) * item.m_amount * attenuation);
				((u16*)&data[0])[offset] = x;
			}
		}
	}


	void rasterFlatHeightItem(Texture* texture, Array<u8>& data, Item& item)
	{
		ASSERT(texture->bytes_per_pixel == 2);

		int texture_size = texture->width;
		Rectangle rect;
		rect = item.getBoundingRectangle(texture_size);

		for (int i = rect.from_x, end = rect.to_x; i < end; ++i)
		{
			for (int j = rect.from_y, end2 = rect.to_y; j < end2; ++j)
			{
				int offset = i - m_x + (j - m_y) * m_width;
				float dist = sqrt(
					(texture_size * item.m_local_pos.x - 0.5f - i) * (texture_size * item.m_local_pos.x - 0.5f - i) +
					(texture_size * item.m_local_pos.z - 0.5f - j) * (texture_size * item.m_local_pos.z - 0.5f - j));
				float t = (dist - texture_size * item.m_radius * item.m_amount) /
						  (texture_size * item.m_radius * (1 - item.m_amount));
				t = Math::clamp(1 - t, 0.0f, 1.0f);
				u16 old_value = ((u16*)&data[0])[offset];
				((u16*)&data[0])[offset] = (u16)(m_flat_height * t + old_value * (1-t));
			}
		}
	}


	void rasterItem(Texture* texture, Array<u8>& data, Item& item)
	{
		if (m_action_type == TerrainEditor::COLOR)
		{
			rasterColorItem(texture, data, item);
			return;
		}
		else if (m_action_type == TerrainEditor::LAYER)
		{
			rasterLayerItem(texture, data, item);
			return;
		}
		else if (m_action_type == TerrainEditor::ADD_GRASS || m_action_type == TerrainEditor::REMOVE_GRASS)
		{
			rasterGrassItem(texture, data, item, m_action_type);
			return;
		}
		else if (m_action_type == TerrainEditor::SMOOTH_HEIGHT)
		{
			rasterSmoothHeightItem(texture, data, item);
			return;
		}
		else if (m_action_type == TerrainEditor::FLAT_HEIGHT)
		{
			rasterFlatHeightItem(texture, data, item);
			return;
		}

		ASSERT(texture->bytes_per_pixel == 2);

		int texture_size = texture->width;
		Rectangle rect;
		rect = item.getBoundingRectangle(texture_size);

		const float STRENGTH_MULTIPLICATOR = 256.0f;
		float amount = Math::maximum(item.m_amount * item.m_amount * STRENGTH_MULTIPLICATOR, 1.0f);

		for (int i = rect.from_x, end = rect.to_x; i < end; ++i)
		{
			for (int j = rect.from_y, end2 = rect.to_y; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j, texture_size);
				int offset = i - m_x + (j - m_y) * m_width;

				int add = int(attenuation * amount);
				u16 x = ((u16*)texture->getData())[(i + j * texture_size)];
				x += m_action_type == TerrainEditor::RAISE_HEIGHT ? Math::minimum(add, 0xFFFF - x)
														   : Math::maximum(-add, -x);
				((u16*)&data[0])[offset] = x;
			}
		}
	}


	void generateNewData()
	{
		auto texture = getDestinationTexture();
		int bpp = texture->bytes_per_pixel;
		Rectangle rect;
		getBoundingRectangle(texture, rect);
		m_new_data.resize(bpp * Math::maximum(1, (rect.to_x - rect.from_x) * (rect.to_y - rect.from_y)));
		copyMemory(&m_new_data[0], &m_old_data[0], m_new_data.size());

		for (int item_index = 0; item_index < m_items.size(); ++item_index)
		{
			Item& item = m_items[item_index];
			rasterItem(texture, m_new_data, item);
		}
	}


	void saveOldData()
	{
		auto texture = getDestinationTexture();
		int bpp = texture->bytes_per_pixel;
		Rectangle rect;
		getBoundingRectangle(texture, rect);
		m_x = rect.from_x;
		m_y = rect.from_y;
		m_width = rect.to_x - rect.from_x;
		m_height = rect.to_y - rect.from_y;
		m_old_data.resize(bpp * (rect.to_x - rect.from_x) * (rect.to_y - rect.from_y));

		int index = 0;
		for (int j = rect.from_y, end2 = rect.to_y; j < end2; ++j)
		{
			for (int i = rect.from_x, end = rect.to_x; i < end; ++i)
			{
				for (int k = 0; k < bpp; ++k)
				{
					m_old_data[index] = texture->getData()[(i + j * texture->width) * bpp + k];
					++index;
				}
			}
		}
	}


	void applyData(Array<u8>& data)
	{
		auto texture = getDestinationTexture();
		int bpp = texture->bytes_per_pixel;

		for (int j = m_y; j < m_y + m_height; ++j)
		{
			for (int i = m_x; i < m_x + m_width; ++i)
			{
				int index = bpp * (i + j * texture->width);
				for (int k = 0; k < bpp; ++k)
				{
					texture->getData()[index + k] = data[bpp * (i - m_x + (j - m_y) * m_width) + k];
				}
			}
		}
		texture->onDataUpdated(m_x, m_y, m_width, m_height);
		static_cast<RenderScene*>(m_terrain.scene)->forceGrassUpdate(m_terrain.handle);

		if (m_action_type != TerrainEditor::LAYER && m_action_type != TerrainEditor::COLOR &&
			m_action_type != TerrainEditor::ADD_GRASS && m_action_type != TerrainEditor::REMOVE_GRASS)
		{
			IScene* scene = m_world_editor.getUniverse()->getScene(crc32("physics"));
			if (!scene) return;

			auto* phy_scene = static_cast<PhysicsScene*>(scene);
			ComponentHandle cmp = scene->getComponent(m_terrain.entity, HEIGHTFIELD_TYPE);
			if (!cmp.isValid()) return;

			phy_scene->updateHeighfieldData(cmp, m_x, m_y, m_width, m_height, &data[0], bpp);
		}
	}


	void resizeData()
	{
		Array<u8> new_data(m_world_editor.getAllocator());
		Array<u8> old_data(m_world_editor.getAllocator());
		auto texture = getDestinationTexture();
		Rectangle rect;
		getBoundingRectangle(texture, rect);

		int new_w = rect.to_x - rect.from_x;
		int bpp = texture->bytes_per_pixel;
		new_data.resize(bpp * new_w * (rect.to_y - rect.from_y));
		old_data.resize(bpp * new_w * (rect.to_y - rect.from_y));

		// original
		for (int row = rect.from_y; row < rect.to_y; ++row)
		{
			copyMemory(&new_data[(row - rect.from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->width + rect.from_x * bpp],
				bpp * new_w);
			copyMemory(&old_data[(row - rect.from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->width + rect.from_x * bpp],
				bpp * new_w);
		}

		// new
		for (int row = 0; row < m_height; ++row)
		{
			copyMemory(&new_data[((row + m_y - rect.from_y) * new_w + m_x - rect.from_x) * bpp],
				&m_new_data[row * bpp * m_width],
				bpp * m_width);
			copyMemory(&old_data[((row + m_y - rect.from_y) * new_w + m_x - rect.from_x) * bpp],
				&m_old_data[row * bpp * m_width],
				bpp * m_width);
		}

		m_x = rect.from_x;
		m_y = rect.from_y;
		m_height = rect.to_y - rect.from_y;
		m_width = rect.to_x - rect.from_x;

		m_new_data.swap(new_data);
		m_old_data.swap(old_data);
	}


	void getBoundingRectangle(Texture* texture, Rectangle& rect)
	{
		int s = texture->width;
		Item& item = m_items[0];
		rect.from_x = Math::maximum(int(s * (item.m_local_pos.x - item.m_radius) - 0.5f), 0);
		rect.from_y = Math::maximum(int(s * (item.m_local_pos.z - item.m_radius) - 0.5f), 0);
		rect.to_x = Math::minimum(int(s * (item.m_local_pos.x + item.m_radius) + 0.5f), texture->width);
		rect.to_y = Math::minimum(int(s * (item.m_local_pos.z + item.m_radius) + 0.5f), texture->height);
		for (int i = 1; i < m_items.size(); ++i)
		{
			Item& item = m_items[i];
			rect.from_x = Math::minimum(int(s * (item.m_local_pos.x - item.m_radius) - 0.5f), rect.from_x);
			rect.to_x = Math::maximum(int(s * (item.m_local_pos.x + item.m_radius) + 0.5f), rect.to_x);
			rect.from_y = Math::minimum(int(s * (item.m_local_pos.z - item.m_radius) - 0.5f), rect.from_y);
			rect.to_y = Math::maximum(int(s * (item.m_local_pos.z + item.m_radius) + 0.5f), rect.to_y);
		}
		rect.from_x = Math::maximum(rect.from_x, 0);
		rect.to_x = Math::minimum(rect.to_x, texture->width);
		rect.from_y = Math::maximum(rect.from_y, 0);
		rect.to_y = Math::minimum(rect.to_y, texture->height);
	}


private:
	Array<u8> m_new_data;
	Array<u8> m_old_data;
	int m_texture_idx;
	u16 m_grass_mask;
	int m_width;
	int m_height;
	int m_x;
	int m_y;
	TerrainEditor::ActionType m_action_type;
	Array<Item> m_items;
	ComponentUID m_terrain;
	WorldEditor& m_world_editor;
	BinaryArray m_mask;
	u16 m_flat_height;
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
	m_component.handle = INVALID_COMPONENT;
}


static IEditorCommand* createPaintTerrainCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PaintTerrainCommand)(editor);
}


TerrainEditor::TerrainEditor(WorldEditor& editor, StudioApp& app)
	: m_world_editor(editor)
	, m_app(app)
	, m_color(1, 1, 1)
	, m_current_brush(0)
	, m_selected_prefabs(editor.getAllocator())
	, m_brush_mask(editor.getAllocator())
	, m_brush_texture(nullptr)
	, m_flat_height(0)
	, m_is_enabled(false)
	, m_size_spread(1, 1)
	, m_y_spread(0, 0)
{
	editor.registerEditorCommandCreator("paint_terrain", createPaintTerrainCommand);
	m_increase_brush_size = LUMIX_NEW(editor.getAllocator(), Action)("Increase brush size", "increaseBrushSize");
	m_increase_brush_size->is_global = false;
	m_increase_brush_size->func.bind<TerrainEditor, &TerrainEditor::increaseBrushSize>(this);
	m_decrease_brush_size = LUMIX_NEW(editor.getAllocator(), Action)("Decrease brush size", "decreaseBrushSize");
	m_decrease_brush_size->func.bind<TerrainEditor, &TerrainEditor::decreaseBrushSize>(this);
	m_decrease_brush_size->is_global = false;
	app.addAction(m_increase_brush_size);
	app.addAction(m_decrease_brush_size);

	m_increase_texture_idx = LUMIX_NEW(editor.getAllocator(), Action)("Next terrain texture", "nextTerrainTexture");
	m_increase_texture_idx->is_global = false;
	m_increase_texture_idx->func.bind<TerrainEditor, &TerrainEditor::nextTerrainTexture>(this);
	m_decrease_texture_idx = LUMIX_NEW(editor.getAllocator(), Action)("Previous terrain texture", "prevTerrainTexture");
	m_decrease_texture_idx->func.bind<TerrainEditor, &TerrainEditor::prevTerrainTexture>(this);
	m_decrease_texture_idx->is_global = false;
	app.addAction(m_increase_texture_idx);
	app.addAction(m_decrease_texture_idx);

	m_smooth_terrain_action = LUMIX_NEW(editor.getAllocator(), Action)("Smooth terrain", "smoothTerrain");
	m_smooth_terrain_action->is_global = false;
	m_lower_terrain_action = LUMIX_NEW(editor.getAllocator(), Action)("Lower terrain", "lowerTerrain");
	m_lower_terrain_action->is_global = false;
	app.addAction(m_smooth_terrain_action);
	app.addAction(m_lower_terrain_action);

	m_remove_grass_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Remove grass from terrain", "removeGrassFromTerrain");
	m_remove_grass_action->is_global = false;
	app.addAction(m_remove_grass_action);

	m_remove_entity_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Remove entities from terrain", "removeEntitiesFromTerrain");
	m_remove_entity_action->is_global = false;
	app.addAction(m_remove_entity_action);

	editor.addPlugin(*this);
	m_terrain_brush_size = 10;
	m_terrain_brush_strength = 0.1f;
	m_action_type = RAISE_HEIGHT;
	m_texture_idx = 0;
	m_grass_mask = 1;
	m_is_align_with_normal = false;
	m_is_rotate_x = false;
	m_is_rotate_y = false;
	m_is_rotate_z = false;
	m_rotate_x_spread = m_rotate_y_spread = m_rotate_z_spread = Vec2(0, Math::PI * 2);

	editor.universeDestroyed().bind<TerrainEditor, &TerrainEditor::onUniverseDestroyed>(this);
}


void TerrainEditor::splitSplatmap(const char* dir)
{
	auto* render_scene = (RenderScene*)m_component.scene;
	Material* material = render_scene->getTerrainMaterial(m_component.handle);
	if (!material)
	{
		g_log_error.log("Renderer") << "Terrain has no material";
		return;
	}
	Texture* splatmap = material->getTextureByUniform("u_texSplatmap");
	if (!splatmap)
	{
		g_log_error.log("Renderer") << "Terrain's material has no splatmap";
		return;
	}

	Texture* diffuse = material->getTextureByUniform("u_texColor");
	if (!diffuse)
	{
		g_log_error.log("Renderer") << "Terrain's material has no diffuse texture";
		return;
	}

	const u32* data = (const u32*)splatmap->getData();
	ASSERT(data);

	WorldEditor& editor = m_app.getWorldEditor();
	IAllocator& allocator = editor.getAllocator();
	FS::FileSystem& fs = editor.getEngine().getFileSystem();
	Array<u32> out_data(allocator);
	int layers_count = diffuse->layers;
	for (int i = 0; i < layers_count; ++i)
	{
		StaticString<MAX_PATH_LENGTH> out_path_str(dir, "//layer", i, ".tga");
		Path out_path(out_path_str);
		out_data.resize(splatmap->width * splatmap->height);
		for (int y = 0; y < splatmap->height; ++y)
		{
			for (int x = 0; x < splatmap->width; ++x)
			{
				int idx = x + y * splatmap->width;
				out_data[idx] = ((data[idx] & 0x000000ff) == i) ? 0xffffFFFF : 0xff000000;
			}
		}

		FS::IFile* file = fs.open(fs.getDefaultDevice(), out_path, FS::Mode::CREATE_AND_WRITE);
		Texture::saveTGA(file, splatmap->width, splatmap->height, 4, (u8*)&out_data[0], out_path, allocator);
		fs.close(*file);
	}

	int grasses_count = render_scene->getGrassCount(m_component.handle);
	for (int i = 0; i < grasses_count; ++i)
	{
		StaticString<MAX_PATH_LENGTH> out_path_str(dir, "//grass", i, ".tga");
		Path out_path(out_path_str);
		out_data.resize(splatmap->width * splatmap->height);
		u32 mask = 1 << (i + 16);
		for (int y = 0; y < splatmap->height; ++y)
		{
			for (int x = 0; x < splatmap->width; ++x)
			{
				int idx = x + y * splatmap->width;
				out_data[idx] = ((data[idx] & mask) != 0) ? 0xffffFFFF : 0xff000000;
			}
		}

		FS::IFile* file = fs.open(fs.getDefaultDevice(), out_path, FS::Mode::CREATE_AND_WRITE);
		Texture::saveTGA(file, splatmap->width, splatmap->height, 4, (u8*)&out_data[0], out_path, allocator);
		fs.close(*file);
	}

}


void TerrainEditor::mergeSplatmap(const char* dir)
{
	auto* render_scene = (RenderScene*)m_component.scene;
	Material* material = render_scene->getTerrainMaterial(m_component.handle);
	if (!material)
	{
		g_log_error.log("Renderer") << "Terrain has no material";
		return;
	}
	Texture* splatmap = material->getTextureByUniform("u_texSplatmap");
	if (!splatmap)
	{
		g_log_error.log("Renderer") << "Terrain's material has no splatmap";
		return;
	}

	WorldEditor& editor = m_app.getWorldEditor();
	IAllocator& allocator = editor.getAllocator();
	FS::FileSystem& fs = editor.getEngine().getFileSystem();
	Path out_path = splatmap->getPath();
	Array<u8> out_data_array(allocator);
	TGAHeader splatmap_tga_header;

	FS::IFile* file = fs.open(fs.getDefaultDevice(), out_path, FS::Mode::OPEN_AND_READ);
	if (!file)
	{
		g_log_error.log("Renderer") << "Failed to open " << out_path;
		return;
	}
	if (!Texture::loadTGA(*file, splatmap_tga_header, out_data_array, out_path.c_str()))
	{
		fs.close(*file);
		g_log_error.log("Renderer") << "Failed to load " << out_path;
		return;
	}
	fs.close(*file);
	u32* out_data = (u32*)&out_data_array[0];

	using namespace PlatformInterface;
	FileIterator* file_iter = createFileIterator(dir, allocator);
	FileInfo info;

	while (getNextFile(file_iter, &info))
	{
		if (info.is_directory) continue;
		if (!PathUtils::hasExtension(info.filename, "tga")) continue;

		if (startsWith(info.filename, "grass"))
		{
			int grass_idx;
			fromCString(info.filename + 5, lengthOf(info.filename) - 5, &grass_idx);
			StaticString<MAX_PATH_LENGTH> grass_path(dir, "/", info.filename);
			FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(grass_path), FS::Mode::OPEN_AND_READ);
			TGAHeader header;
			Array<u8> tmp_data(allocator);
			if (!Texture::loadTGA(*file, header, tmp_data, grass_path))
			{
				g_log_error.log("Renderer") << "Failed to load " << grass_path;
				fs.close(*file);
			}
			else
			{
				const u32* tmp_data_raw = (const u32*)&tmp_data[0];
				u32 mask = 1 << (16 + grass_idx);
				for (int y = 0; y < header.height; ++y)
				{
					for (int x = 0; x < header.width; ++x)
					{
						int idx = x + y * header.width;
						if ((tmp_data_raw[idx] & 0x00ff0000) != 0) out_data[idx] = out_data[idx] | mask;
					}
				}
			}
			fs.close(*file);
		}
		else if (startsWith(info.filename, "layer"))
		{
			int layer_idx;
			fromCString(info.filename + 5, lengthOf(info.filename) - 5, &layer_idx);
			StaticString<MAX_PATH_LENGTH> layer_path(dir, "/", info.filename);
			FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(layer_path), FS::Mode::OPEN_AND_READ);
			TGAHeader header;
			Array<u8> tmp_data(allocator);
			if (!Texture::loadTGA(*file, header, tmp_data, layer_path))
			{
				g_log_error.log("Renderer") << "Failed to load " << layer_path;
				fs.close(*file);
			}
			else
			{
				const u32* tmp_data_raw = (const u32*)&tmp_data[0];
				for (int y = 0; y < header.height; ++y)
				{
					for (int x = 0; x < header.width; ++x)
					{
						int idx = x + y * header.width;
						if ((tmp_data_raw[idx] & 0x00ff0000) != 0)
						{
							out_data[idx] = (out_data[idx] & 0xffffff00) + layer_idx;
						}
					}
				}
			}
			fs.close(*file);
		}
	}
	destroyFileIterator(file_iter);

	FS::IFile* out_file = fs.open(fs.getDefaultDevice(), out_path, FS::Mode::CREATE_AND_WRITE);
	if (!out_file)
	{
		g_log_error.log("Renderer") << "Failed to save " << out_path;
		return;
	}
	if (!Texture::saveTGA(out_file, splatmap_tga_header.width, splatmap_tga_header.height, 4, (u8*)&out_data[0], out_path, allocator))
	{
		g_log_error.log("Renderer") << "Failed to save " << out_path;
	}
	fs.close(*out_file);
}


void TerrainEditor::nextTerrainTexture()
{
	auto* scene = static_cast<RenderScene*>(m_component.scene);
	auto* material = scene->getTerrainMaterial(m_component.handle);
	Texture* tex = material->getTextureByUniform(TEX_COLOR_UNIFORM);
	if (tex)
	{
		m_texture_idx = Math::minimum(tex->layers - 1, m_texture_idx + 1);
	}
}


void TerrainEditor::prevTerrainTexture()
{
	m_texture_idx = Math::maximum(0, m_texture_idx - 1);
}


void TerrainEditor::increaseBrushSize()
{
	if (m_terrain_brush_size < 10)
	{
		++m_terrain_brush_size;
		return;
	}
	m_terrain_brush_size = Math::minimum(100.0f, m_terrain_brush_size + 10);
}


void TerrainEditor::decreaseBrushSize()
{
	if (m_terrain_brush_size < 10)
	{
		m_terrain_brush_size = Math::maximum(MIN_BRUSH_SIZE, m_terrain_brush_size - 1.0f);
		return;
	}
	m_terrain_brush_size = Math::maximum(MIN_BRUSH_SIZE, m_terrain_brush_size - 10.0f);
}


void TerrainEditor::drawCursor(RenderScene& scene, ComponentHandle terrain, const Vec3& center)
{
	PROFILE_FUNCTION();
	static const int SLICE_COUNT = 30;
	if (m_action_type == TerrainEditor::FLAT_HEIGHT && ImGui::GetIO().KeyCtrl)
	{
		scene.addDebugCross(center, 1.0f, 0xff0000ff, 0);
		return;
	}

	float brush_size = m_terrain_brush_size;
	Vec3 local_center = getRelativePosition(center);
	Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);

	for (int i = 0; i < SLICE_COUNT + 1; ++i)
	{
		float angle_step = Math::PI * 2 / SLICE_COUNT;
		float angle = i * angle_step;
		float next_angle = i * angle_step + angle_step;
		Vec3 local_from = local_center + Vec3(cos(angle), 0, sin(angle)) * brush_size;
		local_from.y = scene.getTerrainHeightAt(terrain, local_from.x, local_from.z);
		local_from.y += 0.25f;
		Vec3 local_to =
			local_center + Vec3(cos(next_angle), 0, sin(next_angle)) * brush_size;
		local_to.y = scene.getTerrainHeightAt(terrain, local_to.x, local_to.z);
		local_to.y += 0.25f;

		Vec3 from = terrain_matrix.transformPoint(local_from);
		Vec3 to = terrain_matrix.transformPoint(local_to);
		scene.addDebugLine(from, to, 0xffff0000, 0);
	}
}


void TerrainEditor::detectModifiers()
{
	bool is_height_tool = m_action_type == LOWER_HEIGHT || m_action_type == RAISE_HEIGHT ||
						  m_action_type == SMOOTH_HEIGHT;
	if (is_height_tool)
	{
		if (m_lower_terrain_action->isActive())
		{
			m_action_type = LOWER_HEIGHT;
		}
		else if (m_smooth_terrain_action->isActive())
		{
			m_action_type = SMOOTH_HEIGHT;
		}
		else
		{
			m_action_type = RAISE_HEIGHT;
		}
	}

	if (m_action_type == ADD_GRASS || m_action_type == REMOVE_GRASS)
	{
		if (m_remove_grass_action->isActive())
		{
			m_action_type = REMOVE_GRASS;
		}
		else
		{
			m_action_type = ADD_GRASS;
		}
	}

	bool is_entity_tool = m_action_type == ENTITY || m_action_type == REMOVE_ENTITY;
	if (is_entity_tool)
	{
		if (m_remove_entity_action->isActive())
		{
			m_action_type = REMOVE_ENTITY;
		}
		else
		{
			m_action_type = ENTITY;
		}
	}
}


Vec3 TerrainEditor::getRelativePosition(const Vec3& world_pos) const
{
	Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);
	Matrix inv_terrain_matrix = terrain_matrix;
	inv_terrain_matrix.inverse();

	return inv_terrain_matrix.transformPoint(world_pos);
}


Texture* TerrainEditor::getHeightmap()
{
	return getMaterial()->getTextureByUniform(HEIGHTMAP_UNIFORM);
}


u16 TerrainEditor::getHeight(const Vec3& world_pos)
{
	auto rel_pos = getRelativePosition(world_pos);
	auto* heightmap = getHeightmap();
	if (!heightmap) return 0;

	auto* data = (u16*)heightmap->getData();
	auto* scene = (RenderScene*)m_component.scene;
	float scale = scene->getTerrainXZScale(m_component.handle);
	return data[int(rel_pos.x / scale) + int(rel_pos.z / scale) * heightmap->width];
}


bool TerrainEditor::onMouseDown(const WorldEditor::RayHit& hit, int, int)
{
	if (!m_is_enabled) return false;
	if (!hit.is_hit) return false;
	if (!hit.entity.isValid()) return false;
	const auto& selected_entities = m_world_editor.getSelectedEntities();
	if (selected_entities.size() != 1) return false;
	bool is_terrain = m_world_editor.getUniverse()->hasComponent(selected_entities[0], TERRAIN_TYPE);
	if (!is_terrain) return false;
	if (m_action_type == NOT_SET || !m_component.isValid()) return false;

	detectModifiers();

	if (selected_entities[0] == hit.entity && m_component.isValid())
	{
		Vec3 hit_pos = hit.pos;
		switch (m_action_type)
		{
			case FLAT_HEIGHT:
				if (ImGui::GetIO().KeyCtrl)
				{
					m_flat_height = getHeight(hit_pos);
				}
				else
				{
					paint(hit.pos, m_action_type, false);
				}
				break;
			case RAISE_HEIGHT:
			case LOWER_HEIGHT:
			case SMOOTH_HEIGHT:
			case REMOVE_GRASS:
			case ADD_GRASS:
			case COLOR:
			case LAYER: paint(hit.pos, m_action_type, false); break;
			case ENTITY: paintEntities(hit.pos); break;
			case REMOVE_ENTITY: removeEntities(hit.pos); break;
			default: ASSERT(false); break;
		}
		return true;
	}
	return true;
}


void TerrainEditor::removeEntities(const Vec3& hit_pos)
{
	if (m_selected_prefabs.empty()) return;
	auto& prefab_system = m_world_editor.getPrefabSystem();

	PROFILE_FUNCTION();

	static const u32 REMOVE_ENTITIES_HASH = crc32("remove_entities");
	m_world_editor.beginCommandGroup(REMOVE_ENTITIES_HASH);

	RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
	Frustum frustum;
	frustum.computeOrtho(hit_pos,
		Vec3(0, 0, 1),
		Vec3(0, 1, 0),
		m_terrain_brush_size,
		m_terrain_brush_size,
		-m_terrain_brush_size,
		m_terrain_brush_size);

	Array<Entity> entities(m_world_editor.getAllocator());
	scene->getModelInstanceEntities(frustum, entities);
	if (m_selected_prefabs.empty())
	{
		for (Entity entity : entities)
		{
			if (prefab_system.getPrefab(entity)) m_world_editor.destroyEntities(&entity, 1);
		}
	}
	else
	{
		for (Entity entity : entities)
		{
			for (auto* res : m_selected_prefabs)
			{
				if ((prefab_system.getPrefab(entity) & 0xffffFFFF) == res->getPath().getHash())
				{
					m_world_editor.destroyEntities(&entity, 1);
					break;
				}
			}
		}
	}
	m_world_editor.endCommandGroup();
}


static bool overlaps(float min1, float max1, float min2, float max2)
{
	return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
}


static void getProjections(const Vec3& axis,
	const Vec3 vertices[8],
	float& min,
	float& max)
{
	min = max = dotProduct(vertices[0], axis);
	for(int i = 1; i < 8; ++i)
	{
		float dot = dotProduct(vertices[i], axis);
		min = Math::minimum(dot, min);
		max = Math::maximum(dot, max);
	}
}


static bool testOBBCollision(const Matrix& matrix_a,
	const Model* model_a,
	const Matrix& matrix_b,
	const Model* model_b,
	float scale)
{
	Vec3 box_a_points[8];
	Vec3 box_b_points[8];

	if(fabs(scale - 1.0) < 0.01f)
	{
		model_a->getAABB().getCorners(matrix_a, box_a_points);
		model_b->getAABB().getCorners(matrix_b, box_b_points);
	}
	else
	{
		Matrix scale_matrix_a = matrix_a;
		scale_matrix_a.multiply3x3(scale);
		Matrix scale_matrix_b = matrix_b;
		scale_matrix_b.multiply3x3(scale);
		model_a->getAABB().getCorners(scale_matrix_a, box_a_points);
		model_b->getAABB().getCorners(scale_matrix_b, box_b_points);
	}

	Vec3 normals[] = {
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

	Vec3 normals_b[] = {
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


static bool isOBBCollision(RenderScene& scene,
	const Array<Array<ModelInstanceMesh>>& meshes,
	const Vec3& pos_a,
	Model* model,
	float scale)
{
	float radius_a_squared = model->getBoundingRadius();
	radius_a_squared = radius_a_squared * radius_a_squared;
	for(auto& submeshes : meshes)
	{
		for(auto& mesh : submeshes)
		{
			auto* model_instance = scene.getModelInstance(mesh.model_instance);
			Vec3 pos_b = model_instance->matrix.getTranslation();
			float radius_b = model_instance->model->getBoundingRadius();
			float radius_squared = radius_a_squared + radius_b * radius_b;
			if ((pos_a - pos_b).squaredLength() < radius_squared * scale * scale)
			{
				Matrix matrix = Matrix::IDENTITY;
				matrix.setTranslation(pos_a);
				if (testOBBCollision(matrix, model, model_instance->matrix, model_instance->model, scale))
				{
					return true;
				}
			}
		}
	}
	return false;
}


void TerrainEditor::paintEntities(const Vec3& hit_pos)
{
	PROFILE_FUNCTION();
	if (m_selected_prefabs.empty()) return;
	auto& prefab_system = m_world_editor.getPrefabSystem();

	static const u32 PAINT_ENTITIES_HASH = crc32("paint_entities");
	m_world_editor.beginCommandGroup(PAINT_ENTITIES_HASH);
	{
		RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
		Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);
		Matrix inv_terrain_matrix = terrain_matrix;
		inv_terrain_matrix.inverse();

		Frustum frustum;
		frustum.computeOrtho(hit_pos,
			Vec3(0, 0, 1),
			Vec3(0, 1, 0),
			m_terrain_brush_size,
			m_terrain_brush_size,
			-m_terrain_brush_size,
			m_terrain_brush_size);
		ComponentUID camera = m_world_editor.getEditCamera();
		Entity camera_entity = scene->getCameraEntity(camera.handle);
		Vec3 camera_pos = scene->getUniverse().getPosition(camera_entity);
		
		auto& meshes = scene->getModelInstanceInfos(frustum, camera_pos, camera.handle, ~0ULL);

		Vec2 size = scene->getTerrainSize(m_component.handle);
		float scale = 1.0f - Math::maximum(0.01f, m_terrain_brush_strength);
		for (int i = 0; i <= m_terrain_brush_size * m_terrain_brush_size / 1000.0f; ++i)
		{
			float angle = Math::randFloat(0, Math::PI * 2);
			float dist = Math::randFloat(0, 1.0f) * m_terrain_brush_size;
			float y = Math::randFloat(m_y_spread.x, m_y_spread.y);
			Vec3 pos(hit_pos.x + cos(angle) * dist, 0, hit_pos.z + sin(angle) * dist);
			Vec3 terrain_pos = inv_terrain_matrix.transformPoint(pos);
			if (terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= size.x && terrain_pos.z <= size.y)
			{
				pos.y = scene->getTerrainHeightAt(m_component.handle, terrain_pos.x, terrain_pos.z) + y;
				pos.y += terrain_matrix.getTranslation().y;
				Quat rot(0, 0, 0, 1);
				if(m_is_align_with_normal)
				{
					RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
					Vec3 normal = scene->getTerrainNormalAt(m_component.handle, pos.x, pos.z);
					Vec3 dir = crossProduct(normal, Vec3(1, 0, 0)).normalized();
					Matrix mtx = Matrix::IDENTITY;
					mtx.setXVector(crossProduct(normal, dir));
					mtx.setYVector(normal);
					mtx.setXVector(dir);
					rot = mtx.getRotation();
				}
				else
				{
					if (m_is_rotate_x)
					{
						float angle = Math::randFloat(m_rotate_x_spread.x, m_rotate_x_spread.y);
						Quat q(Vec3(1, 0, 0), angle);
						rot = q * rot;
					}

					if (m_is_rotate_y)
					{
						float angle = Math::randFloat(m_rotate_y_spread.x, m_rotate_y_spread.y);
						Quat q(Vec3(0, 1, 0), angle);
						rot = q * rot;
					}

					if (m_is_rotate_z)
					{
						float angle = Math::randFloat(m_rotate_z_spread.x, m_rotate_z_spread.y);
						Quat q(rot.rotate(Vec3(0, 0, 1)), angle);
						rot = q * rot;
					}
				}

				float size = Math::randFloat(m_size_spread.x, m_size_spread.y);
				int random_idx = Math::rand(0, m_selected_prefabs.size() - 1);
				if (!m_selected_prefabs[random_idx]) continue;
				Entity entity = prefab_system.instantiatePrefab(*m_selected_prefabs[random_idx], pos, rot, size);
				if (entity.isValid())
				{
					ComponentHandle cmp = scene->getComponent(entity, MODEL_INSTANCE_TYPE);
					Model* model = scene->getModelInstanceModel(cmp);
					if (isOBBCollision(*scene, meshes, pos, model, scale))
					{
						m_world_editor.undo();
					}
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

	ComponentUID camera_cmp = m_world_editor.getEditCamera();
	RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
	Vec3 origin, dir;
	scene->getRay(camera_cmp.handle, {(float)x, (float)y}, origin, dir);
	RayCastModelHit hit = scene->castRayTerrain(m_component.handle, origin, dir);
	if (hit.m_is_hit)
	{
		bool is_terrain = m_world_editor.getUniverse()->hasComponent(hit.m_entity, TERRAIN_TYPE);
		if (!is_terrain) return;

		switch (m_action_type)
		{
			case FLAT_HEIGHT:
			case RAISE_HEIGHT:
			case LOWER_HEIGHT:
			case SMOOTH_HEIGHT:
			case REMOVE_GRASS:
			case ADD_GRASS:
			case COLOR:
			case LAYER: paint(hit.m_origin + hit.m_dir * hit.m_t, m_action_type, true); break;
			case ENTITY: paintEntities(hit.m_origin + hit.m_dir * hit.m_t); break;
			case REMOVE_ENTITY: removeEntities(hit.m_origin + hit.m_dir * hit.m_t); break;
			default: ASSERT(false); break;
		}
	}
}


void TerrainEditor::onMouseUp(int, int, MouseButton::Value)
{
}


Material* TerrainEditor::getMaterial()
{
	auto* scene = static_cast<RenderScene*>(m_component.scene);
	return scene->getTerrainMaterial(m_component.handle);
}


void TerrainEditor::onGUI()
{
	if (m_decrease_brush_size->isRequested()) m_decrease_brush_size->func.invoke();
	if (m_increase_brush_size->isRequested()) m_increase_brush_size->func.invoke();
	if (m_increase_texture_idx->isRequested()) m_increase_texture_idx->func.invoke();
	if (m_decrease_texture_idx->isRequested()) m_decrease_texture_idx->func.invoke();

	auto* scene = static_cast<RenderScene*>(m_component.scene);
	if (!ImGui::CollapsingHeader("Terrain editor", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) return;

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
		COLOR,
		GRASS
	};

	bool is_grass_enabled = scene->isGrassEnabled();

	if (ImGui::Checkbox("Enable grass", &is_grass_enabled)) scene->enableGrass(is_grass_enabled);

	if (ImGui::Combo(
			"Brush type", &m_current_brush, "Height\0Layer\0Entity\0Color\0Grass\0"))
	{
		m_action_type = m_current_brush == HEIGHT ? TerrainEditor::RAISE_HEIGHT : m_action_type;
	}

	switch (m_current_brush)
	{
		case HEIGHT:
			if (ImGui::Button("Save heightmap"))
				getMaterial()->getTextureByUniform(HEIGHTMAP_UNIFORM)->save();
			break;
		case GRASS:
		case LAYER:
			if (ImGui::Button("Save layermap and grassmap"))
				getMaterial()->getTextureByUniform(SPLATMAP_UNIFORM)->save();
			break;
		case COLOR:
			if (ImGui::Button("Save colormap"))
				getMaterial()->getTextureByUniform(COLORMAP_UNIFORM)->save();
			break;
	}

	if (m_current_brush == LAYER || m_current_brush == GRASS || m_current_brush == COLOR)
	{
		if (m_brush_texture)
		{
			static auto th = m_brush_texture->handle;
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
			char filename[MAX_PATH_LENGTH];
			if (PlatformInterface::getOpenFilename(filename, lengthOf(filename), "All\0*.*\0", nullptr))
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
					m_brush_texture = LUMIX_NEW(m_world_editor.getAllocator(), Texture)(
						Path("brush_texture"), *rm.get(TEXTURE_TYPE), m_world_editor.getAllocator());
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
			bool is_flat_tool = m_action_type == TerrainEditor::FLAT_HEIGHT;
			if (ImGui::Checkbox("Flat", &is_flat_tool))
			{
				m_action_type = is_flat_tool ? TerrainEditor::FLAT_HEIGHT : TerrainEditor::RAISE_HEIGHT;
			}

			if (m_action_type == TerrainEditor::FLAT_HEIGHT)
			{
				ImGui::SameLine();
				ImGui::Text("- Press Ctrl to pick height");
			}
			break;
		}
		case GRASS:
		{
			m_action_type = TerrainEditor::ADD_GRASS;
			int type_count = scene->getGrassCount(m_component.handle);
			for (int i = 0; i < type_count; ++i)
			{
				if (i % 4 != 0) ImGui::SameLine();
				bool b = (m_grass_mask & (1 << i)) != 0;
				if (ImGui::Checkbox(StaticString<20>("", i, "###rb", i), &b))
				{
					if (b)
					{
						m_grass_mask |= 1 << i;
					}
					else
					{
						m_grass_mask &= ~(1 << i);
					}
				}
			}
			break;
		}
		case COLOR:
		{
			m_action_type = TerrainEditor::COLOR;
			ImGui::ColorPicker3("", &m_color.x);
			break;
		}
		case LAYER:
		{
			m_action_type = TerrainEditor::LAYER;
			Texture* tex = getMaterial()->getTextureByUniform(TEX_COLOR_UNIFORM);
			if (tex)
			{
				for (int i = 0; i < tex->layers; ++i)
				{
					if (i % 4 != 0) ImGui::SameLine();
					if (ImGui::RadioButton(StaticString<20>("", i, "###rb", i), m_texture_idx == i))
					{
						m_texture_idx = i;
					}
				}
			}
			break;
		}
		case ENTITY:
		{
			m_action_type = TerrainEditor::ENTITY;
			
			static char filter[100] = {0};
			static ImVec2 size(-1, 100);
			ImGui::LabellessInputText("Filter", filter, sizeof(filter));
			ImGui::ListBoxHeader("Prefabs", size);
			int resources_idx  = m_app.getAssetBrowser().getTypeIndex(PREFAB_TYPE);
			auto& all_prefabs = m_app.getAssetBrowser().getResources(resources_idx);
			for(int i = 0; i < all_prefabs.size(); ++i)
			{
				if (filter[0] != 0 && stristr(all_prefabs[i].c_str(), filter) == nullptr) continue;
				int selected_idx = m_selected_prefabs.find([&](PrefabResource* res) -> bool {
					return res && res->getPath() == all_prefabs[i];
				});
				bool selected = selected_idx >= 0;
				if (ImGui::Checkbox(all_prefabs[i].c_str(), &selected))
				{
					if (selected)
					{
						ResourceManagerBase* prefab_manager = m_world_editor.getEngine().getResourceManager().get(PREFAB_TYPE);
						PrefabResource* prefab = (PrefabResource*)prefab_manager->load(all_prefabs[i]);
						m_selected_prefabs.push(prefab);
					}
					else
					{
						PrefabResource* prefab = m_selected_prefabs[selected_idx];
						m_selected_prefabs.eraseFast(selected_idx);
						prefab->getResourceManager().unload(*prefab);
					}
				}
			}
			ImGui::ListBoxFooter();
			ImGui::HSplitter("after_prefab", &size);

			if(ImGui::Checkbox("Align with normal", &m_is_align_with_normal))
			{
				if(m_is_align_with_normal) m_is_rotate_x = m_is_rotate_y = m_is_rotate_z = false;
			}
			if (ImGui::Checkbox("Rotate around X", &m_is_rotate_x))
			{
				if (m_is_rotate_x) m_is_align_with_normal = false;
			}
			if (m_is_rotate_x)
			{
				Vec2 tmp = m_rotate_x_spread;
				tmp.x = Math::radiansToDegrees(tmp.x);
				tmp.y = Math::radiansToDegrees(tmp.y);
				if (ImGui::DragFloat2("Rotate X spread", &tmp.x))
				{
					m_rotate_x_spread.x = Math::degreesToRadians(tmp.x);
					m_rotate_x_spread.y = Math::degreesToRadians(tmp.y);
				}
			}
			if (ImGui::Checkbox("Rotate around Y", &m_is_rotate_y))
			{
				if (m_is_rotate_y) m_is_align_with_normal = false;
			}
			if (m_is_rotate_y)
			{
				Vec2 tmp = m_rotate_y_spread;
				tmp.x = Math::radiansToDegrees(tmp.x);
				tmp.y = Math::radiansToDegrees(tmp.y);
				if (ImGui::DragFloat2("Rotate Y spread", &tmp.x))
				{
					m_rotate_y_spread.x = Math::degreesToRadians(tmp.x);
					m_rotate_y_spread.y = Math::degreesToRadians(tmp.y);
				}
			}
			if(ImGui::Checkbox("Rotate around Z", &m_is_rotate_z))
			{
				if(m_is_rotate_z) m_is_align_with_normal = false;
			}
			if (m_is_rotate_z)
			{
				Vec2 tmp = m_rotate_z_spread;
				tmp.x = Math::radiansToDegrees(tmp.x);
				tmp.y = Math::radiansToDegrees(tmp.y);
				if (ImGui::DragFloat2("Rotate Z spread", &tmp.x))
				{
					m_rotate_z_spread.x = Math::degreesToRadians(tmp.x);
					m_rotate_z_spread.y = Math::degreesToRadians(tmp.y);
				}
			}
			ImGui::DragFloat2("Size spread", &m_size_spread.x, 0.01f);
			m_size_spread.x = Math::minimum(m_size_spread.x, m_size_spread.y);
			ImGui::DragFloat2("Y spread", &m_y_spread.x, 0.01f);
			m_y_spread.x = Math::minimum(m_y_spread.x, m_y_spread.y);
		}
		break;
		default: ASSERT(false); break;
	}

	ImGui::Separator();
	char dir[MAX_PATH_LENGTH];
	if (ImGui::Button("Split") && PlatformInterface::getOpenDirectory(dir, lengthOf(dir), nullptr))
	{
		splitSplatmap(dir);
	}
	ImGui::SameLine();
	if (ImGui::Button("Merge") && PlatformInterface::getOpenDirectory(dir, lengthOf(dir), nullptr))
	{
		mergeSplatmap(dir);
	}

	if(!m_component.isValid()) return;
	if(m_action_type == NOT_SET) return;
	if(!m_is_enabled) return;

	float mouse_x = m_world_editor.getMousePos().x;
	float mouse_y = m_world_editor.getMousePos().y;

	for(auto entity : m_world_editor.getSelectedEntities())
	{
		ComponentHandle terrain = m_world_editor.getUniverse()->getComponent(entity, TERRAIN_TYPE).handle;
		if(!terrain.isValid()) continue;

		ComponentUID camera_cmp = m_world_editor.getEditCamera();
		RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
		Vec3 origin, dir;
		scene->getRay(camera_cmp.handle, {(float)mouse_x, (float)mouse_y}, origin, dir);
		RayCastModelHit hit = scene->castRayTerrain(terrain, origin, dir);

		if(hit.m_is_hit)
		{
			Vec3 center = hit.m_origin + hit.m_dir * hit.m_t;
			drawCursor(*scene, terrain, center);
			return;
		}
	}
}


void TerrainEditor::paint(const Vec3& hit_pos, ActionType action_type, bool old_stroke)
{
	PaintTerrainCommand* command = LUMIX_NEW(m_world_editor.getAllocator(), PaintTerrainCommand)(m_world_editor,
		action_type,
		action_type == ADD_GRASS || action_type == REMOVE_GRASS ? (int)m_grass_mask : m_texture_idx,
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


} // namespace Lumix