#include <imgui/imgui.h>

#include "terrain_editor.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/entity_folders.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/core.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stack_array.h"
#include "engine/universe.h"
#include "physics/physics_scene.h"
#include "renderer/culling_system.h"
#include "renderer/draw_stream.h"
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


static const ComponentType INSTANCED_MODEL_TYPE = reflection::getComponentType("instanced_model");
static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");
static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");
static const ComponentType HEIGHTFIELD_TYPE = reflection::getComponentType("physical_heightfield");
static const char* HEIGHTMAP_SLOT_NAME = "Heightmap";
static const char* SPLATMAP_SLOT_NAME = "Splatmap";
static const char* DETAIL_ALBEDO_SLOT_NAME = "Detail albedo";
static const char* DETAIL_NORMAL_SLOT_NAME = "Detail normal";
static const float MIN_BRUSH_SIZE = 0.5f;

struct FillClearGrassCommand final : IEditorCommand {
	FillClearGrassCommand(u32 grass_idx, bool fill, EntityRef terrain, WorldEditor& editor)
		: m_world_editor(editor)
		, m_terrain(terrain)
		, m_grass_idx(grass_idx)
		, m_fill(fill)
		, m_old_data(editor.getAllocator())
	{}

	Texture* getDestinationTexture() const
	{
		RenderScene* scene = (RenderScene*)m_world_editor.getUniverse()->getScene(TERRAIN_TYPE);
		return scene->getTerrainMaterial(m_terrain)->getTextureByName(SPLATMAP_SLOT_NAME);
	}

	bool merge(IEditorCommand& command) override { return false; }
	
	bool execute() override {
		Texture* texture = getDestinationTexture();
		if (!texture) return false;

		const u32 Bpp = gpu::getBytesPerPixel(texture->format);
		if (4 != Bpp) return false;

		u16* data = (u16*)texture->getData();
		if (!data) return false;

		m_old_data.resize(texture->width * texture->height * 4);
		if (m_old_data.empty()) return false;

		memcpy(m_old_data.begin(), data, m_old_data.byte_size());

		u16 grass_mask = 1 << m_grass_idx;
		for (u32 j = 0; j < texture->height; ++j) {
			for (u32 i = 0; i < texture->width; ++i) {
				const u32 index = Bpp * (i + j * texture->width) + 2;
				if (m_fill)
					data[index / sizeof(data[0])] |= grass_mask;
				else 
					data[index / sizeof(data[0])] &= ~grass_mask;
			}
		}
		texture->onDataUpdated(0, 0, texture->width, texture->height);

		RenderScene* scene = (RenderScene*)m_world_editor.getUniverse()->getScene(TERRAIN_TYPE);
		scene->getTerrain(m_terrain)->setGrassDirty();

		return true;
	}

	void undo() override {
		ASSERT(!m_old_data.empty());
		
		Texture* texture = getDestinationTexture();
		ASSERT(texture);
		ASSERT(gpu::getBytesPerPixel(texture->format) == 4);
		ASSERT(texture->width * texture->height * 4 == m_old_data.byte_size());

		u8* data = texture->getData();
		ASSERT(data);

		memcpy(data, m_old_data.begin(), m_old_data.byte_size());
		texture->onDataUpdated(0, 0, texture->width, texture->height);

		RenderScene* scene = (RenderScene*)m_world_editor.getUniverse()->getScene(TERRAIN_TYPE);
		scene->getTerrain(m_terrain)->setGrassDirty();
	}
	const char* getType() override { return "fill_clear_grass"; }

	WorldEditor& m_world_editor;
	EntityRef m_terrain;
	u32 m_grass_idx;
	bool m_fill;

	Array<u8> m_old_data;
};

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
		EntityRef terrain,
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
		m_mask.resize(mask.size());
		for (int i = 0; i < mask.size(); ++i) {
			m_mask[i] = mask[i];
		}

		m_width = m_height = m_x = m_y = -1;
		Universe& universe = *editor.getUniverse();
		const Transform entity_transform = universe.getTransform(terrain).inverted();
		RenderScene* scene = (RenderScene*)universe.getScene(TERRAIN_TYPE);
		DVec3 local_pos = entity_transform.transform(hit_pos);
		float terrain_size = scene->getTerrainSize(terrain).x;
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

		RenderScene* scene = (RenderScene*)m_world_editor.getUniverse()->getScene(TERRAIN_TYPE);
		return scene->getTerrainMaterial(m_terrain)->getTextureByName(uniform_name);
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

		if (m_action_type != TerrainEditor::LAYER && m_action_type != TerrainEditor::REMOVE_GRASS)
		{
			IScene* scene = m_world_editor.getUniverse()->getScene("physics");
			if (!scene) return;

			auto* phy_scene = static_cast<PhysicsScene*>(scene);
			if (!scene->getUniverse().hasComponent(m_terrain, HEIGHTFIELD_TYPE)) return;

			phy_scene->updateHeighfieldData(m_terrain, m_x, m_y, m_width, m_height, &data[0], bpp);
		}
		else {
			RenderScene* scene = (RenderScene*)m_world_editor.getUniverse()->getScene(TERRAIN_TYPE);
			return scene->getTerrain(m_terrain)->setGrassDirty();
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
		Item& first_item = m_items[0];
		rect.from_x = maximum(int(s * (first_item.m_local_pos.x - first_item.m_radius) - 0.5f), 0);
		rect.from_y = maximum(int(s * (first_item.m_local_pos.z - first_item.m_radius) - 0.5f), 0);
		rect.to_x = minimum(1 + int(s * (first_item.m_local_pos.x + first_item.m_radius) + 0.5f), texture->width);
		rect.to_y = minimum(1 + int(s * (first_item.m_local_pos.z + first_item.m_radius) + 0.5f), texture->height);
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
	WorldEditor& m_world_editor;
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
	EntityRef m_terrain;
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

	if (m_brush_texture)
	{
		m_brush_texture->destroy();
		LUMIX_DELETE(m_app.getAllocator(), m_brush_texture);
	}
	m_app.removePlugin(*this);
	
	Engine& engine = m_app.getEngine();
	Lumix::IPlugin* plugin = engine.getPluginManager().getPlugin("renderer");
	Renderer& renderer = *static_cast<Renderer*>(plugin);
	DrawStream& stream = renderer.getDrawStream();
	for (gpu::TextureHandle t : m_layer_views) stream.destroy(t);
}


TerrainEditor::TerrainEditor(StudioApp& app)
	: m_app(app)
	, m_color(1, 1, 1)
	, m_current_brush(0)
	, m_selected_prefabs(app.getAllocator())
	, m_brush_mask(app.getAllocator())
	, m_brush_texture(nullptr)
	, m_flat_height(0)
	, m_is_enabled(false)
	, m_size_spread(1, 1)
	, m_y_spread(0, 0)
	, m_layer_views(app.getAllocator())
	, m_distance_fields(app.getAllocator())
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

	FileSystem& fs = app.getEngine().getFileSystem();
	StaticString<LUMIX_MAX_PATH> path(fs.getBasePath(), "/universes/distance_fields");
	os::FileIterator* iter = os::createFileIterator(path, app.getAllocator());
	os::FileInfo info;
	while (os::getNextFile(iter, &info)) {
		if (info.is_directory) continue;
		if (!Path::hasExtension(info.filename, "df")) continue;

		DistanceField df(app.getAllocator());
		df.name = Path::getBasename(info.filename);
		if (!df.load(fs, app.getAllocator())) {
			logError("Failed to load ", info.filename);
		}
		m_distance_fields.push(static_cast<DistanceField&&>(df));

	}
	os::destroyFileIterator(iter);
	registerLuaAPI();
}

DistanceField::DistanceField(IAllocator& allocator) 
	: data(allocator)
	, name(allocator)
{}

void DistanceField::resize(u32 w, u32 h) {
	width = w;
	height = h;
	data.resize(w * h);
	clear();
}

void DistanceField::clear() {
	for (float& f : data) f = FLT_MAX;
}

bool DistanceField::load(FileSystem& fs, IAllocator& allocator) {
	const StaticString<LUMIX_MAX_PATH> path(fs.getBasePath(), "universes/distance_fields/", name.c_str(), ".df");
	os::InputFile file;
	if (!file.open(path)) {
		logError("Failed to open ", path);
		return false;
	}
	OutputMemoryStream blob(allocator);
	blob.resize(file.size());
	if (!file.read(blob.getMutableData(), blob.size())) {
		logError("Failed to read ", path);
		file.close();
		return false;
	}
	file.close();
	InputMemoryStream input(blob);
	return load(input);
}

