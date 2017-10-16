#include "property_grid.h"
#include "asset_browser.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/iproperty_descriptor.h"
#include "engine/math_utils.h"
#include "engine/prefab.h"
#include "engine/property_register.h"
#include "engine/resource.h"
#include "engine/serializer.h"
#include "engine/vec.h"
#include "imgui/imgui.h"
#include "utils.h"
#include <cmath>
#include <cstdlib>


namespace Lumix
{


PropertyGrid::PropertyGrid(StudioApp& app)
	: m_app(app)
	, m_is_open(true)
	, m_editor(app.getWorldEditor())
	, m_plugins(app.getWorldEditor().getAllocator())
{
	m_particle_emitter_updating = true;
	m_particle_emitter_timescale = 1.0f;
	m_component_filter[0] = '\0';
}


PropertyGrid::~PropertyGrid()
{
	for (auto* i : m_plugins)
	{
		LUMIX_DELETE(m_editor.getAllocator(), i);
	}
}

// TODO
/*
void PropertyGrid::showSampledFunctionProperty(const Array<Entity>& entities,
	ComponentType cmp_type,
	ISampledFunctionDescriptor& desc)
{
	static const int MIN_COUNT = 6;

	OutputBlob blob(m_editor.getAllocator());
	ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	desc.get(cmp, -1, blob);
	int count;
	InputBlob input(blob);
	input.read(count);
	Vec2* f = (Vec2*)input.skip(sizeof(Vec2) * count);

	auto editor = ImGui::BeginCurveEditor(desc.getName());
	if (editor.valid)
	{
		bool changed = false;

		changed |= ImGui::CurveSegment((ImVec2*)(f + 1), editor);

		for (int i = 1; i < count - 3; i += 3)
		{
			changed |= ImGui::CurveSegment((ImVec2*)(f + i), editor);

			if (changed)
			{
				f[i + 3].x = Math::maximum(f[i].x + 0.001f, f[i + 3].x);

				if (i + 3 < count)
				{
					f[i + 3].x = Math::minimum(f[i + 6].x - 0.001f, f[i + 3].x);
				}
			}

			if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0)
				&& count > MIN_COUNT && i + 3 < count - 2)
			{
				for (int j = i + 2; j < count - 3; ++j)
				{
					f[j] = f[j + 3];
				}
				count -= 3;
				*(int*)blob.getData() = count;
				changed = true;
			}
		}

		f[count - 2].x = 1;
		f[1].x = 0;
		ImGui::EndCurveEditor(editor);

		if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0))
		{
			auto mp = ImGui::GetMousePos();
			mp.x -= editor.inner_bb_min.x - 1;
			mp.y -= editor.inner_bb_min.y - 1;
			mp.x /= (editor.inner_bb_max.x - editor.inner_bb_min.x);
			mp.y /= (editor.inner_bb_max.y - editor.inner_bb_min.y);
			mp.y = 1 - mp.y;
			blob.write(ImVec2(-0.2f, 0));
			blob.write(mp);
			blob.write(ImVec2(0.2f, 0));
			count += 3;
			*(int*)blob.getData() = count;
			f = (Vec2*)((int*)blob.getData() + 1);
			changed = true;

			auto compare = [](const void* a, const void* b) -> int
			{
				float fa = ((const float*)a)[2];
				float fb = ((const float*)b)[2];
				return fa < fb ? -1 : (fa > fb) ? 1 : 0;
			};

			qsort(f, count / 3, 3 * sizeof(f[0]), compare);
		}

		if (changed)
		{
			for (int i = 2; i < count - 3; i += 3)
			{
				auto prev_p = ((Vec2*)f)[i - 1];
				auto next_p = ((Vec2*)f)[i + 2];
				auto& tangent = ((Vec2*)f)[i];
				auto& tangent2 = ((Vec2*)f)[i + 1];
				float half = 0.5f * (next_p.x - prev_p.x);
				tangent = tangent.normalized() * half;
				tangent2 = tangent2.normalized() * half;
			}

			f[0].x = 0;
			f[count - 1].x = desc.getMaxX();
			m_editor.setProperty(cmp_type, -1, desc.getName(), &entities[0], entities.size(), blob.getData(), blob.getPos());
		}
	}
}*/
	

struct GridUIVisitor : Properties::IComponentVisitor
{
	GridUIVisitor(StudioApp& app, int index, const Array<Entity>& entities, ComponentType cmp_type, WorldEditor& editor)
		: m_entities(entities)
		, m_cmp_type(cmp_type)
		, m_editor(editor)
		, m_index(index)
		, m_grid(app.getPropertyGrid())
		, m_app(app)
	{}


