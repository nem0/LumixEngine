#include <imgui/imgui.h>

#include "terrain_editor.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/entity_folders.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "physics/physics_scene.h"
#include "renderer/culling_system.h"
#include "renderer/editor/composite_texture.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");
static const ComponentType HEIGHTFIELD_TYPE = reflection::getComponentType("physical_heightfield");
static const char* HEIGHTMAP_SLOT_NAME = "Heightmap";
static const char* SPLATMAP_SLOT_NAME = "Splatmap";
static const char* DETAIL_ALBEDO_SLOT_NAME = "Detail albedo";
static const char* DETAIL_NORMAL_SLOT_NAME = "Detail normal";
static const float MIN_BRUSH_SIZE = 0.5f;


struct PaintTerrainCommand final : IEditorCommand
{
	struct Rectangle
	{
		int from_x;
		int from_y;
		int to_x;
		int to_y;
	};


	PaintTerrainCommand(WorldEditor& editor,
		TerrainEditor::ActionType action_type,
		u16 grass_mask,
		u64 textures_mask,
		const DVec3& hit_pos,
		const Array<bool>& mask,
		float radius,
		float rel_amount,
		u16 flat_height,
		Vec3 color,
		ComponentUID terrain,
		u32 layers_mask,
		Vec2 fixed_value,
		bool can_be_merged)
		: m_world_editor(editor)
		, m_terrain(terrain)
		, m_can_be_merged(can_be_merged)
		, m_new_data(editor.getAllocator())
		, m_old_data(editor.getAllocator())
		, m_items(editor.getAllocator())
		, m_action_type(action_type)
		, m_textures_mask(textures_mask)
		, m_grass_mask(grass_mask)
		, m_mask(editor.getAllocator())
		, m_flat_height(flat_height)
		, m_layers_masks(layers_mask)
		, m_fixed_value(fixed_value)
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
		item.m_local_pos = Vec3(local_pos);
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
			m_textures_mask == my_command.m_textures_mask && m_layers_masks == my_command.m_layers_masks)
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
			case TerrainEditor::LAYER:
				uniform_name = SPLATMAP_SLOT_NAME;
				break;
			default:
				uniform_name = HEIGHTMAP_SLOT_NAME;
				break;
		}

		return getMaterial()->getTextureByName(uniform_name);
	}


	u16 computeAverage16(const Texture* texture, int from_x, int to_x, int from_y, int to_y)
	{
		ASSERT(texture->format == gpu::TextureFormat::R16);
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
			((texture_size * item.m_local_pos.x - 0.5f - i) * (texture_size * item.m_local_pos.x - 0.5f - i) +
				 (texture_size * item.m_local_pos.z - 0.5f - j) * (texture_size * item.m_local_pos.z - 0.5f - j));
		dist = powf(dist, 4);
		float max_dist = powf(texture_size * item.m_radius, 8);
		return 1.0f - minimum(dist / max_dist, 1.0f);
	}


	bool isMasked(float x, float y)
	{
		if (m_mask.size() == 0) return true;

		int s = int(sqrtf((float)m_mask.size()));
		int ix = int(x * s);
		int iy = int(y * s);

		return m_mask[int(ix + x * iy)];
	}


	void rasterLayerItem(Texture* texture, Array<u8>& data, Item& item)
	{
		int texture_size = texture->width;
		Rectangle r = item.getBoundingRectangle(texture_size);

		if (texture->format != gpu::TextureFormat::RGBA8)
		{
			ASSERT(false);
			return;
		}

		float fx = 0;
		float fstepx = 1.0f / (r.to_x - r.from_x);
		float fstepy = 1.0f / (r.to_y - r.from_y);
		u8 tex[64];
		u32 tex_count = 0;
		for (u8 i = 0; i < 64; ++i) {
			if (m_textures_mask & ((u64)1 << i)) {
				tex[tex_count] = i;
				++tex_count;
			}
		}
		if (tex_count == 0) return;

		for (int i = r.from_x, end = r.to_x; i < end; ++i, fx += fstepx) {
			float fy = 0;
			for (int j = r.from_y, end2 = r.to_y; j < end2; ++j, fy += fstepy) {
				if (!isMasked(fx, fy)) continue;

				const int offset = 4 * (i - m_x + (j - m_y) * m_width);
				for (u32 layer = 0; layer < 2; ++layer) {
					if ((m_layers_masks & (1 << layer)) == 0) continue;
					
					const float attenuation = getAttenuation(item, i, j, texture_size);
					int add = int(attenuation * item.m_amount * 255);
					if (add <= 0) continue;

					if (((u64)1 << data[offset]) & m_textures_mask) {
						if (layer == 1) {
							if (m_fixed_value.x >= 0) {
								data[offset + 1] = (u8)clamp(randFloat(m_fixed_value.x, m_fixed_value.y) * 255.f, 0.f, 255.f);
							}
							else {
								data[offset + 1] += minimum(255 - data[offset + 1], add);
							}
						}
					}
					else {
						if (layer == 1) {
							if (m_fixed_value.x >= 0) {
								data[offset + 1] = (u8)clamp(randFloat(m_fixed_value.x, m_fixed_value.y) * 255.f, 0.f, 255.f);
							}
							else {
								data[offset + 1] = add;
							}
						}
						data[offset] = tex[rand() % tex_count];
					}
				}
			}
		}
	}

	void rasterGrassItem(Texture* texture, Array<u8>& data, Item& item, bool remove)
	{
		int texture_size = texture->width;
		Rectangle r = item.getBoundingRectangle(texture_size);

		if (texture->format != gpu::TextureFormat::RGBA8)
		{
			ASSERT(false);
			return;
		}

		float fx = 0;
		float fstepx = 1.0f / (r.to_x - r.from_x);
		float fstepy = 1.0f / (r.to_y - r.from_y);
		for (int i = r.from_x, end = r.to_x; i < end; ++i, fx += fstepx) {
			float fy = 0;
			for (int j = r.from_y, end2 = r.to_y; j < end2; ++j, fy += fstepy) {
				if (isMasked(fx, fy)) {
					int offset = 4 * (i - m_x + (j - m_y) * m_width) + 2;
					float attenuation = getAttenuation(item, i, j, texture_size);
					int add = int(attenuation * item.m_amount * 255);
					if (add > 0) {
						u16* tmp = ((u16*)&data[offset]);
						if (remove) {
							*tmp &= ~m_grass_mask;
						}
						else {
							*tmp |= m_grass_mask;
						}
					}
				}
			}
		}
	}


	void rasterSmoothHeightItem(Texture* texture, Array<u8>& data, Item& item)
	{
		ASSERT(texture->format == gpu::TextureFormat::R16);

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
		ASSERT(texture->format == gpu::TextureFormat::R16);

		int texture_size = texture->width;
		Rectangle rect = item.getBoundingRectangle(texture_size);

		for (int i = rect.from_x, end = rect.to_x; i < end; ++i)
		{
			for (int j = rect.from_y, end2 = rect.to_y; j < end2; ++j)
			{
				int offset = i - m_x + (j - m_y) * m_width;
				float dist = sqrtf(
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
		if (m_action_type == TerrainEditor::LAYER || m_action_type == TerrainEditor::REMOVE_GRASS)
		{
			if (m_textures_mask) {
				rasterLayerItem(texture, data, item);
			}
			if (m_grass_mask) {
				rasterGrassItem(texture, data, item, m_action_type == TerrainEditor::REMOVE_GRASS);
			}
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

		ASSERT(texture->format == gpu::TextureFormat::R16);

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
		const u32 bpp = gpu::getBytesPerPixel(texture->format);
		Rectangle rect;
		getBoundingRectangle(texture, rect);
		m_new_data.resize(bpp * maximum(1, (rect.to_x - rect.from_x) * (rect.to_y - rect.from_y)));
		if(m_old_data.size() > 0) {
			memcpy(&m_new_data[0], &m_old_data[0], m_new_data.size());
		}

		for (int item_index = 0; item_index < m_items.size(); ++item_index)
		{
			Item& item = m_items[item_index];
			rasterItem(texture, m_new_data, item);
		}
	}


	void saveOldData()
	{
		auto texture = getDestinationTexture();
		const u32 bpp = gpu::getBytesPerPixel(texture->format);
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
				for (u32 k = 0; k < bpp; ++k)
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
		const u32 bpp = gpu::getBytesPerPixel(texture->format);

		for (int j = m_y; j < m_y + m_height; ++j)
		{
			for (int i = m_x; i < m_x + m_width; ++i)
			{
				int index = bpp * (i + j * texture->width);
				for (u32 k = 0; k < bpp; ++k)
				{
					texture->getData()[index + k] = data[bpp * (i - m_x + (j - m_y) * m_width) + k];
				}
			}
		}
		texture->onDataUpdated(m_x, m_y, m_width, m_height);
		const EntityRef e = (EntityRef)m_terrain.entity;

		if (m_action_type != TerrainEditor::LAYER && m_action_type != TerrainEditor::REMOVE_GRASS)
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
		const u32 bpp = gpu::getBytesPerPixel(texture->format);
		new_data.resize(bpp * new_w * (rect.to_y - rect.from_y));
		old_data.resize(bpp * new_w * (rect.to_y - rect.from_y));

		// original
		for (int row = rect.from_y; row < rect.to_y; ++row)
		{
			memcpy(&new_data[(row - rect.from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->width + rect.from_x * bpp],
				bpp * new_w);
			memcpy(&old_data[(row - rect.from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->width + rect.from_x * bpp],
				bpp * new_w);
		}

		// new
		for (int row = 0; row < m_height; ++row)
		{
			memcpy(&new_data[((row + m_y - rect.from_y) * new_w + m_x - rect.from_x) * bpp],
				&m_new_data[row * bpp * m_width],
				bpp * m_width);
			memcpy(&old_data[((row + m_y - rect.from_y) * new_w + m_x - rect.from_x) * bpp],
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
		rect.to_x = minimum(1 + int(s * (item.m_local_pos.x + item.m_radius) + 0.5f), texture->width);
		rect.to_y = minimum(1 + int(s * (item.m_local_pos.z + item.m_radius) + 0.5f), texture->height);
		for (int i = 1; i < m_items.size(); ++i)
		{
			Item& item = m_items[i];
			rect.from_x = minimum(int(s * (item.m_local_pos.x - item.m_radius) - 0.5f), rect.from_x);
			rect.to_x = maximum(1 + int(s * (item.m_local_pos.x + item.m_radius) + 0.5f), rect.to_x);
			rect.from_y = minimum(int(s * (item.m_local_pos.z - item.m_radius) - 0.5f), rect.from_y);
			rect.to_y = maximum(1 + int(s * (item.m_local_pos.z + item.m_radius) + 0.5f), rect.to_y);
		}
		rect.from_x = maximum(rect.from_x, 0);
		rect.to_x = minimum(rect.to_x, texture->width);
		rect.from_y = maximum(rect.from_y, 0);
		rect.to_y = minimum(rect.to_y, texture->height);
	}


private:
	Array<u8> m_new_data;
	Array<u8> m_old_data;
	u64 m_textures_mask;
	u16 m_grass_mask;
	int m_width;
	int m_height;
	int m_x;
	int m_y;
	TerrainEditor::ActionType m_action_type;
	Array<Item> m_items;
	ComponentUID m_terrain;
	WorldEditor& m_world_editor;
	Array<bool> m_mask;
	u16 m_flat_height;
	u32 m_layers_masks;
	Vec2 m_fixed_value;
	bool m_can_be_merged;
};


TerrainEditor::~TerrainEditor()
{
	m_app.removeAction(&m_smooth_terrain_action);
	m_app.removeAction(&m_lower_terrain_action);
	m_app.removeAction(&m_remove_grass_action);
	m_app.removeAction(&m_remove_entity_action);

	m_world_editor.universeDestroyed().unbind<&TerrainEditor::onUniverseDestroyed>(this);
	if (m_brush_texture)
	{
		m_brush_texture->destroy();
		LUMIX_DELETE(m_world_editor.getAllocator(), m_brush_texture);
	}
	m_app.removePlugin(*this);
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
	, m_albedo_composite(editor.getAllocator())
{
	m_smooth_terrain_action.init("Smooth terrain", "Terrain editor - smooth", "smoothTerrain", "", false);
	m_lower_terrain_action.init("Lower terrain", "Terrain editor - lower", "lowerTerrain", "", false);
	m_remove_grass_action.init("Remove grass from terrain", "Terrain editor - remove grass", "removeGrassFromTerrain", "", false);
	m_remove_entity_action.init("Remove entities from terrain", "Terrain editor - remove entities", "removeEntitiesFromTerrain", "", false);

	app.addAction(&m_smooth_terrain_action);
	app.addAction(&m_lower_terrain_action);
	app.addAction(&m_remove_grass_action);
	app.addAction(&m_remove_entity_action);

	app.addPlugin(*this);

	m_terrain_brush_size = 10;
	m_terrain_brush_strength = 0.1f;
	m_textures_mask = 0b1;
	m_layers_mask = 0b1;
	m_grass_mask = 1;
	m_is_align_with_normal = false;
	m_is_rotate_x = false;
	m_is_rotate_y = false;
	m_is_rotate_z = false;
	m_rotate_x_spread = m_rotate_y_spread = m_rotate_z_spread = Vec2(0, PI * 2);

	editor.universeDestroyed().bind<&TerrainEditor::onUniverseDestroyed>(this);
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


void TerrainEditor::drawCursor(RenderScene& scene, EntityRef terrain_entity, const DVec3& center)
{
	PROFILE_FUNCTION();
	Terrain* terrain = scene.getTerrain(terrain_entity);
	constexpr int SLICE_COUNT = 30;
	constexpr float angle_step = PI * 2 / SLICE_COUNT;
	if (m_mode == Mode::HEIGHT && m_is_flat_height && ImGui::GetIO().KeyCtrl) {
		scene.addDebugCross(center, 1.0f, 0xff0000ff);
		return;
	}

	float brush_size = m_terrain_brush_size;
	const Vec3 local_center = Vec3(getRelativePosition(center));
	const Transform terrain_transform = m_world_editor.getUniverse()->getTransform((EntityRef)m_component.entity);

	for (int i = 0; i < SLICE_COUNT + 1; ++i) {
		const float angle = i * angle_step;
		const float next_angle = i * angle_step + angle_step;
		Vec3 local_from = local_center + Vec3(cosf(angle), 0, sinf(angle)) * brush_size;
		local_from.y = terrain->getHeight(local_from.x, local_from.z);
		local_from.y += 0.25f;
		Vec3 local_to = local_center + Vec3(cosf(next_angle), 0, sinf(next_angle)) * brush_size;
		local_to.y = terrain->getHeight(local_to.x, local_to.z);
		local_to.y += 0.25f;

		const DVec3 from = terrain_transform.transform(local_from);
		const DVec3 to = terrain_transform.transform(local_to);
		scene.addDebugLine(from, to, 0xffff0000);
	}

	const Vec3 rel_pos = Vec3(terrain_transform.inverted().transform(center));
	const i32 w = terrain->getWidth();
	const i32 h = terrain->getHeight();
	const float scale = terrain->getXZScale();
	const IVec3 p = IVec3(rel_pos / scale);
	const i32 half_extents = i32(1 + brush_size / scale);
	for (i32 j = p.z - half_extents; j <= p.z + half_extents; ++j) {
		for (i32 i = p.x - half_extents; i <= p.x + half_extents; ++i) {
			DVec3 p00(i * scale, 0, j * scale);
			DVec3 p10((i + 1) * scale, 0, j * scale);
			DVec3 p11((i + 1) * scale, 0, (j + 1) * scale);
			DVec3 p01(i * scale, 0, (j + 1) * scale);

			p00.y = terrain->getHeight(i, j);
			p10.y = terrain->getHeight(i + 1, j);
			p11.y = terrain->getHeight(i + 1, j + 1);
			p01.y = terrain->getHeight(i, j + 1);

			p00 = terrain_transform.transform(p00);
			p10 = terrain_transform.transform(p10);
			p01 = terrain_transform.transform(p01);
			p11 = terrain_transform.transform(p11);

			scene.addDebugLine(p10, p01, 0xff800000);
			scene.addDebugLine(p10, p11, 0xff800000);
			scene.addDebugLine(p00, p10, 0xff800000);
			scene.addDebugLine(p01, p11, 0xff800000);
			scene.addDebugLine(p00, p01, 0xff800000);
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


bool TerrainEditor::onMouseDown(UniverseView& view, int x, int y)
{
	if (!m_is_enabled) return false;
	const UniverseView::RayHit hit = view.getCameraRaycastHit(x, y);
	if (!hit.entity.isValid()) return false;
	const auto& selected_entities = m_world_editor.getSelectedEntities();
	if (selected_entities.size() != 1) return false;
	bool is_terrain = m_world_editor.getUniverse()->hasComponent(selected_entities[0], TERRAIN_TYPE);
	if (!is_terrain) return false;
	if (!m_component.isValid()) return false;

	if ((EntityPtr)selected_entities[0] == hit.entity && m_component.isValid()) {
		const DVec3 hit_pos = hit.pos;
		switch(m_mode) {
			case Mode::ENTITY:
				if (m_remove_entity_action.isActive()) {
					removeEntities(hit.pos);
				}
				else {
					paintEntities(hit.pos);
				}
				break;
			case Mode::HEIGHT:
				if (m_is_flat_height) {
					if (ImGui::GetIO().KeyCtrl) {
						m_flat_height = getHeight(hit_pos);
					}
					else {
						paint(hit.pos, TerrainEditor::FLAT_HEIGHT, false);
					}
				}
				else {
					TerrainEditor::ActionType action = TerrainEditor::RAISE_HEIGHT;
					if (m_lower_terrain_action.isActive()) {
						action = TerrainEditor::LOWER_HEIGHT;
					}
					else if (m_smooth_terrain_action.isActive()) {
						action = TerrainEditor::SMOOTH_HEIGHT;
					}
					paint(hit.pos, action, false); break;
				}
				break;
			case Mode::LAYER:
				TerrainEditor::ActionType action = TerrainEditor::LAYER;
				if (m_remove_grass_action.isActive()) {
					action = TerrainEditor::REMOVE_GRASS;
				}
				paint(hit.pos, action, false);
				break;
		}
		return true;
	}
	return true;
}

static void getProjections(const Vec3& axis,
	const Vec3 vertices[8],
	float& min,
	float& max)
{
	max = dot(vertices[0], axis);
	min = max;
	for(int i = 1; i < 8; ++i)
	{
		float d = dot(vertices[i], axis);
		min = minimum(d, min);
		max = maximum(d, max);
	}
}

static bool overlaps(float min1, float max1, float min2, float max2)
{
	return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
}

static bool testOBBCollision(const AABB& a,
	const Matrix& mtx_b,
	const AABB& b)
{
	Vec3 box_a_points[8];
	Vec3 box_b_points[8];

	a.getCorners(Matrix::IDENTITY, box_a_points);
	b.getCorners(mtx_b, box_b_points);

	const Vec3 normals[] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
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
		normalize(mtx_b.getXVector()), normalize(mtx_b.getYVector()), normalize(mtx_b.getZVector())};
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

void TerrainEditor::removeEntities(const DVec3& hit_pos)
{
	if (m_selected_prefabs.empty()) return;
	auto& prefab_system = m_world_editor.getPrefabSystem();

	PROFILE_FUNCTION();

	RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
	Universe& universe = scene->getUniverse();
	ShiftedFrustum frustum;
	frustum.computeOrtho(hit_pos,
		Vec3(0, 0, 1),
		Vec3(0, 1, 0),
		m_terrain_brush_size,
		m_terrain_brush_size,
		-m_terrain_brush_size,
		m_terrain_brush_size);
	const AABB brush_aabb(Vec3(-m_terrain_brush_size), Vec3(m_terrain_brush_size));

	CullResult* meshes = scene->getRenderables(frustum, RenderableTypes::MESH);
	if(meshes) {
		meshes->merge(scene->getRenderables(frustum, RenderableTypes::MESH_GROUP));
	}
	else {
		meshes = scene->getRenderables(frustum, RenderableTypes::MESH_GROUP);
	}
	if(meshes) {
		meshes->merge(scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE));
	}
	else {
		meshes = scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE);
	}
	if(!meshes) return;

	m_world_editor.beginCommandGroup("remove_entities");
	if (m_selected_prefabs.empty())
	{
		meshes->forEach([&](EntityRef entity){
			if (prefab_system.getPrefab(entity) == 0) return; 
			
			const Model* model = scene->getModelInstanceModel(entity);
			const AABB entity_aabb = model ? model->getAABB() : AABB(Vec3::ZERO, Vec3::ZERO);
			const bool collide = testOBBCollision(brush_aabb, universe.getRelativeMatrix(entity, hit_pos), entity_aabb);
			if (collide) m_world_editor.destroyEntities(&entity, 1);
		});
	}
	else
	{
		meshes->forEach([&](EntityRef entity){
			for (auto* res : m_selected_prefabs)
			{
				if ((prefab_system.getPrefab(entity) & 0xffffFFFF) == res->getPath().getHash())
				{
					const Model* model = scene->getModelInstanceModel(entity);
					const AABB entity_aabb = model ? model->getAABB() : AABB(Vec3::ZERO, Vec3::ZERO);
					const bool collide = testOBBCollision(brush_aabb, universe.getRelativeMatrix(entity, hit_pos), entity_aabb);
					if (collide) m_world_editor.destroyEntities(&entity, 1);
				}
			}
		});
	}
	m_world_editor.endCommandGroup();
	meshes->free(scene->getEngine().getPageAllocator());
}


static bool isOBBCollision(RenderScene& scene,
	const CullResult* meshes,
	const Transform& model_tr,
	Model* model,
	bool ignore_not_in_folder,
	const EntityFolders& folders,
	EntityFolders::FolderID folder)
{
	float radius_a_squared = model->getOriginBoundingRadius() * model_tr.scale;
	radius_a_squared = radius_a_squared * radius_a_squared;
	Universe& universe = scene.getUniverse();
	Span<const ModelInstance> model_instances = scene.getModelInstances();
	const Transform* transforms = universe.getTransforms();
	while(meshes) {
		const EntityRef* entities = meshes->entities;
		for (u32 i = 0, c = meshes->header.count; i < c; ++i) {
			const EntityRef mesh = entities[i];
			// we resolve collisions when painting by removing recently added mesh, but we do not refresh `meshes`
			// so it can contain invalid entities
			if (!universe.hasEntity(mesh)) continue;

			const ModelInstance& model_instance = model_instances[mesh.index];
			const Transform& tr_b = transforms[mesh.index];
			const float radius_b = model_instance.model->getOriginBoundingRadius() * tr_b.scale;
			const float radius_squared = radius_a_squared + radius_b * radius_b;
			if (squaredLength(model_tr.pos - tr_b.pos) < radius_squared) {
				const Transform rel_tr = model_tr.inverted() * tr_b;
				Matrix mtx = rel_tr.rot.toMatrix();
				mtx.multiply3x3(rel_tr.scale);
				mtx.setTranslation(Vec3(rel_tr.pos));

				if (testOBBCollision(model->getAABB(), mtx, model_instance.model->getAABB())) {
					if (ignore_not_in_folder && folders.getFolder(EntityRef{mesh.index}) != folder) {
						continue;
					}
					return true;
				}
			}
		}
		meshes = meshes->header.next;
	}
	return false;
}

static bool areAllReady(Span<PrefabResource*> prefabs) {
	for (PrefabResource* p : prefabs) if (!p->isReady()) return false;
	return true;
}


void TerrainEditor::paintEntities(const DVec3& hit_pos)
{
	PROFILE_FUNCTION();
	if (m_selected_prefabs.empty()) return;
	if (!areAllReady(m_selected_prefabs)) return;

	auto& prefab_system = m_world_editor.getPrefabSystem();

	m_world_editor.beginCommandGroup("paint_entities");
	{
		RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
		const Transform terrain_tr = m_world_editor.getUniverse()->getTransform((EntityRef)m_component.entity);
		const Transform inv_terrain_tr = terrain_tr.inverted();

		ShiftedFrustum frustum;
		frustum.computeOrtho(hit_pos,
			Vec3(0, 0, 1),
			Vec3(0, 1, 0),
			m_terrain_brush_size,
			m_terrain_brush_size,
			-m_terrain_brush_size,
			m_terrain_brush_size);
		
		CullResult* meshes = scene->getRenderables(frustum, RenderableTypes::MESH);
		if (meshes) meshes->merge(scene->getRenderables(frustum, RenderableTypes::MESH_GROUP));
		else meshes = scene->getRenderables(frustum, RenderableTypes::MESH_GROUP);
		if (meshes) meshes->merge(scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE));
		else meshes = scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE);

		const EntityFolders& folders = m_world_editor.getEntityFolders();
		const EntityFolders::FolderID folder = folders.getSelectedFolder();

		Vec2 size = scene->getTerrainSize((EntityRef)m_component.entity);
		float scale = 1.0f - maximum(0.01f, m_terrain_brush_strength);
		for (int i = 0; i <= m_terrain_brush_size * m_terrain_brush_size / 100.0f * m_terrain_brush_strength; ++i)
		{
			const float angle = randFloat(0, PI * 2);
			const float dist = randFloat(0, 1.0f) * m_terrain_brush_size;
			const float y = randFloat(m_y_spread.x, m_y_spread.y);
			DVec3 pos(hit_pos.x + cosf(angle) * dist, 0, hit_pos.z + sinf(angle) * dist);
			const Vec3 terrain_pos = Vec3(inv_terrain_tr.transform(pos));
			if (terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= size.x && terrain_pos.z <= size.y)
			{
				pos.y = scene->getTerrainHeightAt((EntityRef)m_component.entity, terrain_pos.x, terrain_pos.z) + y;
				pos.y += terrain_tr.pos.y;
				Quat rot(0, 0, 0, 1);
				if(m_is_align_with_normal)
				{
					RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
					Vec3 normal = scene->getTerrainNormalAt((EntityRef)m_component.entity, terrain_pos.x, terrain_pos.z);
					Vec3 dir = normalize(cross(normal, Vec3(1, 0, 0)));
					Matrix mtx = Matrix::IDENTITY;
					mtx.setXVector(cross(normal, dir));
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
				const EntityPtr entity = prefab_system.instantiatePrefab(*m_selected_prefabs[random_idx], pos, rot, size);
				if (entity.isValid()) {
					if (scene->getUniverse().hasComponent((EntityRef)entity, MODEL_INSTANCE_TYPE)) {
						Model* model = scene->getModelInstanceModel((EntityRef)entity);
						const Transform tr = { pos, rot, size * scale };
						if (isOBBCollision(*scene, meshes, tr, model, m_ignore_entities_not_in_folder, folders, folder)) {
							m_world_editor.undo();
						}
					}
				}
			}
		}
		meshes->free(m_app.getEngine().getPageAllocator());
	}
	m_world_editor.endCommandGroup();
}


void TerrainEditor::onMouseMove(UniverseView& view, int x, int y, int, int)
{
	if (!m_is_enabled) return;

	RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
	DVec3 origin;
	Vec3 dir;
	view.getViewport().getRay({(float)x, (float)y}, origin, dir);
	RayCastModelHit hit = scene->castRayTerrain((EntityRef)m_component.entity, origin, dir);
	if (hit.is_hit) {
		bool is_terrain = m_world_editor.getUniverse()->hasComponent((EntityRef)hit.entity, TERRAIN_TYPE);
		if (!is_terrain) return;

		const DVec3 hit_point = hit.origin + hit.dir * hit.t;
		switch(m_mode) {
			case Mode::ENTITY:
				if (m_remove_entity_action.isActive()) {
					removeEntities(hit_point);
				}
				else {
					paintEntities(hit_point);
				}
				break;
			case Mode::HEIGHT:
				if (m_is_flat_height) {
					if (ImGui::GetIO().KeyCtrl) {
						m_flat_height = getHeight(hit_point);
					}
					else {
						paint(hit_point, TerrainEditor::FLAT_HEIGHT, false);
					}
				}
				else {
					TerrainEditor::ActionType action = TerrainEditor::RAISE_HEIGHT;
					if (m_lower_terrain_action.isActive()) {
						action = TerrainEditor::LOWER_HEIGHT;
					}
					else if (m_smooth_terrain_action.isActive()) {
						action = TerrainEditor::SMOOTH_HEIGHT;
					}
					paint(hit_point, action, false); break;
				}
				break;
			case Mode::LAYER:
				TerrainEditor::ActionType action = TerrainEditor::LAYER;
				if (m_remove_grass_action.isActive()) {
					action = TerrainEditor::REMOVE_GRASS;
				}
				paint(hit_point, action, false);
				break;
		}
	}
}


Material* TerrainEditor::getMaterial() const
{
	if (!m_component.isValid()) return nullptr;
	auto* scene = static_cast<RenderScene*>(m_component.scene);
	return scene->getTerrainMaterial((EntityRef)m_component.entity);
}

static Array<u8> getFileContent(const char* path, IAllocator& allocator) {
	Array<u8> res(allocator);
	os::InputFile file;
	if (!file.open(path)) return res;

	res.resize((u32)file.size());
	if (!file.read(res.begin(), res.byte_size())) res.clear();

	file.close();
	return res;
}

void TerrainEditor::layerGUI() {
	m_mode = Mode::LAYER;
	auto* scene = static_cast<RenderScene*>(m_component.scene);
	if (getMaterial()
		&& getMaterial()->getTextureByName(SPLATMAP_SLOT_NAME)
		&& ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_SAVE, ImGui::GetStyle().Colors[ImGuiCol_Text], "Save"))
	{
		getMaterial()->getTextureByName(SPLATMAP_SLOT_NAME)->save();
	}

	if (m_brush_texture)
	{
		ImGui::Image(m_brush_texture->handle, ImVec2(100, 100));
		if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_TIMES, ImGui::GetStyle().Colors[ImGuiCol_Text], "Clear brush mask"))
		{
			m_brush_texture->destroy();
			LUMIX_DELETE(m_world_editor.getAllocator(), m_brush_texture);
			m_brush_mask.clear();
			m_brush_texture = nullptr;
		}
		ImGui::SameLine();
	}

	ImGui::SameLine();
	if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_MASK, ImGui::GetStyle().Colors[ImGuiCol_Text], "Select brush mask"))
	{
		char filename[LUMIX_MAX_PATH];
		if (os::getOpenFilename(Span(filename), "All\0*.*\0", nullptr))
		{
			int image_width;
			int image_height;
			int image_comp;
			Array<u8> tmp = getFileContent(filename, m_app.getAllocator());
			auto* data = stbi_load_from_memory(tmp.begin(), tmp.byte_size(), &image_width, &image_height, &image_comp, 4);
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

				Lumix::IPlugin* plugin = m_world_editor.getEngine().getPluginManager().getPlugin("renderer");
				Renderer& renderer = *static_cast<Renderer*>(plugin);
				m_brush_texture = LUMIX_NEW(m_world_editor.getAllocator(), Texture)(
					Path("brush_texture"), *rm.get(Texture::TYPE), renderer, m_world_editor.getAllocator());
				m_brush_texture->create(image_width, image_height, gpu::TextureFormat::RGBA8, data, image_width * image_height * 4);

				stbi_image_free(data);
			}
		}
	}

	char grass_mode_shortcut[64];
	if (m_remove_entity_action.shortcutText(Span(grass_mode_shortcut))) {
		ImGuiEx::Label(StaticString<64>("Grass mode (", grass_mode_shortcut, ")"));
		ImGui::TextUnformatted(m_remove_entity_action.isActive() ? "Remove" : "Add");
	}
	int type_count = scene->getGrassCount((EntityRef)m_component.entity);
	for (int i = 0; i < type_count; ++i) {
		ImGui::SameLine();
		if (i == 0 || ImGui::GetContentRegionAvail().x < 50) ImGui::NewLine();
		bool b = (m_grass_mask & (1 << i)) != 0;
		m_app.getAssetBrowser().tile(scene->getGrassPath((EntityRef)m_component.entity, i), b);
		if (ImGui::IsItemClicked()) {
			if (!ImGui::GetIO().KeyCtrl) m_grass_mask = 0;
			if (b) {
				m_grass_mask &= ~(1 << i);
			}
			else {
				m_grass_mask |= 1 << i;
			}
		}
	}

	Texture* albedo = getMaterial()->getTextureByName(DETAIL_ALBEDO_SLOT_NAME);
	Texture* normal = getMaterial()->getTextureByName(DETAIL_NORMAL_SLOT_NAME);
	
	if (!albedo) {
		ImGui::Text("No detail albedo in material %s", getMaterial()->getPath().c_str());
		return;
	}
	if (albedo->isFailure()) {
		ImGui::Text("%s failed to load", albedo->getPath().c_str());
		return;
	}
	if (!albedo->isReady()) {
		ImGui::Text("Loading %s...", albedo->getPath().c_str());
		return;
	}
	
	if (!normal) {
		ImGui::Text("No detail normal in material %s", getMaterial()->getPath().c_str());
		return;
	}
	if (normal->isFailure()) {
		ImGui::Text("%s failed to load", normal->getPath().c_str());
		return;
	}
	if (!normal->isReady()) {
		ImGui::Text("Loading %s...", normal->getPath().c_str());
		return;
	}

	if (albedo->layers != normal->layers) {
		ImGui::TextWrapped(ICON_FA_EXCLAMATION_TRIANGLE " albedo texture %s has different number of layers than normal texture %s"
			, albedo->getPath().c_str()
			, normal->getPath().c_str());
	}

	bool primary = m_layers_mask & 0b1;
	bool secondary = m_layers_mask & 0b10;


	// TODO shader does not handle secondary surfaces now, so pretend they don't exist
	// uncomment once shader is ready
	m_layers_mask = 0xb11;
	/*ImGuiEx::Label("Primary surface");
	ImGui::Checkbox("##prim", &primary);
	ImGuiEx::Label("Secondary surface");
	ImGui::Checkbox("##sec", &secondary);
	if (secondary) {
		bool use = m_fixed_value.x >= 0;
		ImGuiEx::Label("Use fixed value");
		if (ImGui::Checkbox("##fxd", &use)) {
			m_fixed_value.x = use ? 0.f : -1.f;
		}
		if (m_fixed_value.x >= 0) {
		ImGuiEx::Label("Min/max");
			ImGui::DragFloatRange2("##minmax", &m_fixed_value.x, &m_fixed_value.y, 0.01f, 0, 1);
		}
	}*/

	FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
	if (albedo->getPath() != m_albedo_composite_path) {
		m_albedo_composite.loadSync(fs, albedo->getPath());
		m_albedo_composite_path = albedo->getPath();
	}

	m_layers_mask = (primary ? 1 : 0) | (secondary ? 0b10 : 0);
	for (i32 i = 0; i < m_albedo_composite.layers.size(); ++i) {
		ImGui::SameLine();
		if (i == 0 || ImGui::GetContentRegionAvail().x < 50) ImGui::NewLine();
		bool b = m_textures_mask & ((u64)1 << i);
		m_app.getAssetBrowser().tile(m_albedo_composite.layers[i].red.path, b);
		if (ImGui::IsItemClicked()) {
			if (!ImGui::GetIO().KeyCtrl) m_textures_mask = 0;
			if (b) m_textures_mask &= ~((u64)1 << i);
			else m_textures_mask |= (u64)1 << i;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup(StaticString<8>("ctx", i));
		}

		if (ImGui::BeginPopup(StaticString<8>("ctx", i))) {
			if (ImGui::Selectable("Remove surface")) {
				compositeTextureRemoveLayer(albedo->getPath(), i);
				compositeTextureRemoveLayer(normal->getPath(), i);
				m_albedo_composite_path = "";
			}
			ImGui::EndPopup();
		}
	}
	if (albedo->layers < 255) {
		if (ImGui::Button(ICON_FA_PLUS "Add surface")) ImGui::OpenPopup("Add surface");
	}
	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::BeginPopupModal("Add surface")) {
		ImGuiEx::Label("Albedo");
		m_app.getAssetBrowser().resourceInput("albedo", Span(m_add_layer_popup.albedo), Texture::TYPE);
		ImGuiEx::Label("Normal");
		m_app.getAssetBrowser().resourceInput("normal", Span(m_add_layer_popup.normal), Texture::TYPE);
		if (ImGui::Button(ICON_FA_PLUS "Add")) {
			saveCompositeTexture(albedo->getPath(), m_add_layer_popup.albedo);
			saveCompositeTexture(normal->getPath(), m_add_layer_popup.normal);
			m_albedo_composite_path = "";
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TIMES "Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void TerrainEditor::compositeTextureRemoveLayer(const Path& path, i32 layer) {
	CompositeTexture texture(m_app.getAllocator());
	FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
	if (!texture.loadSync(fs, path)) {
		logError("Failed to load ", path);
	}
	else {
		texture.layers.erase(layer);
		if (!texture.save(fs, path)) {
			logError("Failed to save ", path);
		}
	}
}

void TerrainEditor::saveCompositeTexture(const Path& path, const char* channel)
{
	CompositeTexture texture(m_app.getAllocator());
	FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
	if (!texture.loadSync(fs, path)) {
		logError("Failed to load ", path);
	}
	else {
		CompositeTexture::Layer new_layer;
		new_layer.red.path = channel;
		new_layer.red.src_channel = 0;
		new_layer.green.path = channel;
		new_layer.green.src_channel = 1;
		new_layer.blue.path = channel;
		new_layer.blue.src_channel = 2;
		new_layer.alpha.path = channel;
		new_layer.alpha.src_channel = 3;
		texture.layers.push(new_layer);
		if (!texture.save(fs, path)) {
			logError("Failed to save ", path);
		}
	}
}

void TerrainEditor::entityGUI() {
	m_mode = Mode::ENTITY;
			
	ImGuiEx::Label("Ignore other folders (?)");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("When placing entities, ignore collisions with "
			"entities in other folders than the currently selected folder "
			"in hierarchy view");
	}
	ImGui::Checkbox("##ignore_filter", &m_ignore_entities_not_in_folder);

	static char filter[100] = {0};
	const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
	ImGui::SetNextItemWidth(-w);
	ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
		filter[0] = '\0';
	}

	static ImVec2 size(-1, 200);
	if (ImGui::ListBoxHeader("##prefabs", size)) {
		auto& resources = m_app.getAssetCompiler().lockResources();
		u32 count = 0;
		for (const AssetCompiler::ResourceItem& res : resources) {
			if (res.type != PrefabResource::TYPE) continue;
			++count;
			if (filter[0] != 0 && stristr(res.path.c_str(), filter) == nullptr) continue;
			int selected_idx = m_selected_prefabs.find([&](PrefabResource* r) -> bool {
				return r && r->getPath() == res.path;
			});
			bool selected = selected_idx >= 0;
			const char* loading_str = selected_idx >= 0 && m_selected_prefabs[selected_idx]->isEmpty() ? " - loading..." : "";
			StaticString<LUMIX_MAX_PATH + 15> label(res.path.c_str(), loading_str);
			if (ImGui::Checkbox(label, &selected)) {
				if (selected) {
					ResourceManagerHub& manager = m_world_editor.getEngine().getResourceManager();
					PrefabResource* prefab = manager.load<PrefabResource>(res.path);
					m_selected_prefabs.push(prefab);
				}
				else {
					PrefabResource* prefab = m_selected_prefabs[selected_idx];
					m_selected_prefabs.swapAndPop(selected_idx);
					prefab->decRefCount();
				}
			}
		}
		if (count == 0) ImGui::TextUnformatted("No prefabs");
		m_app.getAssetCompiler().unlockResources();
		ImGui::ListBoxFooter();
	}
	ImGuiEx::HSplitter("after_prefab", &size);

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


void TerrainEditor::exportToOBJ() {
	char filename[LUMIX_MAX_PATH];
	if (!os::getSaveFilename(Span(filename), "Wavefront obj\0*.obj", "obj")) return;

	os::OutputFile file;
	if (!file.open(filename)) {
		logError("Failed to open ", filename);
		return;
	}

	char basename[LUMIX_MAX_PATH];
	copyString(Span(basename), Path::getBasename(filename));

	auto* scene = static_cast<RenderScene*>(m_component.scene);
	const EntityRef e = (EntityRef)m_component.entity;
	const Texture* hm = getMaterial()->getTextureByName(HEIGHTMAP_SLOT_NAME);

	OutputMemoryStream blob(m_app.getAllocator());
	blob.reserve(8 * 1024 * 1024);
	blob << "mtllib " << basename << ".mtl\n";
	blob << "o Terrain\n";
	
	const float xz_scale = scene->getTerrainXZScale(e);
	const float y_scale = scene->getTerrainYScale(e);
	ASSERT(hm->format == gpu::TextureFormat::R16);
	const u16* hm_data = (const u16*)hm->getData();

	for (u32 j = 0; j < hm->height; ++j) {
		for (u32 i = 0; i < hm->width; ++i) {
			const float height = hm_data[i + j * hm->width] / float(0xffff) * y_scale;
			blob << "v " << i * xz_scale << " " << height << " " << j * xz_scale << "\n";
		}
	}

	for (u32 j = 0; j < hm->height; ++j) {
		for (u32 i = 0; i < hm->width; ++i) {
			blob << "vt " << i / float(hm->width - 1) << " " << j / float(hm->height - 1) << "\n";
		}
	}

	blob << "usemtl Material\n";

	auto write_face_vertex = [&](u32 idx){
		blob << idx << "/" << idx;
	};

	for (u32 j = 0; j < hm->height - 1; ++j) {
		for (u32 i = 0; i < hm->width - 1; ++i) {
			const u32 idx = i + j * hm->width + 1;
			blob << "f ";
			write_face_vertex(idx);
			blob << " ";
			write_face_vertex(idx + 1);
			blob << " ";
			write_face_vertex(idx + 1 + hm->width);
			blob << "\n";

			blob << "f ";
			write_face_vertex(idx);
			blob << " ";
			write_face_vertex(idx + 1 + hm->width);
			blob << " ";
			write_face_vertex(idx + hm->width);
			blob << "\n";
		}
	}

	if (!file.write(blob.data(), blob.size())) {
		logError("Failed to write ", filename);
	}

	file.close();

	char dir[LUMIX_MAX_PATH];
	copyString(Span(dir), Path::getDir(filename));
	StaticString<LUMIX_MAX_PATH> mtl_filename(dir, basename, ".mtl");

	if (!file.open(mtl_filename)) {
		logError("Failed to open ", mtl_filename);
		return;
	}

	blob.clear();
	blob << "newmtl Material";

	if (!file.write(blob.data(), blob.size())) {
		logError("Failed to write ", mtl_filename);
	}

	file.close();
}

void TerrainEditor::onGUI()
{
	auto* scene = static_cast<RenderScene*>(m_component.scene);
	ImGui::Unindent();
	if (!ImGui::CollapsingHeader("Terrain editor")) {
		ImGui::Indent();
		return;
	}
	ImGui::Indent();

	ImGuiEx::Label("Enable");
	ImGui::Checkbox("##ed_enabled", &m_is_enabled);
	if (!m_is_enabled) return;

	if (!getMaterial()) {
		ImGui::Text("No material");
		return;
	}

	if (!getMaterial()->getTextureByName(HEIGHTMAP_SLOT_NAME)) {
		ImGui::Text("No heightmap");
		return;
	}

	if (ImGui::Button(ICON_FA_FILE_EXPORT)) exportToOBJ();

	ImGuiEx::Label("Brush size");
	ImGui::DragFloat("##br_size", &m_terrain_brush_size, 1, MIN_BRUSH_SIZE, FLT_MAX);
	ImGuiEx::Label("Brush strength");
	ImGui::SliderFloat("##br_str", &m_terrain_brush_strength, 0, 1.0f);

	if (ImGui::BeginTabBar("brush_type")) {
		if (ImGui::BeginTabItem("Height")) {
			m_mode = Mode::HEIGHT;
			if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_SAVE, ImGui::GetStyle().Colors[ImGuiCol_Text], "Save")) 
			{
				getMaterial()->getTextureByName(HEIGHTMAP_SLOT_NAME)->save();
			}
			
			
			ImGuiEx::Label("Mode");
			if (m_is_flat_height) {
				ImGui::TextUnformatted("flat");
			}
			else if (m_smooth_terrain_action.isActive()) {
				char shortcut[64];
				if (m_smooth_terrain_action.shortcutText(Span(shortcut))) {
					ImGui::Text("smooth (%s)", shortcut);
				}
				else {
					ImGui::TextUnformatted("smooth");
				}
			}
			else if (m_lower_terrain_action.isActive()) {
				char shortcut[64];
				if (m_lower_terrain_action.shortcutText(Span(shortcut))) {
					ImGui::Text("lower (%s)", shortcut);
				}
				else {
					ImGui::TextUnformatted("lower");
				}
			}
			else {
				ImGui::TextUnformatted("raise");
			}
			
			ImGui::Checkbox("Flat", &m_is_flat_height);

			if (m_is_flat_height) {
				ImGui::SameLine();
				ImGui::Text("- Press Ctrl to pick height");
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Surface and grass")) {
			layerGUI();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Entity")) {
			entityGUI();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if (!m_component.isValid() || !m_is_enabled) {
		return;
	}

	const Vec2 mp = m_world_editor.getView().getMousePos();

	for(auto entity : m_world_editor.getSelectedEntities()) {
		if (!m_world_editor.getUniverse()->hasComponent(entity, TERRAIN_TYPE)) continue;
		
		RenderScene* scene = static_cast<RenderScene*>(m_component.scene);
		DVec3 origin;
		Vec3 dir;
		m_world_editor.getView().getViewport().getRay(mp, origin, dir);
		const RayCastModelHit hit = scene->castRayTerrain(entity, origin, dir);

		if(hit.is_hit) {
			DVec3 center = hit.origin + hit.dir * hit.t;
			drawCursor(*scene, entity, center);
			return;
		}
	}
}


void TerrainEditor::paint(const DVec3& hit_pos, ActionType action_type, bool old_stroke)
{
	UniquePtr<PaintTerrainCommand> command = UniquePtr<PaintTerrainCommand>::create(m_world_editor.getAllocator(),
		m_world_editor,
		action_type,
		m_grass_mask,
		(u64)m_textures_mask,
		hit_pos,
		m_brush_mask,
		m_terrain_brush_size,
		m_terrain_brush_strength,
		m_flat_height,
		m_color,
		m_component,
		m_layers_mask,
		m_fixed_value,
		old_stroke);
	m_world_editor.executeCommand(command.move());
}


} // namespace Lumix