bool DistanceField::load(InputMemoryStream& blob) {
	Header header;
	blob.read(header);
	if (header.magic != Header::MAGIC) return false;
	if (header.version > 0) return false;
	name = blob.readString();
	width = header.width;
	height = header.height;
	data.resize(width * height);
	blob.read(data.begin(), data.byte_size());
	return blob.getPosition() == blob.size();
}

void DistanceField::save(FileSystem& fs, IAllocator& allocator) {
	StaticString<LUMIX_MAX_PATH> path(fs.getBasePath(), "universes/distance_fields/");
	if (!os::makePath(path)) {
		logError("Failed to create ", path);
	}
	path.add(name.c_str());
	path.add(".df");

	OutputMemoryStream blob(allocator);
	Header header;
	header.width = width;
	header.height = height;
	blob.write(header);
	blob.writeString(name.c_str());
	blob.write(data.begin(), data.byte_size());

	os::OutputFile file;
	if (file.open(path)) {
		if (!file.write(blob.data(), blob.size())) {
			logError("Failed to write ", path);
		}
		file.close();
	}
	else {
		logError("Failed to create ", path);
	}
}

static Vec2 abs(Vec2 v) { return {fabsf(v.x), fabsf(v.y)}; }
static float cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
static Vec2 pow(Vec2 a, Vec2 b) { return {powf(a.x, b.x), powf(a.y, b.y)}; }
static Vec2 sign(Vec2 v) { return {signum(v.x), signum(v.y)}; }

// from curve_decal.shd
static float bezierDistance(Vec2 pos, Vec2 A, Vec2 B, Vec2 C) {    
	Vec2 a = B - A;
	Vec2 b = A - B * 2.f + C;
	Vec2 c = a * 2.f;
	Vec2 d = A - pos;

	float kk = 1.f / dot(b, b);
	float kx = kk * dot(a, b);
	float ky = kk * (2.f * dot(a, a) + dot(d, b)) / 3.f;
	float kz = kk * dot(d, a);

	float res = 0.f;

	float p = ky - kx * kx;
	float p3 = p * p * p;
	float q = kx * (2.f * kx * kx - 3.f * ky) + kz;
	float h = q * q + 4.f * p3;

	if (h >= 0.f) { // 1 root
		h = sqrtf(h);
		Vec2 x = (Vec2(h, -h) - q) / 2.f;
		Vec2 uv = sign(x) * pow(abs(x), Vec2(1.f / 3.f));
		float t = clamp(uv.x + uv.y - kx, 0.f, 1.f);
		Vec2 qv2 = d + (c + b * t) * t;
		res = dot(qv2, qv2);
	}
	else { // 3 roots
		float z = sqrtf(-p);
		float v = acosf(q / (p * z * 2.f)) / 3.f;
		float m = cosf(v);
		float n = sinf(v) * 1.732050808f;
		Vec3 t = clamp(Vec3(m + m, -n - m, n - m) * z - Vec3(kx), Vec3(0), Vec3(1));
		Vec2 qx = d + (c + b * t.x) * t.x;
		float dx = dot(qx, qx), sx = cross2(c + b * 2.f * t.x, qx);
		Vec2 qy = d + (c + b * t.y) * t.y;
		float dy = dot(qy, qy), sy = cross2(c + b * 2.f * t.y, qy);
		if (dx < dy) {
			res = dx;
		} else {
			res = dy;
		}
	}
    
	return sqrtf(res);
}

void TerrainEditor::addSpline(const Terrain& terrain, DistanceField& df, const Spline& spline, EntityRef spline_entity) const {
	const Universe& universe = *m_app.getWorldEditor().getUniverse();
	const Transform spline_tr = universe.getTransform(spline_entity);
	const Transform terrain_tr = universe.getTransform(terrain.m_entity);
	const Transform spline_to_terrain = terrain_tr.inverted() * spline_tr;
	for (u32 pt_idx = 0, c = spline.points.size() - 2; pt_idx < c; ++pt_idx) {
		Vec3 p0 = spline.points[pt_idx];
		Vec3 p1 = spline.points[pt_idx + 1];
		Vec3 p2 = spline.points[pt_idx + 2];
			
		p0 = Vec3(spline_to_terrain.transform(p0));
		p1 = Vec3(spline_to_terrain.transform(p1));
		p2 = Vec3(spline_to_terrain.transform(p2));
			
		p0 = (p0 + p1) * 0.5f;
		p2 = (p1 + p2) * 0.5f;

		const Vec2 p0xz = p0.xz() / terrain.m_scale.xz();
		const Vec2 p1xz = p1.xz() / terrain.m_scale.xz();
		const Vec2 p2xz = p2.xz() / terrain.m_scale.xz();

		Vec2 min = minimum(minimum(p0xz, p1xz), p2xz);
		Vec2 max = maximum(maximum(p0xz, p1xz), p2xz);

		min = min - Vec2(30);
		max = max + Vec2(30);

		const Vec2 size((float)df.width, (float)df.height);
		min = clamp(min, Vec2(0), size);
		max = clamp(max, Vec2(0), size);

		for (u32 j = (u32)min.y, cj = (u32)max.y; j < cj; ++j) {
			for (u32 i = (u32)min.x, ci = (u32)max.x; i < ci; ++i) {
				float& f = df.data[i + j * df.width]; 
				f = minimum(f, bezierDistance(Vec2((float)i, (float)j), p0xz, p1xz, p2xz));
			}
		}
	}
}

struct TerrainTextureChangeCommand : IEditorCommand {
	TerrainTextureChangeCommand(WorldEditor& editor, Terrain& terrain, bool splatmap, IAllocator& allocator)
		: editor(editor)
		, before(allocator)
		, after(allocator)
		, entity(terrain.m_entity)
		, splatmap(splatmap)
	{
		Texture* texture = splatmap ? terrain.m_splatmap : terrain.m_heightmap;
		const u8* mask = texture->getData();
		const u32 size = texture->width * texture->height * (splatmap ? 4 : 2);
		before.resize(size);
		after.resize(size);
		memcpy(before.getMutableData(), mask, size);
		memcpy(after.getMutableData(), mask, size);
	}

	bool apply(OutputMemoryStream& blob) {
		RenderScene* render_scene = (RenderScene*)editor.getUniverse()->getScene(TERRAIN_TYPE);
		Terrain* terrain = render_scene->getTerrain(entity);
		if (!terrain) return false;
		Texture* texture = splatmap ? terrain->m_splatmap : terrain->m_heightmap;
		
		if (!texture) return false;
		if (!texture->isReady()) return false;
		
		u8* data = texture->getData();
		if (!data) return false;
		
		const u32 bytes_per_pixel = splatmap ? 4 : 2; 
		if (texture->width * texture->height * bytes_per_pixel != blob.size()) return false;

		memcpy(data, blob.data(), blob.size());
		texture->onDataUpdated(0, 0, texture->width, texture->height);
		terrain->setGrassDirty();
		return true;
	}

	bool execute() override { return apply(after); }
	void undo() override { apply(before); }
	const char* getType() override { return "terrain_texture_change"; }
	bool merge(IEditorCommand& command) override { return false; }

	WorldEditor& editor;
	EntityRef entity;
	OutputMemoryStream before;
	OutputMemoryStream after;
	bool splatmap;
};

DistanceField* TerrainEditor::findDistanceField(const char* name) const {
	for (DistanceField& df : m_distance_fields) {
		if (df.name == name) return &df;
	}
	return nullptr;
}

Terrain* TerrainEditor::getTerrain() const {
	WorldEditor& editor = m_app.getWorldEditor();
	const Array<EntityRef>& selected_entities = editor.getSelectedEntities();
	if (selected_entities.size() != 1) return nullptr;
	
	Universe& universe = *editor.getUniverse();
	bool is_terrain = universe.hasComponent(selected_entities[0], TERRAIN_TYPE);
	if (!is_terrain) return nullptr;

	RenderScene* scene = (RenderScene*)universe.getScene(TERRAIN_TYPE);
	return scene->getTerrain(selected_entities[0]);
}

struct PrefabProbability {
	PrefabResource* resource;
	Vec4 distances;
	Vec2 scale;
	Vec2 y_offset;
	float multiplier = 1.f;
};