	ComponentUID getComponent()
	{
		ComponentUID first_entity_cmp;
		first_entity_cmp.type = m_cmp_type;
		first_entity_cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
		first_entity_cmp.entity = m_entities[0];
		first_entity_cmp.handle = first_entity_cmp.scene->getComponent(m_entities[0], m_cmp_type);
		return first_entity_cmp;
	}


	struct Attributes : Properties::IAttributeVisitor
	{
		void visit(const Properties::IAttribute& attr) override
		{
			switch (attr.getType())
			{
				case Properties::IAttribute::RADIANS:
					is_radians = true;
					break;
				case Properties::IAttribute::COLOR:
					is_color = true;
					break;
				case Properties::IAttribute::MIN:
					min = ((Properties::MinAttribute&)attr).min;
					break;
				case Properties::IAttribute::CLAMP:
					min = ((Properties::ClampAttribute&)attr).min;
					max = ((Properties::ClampAttribute&)attr).max;
					break;
				case Properties::IAttribute::RESOURCE:
					resource_type = ((Properties::ResourceAttribute&)attr).type;
					break;
			}
		}

		float max = FLT_MAX;
		float min = -FLT_MAX;
		bool is_color = false;
		bool is_radians = false;
		ResourceType resource_type;
	};


	static Attributes getAttributes(const Properties::PropertyBase& prop)
	{
		Attributes attrs;
		prop.visit(attrs);
		return attrs;
	}


	void visit(const Properties::Property<float>& prop) override
	{
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		float f;
		prop.getValue(cmp, m_index, OutputBlob(&f, sizeof(f)));

		if (attrs.is_radians) f = Math::radiansToDegrees(f);
		if (ImGui::DragFloat(prop.name, &f, 1, attrs.min, attrs.max))
		{
			f = Math::clamp(f, attrs.min, attrs.max);
			if (attrs.is_radians) f = Math::degreesToRadians(f);
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &f, sizeof(f));
		}
	}


