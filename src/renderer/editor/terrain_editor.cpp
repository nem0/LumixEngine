#include "terrain_editor.h"
#include "editor/asset_compiler.h"
#include "editor/ieditor_command.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "physics/physics_scene.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"
#include <cmath>


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType TERRAIN_TYPE = Reflection::getComponentType("terrain");
static const ComponentType HEIGHTFIELD_TYPE = Reflection::getComponentType("physical_heightfield");
static const char* HEIGHTMAP_SLOT_NAME = "Heightmap";
static const char* SPLATMAP_SLOT_NAME = "Splatmap";
static const char* DETAIL_ALBEDO_SLOT_NAME = "Albedo";
static const char* SATELLITE_SLOT_NAME = "Satellite";
static const float MIN_BRUSH_SIZE = 0.5f;


struct PaintTerrainCommand final : public IEditorCommand
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
		const DVec3& hit_pos,
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
		ASSERT(terrain.isValid());
		
		m_mask.resize(mask.size());
		for (int i = 0; i < mask.size(); ++i) {
			m_mask[i] = mask[i];
		}

		m_width = m_height = m_x = m_y = -1;
		const Transform entity_transform = editor.getUniverse()->getTransform((EntityRef)terrain.entity).inverted();
		DVec3 local_pos = entity_transform.transform(hit_pos);
		float terrain_size = static_cast<RenderScene*>(terrain.scene)->getTerrainSize((EntityRef)terrain.entity).x;
		local_pos = local_pos / terrain_size;
		local_pos.y = -1;

		Item& item = m_items.emplace();
		item.m_local_pos = local_pos.toFloat();
		item.m_radius = radius / terrain_size;
		item.m_amount = rel_amount;
		item.m_color = color;
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
			r.from_x = maximum(0, int(texture_size * (m_local_pos.x - m_radius) - 0.5f));
			r.from_y = maximum(0, int(texture_size * (m_local_pos.z - m_radius) - 0.5f));
			r.to_x = minimum(texture_size, int(texture_size * (m_local_pos.x + m_radius) + 0.5f));
			r.to_y = minimum(texture_size, int(texture_size * (m_local_pos.z + m_radius) + 0.5f));
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
		return m_terrain.entity.isValid() ? scene->getTerrainMaterial((EntityRef)m_terrain.entity) : nullptr;
	}


	Texture* getDestinationTexture()
	{
		const char* uniform_name;
		switch (m_action_type)
		{
			case TerrainEditor::REMOVE_GRASS:
			case TerrainEditor::ADD_GRASS:
			case TerrainEditor::LAYER:
				uniform_name = SPLATMAP_SLOT_NAME;
				break;
			case TerrainEditor::COLOR:
				uniform_name = SATELLITE_SLOT_NAME;
				break;
			default:
				uniform_name = HEIGHTMAP_SLOT_NAME;
				break;
		}

		return getMaterial()->getTextureByName(uniform_name);
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
		return 1.0f - minimum(dist / (texture_size * item.m_radius), 1.0f);
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
					d[0] += u8((item.m_color.x * 255 - d[0]) * attenuation * item.m_amount);
					d[1] += u8((item.m_color.y * 255 - d[1]) * attenuation * item.m_amount);
					d[2] += u8((item.m_color.z * 255 - d[2]) * attenuation * item.m_amount);
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
							data[offset + 1] += minimum(255 - data[offset + 1], add);
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
		Rectangle rect = item.getBoundingRectangle(texture_size);

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
		Rectangle rect = item.getBoundingRectangle(texture_size);

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
				t = clamp(1 - t, 0.0f, 1.0f);
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
		Rectangle rect = item.getBoundingRectangle(texture_size);

		const float STRENGTH_MULTIPLICATOR = 256.0f;
		float amount = maximum(item.m_amount * item.m_amount * STRENGTH_MULTIPLICATOR, 1.0f);

		for (int i = rect.from_x, end = rect.to_x; i < end; ++i)
		{
			for (int j = rect.from_y, end2 = rect.to_y; j < end2; ++j)
			{
				float attenuation = getAttenuation(item, i, j, texture_size);
				int offset = i - m_x + (j - m_y) * m_width;

				int add = int(attenuation * amount);
				u16 x = ((u16*)texture->getData())[(i + j * texture_size)];
				x += m_action_type == TerrainEditor::RAISE_HEIGHT ? minimum(add, 0xFFFF - x)
														   : maximum(-add, -x);
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
		m_new_data.resize(bpp * maximum(1, (rect.to_x - rect.from_x) * (rect.to_y - rect.from_y)));
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
		if (!m_terrain.isValid()) return;

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
		const EntityRef e = (EntityRef)m_terrain.entity;
		static_cast<RenderScene*>(m_terrain.scene)->forceGrassUpdate(e);

		if (m_action_type != TerrainEditor::LAYER && m_action_type != TerrainEditor::COLOR &&
			m_action_type != TerrainEditor::ADD_GRASS && m_action_type != TerrainEditor::REMOVE_GRASS)
		{
			IScene* scene = m_world_editor.getUniverse()->getScene(crc32("physics"));
			if (!scene) return;

			auto* phy_scene = static_cast<PhysicsScene*>(scene);
			if (!scene->getUniverse().hasComponent(e, HEIGHTFIELD_TYPE)) return;

			phy_scene->updateHeighfieldData(e, m_x, m_y, m_width, m_height, &data[0], bpp);
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
		rect.from_x = maximum(int(s * (item.m_local_pos.x - item.m_radius) - 0.5f), 0);
		rect.from_y = maximum(int(s * (item.m_local_pos.z - item.m_radius) - 0.5f), 0);
		rect.to_x = minimum(int(s * (item.m_local_pos.x + item.m_radius) + 0.5f), texture->width);
		rect.to_y = minimum(int(s * (item.m_local_pos.z + item.m_radius) + 0.5f), texture->height);
		for (int i = 1; i < m_items.size(); ++i)
		{
			Item& item = m_items[i];
			rect.from_x = minimum(int(s * (item.m_local_pos.x - item.m_radius) - 0.5f), rect.from_x);
			rect.to_x = maximum(int(s * (item.m_local_pos.x + item.m_radius) + 0.5f), rect.to_x);
			rect.from_y = minimum(int(s * (item.m_local_pos.z - item.m_radius) - 0.5f), rect.from_y);
			rect.to_y = maximum(int(s * (item.m_local_pos.z + item.m_radius) + 0.5f), rect.to_y);
		}
		rect.from_x = maximum(rect.from_x, 0);
		rect.to_x = minimum(rect.to_x, texture->width);
		rect.from_y = maximum(rect.from_y, 0);
		rect.to_y = minimum(rect.to_y, texture->height);
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
	m_component.entity = INVALID_ENTITY;
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
	m_increase_brush_size = LUMIX_NEW(editor.getAllocator(), Action)("Increase brush size", "Terrain editor - Increase brush size", "increaseBrushSize");
	m_increase_brush_size->is_global = false;
	m_increase_brush_size->func.bind<TerrainEditor, &TerrainEditor::increaseBrushSize>(this);
	m_decrease_brush_size = LUMIX_NEW(editor.getAllocator(), Action)("Decrease brush size", "Terrain editor - decrease brush size", "decreaseBrushSize");
	m_decrease_brush_size->func.bind<TerrainEditor, &TerrainEditor::decreaseBrushSize>(this);
	m_decrease_brush_size->is_global = false;
	app.addAction(m_increase_brush_size);
	app.addAction(m_decrease_brush_size);

	m_increase_texture_idx = LUMIX_NEW(editor.getAllocator(), Action)("Next terrain texture", "Terrain editor - next texture", "nextTerrainTexture");
	m_increase_texture_idx->is_global = false;
	m_increase_texture_idx->func.bind<TerrainEditor, &TerrainEditor::nextTerrainTexture>(this);
	m_decrease_texture_idx = LUMIX_NEW(editor.getAllocator(), Action)("Previous terrain texture", "Terrain editor - previous texture", "prevTerrainTexture");
	m_decrease_texture_idx->func.bind<TerrainEditor, &TerrainEditor::prevTerrainTexture>(this);
	m_decrease_texture_idx->is_global = false;
	app.addAction(m_increase_texture_idx);
	app.addAction(m_decrease_texture_idx);

	m_smooth_terrain_action = LUMIX_NEW(editor.getAllocator(), Action)("Smooth terrain", "Terrain editor - smooth", "smoothTerrain");
	m_smooth_terrain_action->is_global = false;
	m_lower_terrain_action = LUMIX_NEW(editor.getAllocator(), Action)("Lower terrain", "Terrain editor - lower", "lowerTerrain");
	m_lower_terrain_action->is_global = false;
	app.addAction(m_smooth_terrain_action);
	app.addAction(m_lower_terrain_action);

	m_remove_grass_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Remove grass from terrain", "Terrain editor - remove grass", "removeGrassFromTerrain");
	m_remove_grass_action->is_global = false;
	app.addAction(m_remove_grass_action);

	m_remove_entity_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Remove entities from terrain", "Terrain editor - remove entities", "removeEntitiesFromTerrain");
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
	m_rotate_x_spread = m_rotate_y_spread = m_rotate_z_spread = Vec2(0, PI * 2);

	editor.universeDestroyed().bind<TerrainEditor, &TerrainEditor::onUniverseDestroyed>(this);
}


void TerrainEditor::nextTerrainTexture()
{
	Material* material = getMaterial();
	if (!material) return;
	Texture* tex = material->getTextureByName(DETAIL_ALBEDO_SLOT_NAME);
	if (tex) m_texture_idx = minimum(tex->layers - 1, m_texture_idx + 1);
}


void TerrainEditor::prevTerrainTexture()
{
	m_texture_idx = maximum(0, m_texture_idx - 1);
}


void TerrainEditor::increaseBrushSize()
{
	if (m_terrain_brush_size < 10)
	{
		++m_terrain_brush_size;
		return;
	}
	m_terrain_brush_size = minimum(100.0f, m_terrain_brush_size + 10);
}


void TerrainEditor::decreaseBrushSize()
{
	if (m_terrain_brush_size < 10)
	{
		m_terrain_brush_size = maximum(MIN_BRUSH_SIZE, m_terrain_brush_size - 1.0f);
		return;
	}
	m_terrain_brush_size = maximum(MIN_BRUSH_SIZE, m_terrain_brush_size - 10.0f);
}


void TerrainEditor::drawCursor(RenderScene& scene, EntityRef terrain, const DVec3& center)
{
	PROFILE_FUNCTION();
	constexpr int SLICE_COUNT = 30;
	constexpr float angle_step = PI * 2 / SLICE_COUNT;
	if (m_action_type == TerrainEditor::FLAT_HEIGHT && ImGui::GetIO().KeyCtrl) {
		scene.addDebugCross(center, 1.0f, 0xff0000ff);
		return;
	}

	float brush_size = m_terrain_brush_size;
	const Vec3 local_center = getRelativePosition(center).toFloat();
	const Transform terrain_transform = m_world_editor.getUniverse()->getTransform((EntityRef)m_component.entity);

	for (int i = 0; i < SLICE_COUNT + 1; ++i) {
		const float angle = i * angle_step;
		const float next_angle = i * angle_step + angle_step;
		Vec3 local_from = local_center + Vec3(cos(angle), 0, sin(angle)) * brush_size;
		local_from.y = scene.getTerrainHeightAt(terrain, local_from.x, local_from.z);
		local_from.y += 0.25f;
		Vec3 local_to =
			local_center + Vec3(cos(next_angle), 0, sin(next_angle)) * brush_size;
		local_to.y = scene.getTerrainHeightAt(terrain, local_to.x, local_to.z);
		local_to.y += 0.25f;

		const DVec3 from = terrain_transform.transform(local_from);
		const DVec3 to = terrain_transform.transform(local_to);
		scene.addDebugLine(from, to, 0xffff0000);
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


DVec3 TerrainEditor::getRelativePosition(const DVec3& world_pos) const
{
	const Transform transform = m_world_editor.getUniverse()->getTransform((EntityRef)m_component.entity);
	const Transform inv_transform = transform.inverted();

	return inv_transform.transform(world_pos);
}


Texture* TerrainEditor::getHeightmap() const
{
	return getMaterial()->getTextureByName(HEIGHTMAP_SLOT_NAME);
}


u16 TerrainEditor::getHeight(const DVec3& world_pos) const
{
	auto rel_pos = getRelativePosition(world_pos);
	auto* heightmap = getHeightmap();
	if (!heightmap) return 0;

	auto* data = (u16*)heightmap->getData();
	auto* scene = (RenderScene*)m_component.scene;
	float scale = scene->getTerrainXZScale((EntityRef)m_component.entity);
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

	if ((EntityPtr)selected_entities[0] == hit.entity && m_component.isValid()) {
		const DVec3 hit_pos = hit.pos;
		switch (m_action_type)
		{
			case FLAT_HEIGHT:
				if (ImGui::GetIO().KeyCtrl) {
					m_flat_height = getHeight(hit_pos);
				}
				else {
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


void TerrainEditor::removeEntities(const DVec3& hit_pos)
{
	if (m_selected_prefabs.empty()) return;
	auto& prefab_system = m_world_editor.getPrefabSystem();

	PROFILE_FUNCTION();

	static const u32 REMOVE_ENTITIES_HASH = crc32("remove_entities");
	m_world_editor.beginCommandGroup(REMOVE_ENTITIES_HASH);

	RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
	ShiftedFrustum frustum;
	frustum.computeOrtho(hit_pos,
		Vec3(0, 0, 1),
		Vec3(0, 1, 0),
		m_terrain_brush_size,
		m_terrain_brush_size,
		-m_terrain_brush_size,
		m_terrain_brush_size);

	Array<EntityRef> entities(m_world_editor.getAllocator());
	scene->getModelInstanceEntities(frustum, entities);
	if (m_selected_prefabs.empty())
	{
		for (EntityRef entity : entities)
		{
			if (prefab_system.getPrefab(entity)) m_world_editor.destroyEntities(&entity, 1);
		}
	}
	else
	{
		for (EntityRef entity : entities)
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
		min = minimum(dot, min);
		max = maximum(dot, max);
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
	const Array<Array<MeshInstance>>& meshes,
	const Vec3& pos_a,
	Model* model,
	float scale)
{
			// TODO
	ASSERT(false);
/*float radius_a_squared = model->getBoundingRadius();
	radius_a_squared = radius_a_squared * radius_a_squared;
	for(auto& submeshes : meshes)
	{
		for(auto& mesh : submeshes)
		{
			auto* model_instance = scene.getModelInstance(mesh.owner);
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
	}*/
	return false;
}


void TerrainEditor::paintEntities(const DVec3& hit_pos)
{
	// TODO 
	ASSERT(false);
	/*PROFILE_FUNCTION();
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
		EntityRef camera_entity = camera.entity;
		Vec3 camera_pos = scene->getUniverse().getPosition(camera_entity);
		
		auto& meshes = scene->getModelInstanceInfos(frustum, camera_pos, camera.entity, ~0ULL);

		Vec2 size = scene->getTerrainSize(m_component.entity);
		float scale = 1.0f - maximum(0.01f, m_terrain_brush_strength);
		for (int i = 0; i <= m_terrain_brush_size * m_terrain_brush_size / 1000.0f; ++i)
		{
			float angle = randFloat(0, PI * 2);
			float dist = randFloat(0, 1.0f) * m_terrain_brush_size;
			float y = randFloat(m_y_spread.x, m_y_spread.y);
			Vec3 pos(hit_pos.x + cos(angle) * dist, 0, hit_pos.z + sin(angle) * dist);
			Vec3 terrain_pos = inv_terrain_matrix.transformPoint(pos);
			if (terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= size.x && terrain_pos.z <= size.y)
			{
				pos.y = scene->getTerrainHeightAt(m_component.entity, terrain_pos.x, terrain_pos.z) + y;
				pos.y += terrain_matrix.getTranslation().y;
				Quat rot(0, 0, 0, 1);
				if(m_is_align_with_normal)
				{
					RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
					Vec3 normal = scene->getTerrainNormalAt(m_component.entity, pos.x, pos.z);
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
						float angle = randFloat(m_rotate_x_spread.x, m_rotate_x_spread.y);
						Quat q(Vec3(1, 0, 0), angle);
						rot = q * rot;
					}

					if (m_is_rotate_y)
					{
						float angle = randFloat(m_rotate_y_spread.x, m_rotate_y_spread.y);
						Quat q(Vec3(0, 1, 0), angle);
						rot = q * rot;
					}

					if (m_is_rotate_z)
					{
						float angle = randFloat(m_rotate_z_spread.x, m_rotate_z_spread.y);
						Quat q(rot.rotate(Vec3(0, 0, 1)), angle);
						rot = q * rot;
					}
				}

				float size = randFloat(m_size_spread.x, m_size_spread.y);
				int random_idx = rand(0, m_selected_prefabs.size() - 1);
				if (!m_selected_prefabs[random_idx]) continue;
				EntityRef entity = prefab_system.instantiatePrefab(*m_selected_prefabs[random_idx], pos, rot, size);
				if (entity.isValid())
				{
					if (scene->getUniverse().hasComponent(entity, MODEL_INSTANCE_TYPE))
					{
						Model* model = scene->getModelInstanceModel(entity);
						if (isOBBCollision(*scene, meshes, pos, model, scale))
						{
							m_world_editor.undo();
						}
					}
				}
			}
		}
	}
	m_world_editor.endCommandGroup();*/
}


void TerrainEditor::onMouseMove(int x, int y, int, int)
{
	if (!m_is_enabled) return;

	detectModifiers();

	RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
	DVec3 origin;
	Vec3 dir;
	m_world_editor.getViewport().getRay({(float)x, (float)y}, origin, dir);
	RayCastModelHit hit = scene->castRayTerrain((EntityRef)m_component.entity, origin, dir);
	if (hit.is_hit) {
		bool is_terrain = m_world_editor.getUniverse()->hasComponent((EntityRef)hit.entity, TERRAIN_TYPE);
		if (!is_terrain) return;

		const DVec3 hit_point = hit.origin + hit.dir * hit.t;
		switch (m_action_type) {
			case FLAT_HEIGHT:
			case RAISE_HEIGHT:
			case LOWER_HEIGHT:
			case SMOOTH_HEIGHT:
			case REMOVE_GRASS:
			case ADD_GRASS:
			case COLOR:
			case LAYER: paint(hit_point, m_action_type, true); break;
			case ENTITY: paintEntities(hit_point); break;
			case REMOVE_ENTITY: removeEntities(hit_point); break;
			default: ASSERT(false); break;
		}
	}
}


void TerrainEditor::onMouseUp(int, int, OS::MouseButton)
{
}


Material* TerrainEditor::getMaterial() const
{
	if (!m_component.isValid()) return nullptr;
	auto* scene = static_cast<RenderScene*>(m_component.scene);
	return scene->getTerrainMaterial((EntityRef)m_component.entity);
}


void TerrainEditor::onGUI()
{
	if (m_decrease_brush_size->isRequested()) m_decrease_brush_size->func.invoke();
	if (m_increase_brush_size->isRequested()) m_increase_brush_size->func.invoke();
	if (m_increase_texture_idx->isRequested()) m_increase_texture_idx->func.invoke();
	if (m_decrease_texture_idx->isRequested()) m_decrease_texture_idx->func.invoke();

	auto* scene = static_cast<RenderScene*>(m_component.scene);
	ImGui::Unindent();
	if (!ImGui::TreeNodeEx("Terrain editor"))
	{
		ImGui::Indent();
		return;
	}

	ImGui::Checkbox("Editor enabled", &m_is_enabled);
	if (!m_is_enabled)
	{
		ImGui::TreePop();
		ImGui::Indent();
		return;
	}

	if (!getMaterial())
	{
		ImGui::Text("No heightmap");
		ImGui::TreePop();
		ImGui::Indent();
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
				getMaterial()->getTextureByName(HEIGHTMAP_SLOT_NAME)->save();
			break;
		case GRASS:
		case LAYER:
			if (ImGui::Button("Save layermap and grassmap"))
				getMaterial()->getTextureByName(SPLATMAP_SLOT_NAME)->save();
			break;
		case COLOR:
			if (ImGui::Button("Save colormap"))
				getMaterial()->getTextureByName(SATELLITE_SLOT_NAME)->save();
			break;
		case ENTITY: break;
	}

	if (m_current_brush == LAYER || m_current_brush == GRASS || m_current_brush == COLOR)
	{
		if (m_brush_texture)
		{
			const ffr::TextureHandle th = m_brush_texture->handle;
			ImGui::Image((void*)(uintptr_t)th.value, ImVec2(100, 100));
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
			if (OS::getOpenFilename(filename, lengthOf(filename), "All\0*.*\0", nullptr))
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

					IPlugin* plugin = m_world_editor.getEngine().getPluginManager().getPlugin("renderer");
					Renderer& renderer = *static_cast<Renderer*>(plugin);
					m_brush_texture = LUMIX_NEW(m_world_editor.getAllocator(), Texture)(
						Path("brush_texture"), *rm.get(Texture::TYPE), renderer, m_world_editor.getAllocator());
					m_brush_texture->create(image_width, image_height, ffr::TextureFormat::RGBA8, data, image_width * image_height * 4);

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
			int type_count = scene->getGrassCount((EntityRef)m_component.entity);
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
			Texture* tex = getMaterial()->getTextureByName(DETAIL_ALBEDO_SLOT_NAME);
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
			auto& resources = m_app.getAssetCompiler().lockResources();
			for (const AssetCompiler::ResourceItem& res : resources) {
				if (res.type != PrefabResource::TYPE) continue;
				if (filter[0] != 0 && stristr(res.path.c_str(), filter) == nullptr) continue;
				int selected_idx = m_selected_prefabs.find([&](PrefabResource* r) -> bool {
					return r && r->getPath() == res.path;
				});
				bool selected = selected_idx >= 0;
				if (ImGui::Checkbox(res.path.c_str(), &selected)) {
					if (selected) {
						ResourceManagerHub& manager = m_world_editor.getEngine().getResourceManager();
						PrefabResource* prefab = manager.load<PrefabResource>(res.path);
						m_selected_prefabs.push(prefab);
					}
					else {
						PrefabResource* prefab = m_selected_prefabs[selected_idx];
						m_selected_prefabs.eraseFast(selected_idx);
						prefab->getResourceManager().unload(*prefab);
					}
				}
			}
			m_app.getAssetCompiler().unlockResources();
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
				tmp.x = radiansToDegrees(tmp.x);
				tmp.y = radiansToDegrees(tmp.y);
				if (ImGui::DragFloat2("Rotate X spread", &tmp.x))
				{
					m_rotate_x_spread.x = degreesToRadians(tmp.x);
					m_rotate_x_spread.y = degreesToRadians(tmp.y);
				}
			}
			if (ImGui::Checkbox("Rotate around Y", &m_is_rotate_y))
			{
				if (m_is_rotate_y) m_is_align_with_normal = false;
			}
			if (m_is_rotate_y)
			{
				Vec2 tmp = m_rotate_y_spread;
				tmp.x = radiansToDegrees(tmp.x);
				tmp.y = radiansToDegrees(tmp.y);
				if (ImGui::DragFloat2("Rotate Y spread", &tmp.x))
				{
					m_rotate_y_spread.x = degreesToRadians(tmp.x);
					m_rotate_y_spread.y = degreesToRadians(tmp.y);
				}
			}
			if(ImGui::Checkbox("Rotate around Z", &m_is_rotate_z))
			{
				if(m_is_rotate_z) m_is_align_with_normal = false;
			}
			if (m_is_rotate_z)
			{
				Vec2 tmp = m_rotate_z_spread;
				tmp.x = radiansToDegrees(tmp.x);
				tmp.y = radiansToDegrees(tmp.y);
				if (ImGui::DragFloat2("Rotate Z spread", &tmp.x))
				{
					m_rotate_z_spread.x = degreesToRadians(tmp.x);
					m_rotate_z_spread.y = degreesToRadians(tmp.y);
				}
			}
			ImGui::DragFloat2("Size spread", &m_size_spread.x, 0.01f);
			m_size_spread.x = minimum(m_size_spread.x, m_size_spread.y);
			ImGui::DragFloat2("Y spread", &m_y_spread.x, 0.01f);
			m_y_spread.x = minimum(m_y_spread.x, m_y_spread.y);
		}
		break;
		default: ASSERT(false); break;
	}

	if (!m_component.isValid() || m_action_type == NOT_SET || !m_is_enabled)
	{
		ImGui::TreePop();
		ImGui::Indent();

		return;
	}

	const Vec2 mp = m_world_editor.getMousePos();

	for(auto entity : m_world_editor.getSelectedEntities()) {
		if (!m_world_editor.getUniverse()->hasComponent(entity, TERRAIN_TYPE)) continue;
		
		RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
		DVec3 origin;
		Vec3 dir;
		m_world_editor.getViewport().getRay(mp, origin, dir);
		const RayCastModelHit hit = scene->castRayTerrain(entity, origin, dir);

		if(hit.is_hit) {
			DVec3 center = hit.origin + hit.dir * hit.t;
			drawCursor(*scene, entity, center);
			ImGui::TreePop();
			ImGui::Indent();
			return;
		}
	}
	ImGui::TreePop();
	ImGui::Indent();
}


void TerrainEditor::paint(const DVec3& hit_pos, ActionType action_type, bool old_stroke)
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