struct ModelProbability {
	Model* resource;
	Vec4 distances;
	Vec2 scale;
	Vec2 y_offset;
	float multiplier = 1.f;
};

static void getPrefabs(StudioApp& app, lua_State* L, i32 idx, const char* key, Array<PrefabProbability>& prefabs) {
	const int type = LuaWrapper::getField(L, idx, key);
	if (type == LUA_TNIL) luaL_error(L, "missing `%s`", key);
	if (type != LUA_TTABLE) luaL_error(L, "`%s` is not a table", key);
		
	WorldEditor& editor = app.getWorldEditor();
	ResourceManagerHub& rm = editor.getEngine().getResourceManager();
	
	const i32 n = (int)lua_objlen(L, -1);
	for (i32 i = 0; i < n; ++i) {
		lua_rawgeti(L, -1, i + 1);
		if(lua_istable(L, -1)) {
			if (LuaWrapper::getField(L, -1, "prefab") != LUA_TSTRING) {
				lua_pop(L, 1);
				luaL_argerror(L, idx, "'prefab' is not string or is missing");
			}
			const char* prefab_path = LuaWrapper::toType<const char*>(L, -1);
			PrefabResource* res = rm.load<PrefabResource>(Path(prefab_path));
			lua_pop(L, 1);
			lua_getfield(L, -1, "distances");
			if (!LuaWrapper::isType<Vec4>(L, -1)) {
				lua_pop(L, 1);
				res->decRefCount();
				luaL_argerror(L, idx, "'distances' is not vec4 or is missing");
			}
			const Vec4 distances = LuaWrapper::toType<Vec4>(L, -1);
			lua_pop(L, 1);

			Vec2 scale = Vec2(1, 1);
			LuaWrapper::getOptionalField(L, -1, "scale", &scale);
			Vec2 y_offset = Vec2(0, 0);
			LuaWrapper::getOptionalField(L, -1, "y_offset", &y_offset);

			PrefabProbability& prob = prefabs.emplace();
			LuaWrapper::getOptionalField(L, -1, "multiplier", &prob.multiplier);
			prob.resource = res;
			prob.distances = distances;
			prob.scale = scale;
			prob.y_offset = y_offset;
		}
		else {
			lua_pop(L, 1);
			luaL_argerror(L, idx, "table of prefabs expected");
		}
		lua_pop(L, 1);
	}

	lua_pop(L, 1);
}

static void getModels(StudioApp& app, lua_State* L, i32 idx, const char* key, Array<ModelProbability>& prefabs) {
	const int type = LuaWrapper::getField(L, idx, key);
	if (type == LUA_TNIL) luaL_error(L, "missing `%s`", key);
	if (type != LUA_TTABLE) luaL_error(L, "`%s` is not a table", key);
		
	WorldEditor& editor = app.getWorldEditor();
	ResourceManagerHub& rm = editor.getEngine().getResourceManager();
	
	const i32 n = (int)lua_objlen(L, -1);
	for (i32 i = 0; i < n; ++i) {
		lua_rawgeti(L, -1, i + 1);
		if(lua_istable(L, -1)) {
			if (LuaWrapper::getField(L, -1, "model") != LUA_TSTRING) {
				lua_pop(L, 1);
				luaL_argerror(L, idx, "'model' is not string or is missing");
			}
			const char* prefab_path = LuaWrapper::toType<const char*>(L, -1);
			Model* res = rm.load<Model>(Path(prefab_path));
			lua_pop(L, 1);
			lua_getfield(L, -1, "distances");
			if (!LuaWrapper::isType<Vec4>(L, -1)) {
				lua_pop(L, 1);
				res->decRefCount();
				luaL_argerror(L, idx, "'distances' is not vec4 or is missing");
			}
			const Vec4 distances = LuaWrapper::toType<Vec4>(L, -1);
			lua_pop(L, 1);
			if (distances.x > distances.w) {
				res->decRefCount();
				luaL_argerror(L, idx, "'distances' are not sorted");
			}

			Vec2 scale = Vec2(1, 1);
			LuaWrapper::getOptionalField(L, -1, "scale", &scale);
			Vec2 y_offset = Vec2(0, 0);
			LuaWrapper::getOptionalField(L, -1, "y_offset", &y_offset);

			ModelProbability& prob = prefabs.emplace();
			LuaWrapper::getOptionalField(L, -1, "multiplier", &prob.multiplier);
			prob.resource = res;
			prob.distances = distances;
			prob.scale = scale;
			prob.y_offset = y_offset;
		}
		else {
			lua_pop(L, 1);
			luaL_argerror(L, idx, "table of models expected");
		}
		lua_pop(L, 1);
	}

	lua_pop(L, 1);
}

template <typename T>
static u32 getRandomItem(float distance, const Array<T>& probs) {
	float sum = 0;

	auto get = [](float distance, const T& prob){
		if (distance < prob.distances.x) return 0.f;
		if (distance > prob.distances.w) return 0.f;
			
		if (distance < prob.distances.y) {
			return prob.multiplier * (distance - prob.distances.x) / (prob.distances.y - prob.distances.x);
		}
		else if (distance < prob.distances.z) {
			return prob.multiplier;
		}
		return prob.multiplier * (1 - (distance - prob.distances.z) / (prob.distances.w - prob.distances.z));
	};

	for (const T& prob : probs) {
		sum += get(distance, prob);
	}
	if (sum == 0) return 0xffFFffFF;
		
	float r = randFloat() * sum;

	for (i32 i = 0; i < probs.size(); ++i) {
		const T& prob = probs[i];
		float p = get(distance, prob);
		if (r < p) return i;
		r -= p;
	}
		
	ASSERT(false);
	return 0xffFFffFF;
}


int TerrainEditor::placePrefabs(lua_State* L) {
	const int index = lua_upvalueindex(1);
	if (!LuaWrapper::isType<TerrainEditor>(L, index)) {
		ASSERT(false);
		return luaL_error(L, "Invalid Lua closure");
	}

	TerrainEditor* that = LuaWrapper::toType<TerrainEditor*>(L, index);
	
	float spacing;
	if (!LuaWrapper::checkField(L, 1, "spacing", &spacing)) {
		return luaL_error(L, "missing `spacing`");
	}

	char df_name[128];
	if (!LuaWrapper::checkStringField(L, 1, "distance_field", Span(df_name))) {
		return luaL_error(L, "missing `distance_field`");
	}

	DistanceField* df = that->findDistanceField(df_name);
	if (!df) return luaL_error(L, "`distance_field` %s not found", df_name);

	Terrain* terrain = that->getTerrain();
	if (!terrain) return luaL_error(L, "no terrain is selected");

	Array<PrefabProbability> prefabs(that->m_app.getAllocator());
	getPrefabs(that->m_app, L, 1, "prefabs", prefabs);
	if (prefabs.empty()) return 0;

	WorldEditor& editor = that->m_app.getWorldEditor();
	Universe* universe = editor.getUniverse();
	RenderScene* render_scene = (RenderScene*)universe->getScene(TERRAIN_TYPE);
	Array<Array<Transform>> transforms(that->m_app.getAllocator());
	for (const auto& prefab : prefabs) {
		transforms.emplace(that->m_app.getAllocator());
	}

	const double y_base = universe->getPosition(terrain->m_entity).y;
	const Vec2 size = render_scene->getTerrainSize(terrain->m_entity);
	const Vec2 df_to_terrain = size / Vec2((float)df->width, (float)df->height);
	const Vec2 terrain_to_df = Vec2((float)df->width, (float)df->height) / size;

	for (float y = 0; y < df->height; y += spacing * terrain_to_df.y) {
		for (float x = 0; x < df->width; x += spacing * terrain_to_df.x) {
			const i32 idx = i32(x) + i32(y) * df->width;
			const float distance = df->data[idx];
			if (distance > 0.01f) {
				float fx = (x + spacing * randFloat() * 0.9f - spacing * 0.45f);
				float fy = (y + spacing * randFloat() * 0.9f - spacing * 0.45f);
				fx = clamp(fx, 0.f, (float)df->width - 1);
				fy = clamp(fy, 0.f, (float)df->height - 1);

				DVec3 pos;
				pos.x = fx * df_to_terrain.x;
				pos.z = fy * df_to_terrain.y;
				pos.y = render_scene->getTerrainHeightAt(terrain->m_entity, (float)pos.x, (float)pos.z);

				pos.x -= size.x * 0.5f;
				pos.y += y_base;
				pos.z -= size.y * 0.5f;
					
				const u32 r = getRandomItem(distance, prefabs);
				if (r != 0xffFFffFF) {
					const Vec2& yoffset_range = prefabs[r].y_offset;
					pos.y += lerp(yoffset_range.x, yoffset_range.y, randFloat());

					const Vec2& scale_range = prefabs[r].scale;
					const float scale = lerp(scale_range.x, scale_range.y, randFloat());
					transforms[r].push({pos, Quat(Vec3(0, 1, 0), randFloat() * 2 * PI), scale});
				}
			}
		}
	}

	FileSystem& fs = editor.getEngine().getFileSystem();
	while (fs.hasWork()) {
		os::sleep(10);
		fs.processCallbacks();
	}

	editor.beginCommandGroup("maps_place_prefabs");
	for (u32 i = 0; i < (u32)prefabs.size(); ++i) {
		if (!transforms[i].empty()) editor.getPrefabSystem().instantiatePrefabs(*prefabs[i].resource, transforms[i]);
		prefabs[i].resource->decRefCount();
	}
	editor.endCommandGroup();
	return 0;
}