	void visit(const Properties::Property<int>& prop) override
	{
		ComponentUID cmp = getComponent();
		int value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));

		if (ImGui::InputInt(prop.name, &value))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Properties::Property<Entity>& prop) override
	{
		OutputBlob blob(m_editor.getAllocator());

		ComponentUID cmp = getComponent();
		Entity entity;
		prop.getValue(cmp, m_index, OutputBlob(&entity, sizeof(entity)));

		char buf[128];
		getEntityListDisplayName(m_editor, buf, lengthOf(buf), entity);
		ImGui::LabelText(prop.name, "%s", buf);
		ImGui::SameLine();
		ImGui::PushID(prop.name);
		if (ImGui::Button("...")) ImGui::OpenPopup(prop.name);
		Universe& universe = *m_editor.getUniverse();
		if (ImGui::BeginPopup(prop.name))
		{
			if (entity.isValid() && ImGui::Button("Select")) m_deferred_select = entity;

			static char entity_filter[32] = {};
			ImGui::FilterInput("Filter", entity_filter, sizeof(entity_filter));
			for (auto i = universe.getFirstEntity(); i.isValid(); i = universe.getNextEntity(i))
			{
				getEntityListDisplayName(m_editor, buf, lengthOf(buf), i);
				bool show = entity_filter[0] == '\0' || stristr(buf, entity_filter) != 0;
				if (show && ImGui::Selectable(buf))
				{
					m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &i, sizeof(i));
				}
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}


	void visit(const Properties::Property<Int2>& prop) override
	{
		ComponentUID cmp = getComponent();
		Int2 value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));
		if (ImGui::DragInt2(prop.name, &value.x))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Properties::Property<Vec2>& prop) override
	{
		ComponentUID cmp = getComponent();
		Vec2 value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));
		if (ImGui::DragFloat2(prop.name, &value.x))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Properties::Property<Vec3>& prop) override
	{
		Attributes attrs = getAttributes(prop);
		ComponentUID cmp = getComponent();
		Vec3 value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));

		if (attrs.is_color)
		{
			if (ImGui::ColorEdit3(prop.name, &value.x))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
			}
		}
		else
		{
			if (attrs.is_radians) value = Math::radiansToDegrees(value);
			if (ImGui::DragFloat3(prop.name, &value.x, 1, attrs.min, attrs.max))
			{
				if (attrs.is_radians) value = Math::degreesToRadians(value);
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
			}
		}
	}


	void visit(const Properties::Property<Vec4>& prop) override
	{
		ComponentUID cmp = getComponent();
		Vec4 value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));

		if (ImGui::DragFloat4(prop.name, &value.x))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Properties::Property<bool>& prop) override
	{
		ComponentUID cmp = getComponent();
		bool value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));

		if (ImGui::Checkbox(prop.name, &value))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), &value, sizeof(value));
		}
	}


	void visit(const Properties::Property<Path>& prop) override
	{
		ComponentUID cmp = getComponent();
		char tmp[1024];
		prop.getValue(cmp, m_index, OutputBlob(&tmp, sizeof(tmp)));

		Attributes attrs = getAttributes(prop);

		if (attrs.resource_type != INVALID_RESOURCE_TYPE)
		{
			if (m_app.getAssetBrowser().resourceInput(prop.name, StaticString<20>("", (u64)&prop), tmp, sizeof(tmp), attrs.resource_type))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), tmp, stringLength(tmp) + 1);
			}
		}
		else
		{
			if (ImGui::InputText(prop.name, tmp, sizeof(tmp)))
			{
				m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), tmp, stringLength(tmp) + 1);
			}
		}
	}


	void visit(const Properties::Property<const char*>& prop) override
	{
		ComponentUID cmp = getComponent();
		char tmp[1024];
		prop.getValue(cmp, m_index, OutputBlob(&tmp, sizeof(tmp)));

		if (ImGui::InputText(prop.name, tmp, sizeof(tmp)))
		{
			m_editor.setProperty(m_cmp_type, m_index, prop, &m_entities[0], m_entities.size(), tmp, stringLength(tmp) + 1);
		}
	}


	void visit(const Properties::IBlobProperty& prop) override
	{

	}

	void visit(const Properties::IArrayProperty& prop) override
	{
		bool is_open = ImGui::TreeNodeEx(prop.name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlapMode);
		if (m_entities.size() > 1)
		{
			ImGui::Text("Multi-object editing not supported.");
			if (is_open) ImGui::TreePop();
			return;
		}

		ComponentUID cmp = getComponent();
		int count = prop.getCount(cmp);
		if (prop.canAddRemove())
		{
			float w = ImGui::GetContentRegionAvailWidth();
			ImGui::SameLine(w - 45);
			if (ImGui::SmallButton("Add"))
			{
				m_editor.addArrayPropertyItem(cmp, prop);
				count = prop.getCount(cmp);
			}
		}
		if (!is_open) return;

		for (int i = 0; i < count; ++i)
		{
			char tmp[10];
			toCString(i, tmp, sizeof(tmp));
			ImGui::PushID(i);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlapMode;
			bool is_open = !prop.canAddRemove() || ImGui::TreeNodeEx(tmp, flags);
			if (prop.canAddRemove())
			{
				float w = ImGui::GetContentRegionAvailWidth();
				ImGui::SameLine(w - 45);
				if (ImGui::SmallButton("Remove"))
				{
					m_editor.removeArrayPropertyItem(cmp, i, prop);
					--i;
					count = prop.getCount(cmp);
					if(is_open) ImGui::TreePop();
					ImGui::PopID();
					continue;
				}
			}

			if (is_open)
			{
				GridUIVisitor v(m_app, i, m_entities, m_cmp_type, m_editor);
				prop.visit(v);
				if (prop.canAddRemove()) ImGui::TreePop();
			}

			ImGui::PopID();
		}
		ImGui::TreePop();
	}


	void visit(const Properties::IEnumProperty& prop) override
	{
		if (m_entities.size() > 1)
		{
			ImGui::LabelText(prop.name, "Multi-object editing not supported.");
			return;
		}

		ComponentUID cmp = getComponent();
		int value;
		prop.getValue(cmp, m_index, OutputBlob(&value, sizeof(value)));
		int count = prop.getEnumCount(cmp);

		struct Data
		{
			const Properties::IEnumProperty* prop;
			ComponentUID cmp;
		};

		auto getter = [](void* data, int index, const char** out) -> bool {
			Data* combo_data = (Data*)data;
			*out = combo_data->prop->getEnumName(combo_data->cmp, index);
			return true;
		};

		Data data;
		data.cmp = cmp;
		data.prop = &prop;

		if (ImGui::Combo(prop.name, &value, getter, &data, count))
		{
			m_editor.setProperty(cmp.type, m_index, prop, &cmp.entity, 1, &value, sizeof(value));
		}
	}


	StudioApp& m_app;
	WorldEditor& m_editor;
	ComponentType m_cmp_type;
	const Array<Entity>& m_entities;
	int m_index;
	PropertyGrid& m_grid;
	Entity m_deferred_select = INVALID_ENTITY;
};


