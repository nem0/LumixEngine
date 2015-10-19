#include "terrain_editor.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/json_serializer.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/system.h"
#include "editor/entity_template_system.h"
#include "editor/ieditor_command.h"
#include "engine.h"
#include "engine/property_descriptor.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"
#include "universe/universe.h"
#include "utils.h"


static const uint32_t RENDERABLE_HASH = Lumix::crc32("renderable");
static const uint32_t TERRAIN_HASH = Lumix::crc32("terrain");
static const char* HEIGHTMAP_UNIFORM = "u_texHeightmap";
static const char* SPLATMAP_UNIFORM = "u_texSplatmap";
static const char* COLORMAP_UNIFORM = "u_texColormap";
static const char* TEX_COLOR_UNIFORM = "u_texColor";
static const float MIN_BRUSH_SIZE = 0.5f;


struct PaintEntitiesCommand : public Lumix::IEditorCommand
{
	PaintEntitiesCommand(Lumix::WorldEditor& editor,
		Lumix::ComponentUID component,
		uint32_t entity_template,
		float brush_strength,
		float brush_size,
		const Lumix::RayCastModelHit& hit)
		: m_world_editor(editor)
		, m_component(component)
		, m_brush_size(brush_size)
		, m_brush_strength(brush_strength)
		, m_entities(editor.getAllocator())
	{
		auto& template_system = m_world_editor.getEntityTemplateSystem();
		auto& template_names = template_system.getTemplateNames();
		m_template_name_hash = Lumix::crc32(template_names[entity_template].c_str());

		m_center = hit.m_origin + hit.m_dir * hit.m_t;
	}


	virtual void undo()
	{
		for (auto entity : m_entities)
		{
			const auto& cmps = m_world_editor.getComponents(entity);
			for (const auto& cmp : cmps)
			{
				cmp.scene->destroyComponent(cmp.index, cmp.type);
			}
			m_world_editor.getUniverse()->destroyEntity(entity);
		}
		m_entities.clear();
	}


	virtual void serialize(Lumix::JsonSerializer& serializer) 
	{
		serializer.serialize("brush_size", m_brush_size);
		serializer.serialize("brush_strength", m_brush_strength);
		serializer.serialize("center_x", m_center.x);
		serializer.serialize("center_y", m_center.y);
		serializer.serialize("center_z", m_center.z);
		serializer.serialize("cmp_index", m_component.index);
		serializer.serialize("entity", m_component.entity);
		serializer.serialize("template", m_template_name_hash);
	}


	virtual void deserialize(Lumix::JsonSerializer& serializer) 
	{
		serializer.deserialize("brush_size", m_brush_size, 0.0f);
		serializer.deserialize("brush_strength", m_brush_strength, 0.0f);
		serializer.deserialize("center_x", m_center.x, 0.0f);
		serializer.deserialize("center_y", m_center.y, 0.0f);
		serializer.deserialize("center_z", m_center.z, 0.0f);
		serializer.deserialize("cmp_index", m_component.index, 0);
		serializer.deserialize("entity", m_component.entity, 0);
		serializer.deserialize("template", m_template_name_hash, 0);
		m_component.type = TERRAIN_HASH;
		m_component.scene = m_world_editor.getSceneByComponentType(TERRAIN_HASH);
	}
	

	virtual uint32_t getType()
	{
		static const uint32_t type = Lumix::crc32("paint_entities_on_terrain");
		return type;
	}
	

	virtual bool merge(IEditorCommand& command) { return false; }


