#pragma once


#include "editor/world_editor.h"


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

	virtual bool onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int) override
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
		for (int i = 1; i < 8; ++i)
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


	void paintEntities(Lumix::Component terrain, const Lumix::RayCastModelHit& hit)
	{
		Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(terrain.scene);
		Lumix::Vec3 center_pos = hit.m_origin + hit.m_dir * hit.m_t;
		Lumix::Matrix terrain_matrix = terrain.entity.getMatrix();
		Lumix::Matrix inv_terrain_matrix = terrain_matrix;
		inv_terrain_matrix.inverse();
		Lumix::Entity tpl = m_entity_template_list->getTemplate();
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