void PropertyGrid::showComponentProperties(const Array<Entity>& entities, ComponentType cmp_type)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlapMode;
	ImGui::Separator();
	const char* cmp_type_name = m_app.getComponentTypeName(cmp_type);
	bool is_open = ImGui::TreeNodeEx((void*)(intptr_t)cmp_type.index, flags, "%s", cmp_type_name);

	float w = ImGui::GetContentRegionAvailWidth();
	ImGui::SameLine(w - 45);
	if (ImGui::SmallButton("Remove"))
	{
		m_editor.destroyComponent(&entities[0], entities.size(), cmp_type);
		return;
	}

	if (!is_open) return;

	Properties::IComponentDescriptor* component = Properties::getComponent(cmp_type);
	GridUIVisitor visitor(m_app, -1, entities, cmp_type, m_editor);
	if (component) component->visit(visitor);

	if (visitor.m_deferred_select.isValid())
	{
		m_editor.selectEntities(&visitor.m_deferred_select, 1);
		visitor.m_deferred_select = INVALID_ENTITY;
	}


	if (entities.size() == 1)
	{
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.scene = m_editor.getUniverse()->getScene(cmp.type);
		cmp.entity = entities[0];
		cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
		for (auto* i : m_plugins)
		{
			i->onGUI(*this, cmp);
		}
	}
	ImGui::TreePop();
}