int TerrainEditor::placeInstances(lua_State* L) {
	const int index = lua_upvalueindex(1);
	if (!LuaWrapper::isType<TerrainEditor>(L, index)) {
		ASSERT(false);
		return luaL_error(L, "Invalid Lua closure");
	}

	TerrainEditor* that = LuaWrapper::toType<TerrainEditor*>(L, index);
	
	float spacing;
	if (!LuaWrapper::checkField(L, 1, "spacing", &spacing)) {
		return luaL_error(L, "missing `spacing`");
	}

	char df_name[128];
	if (!LuaWrapper::checkStringField(L, 1, "distance_field", Span(df_name))) {
		return luaL_error(L, "missing `distance_field`");
	}

	DistanceField* df = that->findDistanceField(df_name);
	if (!df) return luaL_error(L, "`distance_field` %s not found", df_name);

	Terrain* terrain = that->getTerrain();
	if (!terrain) return luaL_error(L, "no terrain is selected");

	Array<ModelProbability> models(that->m_app.getAllocator());
	getModels(that->m_app, L, 1, "models", models);
	if (models.empty()) return 0;

	WorldEditor& editor = that->m_app.getWorldEditor();
	Universe* universe = editor.getUniverse();
	RenderScene* render_scene = (RenderScene*)universe->getScene(TERRAIN_TYPE);
	const DVec3 terrain_pos = universe->getPosition(terrain->m_entity);

	struct Group {
		EntityRef e;
		InstancedModel* im;
	};

	StackArray<Group, 64> groups(that->m_app.getAllocator());
	editor.beginCommandGroup("maps_place_instances");
	for (const ModelProbability& m : models) {
		const EntityRef e = editor.addEntity();
		groups.emplace().e = e;
		editor.makeParent(terrain->m_entity, e);
		editor.addComponent(Span(&e, 1), INSTANCED_MODEL_TYPE);
		editor.setProperty(INSTANCED_MODEL_TYPE, "", 0, "Model", Span(&e, 1), m.resource->getPath());
		editor.setEntitiesPositions(&e, &terrain_pos, 1);
	}

	for (Group& g : groups) {
		g.im = &render_scene->beginInstancedModelEditing(g.e);
	}

	const double y_base = terrain_pos.y;
	const Vec2 size = render_scene->getTerrainSize(terrain->m_entity);
	const Vec2 df_to_terrain = size / Vec2((float)df->width, (float)df->height);
	const Vec2 terrain_to_df = Vec2((float)df->width, (float)df->height) / size;

	float min_dist = FLT_MAX;
	float max_dist = -FLT_MAX;

	for (const ModelProbability& prob : models) {
		min_dist = minimum(min_dist, prob.distances.x);
		max_dist = maximum(max_dist, prob.distances.w);
	}

	for (float y = 0; y < df->height; y += spacing * terrain_to_df.y) {
		for (float x = 0; x < df->width; x += spacing * terrain_to_df.x) {
			float fx = x;
			float fy = y;
			fx = clamp(fx + (randFloat() * 0.9f - 0.45f) * spacing * terrain_to_df.x, 0.f, (float)df->width - 1);
			fy = clamp(fy + (randFloat() * 0.9f - 0.45f) * spacing * terrain_to_df.y, 0.f, (float)df->height - 1);

			DVec3 pos;
			pos.x = fx * df_to_terrain.x;
			pos.z = fy * df_to_terrain.y;

			const i32 idx = i32(fx) + i32(fy) * df->width;
			const float distance = df->data[idx];
			if (distance >= min_dist && distance <= max_dist) {
				pos.y = render_scene->getTerrainHeightAt(terrain->m_entity, (float)pos.x, (float)pos.z);

				pos.x -= size.x * 0.5f;
				pos.y += y_base;
				pos.z -= size.y * 0.5f;
					
				const u32 r = getRandomItem(distance, models);
				if (r != 0xffFFffFF) {
					const Vec2& yoffset_range = models[r].y_offset;
					pos.y += lerp(yoffset_range.x, yoffset_range.y, randFloat());

					const Vec2& scale_range = models[r].scale;
					const float scale = lerp(scale_range.x, scale_range.y, randFloat());
					
					InstancedModel::InstanceData& id = groups[r].im->instances.emplace();
					const Quat rot(Vec3(0, 1, 0), randFloat() * 2 * PI);
					id.pos = Vec3(pos - terrain_pos);
					id.scale = scale;
					id.rot_quat = rot.w < 0 ? -Vec3(rot.x, rot.y, rot.z) : Vec3(rot.x, rot.y, rot.z);
					id.lod = 3;
				}
			}
		}
	}

	for (Group& g : groups) {
		render_scene->endInstancedModelEditing(g.e);
	}

	editor.endCommandGroup();
	return 0;
}

void TerrainEditor::registerLuaAPI() {
	lua_State* L = m_app.getEngine().getState();
	LuaWrapper::createSystemClosure(L, "TerrainEditor", this, "placeInstances", placeInstances);
	LuaWrapper::createSystemClosure(L, "TerrainEditor", this, "placePrefabs", placePrefabs);
}

void TerrainEditor::exportDistanceField(const DistanceField& df) const {
	OutputMemoryStream blob(m_app.getAllocator());
	
	char filename[LUMIX_MAX_PATH];
	if (!os::getSaveFilename(Span(filename), "Targa TGA\0*.tga\0", "tga")) return;

	const Path path(filename);
	Array<u32> data(m_app.getAllocator());
	data.resize(df.width * df.height);

	for (u32 j = 0; j < df.height; ++j) {
		for (u32 i = 0; i < df.width; ++i) {
			const float dist = df.data[i + j * df.width];
			u8 d8 = u8(clamp(dist, 0.f, 255.f) + 0.5f);
			data[i + j * df.width] = d8 | (d8 << 8) | (d8 << 16) | (d8 << 24);
		}
	}

	bool saved = Texture::saveTGA(&blob, df.width, df.height, gpu::TextureFormat::RGBA8, (const u8*)data.begin(), true, path, m_app.getAllocator());
	if (!saved) {
		logError("Failed to save ", path);
		return;
	}

	os::OutputFile file;
	if (!file.open(filename)) {
		logError("Failed to open ", filename);
		return;
	}

	if (!file.write(blob.data(), blob.size())) {
		logError("Failed to write ", filename, " properly, it's corrupted.");
	}
	file.close();
}