	virtual bool execute() override
	{
		PROFILE_FUNCTION();
		m_entities.clear();
		Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
		Lumix::Matrix terrain_matrix = m_world_editor.getUniverse()->getMatrix(m_component.entity);
		Lumix::Matrix inv_terrain_matrix = terrain_matrix;
		inv_terrain_matrix.inverse();
		auto& template_system = m_world_editor.getEntityTemplateSystem();

		Lumix::Entity tpl = template_system.getInstances(m_template_name_hash)[0];
		if (tpl < 0) return false;

		Lumix::ComponentUID renderable = m_world_editor.getComponent(tpl, RENDERABLE_HASH);
		if (!renderable.isValid()) return false;

		Lumix::Frustum frustum;
		frustum.computeOrtho(m_center,
			Lumix::Vec3(0, 0, 1),
			Lumix::Vec3(0, 1, 0),
			2 * m_brush_size,
			2 * m_brush_size,
			-m_brush_size,
			m_brush_size);

		Lumix::Array<const Lumix::RenderableMesh*> meshes(m_world_editor.getAllocator());
		meshes.clear();
		scene->getRenderableInfos(frustum, meshes, ~0);

		float w, h;
		scene->getTerrainSize(m_component.index, &w, &h);
		float scale = 1.0f - Lumix::Math::maxValue(0.01f, m_brush_strength);
		Lumix::Model* model = scene->getRenderableModel(renderable.index);
		for (int i = 0; i <= m_brush_size * m_brush_size / 1000.0f; ++i)
		{
			float angle = (float)(rand() % 360);
			float dist = (rand() % 100 / 100.0f) * m_brush_size;
			Lumix::Vec3 pos(m_center.x + cos(angle) * dist, 0, m_center.z + sin(angle) * dist);
			Lumix::Vec3 terrain_pos = inv_terrain_matrix.multiplyPosition(pos);
			if (terrain_pos.x >= 0 && terrain_pos.z >= 0 && terrain_pos.x <= w &&
				terrain_pos.z <= h)
			{
				pos.y = scene->getTerrainHeightAt(m_component.index, terrain_pos.x, terrain_pos.z);
				pos.y += terrain_matrix.getTranslation().y;
				if (!isOBBCollision(meshes, pos, model, scale))
				{
					auto entity = template_system.createInstanceNoCommand(m_template_name_hash, pos);
					m_entities.push(entity);
				}
			}
		}

		return !m_entities.empty();
	}


	static void getProjections(const Lumix::Vec3& axis,
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


	static bool overlaps(float min1, float max1, float min2, float max2)
	{
		return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
	}


	static bool testOBBCollision(const Lumix::Matrix& matrix_a,
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
			matrix_a.getXVector(), matrix_a.getYVector(), matrix_a.getZVector() };
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
			matrix_b.getXVector(), matrix_b.getYVector(), matrix_b.getZVector() };
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


	bool isOBBCollision(const Lumix::Array<const Lumix::RenderableMesh*>& meshes,
		const Lumix::Vec3& pos_a,
		Lumix::Model* model,
		float scale)
	{
		float radius_a_squared = model->getBoundingRadius();
		radius_a_squared = radius_a_squared * radius_a_squared;
		for (auto* mesh : meshes)
		{
			Lumix::Vec3 pos_b = mesh->m_matrix->getTranslation();
			float radius_b = mesh->m_model->getBoundingRadius();
			float radius_squared = radius_a_squared + radius_b * radius_b;
			if ((pos_a - pos_b).squaredLength() < radius_squared * scale * scale)
			{
				Lumix::Matrix matrix = Lumix::Matrix::IDENTITY;
				matrix.setTranslation(pos_a);
				if (testOBBCollision(matrix, model, *mesh->m_matrix, mesh->m_model, scale))
				{
					return true;
				}
			}
		}
		return false;
	}

	Lumix::WorldEditor& m_world_editor;
	Lumix::ComponentUID m_component;
	Lumix::Array<Lumix::Entity> m_entities;
	float m_brush_strength;
	float m_brush_size;
	uint32_t m_template_name_hash;
	Lumix::Vec3 m_center;
};



struct RemoveEntitiesCommand : public Lumix::IEditorCommand
{
	RemoveEntitiesCommand(Lumix::WorldEditor& editor,
		Lumix::ComponentUID component,
		int entity_template,
		float brush_size,
		const Lumix::RayCastModelHit& hit)
		: m_editor(editor)
		, m_component(component)
		, m_brush_size(brush_size)
		, m_removed_entities(editor.getAllocator())
	{
		auto& template_system = m_editor.getEntityTemplateSystem();
		auto& template_names = template_system.getTemplateNames();
		m_template_name_hash = Lumix::crc32(template_names[entity_template].c_str());

		m_center = hit.m_origin + hit.m_dir * hit.m_t;
	}