bool PropertyGrid::entityInput(const char* label, const char* str_id, Entity& entity) const
{
	const auto& style = ImGui::GetStyle();
	float item_w = ImGui::CalcItemWidth();
	ImGui::PushItemWidth(
		item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
	char buf[50];
	getEntityListDisplayName(m_editor, buf, sizeof(buf), entity);
	ImGui::LabelText("", "%s", buf);
	ImGui::SameLine();
	StaticString<30> popup_name("pu", str_id);
	if (ImGui::Button(StaticString<30>("...###br", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}

	if (ImGui::IsItemHoveredRect())
	{
		if (ImGui::IsMouseReleased(0) && m_app.getDragData().type == StudioApp::DragData::ENTITY)
		{
			entity = *(Entity*)m_app.getDragData().data;
			return true;
		}
	}

	ImGui::SameLine();
	ImGui::Text("%s", label);
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup(popup_name))
	{
		if (entity.isValid())
		{
			if (ImGui::Button("Select current")) m_editor.selectEntities(&entity, 1);
			ImGui::SameLine();
			if (ImGui::Button("Empty"))
			{
				entity = INVALID_ENTITY;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return true;
			}
		}
		Universe* universe = m_editor.getUniverse();
		static int current_item;
		if (ImGui::ListBoxHeader("Entities"))
		{
			for (auto i = universe->getFirstEntity(); i.isValid(); i = universe->getNextEntity(i))
			{
				getEntityListDisplayName(m_editor, buf, lengthOf(buf), i);
				if (ImGui::Selectable(buf))
				{
					ImGui::ListBoxFooter();
					entity = i;
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return true;
				}
			}
			ImGui::ListBoxFooter();
		}

		ImGui::EndPopup();
	}
	return false;
}


void PropertyGrid::showCoreProperties(const Array<Entity>& entities)
{
	if (entities.size() == 1)
	{
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
		PrefabResource* prefab = prefab_system.getPrefabResource(entities[0]);
		if (prefab)
		{
			ImGui::SameLine();
			if (ImGui::Button("Save prefab"))
			{
				prefab_system.savePrefab(prefab->getPath());
			}
		}

		char name[256];

		ImGui::LabelText("ID", "%d", entities[0].index);
		EntityGUID guid = m_editor.getEntityGUID(entities[0]);
		if (guid == INVALID_ENTITY_GUID)
		{
			ImGui::LabelText("GUID", "%s", "runtime");
		}
		else
		{
			char guid_str[32];
			toCString(guid.value, guid_str, lengthOf(guid_str));
			ImGui::LabelText("GUID", "%s", guid_str);
		}

		Entity parent = m_editor.getUniverse()->getParent(entities[0]);
		if (parent.isValid())
		{
			getEntityListDisplayName(m_editor, name, lengthOf(name), parent);
			ImGui::LabelText("Parent", "%s", name);

			Transform tr = m_editor.getUniverse()->getLocalTransform(entities[0]);
			Vec3 old_pos = tr.pos;
			if (ImGui::DragFloat3("Local position", &tr.pos.x))
			{
				WorldEditor::Coordinate coord;
				if (tr.pos.x != old_pos.x) coord = WorldEditor::Coordinate::X;
				if (tr.pos.y != old_pos.y) coord = WorldEditor::Coordinate::Y;
				if (tr.pos.z != old_pos.z) coord = WorldEditor::Coordinate::Z;
				m_editor.setEntitiesLocalCoordinate(&entities[0], entities.size(), (&tr.pos.x)[(int)coord], coord);
			}
		}

		const char* tmp = m_editor.getUniverse()->getEntityName(entities[0]);
		copyString(name, tmp);
		if (ImGui::InputText("Name", name, sizeof(name))) m_editor.setEntityName(entities[0], name);
	}
	else
	{
		ImGui::LabelText("ID", "%s", "Multiple objects");
		ImGui::LabelText("Name", "%s", "Multi-object editing not supported.");
	}


	Vec3 pos = m_editor.getUniverse()->getPosition(entities[0]);
	Vec3 old_pos = pos;
	if (ImGui::DragFloat3("Position", &pos.x))
	{
		WorldEditor::Coordinate coord;
		if (pos.x != old_pos.x) coord = WorldEditor::Coordinate::X;
		if (pos.y != old_pos.y) coord = WorldEditor::Coordinate::Y;
		if (pos.z != old_pos.z) coord = WorldEditor::Coordinate::Z;
		m_editor.setEntitiesCoordinate(&entities[0], entities.size(), (&pos.x)[(int)coord], coord);
	}

	Universe* universe = m_editor.getUniverse();
	Quat rot = universe->getRotation(entities[0]);
	Vec3 old_euler = rot.toEuler();
	Vec3 euler = Math::radiansToDegrees(old_euler);
	if (ImGui::DragFloat3("Rotation", &euler.x))
	{
		if (euler.x <= -90.0f || euler.x >= 90.0f) euler.y = 0;
		euler.x = Math::degreesToRadians(Math::clamp(euler.x, -90.0f, 90.0f));
		euler.y = Math::degreesToRadians(fmodf(euler.y + 180, 360.0f) - 180);
		euler.z = Math::degreesToRadians(fmodf(euler.z + 180, 360.0f) - 180);
		rot.fromEuler(euler);
		
		Array<Quat> rots(m_editor.getAllocator());
		for (Entity entity : entities)
		{
			Vec3 tmp = universe->getRotation(entity).toEuler();
			
			if (fabs(euler.x - old_euler.x) > 0.01f) tmp.x = euler.x;
			if (fabs(euler.y - old_euler.y) > 0.01f) tmp.y = euler.y;
			if (fabs(euler.z - old_euler.z) > 0.01f) tmp.z = euler.z;
			rots.emplace().fromEuler(tmp);
		}
		m_editor.setEntitiesRotations(&entities[0], &rots[0], entities.size());
	}

	float scale = m_editor.getUniverse()->getScale(entities[0]);
	if (ImGui::DragFloat("Scale", &scale, 0.1f))
	{
		m_editor.setEntitiesScale(&entities[0], entities.size(), scale);
	}
}


static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const char* filter)
{
	if (!node) return;

	if (filter[0])
	{
		if (!node->plugin) showAddComponentNode(node->child, filter);
		else if (stristr(node->plugin->getLabel(), filter)) node->plugin->onGUI(false, true);
		showAddComponentNode(node->next, filter);
		return;
	}

	if (node->plugin)
	{
		node->plugin->onGUI(false, false);
		showAddComponentNode(node->next, filter);
		return;
	}

	const char* last = reverseFind(node->label, nullptr, '/');
	if (ImGui::BeginMenu(last ? last + 1 : node->label))
	{
		showAddComponentNode(node->child, filter);
		ImGui::EndMenu();
	}
	showAddComponentNode(node->next, filter);
}


void PropertyGrid::onGUI()
{
	auto& ents = m_editor.getSelectedEntities();
	if (ImGui::BeginDock("Properties", &m_is_open) && !ents.empty())
	{
		if (ImGui::Button("Add component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}
		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::FilterInput("Filter", m_component_filter, sizeof(m_component_filter));
			showAddComponentNode(m_app.getAddComponentTreeRoot().child, m_component_filter);
			ImGui::EndPopup();
		}

		showCoreProperties(ents);

		Universe& universe = *m_editor.getUniverse();
		for (ComponentUID cmp = universe.getFirstComponent(ents[0]); cmp.isValid();
			 cmp = universe.getNextComponent(cmp))
		{
			showComponentProperties(ents, cmp.type);
		}
	}
	ImGui::EndDock();
}


} // namespace Lumix