void TerrainEditor::distanceFieldsUI(ComponentUID terrain_uid) {
	for (DistanceField& df : m_distance_fields) {
		if (ImGui::TreeNode(df.name.c_str(), "%s %d x %d", df.name.c_str(), df.width, df.height)) {
			RenderScene* render_scene = (RenderScene*)terrain_uid.scene;
			Terrain* terrain = render_scene->getTerrain(*terrain_uid.entity);
				
			static float dig_depth = -1.f;
			bool can_edit = terrain->m_heightmap;
			can_edit = can_edit && terrain->m_heightmap->isReady(); 
			can_edit = can_edit && terrain->m_heightmap->format == gpu::TextureFormat::R16; 
			can_edit = can_edit && terrain->m_splatmap->isReady(); 
			can_edit = can_edit && terrain->m_splatmap->format == gpu::TextureFormat::RGBA8; 
			
			if (can_edit) {
				ASSERT(df.width == terrain->m_width);
				ASSERT(df.height == terrain->m_height);
				WorldEditor& editor = m_app.getWorldEditor();
				IAllocator& allocator = editor.getAllocator();

				if (ImGui::Button("Paint surface and grass")) {
					UniquePtr<TerrainTextureChangeCommand> cmd = UniquePtr<TerrainTextureChangeCommand>::create(allocator, editor, *terrain, true, allocator);
					u16* after = (u16*)cmd->after.getMutableData();

					u8 tex[64];
					u32 tex_count = 0;
					for (u64 i = 0; i < 64; ++i) {
						if (m_textures_mask & (u64(1) << i)) {
							tex[tex_count] = u8(i);
							++tex_count;
						}
					}

					for (u32 j = 0; j < (u32)terrain->m_height; ++j) {
						for (u32 i = 0; i < (u32)terrain->m_width; ++i) {
							const u32 idx = i + j * terrain->m_width;
							if (df.data[idx] < m_terrain_brush_size / terrain->m_scale.x) {
								after[idx * 2 + 1] |= m_grass_mask;
								if (tex_count > 0) after[idx * 2] = tex[rand() % tex_count];
							}
						}
					}

					editor.executeCommand(cmd.move());
				}

				ImGui::SameLine();
				if (ImGui::Button("Clear grass")) {
					UniquePtr<TerrainTextureChangeCommand> cmd = UniquePtr<TerrainTextureChangeCommand>::create(allocator, editor, *terrain, true, allocator);
					u16* after = (u16*)(cmd->after.getMutableData() + 2);

					for (u32 j = 0; j < (u32)terrain->m_height; ++j) {
						for (u32 i = 0; i < (u32)terrain->m_width; ++i) {
							const u32 idx = i + j * terrain->m_width;
							if (df.data[idx] < m_terrain_brush_size / terrain->m_scale.x) {
								after[idx * 2] = 0;
							}
						}
					}

					editor.executeCommand(cmd.move());
				}

				if (ImGui::Button("Dig")) {
					UniquePtr<TerrainTextureChangeCommand> cmd = UniquePtr<TerrainTextureChangeCommand>::create(allocator, editor, *terrain, false, allocator);
					u16* after = (u16*)cmd->after.getMutableData();

					const float dig_amount = 65535.f * dig_depth / terrain->m_scale.y;
					const float R = m_terrain_brush_size / terrain->m_scale.x;

					for (u32 j = 0; j < (u32)terrain->m_height; ++j) {
						for (u32 i = 0; i < (u32)terrain->m_width; ++i) {
							const u32 idx = i + j * terrain->m_width;
							if (df.data[idx] < R) {
								const float t = clamp(df.data[idx] / R, 0.f, 1.f);
								after[idx] += i32(dig_amount * (1 - t * t) + 0.5f);
							}
						}
					}
					editor.executeCommand(cmd.move());
				}
			}
			ImGui::SameLine();
			ImGuiEx::Label("Dig depth");
			ImGui::DragFloat("##depth", &dig_depth);

			if (ImGuiEx::IconButton(ICON_FA_REDO, "Reload")) df.load(m_app.getEngine().getFileSystem(), m_app.getAllocator());
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) df.save(m_app.getEngine().getFileSystem(), m_app.getAllocator());
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_BRUSH, "Clear")) df.clear();
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_PLUS, "Add spline")) {
				ImGui::OpenPopup("add_spline");
			}
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_FILE_EXPORT, "Export as texture")) exportDistanceField(df);

			if (ImGui::BeginPopup("add_spline")) {
				CoreScene* core = (CoreScene*)render_scene->getUniverse().getScene(SPLINE_TYPE);
				
				const HashMap<EntityRef, Spline>& splines = core->getSplines();
				static EntityPtr selected = INVALID_ENTITY;
				char buf[64];
				getEntityListDisplayName(m_app, *m_app.getWorldEditor().getUniverse(), Span(buf), selected);
				if (ImGui::BeginCombo("##spline_ent", buf)) {
					for (auto iter = splines.begin(), end = splines.end(); iter != end; ++iter) {
						getEntityListDisplayName(m_app, *m_app.getWorldEditor().getUniverse(), Span(buf), iter.key());
						if (ImGui::Selectable(buf)) selected = iter.key();
					}
					ImGui::EndCombo();
				}
				
				if (selected.isValid()) {
					auto iter = splines.find(*selected);
					if (iter.isValid() && ImGui::Selectable(ICON_FA_CHECK " Add")) {
						addSpline(*terrain, df, iter.value(), *selected);
					}
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Delete")) {
				StaticString<LUMIX_MAX_PATH> path("universes/distance_fields/", df.name.c_str(), ".df");
				if (!m_app.getEngine().getFileSystem().deleteFile(path)) {
					logError("Failed to delete ", path);
				}
				m_distance_fields.erase(u32(&df - m_distance_fields.begin()));
				ImGui::TreePop();
				break;
			}
			ImGui::TreePop();
		}
	}
	if (ImGui::Button(ICON_FA_PLUS " Add")) {
		ImGui::OpenPopup("Add distance field");
	}

	if (ImGui::BeginPopup("Add distance field")) {
		static char name[64] = "";
		static i32 w = 4096, h = 4096;
		ImGuiEx::Label("Name");
		ImGui::InputText("##name", name, sizeof(name));
		ImGuiEx::Label("Width");
		ImGui::DragInt("##w", &w);
		ImGuiEx::Label("Height");
		ImGui::DragInt("##h", &h);

		if (ImGui::Selectable(ICON_FA_CHECK " Add")) {
			DistanceField& df = m_distance_fields.emplace(m_app.getAllocator());
			df.name = name;
			df.resize(w, h);
		}
		
		ImGui::Dummy(ImVec2(150, 1));
		ImGui::EndPopup();
	}
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


void TerrainEditor::drawCursor(RenderScene& scene, EntityRef entity, const DVec3& center) const
{
	PROFILE_FUNCTION();
	Terrain* terrain = scene.getTerrain(entity);
	constexpr int SLICE_COUNT = 30;
	constexpr float angle_step = PI * 2 / SLICE_COUNT;
	if (m_mode == Mode::HEIGHT && m_is_flat_height && ImGui::GetIO().KeyCtrl) {
		scene.addDebugCross(center, 1.0f, 0xff0000ff);
		return;
	}

	float brush_size = m_terrain_brush_size;
	const Vec3 local_center = Vec3(getRelativePosition(center, entity, scene.getUniverse()));
	const Transform terrain_transform = scene.getUniverse().getTransform(entity);

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
		scene.addDebugLine(from, to, Color::RED);
	}

	const Vec3 rel_pos = Vec3(terrain_transform.inverted().transform(center));
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

			scene.addDebugLine(p10, p01, Color(0x80, 0, 0, 0xff));
			scene.addDebugLine(p10, p11, Color(0x80, 0, 0, 0xff));
			scene.addDebugLine(p00, p10, Color(0x80, 0, 0, 0xff));
			scene.addDebugLine(p01, p11, Color(0x80, 0, 0, 0xff));
			scene.addDebugLine(p00, p01, Color(0x80, 0, 0, 0xff));
		}
	}
}


DVec3 TerrainEditor::getRelativePosition(const DVec3& world_pos, EntityRef terrain, Universe& universe) const
{
	const Transform transform = universe.getTransform(terrain);
	const Transform inv_transform = transform.inverted();

	return inv_transform.transform(world_pos);
}


u16 TerrainEditor::getHeight(const DVec3& world_pos, RenderScene* scene, EntityRef terrain) const
{
	const DVec3 rel_pos = getRelativePosition(world_pos, terrain, scene->getUniverse());
	ComponentUID cmp;
	cmp.entity = terrain;
	cmp.scene = scene;
	cmp.type = TERRAIN_TYPE;
	Texture* heightmap = getMaterial(cmp)->getTextureByName(HEIGHTMAP_SLOT_NAME);
	if (!heightmap) return 0;

	u16* data = (u16*)heightmap->getData();
	float scale = scene->getTerrainXZScale(terrain);
	return data[int(rel_pos.x / scale) + int(rel_pos.z / scale) * heightmap->width];
}

void TerrainEditor::onMouseWheel(float value) {
	m_terrain_brush_size = maximum(0.f, m_terrain_brush_size + value);
}