	virtual void undo()
	{
		Lumix::Universe* universe = m_editor.getUniverse();
		const auto& scenes = m_editor.getScenes();
		Lumix::InputBlob blob(m_removed_entities);
		bool is_entity;
		blob.read(is_entity);
		while(is_entity)
		{
			Lumix::Vec3 pos;
			Lumix::Quat rot;
			float scale;
			blob.read(pos);
			blob.read(rot);
			blob.read(scale);
			Lumix::Entity new_entity = universe->createEntity(pos, rot);
			universe->setScale(new_entity, scale);
			
			int cmps_count;
			blob.read(cmps_count);
			for (int j = cmps_count - 1; j >= 0; --j)
			{
				Lumix::ComponentUID::Type cmp_type;
				blob.read(cmp_type);
				Lumix::ComponentUID new_component;
				for (int i = 0; i < scenes.size(); ++i)
				{
					new_component.index =
						scenes[i]->createComponent(cmp_type, new_entity);
					new_component.entity = new_entity;
					new_component.scene = scenes[i];
					new_component.type = cmp_type;
					if (new_component.isValid()) break;
				}

				auto& props = m_editor.getEngine().getPropertyDescriptors(cmp_type);
				for (int k = 0; k < props.size(); ++k)
				{
					props[k]->set(new_component, blob);
				}
			}
			blob.read(is_entity);
		}
	}


	virtual void serialize(Lumix::JsonSerializer& serializer)
	{
		serializer.serialize("brush_size", m_brush_size);
		serializer.serialize("center_x", m_center.x);
		serializer.serialize("center_y", m_center.y);
		serializer.serialize("center_z", m_center.z);
		serializer.serialize("cmp_index", m_component.index);
		serializer.serialize("entity", m_component.entity);
		serializer.serialize("template", m_template_name_hash);
	}


	virtual void deserialize(Lumix::JsonSerializer& serializer)
	{
		serializer.deserialize("brush_size", m_brush_size, 0.0f);
		serializer.deserialize("center_x", m_center.x, 0.0f);
		serializer.deserialize("center_y", m_center.y, 0.0f);
		serializer.deserialize("center_z", m_center.z, 0.0f);
		serializer.deserialize("cmp_index", m_component.index, 0);
		serializer.deserialize("entity", m_component.entity, 0);
		serializer.deserialize("template", m_template_name_hash, 0);
		m_component.type = TERRAIN_HASH;
		m_component.scene = m_editor.getSceneByComponentType(TERRAIN_HASH);
	}


	virtual uint32_t getType()
	{
		static const uint32_t type = Lumix::crc32("remove_entities_on_terrain");
		return type;
	}


	virtual bool merge(IEditorCommand& command) { return false; }


	virtual bool execute() override
	{
		PROFILE_FUNCTION();

		Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(m_component.scene);
		Lumix::Frustum frustum;
		frustum.computeOrtho(m_center,
			Lumix::Vec3(0, 0, 1),
			Lumix::Vec3(0, 1, 0),
			2 * m_brush_size,
			2 * m_brush_size,
			-m_brush_size,
			m_brush_size);

		Lumix::Array<Lumix::Entity> entities(m_editor.getAllocator());
		scene->getRenderableEntities(frustum, entities, ~0);
		auto& template_system = m_editor.getEntityTemplateSystem();
		m_removed_entities.clear();
		Lumix::Universe* universe = m_editor.getUniverse();
		for (Lumix::Entity entity : entities)
		{
			if (template_system.getTemplate(entity) != m_template_name_hash) continue;

			m_removed_entities.write(true);
			m_removed_entities.write(universe->getPosition(entity));
			m_removed_entities.write(universe->getRotation(entity));
			m_removed_entities.write(universe->getScale(entity));

			const auto& cmps = m_editor.getComponents(entity);
			m_removed_entities.write((int)cmps.size());
			for (const auto& cmp : cmps)
			{
				m_removed_entities.write(cmp.type);
				auto& props = m_editor.getEngine().getPropertyDescriptors(cmp.type);
				for (int k = 0; k < props.size(); ++k)
				{
					props[k]->get(cmp, m_removed_entities);
				}
				cmp.scene->destroyComponent(cmp.index, cmp.type);
			}

			m_editor.getUniverse()->destroyEntity(entity);
		}
		m_removed_entities.write(false);
		return m_removed_entities.getSize() > sizeof(bool);
	}


	Lumix::WorldEditor& m_editor;
	Lumix::ComponentUID m_component;
	Lumix::OutputBlob m_removed_entities;
	float m_brush_size;
	uint32_t m_template_name_hash;
	Lumix::Vec3 m_center;
};