bool TerrainEditor::onMouseDown(UniverseView& view, int x, int y)
{
	if (!m_is_enabled) return false;

	WorldEditor& editor = view.getEditor();
	const Array<EntityRef>& selected_entities = editor.getSelectedEntities();
	if (selected_entities.size() != 1) return false;
	
	Universe& universe = *editor.getUniverse();
	bool is_terrain = universe.hasComponent(selected_entities[0], TERRAIN_TYPE);
	if (!is_terrain) return false;

	RenderScene* scene = (RenderScene*)universe.getScene(TERRAIN_TYPE);
	DVec3 origin;
	Vec3 dir;
	view.getViewport().getRay({(float)x, (float)y}, origin, dir);
	const RayCastModelHit hit = scene->castRayTerrain(origin, dir);
	if (!hit.is_hit) return false;

	const DVec3 hit_pos = hit.origin + hit.dir * hit.t;
	switch(m_mode) {
		case Mode::ENTITY:
			if (m_remove_entity_action.isActive()) {
				removeEntities(hit_pos, editor);
			}
			else {
				paintEntities(hit_pos, editor, selected_entities[0]);
			}
			break;
		case Mode::HEIGHT:
			if (m_is_flat_height) {
				if (ImGui::GetIO().KeyCtrl) {
					m_flat_height = getHeight(hit_pos, scene, selected_entities[0]);
				}
				else {
					paint(hit_pos, TerrainEditor::FLAT_HEIGHT, false, selected_entities[0], editor);
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
				paint(hit_pos, action, false, selected_entities[0], editor); break;
			}
			break;
		case Mode::LAYER:
			TerrainEditor::ActionType action = TerrainEditor::LAYER;
			if (m_remove_grass_action.isActive()) {
				action = TerrainEditor::REMOVE_GRASS;
			}
			paint(hit_pos, action, false, selected_entities[0], editor);
			break;
	}
	return true;
}

void TerrainEditor::removeEntities(const DVec3& hit_pos, WorldEditor& editor) const
{
	if (m_selected_prefabs.empty()) return;
	PrefabSystem& prefab_system = editor.getPrefabSystem();

	PROFILE_FUNCTION();

	Universe& universe = *editor.getUniverse();
	RenderScene* scene = static_cast<RenderScene*>(universe.getScene(TERRAIN_TYPE));
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
		meshes->merge(scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE));
	}
	else {
		meshes = scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE);
	}
	if(!meshes) return;

	editor.beginCommandGroup("remove_entities");
	if (m_selected_prefabs.empty())
	{
		meshes->forEach([&](EntityRef entity){
			if (prefab_system.getPrefab(entity).getHashValue() == 0) return; 
			
			const Model* model = scene->getModelInstanceModel(entity);
			const AABB entity_aabb = model ? model->getAABB() : AABB(Vec3::ZERO, Vec3::ZERO);
			const bool collide = testOBBCollision(brush_aabb, universe.getRelativeMatrix(entity, hit_pos), entity_aabb);
			if (collide) editor.destroyEntities(&entity, 1);
		});
	}
	else
	{
		meshes->forEach([&](EntityRef entity){
			for (auto* res : m_selected_prefabs)
			{
				if (prefab_system.getPrefab(entity) == res->getPath().getHash())
				{
					const Model* model = scene->getModelInstanceModel(entity);
					const AABB entity_aabb = model ? model->getAABB() : AABB(Vec3::ZERO, Vec3::ZERO);
					const bool collide = testOBBCollision(brush_aabb, universe.getRelativeMatrix(entity, hit_pos), entity_aabb);
					if (collide) editor.destroyEntities(&entity, 1);
				}
			}
		});
	}
	editor.endCommandGroup();
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


void TerrainEditor::paintEntities(const DVec3& hit_pos, WorldEditor& editor, EntityRef terrain) const
{
	PROFILE_FUNCTION();
	if (m_selected_prefabs.empty()) return;
	if (!areAllReady(m_selected_prefabs)) return;

	auto& prefab_system = editor.getPrefabSystem();

	editor.beginCommandGroup("paint_entities");
	{
		Universe& universe = *editor.getUniverse();
		RenderScene* scene = static_cast<RenderScene*>(universe.getScene(TERRAIN_TYPE));
		const Transform terrain_tr = universe.getTransform(terrain);
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
		if (meshes) meshes->merge(scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE));
		else meshes = scene->getRenderables(frustum, RenderableTypes::MESH_MATERIAL_OVERRIDE);

		const EntityFolders& folders = editor.getEntityFolders();
		const EntityFolders::FolderID folder = folders.getSelectedFolder();

		Vec2 terrain_size = scene->getTerrainSize(terrain);
		float scale = 1.0f - maximum(0.01f, m_terrain_brush_strength);
		for (int i = 0; i <= m_terrain_brush_size * m_terrain_brush_size / 100.0f * m_terrain_brush_strength; ++i)
		{
			const float angle = randFloat(0, PI * 2);
			const float dist = randFloat(0, 1.0f) * m_terrain_brush_size;
			const float y = randFloat(m_y_spread.x, m_y_spread.y);
			DVec3 pos(hit_pos.x + cosf(angle) * dist, 0, hit_pos.z + sinf(angle) * dist);
			const Vec3 terrain_pos = Vec3(inv_terrain_tr.transform(pos));
			if (terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= terrain_size.x && terrain_pos.z <= terrain_size.y)
			{
				pos.y = scene->getTerrainHeightAt(terrain, terrain_pos.x, terrain_pos.z) + y;
				pos.y += terrain_tr.pos.y;
				Quat rot(0, 0, 0, 1);
				if(m_is_align_with_normal)
				{
					Vec3 normal = scene->getTerrainNormalAt(terrain, terrain_pos.x, terrain_pos.z);
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
						float xangle = randFloat(m_rotate_x_spread.x, m_rotate_x_spread.y);
						Quat q(Vec3(1, 0, 0), xangle);
						rot = q * rot;
					}

					if (m_is_rotate_y)
					{
						float yangle = randFloat(m_rotate_y_spread.x, m_rotate_y_spread.y);
						Quat q(Vec3(0, 1, 0), yangle);
						rot = q * rot;
					}

					if (m_is_rotate_z)
					{
						float zangle = randFloat(m_rotate_z_spread.x, m_rotate_z_spread.y);
						Quat q(rot.rotate(Vec3(0, 0, 1)), zangle);
						rot = q * rot;
					}
				}

				float size = randFloat(m_size_spread.x, m_size_spread.y);
				int random_idx = rand(0, m_selected_prefabs.size() - 1);
				if (!m_selected_prefabs[random_idx]) continue;
				const EntityPtr entity = prefab_system.instantiatePrefab(*m_selected_prefabs[random_idx], pos, rot, size);
				if (entity.isValid()) {
					if (universe.hasComponent((EntityRef)entity, MODEL_INSTANCE_TYPE)) {
						Model* model = scene->getModelInstanceModel((EntityRef)entity);
						const Transform tr = { pos, rot, size * scale };
						if (isOBBCollision(*scene, meshes, tr, model, m_ignore_entities_not_in_folder, folders, folder)) {
							editor.undo();
						}
					}
				}
			}
		}
		meshes->free(m_app.getEngine().getPageAllocator());
	}
	editor.endCommandGroup();
}


void TerrainEditor::onMouseMove(UniverseView& view, int x, int y, int, int)
{
	if (!m_is_enabled) return;

	WorldEditor& editor = view.getEditor();
	const Array<EntityRef>& selected_entities = editor.getSelectedEntities();
	if (selected_entities.size() != 1) return;

	const EntityRef entity = selected_entities[0];
	Universe& universe = *editor.getUniverse();
	if (!universe.hasComponent(entity, TERRAIN_TYPE)) return;
	RenderScene* scene = (RenderScene*)universe.getScene(TERRAIN_TYPE);
	DVec3 origin;
	Vec3 dir;
	view.getViewport().getRay({(float)x, (float)y}, origin, dir);

	const RayCastModelHit hit = scene->castRayTerrain(origin, dir);
	if (!hit.is_hit) return;
	if (hit.entity != entity) return;
	
	const DVec3 hit_pos = hit.origin + hit.dir * hit.t;

	switch(m_mode) {
		case Mode::ENTITY:
			if (m_remove_entity_action.isActive()) {
				removeEntities(hit_pos, editor);
			}
			else {
				paintEntities(hit_pos, editor, entity);
			}
			break;
		case Mode::HEIGHT:
			if (m_is_flat_height) {
				if (ImGui::GetIO().KeyCtrl) {
					m_flat_height = getHeight(hit_pos, scene, entity);
				}
				else {
					paint(hit_pos, TerrainEditor::FLAT_HEIGHT, true, selected_entities[0], editor);
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
				paint(hit_pos, action, true, selected_entities[0], editor); break;
			}
			break;
		case Mode::LAYER:
			TerrainEditor::ActionType action = TerrainEditor::LAYER;
			if (m_remove_grass_action.isActive()) {
				action = TerrainEditor::REMOVE_GRASS;
			}
			paint(hit_pos, action, true, selected_entities[0], editor);
			break;
	}
}


Material* TerrainEditor::getMaterial(ComponentUID cmp) const
{
	if (!cmp.isValid()) return nullptr;
	auto* scene = static_cast<RenderScene*>(cmp.scene);
	return scene->getTerrainMaterial((EntityRef)cmp.entity);
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

void TerrainEditor::exportGrass(u32 idx, EntityRef terrain, WorldEditor& editor) {
	OutputMemoryStream blob(editor.getAllocator());
	RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(TERRAIN_TYPE);
	Texture* texture = scene->getTerrainMaterial(terrain)->getTextureByName(SPLATMAP_SLOT_NAME);
	if (!texture) return;
	
	ASSERT(texture->format == gpu::TextureFormat::RGBA8);
	const u8* src = texture->getData();
	ASSERT(src);

	char filename[LUMIX_MAX_PATH];
	if (!os::getSaveFilename(Span(filename), "Targa TGA\0*.tga\0", "tga")) return;
	
	const Path path(filename);
	Array<u32> data(editor.getAllocator());
	data.resize(texture->width * texture->height);

	for (u32 j = 0; j < texture->height; ++j) {
		for (u32 i = 0; i < texture->width; ++i) {
			const u16 grass_mask = *(const u16*)(&src[(i + j * texture->width) * 4 + 2]);
			const bool masked = grass_mask & (1 << idx);
			data[i + j * texture->width] = masked ? 0xffFFffFF : 0;
		}
	}

	bool saved = Texture::saveTGA(&blob, texture->width, texture->height, gpu::TextureFormat::RGBA8, (const u8*)data.begin(), true, path, editor.getAllocator());
	if (!saved) {
		logError("Failed to save ", path);
		return;
	}

	os::OutputFile file;
	if (!file.open(filename)) {
		logError("Failed to open ", filename);
		return;
	}

	if (!file.write(blob.data(), blob.size())) {
		logError("Failed to write ", filename, " properly, it's corrupted.");
	}
	file.close();
}

void TerrainEditor::importGrass(u32 idx, EntityRef terrain, WorldEditor& editor) {
	RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(TERRAIN_TYPE);
	Texture* texture = scene->getTerrainMaterial(terrain)->getTextureByName(SPLATMAP_SLOT_NAME);
	if (!texture) return;

	u8* dst = texture->getData();
	ASSERT(dst);

	char filename[LUMIX_MAX_PATH];
	if (!os::getOpenFilename(Span(filename), "Targa TGA\0*.tga\0", nullptr)) return;

	Array<u8> src = getFileContent(filename, editor.getAllocator());
	TGAHeader header;
	if (src.size() < sizeof(header)) {
		logError("Invalid TGA ", filename);
		return;
	}

	memcpy(&header, src.begin(), sizeof(header));
	if (header.dataType != 2 && header.bitsPerPixel != 32) {
		logError("Unsupported TGA ", filename);
		return;
	}

	if (texture->width != header.width || texture->height != header.height) {
		logError("Size of ", filename, " does not match terrain's size");
		return;
	}

	const u32* data = (const u32*)(src.begin() + sizeof(header));
	for (u32 j = 0; j < texture->height; ++j) {
		const u32 dst_j = header.imageDescriptor & 32 ? j : texture->height - j - 1; 
		for (u32 i = 0; i < texture->width; ++i) {
			u16& grass_mask = *(u16*)(&dst[(i + dst_j * texture->width) * 4 + 2]);
			const bool masked = data[i + j * texture->width] != 0;
			if (masked) grass_mask |= 1 << idx;
			else grass_mask &= ~(1 << idx);
		}
	}

	texture->onDataUpdated(0, 0, texture->width, texture->height);
}

void TerrainEditor::fillGrass(u32 idx, EntityRef terrain, WorldEditor& editor) {
	UniquePtr<FillClearGrassCommand> command = UniquePtr<FillClearGrassCommand>::create(editor.getAllocator(),
		idx,
		true,
		terrain,
		editor);
	editor.executeCommand(command.move());
}

void TerrainEditor::clearGrass(u32 idx, EntityRef terrain, WorldEditor& editor) {
	UniquePtr<FillClearGrassCommand> command = UniquePtr<FillClearGrassCommand>::create(editor.getAllocator(),
		idx,
		false,
		terrain,
		editor);
	editor.executeCommand(command.move());
}

static void thumbnail(gpu::TextureHandle texture, float size, bool selected) {
	ImVec2 img_size(size, size);
	ImGui::Image(texture, img_size);
	if (selected) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const u32 color = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
		dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color, 0, 0, 3.f);
	}
}

Renderer& TerrainEditor::getRenderer() {
	Engine& engine = m_app.getEngine();
	Lumix::IPlugin* plugin = engine.getPluginManager().getPlugin("renderer");
	return *static_cast<Renderer*>(plugin);
}