struct PaintTerrainCommand : public Lumix::IEditorCommand
{
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
		Lumix::BinaryArray& mask,
		float radius,
		float rel_amount,
		uint16_t flat_height,
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
		auto hm = getMaterial()->getTextureByUniform(HEIGHTMAP_UNIFORM);
		auto texture = getDestinationTexture();

		Item& item = m_items.pushEmpty();
		item.m_local_pos = local_pos;
		item.m_radius = radius;
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


	virtual bool execute() override
	{
		if (m_new_data.empty())
		{
			saveOldData();
			generateNewData();
		}
		applyData(m_new_data);
		return true;
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
		return int(sum / (to_x - from_x) / (to_y - from_y));
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
		float dist = sqrt(
			(item.m_local_pos.x - 0.5f - i) * (item.m_local_pos.x - 0.5f - i) +
			(item.m_local_pos.z - 0.5f - j) * (item.m_local_pos.z - 0.5f - j));
		return 1.0f - Lumix::Math::minValue(dist / item.m_radius, 1.0f);
	}


	void rasterColorItem(Lumix::Texture* texture,
						 Lumix::Array<uint8_t>& data,
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
					uint8_t* d = &data[offset];
					d[0] += uint8_t((item.m_color.x * 255 - d[0]) * attenuation);
					d[1] += uint8_t((item.m_color.y * 255 - d[1]) * attenuation);
					d[2] += uint8_t((item.m_color.z * 255 - d[2]) * attenuation);
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
						 Lumix::Array<uint8_t>& data,
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


	void rasterSmoothHeightItem(Lumix::Texture* texture, Lumix::Array<uint8_t>& data, Item& item)
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
				uint16_t x = ((uint16_t*)texture->getData())[(i + j * texture_width)];
				x += uint16_t((avg - x) * item.m_amount * attenuation);
				((uint16_t*)&data[0])[offset] = x;
			}
		}
	}


	void rasterFlatHeightItem(Lumix::Texture* texture, Lumix::Array<uint8_t>& data, Item& item)
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
				((uint16_t*)&data[0])[offset] = m_flat_height;
			}
		}
	}


	void rasterItem(Lumix::Texture* texture, Lumix::Array<uint8_t>& data, Item& item)
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
				uint16_t x = ((uint16_t*)texture->getData())[(i + j * texture_width)];
				x += m_type == TerrainEditor::RAISE_HEIGHT ? Lumix::Math::minValue(add, 0xFFFF - x)
														   : Lumix::Math::maxValue(-add, -x);
				((uint16_t*)&data[0])[offset] = x;
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
		texture->onDataUpdated(m_x, m_y, m_width, m_height);
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
				&texture->getData()[row * bpp * texture->getWidth() + rect.m_from_x * bpp],
				bpp * new_w);
			memcpy(&old_data[(row - rect.m_from_y) * new_w * bpp],
				&texture->getData()[row * bpp * texture->getWidth() + rect.m_from_x * bpp],
				bpp * new_w);
		}

		// new
		for (int row = 0; row < m_height; ++row)
		{
			memcpy(&new_data[((row + m_y - rect.m_from_y) * new_w + m_x - rect.m_from_x) * bpp],
				&m_new_data[row * bpp * m_width],
				bpp * m_width);
			memcpy(&old_data[((row + m_y - rect.m_from_y) * new_w + m_x - rect.m_from_x) * bpp],
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
	Lumix::BinaryArray m_mask;
	uint16_t m_flat_height;
	bool m_can_be_merged;
};


TerrainEditor::~TerrainEditor()
{
	if (m_brush_texture)
	{
		m_brush_texture->destroy();
		m_world_editor.getAllocator().deleteObject(m_brush_texture);
	}

	m_world_editor.removePlugin(*this);
}


TerrainEditor::TerrainEditor(Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions)
	: m_world_editor(editor)
	, m_color(1, 1, 1)
	, m_current_brush(0)
	, m_selected_entity_template(0)
	, m_brush_mask(editor.getAllocator())
	, m_brush_texture(nullptr)
	, m_flat_height(0)
{
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
		Lumix::Vec3 local_from =
			local_center + Lumix::Vec3(cos(angle), 0, sin(angle)) * brush_size;
		local_from.y =
			scene.getTerrainHeightAt(terrain.index, local_from.x, local_from.z);
		local_from.y += 0.25f;
		Lumix::Vec3 local_to =
			local_center +
			Lumix::Vec3(cos(next_angle), 0, sin(next_angle)) * brush_size;
		local_to.y =
			scene.getTerrainHeightAt(terrain.index, local_to.x, local_to.z);
		local_to.y += 0.25f;

		Lumix::Vec3 from = terrain_matrix.multiplyPosition(local_from);
		Lumix::Vec3 to = terrain_matrix.multiplyPosition(local_to);
		scene.addDebugLine(from, to, 0xffff0000, 0);
	}

	float brush_size2 = brush_size * brush_size;
	Lumix::Vec3 local_pos;
	local_pos.x = Lumix::Math::floor(local_center.x - brush_size);
	float to_x = Lumix::Math::floor(local_center.x + brush_size + 1);
	float to_z = Lumix::Math::floor(local_center.z + brush_size + 1);
	while (local_pos.x < to_x)
	{
		local_pos.z = Lumix::Math::floor(local_center.z - brush_size);
		while (local_pos.z < to_z)
		{
			float dx = local_center.x - local_pos.x;
			float dz = local_center.z - local_pos.z;
			if (dx * dx + dz * dz < brush_size2)
			{
				local_pos.y = scene.getTerrainHeightAt(
					terrain.index, local_pos.x, local_pos.z);
				local_pos.y += 0.05f;
				Lumix::Vec3 world_pos =
						terrain_matrix.multiplyPosition(local_pos);
				scene.addDebugPoint(world_pos, 0xffff0000, 0);
			}
			++local_pos.z;
		}
		++local_pos.x;
	}

	
}


void TerrainEditor::tick()
{
	if (!m_component.isValid()) return;

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
					drawCursor(*scene, terrain, center);
					return;
				}
			}
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


Lumix::Vec3& TerrainEditor::getRelativePosition(const Lumix::Vec3& world_pos) const
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


uint16_t TerrainEditor::getHeight(const Lumix::Vec3& world_pos)
{
	auto rel_pos = getRelativePosition(world_pos);
	auto* heightmap = getHeightmap();
	if (!heightmap) return 0;

	auto* data = (uint16_t*)heightmap->getData();
	return data[int(rel_pos.x) + int(rel_pos.z) * heightmap->getWidth()];
}


bool TerrainEditor::onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int)
{
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
	int templates_count = m_world_editor.getEntityTemplateSystem().getTemplateNames().size();
	if (m_selected_entity_template >= templates_count) return;

	RemoveEntitiesCommand* command =
		LUMIX_NEW(m_world_editor.getAllocator(), RemoveEntitiesCommand)(m_world_editor,
		m_component,
		m_selected_entity_template,
		m_terrain_brush_size,
		hit);
	m_world_editor.executeCommand(command);
}

void TerrainEditor::paintEntities(const Lumix::RayCastModelHit& hit)
{
	if (m_selected_entity_template < 0) return;
	int templates_count = m_world_editor.getEntityTemplateSystem().getTemplateNames().size();
	if (m_selected_entity_template >= templates_count) return;

	PaintEntitiesCommand* command =
		LUMIX_NEW(m_world_editor.getAllocator(), PaintEntitiesCommand)(m_world_editor,
			m_component,
			m_selected_entity_template,
			m_terrain_brush_strength,
			m_terrain_brush_size,
			hit);
	m_world_editor.executeCommand(command);
}


void TerrainEditor::onMouseMove(int x, int y, int, int)
{
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
				m_world_editor.getAllocator().deleteObject(m_brush_texture);
				m_brush_mask.clear();
				m_brush_texture = nullptr;
			}
			ImGui::SameLine();
		}

		ImGui::SameLine();
		if (ImGui::Button("Select mask"))
		{
			char filename[Lumix::MAX_PATH_LENGTH];
			if (Lumix::getOpenFilename(filename, Lumix::lengthOf(filename), "All\0*.*\0"))
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
						m_world_editor.getAllocator().deleteObject(m_brush_texture);
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
			ColorPicker("Color", &m_color.x);
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
		m_world_editor.getAllocator().newObject<PaintTerrainCommand>(
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