void TerrainEditor::layerGUI(ComponentUID cmp) {
	m_mode = Mode::LAYER;
	RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
	Material* material = scene->getTerrainMaterial((EntityRef)cmp.entity);
	if (!material) return;
	if (material->getTextureByName(SPLATMAP_SLOT_NAME) && ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_SAVE, ImGui::GetStyle().Colors[ImGuiCol_Text], "Save"))
	{
		material->getTextureByName(SPLATMAP_SLOT_NAME)->save();
	}

	if (m_brush_texture)
	{
		ImGui::Image(m_brush_texture->handle, ImVec2(100, 100));
		if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_TIMES, ImGui::GetStyle().Colors[ImGuiCol_Text], "Clear brush mask"))
		{
			m_brush_texture->destroy();
			LUMIX_DELETE(m_app.getAllocator(), m_brush_texture);
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

				Engine& engine = m_app.getEngine();
				ResourceManagerHub& rm = engine.getResourceManager();
				if (m_brush_texture)
				{
					m_brush_texture->destroy();
					LUMIX_DELETE(m_app.getAllocator(), m_brush_texture);
				}

				m_brush_texture = LUMIX_NEW(m_app.getAllocator(), Texture)(
					Path("brush_texture"), *rm.get(Texture::TYPE), getRenderer(), m_app.getAllocator());
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
	int type_count = scene->getGrassCount((EntityRef)cmp.entity);
	for (int i = 0; i < type_count; ++i) {
		ImGui::SameLine();
		ImGui::PushID(i);
		if (i == 0 || ImGui::GetContentRegionAvail().x < 50) ImGui::NewLine();
		bool b = (m_grass_mask & (1 << i)) != 0;
		m_app.getAssetBrowser().tile(scene->getGrassPath((EntityRef)cmp.entity, i), b);
		if (ImGui::IsItemClicked()) {
			if (!ImGui::GetIO().KeyCtrl) m_grass_mask = 0;
			if (b) {
				m_grass_mask &= ~(1 << i);
			}
			else {
				m_grass_mask |= 1 << i;
			}
		}
		if (ImGui::BeginPopupContextItem("grs_ctx")) {
			if (ImGui::Selectable("Fill")) fillGrass(i, *cmp.entity, m_app.getWorldEditor());
			if (ImGui::Selectable("Clear")) clearGrass(i, *cmp.entity, m_app.getWorldEditor());
			if (ImGui::Selectable("Export")) exportGrass(i, *cmp.entity, m_app.getWorldEditor());
			if (ImGui::Selectable("Import")) importGrass(i, *cmp.entity, m_app.getWorldEditor());
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	Texture* albedo = material->getTextureByName(DETAIL_ALBEDO_SLOT_NAME);
	Texture* normal = material->getTextureByName(DETAIL_NORMAL_SLOT_NAME);
	
	if (!albedo) {
		ImGui::Text("No detail albedo in material %s", material->getPath().c_str());
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
		ImGui::Text("No detail normal in material %s", material->getPath().c_str());
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

	if (albedo->depth != normal->depth) {
		ImGui::TextWrapped(ICON_FA_EXCLAMATION_TRIANGLE " albedo texture %s has different number of layers than normal texture %s"
			, albedo->getPath().c_str()
			, normal->getPath().c_str());
	}

	bool primary = m_layers_mask & 0b1;
	bool secondary = m_layers_mask & 0b10;


	// TODO shader does not handle secondary surfaces now, so pretend they don't exist
	// uncomment once shader is ready
	// m_layers_mask = 0xb11;
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

	if ((albedo->getPath() != m_albedo_composite_path || albedo->depth != m_layer_views.size()) && albedo->isReady()) {
		m_albedo_composite_path = albedo->getPath();

		DrawStream& stream = getRenderer().getDrawStream();
		
		for (gpu::TextureHandle t : m_layer_views) {
			stream.destroy(t);
		}
		m_layer_views.clear();

		for (u32 layer = 0; layer < albedo->depth; ++layer) {
			gpu::TextureHandle view = gpu::allocTextureHandle();
			m_layer_views.push(view);
			stream.createTextureView(view, albedo->handle, layer);
		}
	}

	m_layers_mask = (primary ? 1 : 0) | (secondary ? 0b10 : 0);

	for (u32 i = 0; i < albedo->depth; ++i) {
		ImGui::SameLine();
		if (i == 0 || ImGui::GetContentRegionAvail().x < 50) ImGui::NewLine();
		bool b = m_textures_mask & ((u64)1 << i);
		if (i < (u32)m_layer_views.size()) {
			thumbnail(m_layer_views[i], 75, m_textures_mask & ((u64)1 << i));
		}
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

	if (albedo->depth < 255) {
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

void TerrainEditor::compositeTextureRemoveLayer(const Path& path, i32 layer) const {
	CompositeTexture texture(m_app, m_app.getAllocator());
	FileSystem& fs = m_app.getEngine().getFileSystem();
	if (!texture.loadSync(fs, path)) {
		logError("Failed to load ", path);
	}
	else {
		texture.removeArrayLayer(layer);
		if (!texture.save(fs, path)) {
			logError("Failed to save ", path);
		}
	}
}

void TerrainEditor::saveCompositeTexture(const Path& path, const char* channel) const
{
	CompositeTexture texture(m_app, m_app.getAllocator());
	FileSystem& fs = m_app.getEngine().getFileSystem();
	if (!texture.loadSync(fs, path)) {
		logError("Failed to load ", path);
	}
	else {
		texture.addArrayLayer(channel);
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
	ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter), ImGuiInputTextFlags_AutoSelectAll);
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
		filter[0] = '\0';
	}

	static ImVec2 size(-1, 200);
	if (ImGui::BeginListBox("##prefabs", size)) {
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
			if (ImGui::Selectable(label, &selected)) {
				if (selected) {
					ResourceManagerHub& manager = m_app.getEngine().getResourceManager();
					PrefabResource* prefab = manager.load<PrefabResource>(res.path);
					if (!ImGui::GetIO().KeyShift) {
						for (PrefabResource* p : m_selected_prefabs) p->decRefCount();
						m_selected_prefabs.clear();
					}
					m_selected_prefabs.push(prefab);
				}
				else {
					PrefabResource* prefab = m_selected_prefabs[selected_idx];
					if (!ImGui::GetIO().KeyShift) {
						for (PrefabResource* p : m_selected_prefabs) p->decRefCount();
						m_selected_prefabs.clear();
					}
					else {
						m_selected_prefabs.swapAndPop(selected_idx);
						prefab->decRefCount();
					}
				}
			}
		}
		if (count == 0) ImGui::TextUnformatted("No prefabs");
		m_app.getAssetCompiler().unlockResources();
		ImGui::EndListBox();
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
		if (ImGui::DragFloatRange2("Rotate X spread", &tmp.x, &tmp.y))
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
		if (ImGui::DragFloatRange2("Rotate Y spread", &tmp.x, &tmp.y))
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
		if (ImGui::DragFloatRange2("Rotate Z spread", &tmp.x, &tmp.y))
		{
			m_rotate_z_spread.x = degreesToRadians(tmp.x);
			m_rotate_z_spread.y = degreesToRadians(tmp.y);
		}
	}
	ImGui::DragFloatRange2("Size spread", &m_size_spread.x, &m_size_spread.y, 0.01f);
	m_size_spread.x = minimum(m_size_spread.x, m_size_spread.y);
	ImGui::DragFloatRange2("Y spread", &m_y_spread.x, &m_y_spread.y, 0.01f);
	m_y_spread.x = minimum(m_y_spread.x, m_y_spread.y);		
}


void TerrainEditor::exportToOBJ(ComponentUID cmp) const {
	char filename[LUMIX_MAX_PATH];
	if (!os::getSaveFilename(Span(filename), "Wavefront obj\0*.obj\0", "obj")) return;

	os::OutputFile file;
	if (!file.open(filename)) {
		logError("Failed to open ", filename);
		return;
	}

	char basename[LUMIX_MAX_PATH];
	copyString(Span(basename), Path::getBasename(filename));

	auto* scene = static_cast<RenderScene*>(cmp.scene);
	const EntityRef e = (EntityRef)cmp.entity;
	const Texture* hm = getMaterial(cmp)->getTextureByName(HEIGHTMAP_SLOT_NAME);

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

void TerrainEditor::onGUI(ComponentUID cmp, WorldEditor& editor) {
	ASSERT(cmp.type == TERRAIN_TYPE);
	
	RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
	ImGui::Unindent();
	if (!ImGui::CollapsingHeader("Terrain editor")) {
		ImGui::Indent();
		return;
	}
	ImGui::Indent();

	ImGuiEx::Label("Enable");
	ImGui::Checkbox("##ed_enabled", &m_is_enabled);
	if (!m_is_enabled) return;

	Material* material = getMaterial(cmp);
	if (!material) {
		ImGui::Text("No material");
		return;
	}

	if (!material->getTextureByName(HEIGHTMAP_SLOT_NAME)) {
		ImGui::Text("No heightmap");
		return;
	}

	if (ImGui::Button(ICON_FA_FILE_EXPORT)) exportToOBJ(cmp);

	ImGuiEx::Label("Brush size");
	ImGui::DragFloat("##br_size", &m_terrain_brush_size, 1, MIN_BRUSH_SIZE, FLT_MAX);
	ImGuiEx::Label("Brush strength");
	ImGui::SliderFloat("##br_str", &m_terrain_brush_strength, 0, 1.0f);

	if (ImGui::BeginTabBar("brush_type")) {
		if (ImGui::BeginTabItem("Height")) {
			m_mode = Mode::HEIGHT;
			if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_SAVE, ImGui::GetStyle().Colors[ImGuiCol_Text], "Save")) 
			{
				material->getTextureByName(HEIGHTMAP_SLOT_NAME)->save();
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
			layerGUI(cmp);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Entity")) {
			entityGUI();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Distance fields")) {
			distanceFieldsUI(cmp);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if (!cmp.isValid() || !m_is_enabled) {
		return;
	}

	const Vec2 mp = editor.getView().getMousePos();

	Universe& universe = *editor.getUniverse();
	for(auto entity : editor.getSelectedEntities()) {
		if (!universe.hasComponent(entity, TERRAIN_TYPE)) continue;
		
		DVec3 origin;
		Vec3 dir;
		editor.getView().getViewport().getRay(mp, origin, dir);
		const RayCastModelHit hit = scene->castRayTerrain(origin, dir);

		if(hit.is_hit) {
			DVec3 center = hit.origin + hit.dir * hit.t;
			drawCursor(*scene, entity, center);
			return;
		}
	}
}


void TerrainEditor::paint(const DVec3& hit_pos, ActionType action_type, bool old_stroke, EntityRef terrain, WorldEditor& editor) const
{
	UniquePtr<PaintTerrainCommand> command = UniquePtr<PaintTerrainCommand>::create(editor.getAllocator(),
		editor,
		action_type,
		m_grass_mask,
		(u64)m_textures_mask,
		hit_pos,
		m_brush_mask,
		m_terrain_brush_size,
		m_terrain_brush_strength,
		m_flat_height,
		m_color,
		terrain,
		m_layers_mask,
		m_fixed_value,
		old_stroke);
	editor.executeCommand(command.move());
}


} // namespace Lumix